// Actual native transport and capture controller on a private, non-activating
// bus. Every image, output directory and clipboard belongs to this fixture.
#include "PortalScreenshot.h"
#include "CaptureController.h"
#include "CaptureRecoveryActions.h"
#include "ApplicationWindow.h"
#include "ActionSelectionDialog.h"
#include "CaptureFileOutput.h"
#include "CaptureSaveAsDialog.h"
#include "capture/RegionGeometry.h"
#include "image-editor/AnnotationDocument.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCheckBox>
#include <QColorSpace>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QPlainTextEdit>
#include <QJsonDocument>
#include <QLabel>
#include <QTreeWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <memory>
#include <atomic>
#include <stdexcept>

using namespace xerahs::app;
namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QString fixtureImage(const QTemporaryDir &dir, QSize size = {8, 6}) {
  const auto path = dir.filePath("snapshot.png"); QImage image(size, QImage::Format_ARGB32);
  image.fill(QColor(42, 71, 100, 155)); image.setColorSpace(QColorSpace::SRgb);
  if (!image.save(path)) qFatal("Fixture PNG save failed"); return path;
}
bool overlayExists() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->windowTitle() == u"XerahS region capture" && widget->isVisible()) return true;
  return false;
}
QWidget *overlayWidget() {
  for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture" && widget->isVisible()) return widget;
  return nullptr;
}
struct CompositionGate {
  std::atomic<bool> entered{false}, released{false}, done{false};
  std::atomic<int> calls{0}; int threshold = 50;
};
struct ReleaseGate {
  std::shared_ptr<CompositionGate> gate;
  ~ReleaseGate() { gate->released.store(true); }
};
SnapshotComposer gatedComposition(const std::shared_ptr<CompositionGate> &gate) {
  return [gate](const QImage &image, const xerahs::capture::PhysicalRect &rectangle, const xerahs::capture::Topology &topology,
                const xerahs::capture::CompositeControl &control) {
    ++gate->calls; auto observed = control;
    observed.progress = [gate, control](int percent) {
      if (control.progress) control.progress(percent);
      if (percent >= gate->threshold && !gate->entered.exchange(true)) {
        QElapsedTimer deadline; deadline.start();
        while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      }
    };
    auto result = composeSnapshot(image, rectangle, topology, observed); gate->done.store(true); return result;
  };
}
}
class FakeRequest final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
  FakeRequest(QString path, QStringList &closed, QObject *parent) : QObject(parent), path(std::move(path)), closed(closed) {}
  QString path; QStringList &closed;
public slots:
  void Close() { closed.append(path); }
};
class FakeScreenshotPortal final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
public:
  explicit FakeScreenshotPortal(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerObject(root, this, QDBusConnection::ExportAllSlots)) qFatal("Fixture portal registration failed");
  }
  ~FakeScreenshotPortal() override { bus.unregisterObject(root); }
  QDBusConnection bus;
  int calls = 0;
  bool early = false, legacy = false, delayed = false, invalidHandle = false;
  QString error, parentWindow, predicted, handle;
  QVariantMap options;
  QVariant uri;
  uint code = 0;
  QStringList closed;
  QDBusMessage pending;
  void respond(const QString &path = {}) {
    auto signal = QDBusMessage::createSignal(path.isEmpty() ? handle : path, requestInterface, QStringLiteral("Response"));
    QVariantMap results; if (uri.isValid()) results.insert(QStringLiteral("uri"), uri);
    signal << code << results; if (!bus.send(signal)) qFatal("Fixture response failed");
  }
  void finishHandle() { if (!bus.send(pending.createReply(QVariant::fromValue(QDBusObjectPath(handle))))) qFatal("Fixture handle reply failed"); }
public slots:
  QDBusObjectPath Screenshot(const QString &parent, const QVariantMap &requested) {
    ++calls; parentWindow = parent; options = requested;
    if (!error.isEmpty()) { sendErrorReply(error, QStringLiteral("Private fixture details must not escape")); return QDBusObjectPath("/"); }
    if (invalidHandle) return QDBusObjectPath("/");
    QString sender = message().service().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    predicted = root + "/request/" + sender + "/" + requested.value("handle_token").toString();
    handle = legacy ? root + QString("/request/legacy/result_%1").arg(calls) : predicted;
    for (const auto &path : predicted == handle ? QStringList{handle} : QStringList{predicted, handle}) {
      auto *request = new FakeRequest(path, closed, this);
      if (!bus.registerObject(path, request, QDBusConnection::ExportAllSlots)) qFatal("Fixture request registration failed");
    }
    if (early) respond();
    if (delayed) { setDelayedReply(true); pending = message(); }
    return QDBusObjectPath(handle);
  }
};
class PortalScreenshotTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()};
  using Outcome = PortalScreenshot::Outcome;
  static Outcome outcome(const QSignalSpy &spy) { return spy.at(0).at(0).value<Outcome>(); }
  static QImage resultImage(const QSignalSpy &spy) { return spy.at(0).at(1).value<QImage>(); }
