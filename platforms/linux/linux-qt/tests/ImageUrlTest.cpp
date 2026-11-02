// EC-003 policy: URL validation and redirect decisions (Qt Core only).
#include "image-editor/ImageUrl.h"
#include <QTest>
using namespace xerahs::editor;
namespace { QString code(const char16_t *c) { return QString::fromUtf16(c); } }
class ImageUrlTest : public QObject {
  Q_OBJECT
private slots:
  void httpsAccepted() {
    const auto r = validateImageUrl(" https://Example.com:8443/a/b.png?token=secret#frag ", false);
    QVERIFY(r.url); QCOMPARE(r.url->scheme(), QString("https")); QVERIFY(r.url->fragment().isEmpty());
    QCOMPARE(sourceLabel(*r.url), QString("example.com:8443"));
    QVERIFY(!sourceLabel(*r.url).contains("secret"));
  }
  void refusals_data() {
    QTest::addColumn<QString>("text"); QTest::addColumn<bool>("optIn"); QTest::addColumn<QString>("error");
    QTest::newRow("empty") << "" << false << code(urlDiagnostic::empty);
    QTest::newRow("blank") << "   " << false << code(urlDiagnostic::empty);
    QTest::newRow("http default") << "http://example.com/a.png" << false << code(urlDiagnostic::insecure);
    QTest::newRow("ftp") << "ftp://example.com/a.png" << true << code(urlDiagnostic::scheme);
    QTest::newRow("file") << "file:///etc/passwd" << true << code(urlDiagnostic::scheme);
    QTest::newRow("data") << "data:image/png;base64,AAAA" << true << code(urlDiagnostic::scheme);
    QTest::newRow("javascript") << "javascript:alert(1)" << true << code(urlDiagnostic::scheme);
    QTest::newRow("no scheme") << "example.com/a.png" << true << code(urlDiagnostic::invalid);
    QTest::newRow("userinfo") << "https://user:pw@example.com/a.png" << false << code(urlDiagnostic::userInfo);
    QTest::newRow("user only") << "https://user@example.com/a.png" << false << code(urlDiagnostic::userInfo);
    QTest::newRow("userinfo http opt-in") << "http://user:pw@example.com/a.png" << true << code(urlDiagnostic::userInfo);
    QTest::newRow("empty host") << "https:///a.png" << false << code(urlDiagnostic::noHost);
    QTest::newRow("whitespace inside") << "https://exa mple.com/a.png" << false << code(urlDiagnostic::invalid);
    QTest::newRow("control char") << QString("https://example.com/a\n.png") << false << code(urlDiagnostic::invalid);
    QTest::newRow("too long") << ("https://example.com/" + QString(3000, 'a')) << false << code(urlDiagnostic::tooLong);
  }
  void refusals() {
    QFETCH(QString, text); QFETCH(bool, optIn); QFETCH(QString, error);
    const auto r = validateImageUrl(text, optIn); QVERIFY(!r.url); QCOMPARE(r.error, error);
    QVERIFY(!describeUrlDiagnostic(r.error).contains("example.com"));  // messages never echo the URL
  }
  void plainHttpOnlyWithOptIn() {
    QVERIFY(!validateImageUrl("http://example.com/a.png", false).url);
    const auto r = validateImageUrl("http://example.com/a.png", true); QVERIFY(r.url); QCOMPARE(r.url->scheme(), QString("http"));
  }
  void redirectFollowedAndResolved() {
    const QUrl from("https://a.example/dir/x.png");
    auto d = decideRedirect(from, "/other.png", 0, false); QVERIFY(d.target); QCOMPARE(d.target->toString(), QString("https://a.example/other.png"));
    d = decideRedirect(from, "https://b.example/y.png", 4, false); QVERIFY(d.target);
  }
  void redirectLimit() {
    const QUrl from("https://a.example/x.png");
    const auto d = decideRedirect(from, "/y.png", kMaxRedirects, false); QVERIFY(!d.target); QCOMPARE(d.error, code(urlDiagnostic::tooManyRedirects));
    QVERIFY(!decideRedirect(from, "/y.png", 2, false, 2).target);
  }
  void httpsToHttpNeverAllowed() {
    const QUrl from("https://a.example/x.png");
    for (const bool optIn : {false, true}) {
      const auto d = decideRedirect(from, "http://a.example/x.png", 0, optIn); QVERIFY(!d.target); QCOMPARE(d.error, code(urlDiagnostic::downgrade));
    }
  }
  void httpToHttpsAndOptedInHttp() {
    const QUrl http("http://a.example/x.png");
    QVERIFY(decideRedirect(http, "https://a.example/x.png", 0, false).target);
    QVERIFY(!decideRedirect(http, "http://b.example/x.png", 0, false).target);  // no opt-in: refused
    QVERIFY(decideRedirect(http, "http://b.example/x.png", 0, true).target);
  }
  void redirectTargetsAreRevalidated() {
    const QUrl from("https://a.example/x.png");
    QVERIFY(!decideRedirect(from, "ftp://a.example/x.png", 0, true).target);
    QVERIFY(!decideRedirect(from, "https://u:p@a.example/x.png", 0, true).target);
    QVERIFY(!decideRedirect(from, "", 0, true).target);
    QVERIFY(!decideRedirect(from, "file:///etc/passwd", 0, true).target);
    QVERIFY(!decideRedirect(from, "https:///x.png", 0, true).target);
  }
  void limitsMatchRasterCeiling() { QCOMPARE(DownloadLimits{}.maxBytes, qint64(1073741824)); QCOMPARE(DownloadLimits{}.maxRedirects, 5); }
};
QTEST_APPLESS_MAIN(ImageUrlTest)
#include "ImageUrlTest.moc"
