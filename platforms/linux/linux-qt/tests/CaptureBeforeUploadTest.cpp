// CORE-020, CORE-021, POST-CAPTURE-ACTIONS-001: native capture to HTTP receipts.
#include "CapturePipeline.h"
#include "uploads/DestinationCatalog.h"
#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <atomic>
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::uploads;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::green); return image; }
QJsonObject uploadJson(const QJsonObject &report) {
  return report.value("extensions").toObject().value("com.xerahs.app").toObject().value("upload_result").toObject();
}
class ObservedCredentials : public CredentialStore {
public:
  mutable std::atomic<int> lookups{0};
  CredentialResult store(const QUuid &, const QString &, const QString &) override { return {}; }
  CredentialResult lookup(const QUuid &, const QString &) const override { ++lookups; return {CredentialStatus::Ok, "private-secret"}; }
  CredentialResult remove(const QUuid &, const QString &) override { return {}; }
  CredentialResult removeAll(const QUuid &) override { return {}; }
};
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

    d.resultUrl = "{json:url}"; d.thumbnailUrl = "{json:thumb}"; d.deletionUrl = "{json:delete}"; return d;
  }
};
}
class CaptureBeforeUploadTest : public QObject {
  Q_OBJECT
private slots:
  void confirmation_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"upload", "cancel", "escape", "close", "global-cancel", "frozen", "missing", "unavailable"})
      QTest::newRow(mode) << QString(mode);
  }
  void confirmation() {
    QFETCH(QString, mode);
    QTemporaryDir dir; HttpFixture http, redirected;
    QVERIFY(http.server.listen(QHostAddress::LocalHost)); QVERIFY(redirected.server.listen(QHostAddress::LocalHost));
    const auto catalogPath = dir.filePath("destinations.json");
    DestinationCatalog catalog(catalogPath);
    if (mode != "missing") QVERIFY(catalog.add(http.destination()).isEmpty());
    CaptureActionPorts ports; ports.uploadCatalogPath = catalogPath;
    ports.uploadCredentials = std::make_shared<MemoryCredentialStore>();
    if (mode == "unavailable") ports.beforeUpload = [](const CaptureUploadReview &, const CancellationToken &) {
      return ActionOutcome{ActionState::Failed, QString("upload-confirmation-unavailable"), {}};
    };
    bool pinned = false, shown = false, copied = false;
    ports.pin = [&](const QImage &, const CancellationToken &) { pinned = true; return ActionOutcome{ActionState::Succeeded, {}, {}}; };
    ports.clipboard = [&](const QImage &image, const CancellationToken &) { copied = image == pixels(); return ActionOutcome{ActionState::Succeeded, {}, {}}; };
    CapturePipeline pipeline(pixels(), dir.filePath("captures"), "fixture",
        {"save", "image-clipboard", "before-upload-window", "upload", "pin"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished);
    QTimer driver;
    connect(&driver, &QTimer::timeout, this, [&] {
      if (shown) return;
      for (auto *widget : QApplication::topLevelWidgets()) {
        auto *dialog = qobject_cast<QMessageBox *>(widget);
        if (!dialog || dialog->objectName() != "captureBeforeUploadDialog" || !dialog->isVisible()) continue;
        shown = true;
        QVERIFY(!http.received); QVERIFY(!redirected.received);
        QCOMPARE(dialog->defaultButton(), dialog->button(QMessageBox::Cancel));
        QVERIFY(dialog->text().contains("12 × 8")); QVERIFY(dialog->text().contains("Fixture"));
        QVERIFY(dialog->text().contains("127.0.0.1")); QVERIFY(!dialog->text().contains("/upload"));
        QVERIFY(dialog->informativeText().contains("HTTP"));
        if (mode == "frozen") {
          DestinationCatalog changed(catalogPath); const auto next = redirected.destination();
          QVERIFY(changed.add(next).isEmpty()); QVERIFY(changed.setDefault(next.id).isEmpty());
        }
        if (mode == "global-cancel") pipeline.cancel();
        else if (mode == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
        else if (mode == "close") dialog->close();
        else dialog->button(mode == "cancel" ? QMessageBox::Cancel : QMessageBox::Yes)->click();
        return;
      }
    });
    driver.start(2); pipeline.start();
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 7000); driver.stop();
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first()); QVERIFY(receipt);
    QVERIFY(receipt->file); QVERIFY(QFileInfo::exists(receipt->file->path)); QCOMPARE(QImage(receipt->file->path), pixels()); QVERIFY(copied);
    const auto &results = receipt->pipeline.orderedResults; QCOMPARE(results.size(), size_t(5));
    QCOMPARE(results[0].action, QString("save")); QCOMPARE(results[1].action, QString("image-clipboard"));
    QCOMPARE(results[2].action, QString("before-upload-window")); QCOMPARE(results[3].action, QString("upload")); QCOMPARE(results[4].action, QString("pin"));
    if (mode == "upload" || mode == "frozen") {
      QVERIFY(shown); QVERIFY(http.received); QVERIFY(!redirected.received); QVERIFY(http.wire.contains("\x89PNG"));
      QCOMPARE(results[2].state, ActionState::Succeeded); QCOMPARE(results[3].state, ActionState::Succeeded); QVERIFY(pinned);
    } else {
      QCOMPARE(shown, mode != "missing" && mode != "unavailable"); QVERIFY(!http.received); QVERIFY(!redirected.received);
      QCOMPARE(results[2].state, (mode == "missing" || mode == "unavailable") ? ActionState::Failed : ActionState::Cancelled);
      if (mode == "missing") QCOMPARE(results[2].diagnostic, std::optional<QString>("upload-destination-missing"));
      if (mode != "global-cancel") {
        QCOMPARE(results[3].state, ActionState::Skipped); QCOMPARE(results[3].diagnostic, std::optional<QString>("upload-declined"));
        QCOMPARE(results[4].state, ActionState::Succeeded); QVERIFY(pinned);
      } else QVERIFY(!pinned);
    }
  }
  void destroyedOwner() {
    QTemporaryDir dir; HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(http.destination()).isEmpty());
    CaptureActionPorts ports; ports.uploadCatalogPath = dir.filePath("destinations.json");
    ports.uploadCredentials = std::make_shared<MemoryCredentialStore>();
    auto pipeline = std::make_unique<CapturePipeline>(pixels(), dir.path(), "owner", QStringList{"before-upload-window", "upload"}, nullptr, ports);
    bool destroyed = false, closed = false;
    QTimer driver;
    connect(&driver, &QTimer::timeout, this, [&] {
      if (destroyed) return;
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() != "captureBeforeUploadDialog" || !widget->isVisible()) continue;
        connect(widget, &QObject::destroyed, this, [&] { closed = true; });
        pipeline.reset(); destroyed = true; return;
      }
    });
    driver.start(2); pipeline->start();
    QTRY_VERIFY_WITH_TIMEOUT(closed, 3000); driver.stop(); QVERIFY(destroyed); QVERIFY(!http.received);
    // A deferred attempt after the owner's disappearance must remain cancelled.
    QTest::qWait(50); QVERIFY(!http.server.hasPendingConnections()); QVERIFY(!http.received);
  }
  void reviewPayload() {
    QTemporaryDir dir; DestinationCatalog catalog(dir.filePath("destinations.json"));
    UploadDestination destination; destination.id = QUuid::createUuid(); destination.name = "Private destination";
    destination.requestUrl = "https://example.test:8443/private/path?secret=hidden#fragment";
    destination.headers = {{"Authorization", "{secret:token}"}};
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<ObservedCredentials>();
    CaptureActionPorts ports; ports.uploadCatalogPath = dir.filePath("destinations.json");
    ports.uploadCredentials = credentials;
    bool checked = false;
    ports.beforeUpload = [&](const CaptureUploadReview &review, const CancellationToken &) {
      checked = credentials->lookups.load() == 0 && QThread::currentThread() == qApp->thread() && review.originalDimensions == QSize(1800, 1200)
          && review.preview.width() <= 640 && review.preview.height() <= 480
          && review.origin.toString() == "https://example.test:8443" && review.destinationName == destination.name;
      return ActionOutcome{ActionState::Cancelled, {}, {}};
    };
    QImage large(1800, 1200, QImage::Format_ARGB32); large.fill(Qt::red);
    CapturePipeline pipeline(large, dir.path(), "preview", {"before-upload-window", "upload"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); QVERIFY(checked); QCOMPARE(credentials->lookups.load(), 0);
  }
};
QTEST_MAIN(CaptureBeforeUploadTest)
#include "CaptureBeforeUploadTest.moc"
