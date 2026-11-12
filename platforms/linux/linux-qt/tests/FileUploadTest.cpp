#include "TerminalLogCapture.h"
#include "actions/PipelineResultJson.h"
// CORE-020, CORE-023: upload an existing file from the window or a dropped path.
#include "ApplicationWindow.h"
#include "FileUploadController.h"
#include "FileUploadResultDialog.h"
#include "history/CaptureHistory.h"
#include "uploads/CatalogPath.h"
#include "uploads/DestinationCatalog.h"

#include "NativeWorkflowExecutions.h"
#include <QApplication>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProgressDialog>
#include <QPushButton>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QToolButton>
#include <QUrl>

using namespace xerahs::app;
using namespace xerahs::uploads;

namespace {
class ThreadProbe final : public CredentialStore {
public:
  mutable QThread *lookupThread = nullptr;
  CredentialResult store(const QUuid &id, const QString &name, const QString &value) override { return m_inner.store(id, name, value); }
  CredentialResult lookup(const QUuid &id, const QString &name) const override {
    lookupThread = QThread::currentThread();
    return m_inner.lookup(id, name);
  }
  CredentialResult remove(const QUuid &id, const QString &name) override { return m_inner.remove(id, name); }
  CredentialResult removeAll(const QUuid &id) override { return m_inner.removeAll(id); }

private:
  MemoryCredentialStore m_inner;
};

class HttpFixture : public QObject {
public:
  QTcpServer server;
  int status = 200;
  bool holdFirst = false;
  int received = 0;
  QVector<QByteArray> bodies;
  QPointer<QTcpSocket> held;

  HttpFixture() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      auto *peer = server.nextPendingConnection();
      // A deleted peer address can be reused by the next queued transfer.
      m_wire.remove(peer); m_done.remove(peer);
      connect(peer, &QTcpSocket::disconnected, peer, &QObject::deleteLater);
      connect(peer, &QTcpSocket::readyRead, this, [this, peer] { read(peer); });
    });
  }
  UploadDestination destination() const {
    UploadDestination d;
    d.id = QUuid::createUuid();
    d.name = QStringLiteral("Fixture");
    d.requestUrl = QStringLiteral("http://127.0.0.1:%1/upload").arg(server.serverPort());
    d.body = BodyType::MultipartFormData;
    d.fileFormName = QStringLiteral("file");
    d.headers = {{QStringLiteral("Authorization"), QStringLiteral("{secret:token}")}};
    d.resultUrl = QStringLiteral("{json:url}");
    d.thumbnailUrl = QStringLiteral("{json:thumb}");
    d.deletionUrl = QStringLiteral("{json:delete}");
    return d;
  }
  void release() {
    if (held) reply(held);
    held.clear();
  }

private:
  QHash<QTcpSocket *, QByteArray> m_wire;
  QSet<QTcpSocket *> m_done;
  void read(QTcpSocket *peer) {
    if (m_done.contains(peer)) return;
    m_wire[peer] += peer->readAll();
    const auto &wire = m_wire[peer];
    const auto split = wire.indexOf("\r\n\r\n");
    if (split < 0) return;
    const QRegularExpression length(QStringLiteral("Content-Length: (\\d+)"), QRegularExpression::CaseInsensitiveOption);
    const auto match = length.match(QString::fromLatin1(wire.left(split)));
    if (!match.hasMatch() || wire.size() < split + 4 + match.captured(1).toInt()) return;
    m_done.insert(peer);
    ++received;
    bodies.append(wire.mid(split + 4));
    if (holdFirst && received == 1) { held = peer; return; }
    reply(peer);
  }
  void reply(QTcpSocket *peer) const {
    const QByteArray body = status == 500 ? QByteArray("failed fixture-secret")
                                           : QByteArray(R"({"url":"https://example.test/image","thumb":"https://example.test/thumb","delete":"https://example.test/delete"})");
    peer->write("HTTP/1.1 " + QByteArray::number(status) + " Reply\r\nContent-Length: " + QByteArray::number(body.size()) +
                "\r\nConnection: close\r\n\r\n" + body);
    peer->disconnectFromHost();
  }
};

QString writeFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
  QFile file(dir.filePath(name));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
  return file.fileName();
}

QList<FileUploadResultDialog *> dialogs() {
  QList<FileUploadResultDialog *> found;
  for (auto *widget : QApplication::allWidgets())
    if (auto *dialog = qobject_cast<FileUploadResultDialog *>(widget))
      if (dialog->isVisible()) found.append(dialog);
  return found;
}

FileUploadPorts portsFor(const QString &catalogPath, const std::shared_ptr<CredentialStore> &credentials) {
  FileUploadPorts ports;
  ports.catalogPath = catalogPath;
  ports.history = std::make_shared<NativePipelineJournal>(QFileInfo(catalogPath).absolutePath() + "/history");
  static QTemporaryDir executionRoot;
  ports.executions = NativeWorkflowExecutions(executionRoot.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces))).writer();
  ports.credentials = credentials;
  return ports;
}
}  // namespace

