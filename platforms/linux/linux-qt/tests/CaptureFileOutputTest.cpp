#include "CaptureFileOutput.h"
#include "CapturePipeline.h"
#include <QApplication>
#include <QClipboard>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <sys/stat.h>
#include <unistd.h>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class FileManager final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")
public:
  explicit FileManager(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerService("org.freedesktop.FileManager1") ||
        !this->bus.registerObject("/org/freedesktop/FileManager1", this, QDBusConnection::ExportAllSlots)) qFatal("private file manager registration failed");
  }
  ~FileManager() override { bus.unregisterObject("/org/freedesktop/FileManager1"); bus.unregisterService("org.freedesktop.FileManager1"); }
  QDBusConnection bus; int calls = 0; QStringList items; QString startup; bool denied = false, delayed = false;
  QString errorName;
  QDBusMessage pending;
public slots:
  void ShowItems(const QStringList &uris, const QString &startupId) {
    ++calls; items = uris; startup = startupId;
    if (!errorName.isEmpty()) sendErrorReply(errorName, "private fixture secret");
    else if (denied) sendErrorReply("org.freedesktop.DBus.Error.AccessDenied", "private fixture secret");
    else if (delayed) { setDelayedReply(true); pending = message(); }
  }
};
class CaptureFileOutputTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()};
private slots:
  void initTestCase() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "file-output-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "file-output-client"); QVERIFY(server.isConnected() && client.isConnected());
  }
  void liveIdentityRejectsChangedReplacedLinkedSymlinkedAndNonregularFiles_data() {
    QTest::addColumn<QString>("change");
    for (const auto &change : {"content", "replace", "symlink", "hardlink", "shared permissions", "fifo", "ancestor symlink", "missing identity"})
      QTest::newRow(change) << QString(change);
  }
  void liveIdentityRejectsChangedReplacedLinkedSymlinkedAndNonregularFiles() {
    QFETCH(QString, change); QTemporaryDir dir; const auto saved = saveCapture(pixels(), dir.filePath("output"), "capture"); QVERIFY(saved.file);
    auto file = *saved.file; QVERIFY(file.identity); QVERIFY(validateCaptureFile(file).isEmpty()); const auto path = QFile::encodeName(file.path);
    if (change == "content") { QFile edit(file.path); QVERIFY(edit.open(QIODevice::WriteOnly)); QCOMPARE(edit.write(QByteArray(file.byteLength, 'x')), file.byteLength); }
    else if (change == "shared permissions") QVERIFY(QFile::setPermissions(file.path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther));
    else if (change == "hardlink") QCOMPARE(::link(path.constData(), QFile::encodeName(dir.filePath("linked")).constData()), 0);
    else if (change == "missing identity") file.identity.reset();
    else if (change == "ancestor symlink") {
      QVERIFY(QDir().rename(dir.filePath("output"), dir.filePath("moved")));
      QCOMPARE(::symlink(QFile::encodeName(dir.filePath("moved")).constData(), QFile::encodeName(dir.filePath("output")).constData()), 0);
    } else {
      QVERIFY(QFile::rename(file.path, dir.filePath("old.png")));
      if (change == "symlink") QCOMPARE(::symlink(QFile::encodeName(dir.filePath("old.png")).constData(), path.constData()), 0);
      else if (change == "fifo") QCOMPARE(::mkfifo(path.constData(), 0600), 0);
      else { const auto replacement = saveCapture(pixels(), dir.filePath("output"), "capture"); QVERIFY(replacement.file); }
    }
    QVERIFY(!validateCaptureFile(file).isEmpty()); QApplication::clipboard()->setText("unchanged fixture");
    QCOMPARE(copyCaptureFile("file-clipboard", file).state, ActionState::Failed); QCOMPARE(QApplication::clipboard()->text(), QString("unchanged fixture"));
  }
  void actualFileAndPathClipboardCarryCopyUrisWithoutChangingTheSavedBytes() {
    QTemporaryDir dir; const auto saved = saveCapture(pixels(), dir.path(), "capture ü #"); QVERIFY(saved.file); const auto before = bytes(saved.file->path);
    const auto outcome = copyCaptureFile("file-clipboard", *saved.file); const auto *mime = QApplication::clipboard()->mimeData(); QVERIFY(mime);
    const auto url = QUrl::fromLocalFile(saved.file->path); QCOMPARE(mime->urls(), QList<QUrl>{url});
    QCOMPARE(mime->data("x-special/gnome-copied-files"), QByteArray("copy\n") + url.toEncoded());
    QCOMPARE(outcome.state, QApplication::clipboard()->ownsClipboard() ? ActionState::Succeeded : ActionState::Failed);
    copyCaptureFile("path-clipboard", *saved.file); QCOMPARE(QApplication::clipboard()->text(), saved.file->path);
    QCOMPARE(bytes(saved.file->path), before); QVERIFY(validateCaptureFile(*saved.file).isEmpty());
  }
  void actualShowItemsIsAsynchronousBoundedAndValueFree() {
    QTemporaryDir dir; const auto saved = saveCapture(pixels(), dir.path(), "capture ü #"); QVERIFY(saved.file); QObject owner;
    std::optional<ActionOutcome> result; revealCaptureFile(*saved.file, &owner, [&](ActionOutcome value) { result = value; }, client);
    QTRY_VERIFY(result); QCOMPARE(result->state, ActionState::Failed); QCOMPARE(result->diagnostic, std::optional<QString>{"reveal-unavailable"});
    FileManager manager(server); result.reset(); revealCaptureFile(*saved.file, &owner, [&](ActionOutcome value) { result = value; }, client);
    QTRY_VERIFY(result); QCOMPARE(result->state, ActionState::Succeeded); QCOMPARE(manager.calls, 1);
    QCOMPARE(manager.items, QStringList{QUrl::fromLocalFile(saved.file->path).toString(QUrl::FullyEncoded)}); QVERIFY(manager.startup.isEmpty());
    manager.denied = true; result.reset(); revealCaptureFile(*saved.file, &owner, [&](ActionOutcome value) { result = value; }, client);
    QTRY_VERIFY(result); QCOMPARE(result->diagnostic, std::optional<QString>{"reveal-unavailable"});
    manager.denied = false; manager.delayed = true; result.reset(); int callbacks = 0;
    revealCaptureFile(*saved.file, &owner, [&](ActionOutcome value) { result = value; ++callbacks; }, client, 100);
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat); QTRY_VERIFY(result);
    QCOMPARE(result->diagnostic, std::optional<QString>{"reveal-outcome-unknown"}); QCOMPARE(callbacks, 1);
    QVERIFY(manager.bus.send(manager.pending.createReply())); QTest::qWait(20); QCOMPARE(callbacks, 1);
  }
  void transportErrorRepliesPreserveUnknownDelivery_data() {
    QTest::addColumn<QString>("name");
    for (const auto &name : {"Timeout", "TimedOut", "NoReply", "Disconnected"}) QTest::newRow(name) << QString(name);
  }
  void transportErrorRepliesPreserveUnknownDelivery() {
    QFETCH(QString, name); QTemporaryDir dir; const auto saved = saveCapture(pixels(), dir.path(), "capture"); QVERIFY(saved.file);
    FileManager manager(server); manager.errorName = "org.freedesktop.DBus.Error." + name;
    QObject owner; std::optional<ActionOutcome> result;
    revealCaptureFile(*saved.file, &owner, [&](ActionOutcome value) { result = value; }, client);
    QTRY_VERIFY(result); QVERIFY(result->outcomeUnknown); QCOMPARE(result->diagnostic, std::optional<QString>{"reveal-outcome-unknown"});
  }
  void ownerDestructionAfterDispatchKeepsUnknownDeliveryAndReleasesTheWait() {
    QTemporaryDir dir; const auto saved = saveCapture(pixels(), dir.path(), "capture"); QVERIFY(saved.file); FileManager manager(server); manager.delayed = true;
    auto owner = std::make_unique<QObject>(); std::optional<ActionOutcome> result; int callbacks = 0;
    revealCaptureFile(*saved.file, owner.get(), [&](ActionOutcome value) { result = value; ++callbacks; }, client);
    QTRY_COMPARE(manager.calls, 1); owner.reset(); QVERIFY(result); QCOMPARE(result->diagnostic, std::optional<QString>{"reveal-outcome-unknown"});
    QVERIFY(manager.bus.send(manager.pending.createReply())); QTest::qWait(20); QCOMPARE(callbacks, 1);
  }
  void failedSaveSkipsFileConsumersAndRetryRunsOriginalPrerequisitesOnly() {
    QTemporaryDir dir; int saves = 0; QStringList dispatched; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { return ++saves > 1 && image.save(out, "PNG"); };
    ports.clipboard = [](const QImage &, const CancellationToken &) { return ActionOutcome{}; };
    ports.fileOutput = [&](const QString &action, const CaptureFile &file, const CancellationToken &) {
      if (!validateCaptureFile(file).isEmpty()) return ActionOutcome{ActionState::Failed, "fixture-invalid-file", {}};
      dispatched.append(action); return ActionOutcome{};
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline original(pixels(), dir.filePath("output"), "capture", {"save", "image-clipboard", "file-clipboard", "path-clipboard", "reveal"}, nullptr, ports, journal);
    QSignalSpy one(&original, &CapturePipeline::finished); original.start(); QTRY_COMPARE(one.count(), 1); const auto source = receipt(one); QVERIFY(source->retryPlan().available());
    QVERIFY(dispatched.isEmpty()); for (size_t i = 2; i < 5; ++i) { QCOMPARE(source->pipeline.orderedResults[i].state, ActionState::Skipped); QCOMPARE(source->pipeline.orderedResults[i].missingArtifact, std::optional<QString>{"save.file"}); }
    CapturePipeline retry(source, nullptr, ports, journal); QSignalSpy two(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(two.count(), 1);
    const auto next = receipt(two); QVERIFY(next->file); QCOMPARE(QImage(next->file->path), pixels()); QCOMPARE(saves, 2);
    QCOMPARE(dispatched, (QStringList{"file-clipboard", "path-clipboard", "reveal"})); QCOMPARE(next->pipeline.retry->preservedSuccessfulActions, QStringList{"image-clipboard"});
    for (const auto &entry : journal->list().entries) { QVERIFY(entry.parsed.record); QVERIFY(entry.parsed.record->terminal); }
  }
  void productionPipelineRevealAndClaimedCancellationPreserveRealAcknowledgement() {
    QTemporaryDir dir; FileManager manager(server); manager.delayed = true;
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "reveal", "analyze"}, nullptr, {}, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(manager.calls, 1);
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    pipeline.cancel(); QTest::qWait(20); QCOMPARE(done.count(), 0); QVERIFY(manager.bus.send(manager.pending.createReply()));
    QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(QImage(result->file->path), pixels());
    QCOMPARE(result->pipeline.state, PipelineState::Cancelled); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded);
    QVERIFY(result->pipeline.orderedResults[1].cancellationArrivedDuring); QCOMPARE(result->pipeline.orderedResults[2].state, ActionState::Cancelled);
    QCOMPARE(manager.items, QStringList{QUrl::fromLocalFile(result->file->path).toString(QUrl::FullyEncoded)});
    QVERIFY(journal->list().entries[0].parsed.record->terminal);
  }
  void unknownDeliveryLeavesAnInterruptedJournalAndCannotAuthorizeRetry() {
    QTemporaryDir dir; auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state")); CaptureActionPorts ports;
    ports.fileOutput = [](const QString &, const CaptureFile &, const CancellationToken &) {
      return ActionOutcome{ActionState::Failed, "reveal-outcome-unknown", {}, true};
    };
    CapturePipeline pipeline(pixels(), dir.filePath("output"), "capture", {"save", "reveal"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QCOMPARE(result->pipeline.state, PipelineState::Interrupted); QCOMPARE(result->diagnostic, QString("reveal-outcome-unknown"));
    QCOMPARE(result->pipeline.orderedResults.size(), size_t(1)); QVERIFY(!result->retryPlan().available());
    const auto listing = journal->list(); QCOMPARE(listing.entries.size(), size_t(1)); QVERIFY(listing.entries[0].parsed.record);
    QVERIFY(!listing.entries[0].parsed.record->terminal); QCOMPARE(*listing.entries[0].parsed.record->pendingAction, QString("reveal"));
    QCOMPARE(listing.entries[0].parsed.record->report.value("pipeline_state"), QJsonValue("interrupted"));
  }
  void cancelledUnclaimedFileOutputCannotMutateClipboardOrStartLaterConsumers() {
    QTemporaryDir dir; int dispatched = 0; CaptureActionPorts ports;
    ports.fileOutput = [&](const QString &, const CaptureFile &, const CancellationToken &) { ++dispatched; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"save", "file-clipboard", "path-clipboard"}, nullptr, ports);
    connect(&pipeline, &CapturePipeline::activityChanged, &pipeline, [&](const QString &text) { if (text.startsWith("Copying the saved file")) pipeline.cancel(); });
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); QCOMPARE(dispatched, 0);
    const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(result->pipeline.state, PipelineState::Cancelled);
    QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Cancelled); QCOMPARE(result->pipeline.orderedResults[2].state, ActionState::Cancelled);
  }
  void failedConsumerRetryKeepsEarlierSuccessesAndRejectsExternalReplacement() {
    QTemporaryDir dir; int saves = 0; QStringList dispatched; bool reject = true; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { ++saves; return image.save(out, "PNG"); };
    ports.fileOutput = [&](const QString &action, const CaptureFile &, const CancellationToken &) { dispatched.append(action); return action == "path-clipboard" && reject ? ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}} : ActionOutcome{}; };
    CapturePipeline original(pixels(), dir.path(), "capture", {"save", "file-clipboard", "path-clipboard", "reveal"}, nullptr, ports);
    QSignalSpy one(&original, &CapturePipeline::finished); original.start(); QTRY_COMPARE(one.count(), 1); const auto source = receipt(one); const auto before = bytes(source->file->path);
    reject = false; CapturePipeline retry(source, nullptr, ports); QSignalSpy two(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(two.count(), 1);
    QCOMPARE(saves, 1); QCOMPARE(dispatched, (QStringList{"file-clipboard", "path-clipboard", "reveal", "path-clipboard"}));
    QCOMPARE(receipt(two)->pipeline.orderedResults[0].inputArtifact, std::optional<QString>{"save.file"}); QCOMPARE(bytes(source->file->path), before);
    QVERIFY(QFile::remove(source->file->path)); QVERIFY(saveCapture(pixels(), dir.path(), "capture").file);
    CapturePipeline changed(source, nullptr, ports); QSignalSpy three(&changed, &CapturePipeline::finished); changed.start(); QTRY_COMPARE(three.count(), 1);
    QCOMPARE(dispatched.size(), 4); QCOMPARE(receipt(three)->pipeline.orderedResults[0].state, ActionState::Failed); QCOMPARE(saves, 1);
    QCOMPARE(receipt(three)->workingMedia, pixels());
  }
};
QTEST_MAIN(CaptureFileOutputTest)
#include "CaptureFileOutputTest.moc"
