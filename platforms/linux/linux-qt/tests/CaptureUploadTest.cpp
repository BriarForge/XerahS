// CORE-020, CORE-021, POST-CAPTURE-ACTIONS-001: native capture to HTTP receipts.
#include "CapturePipeline.h"
#include "uploads/DestinationCatalog.h"
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::uploads;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::green); return image; }
QJsonObject uploadJson(const QJsonObject &report) {
  return report.value("extensions").toObject().value("com.xerahs.app").toObject().value("upload_result").toObject();
}
class HttpFixture : public QObject {
public:
  QTcpServer server;
  QByteArray wire;
  bool received = false;
  bool hold = false;
  int status = 200;
  HttpFixture() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      auto *peer = server.nextPendingConnection();
      connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
      connect(peer, &QTcpSocket::readyRead, this, [this, peer] {
        wire += peer->readAll(); const auto split = wire.indexOf("\r\n\r\n"); if (split < 0 || received) return;
        QRegularExpression length("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption);
        const auto match = length.match(QString::fromLatin1(wire.left(split)));
        if (!match.hasMatch() || wire.size() < split + 4 + match.captured(1).toInt()) return;
        received = true; if (hold) return;
        const QByteArray body = status == 500 ? QByteArray("failed fixture-secret") :
            QByteArray(R"({"url":"https://example.test/image","thumb":"https://example.test/thumb","delete":"https://example.test/delete"})");
        peer->write("HTTP/1.1 " + QByteArray::number(status) + " Reply\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        peer->disconnectFromHost();
      });
    });
  }
  UploadDestination destination() const {
    UploadDestination d; d.id = QUuid::createUuid(); d.name = "Fixture";
    d.requestUrl = QString("http://127.0.0.1:%1/upload").arg(server.serverPort());
    d.body = BodyType::MultipartFormData; d.fileFormName = "file";
    d.headers = {{"Authorization", "{secret:token}"}};
    d.resultUrl = "{json:url}"; d.thumbnailUrl = "{json:thumb}"; d.deletionUrl = "{json:delete}"; return d;
  }
};
}
class CaptureUploadTest : public QObject {
  Q_OBJECT
private slots:
  void transfer_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"memory", "saved", "missing-default", "missing-secret", "http-error", "cancel"}) QTest::newRow(mode) << QString(mode);
  }
  void transfer() {
    QFETCH(QString, mode);
    QTemporaryDir dir; HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    http.status = mode == "http-error" ? 500 : 200; http.hold = mode == "cancel";
    const auto destination = http.destination(); DestinationCatalog catalog(dir.filePath("destinations.json"));
    if (mode != "missing-default") QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    if (mode != "missing-secret") QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    CaptureActionPorts ports; ports.uploadCatalogPath = dir.filePath("destinations.json"); ports.uploadCredentials = credentials;
    bool worker = false; ports.actionStarted = [&](const QString &id) { if (id == "upload") worker = QThread::currentThread() != qApp->thread(); };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    QStringList selected{"upload"}; if (mode == "saved") selected.prepend("save");
    CapturePipeline pipeline(pixels(), dir.filePath("captures"), "fixture", selected, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start();
    if (mode == "cancel") { QTRY_VERIFY(http.received); pipeline.cancel(); }
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt); QVERIFY(worker);
    const auto &result = receipt->pipeline.orderedResults.back();
    if (mode == "memory" || mode == "saved") {
      QCOMPARE(result.state, ActionState::Succeeded); QVERIFY(receipt->pipeline.upload);
      const auto upload = uploadJson(receipt->canonicalResult()); QCOMPARE(upload.value("url").toString(), QString("https://example.test/image"));
      QCOMPARE(upload.value("thumbnail_url").toString(), QString("https://example.test/thumb"));
      QCOMPARE(upload.value("deletion_url").toString(), QString("https://example.test/delete"));
      QVERIFY(http.wire.contains("\x89PNG"));
      if (mode == "saved") {
        QVERIFY(receipt->file); QFile file(receipt->file->path); QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(http.wire.contains(file.readAll())); QVERIFY(http.wire.contains("filename=\"fixture.png\""));
      } else QVERIFY(!QFile::exists(dir.filePath("captures")));
    } else if (mode == "cancel") {
      QCOMPARE(result.state, ActionState::Cancelled); QVERIFY(!receipt->pipeline.upload);
    } else {
      QCOMPARE(result.state, ActionState::Failed); QVERIFY(receipt->pipeline.upload);
      if (mode == "missing-default") QCOMPARE(receipt->pipeline.upload->errorMessage, QString("No upload destination. Add one in Upload destinations."));
      if (mode == "missing-secret") QCOMPARE(result.diagnostic, std::optional<QString>("missing-secret"));
      if (mode == "http-error") { QCOMPARE(result.diagnostic, std::optional<QString>("http-error")); QVERIFY(receipt->pipeline.upload->errorMessage.contains("500")); }
      QVERIFY(!receipt->canonicalResult().isEmpty());
    }
    const auto report = QJsonDocument(receipt->canonicalResult()).toJson(); QVERIFY(!report.contains("fixture-secret"));
    const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(1)); QVERIFY(listing.entries.front().parsed.record);
    QCOMPARE(uploadJson(listing.entries.front().parsed.record->report), uploadJson(receipt->canonicalResult()));
  }
  // PCA-024: url-clipboard copies only the upload URL; skipped when upload fails; clipboard failure leaves upload intact.
  void urlClipboard_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"copied", "upload-failed", "clipboard-failed"}) QTest::newRow(mode) << QString(mode);
  }
  void urlClipboard() {
    QFETCH(QString, mode);
    QTemporaryDir dir; HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    http.status = mode == "upload-failed" ? 500 : 200;
    const auto destination = http.destination(); DestinationCatalog catalog(dir.filePath("destinations.json"));
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>(); QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    CaptureActionPorts ports; ports.uploadCatalogPath = dir.filePath("destinations.json"); ports.uploadCredentials = credentials;
    QGuiApplication::clipboard()->setText("before"); int calls = 0; bool onGui = false;
    ports.textClipboard = [&](const QString &text, const CancellationToken &) -> ActionOutcome {
      ++calls; onGui = QThread::currentThread() == qApp->thread();
      if (mode == "clipboard-failed") return {ActionState::Failed, QString("clipboard-unavailable"), {}};
      QGuiApplication::clipboard()->setText(text); return {};
    };
    CapturePipeline pipeline(pixels(), dir.filePath("captures"), "fixture", {"upload", "url-clipboard", "save"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt);
    const auto &results = receipt->pipeline.orderedResults; QCOMPARE(results.size(), size_t(3));
    QCOMPARE(results[0].action, QString("save")); QCOMPARE(results[0].state, ActionState::Succeeded);  // catalogue order: save precedes upload
    QCOMPARE(results[1].action, QString("upload")); QCOMPARE(results[2].action, QString("url-clipboard"));
    if (mode == "copied") {
      QCOMPARE(results[1].state, ActionState::Succeeded); QCOMPARE(results[2].state, ActionState::Succeeded);
      QVERIFY(onGui); QCOMPARE(calls, 1); QCOMPARE(QGuiApplication::clipboard()->text(), QString("https://example.test/image"));
    } else if (mode == "upload-failed") {
      QCOMPARE(results[1].state, ActionState::Failed); QCOMPARE(results[2].state, ActionState::Skipped);
      QCOMPARE(results[2].diagnostic, std::optional<QString>("dependency-unavailable")); QCOMPARE(results[2].missingArtifact, std::optional<QString>("upload.result"));
      QCOMPARE(calls, 0); QCOMPARE(QGuiApplication::clipboard()->text(), QString("before"));
    } else {
      QCOMPARE(results[1].state, ActionState::Succeeded); QVERIFY(receipt->pipeline.upload);
      QCOMPARE(receipt->pipeline.upload->url, QString("https://example.test/image"));
      QCOMPARE(results[2].state, ActionState::Failed); QCOMPARE(results[2].diagnostic, std::optional<QString>("clipboard-unavailable")); QCOMPARE(calls, 1);
    }
  }
  void oldRecordsLoadWithoutUploadFields() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.filePath("captures"), "old", {"save"}, nullptr, {}, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(1)); QVERIFY(listing.entries.front().parsed.record);
    QVERIFY(uploadJson(listing.entries.front().parsed.record->report).isEmpty());
  }
};
QTEST_MAIN(CaptureUploadTest)
#include "CaptureUploadTest.moc"
