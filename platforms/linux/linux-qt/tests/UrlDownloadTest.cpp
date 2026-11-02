// EC-003 network worker against a loopback server (plain http via per-request opt-in).
#include "UrlDownload.h"
#include "UrlTestServer.h"
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace { QString code(const char16_t *c) { return QString::fromUtf16(c); } }
class UrlDownloadTest : public QObject {
  Q_OBJECT
  UrlTestServer *server = nullptr; QNetworkAccessManager *nam = nullptr;
  UrlDownloadResult run(const QString &path, DownloadLimits limits = {}, bool optIn = true) {
    UrlDownload job(nam); QSignalSpy done(&job, &UrlDownload::finished);
    job.start(QUrl(server->base() + path), optIn, limits);
    if (!done.wait(10000)) return {{}, "test-timeout", {}, 0};
    return qvariant_cast<UrlDownloadResult>(done.first().first());
  }
private slots:
  void initTestCase() { server = new UrlTestServer(this); QVERIFY(server->ok()); nam = new QNetworkAccessManager(this); }
  void init() { server->requests = 0; server->paths.clear(); server->streamed = 0; server->streamClosed = false; server->slowOpened = false; }
  void downloadsBytesInMemory() {
    const auto r = run("/ok.png"); QCOMPARE(r.diagnostic, QString()); QCOMPARE(r.bytes, server->png()); QCOMPARE(r.redirects, 0);
  }
  void contentTypeIsNotTrusted() {  // the worker returns bytes; validation is the decoder's job
    const auto r = run("/fake.png"); QCOMPARE(r.diagnostic, QString()); QVERIFY(r.bytes.startsWith("<html>"));
  }
  void httpStatusAndEmpty() {
    QCOMPARE(run("/missing").diagnostic, code(urlDiagnostic::httpStatus));
    QCOMPARE(run("/empty").diagnostic, code(urlDiagnostic::emptyResponse));
  }
  void redirectsFollowedUpToLimit() {
    auto r = run("/r/4"); QCOMPARE(r.diagnostic, QString()); QCOMPARE(r.redirects, 5); QCOMPARE(r.bytes, server->png());
    QCOMPARE(r.finalUrl.path(), QString("/ok.png"));
  }
  void tooManyRedirects() {
    init(); QCOMPARE(run("/r/5").diagnostic, code(urlDiagnostic::tooManyRedirects)); QCOMPARE(server->requests, 6);
    init(); QCOMPARE(run("/loop").diagnostic, code(urlDiagnostic::tooManyRedirects)); QCOMPARE(server->requests, 6);
  }
  void badRedirectTargetsRefused() {
    QCOMPARE(run("/to-ftp").diagnostic, code(urlDiagnostic::badRedirect));
    QCOMPARE(run("/to-userinfo").diagnostic, code(urlDiagnostic::badRedirect));
    QCOMPARE(server->requests, 2);  // never followed
  }
  void redirectToHttpsFromOptedInHttpIsAllowedByPolicy() {
    // http -> https is a permitted upgrade; the (unreachable) target then fails as a network error.
    QCOMPARE(run("/to-https").diagnostic, code(urlDiagnostic::network));
  }
  void plainHttpRedirectWithoutOptInRefused() {
    // Defensive: even if started with opt-in off, an http target is never followed.
    UrlDownload job(nam); QSignalSpy done(&job, &UrlDownload::finished);
    job.start(QUrl(server->base() + "/r/1"), false, {}); QVERIFY(done.wait(10000));
    QCOMPARE(qvariant_cast<UrlDownloadResult>(done.first().first()).diagnostic, code(urlDiagnostic::downgrade));
  }
  void sizeCapAbortsMidStream() {
    DownloadLimits limits; limits.maxBytes = 1024 * 1024;
    const auto r = run("/big", limits);
    QCOMPARE(r.diagnostic, code(urlDiagnostic::tooLarge)); QVERIFY(r.bytes.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(server->streamClosed, 5000);  // the transfer really stopped
    QVERIFY2(server->streamed < server->plannedBig / 4, qPrintable(QString::number(server->streamed)));
  }
  void sizeCapHonoursDeclaredLength() {
    DownloadLimits limits; limits.maxBytes = 50000;  // /slow declares 100000
    QCOMPARE(run("/slow", limits).diagnostic, code(urlDiagnostic::tooLarge));
  }
  void cancelMidDownload() {
    UrlDownload job(nam); QSignalSpy done(&job, &UrlDownload::finished);
    job.start(QUrl(server->base() + "/slow"), true, {});
    QTRY_VERIFY_WITH_TIMEOUT(server->slowOpened, 5000); QVERIFY(job.running());
    job.cancel();
    QVERIFY(done.wait(5000) || done.count() == 1); QCOMPARE(done.count(), 1);
    const auto r = qvariant_cast<UrlDownloadResult>(done.first().first());
    QCOMPARE(r.diagnostic, code(urlDiagnostic::cancelled)); QVERIFY(r.bytes.isEmpty()); QVERIFY(!job.running());
    QTRY_VERIFY_WITH_TIMEOUT(server->streamClosed, 5000);
  }
  void destroyingTheJobAbortsTheTransfer() {
    auto *job = new UrlDownload(nam); job->start(QUrl(server->base() + "/slow"), true, {});
    QTRY_VERIFY_WITH_TIMEOUT(server->slowOpened, 5000); delete job;
    QTRY_VERIFY_WITH_TIMEOUT(server->streamClosed, 5000);
  }
  void stalledServerTimesOut() {
    DownloadLimits limits; limits.timeoutMs = 400;
    QCOMPARE(run("/slow", limits).diagnostic, code(urlDiagnostic::timeout));
  }
  void unreachableHostIsValueFreeNetworkFailure() {
    UrlDownload job(nam); QSignalSpy done(&job, &UrlDownload::finished);
    job.start(QUrl("http://127.0.0.1:1/x.png?secret=1"), true, {}); QVERIFY(done.wait(10000));
    const auto r = qvariant_cast<UrlDownloadResult>(done.first().first());
    QCOMPARE(r.diagnostic, code(urlDiagnostic::network)); QVERIFY(!r.diagnostic.contains("secret"));
  }
};
QTEST_MAIN(UrlDownloadTest)
#include "UrlDownloadTest.moc"
