// CORE-013: the Secret Service store against a fake org.freedesktop.secrets
// on a private bus. The fake runs on its own thread so blocking calls return.
#include "SecretServiceCredentialStore.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QTest>
#include <QThread>
#include <thread>

using namespace xerahs::app;
using xerahs::uploads::CredentialStatus;
using Attributes = QMap<QString, QString>;
using SecretMap = QMap<QDBusObjectPath, SecretValue>;

#ifdef XERAHS_GCRYPT
#include <gcrypt.h>
// Server side of dh-ietf1024-sha256-aes128-cbc-pkcs7, implemented
// independently from the store so the fake proves the two sides of the
// exchange agree byte for byte. Same published group and derivation.
namespace {
const unsigned char kFakePrime[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, 0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool fakeCryptoReady() {
  static const bool ready = gcry_check_version(nullptr) != nullptr;
  return ready;
}
gcry_mpi_t fakeMpi(const QByteArray &bytes) {
  gcry_mpi_t mpi = nullptr;
  if (bytes.isEmpty() || gcry_mpi_scan(&mpi, GCRYMPI_FMT_USG, bytes.constData(), size_t(bytes.size()), nullptr) != 0) return nullptr;
  return mpi;
}
QByteArray fakeMpiBytes(gcry_mpi_t mpi) {
  unsigned char *buffer = nullptr;
  size_t size = 0;
  if (gcry_mpi_aprint(GCRYMPI_FMT_USG, &buffer, &size, mpi) != 0) return {};
  QByteArray bytes(reinterpret_cast<const char *>(buffer), int(size));
  gcry_free(buffer);
  return bytes;
}
// The fake's one static server pair; every negotiated session derives with
// this pair's private value, matching the public key it advertises.
struct FakeServerPair {
  gcry_mpi_t prime = nullptr, priv = nullptr, pub = nullptr;
  bool initialized = false;
  FakeServerPair() {
    if (gcry_mpi_scan(&prime, GCRYMPI_FMT_USG, kFakePrime, sizeof(kFakePrime), nullptr) != 0) return;
    priv = gcry_mpi_snew(1024);
    do {
      gcry_mpi_randomize(priv, 1024, GCRY_STRONG_RANDOM);
    } while (gcry_mpi_cmp_ui(priv, 0) == 0);
    if (gcry_mpi_get_nbits(priv) > 1023) gcry_mpi_clear_highbit(priv, 1023);
    pub = gcry_mpi_new(1024);
    gcry_mpi_t base = gcry_mpi_set_ui(nullptr, 2);
    if (!base) return;
    gcry_mpi_powm(pub, base, priv, prime);
    gcry_mpi_release(base);
    initialized = true;
  }
  ~FakeServerPair() {
    gcry_mpi_release(prime); gcry_mpi_release(priv); gcry_mpi_release(pub);
  }
};
FakeServerPair &fakePair() {
  static FakeServerPair pair;
  return pair;
}
QByteArray fakeServerPublic() { return fakeMpiBytes(fakePair().pub); }

QByteArray fakeSharedKey(const QByteArray &clientPublic) {
  auto &pair = fakePair();
  gcry_mpi_t peer = fakeMpi(clientPublic);
  QByteArray key;
  if (!peer || !pair.initialized) {
    gcry_mpi_release(peer);
    return {};
  }
  gcry_mpi_t shared = gcry_mpi_snew(1024);
  gcry_mpi_powm(shared, peer, pair.priv, pair.prime);
  // Zero-left-padded shared secret, then RFC 5869 HKDF-SHA256 (salt and info
  // empty) to the 16-byte AES key.
  auto ikm = fakeMpiBytes(shared);
  if (ikm.size() > int(sizeof(kFakePrime))) ikm.clear();
  else ikm = QByteArray(int(sizeof(kFakePrime)) - ikm.size(), '\0') + ikm;
  gcry_md_hd_t extract = nullptr, expand = nullptr;
  QByteArray salt(32, '\0');
  if (gcry_md_open(&extract, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC) == 0 &&
      gcry_md_setkey(extract, salt.constData(), 32) == 0) {
    gcry_md_write(extract, ikm.constData(), size_t(ikm.size()));
    const auto *prk = gcry_md_read(extract, GCRY_MD_SHA256);
    if (gcry_md_open(&expand, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC) == 0 &&
        gcry_md_setkey(expand, prk, 32) == 0) {
      gcry_md_write(expand, "\x01", 1);
      const auto *block = gcry_md_read(expand, GCRY_MD_SHA256);
      key = QByteArray(reinterpret_cast<const char *>(block), 16);
      gcry_md_close(expand);
    }
    gcry_md_close(extract);
  }
  gcry_mpi_release(peer); gcry_mpi_release(shared);
  return key;
}
bool fakeAes(const QByteArray &key, const QByteArray &iv, QByteArray *data, bool encrypt) {
  if (key.size() != 16 || iv.size() != 16 || data->isEmpty() || data->size() % 16 != 0) return false;
  gcry_cipher_hd_t handle = nullptr;
  if (gcry_cipher_open(&handle, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CBC, 0) != 0) return false;
  const bool ok = gcry_cipher_setkey(handle, key.constData(), 16) == 0 &&
                  gcry_cipher_setiv(handle, iv.constData(), 16) == 0 &&
                  (encrypt ? gcry_cipher_encrypt(handle, data->data(), data->size(), data->constData(), data->size())
                           : gcry_cipher_decrypt(handle, data->data(), data->size(), data->constData(), data->size())) == 0;
  gcry_cipher_close(handle);
  return ok;
}
QByteArray fakeEncrypt(const QByteArray &key, const QByteArray &plain, QByteArray *iv) {
  const int pad = 16 - (plain.size() % 16);
  QByteArray data = plain + QByteArray(pad, char(pad));
  *iv = QByteArray(16, '\0');
  gcry_randomize(iv->data(), 16, GCRY_STRONG_RANDOM);
  return fakeAes(key, *iv, &data, true) ? data : QByteArray();
}
bool fakeDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &cipher, QByteArray *plain) {
  QByteArray data = cipher;
  if (!fakeAes(key, iv, &data, false)) return false;
  const int pad = static_cast<unsigned char>(data.at(data.size() - 1));
  if (pad < 1 || pad > 16 || pad > data.size()) return false;
  for (int i = data.size() - pad; i < data.size(); ++i)
    if (static_cast<unsigned char>(data.at(i)) != pad) return false;
  data.chop(pad);
  *plain = data;
  return true;
}
}  // namespace
#endif

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
  int unlockCalls = 0, secretReads = 0, sessionCalls = 0;
  QString session;
  QStringList pendingUnlock;
  // When set, the next GetSecrets fails with this error name once.
  QString secretsErrorOnce;
  // When set, the next OpenSession fails with this error name once.
  QString openSessionErrorOnce;
  // Negotiation observability: which algorithm the store asked for, and the
  // raw bytes its last CreateItem carried on the bus.
  QString sessionAlgorithm;
  QByteArray lastIncomingRaw;
  bool rejectDhSession = false, corruptNextSecret = false;
