// CORE-021, COMPCAT-011: uploader files people actually use, end to end.
#include "uploads/CredentialStore.h"
#include "uploads/SxcuImport.h"
#include "uploads/UploadRequest.h"
#include "uploads/UploadResponse.h"

#include <QFile>
#include <QJsonDocument>
#include <QTest>

#include <optional>

using namespace xerahs::uploads;

namespace {

QByteArray fixtureBytes(const char *name) {
  QFile file(QString::fromUtf8(XERAHS_SXCU_FIXTURES) + QLatin1Char('/') + QLatin1String(name));
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}

SxcuImportResult importFixture(const char *name) { return importSxcu(fixtureBytes(name)); }

QString warnings(const SxcuImportResult &result) {
  QStringList text;
  for (const auto &warning : result.warnings) text.append(warning.code + QLatin1Char('/') + warning.detail);
  return text.join(QStringLiteral(", "));
}

// The path a user takes with a real .sxcu file: import it, keep whatever it
// carried out of band, then build the request that would go on the wire.
struct Loaded {
  SxcuImportResult imported;
  std::optional<PreparedUploadRequest> request;
  QString code;
  bool ok() const { return imported.ok && request.has_value(); }
  QString failure() const { return imported.ok ? code : QStringLiteral("import: ") + imported.code; }
};

Loaded load(const char *name, MemoryCredentialStore *store) {
  Loaded loaded;
  loaded.imported = importFixture(name);
  if (!loaded.imported.ok) return loaded;
  for (const auto &secret : loaded.imported.secrets)
    store->store(loaded.imported.destination.id, secret.name, secret.value);
  UploadInput input;
  input.fileName = QStringLiteral("shot.png");
  input.fileBytes = QByteArrayLiteral("PNGDATA");
  input.mimeType = QStringLiteral("image/png");
  const auto built = buildUploadRequest(loaded.imported.destination, input, *store);
  loaded.request = built.request;
  loaded.code = built.code;
  return loaded;
}

QString fieldValue(const QVector<Field> &fields, const QString &name) {
  for (const auto &field : fields)
    if (field.name == name) return field.value;
  return QString();
}

QString secretName(const SxcuImportResult &result, const QString &name) {
  for (const auto &secret : result.secrets)
    if (secret.value == name) return secret.name;
  return {};
}

// A uploader that carried a credential legitimately reports one
// secret-extracted warning for it. Anything else means the importer had to
// drop or guess at something in a file users actually rely on.
bool onlySecretExtracted(const SxcuImportResult &result, const QString &name) {
  if (result.warnings.size() != 1) return false;
  return result.warnings.at(0).code == QLatin1String("secret-extracted") && result.warnings.at(0).detail == name;
}

// A non-file multipart part: its name on the disposition line, its value
// after the blank line.
bool hasArgument(const QByteArray &body, const QString &name, const QString &value) {
  return body.contains("Content-Disposition: form-data; name=\"" + name.toUtf8() + "\"\r\n\r\n" + value.toUtf8() + "\r\n");
}

bool hasFilePart(const QByteArray &body, const QString &formName) {
  return body.contains("Content-Disposition: form-data; name=\"" + formName.toUtf8() + "\";" +
                       " filename=\"shot.png\"\r\nContent-Type: image/png\r\n\r\nPNGDATA\r\n");
}

}  // namespace

class SxcuFixturesTest : public QObject {
  Q_OBJECT

