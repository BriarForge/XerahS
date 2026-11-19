// EDITOR-UTILITIES-001 / EU-009, EU-010: the Background Remover window. Every
// disclosure item is pinned, the download is an explicit opt-in served from a
// local file or a loopback socket (never the network), the bytes are verified
// before adoption, and the inference/device rows stay honest about what this
// build contains.
#include "BackgroundRemoverDialog.h"

#include "OnnxFixture.h"

#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace xerahs::app;
using xerahs::bg::CatalogueEntry;
namespace fixture = OnnxFixture;

namespace {

QString writeTemp(const QTemporaryDir &dir, const char *name, const QByteArray &bytes) {
  const QString path = dir.filePath(QLatin1String(name));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  file.write(bytes);
  file.close();
  return path;
}

QString sha256Hex(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString md5Hex(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex());
}

// A catalogue entry whose pins describe the test's own fixture model, served
// from a local path (tests inject the URL; the shipped catalogue is https).
CatalogueEntry fixtureEntry(const QByteArray &modelBytes, const QUrl &artifactUrl) {
  CatalogueEntry entry;
  entry.id = QStringLiteral("u2netp");
  entry.label = QStringLiteral("U2-Net lightweight (u2netp)");
  entry.description = QStringLiteral("fixture model for tests");
  entry.upstream = {QStringLiteral("U-2-Net"), QStringLiteral("fixture authors"), QStringLiteral("fixture paper"),
                    QStringLiteral("https://github.com/xuebinqin/U-2-Net")};
  entry.licence = {QStringLiteral("Apache-2.0"), QStringLiteral("https://github.com/xuebinqin/U-2-Net/blob/master/LICENSE"),
                   QStringLiteral("fixture licence note")};
  entry.artifact.url = artifactUrl.toString();
  entry.artifact.host = artifactUrl.host().isEmpty() ? QStringLiteral("localhost") : artifactUrl.host();
  entry.artifact.hostLicence = QStringLiteral("fixture host licence");
  entry.artifact.sha256 = sha256Hex(modelBytes);
  entry.artifact.md5 = md5Hex(modelBytes);
  entry.artifact.sizeBytes = modelBytes.size();
  entry.input = [] {
    xerahs::bg::TensorContract contract;
    contract.name = QStringLiteral("input.1");
    contract.elemType = 1;
    for (const qint64 dim : QList<qint64>{1, 3, 320, 320}) {
      xerahs::bg::TensorDim value;
      value.value = dim;
      contract.dims.append(value);
    }
    return contract;
  }();
  entry.firstOutput = entry.input;
  entry.firstOutput.name = QStringLiteral("1959");
  entry.firstOutput.dims = {xerahs::bg::TensorDim{1, {}}, xerahs::bg::TensorDim{1, {}},
                            xerahs::bg::TensorDim{320, {}}, xerahs::bg::TensorDim{320, {}}};
  return entry;
}

QLabel *label(const BackgroundRemoverDialog &dialog, const char *name) {
  return dialog.findChild<QLabel *>(QLatin1String(name));
}

QPushButton *button(const BackgroundRemoverDialog &dialog, const char *name) {
  return dialog.findChild<QPushButton *>(QLatin1String(name));
}

// Loopback stall server: one response header, a few bytes, then silence, so
// a download stays in flight until the test cancels it.
class StallServer final : public QObject {
public:
  explicit StallServer(QObject *parent = nullptr) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
      auto *socket = m_server.nextPendingConnection();
      connect(socket, &QTcpSocket::readyRead, socket, [socket] {
        if (!socket->readAll().contains("\r\n\r\n")) return;
        const QByteArray head =
            "HTTP/1.1 200 X\r\nContent-Type: application/octet-stream\r\nContent-Length: 1000000\r\n"
            "Connection: close\r\n\r\n";
        socket->write(head);
        socket->write(QByteArray(2000, 'x'));
      });
    });
    m_server.listen(QHostAddress::LocalHost, 0);
  }
  QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/stall").arg(m_server.serverPort())); }
private:
  QTcpServer m_server;
};

}  // namespace

class BackgroundRemoverDialogTest final : public QObject {
  Q_OBJECT

private slots:
  void showsEveryDisclosureItem();
  void downloadRequiresExplicitConfirmAndDeclineSendsNothing();
  void downloadVerifiesAndAdoptsTheModel();
  void downloadRefusesTamperedBytes();
  void downloadRefusesOversizedArtifact();
  void cancelRemovesTheTemporaryFile();
  void localModelValidatesAndReportsUnverifiedProvenance();
  void localGarbageModelIsRefused();
  void honestStatusRows();
  void closeDuringDownloadCancels();

private:
  bool pumpUntilDownloadingEnds(BackgroundRemoverDialog &dialog, int timeoutMs = 5000);
  void pump(int cycles = 50);
};

