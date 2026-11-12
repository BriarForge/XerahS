// Recording QActions must not start while an admitted capture is reserved and the
// controller is still idle. The shell fixture admits through the production dispatcher.
#include "RecordingActions.h"
#include "features/RecordingFeature.h"
#include <QScopedValueRollback>
#include "ShellFixture.h"
#include "features/CaptureFeature.h"
#include "shell/ActivationProtocol.h"
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QRectF>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace xerahs::app;

// Rejects a setup dialog if a host with encoders reaches one. Never accepts it:
// accepting a region recording asks the portal for a screenshot.
struct RejectModal {
  bool opened = false;
  QTimer timer;
  RejectModal() {
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &timer, [this] {
      if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
        opened = true;
        dialog->reject();
      }
    });
    timer.start(0);
  }
  ~RejectModal() { timer.stop(); }
};

class RecordingAdmissionTest final : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  QString busyText() const {
    return QStringLiteral("Activation: ") + activationFailureMessage(QStringLiteral("activation-busy"));
  }
  QString unavailableText() const {
    return QStringLiteral("Activation: ") + activationFailureMessage(QStringLiteral("activation-unavailable"));
  }
  struct Click {
    bool found = false;
    bool opened = false;
    QString status;
    bool reserved = false;
    bool captureBusy = false;
    bool recordingBusy = false;
  };
  Click click(Shell &shell, const QString &id) {
    Click result;
    shell.window().showResult(QStringLiteral("Sentinel"), QStringLiteral("idle"), true);
    auto *action = shell.commands.action(id);
    if (!action) return result;
    result.found = true;
    action->setEnabled(true);
    RejectModal guard;
    action->trigger();
    result.opened = guard.opened;
    auto *status = shell.window().findChild<QLabel *>(QStringLiteral("operationStatus"));
    result.status = status ? status->text() : QString();
    result.reserved = shell.dispatcher->reserved();
    result.captureBusy = shell.capture.busy();
    result.recordingBusy = shell.recording.busy();
    return result;
  }
