#include "uploads/SxcuImport.h"

#include "uploads/DestinationCatalog.h"
#include "uploads/CredentialStore.h"
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace xerahs::uploads;

namespace {

bool hasWarning(const SxcuImportResult &result, const QString &code, const QString &detail) {
  for (const auto &warning : result.warnings)
    if (warning.code == code && warning.detail == detail) return true;
  return false;
}

const Secret *findSecret(const SxcuImportResult &result, const QString &name) {
  for (const auto &secret : result.secrets)
    if (secret.name == name) return &secret;
  return nullptr;
}

}  // namespace

class SxcuImportTest : public QObject {
  Q_OBJECT

private slots:
  void criticalMetadataCannotBeAccepted() {
    using D = xerahs::compatibility::ImportDisposition;
    for (const auto *key : {"Authentication", "unknownArguments", "Script", "$ref", "trustedConsent", "assetPath"}) {
      QJsonObject input{{"RequestURL", "https://example.com/up"}, {key, "../private-value"}};
      const auto imported = importSxcu(QJsonDocument(input).toJson());
      QVERIFY(imported.ok); QVERIFY(imported.acceptanceBlocked);
      QCOMPARE(imported.code, QString("import-critical-unsupported"));
      QCOMPARE(imported.quarantine.value(key), input.value(key));
      QVERIFY(!imported.destination.extra.contains(key));
      QVERIFY(imported.report.count(D::Quarantined) > 0);
      QVERIFY(!imported.report.summary().contains("private-value"));
      QTemporaryDir dir; DestinationCatalog catalog(dir.filePath("destinations.json")); MemoryCredentialStore secrets;
      QCOMPARE(catalog.addImported(imported, secrets).code, QString("import-critical-unsupported"));
      QVERIFY(catalog.entries().isEmpty());
    }
  }
  void boundaryMatrix_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("code");
    QTest::newRow("malformed") << QByteArray("{broken") << QString("invalid-json");
    QTest::newRow("duplicate") << QByteArray("{\"Name\":1,\"\\u004eame\":2}") << QString("duplicate-key");
    QTest::newRow("utf8") << QByteArray("{\"a\":\"\xff\"}") << QString("invalid-encoding");
    QTest::newRow("depth") << QByteArray(33, '[') + "0" + QByteArray(33, ']') << QString("too-deep");
    QTest::newRow("bytes") << QByteArray(kMaxDestinationBytes + 1, ' ') << QString("too-large");
    QTest::newRow("elements") << QByteArray("[") + QByteArray("0,").repeated(100001) + "0]" << QString("too-many-elements");
    QTest::newRow("gzip-unsupported") << QByteArray::fromHex("1f8b0800000000000003") << QString("invalid-encoding");
  }
  void boundaryMatrix() {
    QFETCH(QByteArray, bytes); QFETCH(QString, code); const auto imported = importSxcu(bytes);
    QVERIFY(!imported.ok); QCOMPARE(imported.code, code); QVERIFY(imported.secrets.isEmpty());
    QVERIFY(imported.report.count(xerahs::compatibility::ImportDisposition::Rejected) > 0);
  }
  void reportsAndCanonicalRoundTrip() {
    using D = xerahs::compatibility::ImportDisposition;
    struct Restore { QLocale locale; ~Restore() { QLocale::setDefault(locale); } } restore;
    for (const auto &locale : {QLocale::c(), QLocale(QLocale::German), QLocale(QLocale::Turkish)}) {
      QLocale::setDefault(locale);
      for (bool crlf : {false, true}) {
        QByteArray bytes = "{\n\"Name\":\"Cafe\\u0301\", \"RequestURL\":\"https://example.com/up\", \"FileFormName\":\"file\",\n\"Headers\":{\"Authorization\":\"private-value\"}, \"Future\":{\"number\":1.25}}";
        if (crlf) bytes.replace("\n", "\r\n");
        const auto imported = importSxcu(bytes); QVERIFY(imported.ok); QVERIFY(!imported.acceptanceBlocked);
        for (auto d : {D::Retained, D::Transformed, D::Defaulted}) QVERIFY(imported.report.count(d) > 0);
        QCOMPARE(imported.destination.name, QString::fromUtf8("Café"));
        const auto written = writeDestination(imported.destination); QVERIFY(!written.contains("private-value"));
        const auto back = readDestination(written); QVERIFY(back.destination);
        QCOMPARE(back.destination->extra, imported.destination.extra);
        const auto ignored = importSxcu("{\"RequestMethod\":\"POST\",\"RequestType\":\"POST\"}");
        QCOMPARE(ignored.report.count(D::Ignored), qsizetype(1));
        QVERIFY(!ignored.acceptanceBlocked);
      }
    }
  }
  void importsCurrentUploaderAndExtractsSecrets() {
    const auto result = importSxcu(QByteArray(
        "{"
        "\"Version\":\"17.0.0\","
        "\"Name\":\"Example\","
        "\"DestinationType\":\"ImageUploader, FileUploader\","
        "\"RequestMethod\":\"POST\","
        "\"RequestURL\":\"https://example.com/upload.php\","
        "\"Parameters\":{\"album\":\"shots\",\"api_key\":\"query-secret-value\"},"
        "\"Headers\":{\"Authorization\":\"Client-ID super-secret-value\",\"Accept\":\"application/json\"},"
        "\"Body\":\"MultipartFormData\","
        "\"Arguments\":{\"api_key\":\"argument-secret-value\",\"title\":\"shot\"},"
        "\"FileFormName\":\"file\","
        "\"URL\":\"{json:url}\","
        "\"ThumbnailURL\":\"{json:thumbnail_url}\","
        "\"DeletionURL\":\"{json:deletion_url}\","
        "\"ErrorMessage\":\"{json:error}\","
        "\"vendorNote\":\"keep\","
        "\"Notes\":{\"apiKey\":\"nested-secret-value\",\"label\":\"public\"}"
        "}"));
    QVERIFY(result.ok);
    QCOMPARE(result.code, QString());
    QCOMPARE(result.destination.name, QStringLiteral("Example"));
    QCOMPARE(result.destination.method, RequestMethod::Post);
    QCOMPARE(result.destination.body, BodyType::MultipartFormData);
    QCOMPARE(result.destination.fileFormName, QStringLiteral("file"));
    QCOMPARE(result.destination.resultUrl, QStringLiteral("{json:url}"));
    const auto header = [&](const QString &name) {
      for (const auto &field : result.destination.headers)
        if (field.name == name) return field.value;
      return QString();
    };
    const auto argument = [&](const QString &name) {
      for (const auto &field : result.destination.arguments)
        if (field.name == name) return field.value;
      return QString();
    };
    QCOMPARE(header(QStringLiteral("Authorization")), QStringLiteral("{secret:Authorization}"));
    QCOMPARE(header(QStringLiteral("Accept")), QStringLiteral("application/json"));
    QCOMPARE(argument(QStringLiteral("title")), QStringLiteral("shot"));
    QCOMPARE(argument(QStringLiteral("api_key")), QStringLiteral("{secret:api_key#2}"));
    QCOMPARE(result.destination.extra.value(QStringLiteral("vendorNote")).toString(), QStringLiteral("keep"));
    QCOMPARE(result.destination.extra.value(QStringLiteral("DestinationType")).toString(),
             QStringLiteral("ImageUploader, FileUploader"));
    QCOMPARE(result.destination.extra.value(QStringLiteral("Notes")).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("public"));
    QCOMPARE(referencedSecret(result.destination.extra.value(QStringLiteral("Notes")).toObject().value(QStringLiteral("apiKey")).toString()),
             std::optional<QString>(QStringLiteral("apiKey")));

    const auto authorization = findSecret(result, QStringLiteral("Authorization"));
    const auto queryKey = findSecret(result, QStringLiteral("api_key"));
    const auto argumentKey = findSecret(result, QStringLiteral("api_key#2"));
    const auto nested = findSecret(result, QStringLiteral("apiKey"));
    QVERIFY(authorization && queryKey && argumentKey && nested);
    QCOMPARE(authorization->value, QStringLiteral("Client-ID super-secret-value"));
    QCOMPARE(queryKey->value, QStringLiteral("query-secret-value"));
    QCOMPARE(argumentKey->value, QStringLiteral("argument-secret-value"));
    QCOMPARE(nested->value, QStringLiteral("nested-secret-value"));

    const auto written = writeDestination(result.destination);
    const auto text = QString::fromUtf8(written);
    QVERIFY(!text.contains(QStringLiteral("super-secret-value")));
    QVERIFY(!text.contains(QStringLiteral("query-secret-value")));
    QVERIFY(!text.contains(QStringLiteral("argument-secret-value")));
    QVERIFY(!text.contains(QStringLiteral("nested-secret-value")));
    QVERIFY(text.contains(QStringLiteral("{secret:Authorization}")));
    QVERIFY(text.contains(QStringLiteral("keep")));
    QVERIFY(validateDestination(result.destination).isEmpty());
    QVERIFY(hasWarning(result, QStringLiteral("secret-extracted"), QStringLiteral("Authorization")));
  }

  void importsLegacyFieldNames() {
    const auto result = importSxcu(QByteArray(
        "{"
        "\"Name\":\"fuwa.se\","
        "\"RequestType\":\"POST\","
        "\"RequestURL\":\"https://fuwa.se/api/upload\","
        "\"FileFormName\":\"files[]\","
        "\"ResponseType\":\"Text\","
        "\"URL\":\"$json:files[0].url$\""
        "}"));
    QVERIFY(result.ok);
    QCOMPARE(result.destination.method, RequestMethod::Post);
    QCOMPARE(result.destination.body, BodyType::MultipartFormData);
    QCOMPARE(result.destination.fileFormName, QStringLiteral("files[]"));
    QCOMPARE(result.destination.resultUrl, QStringLiteral("$json:files[0].url$"));
    QCOMPARE(result.destination.requestUrl, QStringLiteral("https://fuwa.se/api/upload"));
    QVERIFY(hasWarning(result, QStringLiteral("inferred-body"), QStringLiteral("MultipartFormData")));
    QVERIFY(!hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("ResponseType")));
    QVERIFY(validateDestination(result.destination).isEmpty());
    QVERIFY(result.secrets.isEmpty());
  }

  void flagsUnsupportedSyntaxAndKeepsSecretsOut() {
    const auto result = importSxcu(QByteArray(
        "{"
        "\"Name\":\"old\","
        "\"RequestMethod\":\"POST\","
        "\"RequestType\":\"GET\","
        "\"RequestURL\":\"https://uploader-user:s3cret-pass@example.com/up?token=url-query-secret\","
        "\"Headers\":{\"X-Token\":\"Bearer {base64:base64-secret-token}\",\"Accept\":\"text/plain\"},"
        "\"Body\":\"XML\","
        "\"FileFormName\":\"file\","
        "\"ResponseType\":\"RedirectionURL\","
        "\"RegexList\":[\"https://example.com/abc\"],"
        "\"URL\":\"$1$\","
        "\"Data\":\"{select:a|b}\""
        "}"));
    QVERIFY(result.ok);
    QCOMPARE(result.destination.method, RequestMethod::Post);
    QCOMPARE(result.destination.body, BodyType::Unknown);
    QCOMPARE(result.destination.rawBody, QStringLiteral("XML"));
    QCOMPARE(result.destination.resultUrl, QStringLiteral("$1$"));
    QCOMPARE(result.destination.data, QStringLiteral("{select:a|b}"));
    QVERIFY(hasWarning(result, QStringLiteral("conflicting-method"), QStringLiteral("POST")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-body"), QStringLiteral("XML")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("ResponseType")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("RegexList")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("regex-group")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("base64")));
    QVERIFY(hasWarning(result, QStringLiteral("unsupported-syntax"), QStringLiteral("select")));
    QVERIFY(hasWarning(result, QStringLiteral("split-query"), QStringLiteral("RequestURL")));

    const auto token = findSecret(result, QStringLiteral("X-Token"));
    const auto query = findSecret(result, QStringLiteral("token"));
    const auto password = findSecret(result, QStringLiteral("url-password"));
    QVERIFY(token && query && password);
    QCOMPARE(token->value, QStringLiteral("Bearer {base64:base64-secret-token}"));
    QCOMPARE(query->value, QStringLiteral("url-query-secret"));
    QCOMPARE(password->value, QStringLiteral("s3cret-pass"));
    QVERIFY(!result.destination.requestUrl.contains(QStringLiteral("s3cret-pass")));
    QVERIFY(!result.destination.requestUrl.contains(QStringLiteral("uploader-user")));
    QVERIFY(!result.destination.requestUrl.contains(QStringLiteral("url-query-secret")));
    QCOMPARE(QUrl(result.destination.requestUrl).host(), QStringLiteral("example.com"));

    const auto written = QString::fromUtf8(writeDestination(result.destination));
    QVERIFY(!written.contains(QStringLiteral("base64-secret-token")));
    QVERIFY(!written.contains(QStringLiteral("url-query-secret")));
    QVERIFY(!written.contains(QStringLiteral("s3cret-pass")));
    for (const auto &warning : result.warnings) {
      QVERIFY(!warning.detail.contains(QStringLiteral("base64-secret-token")));
      QVERIFY(!warning.detail.contains(QStringLiteral("s3cret-pass")));
    }
    QVERIFY(validateDestination(result.destination).contains(QStringLiteral("unsupported-body")));
  }

  void leavesExistingSecretReferencesInPlace() {
    const auto result = importSxcu(QByteArray(
        "{"
        "\"Name\":\"Example\","
        "\"RequestMethod\":\"GET\","
        "\"RequestURL\":\"https://example.com/resource\","
        "\"Headers\":{\"Authorization\":\"{secret:Authorization}\"}"
        "}"));
    QVERIFY(result.ok);
    QCOMPARE(result.destination.headers.at(0).value, QStringLiteral("{secret:Authorization}"));
    QVERIFY(result.secrets.isEmpty());
    QCOMPARE(referencedSecret(result.destination.headers.at(0).value), std::optional<QString>(QStringLiteral("Authorization")));
  }

  void rejectsUnusableInput() {
    QCOMPARE(importSxcu(QByteArray("{")).code, QStringLiteral("invalid-json"));
    QVERIFY(!importSxcu(QByteArray("[]")).ok);
    QCOMPARE(importSxcu(QByteArray(kMaxDestinationBytes + 1, 'x')).code, QStringLiteral("too-large"));
    const auto nested = QByteArray(33, '[') + QByteArray(33, ']');
    QCOMPARE(importSxcu(nested).code, QStringLiteral("too-deep"));
    QVERIFY(importSxcu(nested).secrets.isEmpty());
  }
};

QTEST_GUILESS_MAIN(SxcuImportTest)
#include "SxcuImportTest.moc"