#ifdef XERAHS_GCRYPT
  // The derived session key once an encrypted session is negotiated.
  QByteArray sessionKey;
#endif

  QString introspect(const QString &) const override { return {}; }

  bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
    const auto member = message.member();
    const auto args = message.arguments();
    const auto reply = [&](const QVariantList &values) { connection.send(message.createReply(values)); return true; };
    const auto error = [&](const QString &name) { connection.send(message.createErrorReply(name, QStringLiteral("fake"))); return true; };
    if (member == "OpenSession") {
      ++sessionCalls;
      if (!openSessionErrorOnce.isEmpty()) { const auto name = openSessionErrorOnce; openSessionErrorOnce.clear(); return error(name); }
      const auto algorithm = args.value(0).toString();
#ifdef XERAHS_GCRYPT
      if (algorithm == QLatin1String("dh-ietf1024-sha256-aes128-cbc-pkcs7") && fakeCryptoReady()) {
        if (rejectDhSession) return error("org.freedesktop.DBus.Error.NotSupported");
        QVariant input = args.value(1);
        if (input.canConvert<QDBusVariant>()) input = input.value<QDBusVariant>().variant();
        sessionKey = fakeSharedKey(input.toByteArray());
        if (sessionKey.size() != 16) return error("org.freedesktop.Secret.Error.NoSession");
        session = kRoot + "/session/1";
        sessionAlgorithm = algorithm;
        return reply({QVariant::fromValue(QDBusVariant(fakeServerPublic())), QVariant::fromValue(QDBusObjectPath(session))});
      }
#endif
      if (algorithm != "plain") return error("org.freedesktop.DBus.Error.NotSupported");
      session = kRoot + "/session/1";
      sessionAlgorithm = QStringLiteral("plain");
#ifdef XERAHS_GCRYPT
      sessionKey.clear();
#endif
      return reply({QVariant::fromValue(QDBusVariant(QString())), QVariant::fromValue(QDBusObjectPath(session))});
    }
    if (member == "ReadAlias") return reply({QVariant::fromValue(QDBusObjectPath(noDefault ? "/" : kCollection))});
    if (member == "SearchItems") {
      // Collection-scoped search: only items under the called collection match.
      const auto wanted = qdbus_cast<Attributes>(args.value(0));
      const QString collection = message.path() + QLatin1Char('/');
      QList<QDBusObjectPath> unlocked, locked;
      for (auto it = items.cbegin(); it != items.cend(); ++it) {
        if (!it.key().startsWith(collection)) continue;
        bool match = true;
        for (auto w = wanted.cbegin(); w != wanted.cend(); ++w) match = match && it->attributes.value(w.key()) == w.value();
        if (match) (it->locked ? locked : unlocked).append(QDBusObjectPath(it.key()));
      }
      return reply({QVariant::fromValue(unlocked), QVariant::fromValue(locked)});
    }
    if (member == "Unlock") {
      ++unlockCalls;
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
      ++secretReads;
      if (!secretsErrorOnce.isEmpty()) { const auto name = secretsErrorOnce; secretsErrorOnce.clear(); return error(name); }
      if (qdbus_cast<QDBusObjectPath>(args.value(1)).path() != session) return error("org.freedesktop.Secret.Error.NoSession");
      SecretMap secrets;
      for (const auto &path : qdbus_cast<QList<QDBusObjectPath>>(args.value(0))) {
        const auto found = items.constFind(path.path());
        if (found != items.cend() && !found->locked) {
#ifdef XERAHS_GCRYPT
          if (!sessionKey.isEmpty()) {
            QByteArray iv, cipher = fakeEncrypt(sessionKey, found->value, &iv);
            if (cipher.isEmpty()) continue;
            secrets.insert(path, {QDBusObjectPath(session), iv, cipher, "text/plain"});
            continue;
          }
#endif
          secrets.insert(path, {QDBusObjectPath(session), {}, found->value, "text/plain"});
        }
      }
      // A corrupted ciphertext must be refused whole by the store, never
      // partially decoded.
      if (corruptNextSecret && !secrets.isEmpty()) {
        auto first = secrets.begin();
        auto value = first.value().value;
        if (!value.isEmpty()) value[value.size() - 1] = char(quint8(value.at(value.size() - 1)) ^ 0x5A);
        first.value().value = value;
        corruptNextSecret = false;
      }
      return reply({QVariant::fromValue(secrets)});
    }
    if (member == "CreateItem") {
      if (collectionLocked) return error("org.freedesktop.Secret.Error.IsLocked");
      const auto properties = qdbus_cast<QVariantMap>(args.value(0));
      const auto attributes = qdbus_cast<Attributes>(properties.value("org.freedesktop.Secret.Item.Attributes"));
      const auto secret = qdbus_cast<SecretValue>(args.value(1));
      if (secret.session.path() != session) return error("org.freedesktop.Secret.Error.NoSession");
      lastIncomingRaw = secret.value;
      QByteArray value = secret.value;
#ifdef XERAHS_GCRYPT
      if (!sessionKey.isEmpty()) {
        value.clear();
        if (!fakeDecrypt(sessionKey, secret.parameters, secret.value, &value)) return error("org.freedesktop.Secret.Error.InvalidSecret");
      }
#endif
      QString path;
      if (args.value(2).toBool())
        for (auto it = items.cbegin(); it != items.cend(); ++it) if (it->attributes == attributes) path = it.key();
      if (path.isEmpty()) path = message.path() + "/" + QString::number(nextItem++);
      items[path] = FakeItem{attributes, properties.value("org.freedesktop.Secret.Item.Label").toString(), value, false};
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
  // Plants a look-alike item under an arbitrary collection path.
  void plant(const QString &path, const QString &name, const QString &value) {
    m_fake->items[path] = FakeItem{{{"xdg:schema", kSecretSchema}, {"destination", m_destination.toString(QUuid::WithoutBraces)}, {"name", name}}, QStringLiteral("planted"), value.toUtf8(), false};
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

  void availabilityAndProbeReadsNeverUnlockOrPrompt() {
    addLockedItem("token", "scoped-probe-value"); auto secrets = store();
    QCOMPARE(secrets.availability(m_destination,"token"),CredentialStatus::Denied);
    QCOMPARE(m_fake->unlockCalls,0); QCOMPARE(m_fake->prompts,0); QCOMPARE(m_fake->secretReads,0); QCOMPARE(m_fake->sessionCalls,0);
    QCOMPARE(secrets.lookupWithoutPrompt(m_destination,"token").status,CredentialStatus::Denied);
    QCOMPARE(m_fake->unlockCalls,0); QCOMPARE(m_fake->prompts,0); QCOMPARE(m_fake->secretReads,0);
    m_fake->items.begin().value().locked=false;
    QCOMPARE(secrets.availability(m_destination,"token"),CredentialStatus::Ok); QCOMPARE(m_fake->secretReads,0);
    QCOMPARE(secrets.lookupWithoutPrompt(m_destination,"token").value,QString("scoped-probe-value"));
    QCOMPARE(m_fake->secretReads,1); QCOMPARE(m_fake->unlockCalls,0); QCOMPARE(m_fake->prompts,0);
    QCOMPARE(secrets.lookupWithoutPrompt(m_other,"token").status,CredentialStatus::NotFound);
    QCOMPARE(m_fake->secretReads,1);
  }
  void concurrentProbeReadsShareOneSessionWithoutUnlocking() {
    addLockedItem("token","scoped-probe-value"); m_fake->items.begin().value().locked=false;
    auto secrets=store(); xerahs::uploads::CredentialResult first, second;
    std::thread one([&] { first=secrets.lookupWithoutPrompt(m_destination,"token"); });
    std::thread two([&] { second=secrets.lookupWithoutPrompt(m_destination,"token"); });
    one.join(); two.join();
    QVERIFY(first.ok()); QVERIFY(second.ok()); QCOMPARE(first.value,second.value);
    QCOMPARE(m_fake->sessionCalls,1); QCOMPARE(m_fake->unlockCalls,0); QCOMPARE(m_fake->prompts,0);
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

  void plantedLookAlikesInOtherCollectionsAreInvisible() {
    // Review fix A1: search is scoped to the default alias collection, so an
    // item another application planted with our attributes outside that
    // collection is never read, and it is left untouched.
    const QString other = QStringLiteral("/org/freedesktop/secrets/collection/other");
    plant(other + "/1", "token", QStringLiteral("impostor"));
    auto secrets = store();
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::NotFound);
    QCOMPARE(secrets.availability(m_destination, "token"), CredentialStatus::NotFound);
    QVERIFY(secrets.remove(m_destination, "token").status == CredentialStatus::NotFound);
    QVERIFY(m_fake->items.contains(other + "/1"));
    // The same item inside the default collection is the one that is read.
    plant(kCollection + "/2", "token", QStringLiteral("real"));
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("real"));
  }

  void duplicateMatchesInTheDefaultCollectionAreRefused() {
    // Two items with identical attributes inside the trusted collection are
    // ambiguous: reads and probes refuse instead of guessing; removal clears
    // both so the store becomes usable again.
    plant(kCollection + "/1", "token", QStringLiteral("one"));
    plant(kCollection + "/2", "token", QStringLiteral("two"));
    auto secrets = store();
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::Unavailable);
    QVERIFY(secrets.lookup(m_destination, "token").value.isEmpty());
    QCOMPARE(secrets.availability(m_destination, "token"), CredentialStatus::Unavailable);
    QVERIFY(secrets.remove(m_destination, "token").ok());
    QVERIFY(m_fake->items.isEmpty());
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::NotFound);
  }

  void serviceRestartDropsTheSessionAndTheStoreRecoversOnce() {
    // Review fix A2: after a service restart the old session object is gone.
    // The store clears it under the session mutex, negotiates a fresh session
    // and retries the read once; later calls reuse the recovered session.
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "token", "v").ok());
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("v"));
    QCOMPARE(m_fake->sessionCalls, 1);
    m_fake->session = kRoot + "/session/2";
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("v"));
    QCOMPARE(m_fake->sessionCalls, 2);
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("v"));
    QCOMPARE(m_fake->sessionCalls, 2);
    // Storing recovers the same way.
    m_fake->session = kRoot + "/session/3";
    QVERIFY(secrets.store(m_destination, "token", "rotated").ok());
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("rotated"));
  }

  void staleSessionIsRetriedOnceAndNeverLoops() {
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "token", "v").ok());
    // A scripted UnknownObject once: the store recovers transparently.
    m_fake->secretsErrorOnce = QStringLiteral("org.freedesktop.DBus.Error.UnknownObject");
    QCOMPARE(secrets.lookup(m_destination, "token").value, QStringLiteral("v"));
    QVERIFY(m_fake->secretsErrorOnce.isEmpty());
    // A stale session whose reopen also fails is terminal after exactly one
    // retry: the store reports unavailable instead of retrying in a loop.
    m_fake->session = kRoot + "/session/9";
    m_fake->openSessionErrorOnce = QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown");
    const int sessionCalls = m_fake->sessionCalls;
    QCOMPARE(secrets.lookup(m_destination, "token").status, CredentialStatus::Unavailable);
    QVERIFY(m_fake->openSessionErrorOnce.isEmpty());
    QCOMPARE(m_fake->sessionCalls, sessionCalls + 1);
  }

  void negotiatesEncryptedSessionFirst() {
#ifdef XERAHS_GCRYPT
    // Review fix A3: the store asks for the encrypted algorithm before any
    // secret moves, and the two independent implementations of the published
    // algorithm agree byte for byte.
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "api-key", "s3cr3t").ok());
    QCOMPARE(m_fake->sessionAlgorithm, QStringLiteral("dh-ietf1024-sha256-aes128-cbc-pkcs7"));
    QCOMPARE(m_fake->sessionCalls, 1);
    // The bus carried ciphertext, never the plaintext; the fake decoded it.
    QVERIFY(m_fake->lastIncomingRaw != QByteArray("s3cr3t"));
    QCOMPARE(m_fake->lastIncomingRaw.size() % 16, 0);
    QCOMPARE(m_fake->items.first().value, QByteArray("s3cr3t"));
    QCOMPARE(secrets.lookup(m_destination, "api-key").value, QStringLiteral("s3cr3t"));