class FileUploadTest final : public QObject {
  Q_OBJECT
private slots:
  void existingFileReceiptsSurviveRestart_data() {
    QTest::addColumn<int>("status");
    QTest::newRow("success") << 200;
    QTest::newRow("failure") << 500;
    QTest::newRow("local-preparation-failure") << 0;
  }
  void existingFileReceiptsSurviveRestart() {
    QFETCH(int, status);
    QTemporaryDir dir; HttpFixture http; http.status = status;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    auto ports = portsFor(dir.filePath("destinations.json"), credentials);
    if (status == 0) ports.maxInputBytes = 1;
    DestinationCatalog catalog(ports.catalogPath); QVERIFY(catalog.add(destination).isEmpty());
    const auto path = writeFile(dir, "retained-note.txt", "existing file");
    QVERIFY(!path.isEmpty());
    {
      QWidget owner; FileUploadController controller(ports, &owner);
      QSignalSpy idle(&controller, &FileUploadController::finished);
      controller.upload({path, path});
      QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    }
    NativePipelineJournal restarted(dir.filePath("history"));
    const auto listing = restarted.list(); QVERIFY(listing.diagnostic.isEmpty());
    QCOMPARE(listing.entries.size(), std::size_t(2));
    std::vector<xerahs::actions::RecoveredPipelineJournal> records;
    for (const auto &entry : listing.entries) {
      QVERIFY(entry.parsed.record); QVERIFY(!entry.bytes.contains("fixture-secret"));
      records.push_back(*entry.parsed.record);
    }
    const auto items = xerahs::history::captureHistoryItems(records);
    QCOMPARE(items.size(), std::size_t(2)); QVERIFY(items[0].taskId != items[1].taskId);
    const auto &item = items.front();
    QCOMPARE(item.origin, QString("file-upload")); QCOMPARE(item.destinationName, destination.name);
    QCOMPARE(item.files.front().path, path); QVERIFY(item.createdAt.isValid()); QVERIFY(item.uploadOnly());
    QVERIFY(xerahs::history::matchesHistoryQuery(item, "RETAINED-note"));
    QVERIFY(xerahs::history::matchesHistoryQuery(item, "FIXTURE"));
    const auto receipt = records.front().report.value("extensions").toObject().value("com.xerahs.app").toObject().value("upload_result").toObject();
    if (status == 200) {
      QCOMPARE(receipt.value("url").toString(), QString("https://example.test/image"));
      QCOMPARE(receipt.value("thumbnail_url").toString(), QString("https://example.test/thumb"));
      QCOMPARE(receipt.value("deletion_url").toString(), QString("https://example.test/delete"));
      QVERIFY(item.failureMessage.isEmpty());
    } else {
      QCOMPARE(item.state, QString("failed"));
      if (status == 0) {
        QVERIFY(item.failureMessage.startsWith("input-too-large: ")); QCOMPARE(http.received, 0);
      } else {
        QVERIFY(item.failureMessage.startsWith("http-error: "));
        QVERIFY(item.failureMessage.contains("HTTP 500")); QVERIFY(item.failureMessage.contains("[redacted]"));
      }
      QVERIFY(receipt.value("url").toString().isEmpty());
    }
    QVERIFY(QFile::remove(path));
    QVERIFY(!QFileInfo::exists(item.files.front().path));
    QCOMPARE(xerahs::history::captureHistoryItems(records).size(), std::size_t(2));
  }

  void catalogPathKeepsAnExplicitFileAndOtherwiseUsesTheSharedConfigFile() {
    QCOMPARE(defaultCatalogPath(QStringLiteral("/tmp/upload-destinations.json")), QStringLiteral("/tmp/upload-destinations.json"));
    QVERIFY(defaultCatalogPath({}).endsWith(QStringLiteral("upload-destinations.json")));
  }

