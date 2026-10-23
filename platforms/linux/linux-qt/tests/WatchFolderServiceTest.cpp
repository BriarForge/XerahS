// CORE-024, CORE-025: native watch actions, restart checkpoints and consent UI.
#include "FileUploadController.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "uploads/DestinationCatalog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs;
using namespace xerahs::app;
using namespace xerahs::uploads;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes = "watch fixture pixels") {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
FileUploadPorts portsFor(const QTemporaryDir &dir) {
  FileUploadPorts ports;
  ports.catalogPath = dir.filePath("destinations.json");
  ports.credentials = std::make_shared<MemoryCredentialStore>();
  return ports;
}
class HttpFixture final : public QObject {
public:
  QTcpServer server;
  int received = 0;
  QByteArray request;
  HttpFixture() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (server.hasPendingConnections()) {
        auto *peer = server.nextPendingConnection();
        connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
        connect(peer, &QTcpSocket::readyRead, this, [this, peer, wire = QByteArray(), done = false]() mutable {
          wire += peer->readAll();
          if (done) return;
          const auto split = wire.indexOf("\r\n\r\n");
          if (split < 0) return;
          const auto length = QRegularExpression("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption)
              .match(QString::fromLatin1(wire.left(split)));
          if (!length.hasMatch() || wire.size() < split + 4 + length.captured(1).toLongLong()) return;
          done = true;
          request = wire;
          ++received;
          const QByteArray body = R"({"url":"https://example.test/watched"})";
          peer->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nConnection: close\r\n\r\n" + body);
          peer->disconnectFromHost();
        });
      }
    });
  }
};
}

class WatchFolderServiceTest final : public QObject {
  Q_OBJECT
private slots:
  void stableNewFileCopiesOrMoves_data() {
    QTest::addColumn<bool>("move");
    QTest::newRow("copy") << false;
    QTest::newRow("move") << true;
  }
  void stableNewFileCopiesOrMoves() {
    QFETCH(bool, move);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/new.png");
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    watch::Rule rule;
    rule.path = dir.filePath("input");
    rule.action = move ? watch::Action::Move : watch::Action::Copy;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start();
    QVERIFY(writeFile(source));
    service.rescan();
    now = 1999;
    service.rescan();
    QCOMPARE(processed.count(), 0);
    QVERIFY(writeFile(source, "new complete file content"));
    service.rescan();
    now += 1999;
    service.rescan();
    QCOMPARE(processed.count(), 0);
    const auto key = watch::fingerprint(source);
    ++now;
    service.rescan();
    QCOMPARE(processed.count(), 1);
    QCOMPARE(processed.first().at(0).toString(), source);
    QCOMPARE(processed.first().at(1).toBool(), true);
    QCOMPARE(QFileInfo::exists(source), !move);
    const auto outputs = QDir(captures).entryList(QDir::Files);
    QCOMPARE(outputs.size(), 1);
    QVERIFY(outputs.first().endsWith("-new.png"));
    QCOMPARE(readFile(QDir(captures).filePath(outputs.first())), QByteArray("new complete file content"));
    QVERIFY(store.processed(key));
    now += 3000;
    service.rescan();
    QCOMPARE(processed.count(), 1);
    service.stop();
    watch::WatchFolders restarted(dir.filePath("store"));
    QVERIFY(restarted.loadError().isEmpty());
    QVERIFY(restarted.processed(key));
    WatchFolderService second(restarted, uploads, [&] { return captures; }, [&] { return now; });
    QSignalSpy repeated(&second, &WatchFolderService::processed);
    second.start();
    now += 3000;
    second.rescan();
    QCOMPARE(repeated.count(), 0);
    QCOMPARE(QDir(captures).entryList(QDir::Files), outputs);
    second.stop();
  }

