// CORE-013, CORE-020, CORE-021, COMPCAT-011.
#include "uploads/UploadRequest.h"
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QTest>
using namespace xerahs::uploads;
namespace {
UploadDestination sample() {
  UploadDestination d; d.id = QUuid::createUuid(); d.name = QStringLiteral("fixture");
  d.requestUrl = QStringLiteral("https://example.com/upload?existing=yes"); return d;
}
}
class UploadRequestTest : public QObject {
  Q_OBJECT
private slots:
  void placeholders() {
    auto d = sample(); MemoryCredentialStore store;
    store.store(d.id, QStringLiteral("token"), QStringLiteral("a&b+ c"));
    d.requestUrl = QStringLiteral("https://example.com/{filename}?existing=yes");
    d.query = {{QStringLiteral("q"), QStringLiteral("{secret:token}")}};
    d.headers = {{"X-Name", "{filename}"}, {"X-Base64", "{base64:hello}"},
                 {"Authorization", "Bearer {secret:token}"}, {"X-Random", "{random:a|b|c}"}};
    UploadInput input; input.fileName = QStringLiteral("a &+?.png");
    UploadRequestOptions options; options.randomSeed = 7;
    const auto result = buildUploadRequest(d, input, store, options);
    QVERIFY2(result.request.has_value(), qPrintable(result.code));
    QCOMPARE(result.request->url.path(), QStringLiteral("/a &+?.png"));
    QVERIFY(result.request->url.toEncoded().contains("q=a%26b%2B%20c"));
    QVERIFY(result.request->url.toEncoded().contains("existing=yes"));
    QCOMPARE(result.request->headers.at(0).value, input.fileName);
    QCOMPARE(result.request->headers.at(1).value, QStringLiteral("aGVsbG8="));
    QCOMPARE(result.request->headers.at(2).value, QStringLiteral("Bearer a&b+ c"));
    QCOMPARE(result.request->headers.at(3).value, buildUploadRequest(d, input, store, options).request->headers.at(3).value);
    QVERIFY(QStringList({"a", "b", "c"}).contains(result.request->headers.at(3).value));
  }
  void urlTemplates() {
    auto d = sample(); MemoryCredentialStore store; UploadInput input;
    d.requestUrl = QStringLiteral("https://{random:a|b}.example.com/upload?name={filename}");
    input.fileName = QStringLiteral("a&b.png");
    const auto result = buildUploadRequest(d, input, store);
    QVERIFY2(result.request.has_value(), qPrintable(result.code));
    QVERIFY(result.request->url.host() == QLatin1String("a.example.com") || result.request->url.host() == QLatin1String("b.example.com"));
    QVERIFY(result.request->url.toEncoded().contains("name=a%26b.png"));
  }
  void methodsAndValidation() {
    MemoryCredentialStore store; UploadInput input;
    for (auto method : {RequestMethod::Get, RequestMethod::Post, RequestMethod::Put, RequestMethod::Patch, RequestMethod::Delete}) {
      auto d = sample(); d.method = method;
      const auto result = buildUploadRequest(d, input, store);
      QVERIFY(result.request); QCOMPARE(result.request->method, requestMethodString(method).toLatin1());
    }
    for (auto url : {"ftp://example.com/upload", "https://user:password@example.com/upload", "https://example.com/invalid path"}) {
      auto d = sample(); d.requestUrl = QString::fromLatin1(url);
      QCOMPARE(buildUploadRequest(d, input, store).code, validateDestination(d).first());
    }
  }
  void bodies() {
    auto d = sample(); MemoryCredentialStore store;
    UploadInput input; input.fileName = QStringLiteral("a\".png"); input.fileBytes = QByteArray("a\0b", 3); input.mimeType = QStringLiteral("image/png");
    d.arguments = {{"title", "{filename}"}};
    for (auto type : {BodyType::None, BodyType::Binary, BodyType::FormUrlEncoded, BodyType::Json, BodyType::MultipartFormData}) {
      d.body = type; d.fileFormName = QStringLiteral("file"); d.data = QStringLiteral("{\"name\":\"{filename}\"}");
      const auto result = buildUploadRequest(d, input, store);
      QVERIFY(result.request); const auto &r = *result.request;
      if (type == BodyType::None) QVERIFY(r.body.isEmpty());
      if (type == BodyType::Binary) { QCOMPARE(r.body, input.fileBytes); QCOMPARE(r.contentType, QByteArray("image/png")); }
      if (type == BodyType::Json) QCOMPARE(QJsonDocument::fromJson(r.body).object().value("name").toString(), input.fileName);
      if (type == BodyType::FormUrlEncoded) QCOMPARE(r.body, QByteArray("title=a%22.png"));
      if (type == BodyType::MultipartFormData) {
        const auto boundary = r.contentType.mid(r.contentType.indexOf("boundary=") + 9);
        QVERIFY(r.body.startsWith("--" + boundary + "\r\n"));
        QVERIFY(r.body.contains("name=\"title\"\r\n\r\na\".png\r\n"));
        QVERIFY(r.body.contains("filename=\"a\\\".png\"\r\nContent-Type: image/png\r\n\r\n" + input.fileBytes));
        QVERIFY(r.body.endsWith("\r\n--" + boundary + "--\r\n"));
      }
    }
    d.body = BodyType::Json; d.data = QStringLiteral("{\"v\":\"{secret:s}\"}");
    store.store(d.id, QStringLiteral("s"), QStringLiteral("\"\n\\"));
    const auto r = buildUploadRequest(d, input, store); QVERIFY(r.request);
    QCOMPARE(QJsonDocument::fromJson(r.request->body).object().value("v").toString(), QStringLiteral("\"\n\\"));
  }
  void failuresAndLimits() {
    auto d = sample(); MemoryCredentialStore store; UploadInput input;
    d.headers = {{"Authorization", "{secret:token}"}};
    auto r = buildUploadRequest(d, input, store);
    QCOMPARE(r.code, QStringLiteral("missing-secret")); QCOMPARE(r.errorMessage, QStringLiteral("token"));
    for (auto status : {CredentialStatus::Denied, CredentialStatus::Unavailable}) {
      store.setForcedStatus(status);
      QCOMPARE(buildUploadRequest(d, input, store).code, QStringLiteral("credential-unavailable"));
    }
    store.setForcedStatus(std::nullopt); d.headers.clear();
    UploadRequestOptions options; options.maxInputBytes = 3; input.fileBytes = "four";
    QCOMPARE(buildUploadRequest(d, input, store, options).code, QStringLiteral("input-too-large"));
    QTemporaryFile file; QVERIFY(file.open()); file.write("four"); file.flush();
    input.fileBytes.clear(); input.filePath = file.fileName();
    QCOMPARE(buildUploadRequest(d, input, store, options).code, QStringLiteral("input-too-large"));
    options.maxInputBytes = 4; d.body = BodyType::Binary;
    QCOMPARE(buildUploadRequest(d, input, store, options).request->body, QByteArray("four"));
    d.id = {}; input.filePath = QStringLiteral("/nonexistent");
    QCOMPARE(buildUploadRequest(d, input, store).code, QStringLiteral("empty-id"));
    d = sample(); input = {}; d.headers = {{"X-Injected", "bad\r\nHeader: value"}};
    QCOMPARE(buildUploadRequest(d, input, store).code, QStringLiteral("invalid-header"));
  }
};
QTEST_GUILESS_MAIN(UploadRequestTest)
#include "UploadRequestTest.moc"
