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

#ifdef XERAHS_GCRYPT
#include <gcrypt.h>

namespace {
// ietf-ike-grp-modp-1024: the 1024-bit MODP group every
// org.freedesktop.Secret peer uses for the encrypted session algorithm
// (generator 2, prime below). Byte-for-byte the group libsecret exchanges.
const unsigned char kDhPrime[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2, 0x21, 0x68, 0xC2, 0x34,
    0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1, 0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74,
    0x02, 0x0B, 0xBE, 0xA6, 0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
    0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D, 0xF2, 0x5F, 0x14, 0x37,
    0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45, 0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6,
    0xF4, 0x4C, 0x42, 0xE9, 0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
    0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11, 0x7C, 0x4B, 0x1F, 0xE6,
    0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const char kAesAlgorithm[] = "dh-ietf1024-sha256-aes128-cbc-pkcs7";

bool cryptoReady() {
  static const bool ready = gcry_check_version(nullptr) != nullptr;
  return ready;
}

// Big-endian unsigned MPI bytes without a leading zero (GCRYMPI_FMT_USG),
// the exact public-key byte format org.freedesktop.Secret peers exchange.
gcry_mpi_t mpiFromBytes(const QByteArray &bytes) {
  gcry_mpi_t mpi = nullptr;
  if (bytes.isEmpty() || gcry_mpi_scan(&mpi, GCRYMPI_FMT_USG, bytes.constData(), size_t(bytes.size()), nullptr) != 0)
    return nullptr;
  return mpi;
}

QByteArray mpiToBytes(gcry_mpi_t mpi) {
  unsigned char *buffer = nullptr;
  size_t size = 0;
  if (gcry_mpi_aprint(GCRYMPI_FMT_USG, &buffer, &size, mpi) != 0) return {};
  QByteArray bytes(reinterpret_cast<const char *>(buffer), int(size));
  gcry_free(buffer);
  return bytes;
}

// The shared secret is written zero-left-padded to the prime length in bytes
// (128) before key derivation, as the reference algorithm specifies.
QByteArray mpiFixedBytes(gcry_mpi_t mpi, int fixed) {
  const auto bytes = mpiToBytes(mpi);
  if (bytes.isEmpty() || bytes.size() > fixed) return {};
  return QByteArray(fixed - bytes.size(), '\0') + bytes;
}

// RFC 5869 HKDF with SHA-256, no salt (zeros) and no info, one output block:
// the session-key derivation of the reference algorithm.
QByteArray hkdfSha256(const QByteArray &ikm, int outputLength) {
  const int hashLength = 32;
  if (outputLength < 1 || outputLength > hashLength) return {};
  gcry_md_hd_t extract = nullptr, expand = nullptr;
  QByteArray salt(hashLength, '\0');
  if (gcry_md_open(&extract, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC) != 0 ||
      gcry_md_setkey(extract, salt.constData(), size_t(salt.size())) != 0) {
    if (extract) gcry_md_close(extract);
    return {};
  }
  gcry_md_write(extract, ikm.constData(), size_t(ikm.size()));
  const auto *prk = gcry_md_read(extract, GCRY_MD_SHA256);
  if (gcry_md_open(&expand, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC) != 0 ||
      gcry_md_setkey(expand, prk, hashLength) != 0) {
    gcry_md_close(extract);
    if (expand) gcry_md_close(expand);
    return {};
  }
  gcry_md_write(expand, "\x01", 1);
  const auto *block = gcry_md_read(expand, GCRY_MD_SHA256);
  QByteArray result(reinterpret_cast<const char *>(block), outputLength);
  gcry_md_close(expand);
  gcry_md_close(extract);
  return result;
}

// AES-128-CBC with PKCS#7 padding; the random IV travels in the secret
// structure's parameters field.
bool aesEncrypt(const QByteArray &key, const QByteArray &plain, QByteArray *iv, QByteArray *cipher) {
  if (key.size() != 16) return false;
  const int pad = 16 - (plain.size() % 16);
  QByteArray padded = plain + QByteArray(pad, char(pad));
  QByteArray ivBytes(16, '\0');
  gcry_randomize(ivBytes.data(), 16, GCRY_STRONG_RANDOM);
  gcry_cipher_hd_t handle = nullptr;
  if (gcry_cipher_open(&handle, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CBC, 0) != 0) {
    zeroSecretBytes(&padded);
    return false;
  }
  bool ok = gcry_cipher_setkey(handle, key.constData(), 16) == 0 &&
             gcry_cipher_setiv(handle, ivBytes.constData(), 16) == 0;
  cipher->resize(padded.size());
  if (ok) ok = gcry_cipher_encrypt(handle, cipher->data(), padded.size(), padded.constData(), padded.size()) == 0;
  gcry_cipher_close(handle);
  zeroSecretBytes(&padded);
  if (!ok) cipher->clear();
  *iv = ivBytes;
  return ok;
}

bool aesDecrypt(const QByteArray &key, const QByteArray &iv, const QByteArray &cipher, QByteArray *plain) {
  if (key.size() != 16 || iv.size() != 16 || cipher.isEmpty() || cipher.size() % 16 != 0) return false;
  gcry_cipher_hd_t handle = nullptr;
  if (gcry_cipher_open(&handle, GCRY_CIPHER_AES128, GCRY_CIPHER_MODE_CBC, 0) != 0) return false;
  QByteArray padded;
  padded.resize(cipher.size());
  const bool ok = gcry_cipher_setkey(handle, key.constData(), 16) == 0 &&
                  gcry_cipher_setiv(handle, iv.constData(), 16) == 0 &&
                  gcry_cipher_decrypt(handle, padded.data(), padded.size(), cipher.constData(), cipher.size()) == 0;
  gcry_cipher_close(handle);
  if (!ok) {
    zeroSecretBytes(&padded);
    return false;
  }
  const int pad = static_cast<unsigned char>(padded.at(padded.size() - 1));
  if (pad < 1 || pad > 16 || pad > padded.size()) {
    zeroSecretBytes(&padded);
    return false;
  }
  for (int i = padded.size() - pad; i < padded.size(); ++i)
    if (static_cast<unsigned char>(padded.at(i)) != pad) {
      zeroSecretBytes(&padded);
      return false;
    }
  padded.chop(pad);
  *plain = padded;
  return true;
}
}  // namespace
#else
namespace {
// Without libgcrypt only the plain session can ever be negotiated, so the
// session key is always empty and these codecs are unreachable; inert
// definitions keep the store and read paths uniform for both builds.
bool aesEncrypt(const QByteArray &, const QByteArray &, QByteArray *, QByteArray *) { return false; }
bool aesDecrypt(const QByteArray &, const QByteArray &, const QByteArray &, QByteArray *) { return false; }
}  // namespace
#endif

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

// The encrypted algorithm is negotiated first: a Diffie-Hellman session key
// keeps secret bytes off the bus in the clear. A service that answers
// NotSupported gets the plain fallback, whose secrets stay unencrypted only
// on the user's own session bus, as libsecret's fallback does. Nothing
// crosses the network either way.
CredentialStatus SecretServiceCredentialStore::openSession() const {
  // A probe and an upload can prepare on separate owned workers. Publish the
  // shared immutable session once before either worker reads it.
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  if (!m_session.path().isEmpty()) return CredentialStatus::Ok;
  if (!m_bus.isConnected()) return CredentialStatus::Unavailable;
#ifdef XERAHS_GCRYPT
  if (cryptoReady()) {
    QDBusObjectPath session;
    QByteArray key;
    bool notSupported = false;
    const auto status = negotiateAesSession(&session, &key, &notSupported);
    if (status == CredentialStatus::Ok && !session.path().isEmpty() && key.size() == 16) {
      m_session = session;
      m_sessionKey = key;
      return CredentialStatus::Ok;
    }
    // Only an explicit NotSupported falls back to plain; any other error is
    // returned as it is (review fix A3: never silently retry plain).
    if (!notSupported) return status;
  }
#endif
  return openPlainSessionLocked();
}

CredentialStatus SecretServiceCredentialStore::openPlainSessionLocked() const {
  zeroSecretBytes(&m_sessionKey);
  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("OpenSession"));
  call << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
  const auto reply = m_bus.call(call);
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) return statusFor(QDBusError(reply));
  m_session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
  return m_session.path().isEmpty() ? CredentialStatus::Unavailable : CredentialStatus::Ok;
}