#else
    QSKIP("libgcrypt is not available in this build");
#endif
  }

  void notSupportedServiceFallsBackToPlain() {
    // Plain is a fallback only: the service that refuses the encrypted
    // algorithm still serves credentials, unencrypted on the session bus.
    m_fake->rejectDhSession = true;
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "api-key", "v").ok());
    QCOMPARE(m_fake->sessionAlgorithm, QStringLiteral("plain"));
#ifdef XERAHS_GCRYPT
    QCOMPARE(m_fake->sessionCalls, 2);
    QCOMPARE(m_fake->lastIncomingRaw, QByteArray("v"));
#else
    QCOMPARE(m_fake->sessionCalls, 1);
#endif
    QCOMPARE(secrets.lookup(m_destination, "api-key").value, QStringLiteral("v"));
  }

  void corruptCiphertextIsRefusedWhole() {
#ifdef XERAHS_GCRYPT
    auto secrets = store();
    QVERIFY(secrets.store(m_destination, "api-key", "v").ok());
    QCOMPARE(m_fake->sessionAlgorithm, QStringLiteral("dh-ietf1024-sha256-aes128-cbc-pkcs7"));
    m_fake->corruptNextSecret = true;
    const auto result = secrets.lookup(m_destination, "api-key");
    QCOMPARE(result.status, CredentialStatus::Denied);
    QVERIFY(result.value.isEmpty());
    QVERIFY(!m_fake->corruptNextSecret);
    // An intact read still works after the refused one.
    QCOMPARE(secrets.lookup(m_destination, "api-key").value, QStringLiteral("v"));
#else
    QSKIP("libgcrypt is not available in this build");
#endif
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
