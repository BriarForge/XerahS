#include "uploads/SxcuImport.h"

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