  void successShowsTheUrlAndCopyPutsItOnTheClipboard() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HttpFixture http;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json")));
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<ThreadProbe>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto path = writeFile(dir, QStringLiteral("note.txt"), QByteArray("hello upload"));
    QVERIFY(!path.isEmpty());
    QWidget parent;
    parent.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), &parent);
    QSignalSpy progress(&controller, &FileUploadController::progress);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QVERIFY(!progress.isEmpty());
    QVERIFY(credentials->lookupThread);
    QVERIFY(credentials->lookupThread != QThread::currentThread());
    QCOMPARE(dialogs().size(), 1);
    auto *dialog = dialogs().first();
    QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("uploadFileName"))->text(), QStringLiteral("note.txt"));
    auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("uploadUrl"));
    QVERIFY(url);
    QCOMPARE(url->text(), QStringLiteral("https://example.test/image"));
    QCOMPARE(url->isReadOnly(), true);
    auto *deletion = dialog->findChild<QLineEdit *>(QStringLiteral("deletionUrl"));
    QVERIFY(deletion);
    QCOMPARE(deletion->text(), QStringLiteral("https://example.test/delete"));
    const auto mime = QMimeDatabase().mimeTypeForFile(path).name();
    QVERIFY(http.bodies.first().contains("Content-Type: " + mime.toUtf8()));
    QVERIFY(http.bodies.first().contains("filename=\"note.txt\""));
    QVERIFY(http.bodies.first().contains("hello upload"));
    QVERIFY(!dialog->findChild<QLabel *>(QStringLiteral("uploadMessage")));
    dialog->findChild<QPushButton *>(QStringLiteral("copyUploadUrl"))->click();
    QCOMPARE(QApplication::clipboard()->text(), url->text());
    QVERIFY(!QApplication::clipboard()->text().contains(QStringLiteral("fixture-secret")));
  }

  void missingFile() {
    QWidget parent;
    parent.show();
    FileUploadController controller(portsFor(QStringLiteral("/none/destinations.json"), std::make_shared<MemoryCredentialStore>()), &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({QStringLiteral("/no/such/upload-file.txt")});
    QTRY_COMPARE(idle.count(), 1);
    QCOMPARE(dialogs().size(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text(), QStringLiteral("The file does not exist."));
    QVERIFY(!dialogs().first()->findChild<QPushButton *>(QStringLiteral("openUploadDestinations")));
  }

  void directoryIsRejected() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QWidget parent;
    parent.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), std::make_shared<MemoryCredentialStore>()), &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({dir.path()});
    QTRY_COMPARE(idle.count(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text(), QStringLiteral("The path is not a regular file."));
  }

  void oversizeUsesTheEngineCap() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir, QStringLiteral("big.bin"), QByteArray("12345"));
    QVERIFY(!path.isEmpty());
    auto ports = portsFor(dir.filePath(QStringLiteral("destinations.json")), std::make_shared<MemoryCredentialStore>());
    ports.maxInputBytes = 4;
    QWidget parent;
    parent.show();
    FileUploadController controller(ports, &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE(idle.count(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text(), QStringLiteral("input-too-large"));
  }

  void noDefaultDestinationOffersUploadDestinations() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir, QStringLiteral("note.txt"), QByteArray("hello"));
    QVERIFY(!path.isEmpty());
    bool opened = false;
    auto ports = portsFor(dir.filePath(QStringLiteral("missing-destinations.json")), std::make_shared<MemoryCredentialStore>());
    ports.openDestinations = [&] { opened = true; };
    QWidget parent;
    parent.show();
    FileUploadController controller(ports, &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE(idle.count(), 1);
    auto *dialog = dialogs().first();
    QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text(),
             QStringLiteral("No upload destination. Add one in Upload destinations."));
    auto *button = dialog->findChild<QPushButton *>(QStringLiteral("openUploadDestinations"));
    QVERIFY(button);
    button->click();
    QVERIFY(opened);
  }

  void httpErrorShowsTheEngineMessageWithoutTheSecret() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HttpFixture http;
    http.status = 500;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json")));
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto path = writeFile(dir, QStringLiteral("note.txt"), QByteArray("hello"));
    QVERIFY(!path.isEmpty());
    QWidget parent;
    parent.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto message = dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text();
    QVERIFY(message.contains(QStringLiteral("HTTP 500")));
    QVERIFY(message.contains(QStringLiteral("[redacted]")));
    QVERIFY(!message.contains(QStringLiteral("fixture-secret")));
  }

  // WFCAT-002/004/017: one execution per upload, projected identically by record, history and dialog.
  void uploadWritesAnExecutionThatAgreesWithHistoryAndDialog() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    const auto path = writeFile(dir, "note.txt", "hello upload"); QVERIFY(!path.isEmpty());
    NativeWorkflowExecutions store(dir.filePath("executions"));
    auto ports = portsFor(dir.filePath("destinations.json"), credentials); ports.executions = store.writer();
    ApplicationWindow parent(QMap<QString, QAction *>{}); parent.show();
    FileUploadController controller(ports, &parent);
    connect(&controller, &FileUploadController::activityChanged, &parent, [&](const QString &text) { parent.showActivity("File upload", text); });
    QSignalSpy terminal(&controller, &FileUploadController::executionFinished);
    QSignalSpy observation(&controller, &FileUploadController::terminalObserved), activity(&controller, &FileUploadController::activityChanged), notices(&controller, &FileUploadController::notify);
    TerminalLogCapture logs;
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto &entry = listing.entries.front();
    const auto record = xerahs::workflows::parseWorkflowExecution(entry.snapshot, entry.id); QVERIFY(record);
    QCOMPARE(observation.size(), 1); const auto observed = observation.first().first().toJsonObject();
    QCOMPARE(observed.value("state"), entry.result.value("state")); QCOMPARE(observed.value("outputs"), entry.result.value("outputs"));
    const auto terminalText = xerahs::actions::pipelineObservationText(observed);
    QCOMPARE(activity.size(), 1); QCOMPARE(activity.first().first().toString(), terminalText);
    QVERIFY(parent.findChild<QLabel *>("operationStatus")->text().contains(terminalText));
    QCOMPARE(notices.size(), 1); QCOMPARE(notices.first()[1].toString(), terminalText); QVERIFY(notices.first()[2].toBool());
    QCOMPARE(logs.lines, QStringList{"Workflow terminal " + terminalText});
    QVERIFY(!terminalText.contains("note.txt")); QVERIFY(!terminalText.contains("hello upload"));
    QCOMPARE(record->origin, QString("file-upload")); QCOMPARE(record->state, QString("succeeded"));
    QVERIFY(record->startedAt.isValid() && record->finishedAt.isValid()); QVERIFY(record->diagnostic.isEmpty());
    QCOMPARE(record->steps.size(), 2);
    QCOMPARE(record->steps.at(0).toObject().value("action").toString(), QString("read-file"));
    QCOMPARE(record->steps.at(1).toObject().value("action").toString(), QString("upload-file"));
    QCOMPARE(record->steps.at(1).toObject().value("state").toString(), QString("succeeded"));
    QCOMPARE(record->settings.value("operation").toString(), QString("upload"));
    QCOMPARE(record->settings.value("destination").toObject().value("name").toString(), QString("Fixture"));
    QCOMPARE(record->settings.value("destination").toObject().value("id").toString(), destination.id.toString(QUuid::WithoutBraces));
    QCOMPARE(record->outputs, QStringList{entry.id + "/upload.receipt"});
    // The history record shares the execution's stable ID and carries no failure.
    const auto history = NativePipelineJournal(dir.filePath("history")).list();
    QCOMPARE(history.entries.size(), size_t(1)); QCOMPARE(history.entries.front().taskId, entry.id);
    QVERIFY(history.entries.front().parsed.record);
    QVERIFY(history.entries.front().parsed.record->terminal);
    // Dialog and signal expose the same terminal state.
    QCOMPARE(terminal.count(), 1);
    QCOMPARE(terminal.first().at(1).toString(), entry.id); QCOMPARE(terminal.first().at(2).toString(), record->state);
    QCOMPARE(terminal.first().at(3).toStringList(), record->outputs);
    QCOMPARE(dialogs().size(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>("uploadTerminalState")->text(), xerahs::workflows::fileOperationNotice(*record, "note.txt").headline + QLatin1Char('\n') + terminalText);
    // Secrets, URLs and paths never enter the record.
    for (const auto &needle : {QByteArray("fixture-secret"), QByteArray("example.test"), dir.path().toUtf8(), QByteArray("hello upload")})
      QVERIFY2(!entry.snapshot.contains(needle), needle.constData());
  }

  void failuresMapToTheSharedTaxonomyAndNeverReadAsCompleted_data() {
    QTest::addColumn<int>("status"); QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("state"); QTest::addColumn<QString>("diagnostic");
    QTest::newRow("http-5xx-transient") << 500 << "http" << "transient-failure" << "http-error";
    QTest::newRow("http-4xx-permanent") << 404 << "http" << "permanent-failure" << "http-error";
    QTest::newRow("missing-file") << 200 << "missing" << "permanent-failure" << "input-missing";
    QTest::newRow("not-a-file") << 200 << "directory" << "validation-failure" << "input-not-file";
    QTest::newRow("no-destination") << 200 << "nodestination" << "unavailable-dependency" << "destination-missing";
  }
  void failuresMapToTheSharedTaxonomyAndNeverReadAsCompleted() {
    QFETCH(int, status); QFETCH(QString, kind); QFETCH(QString, state); QFETCH(QString, diagnostic);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.status = status; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    auto credentials = std::make_shared<MemoryCredentialStore>();
    const auto destination = http.destination();
    if (kind != "nodestination") {
      DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(destination).isEmpty());
      QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    }
    QString path = writeFile(dir, "note.txt", "hello");
    if (kind == "missing") path = dir.filePath("absent.txt");
    if (kind == "directory") path = dir.path();
    NativeWorkflowExecutions store(dir.filePath("executions"));
    auto ports = portsFor(dir.filePath("destinations.json"), credentials); ports.executions = store.writer();
    ApplicationWindow parent(QMap<QString, QAction *>{}); parent.show();
    FileUploadController controller(ports, &parent);
    connect(&controller, &FileUploadController::activityChanged, &parent, [&](const QString &text) { parent.showActivity("File upload", text); });
    QSignalSpy terminal(&controller, &FileUploadController::executionFinished);
    QSignalSpy observation(&controller, &FileUploadController::terminalObserved), activity(&controller, &FileUploadController::activityChanged), notices(&controller, &FileUploadController::notify);
    TerminalLogCapture logs;
    QSignalSpy completed(&controller, &FileUploadController::fileCompleted);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id); QVERIFY(record);
    QCOMPARE(record->state, state); QCOMPARE(record->diagnostic, diagnostic); QVERIFY(record->outputs.isEmpty());
    const auto observed = observation.first().first().toJsonObject();
    QCOMPARE(observed.value("state"), listing.entries.front().result.value("state"));
    QCOMPARE(observed.value("outputs"), listing.entries.front().result.value("outputs"));
    const auto text = xerahs::actions::pipelineObservationText(observed);
    QCOMPARE(activity.first().first().toString(), text); QCOMPARE(notices.first()[1].toString(), text); QVERIFY(!notices.first()[2].toBool());
    QCOMPARE(logs.lines, QStringList{"Workflow terminal " + text});

    const auto last = record->steps.last().toObject();
    QCOMPARE(last.value("state").toString(), QString("failed"));
    QCOMPARE(last.value("action").toString(), http.received ? QString("upload-file") : QString("read-file"));
    QVERIFY(last.contains("failure_class"));
    QCOMPARE(terminal.first().at(2).toString(), state);
    QCOMPARE(completed.first().at(1).toBool(), false);
    QCOMPARE(dialogs().size(), 1);
    const auto label = dialogs().first()->findChild<QLabel *>("uploadTerminalState")->text();
    QVERIFY(label.contains(state)); QVERIFY(!label.startsWith("Uploaded"));
    QVERIFY(!listing.entries.front().snapshot.contains("fixture-secret"));
    QVERIFY(!dialogs().first()->findChild<QLineEdit *>("uploadUrl"));
  }

  void terminalStorageFailureAgreesWithDurableHistoryWithoutReplayingUpload() {
    QTemporaryDir dir; HttpFixture http; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination(); DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>(); QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    NativeWorkflowExecutions store(dir.filePath("executions")); const auto writer = store.writer();
    auto ports = portsFor(dir.filePath("destinations.json"), credentials);
    ports.executions = [writer](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) {
      if (!QJsonDocument::fromJson(bytes).object().value("finishedAt").toString().isEmpty()) return xerahs::actions::JournalWrite{false, "pipeline-journal-write-failed"};
      return writer(id, expected, bytes);
    };
    QWidget parent; FileUploadController controller(ports, &parent);
    QSignalSpy observed(&controller, &FileUploadController::terminalObserved), notices(&controller, &FileUploadController::notify), activity(&controller, &FileUploadController::activityChanged);
    QSignalSpy idle(&controller, &FileUploadController::finished), effect(&controller, &FileUploadController::fileCompleted); TerminalLogCapture logs;
    controller.upload({writeFile(dir, "PRIVATE-file.txt", "PRIVATE-content")}); QTRY_COMPARE_WITH_TIMEOUT(idle.size(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto observation = observed.first().first().toJsonObject();
    QCOMPARE(observation.value("state"), listing.entries.front().result.value("state")); QCOMPARE(observation.value("state"), QJsonValue("outcome-unconfirmed"));
    QCOMPARE(observation.value("outputs"), listing.entries.front().result.value("outputs")); QVERIFY(observation.value("outputs").toArray().isEmpty());
    const auto text = xerahs::actions::pipelineObservationText(observation);
    QCOMPARE(activity.first().first().toString(), text); QCOMPARE(notices.first()[1].toString(), text); QVERIFY(!notices.first()[2].toBool());
    QCOMPARE(logs.lines, QStringList{"Workflow terminal " + text}); QVERIFY(!text.contains("PRIVATE"));
    QVERIFY(effect.first()[1].toBool()); QCOMPARE(http.received, 1);
  }

  void cancellationIsTerminalCancelledWithoutOutputs() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.holdFirst = true; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    const auto path = writeFile(dir, "one.txt", "one");
    NativeWorkflowExecutions store(dir.filePath("executions"));
    auto ports = portsFor(dir.filePath("destinations.json"), credentials); ports.executions = store.writer();
    QWidget parent; parent.show();
    FileUploadController controller(ports, &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    controller.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id); QVERIFY(record);
    QCOMPARE(record->state, QString("cancelled")); QVERIFY(record->outputs.isEmpty());
    QCOMPARE(record->steps.last().toObject().value("state").toString(), QString("cancelled"));
    QVERIFY(dialogs().first()->findChild<QLabel *>("uploadTerminalState")->text().contains("cancelled"));
  }

  void cancelStopsTheQueue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HttpFixture http;
    http.holdFirst = true;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json")));
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto first = writeFile(dir, QStringLiteral("one.txt"), QByteArray("one"));
    const auto second = writeFile(dir, QStringLiteral("two.txt"), QByteArray("two"));
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    QWidget parent;
    parent.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({first, second});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    QProgressDialog *progress = nullptr;
    for (auto *widget : QApplication::allWidgets())
      if (auto *dialog = qobject_cast<QProgressDialog *>(widget)) progress = dialog;
    QVERIFY(progress);
    auto *button = progress->findChild<QPushButton *>(QStringLiteral("cancelFileUpload"));
    QVERIFY(button);
    button->click();
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QCOMPARE(http.received, 1);
    QCOMPARE(dialogs().size(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage"))->text(), QStringLiteral("cancelled"));
    QTest::qWait(200);
    QCOMPARE(http.received, 1);
    QCOMPARE(dialogs().size(), 1);
  }

  void cancelledTransferKeepsPublicationWarningSeparateFromStatusAndHistory() {
    QTemporaryDir dir; HttpFixture http; http.holdFirst = true;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    const auto catalogPath = dir.filePath("destinations.json");
    DestinationCatalog catalog(catalogPath); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, "token", "fixture-secret").ok());
    const auto path = writeFile(dir, "cancelled.txt", "synthetic upload"); QVERIFY(!path.isEmpty());
    QWidget owner; FileUploadController controller(portsFor(catalogPath, credentials), &owner);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path}); QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    controller.cancel(); QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QCOMPARE(dialogs().size(), 1);
    QCOMPARE(dialogs().first()->findChild<QLabel *>("uploadMessage")->text(), QString("cancelled"));
    auto *warning = dialogs().first()->findChild<QLabel *>("uploadPublicationStatus");
    QVERIFY(warning); QVERIFY(warning->isVisible());
    QVERIFY(warning->text().contains("publication is unconfirmed"));
    NativePipelineJournal history(dir.filePath("history"));
    const auto records = history.list(); QCOMPARE(records.entries.size(), size_t(1));
    QVERIFY(records.entries.front().parsed.record);
    const auto upload = records.entries.front().parsed.record->report.value("extensions").toObject()
        .value("com.xerahs.app").toObject().value("upload_result").toObject();
    QVERIFY(upload.value("url").toString().isEmpty());
    QCOMPARE(upload.value("attempts").toInt(), 1);
    QVERIFY(upload.value("error_message").toString().contains("publication is unconfirmed"));
    QVERIFY(QFileInfo::exists(path)); QCOMPARE(http.received, 1);
  }

  void droppedFilesUploadSequentially() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HttpFixture http;
    http.holdFirst = true;
    QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json")));
    QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto first = writeFile(dir, QStringLiteral("one.txt"), QByteArray("alpha-file"));
    const auto second = writeFile(dir, QStringLiteral("two.txt"), QByteArray("beta-file"));
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    ApplicationWindow window({});
    window.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), &window);
    connect(&window, &ApplicationWindow::filesDropped, &controller, &FileUploadController::upload);
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(first), QUrl(QStringLiteral("https://example.test/skip")), QUrl::fromLocalFile(second)});
    // Qt delivers a drop only to the widget that accepted the matching drag enter.
    QDragEnterEvent enter(QPoint(8, 8), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.centralWidget(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(8, 8), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.centralWidget(), &drop);
    QVERIFY(drop.isAccepted());
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    QVERIFY(http.bodies.first().contains("alpha-file"));
    QTest::qWait(250);
    QCOMPARE(http.received, 1);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    http.release();
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 2, 7000);
    QVERIFY(http.bodies.at(1).contains("beta-file"));
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QStringList names;
    for (auto *dialog : dialogs()) names.append(dialog->findChild<QLabel *>(QStringLiteral("uploadFileName"))->text());
    QCOMPARE(names.size(), 2);
    QVERIFY(names.contains(QStringLiteral("one.txt")));
    QVERIFY(names.contains(QStringLiteral("two.txt")));
    for (auto *dialog : dialogs())
      QCOMPARE(dialog->findChild<QLineEdit *>(QStringLiteral("uploadUrl"))->text(), QStringLiteral("https://example.test/image"));
  }

  void windowDropRejectsTextOnlyPayload() {
    ApplicationWindow window({}); window.show();
    QSignalSpy dropped(&window, &ApplicationWindow::filesDropped);
    QMimeData text; text.setText(QStringLiteral("private-drag-fixture"));
    QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.centralWidget(), &enter); QVERIFY(!enter.isAccepted());
    QDropEvent drop(QPointF(4, 4), Qt::CopyAction, &text, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop); QVERIFY(!drop.isAccepted()); QCOMPARE(dropped.count(), 0);
  }

  void windowDropIgnoresNonLocalUrls() {
    ApplicationWindow window({});
    window.show();
    QSignalSpy dropped(&window, &ApplicationWindow::filesDropped);
    const auto first = QUrl::fromLocalFile(QStringLiteral("/tmp/upload-a.txt")).toLocalFile();
    const auto second = QUrl::fromLocalFile(QStringLiteral("/tmp/upload-b.txt")).toLocalFile();
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(first), QUrl(QStringLiteral("https://example.test/ignored")), QUrl::fromLocalFile(second)});
    QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.centralWidget(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(QPointF(4, 4), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &drop);
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.first().first().toStringList(), QStringList({first, second}));

    QMimeData remote;
    remote.setUrls({QUrl(QStringLiteral("https://example.test/only"))});
    QDragEnterEvent remoteEnter(QPoint(4, 4), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.centralWidget(), &remoteEnter);
    QVERIFY(!remoteEnter.isAccepted());
    QDropEvent remoteDrop(QPointF(4, 4), Qt::CopyAction, &remote, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&window, &remoteDrop);
    QVERIFY(!remoteDrop.isAccepted());
    QCOMPARE(dropped.count(), 1);
  }

  void explicitUploadAfterCancelRunsOnceAndDoesNotRepeatTheCancelledTransfer() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.holdFirst = true; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json"))); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto first = writeFile(dir, QStringLiteral("one.txt"), QByteArray("alpha-file"));
    const auto dropped = writeFile(dir, QStringLiteral("two.txt"), QByteArray("beta-file"));
    const auto third = writeFile(dir, QStringLiteral("three.txt"), QByteArray("gamma-file"));
    QVERIFY(!first.isEmpty() && !dropped.isEmpty() && !third.isEmpty());
    QWidget parent; parent.show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), &parent);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    QSignalSpy completed(&controller, &FileUploadController::fileCompleted);
    controller.upload({first, dropped});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    controller.cancel();
    controller.upload({third});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QCOMPARE(http.received, 2);
    QCOMPARE(http.bodies.size(), 2);
    QVERIFY(http.bodies.at(0).contains("alpha-file"));
    QVERIFY(http.bodies.at(1).contains("gamma-file"));
    QVERIFY(!http.bodies.at(0).contains("beta-file"));
    QVERIFY(!http.bodies.at(1).contains("beta-file"));
    QVERIFY(!http.bodies.at(1).contains("alpha-file"));
    QStringList messages, urls;
    for (auto *dialog : dialogs()) {
      if (auto *message = dialog->findChild<QLabel *>(QStringLiteral("uploadMessage"))) messages.append(message->text());
      if (auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("uploadUrl"))) urls.append(url->text());
    }
    QCOMPARE(messages, QStringList{QStringLiteral("cancelled")});
    QCOMPARE(urls, QStringList{QStringLiteral("https://example.test/image")});
    QCOMPARE(completed.count(), 2);
    QCOMPARE(completed.at(0).at(0).toString(), first);
    QCOMPARE(completed.at(0).at(1).toBool(), false);
    QCOMPARE(completed.at(1).at(0).toString(), third);
    QCOMPARE(completed.at(1).at(1).toBool(), true);
    const auto history = NativePipelineJournal(dir.filePath(QStringLiteral("history"))).list();
    QCOMPARE(history.entries.size(), std::size_t(2));
    int successes = 0;
    for (const auto &entry : history.entries) {
      QVERIFY(entry.parsed.record);
      const auto upload = entry.parsed.record->report.value(QStringLiteral("extensions")).toObject()
          .value(QStringLiteral("com.xerahs.app")).toObject().value(QStringLiteral("upload_result")).toObject();
      const auto path = entry.parsed.record->report.value(QStringLiteral("extensions")).toObject()
          .value(QStringLiteral("com.xerahs.app")).toObject().value(QStringLiteral("file_upload")).toObject()
          .value(QStringLiteral("path")).toString();
      if (path == first) {
        QVERIFY(upload.value(QStringLiteral("url")).toString().isEmpty());
        QVERIFY(upload.value(QStringLiteral("error_message")).toString().contains(QStringLiteral("publication is unconfirmed")));
      } else if (path == third) {
        QCOMPARE(upload.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.test/image"));
        ++successes;
      } else {
        QFAIL("cancelled queue entry was uploaded");
      }
    }
    QCOMPARE(successes, 1);
    QTest::qWait(200);
    QCOMPARE(http.received, 2);
    QCOMPARE(idle.count(), 1);
  }

  void dialogOwnerDestructionRetainsTheKnownResultWithoutAnotherAttempt() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.holdFirst = true; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json"))); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto path = writeFile(dir, QStringLiteral("kept.txt"), QByteArray("kept-bytes"));
    QVERIFY(!path.isEmpty());
    QObject lifetime;
    auto *owner = new QWidget; owner->show();
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), owner, &lifetime);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    QSignalSpy completed(&controller, &FileUploadController::fileCompleted);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    delete owner;
    QApplication::processEvents();
    http.release();
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    QCOMPARE(http.received, 1);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(1).toBool(), true);
    QCOMPARE(dialogs().size(), 1);
    QCOMPARE(dialogs().first()->findChild<QLineEdit *>(QStringLiteral("uploadUrl"))->text(), QStringLiteral("https://example.test/image"));
    QVERIFY(!dialogs().first()->findChild<QLabel *>(QStringLiteral("uploadMessage")));
    const auto history = NativePipelineJournal(dir.filePath(QStringLiteral("history"))).list();
    QCOMPARE(history.entries.size(), std::size_t(1));
    const auto upload = history.entries.front().parsed.record->report.value(QStringLiteral("extensions")).toObject()
        .value(QStringLiteral("com.xerahs.app")).toObject().value(QStringLiteral("upload_result")).toObject();
    QCOMPARE(upload.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.test/image"));
    QTest::qWait(200);
    QCOMPARE(http.received, 1);
    QCOMPARE(dialogs().size(), 1);
    // The retained receipt is parentless once its owner is gone. Close it here
    // so a later case does not treat that receipt as its own result.
    for (auto *dialog : dialogs()) dialog->close();
    QApplication::processEvents();
  }

  void uploadAfterDialogOwnerLossStillHonorsItsCancelButton() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.holdFirst = true; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json"))); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto path = writeFile(dir, QStringLiteral("cancel-after-owner.txt"), QByteArray("bytes"));
    auto *owner = new QWidget;
    FileUploadController controller(portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials), owner);
    delete owner;
    QSignalSpy idle(&controller, &FileUploadController::finished);
    QSignalSpy completed(&controller, &FileUploadController::fileCompleted);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    QProgressDialog *progress = nullptr;
    for (auto *widget : QApplication::topLevelWidgets())
      if (widget->objectName() == QStringLiteral("fileUploadProgress") && widget->isVisible()) progress = qobject_cast<QProgressDialog *>(widget);
    QVERIFY(progress);
    auto *cancel = progress->findChild<QPushButton *>(QStringLiteral("cancelFileUpload")); QVERIFY(cancel);
    cancel->click();
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 1500);
    QCOMPARE(completed.count(), 1); QCOMPARE(completed.first().at(1).toBool(), false);
    QCOMPARE(http.received, 1);
    for (auto *dialog : dialogs()) dialog->close();
  }

  void destroyingTheControllerDuringTransferDoesNotRecordSuccessOrSendAgain() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    HttpFixture http; http.holdFirst = true; QVERIFY(http.server.listen(QHostAddress::LocalHost));
    const auto destination = http.destination();
    DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json"))); QVERIFY(catalog.add(destination).isEmpty());
    auto credentials = std::make_shared<MemoryCredentialStore>();
    QVERIFY(credentials->store(destination.id, QStringLiteral("token"), QStringLiteral("fixture-secret")).ok());
    const auto path = writeFile(dir, QStringLiteral("in-flight.txt"), QByteArray("in-flight"));
    QVERIFY(!path.isEmpty());
    NativeWorkflowExecutions store(dir.filePath(QStringLiteral("executions")));
    auto ports = portsFor(dir.filePath(QStringLiteral("destinations.json")), credentials);
    ports.executions = store.writer();
    auto *owner = new QWidget; owner->show();
    auto *controller = new FileUploadController(ports, owner, owner);
    controller->upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 7000);
    const auto before = dialogs();
    delete owner;
    QApplication::processEvents();
    QTest::qWait(250);
    QCOMPARE(http.received, 1);
    for (auto *dialog : dialogs()) QVERIFY(before.contains(dialog));
    const auto listing = store.list();
    QCOMPARE(listing.entries.size(), std::size_t(1));
    QCOMPARE(listing.entries.front().result.value(QStringLiteral("state")).toString(), QStringLiteral("outcome-unconfirmed"));
    QVERIFY(listing.entries.front().result.value(QStringLiteral("outputs")).toArray().isEmpty());
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id);
    QVERIFY(record);
    QCOMPARE(record->state, QStringLiteral("capturing"));
    QVERIFY(record->outputs.isEmpty());
    QVERIFY(!record->finishedAt.isValid());
    const auto history = NativePipelineJournal(dir.filePath(QStringLiteral("history"))).list();
    QCOMPARE(history.entries.size(), std::size_t(0));
    QTest::qWait(200);
    QCOMPARE(http.received, 1);
    for (auto *dialog : dialogs()) QVERIFY(before.contains(dialog));
  }

  void uploadFileActionIsSharedWithTheWindow() {
    QAction upload(QStringLiteral("Upload file…"));
    ApplicationWindow window({{QStringLiteral("upload-file"), &upload}});
    window.activate();
    auto *button = window.findChild<QToolButton *>(QStringLiteral("upload-file"));
    QVERIFY(button);
    QSignalSpy invoked(&upload, &QAction::triggered);
    button->click();
    QCOMPARE(invoked.count(), 1);
    bool found = false;
    for (auto *action : window.menuBar()->actions())
      if (auto *menu = action->menu()) found |= menu->actions().contains(&upload);
    QVERIFY(found);
  }
};

QTEST_MAIN(FileUploadTest)
#include "FileUploadTest.moc"
