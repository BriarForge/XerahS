// CORE-020, CORE-021, COMPCAT-011: loopback-only HTTP fixture.
#include "uploads/UploadExecutor.h"
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
  FixtureReply(const QNetworkRequest &r, const QString &mode, QObject *parent) : QNetworkReply(parent) {
    setRequest(r); setUrl(r.url()); open(QIODevice::ReadOnly);
    if (mode == QLatin1String("timeout") || mode == QLatin1String("cancel")) return;
    QTimer::singleShot(0, this, [this, mode] {
      setAttribute(QNetworkRequest::HttpStatusCodeAttribute, mode == QLatin1String("http") ? 500 : 200);
      m_bytes = (mode == QLatin1String("oversize") || mode == QLatin1String("late-oversize")) ? QByteArray(4 * 1024 * 1024 + 1, 'x') :
          mode == QLatin1String("http") ? QByteArray("secret-value") : QByteArray("https://example.com/result");
      if (mode == QLatin1String("network")) setError(QNetworkReply::RemoteHostClosedError, QStringLiteral("closed secret-value"));
      if (mode != QLatin1String("late-oversize")) emit readyRead();
      if (!isFinished()) { setFinished(true); emit finished(); }
    });
  }
  void abort() override {
    if (isFinished()) return;
    setFinished(true); setError(QNetworkReply::OperationCanceledError, QStringLiteral("aborted"));
    emit errorOccurred(error()); emit finished();
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
};
class FixtureManager : public QNetworkAccessManager {
public:
  QString mode;
  bool safeRedirect = false;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &r, QIODevice *) override {
    safeRedirect = r.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::NoLessSafeRedirectPolicy;
    return new FixtureReply(r, mode, this);
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