  void uploadsOnceThroughDefaultDestinationAcrossRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/new.png");
    HttpFixture http;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    UploadDestination destination;
    destination.id = QUuid::createUuid();
    destination.name = "Watch fixture";
    destination.requestUrl = QString("http://127.0.0.1:%1/upload").arg(http.server.serverPort());
    destination.body = BodyType::MultipartFormData;
    destination.fileFormName = "file";
    destination.headers = {{"Authorization", "{secret:token}"}};
    destination.resultUrl = "{json:url}";
    auto ports = portsFor(dir);
    DestinationCatalog catalog(ports.catalogPath);
    QCOMPARE(catalog.add(destination), QString());
    QVERIFY(ports.credentials->store(destination.id, "token", "watch-fixture-secret").ok());
    qint64 now = 0;
    QWidget owner;
    {
      FileUploadController uploads(ports, &owner);
      watch::WatchFolders store(dir.filePath("store"));
      WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
      watch::Rule rule;
      rule.path = dir.filePath("input");
      QCOMPARE(service.setRules({rule}), QString());
      QSignalSpy processed(&service, &WatchFolderService::processed);
      QSignalSpy idle(&uploads, &FileUploadController::finished);
      service.start();
      QVERIFY(writeFile(source));
      service.rescan();
      now = 1999;
      service.rescan();
      QCOMPARE(http.received, 0);
      now = 2000;
      service.rescan();
      QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
      QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
      QCOMPARE(processed.first().at(0).toString(), source);
      QCOMPARE(processed.first().at(1).toBool(), true);
      QCOMPARE(http.received, 1);
      QVERIFY(http.request.startsWith("POST /upload HTTP/1.1\r\n"));
      QVERIFY(http.request.contains("Authorization: watch-fixture-secret\r\n"));
      QVERIFY(http.request.contains("filename=\"new.png\""));
      QVERIFY(http.request.contains("watch fixture pixels"));
      QVERIFY(store.processed(watch::fingerprint(source)));
      QVERIFY(QFileInfo::exists(source));
      now += 3000;
      service.rescan();
      QCOMPARE(processed.count(), 1);
      service.stop();
    }
    FileUploadController uploads(ports, &owner);
    watch::WatchFolders restarted(dir.filePath("store"));
    QVERIFY(restarted.loadError().isEmpty());
    WatchFolderService second(restarted, uploads, [&] { return captures; }, [&] { return now; });
    QSignalSpy processed(&second, &WatchFolderService::processed);
    QSignalSpy progress(&uploads, &FileUploadController::progress);
    second.start();
    now += 3000;
    second.rescan();
    // Flush queued preparation/network work to detect an admitted duplicate.
    QTest::qWait(200);
    QCOMPARE(http.received, 1);
    QCOMPARE(processed.count(), 0);
    QCOMPARE(progress.count(), 0);
    second.stop();
  }

  void recursiveScanExcludesCapturesHiddenTemporaryAndSymlinkLoops() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input/sub"));
    QVERIFY(QDir(dir.path()).mkpath("input/.hidden"));
    const auto captures = dir.filePath("input/captures");
    QVERIFY(QDir().mkpath(captures));
    const auto source = dir.filePath("input/sub/good.png");
    QVERIFY(writeFile(source));
    for (const auto &name : QStringList{".hidden/hidden.png", ".hidden.png", "pending.tmp", "~backup.png", "captures/old.png"})
      QVERIFY(writeFile(dir.filePath("input/" + name)));
    QVERIFY(QFile::link(dir.filePath("input"), dir.filePath("input/sub/loop")));
    QVERIFY(QFile::link(source, dir.filePath("input/link.png")));
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    watch::Rule rule;
    rule.path = dir.filePath("input");
    rule.filters = {"*"};
    rule.recursive = true;
    rule.action = watch::Action::Copy;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start();
    now = 2000;
    service.rescan();
    QCOMPARE(processed.count(), 1);
    QCOMPARE(processed.first().at(0).toString(), source);
    now += 3000;
    service.rescan();
    now += 3000;
    service.rescan();
    QCOMPARE(processed.count(), 1);
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 2);
    service.stop();
    QVERIFY(writeFile(dir.filePath("input/stopped.png")));
    now += 3000;
    service.rescan();
    QCOMPARE(processed.count(), 1);
    service.start();
    now += 2000;
    service.rescan();
    QCOMPARE(processed.count(), 2);
    service.stop();
  }

  void dialogAddsRemovesAndPersistsEnabledConsent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return dir.filePath("captures"); });
    WatchFoldersDialog dialog(service);
    dialog.show();
    auto *path = dialog.findChild<QLineEdit *>("watchPath");
    auto *filters = dialog.findChild<QLineEdit *>("watchFilters");
    auto *recursive = dialog.findChild<QCheckBox *>("watchRecursive");
    auto *action = dialog.findChild<QComboBox *>("watchAction");
    auto *add = dialog.findChild<QPushButton *>("addWatchFolder");
    auto *remove = dialog.findChild<QPushButton *>("removeWatchFolder");
    auto *list = dialog.findChild<QListWidget *>("watchFolders");
    auto *error = dialog.findChild<QLabel *>("watchError");
    QVERIFY(path && filters && recursive && action && add && remove && list && error);
    path->setText(dir.filePath("input"));
    filters->setText("*.png; *.jpg");
    recursive->setChecked(true);
    action->setCurrentIndex(1);
    QTest::mouseClick(add, Qt::LeftButton);
    QCOMPARE(error->text(), QString());
    QCOMPARE(list->count(), 1);
    QCOMPARE(service.rules().size(), 1);
    QCOMPARE(service.rules().first().path, QFileInfo(dir.filePath("input")).canonicalFilePath());
    QCOMPARE(service.rules().first().filters, QStringList({"*.png", "*.jpg"}));
    QCOMPARE(service.rules().first().recursive, true);
    QCOMPARE(service.rules().first().action, watch::Action::Copy);
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    list->setCurrentRow(0);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Space);
    QCOMPARE(service.rules().first().enabled, false);
    watch::WatchFolders disabled(dir.filePath("store"));
    QVERIFY(disabled.loadError().isEmpty());
    QCOMPARE(disabled.rules().first().enabled, false);
    list->setCurrentRow(0);
    QTest::keyClick(list, Qt::Key_Space);
    QCOMPARE(service.rules().first().enabled, true);
    watch::WatchFolders enabled(dir.filePath("store"));
    QCOMPARE(enabled.rules().first().enabled, true);
    QTest::mouseClick(add, Qt::LeftButton);
    QCOMPARE(error->text(), QString("watch-overlapping-roots"));
    QCOMPARE(list->count(), 1);
    list->setCurrentRow(0);
    QTest::mouseClick(remove, Qt::LeftButton);
    QCOMPARE(error->text(), QString());
    QCOMPARE(list->count(), 0);
    QVERIFY(service.rules().isEmpty());
    watch::WatchFolders removed(dir.filePath("store"));
    QVERIFY(removed.loadError().isEmpty());
    QVERIFY(removed.rules().isEmpty());
  }
};

QTEST_MAIN(WatchFolderServiceTest)
#include "WatchFolderServiceTest.moc"
