// CORE-013: the Secret Service store against a fake org.freedesktop.secrets
// on a private bus. The fake runs on its own thread so blocking calls return.
#include "SecretServiceCredentialStore.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QTest>
#include <QThread>

using namespace xerahs::app;
using xerahs::uploads::CredentialStatus;
using Attributes = QMap<QString, QString>;
using SecretMap = QMap<QDBusObjectPath, SecretValue>;

namespace {
const QString kRoot = QStringLiteral("/org/freedesktop/secrets");
const QString kCollection = QStringLiteral("/org/freedesktop/secrets/collection/login");

struct FakeItem {
  Attributes attributes;
  QString label;
  QByteArray value;
  bool locked = false;
};

class FakeSecretService final : public QDBusVirtualObject {
public:
  enum class PromptReply { Accept, Dismiss, Ignore };
  QMap<QString, FakeItem> items;
  bool collectionLocked = false, noDefault = false;
  PromptReply promptReply = PromptReply::Accept;
  int prompts = 0, dismissCalls = 0, nextItem = 1;
  QString session;
  QStringList pendingUnlock;

  QString introspect(const QString &) const override { return {}; }

  bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
    const auto member = message.member();
    const auto args = message.arguments();
    const auto reply = [&](const QVariantList &values) { connection.send(message.createReply(values)); return true; };
    const auto error = [&](const QString &name) { connection.send(message.createErrorReply(name, QStringLiteral("fake"))); return true; };
    if (member == "OpenSession") {
      if (args.value(0).toString() != "plain") return error("org.freedesktop.DBus.Error.NotSupported");
      session = kRoot + "/session/1";
      return reply({QVariant::fromValue(QDBusVariant(QString())), QVariant::fromValue(QDBusObjectPath(session))});
    }
    if (member == "ReadAlias") return reply({QVariant::fromValue(QDBusObjectPath(noDefault ? "/" : kCollection))});
    if (member == "SearchItems") {
      const auto wanted = qdbus_cast<Attributes>(args.value(0));
      QList<QDBusObjectPath> unlocked, locked;
      for (auto it = items.cbegin(); it != items.cend(); ++it) {
        bool match = true;
        for (auto w = wanted.cbegin(); w != wanted.cend(); ++w) match = match && it->attributes.value(w.key()) == w.value();
        if (match) (it->locked ? locked : unlocked).append(QDBusObjectPath(it.key()));
      }
      return reply({QVariant::fromValue(unlocked), QVariant::fromValue(locked)});
    }
    if (member == "Unlock") {
      const auto objects = qdbus_cast<QList<QDBusObjectPath>>(args.value(0));
      bool anyLocked = false;
      for (const auto &object : objects) anyLocked = anyLocked || (object.path() == kCollection ? collectionLocked : items.value(object.path()).locked);
      if (!anyLocked) return reply({QVariant::fromValue(objects), QVariant::fromValue(QDBusObjectPath("/"))});
      pendingUnlock.clear();
      for (const auto &object : objects) pendingUnlock << object.path();
      return reply({QVariant::fromValue(QList<QDBusObjectPath>{}), QVariant::fromValue(QDBusObjectPath(kRoot + "/prompt/1"))});
    }
    if (member == "Prompt") {
      ++prompts;
      reply(QVariantList{});
      if (promptReply == PromptReply::Ignore) return true;
      const bool dismissed = promptReply == PromptReply::Dismiss;
      QList<QDBusObjectPath> unlocked;
      if (!dismissed) {
        for (const auto &path : pendingUnlock) {
          if (path == kCollection) collectionLocked = false;
          else items[path].locked = false;
          unlocked << QDBusObjectPath(path);
        }
      }
      auto signal = QDBusMessage::createSignal(message.path(), QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Completed"));
      signal << dismissed << QVariant::fromValue(QDBusVariant(QVariant::fromValue(unlocked)));
      connection.send(signal);
      return true;
    }
    if (member == "Dismiss") { ++dismissCalls; return reply(QVariantList{}); }
    if (member == "GetSecrets") {
      if (qdbus_cast<QDBusObjectPath>(args.value(1)).path() != session) return error("org.freedesktop.Secret.Error.NoSession");
      SecretMap secrets;
      for (const auto &path : qdbus_cast<QList<QDBusObjectPath>>(args.value(0))) {
        const auto found = items.constFind(path.path());
        if (found != items.cend() && !found->locked) secrets.insert(path, {QDBusObjectPath(session), {}, found->value, "text/plain"});
      }
      return reply({QVariant::fromValue(secrets)});
    }
    if (member == "CreateItem") {
      if (collectionLocked) return error("org.freedesktop.Secret.Error.IsLocked");
      const auto properties = qdbus_cast<QVariantMap>(args.value(0));
      const auto attributes = qdbus_cast<Attributes>(properties.value("org.freedesktop.Secret.Item.Attributes"));
      const auto secret = qdbus_cast<SecretValue>(args.value(1));
      if (secret.session.path() != session) return error("org.freedesktop.Secret.Error.NoSession");
      QString path;
      if (args.value(2).toBool())
        for (auto it = items.cbegin(); it != items.cend(); ++it) if (it->attributes == attributes) path = it.key();
      if (path.isEmpty()) path = kCollection + "/" + QString::number(nextItem++);
      items[path] = FakeItem{attributes, properties.value("org.freedesktop.Secret.Item.Label").toString(), secret.value, false};
      return reply({QVariant::fromValue(QDBusObjectPath(path)), QVariant::fromValue(QDBusObjectPath("/"))});
    }
    if (member == "Delete") {
      if (!items.remove(message.path())) return error("org.freedesktop.Secret.Error.NoSuchObject");
      return reply({QVariant::fromValue(QDBusObjectPath("/"))});
    }
    return false;
  }
};
}  // namespace

