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

CredentialStatus SecretServiceCredentialStore::search(const Attributes &attributes, QList<QDBusObjectPath> &items) const {
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("SearchItems"));
  call << QVariant::fromValue(attributes);
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return statusFor(QDBusError(reply));
  items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
  const auto locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
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
  auto alias = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("ReadAlias"));
  alias << QStringLiteral("default");
  const QDBusReply<QDBusObjectPath> collection = m_bus.call(alias);
  if (!collection.isValid()) return {statusFor(collection.error()), {}};
  if (collection.value().path() == kNoPrompt) return {CredentialStatus::Unavailable, {}};
  if (const auto status = unlock({collection.value()}); status != CredentialStatus::Ok) return {status, {}};

  const QVariantMap properties{{kItemInterface + QStringLiteral(".Label"), QStringLiteral("XerahS upload secret (%1)").arg(name)},
                               {kItemInterface + QStringLiteral(".Attributes"), QVariant::fromValue(attributesFor(destinationId, name))}};
  const SecretValue secret{m_session, {}, value.toUtf8(), QStringLiteral("text/plain; charset=utf8")};
  auto call = QDBusMessage::createMethodCall(m_service, collection.value().path(), kCollectionInterface, QStringLiteral("CreateItem"));
  call << properties << QVariant::fromValue(secret) << true;
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return {statusFor(QDBusError(reply)), {}};
  return {prompt(qdbus_cast<QDBusObjectPath>(reply.arguments().at(1))), {}};
}

CredentialResult SecretServiceCredentialStore::lookup(const QUuid &destinationId, const QString &name) const {
  if (destinationId.isNull() || name.isEmpty()) return {CredentialStatus::Denied, {}};
  if (const auto status = openSession(); status != CredentialStatus::Ok) return {status, {}};
  QList<QDBusObjectPath> items;
  if (const auto status = search(attributesFor(destinationId, name), items); status != CredentialStatus::Ok) return {status, {}};
  if (items.isEmpty()) return {CredentialStatus::NotFound, {}};
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("GetSecrets"));
  call << QVariant::fromValue(QList<QDBusObjectPath>{items.first()}) << QVariant::fromValue(m_session);
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1) return {statusFor(QDBusError(reply)), {}};
  const auto secrets = qdbus_cast<SecretMap>(reply.arguments().at(0));
  const auto found = secrets.constFind(items.first());
  // A locked item is left out of the reply instead of raising an error.
  if (found == secrets.cend()) return {CredentialStatus::Denied, {}};
  return {CredentialStatus::Ok, QString::fromUtf8(found->value)};
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