  // Every multipart uploader must produce one well-formed envelope carrying
  // the file under the form name the uploader asked for.
  void verifyMultipart(const PreparedUploadRequest &request, const QString &formName) {
    QVERIFY(request.contentType.startsWith("multipart/form-data; boundary="));
    const auto boundary = request.contentType.mid(request.contentType.indexOf("boundary=") + 9);
    QVERIFY(!boundary.isEmpty());
    QVERIFY(request.body.startsWith("--" + boundary + "\r\n"));
    QVERIFY(request.body.endsWith("\r\n--" + boundary + "--\r\n"));
    QVERIFY(hasFilePart(request.body, formName));
  }

private slots:
  void catboxUploader() {
    MemoryCredentialStore store;
    const auto loaded = load("catbox.sxcu", &store);
    QVERIFY2(loaded.ok(), qPrintable(loaded.failure()));
    const auto &destination = loaded.imported.destination;
    QCOMPARE(destination.name, QStringLiteral("Catbox"));
    QCOMPARE(destination.method, RequestMethod::Post);
    QCOMPARE(destination.requestUrl, QStringLiteral("https://catbox.moe/user/api.php"));
    QCOMPARE(destination.body, BodyType::MultipartFormData);
    QCOMPARE(destination.fileFormName, QStringLiteral("fileToUpload"));
    QCOMPARE(destination.resultUrl, QStringLiteral("{response}"));
    QVERIFY2(loaded.imported.warnings.isEmpty(), qPrintable(warnings(loaded.imported)));
    QVERIFY(loaded.imported.secrets.isEmpty());

    const auto &request = *loaded.request;
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.url.scheme(), QStringLiteral("https"));
    QCOMPARE(request.url.host(), QStringLiteral("catbox.moe"));
    QCOMPARE(request.url.path(), QStringLiteral("/user/api.php"));
    QVERIFY(request.url.query().isEmpty());
    verifyMultipart(request, QStringLiteral("fileToUpload"));
    QVERIFY(hasArgument(request.body, QStringLiteral("reqtype"), QStringLiteral("fileupload")));
    QVERIFY(hasArgument(request.body, QStringLiteral("userhash"), QString()));
  }

  void litterboxUploader() {
    MemoryCredentialStore store;
    const auto loaded = load("litterbox.sxcu", &store);
    QVERIFY2(loaded.ok(), qPrintable(loaded.failure()));
    const auto &destination = loaded.imported.destination;
    QCOMPARE(destination.name, QStringLiteral("Litterbox"));
    QCOMPARE(destination.method, RequestMethod::Post);
    QCOMPARE(destination.requestUrl, QStringLiteral("https://litterbox.catbox.moe/resources/internals/api.php"));
    QCOMPARE(destination.body, BodyType::MultipartFormData);
    QCOMPARE(destination.fileFormName, QStringLiteral("fileToUpload"));
    QVERIFY2(loaded.imported.warnings.isEmpty(), qPrintable(warnings(loaded.imported)));
    QVERIFY(loaded.imported.secrets.isEmpty());

    const auto &request = *loaded.request;
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.url.host(), QStringLiteral("litterbox.catbox.moe"));
    QCOMPARE(request.url.path(), QStringLiteral("/resources/internals/api.php"));
    verifyMultipart(request, QStringLiteral("fileToUpload"));
    QVERIFY(hasArgument(request.body, QStringLiteral("reqtype"), QStringLiteral("fileupload")));
    QVERIFY(hasArgument(request.body, QStringLiteral("time"), QStringLiteral("1h")));
  }

  void zerox0Uploader() {
    MemoryCredentialStore store;
    const auto loaded = load("zerox0.sxcu", &store);
    QVERIFY2(loaded.ok(), qPrintable(loaded.failure()));
    const auto &destination = loaded.imported.destination;
    QCOMPARE(destination.name, QStringLiteral("0x0.st"));
    QCOMPARE(destination.method, RequestMethod::Post);
    QCOMPARE(destination.requestUrl, QStringLiteral("https://0x0.st"));
    QCOMPARE(destination.body, BodyType::MultipartFormData);
    QCOMPARE(destination.fileFormName, QStringLiteral("file"));
    QVERIFY2(loaded.imported.warnings.isEmpty(), qPrintable(warnings(loaded.imported)));
    QVERIFY(loaded.imported.secrets.isEmpty());

    const auto &request = *loaded.request;
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.url.scheme(), QStringLiteral("https"));
    QCOMPARE(request.url.host(), QStringLiteral("0x0.st"));
    QCOMPARE(request.url.path(), QString());
    // No arguments: the file part is the whole body after the preamble.
    verifyMultipart(request, QStringLiteral("file"));
    QCOMPARE(request.body.count("Content-Disposition"), 1);
  }

  void imgurUploader() {
    MemoryCredentialStore store;
    const auto loaded = load("imgur.sxcu", &store);
    QVERIFY2(loaded.ok(), qPrintable(loaded.failure()));
    const auto &destination = loaded.imported.destination;
    QCOMPARE(destination.name, QStringLiteral("Imgur"));
    QCOMPARE(destination.method, RequestMethod::Post);
    QCOMPARE(destination.requestUrl, QStringLiteral("https://api.imgur.com/3/image"));
    QCOMPARE(destination.body, BodyType::MultipartFormData);
    QCOMPARE(destination.fileFormName, QStringLiteral("image"));
    QCOMPARE(destination.resultUrl, QStringLiteral("{json:data.link}"));
    QCOMPARE(destination.deletionUrl, QStringLiteral("https://imgur.com/delete/{json:data.deletehash}"));
    QCOMPARE(destination.errorMessage, QStringLiteral("{json:data.error}"));
    QCOMPARE(loaded.imported.secrets.size(), 1);
    QVERIFY2(onlySecretExtracted(loaded.imported, QStringLiteral("Authorization")), qPrintable(warnings(loaded.imported)));

    // The credential never stays in the destination: it is a reference there
    // and a keyring value here.
    QCOMPARE(fieldValue(destination.headers, QStringLiteral("Authorization")), QStringLiteral("{secret:Authorization}"));
    QCOMPARE(secretName(loaded.imported, QStringLiteral("Client-ID 0123456789abcdef")), QStringLiteral("Authorization"));
    const auto stored = QString::fromUtf8(writeDestination(destination));
    QVERIFY(!stored.contains(QStringLiteral("0123456789abcdef")));
    QVERIFY(stored.contains(QStringLiteral("{secret:Authorization}")));
    QCOMPARE(store.lookup(destination.id, QStringLiteral("Authorization")).value, QStringLiteral("Client-ID 0123456789abcdef"));

    const auto &request = *loaded.request;
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.url.host(), QStringLiteral("api.imgur.com"));
    QCOMPARE(request.url.path(), QStringLiteral("/3/image"));
    QCOMPARE(fieldValue(request.headers, QStringLiteral("Authorization")), QStringLiteral("Client-ID 0123456789abcdef"));
    QVERIFY(request.sensitiveValues.contains(QStringLiteral("Client-ID 0123456789abcdef")));
    verifyMultipart(request, QStringLiteral("image"));
  }

  void jsonBodyUploader() {
    MemoryCredentialStore store;
    const auto loaded = load("json-body.sxcu", &store);
    QVERIFY2(loaded.ok(), qPrintable(loaded.failure()));
    const auto &destination = loaded.imported.destination;
    QCOMPARE(destination.name, QStringLiteral("JSON Body API"));
    QCOMPARE(destination.method, RequestMethod::Post);
    QCOMPARE(destination.requestUrl, QStringLiteral("https://example.com/api/upload"));
    QCOMPARE(destination.body, BodyType::Json);
    QVERIFY(destination.fileFormName.isEmpty());
    QCOMPARE(destination.resultUrl, QStringLiteral("{json:files[0].url}"));
    QCOMPARE(loaded.imported.secrets.size(), 1);
    QVERIFY2(onlySecretExtracted(loaded.imported, QStringLiteral("X-API-Key")), qPrintable(warnings(loaded.imported)));

    QCOMPARE(fieldValue(destination.headers, QStringLiteral("X-API-Key")), QStringLiteral("{secret:X-API-Key}"));
    QCOMPARE(secretName(loaded.imported, QStringLiteral("secret-key-123")), QStringLiteral("X-API-Key"));
    const auto stored = QString::fromUtf8(writeDestination(destination));
    QVERIFY(!stored.contains(QStringLiteral("secret-key-123")));
    QCOMPARE(store.lookup(destination.id, QStringLiteral("X-API-Key")).value, QStringLiteral("secret-key-123"));

    const auto &request = *loaded.request;
    QCOMPARE(request.method, QByteArrayLiteral("POST"));
    QCOMPARE(request.url.host(), QStringLiteral("example.com"));
    QCOMPARE(request.url.path(), QStringLiteral("/api/upload"));
    QCOMPARE(fieldValue(request.headers, QStringLiteral("X-API-Key")), QStringLiteral("secret-key-123"));
    QCOMPARE(request.contentType, QByteArrayLiteral("application/json"));
    const auto body = QJsonDocument::fromJson(request.body);
    QVERIFY2(body.isObject(), request.body.constData());
    QCOMPARE(body.object().value(QStringLiteral("name")).toString(), QStringLiteral("shot.png"));
    QVERIFY(request.sensitiveValues.contains(QStringLiteral("secret-key-123")));
  }

  void catboxResponse() {
    const auto imported = importFixture("catbox.sxcu");
    QVERIFY2(imported.ok, qPrintable(imported.code));
    const auto outcome = parseUploadResponse(imported.destination, 200, {}, QByteArrayLiteral("https://files.catbox.moe/abc.png\n"));
    QCOMPARE(outcome.httpStatus, 200);
    QCOMPARE(outcome.url, QStringLiteral("https://files.catbox.moe/abc.png"));
    QVERIFY(outcome.deletionUrl.isEmpty());
    QVERIFY(outcome.errorMessage.isEmpty());
    QVERIFY(outcome.code.isEmpty());
  }

  void litterboxResponse() {
    const auto imported = importFixture("litterbox.sxcu");
    QVERIFY2(imported.ok, qPrintable(imported.code));
    const auto outcome = parseUploadResponse(imported.destination, 200, {}, QByteArrayLiteral("https://files.catbox.moe/abc.png\n"));
    QCOMPARE(outcome.httpStatus, 200);
    QCOMPARE(outcome.url, QStringLiteral("https://files.catbox.moe/abc.png"));
    QVERIFY(outcome.code.isEmpty());
  }

  void zerox0Response() {
    const auto imported = importFixture("zerox0.sxcu");
    QVERIFY2(imported.ok, qPrintable(imported.code));
    const auto outcome = parseUploadResponse(imported.destination, 200, {}, QByteArrayLiteral("https://files.catbox.moe/abc.png\n"));
    QCOMPARE(outcome.httpStatus, 200);
    QCOMPARE(outcome.url, QStringLiteral("https://files.catbox.moe/abc.png"));
    QVERIFY(outcome.code.isEmpty());
  }

  void imgurResponse() {
    const auto imported = importFixture("imgur.sxcu");
    QVERIFY2(imported.ok, qPrintable(imported.code));
    const auto outcome = parseUploadResponse(imported.destination, 200, {},
                                             QByteArrayLiteral("{\"data\":{\"link\":\"https://i.imgur.com/x.png\","
                                                               "\"deletehash\":\"DH\"},\"success\":true}"));
    QCOMPARE(outcome.httpStatus, 200);
    QCOMPARE(outcome.url, QStringLiteral("https://i.imgur.com/x.png"));
    QCOMPARE(outcome.deletionUrl, QStringLiteral("https://imgur.com/delete/DH"));
    QVERIFY(outcome.thumbnailUrl.isEmpty());
    QVERIFY(outcome.errorMessage.isEmpty());
    QVERIFY(outcome.code.isEmpty());
  }

  void jsonBodyResponse() {
    const auto imported = importFixture("json-body.sxcu");
    QVERIFY2(imported.ok, qPrintable(imported.code));
    const auto outcome = parseUploadResponse(imported.destination, 200, {},
                                             QByteArrayLiteral("{\"files\":[{\"url\":\"https://example.com/f/1.png\"}]}"));
    QCOMPARE(outcome.httpStatus, 200);
    QCOMPARE(outcome.url, QStringLiteral("https://example.com/f/1.png"));
    QVERIFY(outcome.code.isEmpty());
  }
};

QTEST_GUILESS_MAIN(SxcuFixturesTest)
#include "SxcuFixturesTest.moc"