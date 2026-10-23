// CORE-020, CORE-021, COMPCAT-011.
#include "uploads/UploadResponse.h"
#include <QElapsedTimer>
#include <QTest>
using namespace xerahs::uploads;
class UploadResponseTest : public QObject {
  Q_OBJECT
private slots:
  void unresolvedPlaceholdersNeverPublishATruncatedUrl() {
    // A reply missing a field must not turn https://example.com/{json:data.id}
    // into a "successful" https://example.com/.
    UploadDestination d;
    d.resultUrl = QStringLiteral("https://example.com/{json:data.id}");
    d.thumbnailUrl = QStringLiteral("https://example.com/t/{json:data.thumb}");
    d.deletionUrl = QStringLiteral("https://example.com/delete/{json:data.hash}");
    for (const QByteArray body : {QByteArray("{\"data\":{}}"), QByteArray("{\"data\":{\"id\":null}}")}) {
      const auto r = parseUploadResponse(d, 200, {}, body);
      QCOMPARE(r.code, QStringLiteral("parse-failed"));
      QVERIFY(r.url.isEmpty());
      QVERIFY(!r.errorMessage.isEmpty());
    }
    // Optional links are dropped whole, never published half-expanded.
    const auto partial = parseUploadResponse(d, 200, {}, "{\"data\":{\"id\":\"abc\"}}");
    QCOMPARE(partial.code, QString());
    QCOMPARE(partial.url, QStringLiteral("https://example.com/abc"));
    QVERIFY(partial.thumbnailUrl.isEmpty());
    QVERIFY(partial.deletionUrl.isEmpty());
    const auto full = parseUploadResponse(d, 200, {}, "{\"data\":{\"id\":\"abc\",\"thumb\":\"t1\",\"hash\":\"h1\"}}");
    QCOMPARE(full.thumbnailUrl, QStringLiteral("https://example.com/t/t1"));
    QCOMPARE(full.deletionUrl, QStringLiteral("https://example.com/delete/h1"));
  }
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
  // CORE-020: JSON templates resolve dotted paths and array indexes. A path the
  // template cannot name expands to nothing.
  void jsonTemplates_data() {
    QTest::addColumn<QString>("urlTemplate"); QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expectedUrl"); QTest::addColumn<QString>("expectedCode");
    QTest::newRow("nested-objects") << QStringLiteral("{json:data.links.url}")
        << QByteArray("{\"data\":{\"links\":{\"url\":\"https://example.com/n\"}}}")
        << QStringLiteral("https://example.com/n") << QString();
    QTest::newRow("array-index-0") << QStringLiteral("{json:items[0]}")
        << QByteArray("{\"items\":[\"https://example.com/a\",\"https://example.com/b\",\"https://example.com/c\"]}")
        << QStringLiteral("https://example.com/a") << QString();
    QTest::newRow("array-index-2") << QStringLiteral("{json:items[2]}")
        << QByteArray("{\"items\":[\"https://example.com/a\",\"https://example.com/b\",\"https://example.com/c\"]}")
        << QStringLiteral("https://example.com/c") << QString();
    QTest::newRow("array-index-out-of-range") << QStringLiteral("{json:items[3]}")
        << QByteArray("{\"items\":[\"https://example.com/a\",\"https://example.com/b\",\"https://example.com/c\"]}")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("negative-index") << QStringLiteral("{json:items[-1]}")
        << QByteArray("{\"items\":[\"https://example.com/a\"]}")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("root-array-index") << QStringLiteral("{json:[0].url}")
        << QByteArray("[{\"url\":\"https://example.com/root\"}]")
        << QStringLiteral("https://example.com/root") << QString();
    QTest::newRow("path-through-string") << QStringLiteral("{json:data.note.more}")
        << QByteArray("{\"data\":{\"note\":\"text\"}}")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("unicode-value-in-url") << QStringLiteral("https://example.com/{json:name}")
        << QByteArray("{\"name\":\"日本語-café\"}")
        << QString::fromUtf8("https://example.com/日本語-café") << QString();
    QTest::newRow("unicode-value-alone") << QStringLiteral("{json:name}")
        << QByteArray("{\"name\":\"日本語-café\"}")
        << QString::fromUtf8("日本語-café") << QStringLiteral("invalid-result-url");
    QTest::newRow("number-value") << QStringLiteral("https://example.com/{json:count}")
        << QByteArray("{\"count\":42}") << QStringLiteral("https://example.com/42") << QString();
    QTest::newRow("boolean-value") << QStringLiteral("https://example.com/{json:flag}")
        << QByteArray("{\"flag\":true}") << QStringLiteral("https://example.com/true") << QString();
    QTest::newRow("null-value") << QStringLiteral("{json:data}") << QByteArray("{\"data\":null}")
        << QString() << QStringLiteral("parse-failed");
    // The path syntax has no escape, so a key that contains a dot is unreachable:
    // "a.b" is read as the path "a" then "b" and resolves to nothing.
    QTest::newRow("key-containing-dot") << QStringLiteral("{json:a.b}")
        << QByteArray("{\"a.b\":\"https://example.com/dot\"}")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("invalid-json") << QStringLiteral("{json:data}") << QByteArray("{\"data\":")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("empty-body") << QStringLiteral("{json:data}") << QByteArray()
        << QString() << QStringLiteral("parse-failed");
  }
  void jsonTemplates() {
    QFETCH(QString, urlTemplate); QFETCH(QByteArray, body); QFETCH(QString, expectedUrl); QFETCH(QString, expectedCode);
    UploadDestination d; d.resultUrl = urlTemplate;
    const auto r = parseUploadResponse(d, 200, {}, body);
    QCOMPARE(r.url, expectedUrl); QCOMPARE(r.code, expectedCode);
  }
  // CORE-020: XML templates read absolute element paths only. A DTD or an
  // entity reference is refused instead of being expanded.
  void xmlTemplates_data() {
    QTest::addColumn<QString>("urlTemplate"); QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expectedUrl"); QTest::addColumn<QString>("expectedCode");
    QTest::newRow("element-path") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url>https://example.com/c</url></response>")
        << QStringLiteral("https://example.com/c") << QString();
    QTest::newRow("nested-path") << QStringLiteral("{xml:/a/b/c}")
        << QByteArray("<a><b><c>https://example.com/n</c></b></a>")
        << QStringLiteral("https://example.com/n") << QString();
    QTest::newRow("missing-element") << QStringLiteral("{xml:/response/missing}")
        << QByteArray("<response><url>https://example.com/c</url></response>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("relative-path") << QStringLiteral("{xml:response/url}")
        << QByteArray("<response><url>https://example.com/c</url></response>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("invalid-xml") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url>") << QString() << QStringLiteral("parse-failed");
    QTest::newRow("unclosed-root") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url>https://example.com/c</url>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("child-element") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url><a>x</a></url></response>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("empty-element") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url/></response>") << QString() << QStringLiteral("parse-failed");
    QTest::newRow("whitespace-text") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url>\n  https://example.com/c\n</url></response>")
        << QStringLiteral("https://example.com/c") << QString();
    QTest::newRow("dtd-internal-entity") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<!DOCTYPE r [<!ENTITY x \"EXPANDED-SECRET\">]><response><url>&x;</url></response>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("dtd-external-entity") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<!DOCTYPE r [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><response><url>&x;</url></response>")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("undeclared-entity") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response><url>&undefined;</url></response>")
        << QString() << QStringLiteral("parse-failed");
    // Namespaces are not resolved: a default namespace keeps the local name, a
    // prefixed element only matches its qualified name.
    QTest::newRow("default-namespace") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<response xmlns=\"http://example.com\"><url>https://example.com/c</url></response>")
        << QStringLiteral("https://example.com/c") << QString();
    QTest::newRow("prefixed-namespace") << QStringLiteral("{xml:/response/url}")
        << QByteArray("<r:response xmlns:r=\"http://example.com\"><r:url>https://example.com/c</r:url></r:response>")
        << QString() << QStringLiteral("parse-failed");
  }
  void xmlTemplates() {
    QFETCH(QString, urlTemplate); QFETCH(QByteArray, body); QFETCH(QString, expectedUrl); QFETCH(QString, expectedCode);
    UploadDestination d; d.resultUrl = urlTemplate;
    const auto r = parseUploadResponse(d, 200, {}, body);
    QCOMPARE(r.url, expectedUrl); QCOMPARE(r.code, expectedCode);
  }
  void xmlEntityIsNotExpanded() {
    UploadDestination d; d.resultUrl = QStringLiteral("{xml:/response/url}");
    const auto internal = parseUploadResponse(d, 200, {},
        QByteArray("<!DOCTYPE r [<!ENTITY x \"EXPANDED-SECRET\">]><response><url>&x;</url></response>"));
    QCOMPARE(internal.code, QStringLiteral("parse-failed"));
    QVERIFY(internal.url.isEmpty()); QVERIFY(!internal.url.contains("EXPANDED-SECRET"));
    QVERIFY(!internal.thumbnailUrl.contains("EXPANDED-SECRET"));
    QVERIFY(!internal.deletionUrl.contains("EXPANDED-SECRET"));
    QVERIFY(!internal.errorMessage.contains("EXPANDED-SECRET"));
    const auto external = parseUploadResponse(d, 200, {},
        QByteArray("<!DOCTYPE r [<!ENTITY x SYSTEM \"file:///etc/passwd\">]><response><url>&x;</url></response>"));
    QCOMPARE(external.code, QStringLiteral("parse-failed"));
    QVERIFY(external.url.isEmpty()); QVERIFY(!external.url.contains("root:"));
  }
  // CORE-020: regex templates select a numbered capture group of the response.
  void regexTemplates_data() {
    QTest::addColumn<QString>("urlTemplate"); QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expectedUrl"); QTest::addColumn<QString>("expectedCode");
    QTest::newRow("group-zero") << QStringLiteral("{regex:(https://[^ ]+)|0}")
        << QByteArray("url https://example.com/a end")
        << QStringLiteral("https://example.com/a") << QString();
    QTest::newRow("group-one") << QStringLiteral("https://example.com/{regex:(x{2})|1}")
        << QByteArray("xx") << QStringLiteral("https://example.com/xx") << QString();
    // A named capture group is usable through its number; the group selector
    // itself only accepts digits, so a name leaves the token unexpanded.
    QTest::newRow("named-group-by-number") << QStringLiteral("https://example.com/{regex:(?<code>[A-Z]{3})|1}")
        << QByteArray("abc ABC") << QStringLiteral("https://example.com/ABC") << QString();
    QTest::newRow("named-group-by-name") << QStringLiteral("{regex:(?<code>[A-Z]{3})|code}")
        << QByteArray("abc ABC") << QStringLiteral("{regex:(?<code>[A-Z]{3})|code}")
        << QStringLiteral("invalid-result-url");
    QTest::newRow("no-match") << QStringLiteral("{regex:(zzz)|1}") << QByteArray("abc")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("invalid-regex") << QStringLiteral("{regex:([a-z|1}") << QByteArray("abc")
        << QString() << QStringLiteral("parse-failed");
    QTest::newRow("group-out-of-range") << QStringLiteral("{regex:(abc)|3}") << QByteArray("abc")
        << QString() << QStringLiteral("parse-failed");
  }
  void regexTemplates() {
    QFETCH(QString, urlTemplate); QFETCH(QByteArray, body); QFETCH(QString, expectedUrl); QFETCH(QString, expectedCode);
    UploadDestination d; d.resultUrl = urlTemplate;
    const auto r = parseUploadResponse(d, 200, {}, body);
    QCOMPARE(r.url, expectedUrl); QCOMPARE(r.code, expectedCode);
  }
  void catastrophicRegexReturns() {
    UploadDestination d; d.resultUrl = QStringLiteral("{regex:(a|aa)+$|0}");
    const auto body = QByteArray(4096, 'a') + QByteArray("!");
    QElapsedTimer timer; timer.start();
    const auto r = parseUploadResponse(d, 200, {}, body);
    const auto elapsed = timer.elapsed();
    QVERIFY2(elapsed < 2000, qPrintable(QStringLiteral("catastrophic regex took %1 ms").arg(elapsed)));
    QCOMPARE(r.code, QStringLiteral("parse-failed")); QVERIFY(r.url.isEmpty());
  }
  // CORE-020: header templates match the header name case-insensitively.
  void headerTemplates_data() {
    QTest::addColumn<QString>("urlTemplate"); QTest::addColumn<QByteArray>("headerName");
    QTest::addColumn<QByteArray>("headerValue"); QTest::addColumn<QString>("expectedUrl");
    QTest::addColumn<QString>("expectedCode");
    QTest::newRow("name-upper-case") << QStringLiteral("{header:x-token}") << QByteArray("X-Token")
        << QByteArray("https://example.com/h") << QStringLiteral("https://example.com/h") << QString();
    QTest::newRow("name-lower-case") << QStringLiteral("{header:X-TOKEN}") << QByteArray("x-token")
        << QByteArray("https://example.com/h") << QStringLiteral("https://example.com/h") << QString();
    QTest::newRow("missing-header") << QStringLiteral("{header:Missing}") << QByteArray("X-Token")
        << QByteArray("https://example.com/h") << QString() << QStringLiteral("parse-failed");
  }
  void headerTemplates() {
    QFETCH(QString, urlTemplate); QFETCH(QByteArray, headerName); QFETCH(QByteArray, headerValue);
    QFETCH(QString, expectedUrl); QFETCH(QString, expectedCode);
    UploadDestination d; d.resultUrl = urlTemplate;
    const auto r = parseUploadResponse(d, 200, {{headerName, headerValue}}, {});
    QCOMPARE(r.url, expectedUrl); QCOMPARE(r.code, expectedCode);
  }
  void repeatedHeaderTakesFirstValue() {
    UploadDestination d; d.resultUrl = QStringLiteral("{header:Location}");
    const auto r = parseUploadResponse(d, 200,
        {{"Location", "https://example.com/first"}, {"location", "https://example.com/second"}}, {});
    QCOMPARE(r.url, QStringLiteral("https://example.com/first")); QCOMPARE(r.code, QString());
  }
  // CORE-020: combined templates, result URL scheme validation and HTTP errors.
  void combinedTemplates_data() {
    QTest::addColumn<QString>("urlTemplate"); QTest::addColumn<QString>("thumbnailTemplate");
    QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("status");
    QTest::addColumn<QString>("errorTemplate"); QTest::addColumn<QString>("expectedUrl");
    QTest::addColumn<QString>("expectedCode"); QTest::addColumn<QString>("expectedError");
    QTest::newRow("literal-and-two-placeholders")
        << QStringLiteral("https://example.com/{json:data.id}/{json:data.name}.png") << QString()
        << QByteArray("{\"data\":{\"id\":42,\"name\":\"shot\"}}") << 200 << QString()
        << QStringLiteral("https://example.com/42/shot.png") << QString() << QString();
    QTest::newRow("response-trimmed") << QStringLiteral("{response}") << QString()
        << QByteArray("  https://example.com/a\n") << 200 << QString()
        << QStringLiteral("https://example.com/a") << QString() << QString();
    QTest::newRow("expanded-javascript-url") << QStringLiteral("{json:url}") << QString()
        << QByteArray("{\"url\":\"javascript:alert(1)\"}") << 200 << QString()
        << QStringLiteral("javascript:alert(1)") << QStringLiteral("invalid-result-url") << QString();
    QTest::newRow("expanded-file-url") << QStringLiteral("{json:url}") << QString()
        << QByteArray("{\"url\":\"file:///tmp/a\"}") << 200 << QString()
        << QStringLiteral("file:///tmp/a") << QStringLiteral("invalid-result-url") << QString();
    QTest::newRow("expanded-thumbnail-javascript") << QStringLiteral("{json:url}")
        << QStringLiteral("{json:t}") << QByteArray("{\"url\":\"https://example.com/ok\",\"t\":\"javascript:alert(1)\"}")
        << 200 << QString() << QStringLiteral("https://example.com/ok")
        << QStringLiteral("invalid-result-url") << QString();
    QTest::newRow("http-error-template") << QStringLiteral("{json:url}") << QString()
        << QByteArray("{\"url\":\"https://example.com/a\",\"error\":\"upload failed\"}") << 500
        << QStringLiteral("{json:error}") << QString() << QStringLiteral("http-error")
        << QStringLiteral("HTTP 500: upload failed");
    QTest::newRow("http-error-trims-template") << QStringLiteral("{json:url}") << QString()
        << QByteArray("{\"url\":\"https://example.com/a\",\"error\":\"upload failed\"}") << 503
        << QStringLiteral("  {json:error}  ") << QString() << QStringLiteral("http-error")
        << QStringLiteral("HTTP 503: upload failed");
    QTest::newRow("http-error-falls-back-to-body") << QStringLiteral("{json:url}") << QString()
        << QByteArray("server said no") << 500 << QString() << QString()
        << QStringLiteral("http-error") << QStringLiteral("HTTP 500: server said no");
  }
  void combinedTemplates() {
    QFETCH(QString, urlTemplate); QFETCH(QString, thumbnailTemplate); QFETCH(QByteArray, body);
    QFETCH(int, status); QFETCH(QString, errorTemplate); QFETCH(QString, expectedUrl);
    QFETCH(QString, expectedCode); QFETCH(QString, expectedError);
    UploadDestination d; d.resultUrl = urlTemplate; d.thumbnailUrl = thumbnailTemplate;
    d.errorMessage = errorTemplate;
    const auto r = parseUploadResponse(d, status, {}, body);
    QCOMPARE(r.url, expectedUrl); QCOMPARE(r.code, expectedCode); QCOMPARE(r.errorMessage, expectedError);
  }
  void largeBodyIsParsed() {
    QByteArray body = QByteArray("{\"url\":\"https://example.com/big\",\"pad\":\"");
    body += QByteArray(int(4 * 1024 * 1024 - body.size() - 2), 'x');
    body += QByteArray("\"}");
    QCOMPARE(body.size(), qsizetype(4 * 1024 * 1024));
    UploadDestination d; d.resultUrl = QStringLiteral("{json:url}");
    const auto r = parseUploadResponse(d, 200, {}, body);
    QCOMPARE(r.code, QString()); QCOMPARE(r.url, QStringLiteral("https://example.com/big"));
  }
};
QTEST_GUILESS_MAIN(UploadResponseTest)
#include "UploadResponseTest.moc"