bool BackgroundRemoverDialogTest::pumpUntilDownloadingEnds(BackgroundRemoverDialog &dialog, int timeoutMs) {
  QElapsedTimer timer;
  timer.start();
  while (dialog.downloading()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    if (timer.elapsed() > timeoutMs) return false;
    QTest::qSleep(1);
  }
  return true;
}

void BackgroundRemoverDialogTest::pump(int cycles) {
  for (int i = 0; i < cycles; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QTest::qSleep(2);
  }
}

void BackgroundRemoverDialogTest::showsEveryDisclosureItem() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray bytes = fixture::u2netpLikeModel();
  const QString modelPath = writeTemp(serveDir, "model.onnx", bytes);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(bytes, QUrl::fromLocalFile(modelPath))});
  dialog.show();

  QVERIFY(label(dialog, "upstreamValue")->text().contains("U-2-Net"));
  QVERIFY(label(dialog, "upstreamValue")->text().contains("https://github.com/xuebinqin/U-2-Net"));
  QVERIFY(label(dialog, "licenceValue")->text().contains("Apache-2.0"));
  QVERIFY(label(dialog, "licenceValue")->text().contains("https://github.com/xuebinqin/U-2-Net/blob/master/LICENSE"));
  QVERIFY(label(dialog, "sizeValue")->text().contains(QString::number(bytes.size())));
  QVERIFY(label(dialog, "urlValue")->text().startsWith("file://"));
  QVERIFY(label(dialog, "checksumValue")->text().contains(sha256Hex(bytes)));
  QVERIFY(label(dialog, "checksumValue")->text().contains(md5Hex(bytes)));
  QVERIFY(label(dialog, "storeValue")->text().contains(storeDir.path()));
  // EU-010 network behaviour sentence, pinned word for word in meaning.
  QVERIFY(label(dialog, "networkValue")->text().contains("only network activity"));
  QVERIFY(label(dialog, "networkValue")->text().contains("never uploaded"));
}

void BackgroundRemoverDialogTest::downloadRequiresExplicitConfirmAndDeclineSendsNothing() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray bytes = fixture::u2netpLikeModel();
  const QString modelPath = writeTemp(serveDir, "model.onnx", bytes);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(bytes, QUrl::fromLocalFile(modelPath))});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return false; });
  dialog.show();

  dialog.downloadModel();
  pump();
  QVERIFY(!dialog.downloading());
  QVERIFY(dialog.setupStatus().contains("declined", Qt::CaseInsensitive));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx")));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
}

void BackgroundRemoverDialogTest::downloadVerifiesAndAdoptsTheModel() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray bytes = fixture::u2netpLikeModel();
  const QString modelPath = writeTemp(serveDir, "model.onnx", bytes);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(bytes, QUrl::fromLocalFile(modelPath))});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return true; });
  dialog.show();

  dialog.downloadModel();
  QVERIFY(dialog.downloading());
  QVERIFY(pumpUntilDownloadingEnds(dialog));
  QVERIFY(!dialog.downloading());
  QVERIFY2(dialog.errorText().isEmpty(), qPrintable(dialog.errorText()));
  const QString adopted = storeDir.filePath("u2netp.onnx");
  QVERIFY(QFile::exists(adopted));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
  QVERIFY(dialog.setupStatus().contains("pinned and validated"));
  QVERIFY(dialog.modelStatus().contains("verified in your model store"));
  // A verified model makes the download button unnecessary.
  QVERIFY(!button(dialog, "download")->isEnabled());
}

void BackgroundRemoverDialogTest::downloadRefusesTamperedBytes() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray good = fixture::u2netpLikeModel();
  QByteArray tampered = good;
  tampered.replace(0, 1, QByteArray(1, char(tampered.at(0) ^ 0x20)));
  const QString modelPath = writeTemp(serveDir, "model.onnx", tampered);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  // The pins describe the GOOD bytes; the server serves tampered bytes.
  dialog.setCatalogue({fixtureEntry(good, QUrl::fromLocalFile(modelPath))});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return true; });
  dialog.show();

  dialog.downloadModel();
  QVERIFY(pumpUntilDownloadingEnds(dialog));
  QVERIFY(!dialog.downloading());
  QVERIFY(dialog.errorText().contains("model-integrity-mismatch"));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx")));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
}