private slots:
  void initTestCase() { QVERIFY(root.isValid()); }
  void admittedCaptureBlocksRecordingActionsWhileTheControllerIsIdle() {
    Shell shell(root.path());
    const auto request = xerahs::shell::makeActivationRequest(xerahs::shell::ActivationAction::CaptureRegion);
    QCOMPARE(shell.dispatcher->admit(request), xerahs::shell::ActivationReplyState::Accepted);
    QVERIFY(shell.dispatcher->reserved());
    QVERIFY(!shell.capture.busy());
    QVERIFY(!shell.recording.busy());
    auto *status = shell.window().findChild<QLabel *>(QStringLiteral("operationStatus"));
    QVERIFY(status);
    const QStringList ids = {QStringLiteral("start-recording"), QStringLiteral("start-region-recording"),
        QStringLiteral("start-gif-recording")};
    for (const auto &id : ids) {
      auto *action = shell.commands.action(id);
      QVERIFY2(action, qPrintable(id));
      const auto unavailable = shell.recording.unavailable(id == QLatin1String("start-gif-recording")
          ? xerahs::recording::Format::Gif : xerahs::recording::Format::Mp4);
      // This host may lack encoders, which disables the action. The slot is still the
      // production handler an enabled button runs, so force it on for the admission check.
      if (!unavailable.isEmpty()) action->setEnabled(true);
      bool opened = false;
      QTimer timer;
      timer.setSingleShot(true);
      QObject::connect(&timer, &QTimer::timeout, &timer, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) { opened = true; dialog->reject(); }
      });
      timer.start(0);
      action->trigger();
      timer.stop();
      QVERIFY2(!opened, qPrintable(id));
      QCOMPARE(status->text(), busyText());
      QVERIFY2(shell.dispatcher->reserved(), qPrintable(id));
      QVERIFY2(!shell.capture.busy(), qPrintable(id));
      QVERIFY2(!shell.recording.busy(), qPrintable(id));
    }
    const auto id = request.id;
    QVERIFY(shell.capture.acquireExternal([] {}));
    QCOMPARE(shell.dispatcher->dispatch(request), QStringLiteral("activation-busy"));
    QCOMPARE(request.id, id);
    QVERIFY(!shell.dispatcher->reserved());
    shell.capture.releaseExternal();
  }
  void idleRecordingClickDoesNotReportBusy() {
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("idle"))));
    Shell shell(root.filePath(QStringLiteral("idle")));
    QVERIFY(!shell.dispatcher->reserved());
    QVERIFY(shell.ctx.recordingActions);
    QVERIFY(!shell.ctx.recordingActions->blockedByForeignAdmission());
    const bool video = shell.recording.unavailable(xerahs::recording::Format::Mp4).isEmpty()
        || shell.recording.unavailable(xerahs::recording::Format::WebM).isEmpty();
    const auto sentinel = QStringLiteral("Sentinel: idle");
    for (const auto &id : {QStringLiteral("start-recording"), QStringLiteral("start-region-recording")}) {
      const auto result = click(shell, id);
      QVERIFY2(result.found, qPrintable(id));
      QVERIFY2(!result.reserved, qPrintable(id));
      QVERIFY2(!result.captureBusy, qPrintable(id));
      QVERIFY2(!result.recordingBusy, qPrintable(id));
      QVERIFY2(result.status != busyText(), qPrintable(id + ": " + result.status));
      if (result.opened) QCOMPARE(result.status, sentinel);
      else QCOMPARE(result.status, unavailableText());
    }
    const auto gif = click(shell, QStringLiteral("start-gif-recording"));
    QVERIFY(gif.found);
    QVERIFY(!gif.reserved);
    QVERIFY(!gif.captureBusy);
    QVERIFY(!gif.recordingBusy);
    QVERIFY(gif.status != busyText());
    // No MP4/WebM: GIF stays on the direct start path and must not look like a rejected
    // StartRecording admission. With a video encoder, cancel leaves the sentinel in place.
    if (gif.opened || !video) QCOMPARE(gif.status, sentinel);
    else QCOMPARE(gif.status, unavailableText());
  }
  void unownedStartRefusesBeforeRegionSelection() {
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("unowned"))));
    Shell shell(root.filePath(QStringLiteral("unowned")));
    const auto request = xerahs::shell::makeActivationRequest(xerahs::shell::ActivationAction::CaptureRegion);
    QCOMPARE(shell.dispatcher->admit(request), xerahs::shell::ActivationReplyState::Accepted);
    int calls = 0;
    shell.ctx.recordingActions->setInputs([&shell] { return shell.ctx.settings.current().values; },
        [&calls](std::function<void(QRectF)>) { ++calls; });
    auto *status = shell.window().findChild<QLabel *>(QStringLiteral("operationStatus"));
    QVERIFY(status);
    shell.ctx.recordingActions->start(xerahs::recording::Format::Mp4, true);
    QCOMPARE(calls, 0);
    QCOMPARE(status->text(), busyText());
    QVERIFY(shell.dispatcher->reserved());
    QVERIFY(shell.ctx.recordingActions->blockedByForeignAdmission());
    QVERIFY(!shell.capture.busy());
    QVERIFY(!shell.recording.busy());
    const auto id = request.id;
    QVERIFY(shell.capture.acquireExternal([] {}));
    QCOMPARE(shell.dispatcher->dispatch(request), QStringLiteral("activation-busy"));
    QCOMPARE(request.id, id);
    QVERIFY(!shell.dispatcher->reserved());
    shell.capture.releaseExternal();
  }
  void admittedStartDoesNotRefuseItsOwnReservation_data() {
    QTest::addColumn<bool>("region");
    QTest::newRow("screen") << false;
    QTest::newRow("region") << true;
  }
  void admittedStartDoesNotRefuseItsOwnReservation() {
    QFETCH(bool, region);
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("owned"))));
    Shell shell(root.filePath(QStringLiteral("owned")));
    using namespace xerahs::shell;
    ActivationDispatcher *active = nullptr;
    int calls = 0;
    ActivationOperations operations;
    // Admit the request independently of this host's encoder inventory. The
    // production handler still performs its real availability/setup checks.
    operations.canStartRecording = [] { return true; };
    const auto run = [&] {
      ++calls; QVERIFY(active->reserved());
      if (region) startRegionRecording(shell.ctx); else startScreenRecording(shell.ctx);
      QVERIFY(active->reserved());
      QVERIFY(shell.ctx.recordingActions->blockedByForeignAdmission());
    };
    operations.startRecording = run; operations.startRegionRecording = run;
    ActivationDispatcher dispatcher(std::move(operations)); active = &dispatcher;
    const QScopedValueRollback<ActivationDispatcher *> route(shell.ctx.activation, &dispatcher);
    const auto request = makeActivationRequest(region ? ActivationAction::StartRegionRecording : ActivationAction::StartRecording);
    shell.window().showResult(QStringLiteral("Sentinel"), QStringLiteral("owned"), true);
    auto *status = shell.window().findChild<QLabel *>(QStringLiteral("operationStatus")); QVERIFY(status);
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted);
    QVERIFY(dispatcher.reserved());
    {
      RejectModal guard;
      QCOMPARE(dispatcher.dispatch(request), QString());
    }
    QCOMPARE(calls, 1); QCOMPARE(status->text(), QStringLiteral("Sentinel: owned"));
    QVERIFY(!dispatcher.reserved());
    QVERIFY(!shell.ctx.recordingActions->blockedByForeignAdmission());
    QVERIFY(!shell.capture.busy()); QVERIFY(!shell.recording.busy());
    QCOMPARE(dispatcher.dispatch(request), QStringLiteral("activation-invalid"));
    QCOMPARE(calls, 1);
  }

};

int main(int argc, char **argv) {
  QTemporaryDir profile;
  if (!profile.isValid()) return 2;
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
    qputenv(key, profile.filePath(key).toUtf8());
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  RecordingAdmissionTest test;
  return QTest::qExec(&test, argc, argv);
}
#include "RecordingAdmissionTest.moc"
