// CORE-021, SETCAT-014: saved destinations, default choice and secret lifetime.
#include "uploads/DestinationCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::uploads;

namespace {
const QByteArray kSxcu =
    "{\"Version\":\"17.0.0\",\"Name\":\"Example\",\"RequestMethod\":\"POST\","
    "\"RequestURL\":\"https://example.com/upload.php\","
    "\"Headers\":{\"Authorization\":\"Client-ID super-secret-value\",\"Accept\":\"application/json\"},"
    "\"Body\":\"MultipartFormData\",\"Arguments\":{\"title\":\"shot\"},\"FileFormName\":\"file\",\"URL\":\"{json:url}\"}";

// Fails the Nth store call; everything else goes to a real in-memory store.
class FailingStore final : public CredentialStore {
public:
  MemoryCredentialStore inner;
  int failOnStore = 0, stores = 0;
  CredentialResult store(const QUuid &id, const QString &name, const QString &value) override {
    if (++stores == failOnStore) return {CredentialStatus::Unavailable, {}};
    return inner.store(id, name, value);
  }
  CredentialResult lookup(const QUuid &id, const QString &name) const override { return inner.lookup(id, name); }
  CredentialResult remove(const QUuid &id, const QString &name) override { return inner.remove(id, name); }
  CredentialResult removeAll(const QUuid &id) override { return inner.removeAll(id); }
};

UploadDestination plain(const QString &name) {
  UploadDestination destination;
  destination.id = QUuid::createUuid();
  destination.name = name;
  destination.requestUrl = QStringLiteral("https://example.com/%1").arg(name);
  destination.body = BodyType::Binary;
  return destination;
}
}  // namespace

class DestinationCatalogTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QString path() const { return m_dir.filePath(QStringLiteral("uploads/destinations.json")); }

private slots:
  void init() { QFile::remove(path()); }

  void importStoresSecretsAndPersistsOnlyReferences() {
    MemoryCredentialStore credentials;
    DestinationCatalog catalog(path());
    QVERIFY(catalog.writable());
    const auto import = importSxcu(kSxcu);
    QVERIFY(import.ok);
    const auto outcome = catalog.addImported(import, credentials);
    QVERIFY2(outcome.code.isEmpty(), qPrintable(outcome.code));
    QCOMPARE(catalog.defaultId(), outcome.id);
    QCOMPARE(credentials.lookup(outcome.id, QStringLiteral("Authorization")).value, QStringLiteral("Client-ID super-secret-value"));

    QFile file(path());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(!bytes.contains("super-secret-value"));
    QVERIFY(bytes.contains("{secret:Authorization}"));
    QCOMPARE(file.permissions() & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());

    DestinationCatalog reopened(path());
    QCOMPARE(reopened.entries().size(), 1);
    QCOMPARE(reopened.defaultDestination()->name, QStringLiteral("Example"));
    QCOMPARE(DestinationCatalog::secretNames(*reopened.defaultDestination()), QStringList{QStringLiteral("Authorization")});
    QVERIFY(reopened.entries().first().problems.isEmpty());
  }

  void failedSecretRollsBackAndSavesNothing() {
    FailingStore credentials;
    credentials.failOnStore = 1;
    DestinationCatalog catalog(path());
    const auto outcome = catalog.addImported(importSxcu(kSxcu), credentials);
    QCOMPARE(outcome.code, QStringLiteral("secret-store-failed"));
    QCOMPARE(outcome.secretName, QStringLiteral("Authorization"));
    QVERIFY(catalog.entries().isEmpty());
    QVERIFY(!QFile::exists(path()));
  }

  void removeMovesDefaultAndDeletesSecrets() {
    MemoryCredentialStore credentials;
    DestinationCatalog catalog(path());
    const auto first = plain("one"), second = plain("two");
    QVERIFY(catalog.add(first).isEmpty());
    QVERIFY(catalog.add(second).isEmpty());
    QCOMPARE(catalog.defaultId(), first.id);
    QVERIFY(catalog.setDefault(second.id).isEmpty());
    QVERIFY(credentials.store(second.id, "token", "x").ok());
    QVERIFY(catalog.remove(second.id, credentials).isEmpty());
    QCOMPARE(catalog.defaultId(), first.id);
    QCOMPARE(credentials.lookup(second.id, "token").status, CredentialStatus::NotFound);
    QCOMPARE(catalog.remove(second.id, credentials), QStringLiteral("not-found"));
    QCOMPARE(catalog.setDefault(QUuid::createUuid()), QStringLiteral("not-found"));
    QCOMPARE(catalog.add(first), QStringLiteral("duplicate-id"));
    QVERIFY(catalog.remove(first.id, credentials).isEmpty());
    QVERIFY(catalog.defaultId().isNull());
  }

  void keepsUnreadableEntriesAndRefusesNewerVersions() {
    QDir().mkpath(QFileInfo(path()).absolutePath());
    const auto good = plain("good");
    QJsonArray destinations{QJsonDocument::fromJson(writeDestination(good)).object(), QJsonObject{{"from", "the future"}}};
    QFile file(path());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 1}, {"default", good.id.toString(QUuid::WithoutBraces)}, {"destinations", destinations}, {"note", "keep"}}).toJson());
    file.close();

    DestinationCatalog catalog(path());
    QCOMPARE(catalog.entries().size(), 1);
    QCOMPARE(catalog.unreadableCount(), 1);
    QVERIFY(catalog.add(plain("another")).isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QCOMPARE(saved.value("destinations").toArray().size(), 3);
    QCOMPARE(saved.value("note").toString(), QStringLiteral("keep"));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"version": 2, "destinations": []})");
    file.close();
    DestinationCatalog newer(path());
    QCOMPARE(newer.loadCode(), QStringLiteral("catalog-version-unsupported"));
    QCOMPARE(newer.add(plain("x")), QStringLiteral("catalog-read-only"));
  }

  void refusesToOverwriteAnotherWriter() {
    DestinationCatalog first(path()), second(path());
    QVERIFY(first.add(plain("a")).isEmpty());
    QCOMPARE(second.add(plain("b")), QStringLiteral("catalog-changed-reopen"));
    DestinationCatalog reopened(path());
    QVERIFY(reopened.add(plain("b")).isEmpty());
    QCOMPARE(DestinationCatalog(path()).entries().size(), 2);
  }
};

QTEST_GUILESS_MAIN(DestinationCatalogTest)
#include "DestinationCatalogTest.moc"