void BackgroundRemoverDialogTest::downloadRefusesOversizedArtifact() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray good = fixture::u2netpLikeModel();
  QByteArray oversized = good + QByteArray(4096, 'p');
  const QString modelPath = writeTemp(serveDir, "model.onnx", oversized);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(good, QUrl::fromLocalFile(modelPath))});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return true; });
  dialog.show();

  dialog.downloadModel();
  QVERIFY(pumpUntilDownloadingEnds(dialog));
  QVERIFY(!dialog.downloading());
  QVERIFY(dialog.errorText().contains("model-size-mismatch"));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx")));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
}

void BackgroundRemoverDialogTest::cancelRemovesTheTemporaryFile() {
  QTemporaryDir storeDir;
  StallServer server;
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(QByteArray(1000000, 'x'), server.url())});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return true; });
  dialog.show();

  dialog.downloadModel();
  pump(50);
  QVERIFY(dialog.downloading());
  dialog.cancelDownload();
  pump(50);
  QVERIFY(!dialog.downloading());
  QVERIFY(dialog.setupStatus().contains("cancelled", Qt::CaseInsensitive));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx")));
}

void BackgroundRemoverDialogTest::localModelValidatesAndReportsUnverifiedProvenance() {
  QTemporaryDir storeDir, serveDir;
  const QByteArray bytes = fixture::u2netpLikeModel();
  const QString modelPath = writeTemp(serveDir, "model.onnx", bytes);
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(bytes, QUrl::fromLocalFile(modelPath))});
  dialog.setModelFilePicker([modelPath] { return modelPath; });
  dialog.show();

  dialog.chooseLocalModel();
  QVERIFY2(dialog.errorText().isEmpty(), qPrintable(dialog.errorText()));
  QVERIFY(dialog.modelStatus().contains("User-supplied model"));
  QVERIFY(dialog.modelStatus().contains("model.onnx"));
  QVERIFY(dialog.modelStatus().contains("float32"));
  QVERIFY(dialog.modelStatus().contains("not verified", Qt::CaseInsensitive));
}

void BackgroundRemoverDialogTest::localGarbageModelIsRefused() {
  QTemporaryDir storeDir, serveDir;
  const QString garbagePath = writeTemp(serveDir, "garbage.onnx", QByteArrayLiteral("not an onnx model"));
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(fixture::u2netpLikeModel(), QUrl::fromLocalFile(garbagePath))});
  dialog.setModelFilePicker([garbagePath] { return garbagePath; });
  dialog.show();

  dialog.chooseLocalModel();
  QVERIFY(!dialog.errorText().isEmpty());
  QVERIFY(dialog.errorText().contains("not a usable model"));
}

void BackgroundRemoverDialogTest::honestStatusRows() {
  QTemporaryDir storeDir;
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(fixture::u2netpLikeModel(),
                                     QUrl(QStringLiteral("https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2netp.onnx")))});
  dialog.show();

  // EU-009: the device and inference rows say exactly what this build has.
  QVERIFY(dialog.deviceStatus().contains("CPU"));
  QVERIFY(dialog.inferenceStatus().contains("Not available in this build"));
  QVERIFY(dialog.inferenceStatus().contains("onnxruntime"));
  // The un-downloaded model is reported missing, not silently fetched.
  QVERIFY(dialog.modelStatus().contains("not in your model store"));
  QVERIFY(dialog.setupStatus().contains("without your confirmation", Qt::CaseInsensitive));
}

void BackgroundRemoverDialogTest::closeDuringDownloadCancels() {
  QTemporaryDir storeDir;
  StallServer server;
  BackgroundRemoverDialog dialog;
  dialog.setStoreDirectory(storeDir.path());
  dialog.setCatalogue({fixtureEntry(QByteArray(1000000, 'x'), server.url())});
  dialog.setDownloadConfirm([](const CatalogueEntry &) { return true; });
  dialog.show();

  dialog.downloadModel();
  pump(50);
  QVERIFY(dialog.downloading());
  // The real close path: the Close button rejects the dialog.
  auto *box = dialog.findChild<QDialogButtonBox *>();
  QVERIFY(box);
  auto *close = box->button(QDialogButtonBox::Close);
  QVERIFY(close);
  close->click();
  QVERIFY(!dialog.downloading());
  QVERIFY(!dialog.isVisible());
  QVERIFY(!QFile::exists(storeDir.filePath("u2netp.onnx.download")));
  QVERIFY(dialog.setupStatus().contains("cancelled on close", Qt::CaseInsensitive));
}

QTEST_MAIN(BackgroundRemoverDialogTest)
#include "BackgroundRemoverDialogTest.moc"
