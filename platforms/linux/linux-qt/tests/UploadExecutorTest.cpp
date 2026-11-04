// CORE-020, CORE-021, COMPCAT-011: loopback-only HTTP fixture.
#include "uploads/UploadExecutor.h"
#include "actions/PipelineResultJson.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <functional>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <cstring>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTest>
using namespace xerahs::uploads;
namespace {
class FixtureReply : public QNetworkReply {
public:
  FixtureReply(const QNetworkRequest &r, const QString &mode, QObject *parent, std::function<void()> onAbort = {}) : QNetworkReply(parent), m_onAbort(std::move(onAbort)) {
    setRequest(r); setUrl(r.url()); open(QIODevice::ReadOnly);
    if (mode == QLatin1String("timeout") || mode == QLatin1String("cancel")) return;
    QTimer::singleShot(0, this, [this, mode] {
      setAttribute(QNetworkRequest::HttpStatusCodeAttribute, mode == QLatin1String("http") ? 500 : mode == QLatin1String("forbidden") ? 403 : mode == QLatin1String("rate") ? 429 : 200);
      m_bytes = (mode == QLatin1String("oversize") || mode == QLatin1String("late-oversize")) ? QByteArray(4 * 1024 * 1024 + 1, 'x') :
          mode == QLatin1String("parse") ? QByteArray("invalid result") : mode == QLatin1String("http") ? QByteArray("secret-value") : QByteArray("https://example.com/result");
      if (mode == QLatin1String("network")) setError(QNetworkReply::RemoteHostClosedError, QStringLiteral("closed secret-value"));
      if (mode == QLatin1String("auth")) setError(QNetworkReply::AuthenticationRequiredError, QStringLiteral("denied"));
      if (mode != QLatin1String("late-oversize")) emit readyRead();
      if (!isFinished()) { setFinished(true); emit finished(); }
    });
  }
  void abort() override {
    if (isFinished()) return;
    setFinished(true); setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
    emit errorOccurred(error()); emit finished();
    const auto observer = m_onAbort; if (observer) observer();
  }
  qint64 bytesAvailable() const override { return m_bytes.size() + QNetworkReply::bytesAvailable(); }
protected:
  qint64 readData(char *data, qint64 max) override {
    const qint64 count = qMin(max, qint64(m_bytes.size()));
    if (!count) return -1;
    std::memcpy(data, m_bytes.constData(), size_t(count)); m_bytes.remove(0, count); return count;
  }
private:
  QByteArray m_bytes;
  std::function<void()> m_onAbort;
};
class FixtureManager : public QNetworkAccessManager {
public:
  QString mode;
  bool safeRedirect = false;
  QStringList modes;
  QVector<QNetworkRequest> requests;
  QVector<QByteArray> bodies;
  QSet<QByteArray> publishedKeys;
  std::function<void()> onAbort;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &r, QIODevice *body) override {
    requests.append(r); bodies.append(body ? body->readAll() : QByteArray{});
    if (!r.rawHeader("Idempotency-Key").isEmpty()) publishedKeys.insert(r.rawHeader("Idempotency-Key"));
    safeRedirect = r.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::NoLessSafeRedirectPolicy;
    return new FixtureReply(r, modes.isEmpty() ? mode : modes.takeFirst(), this, onAbort);
  }
};
}
class UploadExecutorTest : public QObject {
  Q_OBJECT
private slots:
  void lifecycle() {
    // Synchronous abort signals must not cause a second completion.
    const QStringList modes = {"success", "http", "network", "oversize", "timeout", "cancel"};
    const QStringList codes = {"", "http-error", "network-error", "response-too-large", "timeout", "cancelled"};
    for (int i = 0; i < modes.size(); ++i) {
      FixtureManager manager; manager.mode = modes.at(i);
      UploadExecutor executor(&manager, nullptr, 20);
      PreparedUploadRequest request; request.url = QUrl(QStringLiteral("https://example.com/upload"));
      request.method = "POST"; request.sensitiveValues = {QStringLiteral("secret-value")};
      QSignalSpy done(&executor, &UploadExecutor::finished);
      executor.start(request);
      if (manager.mode == QLatin1String("cancel")) executor.cancel();
      QTRY_COMPARE(done.size(), 1);
      const auto out = qvariant_cast<UploadOutcome>(done.first().first());
      QCOMPARE(out.code, codes.at(i)); QVERIFY(manager.safeRedirect);
      QVERIFY(!out.errorMessage.contains(QStringLiteral("secret-value")));
      if (i == 1 || i == 2) QVERIFY(out.errorMessage.contains(QStringLiteral("[redacted]")));
      executor.cancel(); QTest::qWait(25); QCOMPARE(done.size(), 1);
    }
    UploadExecutor missing(nullptr); PreparedUploadRequest request;
    QSignalSpy done(&missing, &UploadExecutor::finished);
    missing.start(request); missing.cancel(); missing.start(request);
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(qvariant_cast<UploadOutcome>(done.at(0).first()).code, QStringLiteral("cancelled"));
    QCOMPARE(qvariant_cast<UploadOutcome>(done.at(1).first()).code, QStringLiteral("network-error"));
  }
  void reentrantRestart() {
    FixtureManager manager; manager.mode = QStringLiteral("late-oversize");
    UploadExecutor executor(&manager);
    PreparedUploadRequest request; request.url = QUrl(QStringLiteral("https://example.com/upload")); request.method = "POST";
    QSignalSpy done(&executor, &UploadExecutor::finished);
    connect(&executor, &UploadExecutor::finished, this, [&](const UploadOutcome &out) {
      if (out.code == QLatin1String("response-too-large")) {
        manager.mode = QStringLiteral("success"); executor.start(request);
      }
    });
    executor.start(request);
    QTRY_COMPARE(done.size(), 2);
    QCOMPARE(qvariant_cast<UploadOutcome>(done.at(0).first()).code, QStringLiteral("response-too-large"));
    QVERIFY(qvariant_cast<UploadOutcome>(done.at(1).first()).code.isEmpty());
    QTest::qWait(25); QCOMPARE(done.size(), 2);
  }
  void safeRetriesKeepPreparedBytesKeyAndOneRemoteObject_data() {
    QTest::addColumn<QByteArray>("method"); QTest::addColumn<QString>("firstFailure");
    for (const auto &method : {QByteArray("POST"), QByteArray("PATCH"), QByteArray("PUT"), QByteArray("DELETE"), QByteArray("GET"), QByteArray("HEAD")})
      for (const auto *failure : {"network", "http", "timeout"})
        QTest::newRow(qPrintable(QString::fromLatin1(method) + '-' + failure)) << method << QString(failure);
  }
  void safeRetriesKeepPreparedBytesKeyAndOneRemoteObject() {
    QFETCH(QByteArray, method); QFETCH(QString, firstFailure);
    UploadDestination destination; destination.id = QUuid::createUuid(); destination.name = "Retry fixture";
    destination.requestUrl = "https://example.test/upload"; destination.body = BodyType::Binary;
    destination.headers = {{"Idempotency-Key", "stable-operation-key"}, {"Authorization", "{secret:token}"}};
    MemoryCredentialStore credentials; credentials.store(destination.id, "token", "private-token");
    UploadInput input; input.fileName = "capture.png"; input.fileBytes = QByteArray("pixels\0data", 11);
    auto prepared = buildUploadRequest(destination, input, credentials); QVERIFY(prepared.request);
    prepared.request->method = method;
    const auto frozen = *prepared.request;
    FixtureManager manager; manager.modes = {firstFailure, "success"};
    UploadExecutor executor(&manager, nullptr, 20); QSignalSpy done(&executor, &UploadExecutor::finished);
    QSignalSpy retry(&executor, &UploadExecutor::retryScheduled), progress(&executor, &UploadExecutor::progress);
    QElapsedTimer elapsed; elapsed.start(); executor.start(*prepared.request);
    prepared.request->body = "caller changed bytes"; prepared.request->headers.clear();
    executor.start(*prepared.request); // Admission remains held during the entire logical transfer.
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 2000);
    const auto outcome = done.first().first().value<UploadOutcome>();
    QVERIFY(outcome.code.isEmpty()); QCOMPARE(outcome.url, QString("https://example.com/result"));
    QCOMPARE(outcome.attempts, 2); QCOMPARE(manager.requests.size(), 2); QCOMPARE(retry.size(), 1);
    QCOMPARE(retry[0][0].toInt(), 2); QCOMPARE(retry[0][1].toInt(), 250); QVERIFY(elapsed.elapsed() >= 200);
    QCOMPARE(manager.publishedKeys.size(), 1); QVERIFY(progress.size() >= 2);
    for (int i = 0; i < 2; ++i) {
      QCOMPARE(manager.bodies[i], frozen.body); QCOMPARE(manager.requests[i].url(), frozen.url);
      QCOMPARE(manager.requests[i].rawHeader("Idempotency-Key"), QByteArray("stable-operation-key"));
      QCOMPARE(manager.requests[i].rawHeader("Authorization"), QByteArray("private-token"));
    }
    xerahs::actions::PipelineResult result;
    result.upload = xerahs::actions::UploadResult{outcome.url, {}, {}, {}, outcome.attempts};
    const auto report = xerahs::actions::pipelineResultJson(result).value("extensions").toObject()
        .value("com.xerahs.app").toObject().value("upload_result").toObject();
    QCOMPARE(report.value("attempts").toInt(), 2); QCOMPARE(report.value("url").toString(), outcome.url);
    QVERIFY(!report.contains("Authorization"));
  }
  void exhaustedRetriesAreBoundedAndBackoffIncreases() {
    FixtureManager manager; manager.mode = "http";
    UploadExecutor executor(&manager); QSignalSpy done(&executor, &UploadExecutor::finished), retries(&executor, &UploadExecutor::retryScheduled);
    PreparedUploadRequest request; request.url = QUrl("https://example.test/upload"); request.method = "PUT";
    request.sensitiveValues = {"secret-value"}; executor.start(request);
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 2500);
    QCOMPARE(manager.requests.size(), 3); QCOMPARE(retries.size(), 2);
    QCOMPARE(retries[0][1].toInt(), 250); QCOMPARE(retries[1][1].toInt(), 500);
    const auto outcome = done.first().first().value<UploadOutcome>();
    QCOMPARE(outcome.attempts, 3); QCOMPARE(outcome.code, QString("http-error")); QVERIFY(outcome.url.isEmpty());
    QVERIFY(!outcome.errorMessage.contains("secret-value"));
    QTest::qWait(550); QCOMPARE(done.size(), 1); QCOMPARE(manager.requests.size(), 3);
  }
  void unsafeOrPermanentFailuresNeverRetry_data() {
    QTest::addColumn<QByteArray>("method"); QTest::addColumn<QString>("mode"); QTest::addColumn<QVector<Field>>("headers");
    for (const auto &method : {QByteArray("POST"), QByteArray("PATCH")})
      QTest::newRow(qPrintable(QString::fromLatin1(method))) << method << QString("network") << QVector<Field>{};
    QTest::newRow("blank-key") << QByteArray("POST") << QString("http") << QVector<Field>{{"Idempotency-Key", " "}};
    QTest::newRow("duplicate-key") << QByteArray("POST") << QString("http") << QVector<Field>{{"Idempotency-Key", "one"}, {"idempotency-key", "two"}};
    for (const auto *mode : {"forbidden", "rate", "parse", "oversize", "auth"})
      QTest::newRow(mode) << QByteArray("PUT") << QString(mode) << QVector<Field>{};
  }
  void unsafeOrPermanentFailuresNeverRetry() {
    QFETCH(QByteArray, method); QFETCH(QString, mode); QFETCH(QVector<Field>, headers);
    FixtureManager manager; manager.mode = mode; UploadExecutor executor(&manager);
    PreparedUploadRequest request; request.method = method; request.url = QUrl("https://example.test/upload"); request.headers = headers;
    QSignalSpy done(&executor, &UploadExecutor::finished), retries(&executor, &UploadExecutor::retryScheduled); executor.start(request);
    QTRY_COMPARE(done.size(), 1); QVERIFY(!done.first().first().value<UploadOutcome>().code.isEmpty());
    QCOMPARE(done.first().first().value<UploadOutcome>().attempts, 1); QVERIFY(retries.isEmpty()); QCOMPARE(manager.requests.size(), 1);
  }
  void cancellationDuringBackoffCannotRestartOrCompleteANewerRun() {
    FixtureManager manager; manager.mode = "network"; UploadExecutor executor(&manager);
    PreparedUploadRequest request; request.method = "PUT"; request.url = QUrl("https://example.test/upload");
    QSignalSpy done(&executor, &UploadExecutor::finished);
    connect(&executor, &UploadExecutor::retryScheduled, this, [&](int, int) { executor.cancel(); });
    connect(&executor, &UploadExecutor::finished, this, [&](const UploadOutcome &out) {
      if (out.code != "cancelled") return;
      QCOMPARE(out.attempts, 1); manager.mode = "success"; executor.start(request);
    });
    executor.start(request); QTRY_COMPARE(done.size(), 2);
    QCOMPARE(done[0][0].value<UploadOutcome>().code, QString("cancelled"));
    QVERIFY(done[1][0].value<UploadOutcome>().code.isEmpty()); QCOMPARE(done[1][0].value<UploadOutcome>().attempts, 1);
    QTest::qWait(800); QCOMPARE(manager.requests.size(), 2); QCOMPARE(done.size(), 2);
  }
  void abortObserversCannotOverwriteANewerRunOrEscapeAdmission() {
    for (bool cancelDuringRetry : {false, true}) {
      FixtureManager manager; manager.mode = "timeout";
      UploadExecutor executor(&manager, nullptr, 10);
      PreparedUploadRequest first; first.method = "PUT"; first.url = QUrl("https://example.test/first"); first.body = "first";
      auto second = first; second.url = QUrl("https://example.test/second"); second.body = "second";
      QSignalSpy done(&executor, &UploadExecutor::finished);
      bool aborted = false;
      manager.onAbort = [&] {
        aborted = true;
        executor.cancel();
        executor.start(second); // Ignored during terminal cleanup, admitted only after finished.
      };
      connect(&executor, &UploadExecutor::finished, this, [&](const UploadOutcome &out) {
        if (out.code != "cancelled") return;
        manager.mode = "success"; executor.start(second);
      });
      executor.start(first);
      if (!cancelDuringRetry) executor.cancel();
      QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 2000);
      QVERIFY(aborted); QCOMPARE(done[0][0].value<UploadOutcome>().code, QString("cancelled"));
      QVERIFY(done[1][0].value<UploadOutcome>().code.isEmpty());
      QCOMPARE(manager.requests.size(), 2); QCOMPARE(manager.requests[1].url(), second.url); QCOMPARE(manager.bodies[1], second.body);
      QTest::qWait(550); QCOMPARE(done.size(), 2); QCOMPARE(manager.requests.size(), 2);
    }
  }
  void attemptMetadataDoesNotInventPublicationAndLegacyReceiptsStayUnchanged() {
    xerahs::actions::PipelineResult result;
    result.upload = xerahs::actions::UploadResult{{}, {}, {}, "network-error", 3};
    const auto report = xerahs::actions::pipelineResultJson(result).value("extensions").toObject()
        .value("com.xerahs.app").toObject().value("upload_result").toObject();
    QCOMPARE(report.value("attempts").toInt(), 3); QVERIFY(!report.contains("url"));
    QVERIFY(xerahs::actions::pipelineObservationJson(result).value("outputs").toArray().isEmpty());
    result.upload->attempts = 0;
    const auto legacy = xerahs::actions::pipelineResultJson(result).value("extensions").toObject()
        .value("com.xerahs.app").toObject().value("upload_result").toObject();
    QVERIFY(!legacy.contains("attempts"));
  }
  void transfers_data() {
    QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("code");
    QTest::newRow("success") << QStringLiteral("success") << QString();
    QTest::newRow("http") << QStringLiteral("http") << QStringLiteral("http-error");
    QTest::newRow("timeout") << QStringLiteral("timeout") << QStringLiteral("timeout");
    QTest::newRow("cancel") << QStringLiteral("cancel") << QStringLiteral("cancelled");
    QTest::newRow("oversize") << QStringLiteral("oversize") << QStringLiteral("response-too-large");
    QTest::newRow("network") << QStringLiteral("network") << QStringLiteral("network-error");
  }
  void transfers() {
    QFETCH(QString, mode); QFETCH(QString, code);
    QTcpServer server; QVERIFY2(server.listen(QHostAddress::LocalHost), qPrintable(server.errorString()));
    QByteArray wire; bool replied = false; QPointer<QTcpSocket> peer;
    connect(&server, &QTcpServer::newConnection, this, [&] {
      peer = server.nextPendingConnection();
      connect(peer, &QTcpSocket::readyRead, this, [&] {
        wire += peer->readAll();
        const auto split = wire.indexOf("\r\n\r\n"); if (split < 0 || replied) return;
        const QRegularExpression length(QStringLiteral("Content-Length: (\\d+)"), QRegularExpression::CaseInsensitiveOption);
        const auto match = length.match(QString::fromLatin1(wire.left(split)));
        if (!match.hasMatch() || wire.size() < split + 4 + match.captured(1).toInt()) return;
        replied = true;
        if (mode == QLatin1String("timeout") || mode == QLatin1String("cancel")) return;
        if (mode == QLatin1String("network")) {
          peer->write("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n");
          peer->disconnectFromHost(); return;
        }
        const QByteArray body = mode == QLatin1String("oversize") ? QByteArray(4 * 1024 * 1024 + 1, 'x') :
            mode == QLatin1String("http") ? QByteArray("server failed secret-value") : QByteArray("https://example.com/result");
        peer->write("HTTP/1.1 " + QByteArray(mode == QLatin1String("http") ? "500 Error" : "200 OK") +
                    "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        peer->disconnectFromHost();
      });
    });
    UploadDestination d; d.id = QUuid::createUuid(); d.name = QStringLiteral("local");
    d.requestUrl = QStringLiteral("http://127.0.0.1:%1/upload").arg(server.serverPort());
    d.body = BodyType::MultipartFormData; d.fileFormName = QStringLiteral("file"); d.arguments = {{"title", "test title"}};
    UploadInput input; input.fileName = QStringLiteral("shot.png"); input.fileBytes = QByteArray("PNG\0data", 8); input.mimeType = QStringLiteral("image/png");
    MemoryCredentialStore credentials;
    credentials.store(d.id, QStringLiteral("token"), QStringLiteral("secret-value"));
    d.headers = {{"Authorization", "{secret:token}"}};
    const auto built = buildUploadRequest(d, input, credentials); QVERIFY(built.request);
    QNetworkAccessManager manager; UploadExecutor executor(&manager, nullptr, mode == QLatin1String("timeout") ? 100 : 5000);
    QSignalSpy done(&executor, &UploadExecutor::finished); QSignalSpy progress(&executor, &UploadExecutor::progress);
    executor.start(*built.request);
    QTRY_VERIFY_WITH_TIMEOUT(replied, 3000);
    if (mode == QLatin1String("cancel")) { executor.cancel(); executor.cancel(); }
    QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 7000);
    const auto out = qvariant_cast<UploadOutcome>(done.first().first()); QCOMPARE(out.code, code);
    if (code.isEmpty()) QCOMPARE(out.url, QStringLiteral("https://example.com/result"));
    if (mode == QLatin1String("http")) {
      QVERIFY(out.errorMessage.contains(QStringLiteral("500")));
      QVERIFY(!out.errorMessage.contains(QStringLiteral("secret-value")));
      QVERIFY(out.errorMessage.contains(QStringLiteral("[redacted]")));
    }
    const auto split = wire.indexOf("\r\n\r\n"); QCOMPARE(wire.mid(split + 4), built.request->body);
    QVERIFY(wire.left(split).contains(built.request->contentType));
    QVERIFY(wire.mid(split + 4).contains("name=\"title\"\r\n\r\ntest title\r\n"));
    QVERIFY(wire.mid(split + 4).contains("name=\"file\"; filename=\"shot.png\"\r\nContent-Type: image/png\r\n\r\n" + input.fileBytes));
    QVERIFY(!progress.isEmpty());
    executor.cancel(); QTest::qWait(30); QCOMPARE(done.size(), 1);
    if (peer) { peer->disconnect(this); peer->deleteLater(); }
  }
};
QTEST_GUILESS_MAIN(UploadExecutorTest)
#include "UploadExecutorTest.moc"