#ifdef XERAHS_GCRYPT
// One round of dh-ietf1024-sha256-aes128-cbc-pkcs7, byte-compatible with the
// reference exchanges: the client public key travels as an unsigned
// big-endian byte array, the shared secret is zero-left-padded to the prime
// length and derived through RFC 5869 HKDF-SHA256 into a 16-byte AES key.
CredentialStatus SecretServiceCredentialStore::negotiateAesSession(QDBusObjectPath *session, QByteArray *key, bool *notSupported) const {
  gcry_mpi_t prime = nullptr, base = nullptr, priv = nullptr, pub = nullptr, peer = nullptr, shared = nullptr;
  const auto release = [&] {
    gcry_mpi_release(prime); gcry_mpi_release(base); gcry_mpi_release(priv);
    gcry_mpi_release(pub); gcry_mpi_release(peer); gcry_mpi_release(shared);
  };
  // gcry_mpi_set_ui allocates and returns the MPI when given null; the
  // return value must be kept.
  if (gcry_mpi_scan(&prime, GCRYMPI_FMT_USG, kDhPrime, sizeof(kDhPrime), nullptr) != 0 ||
      (base = gcry_mpi_set_ui(nullptr, 2)) == nullptr) {
    release();
    return CredentialStatus::Unavailable;
  }
  // A fresh private value below p/2 in secure memory for every negotiation.
  priv = gcry_mpi_snew(1024);
  do {
    gcry_mpi_randomize(priv, 1024, GCRY_STRONG_RANDOM);
  } while (gcry_mpi_cmp_ui(priv, 0) == 0);
  if (gcry_mpi_get_nbits(priv) > 1023) gcry_mpi_clear_highbit(priv, 1023);
  pub = gcry_mpi_new(1024);
  gcry_mpi_powm(pub, base, priv, prime);

  auto call = QDBusMessage::createMethodCall(m_service, kServicePath, kServiceInterface, QStringLiteral("OpenSession"));
  call << QString::fromLatin1(kAesAlgorithm) << QVariant::fromValue(QDBusVariant(mpiToBytes(pub)));
  const auto reply = m_bus.call(call);
  if (reply.type() == QDBusMessage::ErrorMessage) {
    const QDBusError error(reply);
    if (error.name() == QStringLiteral("org.freedesktop.DBus.Error.NotSupported")) *notSupported = true;
    release();
    return statusFor(error);
  }
  if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2) {
    release();
    return statusFor(QDBusError(reply));
  }
  QVariant output = reply.arguments().at(0);
  if (output.canConvert<QDBusVariant>()) output = output.value<QDBusVariant>().variant();
  *session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
  peer = mpiFromBytes(output.toByteArray());
  QByteArray ikm, derived;
  if (peer && !session->path().isEmpty()) {
    shared = gcry_mpi_snew(1024);
    gcry_mpi_powm(shared, peer, priv, prime);
    ikm = mpiFixedBytes(shared, int(sizeof(kDhPrime)));
    derived = hkdfSha256(ikm, 16);
  }
  release();
  zeroSecretBytes(&ikm);
  if (derived.size() != 16) return CredentialStatus::Unavailable;
  *key = derived;
  return CredentialStatus::Ok;
}
#endif

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

