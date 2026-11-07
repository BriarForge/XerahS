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
#include <QLineEdit>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProgressDialog>
#include <QPushButton>
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
    QWidget parent; parent.show();
    FileUploadController controller(ports, &parent);
    QSignalSpy terminal(&controller, &FileUploadController::executionFinished);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto &entry = listing.entries.front();
    const auto record = xerahs::workflows::parseWorkflowExecution(entry.snapshot, entry.id); QVERIFY(record);
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
    QCOMPARE(dialogs().first()->findChild<QLabel *>("uploadTerminalState")->text(), xerahs::workflows::fileOperationNotice(*record, "note.txt").headline);
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
    QWidget parent; parent.show();
    FileUploadController controller(ports, &parent);
    QSignalSpy terminal(&controller, &FileUploadController::executionFinished);
    QSignalSpy completed(&controller, &FileUploadController::fileCompleted);
    QSignalSpy idle(&controller, &FileUploadController::finished);
    controller.upload({path});
    QTRY_COMPARE_WITH_TIMEOUT(idle.count(), 1, 7000);
    const auto listing = store.list(); QCOMPARE(listing.entries.size(), size_t(1));
    const auto record = xerahs::workflows::parseWorkflowExecution(listing.entries.front().snapshot, listing.entries.front().id); QVERIFY(record);
    QCOMPARE(record->state, state); QCOMPARE(record->diagnostic, diagnostic); QVERIFY(record->outputs.isEmpty());
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
