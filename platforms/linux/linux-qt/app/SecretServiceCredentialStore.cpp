#include "SecretServiceCredentialStore.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusVariant>
#include <QEventLoop>
#include <QTimer>

namespace xerahs::app {

using uploads::CredentialResult;
using uploads::CredentialStatus;

namespace {
const QString kServicePath = QStringLiteral("/org/freedesktop/secrets");
const QString kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const QString kCollectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const QString kPromptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString kNoPrompt = QStringLiteral("/");

using Attributes = QMap<QString, QString>;
using SecretMap = QMap<QDBusObjectPath, SecretValue>;

void registerTypes() {
  static const bool registered = [] {
    qDBusRegisterMetaType<SecretValue>();
    qDBusRegisterMetaType<Attributes>();
    qDBusRegisterMetaType<SecretMap>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    return true;
  }();
  Q_UNUSED(registered);
}

// A missing or crashed service is Unavailable; a locked keyring or refused
// access is Denied. Error text is never returned, so no secret can leak through it.
CredentialStatus statusFor(const QDBusError &error) {
  if (error.name() == QStringLiteral("org.freedesktop.Secret.Error.IsLocked") || error.type() == QDBusError::AccessDenied)
    return CredentialStatus::Denied;
  return CredentialStatus::Unavailable;
}

// The service can restart while the application runs; the session object it
// handed out then no longer exists. These names mean the cached session is
// stale, not that the credential is missing or refused.
bool staleSessionError(const QString &name) {
  return name == QStringLiteral("org.freedesktop.Secret.Error.NoSession") ||
         name == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown") ||
         name == QStringLiteral("org.freedesktop.DBus.Error.UnknownObject");
}

// Owned secret bytes are wiped as soon as their last owned copy is consumed.
// QByteArray is copy-on-write: filling detaches, so only our own copy is touched.
void zeroSecretBytes(QByteArray *bytes) {
  if (!bytes->isEmpty()) bytes->fill('\0');
  bytes->clear();
}

Attributes attributesFor(const QUuid &destinationId, const QString &name) {
  Attributes attributes{{QStringLiteral("xdg:schema"), kSecretSchema},
                        {QStringLiteral("destination"), destinationId.toString(QUuid::WithoutBraces)}};
  if (!name.isNull()) attributes.insert(QStringLiteral("name"), name);
  return attributes;
}

class PromptWaiter final : public QObject {
  Q_OBJECT
public:
  bool done = false, dismissed = true;
  QDBusVariant result;
  QEventLoop loop;
public slots:
  void Completed(bool wasDismissed, const QDBusVariant &value) {
    done = true; dismissed = wasDismissed; result = value; loop.quit();
  }
};
}  // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const SecretValue &secret) {
  argument.beginStructure();
  argument << secret.session << secret.parameters << secret.value << secret.contentType;
  argument.endStructure();
  return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SecretValue &secret) {
  argument.beginStructure();
  argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
  argument.endStructure();
  return argument;
}

SecretServiceCredentialStore::SecretServiceCredentialStore(QDBusConnection bus, QString service, int promptTimeoutMs)
    : m_bus(std::move(bus)), m_service(std::move(service)), m_promptTimeoutMs(promptTimeoutMs) {
  registerTypes();
}

// The plain algorithm keeps the secret unencrypted only on the user's own
// session bus, as libsecret's fallback does; nothing crosses the network.
CredentialStatus SecretServiceCredentialStore::openSession() const {
  // A probe and an upload can prepare on separate owned workers. Publish the
  // shared immutable session once before either worker reads it.
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  if (!m_session.path().isEmpty()) return CredentialStatus::Ok;
  if (!m_bus.isConnected()) return CredentialStatus::Unavailable;
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("OpenSession"));
  call << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return statusFor(QDBusError(reply));
  m_session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
  return m_session.path().isEmpty() ? CredentialStatus::Unavailable : CredentialStatus::Ok;
}

CredentialStatus SecretServiceCredentialStore::prompt(const QDBusObjectPath &path, QDBusVariant *result) const {
  if (path.path() == kNoPrompt) return CredentialStatus::Ok;
  PromptWaiter waiter;
  if (!m_bus.connect(m_service, path.path(), kPromptInterface, QStringLiteral("Completed"), &waiter, SLOT(Completed(bool, QDBusVariant))))
    return CredentialStatus::Unavailable;
  auto call = QDBusMessage::createMethodCall(m_service, path.path(), kPromptInterface, QStringLiteral("Prompt"));
  call << QString();
  const auto reply = m_bus.call(call);
  CredentialStatus status = CredentialStatus::Ok;
  if (reply.type() == QDBusMessage::ErrorMessage) {
    status = statusFor(QDBusError(reply));
  } else {
    if (!waiter.done) {
      QTimer::singleShot(m_promptTimeoutMs, &waiter.loop, &QEventLoop::quit);
      waiter.loop.exec();
    }
    if (!waiter.done) {
      // Withdraw an unanswered prompt rather than leave it on screen.
      m_bus.call(QDBusMessage::createMethodCall(m_service, path.path(), kPromptInterface, QStringLiteral("Dismiss")));
      status = CredentialStatus::Denied;
    } else if (waiter.dismissed) {
      status = CredentialStatus::Denied;
    } else if (result) {
      *result = waiter.result;
    }
  }
  m_bus.disconnect(m_service, path.path(), kPromptInterface, QStringLiteral("Completed"), &waiter, SLOT(Completed(bool, QDBusVariant)));
  return status;
}