QByteArray SecretServiceCredentialStore::currentSessionKey() const {
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  return m_sessionKey;
}

void SecretServiceCredentialStore::resetSession() const {
  const std::lock_guard<std::mutex> lock(m_sessionMutex);
  zeroSecretBytes(&m_sessionKey);
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
  // An encrypted session sends only AES-128-CBC ciphertext, with the IV in
  // the secret structure's parameters (review fix A3).
  QByteArray parameters, encoded = plain;
  const QByteArray sessionKey = currentSessionKey();
  bool encodedOk = true;
  if (!sessionKey.isEmpty()) encodedOk = aesEncrypt(sessionKey, plain, &parameters, &encoded);
  if (!encodedOk) {
    zeroSecretBytes(&plain);
    return {CredentialStatus::Unavailable, {}};
  }
  SecretValue secret{currentSession(), parameters, encoded, QStringLiteral("text/plain; charset=utf8")};
  auto call = QDBusMessage::createMethodCall(m_service, collection.path(), kCollectionInterface, QStringLiteral("CreateItem"));
  call << properties << QVariant::fromValue(secret) << true;
  auto reply = m_bus.call(call);
  // A restarted service no longer knows the session; reopen once and retry
  // instead of failing the save (review fix A2). One retry, never a loop.
  if (reply.type() == QDBusMessage::ErrorMessage && staleSessionError(QDBusError(reply).name())) {
    resetSession();
    if (openSession() == CredentialStatus::Ok) {
      // The recovered session may negotiate a different key: re-encode the
      // secret for it before the single retry, never a loop.
      const QByteArray retryKey = currentSessionKey();
      parameters.clear();
      encoded = plain;
      if (!retryKey.isEmpty() && !aesEncrypt(retryKey, plain, &parameters, &encoded)) {
        zeroSecretBytes(&plain);
        return {CredentialStatus::Unavailable, {}};
      }
      secret.session = currentSession();
      secret.parameters = parameters;
      secret.value = encoded;
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
  // An encrypted session receives AES-128-CBC ciphertext with the IV in the
  // parameters field; a structure that does not decrypt is refused whole
  // (review fix A3: no partial or garbled secret is ever returned).
  const QByteArray sessionKey = currentSessionKey();
  if (!sessionKey.isEmpty()) {
    QByteArray plainBytes;
    const bool decoded = aesDecrypt(sessionKey, found->parameters, found->value, &plainBytes);
    for (auto it = secrets.begin(); it != secrets.end(); ++it) zeroSecretBytes(&it.value().value);
    if (!decoded) return {CredentialStatus::Denied, {}};
    const QString value = QString::fromUtf8(plainBytes);
    zeroSecretBytes(&plainBytes);
    return {CredentialStatus::Ok, value};
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