private slots:
  void nativeSaveAsAndSharedRetryUseRetainedPixelsWithoutAnotherPortalCapture() {
    QTemporaryDir dir, preferences;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto borrowed = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int encoded = 0, outputs = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *out) { return ++encoded > 1 && image.save(out, "PNG"); };
    ports.fileOutput = [&](const QString &, const CaptureFile &, const xerahs::actions::CancellationToken &) { ++outputs; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    CaptureRecoveryActions recovery(controller, &home, {});
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *selection = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    selection->findChild<QCheckBox *>("capture-action-save")->setChecked(false); selection->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false);
    selection->findChild<QCheckBox *>("capture-action-save-as")->setChecked(true); selection->findChild<QCheckBox *>("capture-action-path-clipboard")->setChecked(true); selection->accept();
    QTRY_VERIFY(qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget())); auto *dialog = qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget());
    QCOMPARE(dialog->parentWidget(), &home); QVERIFY(!overlayExists()); dialog->selectFile(dir.filePath("first.png")); dialog->accept(); QTRY_VERIFY(!controller.busy());
    const auto original = controller.lastPipelineResult(); QVERIFY(original->files.empty()); QCOMPARE(original->workingMedia.size(), QSize(30, 25)); QCOMPARE(outputs, 0);
    QVERIFY(recovery.retryAction()->isEnabled()); recovery.retryAction()->trigger(); QTRY_VERIFY(qobject_cast<QMessageBox *>(QApplication::activeModalWidget()));
    auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(confirmation->text().contains("Save image as new PNG")); confirmation->button(QMessageBox::Retry)->click();
    QTRY_VERIFY(qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget())); dialog = qobject_cast<CaptureSaveAsDialog *>(QApplication::activeModalWidget());
    QCOMPARE(dialog->parentWidget(), &home); dialog->selectFile(dir.filePath("recovered.png")); dialog->accept(); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QCOMPARE(result->files.size(), size_t(1)); QCOMPARE(result->file->artifact, QString("save-as.file"));
    QCOMPARE(result->pipeline.orderedResults[1].inputArtifact, std::optional<QString>{"save-as.file"}); QCOMPARE(result->file->creatorTaskId, result->pipeline.taskId);
    QCOMPARE(QImage(result->file->path), original->workingMedia); QCOMPARE(portal.calls, 1); QCOMPARE(outputs, 1); QCOMPARE(bytes(path), borrowed);
    QVERIFY(journal->list().entries[0].parsed.record); QVERIFY(journal->list().entries[0].parsed.record->terminal);
  }
  void selectedFileOutputsAndSharedRetryKeepTheSameSavedArtifactWithoutNewCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); QStringList outputs; bool denied = true;
    CaptureActionPorts ports; ports.fileOutput = [&](const QString &action, const CaptureFile &file, const xerahs::actions::CancellationToken &) {
      outputs.append(action);
      if (action == "reveal") return denied ? xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "reveal-unavailable", {}} : xerahs::actions::ActionOutcome{};
      copyCaptureFile(action, file); return xerahs::actions::ActionOutcome{}; // fixture acknowledges its local clipboard payload
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    ApplicationWindow home({}); controller.setActionSelectionParent(&home); connect(&controller, &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    CaptureRecoveryActions recovery(controller, &home, {});
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    dialog->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false);
    for (const auto &id : {"file-clipboard", "path-clipboard", "reveal"}) {
      auto *box = dialog->findChild<QCheckBox *>(QString("capture-action-") + id); QVERIFY(box && box->isEnabled()); box->setChecked(true);
    }
    dialog->accept(); QTRY_VERIFY(!controller.busy()); const auto original = controller.lastPipelineResult(); QVERIFY(original->file);
    const auto saved = bytes(original->file->path); QCOMPARE(outputs, (QStringList{"file-clipboard", "path-clipboard", "reveal"}));
    QCOMPARE(QApplication::clipboard()->text(), original->file->path); QCOMPARE(original->pipeline.orderedResults.size(), size_t(5));
    QVERIFY(recovery.retryAction()->isEnabled()); recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(confirmation); QVERIFY(confirmation->text().contains("Show saved file in folder"));
    QTest::keyClick(confirmation, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget()); QCOMPARE(outputs.size(), 3);
    denied = false; recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); confirmation->button(QMessageBox::Retry)->click(); QTRY_VERIFY(!controller.busy());
    const auto result = controller.lastPipelineResult(); QCOMPARE(outputs, (QStringList{"file-clipboard", "path-clipboard", "reveal", "reveal"}));
    QCOMPARE(portal.calls, 1); QCOMPARE(result->file->path, original->file->path); QCOMPARE(bytes(original->file->path), saved);
    QCOMPARE(result->pipeline.orderedResults.size(), size_t(1)); QCOMPARE(result->pipeline.orderedResults[0].inputArtifact, std::optional<QString>{"save.file"});
    QCOMPARE(journal->list().entries.size(), size_t(2));
  }
  void perRunSelectionCancelsBeforeEffectsAndDoesNotChangeTheNextDefaultCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copies; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    QApplication::clipboard()->setText("selection fixture clipboard");
    controller.captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()));
    QVERIFY(controller.busy()); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QVERIFY(!QFileInfo::exists(dir.filePath("state")));
    QTest::keyClick(QApplication::activeModalWidget(), Qt::Key_Escape); QTRY_VERIFY(!controller.busy());
    const auto cancelled = controller.lastPipelineResult(); QVERIFY(cancelled); QCOMPARE(cancelled->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QVERIFY(!cancelled->file); QCOMPARE(cancelled->workingMedia.size(), QSize(30, 25)); QCOMPARE(copies, 0); QCOMPARE(portal.calls, 1);
    QCOMPARE(QApplication::clipboard()->text(), QString("selection fixture clipboard"));
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    QVERIFY(!QApplication::activeModalWidget()); const auto next = controller.lastPipelineResult(); QVERIFY(next->file); QCOMPARE(next->pipeline.orderedResults.size(), size_t(2));
    QCOMPARE(next->pipeline.orderedResults[0].action, QString("save")); QCOMPARE(copies, 1); QCOMPARE(portal.calls, 2); QCOMPARE(bytes(path), before);
  }
  void acceptedSelectionKeepsCaptureIdentityBusyAdmissionAndActualArtifactFacts() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copies; return xerahs::actions::ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, SnapshotComposer{}, ports, journal);
    ApplicationWindow home({}); home.showMaximized(); home.showMinimized();
    controller->setActionSelectionParent(&home);
    connect(controller.get(), &CaptureController::choosingActions, &home, &ApplicationWindow::activate);
    controller->captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()));
    auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget());
    QVERIFY(home.isVisible()); QVERIFY(!home.isMinimized()); QVERIFY(home.isMaximized());
    controller->captureRegion(); controller->captureRegionWithActions(); QCOMPARE(portal.calls, 1);
    auto *copy = dialog->findChild<QCheckBox *>("capture-action-image-clipboard"); QVERIFY(copy); copy->setFocus(); QTest::keyClick(copy, Qt::Key_Space);
    dialog->accept(); QTRY_VERIFY(!controller->busy()); QTRY_VERIFY(home.isActiveWindow()); const auto result = controller->lastPipelineResult(); QVERIFY(result->file); QCOMPARE(copies, 0);
    QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(result->pipeline.orderedResults.size(), size_t(2));
    QCOMPARE(result->pipeline.orderedResults[0].action, QString("after-capture-window")); QCOMPARE(result->pipeline.orderedResults[1].action, QString("save"));
    QCOMPARE(journal->list().entries[0].parsed.record->selected, (QStringList{"after-capture-window", "save"}));
    controller->captureRegionWithActions(); QTRY_VERIFY(overlayExists()); controller->pointerDown({5, 5}); controller->pointerUp({15, 15});
    QTRY_VERIFY(qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())); QPointer<QWidget> pending(QApplication::activeModalWidget()); controller.reset();
    QVERIFY(!pending); QTest::qWait(30); QCOMPARE(copies, 0); QCOMPARE(journal->list().entries.size(), size_t(1)); QVERIFY(QFileInfo::exists(result->file->path));
  }
  void nativeRetryUsesSharedKeyboardApprovalKeepsSuccessfulSaveAndStartsNoNewPortal() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto sourceBytes = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    int encoded = 0, copied = 0; CaptureActionPorts ports;
    ports.save.encode = [&](const QImage &image, QIODevice *output) { ++encoded; return image.save(output, "PNG"); };
    ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copied; return copied == 1 ? xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}} : xerahs::actions::ActionOutcome{};
    };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports, journal);
    QAction cancel(QStringLiteral("Cancel capture")); cancel.setEnabled(false); connect(&cancel, &QAction::triggered, &controller, &CaptureController::cancel);
    connect(&controller, &CaptureController::busyChanged, &cancel, [&](bool busy) { cancel.setEnabled(busy); });
    ApplicationWindow home({{"cancel-capture", &cancel}}); CaptureRecoveryActions recovery(controller, &home, {});
    home.addCaptureRecoveryActions(recovery.editAction(), recovery.discardAction(), recovery.reviewAction(), recovery.retryAction()); home.activate();
    auto *retry = home.findChild<QToolButton *>("retry-retained-capture"); QVERIFY(retry && !retry->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto original = controller.lastPipelineResult(); QVERIFY(original && original->file); const auto saved = bytes(original->file->path);
    QTRY_VERIFY(retry->isEnabled()); retry->setFocus(); QTest::keyClick(retry, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); QCOMPARE(box->objectName(), QString("retryRetainedCaptureDialog"));
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QTest::keyClick(box, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), original); QCOMPARE(copied, 1); QCOMPARE(encoded, 1);
    retry->setFocus(); QTest::keyClick(retry, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Retry)->click();
    QTRY_VERIFY(!controller.busy()); const auto result = controller.lastPipelineResult(); QVERIFY(result != original && result->pipeline.retry);
    QCOMPARE(result->pipeline.retry->originalTaskId, original->pipeline.taskId); QCOMPARE(result->pipeline.orderedResults.size(), size_t(1));
    QCOMPARE(copied, 2); QCOMPARE(encoded, 1); QCOMPARE(portal.calls, 1); QCOMPARE(bytes(original->file->path), saved); QCOMPARE(bytes(path), sourceBytes);
    QVERIFY(!retry->isEnabled()); QCOMPARE(journal->list().entries.size(), size_t(2));
    recovery.reviewAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *review = QApplication::activeModalWidget(); auto *lineage = review->findChild<QLabel *>("captureRetryLineage"); QVERIFY(lineage);
    QCOMPARE(lineage->textFormat(), Qt::PlainText); QVERIFY(lineage->text().contains("Linked retry 1")); QVERIFY(lineage->text().contains("save"));
    QTest::keyClick(review, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), result); QCOMPARE(copied, 2); QCOMPARE(bytes(original->file->path), saved);
  }
  void staleRetryConfirmationCannotRepeatActionsForANewerReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copies; return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}};
    };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    CaptureRecoveryActions recovery(controller, nullptr, {}); QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto first = controller.lastPipelineResult(); recovery.retryAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QMessageBox> approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(approval);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    const auto second = controller.lastPipelineResult(); QVERIFY(second != first); approval->button(QMessageBox::Retry)->click(); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), second); QCOMPARE(copies, 2); QCOMPARE(portal.calls, 2); QCOMPARE(notices.count(), 1); QVERIFY(!notices.first().last().toBool());
    QVERIFY(!controller.retryFailedActions(first)); QCOMPARE(copies, 2);
  }
  void cancellationAtRetryAvailabilityKeepsPixelsAndSuccessfulFileAndOwnerDestructionCannotStartWork() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString(); int copies = 0;
    CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) {
      ++copies; return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", {}};
    };
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, SnapshotComposer{}, ports);
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); QTRY_VERIFY(!controller->busy());
    const auto original = controller->lastPipelineResult(); const auto saved = bytes(original->file->path);
    const auto connection = connect(controller.get(), &CaptureController::busyChanged, controller.get(), [&](bool busy) { if (busy) controller->cancel(); });
    QVERIFY(controller->retryFailedActions(original)); QTRY_VERIFY(!controller->busy()); disconnect(connection);
    const auto cancelled = controller->lastPipelineResult(); QCOMPARE(cancelled->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QCOMPARE(cancelled->workingMedia, original->workingMedia); QCOMPARE(bytes(cancelled->file->path), saved); QCOMPARE(copies, 1); QCOMPARE(portal.calls, 1);
    // Restore a definitive failed receipt through a second isolated capture,
    // then destroy the controller synchronously at admission of its retry.
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35}); QTRY_VERIFY(!controller->busy());
    const auto expected = controller->lastPipelineResult(); connect(controller.get(), &CaptureController::busyChanged, [&](bool busy) { if (busy) controller.reset(); });
    controller->retryFailedActions(expected); QVERIFY(!controller); QTest::qWait(30); QCOMPARE(copies, 2); QCOMPARE(portal.calls, 2); QVERIFY(QFileInfo::exists(expected->file->path));
  }
  void init() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-client");
    QVERIFY(server.isConnected() && client.isConnected()); QVERIFY(server.registerService(service));
  }
  void cleanup() {
    server.unregisterService(service);
    QDBusConnection::disconnectFromBus("screenshot-test-client");
    QDBusConnection::disconnectFromBus("screenshot-test-server");
  }
  void authenticResponseAndEarlyLegacyHandle_data() {
    QTest::addColumn<bool>("early"); QTest::addColumn<bool>("legacy");
    QTest::addColumn<bool>("localhost");
    QTest::newRow("after-handle") << false << false << false;
    QTest::newRow("before-first-handle") << true << false << false;
    QTest::newRow("early-legacy-handle") << true << true << false;
    QTest::newRow("localhost-file-uri") << true << false << true;
  }
  void authenticResponseAndEarlyLegacyHandle() {
    QFETCH(bool, early); QFETCH(bool, legacy); QFETCH(bool, localhost); QTemporaryDir dir; const auto path = fixtureImage(dir);
    const auto before = bytes(path); FakeScreenshotPortal portal(server); portal.early = early; portal.legacy = legacy;
    auto uri = QUrl::fromLocalFile(path); if (localhost) uri.setHost("localhost");
    portal.uri = uri.toString(); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); source.request(); QTRY_COMPARE(portal.calls, 1);
    if (!early) portal.respond(); QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Captured); QCOMPARE(resultImage(done), QImage(path));
    QCOMPARE(resultImage(done).colorSpace(), QColorSpace(QColorSpace::SRgb)); QCOMPARE(done.at(0).at(2).toString(), QString{});
    QCOMPARE(portal.parentWindow, QString{}); QCOMPARE(portal.options.value("interactive"), QVariant(false));
    QCOMPARE(portal.options.value("modal"), QVariant(false)); QVERIFY(portal.options.value("handle_token").toString().startsWith("xerahs"));
    QCOMPARE(bytes(path), before); portal.respond(); source.cancel(); source.request(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QCOMPARE(portal.calls, 1); QVERIFY(portal.closed.isEmpty());
  }
  void userCancelIsDifferentFromPermissionDenial() {
    FakeScreenshotPortal portal(server); portal.code = 1; portal.early = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
    QVERIFY(resultImage(done).isNull()); QCOMPARE(done.at(0).at(2).toString(), QString{}); QCOMPARE(portal.calls, 1);
  }
  void methodErrorsAreStableAndValueFree_data() {
    QTest::addColumn<QString>("error"); QTest::addColumn<bool>("denied");
    QTest::newRow("permission") << QString("org.freedesktop.portal.Error.NotAllowed") << true;
    QTest::newRow("restricted") << QString("org.freedesktop.DBus.Error.AccessDenied") << true;
    QTest::newRow("source") << QString("org.freedesktop.DBus.Error.Failed") << false;
  }
  void methodErrorsAreStableAndValueFree() {
    QFETCH(QString, error); QFETCH(bool, denied); FakeScreenshotPortal portal(server); portal.error = error;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), denied ? Outcome::Denied : Outcome::Failed); QVERIFY(resultImage(done).isNull());
    QCOMPARE(done.at(0).at(2).toString(), denied ? QString("capture-permission-denied") : QString("capture-source-failed"));
    QCOMPARE(portal.calls, 1); source.request(); QCOMPARE(portal.calls, 1);
  }
  void invalidResultsFailWithoutMutatingFiles_data() {
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"missing", "wrong-type", "remote", "authority", "relative", "query", "fragment", "nul", "unknown-code", "corrupt", "oversized", "encoded-limit", "invalid-handle"})
      QTest::newRow(kind) << QString(kind);
  }
  void invalidResultsFailWithoutMutatingFiles() {
    QFETCH(QString, kind); QTemporaryDir dir; auto path = fixtureImage(dir);
    if (kind == u"corrupt") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("not an image") > 0); }
    if (kind == u"oversized") path = fixtureImage(dir, {int(xerahs::editor::kMaxDimension) + 1, 1});
    if (kind == u"encoded-limit") { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(1073741825)); }
    const auto before = kind == u"encoded-limit" ? QByteArray{} : bytes(path);
    FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString(); portal.early = true;
    if (kind == u"missing") portal.uri = {};
    if (kind == u"wrong-type") portal.uri = 17;
    if (kind == u"remote") portal.uri = "https://example.invalid/private.png";
    if (kind == u"authority") portal.uri = "file://elsewhere" + path;
    if (kind == u"relative") portal.uri = "file:relative.png";
    if (kind == u"query") portal.uri = portal.uri.toString() + "?private=value";
    if (kind == u"fragment") portal.uri = portal.uri.toString() + "#private";
    if (kind == u"nul") portal.uri = "file:///tmp/fixture%00.png";
    if (kind == u"unknown-code") portal.code = 999;
    if (kind == u"invalid-handle") portal.invalidHandle = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request();
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed); QVERIFY(resultImage(done).isNull());
    QCOMPARE(done.at(0).at(2).toString(), QString("capture-source-failed"));
    if (kind == u"encoded-limit") QCOMPARE(QFileInfo(path).size(), qint64(1073741825)); else QCOMPARE(bytes(path), before);
  }
  void cancellingBeforeStartPreventsAllRequests() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.cancel(); source.request(); source.cancel(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled); QCOMPARE(portal.calls, 0);
  }
  void cancelClosesThePendingAndLateLegacyHandleOnce() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    source.cancel(); source.cancel(); QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle(); QTRY_COMPARE(portal.closed.count(portal.handle), 1);
    portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void methodTimeoutFailsOnceAndLatePixelsCannotPublish() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); FakeScreenshotPortal portal(server); portal.delayed = true;
    portal.uri = QUrl::fromLocalFile(path).toString(); PortalScreenshot source(client, {service, 500});
    QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle(); portal.respond(); QTest::qWait(20);
    QCOMPARE(done.count(), 1); QVERIFY(QFileInfo::exists(path));
  }
  void userInteractionDoesNotInheritTheMethodDeadline() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client, {service, 100}); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QTest::qWait(150); QCOMPARE(done.count(), 0);
    source.cancel(); QTRY_COMPARE(portal.closed.count(portal.handle), 1); QCOMPARE(outcome(done), Outcome::Cancelled);
  }
  void ownerLossCannotBeReplacedByAnotherPortal() {
    FakeScreenshotPortal portal(server); PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QVERIFY(server.unregisterService(service));
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QVERIFY(server.registerService(service)); portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void ownerLossBeforeTheHandleStillClosesTheOldOwnersLegacyRequest() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true;
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished);
    source.request(); QTRY_COMPARE(portal.calls, 1); QVERIFY(server.unregisterService(service));
    QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle();
    QTRY_COMPARE(portal.closed.count(portal.handle), 1); portal.respond(); QTest::qWait(20); QCOMPARE(done.count(), 1);
  }
  void wrongSenderAndWrongPathCannotGrantPixels() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString();
    PortalScreenshot source(client); QSignalSpy done(&source, &PortalScreenshot::finished); source.request(); QTRY_COMPARE(portal.calls, 1);
    auto rogue = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "screenshot-test-rogue");
    auto fake = QDBusMessage::createSignal(portal.handle, requestInterface, "Response"); fake << uint(0) << QVariantMap{{"uri", portal.uri}};
    QVERIFY(rogue.send(fake)); portal.respond(root + "/request/another/handle"); QTest::qWait(30); QCOMPARE(done.count(), 0);
    portal.respond(); QTRY_COMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Captured);
    QDBusConnection::disconnectFromBus("screenshot-test-rogue");
  }
  void missingAndDisconnectedTransportFailWithoutPromptLoops() {
    QVERIFY(server.unregisterService(service));
    PortalScreenshot absent(client); QSignalSpy missing(&absent, &PortalScreenshot::finished); absent.request();
    QTRY_COMPARE(missing.count(), 1); QCOMPARE(outcome(missing), Outcome::Failed);
    PortalScreenshot disconnected(QDBusConnection("disconnected-fixture")); QSignalSpy done(&disconnected, &PortalScreenshot::finished);
    disconnected.request(); QCOMPARE(done.count(), 1); QCOMPARE(outcome(done), Outcome::Failed);
  }
  void destructionClosesAnOutstandingInteraction() {
    FakeScreenshotPortal portal(server); auto *source = new PortalScreenshot(client);
    source->request(); QTRY_COMPARE(portal.calls, 1); QTest::qWait(20); delete source; QTRY_COMPARE(portal.closed.count(portal.handle), 1);
  }
  void actualControllerCancellationReleasesBusyAndClosesLateLegacyRequest() {
    QTemporaryDir dir; const auto path = fixtureImage(dir); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true; portal.uri = QUrl::fromLocalFile(path).toString();
    QApplication::clipboard()->setText("fixture clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy busy(&controller, &CaptureController::busyChanged), notified(&controller, &CaptureController::notify);
    controller.captureRegion(); controller.captureRegion(); QTRY_COMPARE(portal.calls, 1); QVERIFY(controller.busy());
    controller.cancel(); QVERIFY(!controller.busy()); QCOMPARE(busy.count(), 2); QCOMPARE(notified.count(), 0);
    portal.finishHandle(); QTRY_COMPARE(portal.closed.count(portal.handle), 1); portal.respond(); QTest::qWait(30);
    QCOMPARE(busy.count(), 2); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture clipboard")); QCOMPARE(bytes(path), before);
    // The cancelled source cannot finish a new pending controller request.
    portal.delayed = false; controller.captureRegion(); QTRY_COMPARE(portal.calls, 2);
    portal.respond(root + "/request/legacy/result_1"); QTest::qWait(20); QVERIFY(controller.busy());
    controller.cancel(); QVERIFY(!controller.busy()); QCOMPARE(busy.count(), 4); QCOMPARE(notified.count(), 0);
  }
  void actualControllerDenialShowsNoOverlayAndNoPostCaptureActions() {
    FakeScreenshotPortal portal(server); portal.error = "org.freedesktop.portal.Error.NotAllowed"; QTemporaryDir dir;
    QApplication::clipboard()->setText("fixture denial clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(!controller.busy());
    QCOMPARE(notified.count(), 1); QCOMPARE(notified.at(0).at(1).toString(), QString("capture-permission-denied"));
    QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture denial clipboard")); QCOMPARE(portal.calls, 1);
  }
  void cancellingAtTheFirstAvailabilitySignalPreventsPortalAcquisition() {
    FakeScreenshotPortal portal(server); QTemporaryDir dir;
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    connect(&controller, &CaptureController::busyChanged, &controller, [&](bool busy) { if (busy) controller.cancel(); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion();
    QVERIFY(!controller.busy()); QTest::qWait(20); QCOMPARE(portal.calls, 0); QCOMPARE(notified.count(), 0);
    QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QVERIFY(!overlayExists());
  }
  void controllerDestructionDoesNotAbandonALateLegacyHandle() {
    FakeScreenshotPortal portal(server); portal.delayed = true; portal.legacy = true; QTemporaryDir dir;
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller->captureRegion(); QTRY_COMPARE(portal.calls, 1); controller.reset();
    QTRY_COMPARE(portal.closed.count(portal.predicted), 1); portal.finishHandle();
    QTRY_COMPARE(portal.closed.count(portal.handle), 1); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void actualFrozenOverlayEscapeLeavesFilesAndClipboardUntouched() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.uri = QUrl::fromLocalFile(path).toString(); portal.early = true;
    QApplication::clipboard()->setText("fixture overlay clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    QWidget *overlay = nullptr; for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture") overlay = widget;
    QVERIFY(overlay); QVERIFY(overlay->accessibleDescription().contains("Escape cancels"));
    QTest::keyClick(overlay, Qt::Key_Escape); QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists());
    QCOMPARE(notified.count(), 0); QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QCOMPARE(bytes(path), before);
    QCOMPARE(QApplication::clipboard()->text(), QString("fixture overlay clipboard"));
  }
  void cancellationDuringActualCompositionNeverPublishesPixelsOrRunsActions_data() {
    QTest::addColumn<int>("cancelAt");
    QTest::newRow("before-worker") << 0; QTest::newRow("partial-rows") << 50; QTest::newRow("last-row") << 100;
  }
  void cancellationDuringActualCompositionNeverPublishesPixelsOrRunsActions() {
    QFETCH(int, cancelAt); QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    const auto before = bytes(path); FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); gate->threshold = cancelAt; ReleaseGate release{gate};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    QSignalSpy progress(&controller, &CaptureController::compositionProgressChanged), notified(&controller, &CaptureController::notify);
    if (cancelAt == 0) connect(&controller, &CaptureController::compositionProgressChanged, &controller, [&](int percent) { if (percent == 0) controller.cancel(); });
    QApplication::clipboard()->setText("composition fixture clipboard"); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    auto *overlay = overlayWidget(); QVERIFY(overlay);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(40, 35));
    if (cancelAt != 0) {
      QTRY_VERIFY(gate->entered.load()); QTRY_VERIFY(controller.compositionProgress().value_or(-1) >= cancelAt);
      bool heartbeat = false; QTimer::singleShot(0, &controller, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
      QVERIFY(!gate->done.load()); QTest::keyClick(overlay, Qt::Key_Escape);
    }
    QTRY_VERIFY(!controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(notified.count(), 0); QVERIFY(!progress.isEmpty());
    QVERIFY(!QFileInfo::exists(controller.saveDirectory())); QCOMPARE(QApplication::clipboard()->text(), QString("composition fixture clipboard"));
    if (cancelAt == 0) QCOMPARE(gate->calls.load(), 0);
    else { QVERIFY(!gate->done.load()); gate->released.store(true); QTRY_VERIFY(gate->done.load()); }
    QTest::qWait(30); QCOMPARE(notified.count(), 0); QCOMPARE(bytes(path), before);
  }
  void aLateCancelledWorkerCannotFinishANewerPendingCapture() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    controller.cancel(); QTRY_VERIFY(!controller.busy());
    portal.early = false; controller.captureRegion(); QTRY_COMPARE(portal.calls, 2); QVERIFY(controller.busy());
    gate->released.store(true); QTRY_VERIFY(gate->done.load()); QTest::qWait(30);
    QVERIFY(controller.busy()); QVERIFY(!overlayExists()); QCOMPARE(notified.count(), 0); QVERIFY(!QFileInfo::exists(controller.saveDirectory()));
    controller.cancel(); QTRY_VERIFY(!controller.busy());
  }
  void destructionReleasesTheUiWithoutWaitingForACompositionWorker() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr,
        [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, gatedComposition(gate));
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QTRY_VERIFY(gate->entered.load()); QElapsedTimer elapsed; elapsed.start(); controller.reset();
    QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->done.load()); QVERIFY(!overlayExists());
    gate->released.store(true); QTRY_VERIFY(gate->done.load()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void progressReceiverCanDestroyTheControllerBeforeWorkerStart() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto controller = std::make_unique<CaptureController>(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    connect(controller.get(), &CaptureController::compositionProgressChanged, [&](int) { controller.reset(); });
    controller->captureRegion(); QTRY_VERIFY(overlayExists()); controller->pointerDown({10, 10}); controller->pointerUp({40, 35});
    QVERIFY(!controller); QVERIFY(!overlayExists()); QVERIFY(!QFileInfo::exists(dir.filePath("outputs")));
  }
  void successfulCompositionRunsOnePipelineAndSavesOnePhysicalImage() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); controller.pointerUp({70, 50});
    QTRY_VERIFY(!controller.busy()); QCOMPARE(notified.count(), 1);
    const auto files = QDir(controller.saveDirectory()).entryList({"*.png"}, QDir::Files); QCOMPARE(files.size(), 1);
    const QImage output(QDir(controller.saveDirectory()).filePath(files.first())); QCOMPARE(output.size(), QSize(30, 25));
    QTest::qWait(30); QCOMPARE(notified.count(), 1); QVERIFY(!overlayExists());
  }
  void actualOverlayEscapeDuringSavingKeepsTheCompletedFileAndCancelsClipboard() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    auto gate = std::make_shared<CompositionGate>(); ReleaseGate release{gate};
    CaptureActionPorts ports;
    ports.save.beforePublish = [gate](const QString &) {
      gate->entered.store(true); QElapsedTimer deadline; deadline.start();
      while (!gate->released.load() && deadline.elapsed() < 10000) QThread::msleep(1);
      if (!gate->released.load()) throw 1;
    };
    QApplication::clipboard()->setText("saving fixture clipboard");
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    QSignalSpy notified(&controller, &CaptureController::notify); controller.captureRegion(); QTRY_VERIFY(overlayExists());
    controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(gate->entered.load());
    QWidget *overlay = nullptr;
    for (auto *widget : QApplication::topLevelWidgets()) if (widget->windowTitle() == u"XerahS region capture") overlay = widget;
    QVERIFY(overlay); QTest::keyClick(overlay, Qt::Key_Escape);
    QVERIFY(controller.busy()); QVERIFY(controller.pipelineActivity().contains("Cancellation requested"));
    bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    gate->released.store(true); QTRY_VERIFY(!controller.busy()); QCOMPARE(notified.count(), 1); QVERIFY(!overlayExists());
    const auto result = controller.lastPipelineResult(); QVERIFY(result); QVERIFY(result->file);
    QCOMPARE(result->pipeline.state, xerahs::actions::PipelineState::Cancelled);
    QCOMPARE(result->pipeline.orderedResults[0].state, xerahs::actions::ActionState::Succeeded); QVERIFY(result->pipeline.orderedResults[0].cancellationArrivedDuring);
    QCOMPARE(result->pipeline.orderedResults[1].state, xerahs::actions::ActionState::Cancelled);
    QCOMPARE(QImage(result->file->path).size(), QSize(30, 25)); QCOMPARE(result->workingMedia.size(), QSize(30, 25));
    QCOMPARE(QApplication::clipboard()->text(), QString("saving fixture clipboard")); QCOMPARE(bytes(path), before);
    QVERIFY(notified.first().first().toString().contains("cancelled"));
  }
  void sharedReviewCommandShowsAnImmutableReadOnlyReportAndHasNoOutputSideEffects() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureActionPorts ports;
    ports.clipboard = [](const QImage &, const xerahs::actions::CancellationToken &) {
      return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, QString("clipboard-unavailable"), {}};
    };
    CaptureController controller(dir.filePath("private-<img>"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    auto recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, CapturedImageOpener{});
    ApplicationWindow home({}); home.addCaptureRecoveryActions(recovery->editAction(), recovery->discardAction(), recovery->reviewAction()); home.activate();
    auto *review = home.findChild<QToolButton *>("review-retained-capture"); QVERIFY(review && !review->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && retained->file);
    const auto saved = bytes(retained->file->path); QApplication::clipboard()->setText("review fixture private clipboard");
    QVERIFY(review->isEnabled()); QVERIFY(!recovery->editAction()->isEnabled());
    review->setFocus(); QTest::keyClick(review, Qt::Key_Space); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *dialog = QApplication::activeModalWidget(); QCOMPARE(dialog->objectName(), QString("retainedCaptureResultDialog"));
    auto *report = dialog->findChild<QPlainTextEdit *>("captureResultJson"); QVERIFY(report && report->isReadOnly()); QVERIFY(!report->isVisible());
    QCOMPARE(QJsonDocument::fromJson(report->toPlainText().toUtf8()).object(), retained->canonicalResult());
    auto *outcomes = dialog->findChild<QTreeWidget *>("captureActionResults"); QVERIFY(outcomes); QCOMPARE(outcomes->topLevelItemCount(), 2);
    QCOMPARE(outcomes->topLevelItem(0)->text(1), QString("succeeded")); QCOMPARE(outcomes->topLevelItem(1)->text(2), QString("clipboard-unavailable"));
    QPushButton *toggle = nullptr; for (auto *button : dialog->findChildren<QPushButton *>()) if (button->text() == "Show report details") toggle = button;
    QVERIFY(toggle); toggle->click(); QVERIFY(report->isVisible()); const auto original = report->toPlainText();
    report->setFocus(); QTest::keyClicks(report, "cannot edit"); QCOMPARE(report->toPlainText(), original);
    QCOMPARE(controller.lastPipelineResult(), retained); QCOMPARE(QApplication::clipboard()->text(), QString("review fixture private clipboard"));
    QTest::keyClick(dialog, Qt::Key_Escape); QTRY_VERIFY(!QApplication::activeModalWidget()); QVERIFY(review->isEnabled());
    recovery->reviewAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QWidget> owned = QApplication::activeModalWidget(); recovery.reset(); QTRY_VERIFY(!owned);
    QCOMPARE(controller.lastPipelineResult(), retained); QCOMPARE(bytes(retained->file->path), saved); QCOMPARE(bytes(path), before);
    QCOMPARE(QApplication::clipboard()->text(), QString("review fixture private clipboard"));
  }
  void recoveryActionsUseActualSharedKeyboardCommandsAndKeepFailedMediaUntilExplicitDiscard() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size()); const auto before = bytes(path);
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureActionPorts ports; ports.save.encode = [](const QImage &, QIODevice *) { return false; };
    ports.clipboard = [](const QImage &, const xerahs::actions::CancellationToken &) {
      return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, QString("clipboard-unavailable"), {}};
    };
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); }, {}, ports);
    QImage opened; int calls = 0;
    CaptureRecoveryActions recovery(controller, nullptr, [&](const QImage &image, QWidget *) { ++calls; opened = image.copy(); return true; });
    ApplicationWindow home({}); home.addCaptureRecoveryActions(recovery.editAction(), recovery.discardAction()); home.activate();
    auto *edit = home.findChild<QToolButton *>("edit-retained-capture"); auto *discard = home.findChild<QToolButton *>("discard-retained-capture");
    QVERIFY(edit && discard && !edit->isEnabled() && !discard->isEnabled());
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && !retained->file); QCOMPARE(retained->workingMedia.size(), QSize(30, 25));
    QVERIFY(edit->isEnabled() && discard->isEnabled()); edit->setFocus(); QTest::keyClick(edit, Qt::Key_Space);
    QCOMPARE(calls, 1); QCOMPARE(opened, retained->workingMedia); QCOMPARE(controller.lastPipelineResult(), retained);
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); QCOMPARE(box->objectName(), QString("discardRetainedCaptureDialog"));
    QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); QVERIFY(!edit->isEnabled() && !discard->isEnabled());
    box->button(QMessageBox::Cancel)->click(); QTRY_VERIFY(!QApplication::activeModalWidget()); QCOMPARE(controller.lastPipelineResult(), retained);
    QVERIFY(edit->isEnabled()); recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(!controller.lastPipelineResult()); QVERIFY(!edit->isEnabled() && !discard->isEnabled());
    QCOMPARE(opened, retained->workingMedia); QCOMPARE(bytes(path), before);
  }
  void discardApprovalCannotClearANewerResultAndNeverRemovesSavedFiles() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    CaptureRecoveryActions recovery(controller, nullptr, {}); QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto first = controller.lastPipelineResult(); QVERIFY(first && first->file); const auto firstFile = bytes(first->file->path);
    QVERIFY(!recovery.editAction()->isEnabled()); QVERIFY(recovery.discardAction()->isEnabled());
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QMessageBox> approval = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(approval);
    // A new production capture completes while the old target's confirmation
    // remains open. Its owned receipt cannot be consumed by that approval.
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({5, 5}); controller.pointerUp({15, 15}); QTRY_VERIFY(!controller.busy());
    const auto second = controller.lastPipelineResult(); QVERIFY(second && second != first && second->file);
    approval->button(QMessageBox::Discard)->click(); QTRY_VERIFY(!QApplication::activeModalWidget());
    QCOMPARE(controller.lastPipelineResult(), second); QCOMPARE(notices.count(), 1); QVERIFY(!notices.first().last().toBool());
    QCOMPARE(bytes(first->file->path), firstFile);
    recovery.discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Discard)->click();
    QTRY_VERIFY(!controller.lastPipelineResult()); QVERIFY(QFileInfo::exists(second->file->path)); QCOMPARE(bytes(first->file->path), firstFile);
  }
  void recoveryCallbackCancellationExceptionsAndReentrancyDoNotConsumeTheReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); int calls = 0; CaptureRecoveryActions *running = nullptr;
    CaptureRecoveryActions recovery(controller, nullptr, [&](const QImage &, QWidget *) {
      ++calls; running->editAction()->trigger(); running->discardAction()->trigger(); if (calls == 2) throw std::runtime_error("private pixels"); return false;
    }); running = &recovery; QSignalSpy notices(&recovery, &CaptureRecoveryActions::notify);
    recovery.editAction()->trigger(); QCOMPARE(calls, 1); QCOMPARE(notices.count(), 0); QCOMPARE(controller.lastPipelineResult(), retained);
    recovery.editAction()->trigger(); QCOMPARE(calls, 2); QCOMPARE(notices.count(), 1); QVERIFY(notices.first().first().toString().contains("action-failed"));
    QVERIFY(!notices.first().first().toString().contains("private pixels")); QCOMPARE(controller.lastPipelineResult(), retained); QVERIFY(!QApplication::activeModalWidget());
  }
  void recoveryOwnerDestructionCancelsPendingApprovalAndKeepsTheReceipt() {
    QTemporaryDir dir; const auto path = fixtureImage(dir, QApplication::primaryScreen()->geometry().size());
    FakeScreenshotPortal portal(server); portal.early = true; portal.uri = QUrl::fromLocalFile(path).toString();
    CaptureController controller(dir.filePath("outputs"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    controller.captureRegion(); QTRY_VERIFY(overlayExists()); controller.pointerDown({10, 10}); controller.pointerUp({40, 35}); QTRY_VERIFY(!controller.busy());
    const auto retained = controller.lastPipelineResult(); QVERIFY(retained && retained->file);
    auto recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, CapturedImageOpener{});
    recovery->discardAction()->trigger(); QTRY_VERIFY(QApplication::activeModalWidget());
    QPointer<QWidget> approval = QApplication::activeModalWidget(); recovery.reset(); QTRY_VERIFY(!approval);
    QCOMPARE(controller.lastPipelineResult(), retained); QVERIFY(QFileInfo::exists(retained->file->path));
    recovery = std::make_unique<CaptureRecoveryActions>(controller, nullptr, [&](const QImage &, QWidget *) { recovery.reset(); return false; });
    recovery->editAction()->trigger(); QVERIFY(!recovery); QCOMPARE(controller.lastPipelineResult(), retained);
  }
};
QTEST_MAIN(PortalScreenshotTest)
#include "PortalScreenshotTest.moc"