CredentialStatus SecretServiceCredentialStore::unlock(const QList<QDBusObjectPath> &objects) const {
  if (objects.isEmpty()) return CredentialStatus::Ok;
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("Unlock"));
  call << QVariant::fromValue(objects);
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return statusFor(QDBusError(reply));
  const auto unlocked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
  const auto promptPath = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
  if (promptPath.path() == kNoPrompt) return unlocked.size() == objects.size() ? CredentialStatus::Ok : CredentialStatus::Denied;
  return prompt(promptPath);
}

QDBusObjectPath SecretServiceCredentialStore::currentSession() const {
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  return m_session;
}

void SecretServiceCredentialStore::resetSession() const {
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  m_session = QDBusObjectPath();
}

// Item search is scoped to the default alias collection: items carry only
// ordinary attributes, so another application could plant look-alikes. Only
// the collection our own store() writes into is trusted (review fix A1).
CredentialStatus SecretServiceCredentialStore::defaultCollection(QDBusObjectPath *collection) const {
  auto alias = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("ReadAlias"));
  alias << QStringLiteral("default");
  const QDBusReply<QDBusObjectPath> reply = m_bus.call(alias);
  if (!reply.isValid()) return statusFor(reply.error());
  if (reply.value().path() == kNoPrompt) return CredentialStatus::Unavailable;
  *collection = reply.value();
  return CredentialStatus::Ok;
}

CredentialStatus SecretServiceCredentialStore::search(const Attributes &attributes, QList<QDBusObjectPath> &items, bool allowUnlock) const {
  QDBusObjectPath collection;
  if (const auto status = defaultCollection(&collection); status != CredentialStatus::Ok) return status;
  auto call = QDBusMessage::createMethodCall(m_service, collection.path(), kCollectionInterface, QStringLiteral("SearchItems"));
  call << QVariant::fromValue(attributes);
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return statusFor(QDBusError(reply));
  items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
  const auto locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
  if (!allowUnlock) return !items.isEmpty() || locked.isEmpty() ? CredentialStatus::Ok : CredentialStatus::Denied;
  if (const auto status = unlock(locked); status != CredentialStatus::Ok) return status;
  items += locked;
  return CredentialStatus::Ok;
}

CredentialStatus SecretServiceCredentialStore::deleteItems(const QList<QDBusObjectPath> &items) const {
  for (const auto &item : items) {
    const auto reply = m_bus.call(QDBusMessage::createMethodCall(m_service, item.path(), kItemInterface, QStringLiteral("Delete")));
    if (reply.type() != QDBusMessage::ReplyMessage) return statusFor(QDBusError(reply));
    const auto promptPath = reply.arguments().isEmpty() ? QDBusObjectPath(kNoPrompt) : qdbus_cast<QDBusObjectPath>(reply.arguments().at(0));
    if (const auto status = prompt(promptPath); status != CredentialStatus::Ok) return status;
  }
  return CredentialStatus::Ok;
}

CredentialResult SecretServiceCredentialStore::store(const QUuid &destinationId, const QString &name, const QString &value) {
  if (destinationId.isNull() || name.isEmpty()) return {CredentialStatus::Denied, {}};
  if (const auto status = openSession(); status != CredentialStatus::Ok) return {status, {}};
  QDBusObjectPath collection;
  if (const auto status = defaultCollection(&collection); status != CredentialStatus::Ok) return {status, {}};
  if (const auto status = unlock({collection}); status != CredentialStatus::Ok) return {status, {}};

  const QVariantMap properties{{kItemInterface + QStringLiteral(".Label"), QStringLiteral("XerahS upload secret (%1)").arg(name)},
                               {kItemInterface + QStringLiteral(".Attributes"), QVariant::fromValue(attributesFor(destinationId, name))}};
  QByteArray plain = value.toUtf8();
  SecretValue secret{currentSession(), {}, plain, QStringLiteral("text/plain; charset=utf8")};
  auto call = QDBusMessage::createMethodCall(m_service, collection.path(), kCollectionInterface, QStringLiteral("CreateItem"));
  call << properties << QVariant::fromValue(secret) << true;
  auto reply = m_bus.call(call);
  // A restarted service no longer knows the session; reopen once and retry
  // instead of failing the save (review fix A2). One retry, never a loop.
  if (reply.type() == QDBusMessage::ErrorMessage && staleSessionError(QDBusError(reply).name())) {
    resetSession();
    if (openSession() == CredentialStatus::Ok) {
      secret.session = currentSession();
      call.setArguments({QVariant::fromValue(properties), QVariant::fromValue(secret), QVariant(true)});
      reply = m_bus.call(call);
    }
  }
  // The secret leaves only through the D-Bus call; wipe our owned copies
  // (review fix A4). plain and secret.value may share one buffer until the
  // first fill detaches, so both are wiped in order.
  zeroSecretBytes(&secret.value);
  zeroSecretBytes(&plain);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return {statusFor(QDBusError(reply)), {}};
  return {prompt(qdbus_cast<QDBusObjectPath>(reply.arguments().at(1))), {}};
}

