#include "TerminalLogCapture.h"
#include "actions/PipelineResultJson.h"
// CORE-024, CORE-025: native watch actions, restart checkpoints and consent UI.
#include "FileUploadController.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "history/CaptureHistory.h"
#include "NativeWorkflowExecutions.h"
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
#include <QElapsedTimer>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

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
  ports.history = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
  ports.executions = NativeWorkflowExecutions(dir.filePath("executions")).writer();
  ports.credentials = std::make_shared<MemoryCredentialStore>();
  return ports;
}
void scan(WatchFolderService &service) {
  service.rescan();
  QTRY_VERIFY_WITH_TIMEOUT(!service.discovering(), 7000);
}
class HttpFixture final : public QObject {
public:
  QTcpServer server;
  int received = 0;
  bool stall = false;
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
          if (stall) return;
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
  void initTestCase() {
    static QTemporaryDir state;  // services without an explicit writer must not touch the real state directory
    qputenv("XDG_STATE_HOME", state.path().toUtf8());
  }

  void discoveryDoesNotBlockTheGui_data() {
    QTest::addColumn<QString>("interrupt");
    QTest::newRow("stop-and-restart") << QString("stop");
    QTest::newRow("replace-rules") << QString("rules");
    QTest::newRow("change-capture-folder") << QString("captures");
    QTest::newRow("destroy-owner") << QString("destroy");
  }
  void discoveryDoesNotBlockTheGui() {
    QFETCH(QString, interrupt);
    QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto source = dir.filePath("input/ready.png");
    auto captures = dir.filePath("captures");
    QVERIFY(writeFile(source, "retained source"));
    QWidget owner; FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    qint64 now = 0;
    auto service = std::make_unique<WatchFolderService>(store, uploads, [&] { return captures; }, [&] { return now; });
    watch::Rule rule; rule.path = dir.filePath("input"); rule.action = watch::Action::Copy;
    QCOMPARE(service->setRules({rule}), QString());
    struct Probe { std::atomic_bool entered{false}, release{false}, onGui{false}, exited{false}; std::atomic_int calls{0}; };
    const auto probe = std::make_shared<Probe>();
    struct Release { std::shared_ptr<Probe> probe; ~Release() { probe->release = true; } } release{probe};
    const auto guiThread = QThread::currentThread();
    service->setDiscoveryHook([probe, guiThread] {
      ++probe->calls;
      probe->onGui = QThread::currentThread() == guiThread;
      probe->entered = true;
      if (probe->onGui) return;
      while (!probe->release) QThread::msleep(5);
      probe->exited = true;
    });
    QSignalSpy processed(service.get(), &WatchFolderService::processed);
    service->start();
    QTRY_VERIFY_WITH_TIMEOUT(probe->entered.load(), 5000);
    QVERIFY2(!probe->onGui.load(), "Directory discovery must run outside the GUI thread");
    for (int i = 0; i < 5; ++i) service->rescan();
    int ticks = 0; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++ticks; }); timer.start(1);
    QTRY_VERIFY_WITH_TIMEOUT(ticks >= 3, 5000);
    QCOMPARE(probe->calls.load(), 1);
    if (interrupt == "destroy") service.reset();
    else if (interrupt == "rules") QCOMPARE(service->setRules({}), QString());
    else if (interrupt == "captures") captures = dir.filePath("input");
    else service->stop();
    probe->release = true;
    QTRY_VERIFY_WITH_TIMEOUT(probe->exited.load(), 5000);
    if (service) QTRY_VERIFY_WITH_TIMEOUT(!service->discovering(), 5000);
    QCOMPARE(processed.count(), 0);
    QCOMPARE(readFile(source), QByteArray("retained source"));
    QVERIFY(QDir(dir.filePath("captures")).entryList(QDir::Files).isEmpty());
    if (!service) return;
    service->setDiscoveryHook({});
    captures = dir.filePath("captures");
    if (interrupt == "rules") QCOMPARE(service->setRules({rule}), QString());
    service->start(); scan(*service);
    now = 2000; scan(*service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QVERIFY(processed.first()[1].toBool());
    const auto files = QDir(captures).entryList(QDir::Files);
    QCOMPARE(files.size(), 1);
    QCOMPARE(readFile(QDir(captures).filePath(files.first())), QByteArray("retained source"));
    service->stop();
  }

  // WFCAT-002/004/017: copy/move own an execution and notify from its terminal state.
  void copyAndMoveWriteExecutionsAndNotifyFromTerminalState_data() {
    QTest::addColumn<bool>("move"); QTest::addColumn<bool>("removalFails");
    QTest::newRow("copy") << false << false;
    QTest::newRow("move") << true << false;
    QTest::newRow("move-source-removal-fails") << true << true;
  }
  void copyAndMoveWriteExecutionsAndNotifyFromTerminalState() {
    QFETCH(bool, move); QFETCH(bool, removalFails);
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures"); const auto source = dir.filePath("input/new.png");
    qint64 now = 0; QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    service.setExecutionWriter(executions.writer());
    if (removalFails) service.setBeforeRemoveHook([](const QString &path) { QFile::remove(path); });  // the later remove then fails
    watch::Rule rule; rule.path = dir.filePath("input"); rule.recursive = true; rule.filters = {"*.png"};
    rule.action = move ? watch::Action::Move : watch::Action::Copy;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy observed(&service, &WatchFolderService::terminalObserved), activity(&service, &WatchFolderService::activityChanged);
    TerminalLogCapture logs;
    QSignalSpy notified(&service, &WatchFolderService::notify);
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    QVERIFY(writeFile(source, "new complete file content"));
    scan(service); now = 2000; scan(service); now += 2000; scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    const auto listing = executions.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto &entry = listing.entries.front();
    const auto record = xerahs::workflows::parseWorkflowExecution(entry.snapshot, entry.id); QVERIFY(record);
    QCOMPARE(record->origin, QString("watch-folder"));
    QCOMPARE(record->settings.value("operation").toString(), QString(move ? "move" : "copy"));
    QCOMPARE(record->settings.value("rule").toObject().value("action").toString(), QString(move ? "move" : "copy"));
    QCOMPARE(record->settings.value("rule").toObject().value("recursive").toBool(), true);
    QCOMPARE(record->settings.value("rule").toObject().value("filters").toArray().first().toString(), QString("*.png"));
    QVERIFY(record->startedAt.isValid() && record->finishedAt.isValid());
    QStringList actions, stepStates;
    for (const auto &step : record->steps) { actions << step.toObject().value("action").toString(); stepStates << step.toObject().value("state").toString(); }
    QStringList expectedActions{"read-file", "copy-file"}, expectedStates{"succeeded", "succeeded"};
    if (move) { expectedActions << "remove-source"; expectedStates << (removalFails ? "failed" : "succeeded"); }
    QCOMPARE(actions, expectedActions); QCOMPARE(stepStates, expectedStates);
    const auto expectedState = removalFails ? QString("partial") : QString("succeeded");
    QCOMPARE(record->state, expectedState);
    QCOMPARE(record->diagnostic, removalFails ? QString("watch-move-remove-failed") : QString());
    // The copy committed before the source was touched, and stays referenced even when removal failed.
    QCOMPARE(record->outputs, QStringList{entry.id + "/watch.copy"});
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 1);
    QCOMPARE(QFileInfo::exists(source), !move);
    QCOMPARE(entry.result.value("state").toString(), expectedState);
    QCOMPARE(entry.result.value("outputs").toArray().size(), 1);
    // Notification agrees with the terminal state and never claims completion for a partial outcome.
    QCOMPARE(notified.count(), 1);
    QCOMPARE(notified.first().at(1).toBool(), !removalFails);
    const auto message = notified.first().at(0).toString();
    if (removalFails) { QVERIFY(message.contains("partial")); QVERIFY(!message.startsWith("Processed")); }
    else QVERIFY(message.contains("Workflow: succeeded"));
    QCOMPARE(observed.size(), 1); const auto observation = observed.first().first().toJsonObject();
    QCOMPARE(observation.value("state"), entry.result.value("state")); QCOMPARE(observation.value("outputs"), entry.result.value("outputs"));
    const auto text = actions::pipelineObservationText(observation);
    QCOMPARE(activity.first().first().toString(), text); QVERIFY(message.startsWith(text));
    QCOMPARE(logs.lines, QStringList{"Workflow terminal " + text});
    QVERIFY(!text.contains("new.png")); QVERIFY(!text.contains("new complete file content"));
    QVERIFY(!entry.snapshot.contains(dir.path().toUtf8()));
    service.stop();
  }

  void storageFailuresNeverSilentlyCompleteFileWork_data() {
    QTest::addColumn<bool>("move"); QTest::addColumn<int>("failedWrite");
    for (bool move : {false, true}) {
      const auto prefix = move ? QByteArray("move-") : QByteArray("copy-");
      QTest::newRow((prefix + "admission").constData()) << move << 1;
      QTest::newRow((prefix + "start").constData()) << move << 2;
      QTest::newRow((prefix + "step").constData()) << move << 3;
      QTest::newRow((prefix + "terminal").constData()) << move << (move ? 6 : 5);
    }
  }
  void storageFailuresNeverSilentlyCompleteFileWork() {
    QFETCH(bool, move); QFETCH(int, failedWrite);
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures"), source = dir.filePath("input/new.png");
    qint64 now = 0; QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    const auto writer = executions.writer();
    int writes = 0;
    service.setExecutionWriter([&](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) {
      if (++writes == failedWrite) return actions::JournalWrite{false, "fixture-storage-unavailable"};
      return writer(id, expected, bytes);
    });
    watch::Rule rule; rule.path = dir.filePath("input"); rule.action = move ? watch::Action::Move : watch::Action::Copy;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy notified(&service, &WatchFolderService::notify), processed(&service, &WatchFolderService::processed);
    service.start(); scan(service); QVERIFY(writeFile(source, "retain actual file contents"));
    const auto key = watch::fingerprint(source);
    scan(service); now = 2000; scan(service); now += 2000; scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QVERIFY(!processed.first().at(1).toBool());
    QCOMPARE(notified.count(), 1); QVERIFY(!notified.first().at(1).toBool());
    const auto message = notified.first().at(0).toString();
    QVERIFY(message.contains("workflow-storage-unavailable")); QVERIFY(!message.startsWith("Processed"));
    const auto outputs = QDir(captures).entryList(QDir::Files);
    const bool dispatched = failedWrite > 2;
    QCOMPARE(outputs.size(), dispatched ? 1 : 0);
    QCOMPARE(QFileInfo::exists(source), !dispatched || !move);
    if (QFileInfo::exists(source)) QCOMPARE(readFile(source), QByteArray("retain actual file contents"));
    if (dispatched) {
      QCOMPARE(readFile(QDir(captures).filePath(outputs.first())), QByteArray("retain actual file contents"));
      QVERIFY(message.contains("outcome-unconfirmed"));
      QVERIFY(store.processed(key)); // completed side effects must not be replayed after restart
    } else QVERIFY(!store.processed(key));
    const auto writesAfterAttempt = writes;
    QVERIFY(writeFile(dir.filePath("input/second.png")));
    for (int attempt = 0; attempt < 4; ++attempt) { now += 2000; scan(service); QTest::qWait(20); }
    QCOMPARE(processed.count(), 1); QCOMPARE(writes, writesAfterAttempt);
    service.stop();
  }

  void copyFailureIsAFailedExecutionNeverSucceeded() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures"); const auto source = dir.filePath("input/new.png");
    qint64 now = 0; QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    NativeWorkflowExecutions executions(dir.filePath("executions")); service.setExecutionWriter(executions.writer());
    service.setFileOperationHook([](const QString &path) { QFile::remove(path); });  // vanishes mid-copy
    watch::Rule rule; rule.path = dir.filePath("input"); rule.action = watch::Action::Move;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy observed(&service, &WatchFolderService::terminalObserved), activity(&service, &WatchFolderService::activityChanged);
    TerminalLogCapture logs;
    QSignalSpy notified(&service, &WatchFolderService::notify); QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    QVERIFY(writeFile(source)); scan(service); now = 2000; scan(service); now += 2000; scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    const auto listing = executions.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id); QVERIFY(record);
    QCOMPARE(record->state, QString("permanent-failure")); QVERIFY(record->outputs.isEmpty());
    for (const auto &step : record->steps) QVERIFY(step.toObject().value("action") != "remove-source");
    QCOMPARE(record->steps.last().toObject().value("action").toString(), QString("copy-file"));
    QCOMPARE(record->steps.last().toObject().value("state").toString(), QString("failed"));
    QVERIFY(!notified.first().at(1).toBool()); QVERIFY(!notified.first().at(0).toString().startsWith("Processed"));
    service.stop();
  }

  // The watch upload shares the upload execution: origin watch-folder with the redacted rule snapshot.
  void watchUploadRecordsTheRuleAndNotifiesFromItsTerminalState() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto source = dir.filePath("input/new.png");
    HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    UploadDestination destination;
    destination.id = QUuid::createUuid(); destination.name = "Watch fixture";
    destination.requestUrl = QString("http://127.0.0.1:%1/upload").arg(http.server.serverPort());
    destination.body = BodyType::MultipartFormData; destination.fileFormName = "file"; destination.resultUrl = "{json:url}";
    auto ports = portsFor(dir);
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    DestinationCatalog catalog(ports.catalogPath); QCOMPARE(catalog.add(destination), QString());
    qint64 now = 0; QWidget owner;
    FileUploadController uploads(ports, &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return dir.filePath("captures"); }, [&] { return now; });
    watch::Rule rule; rule.path = dir.filePath("input");
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy observed(&service, &WatchFolderService::terminalObserved), activity(&service, &WatchFolderService::activityChanged);
    TerminalLogCapture logs;
    QSignalSpy notified(&service, &WatchFolderService::notify); QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    QVERIFY(writeFile(source)); scan(service); now = 2000; scan(service); now += 2000; scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    const auto listing = executions.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id); QVERIFY(record);
    QCOMPARE(record->origin, QString("watch-folder")); QCOMPARE(record->state, QString("succeeded"));
    QCOMPARE(record->settings.value("operation").toString(), QString("upload"));
    QCOMPARE(record->settings.value("rule").toObject().value("action").toString(), QString("upload"));
    QCOMPARE(record->settings.value("destination").toObject().value("name").toString(), QString("Watch fixture"));
    QCOMPARE(observed.size(), 1); const auto observation = observed.first().first().toJsonObject();
    QCOMPARE(observation.value("state"), listing.entries.front().result.value("state"));
    QCOMPARE(observation.value("outputs"), listing.entries.front().result.value("outputs"));
    const auto text = actions::pipelineObservationText(observation);
    QCOMPARE(notified.last().at(0).toString(), text); QCOMPARE(activity.first().first().toString(), text);
    QCOMPARE(logs.lines, QStringList{"Workflow terminal " + text});
    service.stop();
  }

  void stoppingAfterMoveCopyCommitsPreservesTheSourceAndReportsPartial() {
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto source = dir.filePath("input/new.png"), captures = dir.filePath("captures");
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    NativeWorkflowExecutions executions(dir.filePath("executions"));
    service.setExecutionWriter(executions.writer());
    service.setBeforeRemoveHook([&](const QString &) {
      QMetaObject::invokeMethod(&service, [&] { service.stop(); }, Qt::BlockingQueuedConnection);
    });
    watch::Rule rule; rule.path = dir.filePath("input"); rule.action = watch::Action::Move;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed), notified(&service, &WatchFolderService::notify);
    service.start(); scan(service);
    const QByteArray contents("completed copy must survive cancellation");
    QVERIFY(writeFile(source, contents)); scan(service); now = 2000; scan(service); now += 2000; scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(readFile(source), contents);
    const auto outputs = QDir(captures).entryList(QDir::Files); QCOMPARE(outputs.size(), 1);
    QCOMPARE(readFile(QDir(captures).filePath(outputs.first())), contents);
    QVERIFY(!processed.first().at(1).toBool());
    QVERIFY(!notified.last().at(1).toBool());
    const auto listing = executions.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id);
    QVERIFY(record); QCOMPARE(record->state, QString("partial")); QVERIFY(!record->outputs.isEmpty());
    QCOMPARE(record->steps.last().toObject().value("action").toString(), QString("remove-source"));
    QCOMPARE(record->steps.last().toObject().value("state").toString(), QString("cancelled"));
  }

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
    service.start(); scan(service);
    QVERIFY(writeFile(source));
    scan(service);
    now = 1999;
    scan(service);
    QCOMPARE(processed.count(), 0);
    QVERIFY(writeFile(source, "new complete file content"));
    scan(service);
    now += 1999;
    scan(service);
    QCOMPARE(processed.count(), 0);
    const auto key = watch::fingerprint(source);
    ++now;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(processed.first().at(0).toString(), source);
    QCOMPARE(processed.first().at(1).toBool(), true);
    QCOMPARE(QFileInfo::exists(source), !move);
    const auto outputs = QDir(captures).entryList(QDir::Files);
    QCOMPARE(outputs.size(), 1);
    QVERIFY(outputs.first().endsWith("-new.png"));
    QCOMPARE(readFile(QDir(captures).filePath(outputs.first())), QByteArray("new complete file content"));
    QVERIFY(store.processed(key));
    now += 3000;
    scan(service);
    QCOMPARE(processed.count(), 1);
    service.stop();
    watch::WatchFolders restarted(dir.filePath("store"));
    QVERIFY(restarted.loadError().isEmpty());
    QVERIFY(restarted.processed(key));
    WatchFolderService second(restarted, uploads, [&] { return captures; }, [&] { return now; });
    QSignalSpy repeated(&second, &WatchFolderService::processed);
    second.start(); scan(second);
    now += 3000;
    scan(second);
    QCOMPARE(repeated.count(), 0);
    QCOMPARE(QDir(captures).entryList(QDir::Files), outputs);
    second.stop();
  }

  // CORE-025: one failing file is reported once, stays unprocessed in the store and does not block the next file.
  void failingFileDoesNotBlockTheNext() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto bad = dir.filePath("input/a.png"), good = dir.filePath("input/b.png");
    QVERIFY(writeFile(bad));
    QVERIFY(writeFile(good, "second file content"));
    QVERIFY(QFile::setPermissions(bad, QFileDevice::Permissions()));
    if (QFile(bad).open(QIODevice::ReadOnly)) QSKIP("Permissions are not enforced for this user");
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    watch::Rule rule;
    rule.action = watch::Action::Copy;
    rule.path = dir.filePath("input");
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    scan(service);
    now = 2000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(processed.at(0).at(0).toString(), bad);
    QCOMPARE(processed.at(0).at(1).toBool(), false);
    now += 500;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 7000);
    QCOMPARE(processed.at(1).at(0).toString(), good);
    QCOMPARE(processed.at(1).at(1).toBool(), true);
    for (int i = 0; i < 3; ++i) { now += 500; scan(service); QTest::qWait(30); }
    QCOMPARE(processed.count(), 2);
    QVERIFY(!store.processed(watch::fingerprint(bad)));
    QVERIFY(store.processed(watch::fingerprint(good)));
    QVERIFY(QFileInfo::exists(bad) && QFileInfo::exists(good));
    const auto outputs = QDir(captures).entryList(QDir::Files);
    QCOMPARE(outputs.size(), 1);
    QCOMPARE(readFile(QDir(captures).filePath(outputs.first())), QByteArray("second file content"));
    service.stop();
    QFile::setPermissions(bad, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }

  // CORE-025: stopping while an upload is in flight leaves the file in place, unchecked-in, and discoverable
  // again by the same service after start() and by a fresh service after restart.
  void stoppingMidUploadLeavesTheFileDiscoverable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/new.png");
    HttpFixture http;
    http.stall = true;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    UploadDestination destination;
    destination.id = QUuid::createUuid();
    destination.name = "Watch fixture";
    destination.requestUrl = QString("http://127.0.0.1:%1/upload").arg(http.server.serverPort());
    destination.body = BodyType::MultipartFormData;
    destination.fileFormName = "file";
    destination.resultUrl = "{json:url}";
    auto ports = portsFor(dir);
    DestinationCatalog catalog(ports.catalogPath);
    QCOMPARE(catalog.add(destination), QString());
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(ports, &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    watch::Rule rule;
    rule.path = dir.filePath("input");
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    QVERIFY(writeFile(source));
    scan(service);
    now = 2000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    service.stop();
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(processed.first().at(1).toBool(), false);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(readFile(source), QByteArray("watch fixture pixels"));
    QVERIFY(!store.processed(watch::fingerprint(source)));
    QVERIFY(QDir(captures).entryList(QDir::Files).isEmpty());
    // The same service picks the file up again once started.
    http.stall = false;
    service.start(); scan(service);
    now += 3000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 7000);
    QCOMPARE(processed.at(1).at(0).toString(), source);
    QCOMPARE(processed.at(1).at(1).toBool(), true);
    QCOMPARE(http.received, 2);
    QVERIFY(store.processed(watch::fingerprint(source)));
    service.stop();
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
      service.start(); scan(service);
      QVERIFY(writeFile(source));
      scan(service);
      now = 1999;
      scan(service);
      QCOMPARE(http.received, 0);
      now = 2000;
      scan(service);
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
      scan(service);
      QCOMPARE(processed.count(), 1);
      service.stop();
    }
    const auto journalHistory = NativePipelineJournal(dir.filePath("history")).list();
    QCOMPARE(journalHistory.entries.size(), std::size_t(1)); QVERIFY(journalHistory.entries.front().parsed.record);
    QVERIFY(!journalHistory.entries.front().bytes.contains("watch-fixture-secret"));
    const auto items = history::captureHistoryItems({*journalHistory.entries.front().parsed.record});
    QCOMPARE(items.size(), std::size_t(1)); QCOMPARE(items.front().origin, QString("watch-folder"));
    QCOMPARE(items.front().destinationName, QString("Watch fixture")); QCOMPARE(items.front().files.front().path, source);
    QVERIFY(journalHistory.entries.front().bytes.contains("https://example.test/watched"));
    FileUploadController uploads(ports, &owner);
    watch::WatchFolders restarted(dir.filePath("store"));
    QVERIFY(restarted.loadError().isEmpty());
    WatchFolderService second(restarted, uploads, [&] { return captures; }, [&] { return now; });
    QSignalSpy processed(&second, &WatchFolderService::processed);
    QSignalSpy progress(&uploads, &FileUploadController::progress);
    second.start(); scan(second);
    now += 3000;
    scan(second);
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
    service.start(); scan(service);
    now = 2000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(processed.first().at(0).toString(), source);
    now += 3000;
    scan(service);
    now += 3000;
    scan(service);
    QTest::qWait(50);
    QCOMPARE(processed.count(), 1);
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 2);
    service.stop();
    QVERIFY(writeFile(dir.filePath("input/stopped.png")));
    now += 3000;
    scan(service);
    QCOMPARE(processed.count(), 1);
    service.start(); scan(service);
    now += 2000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 7000);
    service.stop();
  }

  // CORE-004: copy/move runs on an owned worker; the GUI thread keeps spinning while a slow copy is in flight,
  // repeated rescans never double-process the source, and the result is delivered afterwards.
  void slowCopyDoesNotBlockGuiAndIsNotDoubleProcessed_data() {
    QTest::addColumn<bool>("move");
    QTest::newRow("copy") << false;
    QTest::newRow("move") << true;
  }
  void slowCopyDoesNotBlockGuiAndIsNotDoubleProcessed() {
    QFETCH(bool, move);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/big.png");
    QVERIFY(writeFile(source, QByteArray(3 * 1024 * 1024 + 7, 'x')));
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    std::atomic_bool release{false};
    std::atomic_int hookCalls{0};
    service.setFileOperationHook([&](const QString &) { ++hookCalls; while (!release.load()) QThread::msleep(5); });
    watch::Rule rule;
    rule.path = dir.filePath("input");
    rule.action = move ? watch::Action::Move : watch::Action::Copy;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    QSignalSpy progress(&service, &WatchFolderService::progress);
    service.start(); scan(service);
    scan(service);
    now = 2000;
    QElapsedTimer timer; timer.start();
    service.rescan();
    QVERIFY2(timer.elapsed() < 1000, "rescan must only enqueue work");
    QTRY_VERIFY_WITH_TIMEOUT(hookCalls.load() >= 1, 7000);
    int ticks = 0;
    QTimer ticker; QObject::connect(&ticker, &QTimer::timeout, [&] { ++ticks; });
    ticker.start(5);
    for (int i = 0; i < 5; ++i) { now += 500; scan(service); QTest::qWait(20); }
    QVERIFY(ticks >= 3);
    QCOMPARE(processed.count(), 0);
    QVERIFY(QFileInfo::exists(source));
    release = true;
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 10000);
    QCOMPARE(processed.first().at(1).toBool(), true);
    QVERIFY(progress.count() >= 1);
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 1);
    QCOMPARE(QFileInfo::exists(source), !move);
    for (int i = 0; i < 3; ++i) { now += 3000; scan(service); QTest::qWait(30); }
    QCOMPARE(processed.count(), 1);
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 1);
    service.stop();
  }

  // CORE-004: stopping cancels the in-flight copy, leaves no partial output, keeps the source, and a restart retries it.
  void stopCancelsCopyWithoutPartialOutput() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/big.png");
    QVERIFY(writeFile(source, QByteArray(3 * 1024 * 1024, 'y')));
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    WatchFolderService service(store, uploads, [&] { return captures; }, [&] { return now; });
    std::atomic_bool slow{true};
    std::atomic_int hookCalls{0};
    service.setFileOperationHook([&](const QString &) { ++hookCalls; if (slow.load()) QThread::msleep(100); });
    watch::Rule rule;
    rule.path = dir.filePath("input");
    rule.action = watch::Action::Move;
    QCOMPARE(service.setRules({rule}), QString());
    QSignalSpy processed(&service, &WatchFolderService::processed);
    service.start(); scan(service);
    scan(service);
    now = 2000;
    scan(service);
    QTRY_VERIFY_WITH_TIMEOUT(hookCalls.load() >= 1, 7000);
    service.stop();
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 1, 7000);
    QCOMPARE(processed.first().at(1).toBool(), false);
    QVERIFY(QFileInfo::exists(source));
    QCOMPARE(QFileInfo(source).size(), qint64(3 * 1024 * 1024));
    QVERIFY(QDir(captures).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
    QVERIFY(!store.processed(watch::fingerprint(source)));
    slow = false;
    service.start(); scan(service);
    now += 3000;
    scan(service);
    QTRY_COMPARE_WITH_TIMEOUT(processed.count(), 2, 10000);
    QCOMPARE(processed.at(1).at(1).toBool(), true);
    QVERIFY(!QFileInfo::exists(source));
    QCOMPARE(QDir(captures).entryList(QDir::Files).size(), 1);
    service.stop();
  }

  // CORE-004: destroying the service mid-copy abandons the result safely and leaves no partial output.
  void destroyingServiceMidCopyIsSafe() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input"));
    const auto captures = dir.filePath("captures");
    const auto source = dir.filePath("input/big.png");
    QVERIFY(writeFile(source, QByteArray(3 * 1024 * 1024, 'z')));
    qint64 now = 0;
    QWidget owner;
    FileUploadController uploads(portsFor(dir), &owner);
    watch::WatchFolders store(dir.filePath("store"));
    std::atomic_int hookCalls{0};
    QSignalSpy *spy = nullptr;
    {
      auto service = std::make_unique<WatchFolderService>(store, uploads, [&] { return captures; }, [&] { return now; });
      service->setFileOperationHook([&](const QString &) { ++hookCalls; QThread::msleep(100); });
      watch::Rule rule;
      rule.path = dir.filePath("input");
      rule.action = watch::Action::Move;
      QCOMPARE(service->setRules({rule}), QString());
      QSignalSpy processed(service.get(), &WatchFolderService::processed);
      spy = &processed;
      service->start(); scan(*service);
      scan(*service);
      now = 2000;
      scan(*service);
      QTRY_VERIFY_WITH_TIMEOUT(hookCalls.load() >= 1, 7000);
      service.reset();
      QCOMPARE(processed.count(), 0);
    }
    Q_UNUSED(spy);
    QTest::qWait(500);
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(QDir(captures).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
    QVERIFY(!store.processed(watch::fingerprint(source)));
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
    QCOMPARE(error->text(), QString("That folder overlaps a folder that is already watched. (watch-overlapping-roots)"));
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
