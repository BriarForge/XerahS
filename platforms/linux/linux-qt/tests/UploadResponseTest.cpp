// CORE-020, CORE-021, COMPCAT-011.
#include "uploads/UploadResponse.h"
#include <QTest>
using namespace xerahs::uploads;
class UploadResponseTest : public QObject {
  Q_OBJECT
private slots:
  void json() {
    UploadDestination d; d.resultUrl = QStringLiteral("{json:data.links[0].url}");
    d.thumbnailUrl = QStringLiteral("{json:data.thumb}"); d.deletionUrl = QStringLiteral("https://example.com/delete/{json:data.deletehash}");
    const auto r = parseUploadResponse(d, 200, {}, "{\"data\":{\"links\":[{\"url\":\"https://i.imgur.com/image.png\"}],\"thumb\":\"https://i.imgur.com/thumb.png\",\"deletehash\":\"abc\"}}");
    QVERIFY(r.code.isEmpty()); QCOMPARE(r.url, QStringLiteral("https://i.imgur.com/image.png"));
    QCOMPARE(r.thumbnailUrl, QStringLiteral("https://i.imgur.com/thumb.png"));
    QCOMPARE(r.deletionUrl, QStringLiteral("https://example.com/delete/abc"));
    for (auto body : {"{", "{}", "{\"data\":null}"})
      QCOMPARE(parseUploadResponse(d, 200, {}, body).code, QStringLiteral("parse-failed"));
    d.resultUrl = QStringLiteral("{json:data[99999999999999999999]}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "{}").code, QStringLiteral("parse-failed"));
  }
  void templates() {
    UploadDestination d;
    QCOMPARE(parseUploadResponse(d, 201, {}, "  https://example.com/a\n").url, QStringLiteral("https://example.com/a"));
    d.resultUrl = QStringLiteral("{response}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "https://example.com/a").url, QStringLiteral("https://example.com/a"));
    d.resultUrl = QStringLiteral("https://example.com/{filename}");
    QCOMPARE(parseUploadResponse(d, 200, {}, {}, QStringLiteral("shot.png")).url, QStringLiteral("https://example.com/shot.png"));
    d.resultUrl = QStringLiteral("{regex:(https://[^ ]+)|1}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "url https://example.com/a end").url, QStringLiteral("https://example.com/a"));
    d.resultUrl = QStringLiteral("https://example.com/{regex:([a-z]{2})|1}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "ab").url, QStringLiteral("https://example.com/ab"));
    d.resultUrl = QStringLiteral("{header:Location}");
    QCOMPARE(parseUploadResponse(d, 200, {{"location", "https://example.com/b"}}, {}).url, QStringLiteral("https://example.com/b"));
    d.resultUrl = QStringLiteral("{xml:/response/url}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "<response><url>https://example.com/c</url></response>").url, QStringLiteral("https://example.com/c"));
    QCOMPARE(parseUploadResponse(d, 200, {}, "<response><url>").code, QStringLiteral("parse-failed"));
    QCOMPARE(parseUploadResponse(d, 200, {}, "<!DOCTYPE r [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><response><url>&x;</url></response>").code, QStringLiteral("parse-failed"));
  }
  void errors() {
    UploadDestination d; d.errorMessage = QStringLiteral("{json:error}");
    auto r = parseUploadResponse(d, 500, {}, "{\"error\":\"upload failed\"}");
    QCOMPARE(r.code, QStringLiteral("http-error")); QCOMPARE(r.httpStatus, 500);
    QCOMPARE(r.errorMessage, QStringLiteral("HTTP 500: upload failed"));
    d.errorMessage.clear(); r = parseUploadResponse(d, 403, {}, QByteArray(300, 'x'));
    QCOMPARE(r.errorMessage, QStringLiteral("HTTP 403: ") + QString(200, u'x'));
    for (auto body : {"file:///tmp/a", "javascript:alert(1)", "hello", "https:///missing"})
      QCOMPARE(parseUploadResponse(d, 200, {}, body).code, QStringLiteral("invalid-result-url"));
    d.resultUrl = QStringLiteral("{regex:(|1}");
    QCOMPARE(parseUploadResponse(d, 200, {}, "anything").code, QStringLiteral("parse-failed"));
  }
};
QTEST_GUILESS_MAIN(UploadResponseTest)
#include "UploadResponseTest.moc"