class SecretServiceCredentialStoreTest : public QObject {
  Q_OBJECT
  const QString m_service = QStringLiteral("org.xerahs.TestSecrets");
  QDBusConnection m_server{QString()}, m_client{QString()};
  QThread m_thread;
  FakeSecretService *m_fake = nullptr;
  const QUuid m_destination = QUuid::fromString(QStringLiteral("{4a1d6a2e-1111-4c4c-9a9a-000000000001}"));
  const QUuid m_other = QUuid::fromString(QStringLiteral("{4a1d6a2e-1111-4c4c-9a9a-000000000002}"));

  SecretServiceCredentialStore store(int promptTimeoutMs = 2000) { return SecretServiceCredentialStore(m_client, m_service, promptTimeoutMs); }
  void addLockedItem(const QString &name, const QByteArray &value) {
    m_fake->items[kCollection + "/locked"] = FakeItem{{{"xdg:schema", kSecretSchema}, {"destination", m_destination.toString(QUuid::WithoutBraces)}, {"name", name}}, "x", value, true};
  }

private slots:
  void init() {
    qDBusRegisterMetaType<SecretValue>(); qDBusRegisterMetaType<Attributes>(); qDBusRegisterMetaType<SecretMap>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    m_server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "secret-test-server");
    m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "secret-test-client");
    QVERIFY(m_server.isConnected() && m_client.isConnected());
    m_fake = new FakeSecretService;
    m_fake->moveToThread(&m_thread);
    m_thread.start();
    QVERIFY(m_server.registerService(m_service));
    QVERIFY(m_server.registerVirtualObject(kRoot, m_fake, QDBusConnection::SubPath));
  }
  void cleanup() {
    m_server.unregisterObject(kRoot); m_server.unregisterService(m_service);
    m_thread.quit(); m_thread.wait();
    delete m_fake; m_fake = nullptr;
    QDBusConnection::disconnectFromBus("secret-test-client"); QDBusConnection::disconnectFromBus("secret-test-server");
  }

  void storesLooksUpReplacesAndRemoves() {
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "api-key", "s3cr3t").ok());
    QCOMPARE(m_fake->items.size(), 1);
    const auto item = m_fake->items.first();
    QCOMPARE(item.attributes.value("xdg:schema"), kSecretSchema);
    QCOMPARE(item.attributes.value("destination"), m_destination.toString(QUuid::WithoutBraces));
    QCOMPARE(item.attributes.value("name"), QStringLiteral("api-key"));
    QVERIFY(!item.label.contains("s3cr3t"));
    QCOMPARE(secrets.lookup(m_destination, "api-key").value, QStringLiteral("s3cr3t"));

    QVERIFY(secrets.store(m_destination, "api-key", "rotated").ok());
    QCOMPARE(m_fake->items.size(), 1);
    QCOMPARE(secrets.lookup(m_destination, "api-key").value, QStringLiteral("rotated"));

    QVERIFY(secrets.remove(m_destination, "api-key").ok());
    QCOMPARE(secrets.lookup(m_destination, "api-key").status, CredentialStatus::NotFound);
    QCOMPARE(secrets.remove(m_destination, "api-key").status, CredentialStatus::NotFound);
  }

  void keepsDestinationsAndNamesSeparate() {
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "token", "a").ok());
    QVERIFY(secrets.store(m_destination, "user", "b").ok());
    QVERIFY(secrets.store(m_other, "token", "c").ok());
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("a"));
    QCOMPARE(secrets.lookup(m_other, "token").value, QStringLiteral("c"));
    QCOMPARE(secrets.lookup(m_other, "user").status, CredentialStatus::NotFound);

    QVERIFY(secrets.removeAll(m_destination).ok());
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::NotFound);
    QCOMPARE(secrets.lookup(m_destination, "user").status, CredentialStatus::NotFound);
    QCOMPARE(secrets.lookup(m_other, "token").value, QStringLiteral("c"));
    QVERIFY(secrets.removeAll(m_destination).ok());
  }

  void unlocksLockedItemThroughAcceptedPrompt() {
    addLockedItem("token", "unlocked-value");
    auto secrets = store();
    const auto result = secrets.lookup(m_destination, "token");
    QCOMPARE(result.status, CredentialStatus::Ok);
    QCOMPARE(result.value, QStringLiteral("unlocked-value"));
    QCOMPARE(m_fake->prompts, 1);
  }

  void dismissedPromptIsDenied() {
    addLockedItem("token", "hidden");
    m_fake->promptReply = FakeSecretService::PromptReply::Dismiss;
    auto secrets = store();
    const auto result = secrets.lookup(m_destination, "token");
    QCOMPARE(result.status, CredentialStatus::Denied);
    QVERIFY(result.value.isEmpty());
  }

  void unansweredPromptTimesOutAndIsWithdrawn() {
    addLockedItem("token", "hidden");
    m_fake->promptReply = FakeSecretService::PromptReply::Ignore;
    auto secrets = store(200);
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::Denied);
    QCOMPARE(m_fake->dismissCalls, 1);
  }

  void unlocksLockedCollectionBeforeStoring() {
    m_fake->collectionLocked = true;
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "token", "v").ok());
    QCOMPARE(m_fake->prompts, 1);
    m_fake->collectionLocked = true;
    m_fake->promptReply = FakeSecretService::PromptReply::Dismiss;
    QCOMPARE(secrets.store(m_destination, "token", "w").status, CredentialStatus::Denied);
  }

  void missingServiceOrCollectionIsUnavailable() {
    SecretServiceCredentialStore absent(m_client, QStringLiteral("org.xerahs.NoSuchSecrets"), 200);
    QCOMPARE(absent.lookup(m_destination, "token").status, CredentialStatus::Unavailable);
    QCOMPARE(absent.store(m_destination, "token", "v").status, CredentialStatus::Unavailable);
    m_fake->noDefault = true;
    QCOMPARE(store().store(m_destination, "token", "v").status, CredentialStatus::Unavailable);
    QVERIFY(m_fake->items.isEmpty());
  }

  void rejectsEmptyIdentifiersWithoutCallingTheService() {
    auto secrets = store();
    QCOMPARE(secrets.store(QUuid(), "token", "v").status, CredentialStatus::Denied);
    QCOMPARE(secrets.store(m_destination, "", "v").status, CredentialStatus::Denied);
    QCOMPARE(secrets.lookup(m_destination, "").status, CredentialStatus::Denied);
    QCOMPARE(secrets.removeAll(QUuid()).status, CredentialStatus::Denied);
    QVERIFY(m_fake->session.isEmpty());
  }
};

QTEST_GUILESS_MAIN(SecretServiceCredentialStoreTest)
#include "SecretServiceCredentialStoreTest.moc"