CredentialStatus SecretServiceCredentialStore::availability(const QUuid &destinationId, const QString &name) const {
  if (destinationId.isNull() || name.isEmpty()) return CredentialStatus::Denied;
  QList<QDBusObjectPath> items;
  const auto status = search(attributesFor(destinationId, name), items, false);
  if (status != CredentialStatus::Ok) return status;
  if (items.isEmpty()) return CredentialStatus::NotFound;
  // Several matching items in the trusted collection are ambiguous; no guess.
  if (items.size() > 1) return CredentialStatus::Unavailable;
  return CredentialStatus::Ok;
}
CredentialResult SecretServiceCredentialStore::lookup(const QUuid &destinationId, const QString &name) const { return readSecret(destinationId, name, true); }
CredentialResult SecretServiceCredentialStore::lookupWithoutPrompt(const QUuid &destinationId, const QString &name) const { return readSecret(destinationId, name, false); }
CredentialResult SecretServiceCredentialStore::readSecret(const QUuid &destinationId, const QString &name, bool allowUnlock) const {
  if (destinationId.isNull() || name.isEmpty()) return {CredentialStatus::Denied, {}};
  if (const auto status = openSession(); status != CredentialStatus::Ok) return {status, {}};
  QList<QDBusObjectPath> items;
  if (const auto status = search(attributesFor(destinationId, name), items, allowUnlock); status != CredentialStatus::Ok) return {status, {}};
  if (items.isEmpty()) return {CredentialStatus::NotFound, {}};
  // Several matching items inside the trusted collection cannot be told
  // apart; refuse instead of returning whichever the service listed first.
  if (items.size() > 1) return {CredentialStatus::Unavailable, {}};
  return readItemSecret(items.first());
}

CredentialResult SecretServiceCredentialStore::readItemSecret(const QDBusObjectPath &item) const {
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("GetSecrets"));
  call << QVariant::fromValue(QList<QDBusObjectPath>{item}) << QVariant::fromValue(currentSession());
  auto reply = m_bus.call(call);
  // A restarted service no longer knows the session; reopen once and retry
  // instead of failing the read (review fix A2). One retry, never a loop.
  if (reply.type() == QDBusMessage::ErrorMessage && staleSessionError(QDBusError(reply).name())) {
    resetSession();
    if (openSession() == CredentialStatus::Ok) {
      call.setArguments({QVariant::fromValue(QList<QDBusObjectPath>{item}), QVariant::fromValue(currentSession())});
      reply = m_bus.call(call);
    }
  }
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1) return {statusFor(QDBusError(reply)), {}};
  SecretMap secrets = qdbus_cast<SecretMap>(reply.arguments().at(0));
  const auto found = secrets.constFind(item);
  // A locked item is left out of the reply instead of raising an error.
  if (found == secrets.cend()) {
    for (auto it = secrets.begin(); it != secrets.end(); ++it) zeroSecretBytes(&it.value().value);
    return {CredentialStatus::Denied, {}};
  }
  const QString value = QString::fromUtf8(found->value);
  // The reply bytes are ours alone now; wipe them before they go out of scope
  // (review fix A4).
  for (auto it = secrets.begin(); it != secrets.end(); ++it) zeroSecretBytes(&it.value().value);
  return {CredentialStatus::Ok, value};
}

CredentialResult SecretServiceCredentialStore::remove(const QUuid &destinationId, const QString &name) {
  if (destinationId.isNull() || name.isEmpty()) return {CredentialStatus::Denied, {}};
  QList<QDBusObjectPath> items;
  if (const auto status = search(attributesFor(destinationId, name), items); status != CredentialStatus::Ok) return {status, {}};
  if (items.isEmpty()) return {CredentialStatus::NotFound, {}};
  return {deleteItems(items), {}};
}

CredentialResult SecretServiceCredentialStore::removeAll(const QUuid &destinationId) {
  if (destinationId.isNull()) return {CredentialStatus::Denied, {}};
  QList<QDBusObjectPath> items;
  if (const auto status = search(attributesFor(destinationId, {}), items); status != CredentialStatus::Ok) return {status, {}};
  return {deleteItems(items), {}};
}

}  // namespace xerahs::app

#include "SecretServiceCredentialStore.moc"
