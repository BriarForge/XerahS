#include "EditorPin.h"
#include "EditorWindow.h"
#include "EditorSource.h"
#include "EditorCanvas.h"
#include "NativeEditorPreferences.h"
#include "PinWindow.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QFile>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <stdexcept>
#include <thread>

using namespace xerahs::app;
using namespace xerahs::editor;
using namespace xerahs::actions;
namespace {
LoadedSource fixture() {
  QImage image(40, 20, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); image.fill(qRgba(255, 0, 0, 128));
  auto source = prepareCapturedSource(image); RectangleAnnotation rectangle;
  rectangle.id = QUuid::createUuid(); rectangle.start = {20, 0}; rectangle.end = {40, 20};
  rectangle.style.fillColor = 0xFF0000FF; rectangle.style.strokeColor = 0xFF0000FF; rectangle.style.strokeWidth = 1;
  source.document->annotations = {rectangle}; return source;
}
QList<PinWindow *> pins() {
  QList<PinWindow *> result;
  for (auto *widget : QApplication::topLevelWidgets()) if (auto *pin = qobject_cast<PinWindow *>(widget)) result.append(pin);
  return result;
}
struct Gate { std::atomic<bool> entered{false}, released{false}, finished{false}; };
void waitGate(const std::shared_ptr<Gate> &gate) {
  gate->entered.store(true);
  while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  gate->finished.store(true);
}
}
class EditorPinTest final : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase() {
    QVERIFY(settings.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  }
  void init() { QVERIFY(initializeNativeEditorPreferences(settings.filePath("editor.json")).isEmpty()); }
  void cleanup() { for (auto *pin : pins()) delete pin; QThreadPool::globalInstance()->waitForDone(); }
  void nativeActionPinsRenderedAlphaWithoutChangingDocumentOrHistory() {
    const auto source = fixture(); std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(*source.document)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({2, 2}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    window->findChild<QAction *>("undo")->trigger();
    const auto title = window->windowTitle(); const auto preferences = nativeEditorPreferences().values;
    const auto document = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState();
    const auto view = canvas->viewState(); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    QSignalSpy finished(window.get(), &EditorWindow::pinFinished), saved(window.get(), &EditorWindow::saveFinished);
    auto *action = window->findChild<QAction *>("pinImage"); QVERIFY(action && action->isEnabled());
    QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_P)); QVERIFY(!action->autoRepeat()); QVERIFY(action->statusTip().contains("visible"));
    action->trigger(); QCOMPARE(finished.count(), 1); QCOMPARE(finished.first()[0].toString(), QString("succeeded"));
    QCOMPARE(pins().size(), 1); QPointer<PinWindow> pin = pins().first();
    QCOMPARE(pin->image().pixel(5, 5), qRgba(255, 0, 0, 128)); QCOMPARE(pin->image().pixel(30, 10), qRgb(0, 0, 255));
    const QImage pinned = pin->image(); pin->move(17, 31); pin->setZoom(2, {0, 0});
    QCOMPARE(pin->image(), pinned); QCOMPARE(window->windowTitle(), title); QCOMPARE(saved.count(), 0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), document); QCOMPARE(canvas->annotationState(), state);
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(canvas->viewState().zoom(), view.zoom()); QCOMPARE(canvas->viewState().offset(), view.offset());
    QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    window->findChild<QAction *>("redo")->trigger(); QCOMPARE(pin->image(), pinned);
    window.reset(); QVERIFY(pin && pin->isVisible()); pin->close(); QTRY_VERIFY(!pin);
  }
  void fixedShortcutPinsAndClosingPinKeepsEditorOpen() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); window->activateWindow();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow());
    QSignalSpy finished(window.get(), &EditorWindow::pinFinished); const auto title = window->windowTitle();
    QTest::keyClick(canvas, Qt::Key_P, Qt::ControlModifier); QCOMPARE(finished.count(), 1); QCOMPARE(pins().size(), 1);
    QPointer<PinWindow> pin = pins().first(); QTest::keyClick(pin, Qt::Key_Escape); QTRY_VERIFY(!pin);
    QVERIFY(window->isVisible()); QCOMPARE(window->windowTitle(), title);
  }
  void cancellationReturnsBeforeBlockedRenderWithoutPinOrDocumentMutation() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(*fixture().document)); QVERIFY(window); window->show(); const auto title = window->windowTitle();
    auto gate = std::make_shared<Gate>(); EditorPinPorts ports; int publications = 0;
    ports.beforeRender = [gate] { waitGate(gate); };
    ports.publish = [&](const EditorPinSnapshot &) { ++publications; return ActionOutcome{}; }; window->setPinPorts(ports);
    bool cancelled = false; QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
      auto *dialog = window->findChild<QProgressDialog *>("pinImageProgress"); if (!dialog || !gate->entered.load() || cancelled) return;
      cancelled = true; QVERIFY(!window->findChild<QAction *>("pinImage")->isEnabled()); QVERIFY(!window->findChild<QAction *>("copyImage")->isEnabled());
      QVERIFY(!window->close()); dialog->findChild<QPushButton *>()->click();
    });
    QSignalSpy finished(window.get(), &EditorWindow::pinFinished); timer.start(2); window->findChild<QAction *>("pinImage")->trigger(); timer.stop();
    gate->released.store(true); QVERIFY(cancelled); QCOMPARE(publications, 0); QCOMPARE(finished.count(), 1); QCOMPARE(finished.first()[0].toString(), QString("cancelled"));
    QCOMPARE(window->windowTitle(), title); QVERIFY(window->findChild<QAction *>("pinImage")->isEnabled()); QVERIFY(pins().isEmpty());
  }
  void destroyingEditorAbandonsBlockedRenderWithoutPublication() {
    QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    auto gate = std::make_shared<Gate>(); EditorPinPorts ports; int publications = 0;
    ports.beforeRender = [gate] { waitGate(gate); }; ports.publish = [&](const EditorPinSnapshot &) { ++publications; return ActionOutcome{}; }; window->setPinPorts(ports);
    QTimer timer; connect(&timer, &QTimer::timeout, this, [&] { if (gate->entered.load() && window) delete window.data(); }); timer.start(2);
    window->findChild<QAction *>("pinImage")->trigger(); timer.stop(); gate->released.store(true);
    QVERIFY(!window); QCOMPARE(publications, 0); QVERIFY(pins().isEmpty());
  }
  void missingAndFailingServicesKeepEditorAndExposeRetry() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); const auto title = window->windowTitle();
    EditorPinPorts ports; ports.publish = {}; window->setPinPorts(ports); auto *action = window->findChild<QAction *>("pinImage");
    QVERIFY(!action->isEnabled()); QVERIFY(action->toolTip().contains("no pin service"));
    ports.publish = [](const EditorPinSnapshot &) { return ActionOutcome{ActionState::Failed, "PRIVATE provider details", {}}; }; window->setPinPorts(ports);
    QSignalSpy finished(window.get(), &EditorWindow::pinFinished); action->trigger(); QCOMPARE(finished.count(), 1); QCOMPARE(finished.first()[0].toString(), QString("failed")); QCOMPARE(finished.first()[1].toString(), QString("pin-failed"));
    QVERIFY(action->isEnabled()); QVERIFY(window->statusBar()->currentMessage().contains("try again")); QVERIFY(!window->statusBar()->currentMessage().contains("PRIVATE")); QCOMPARE(window->windowTitle(), title);
    window->setPinPorts({}); action->trigger(); QCOMPARE(pins().size(), 1); QCOMPARE(finished.last()[0].toString(), QString("succeeded"));
  }
  void thrownPublicationIsUnknownAndReentrantActionDoesNotPublishAgain() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); int publications = 0;
    EditorPinPorts ports; ports.publish = [&](const EditorPinSnapshot &) -> ActionOutcome { ++publications; window->findChild<QAction *>("pinImage")->trigger(); throw std::runtime_error("PRIVATE"); }; window->setPinPorts(ports);
    QSignalSpy finished(window.get(), &EditorWindow::pinFinished); window->findChild<QAction *>("pinImage")->trigger();
    QCOMPARE(publications, 1); QCOMPARE(finished.first()[0].toString(), QString("unknown")); QCOMPARE(finished.first()[1].toString(), QString("pin-delivery-unknown")); QVERIFY(window->statusBar()->currentMessage().contains("Check the screen"));
  }
  void completionMayDestroyEditorAfterNativePinTakesOwnership() {
    QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    connect(window, &EditorWindow::pinFinished, this, [&] { delete window.data(); }); window->findChild<QAction *>("pinImage")->trigger();
    QVERIFY(!window); QCOMPARE(pins().size(), 1); QCOMPARE(pins().first()->image(), fixture().image);
  }
  void visibleUnsupportedAnnotationRefusesLossyPin() {
    auto source = fixture(); const auto id = QUuid::createUuid();
    source.document->annotations.emplace_back(UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-shape"}, {"visible", true}}});
    QWidget parent; EditorPinPorts ports; int publications = 0;
    ports.publish = [&](const EditorPinSnapshot &) { ++publications; return ActionOutcome{}; };
    const auto result = runEditorPin(source.image, *source.document, &parent, ports);
    QCOMPARE(result.action.state, ActionState::Failed); QCOMPARE(result.action.diagnostic, std::optional<QString>{"pin-annotation-unsupported"});
    QVERIFY(!result.publicationAttempted); QCOMPARE(publications, 0); QVERIFY(pins().isEmpty());
  }
};
QTEST_MAIN(EditorPinTest)
#include "EditorPinTest.moc"
