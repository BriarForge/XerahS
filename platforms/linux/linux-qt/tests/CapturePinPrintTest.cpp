// POST-CAPTURE-ACTIONS-001: pin and print actions through the real pipeline.
#include "CapturePipeline.h"
#include "ActionSelectionDialog.h"
#include "PinWindow.h"
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QWheelEvent>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(QColor(23, 45, 67)); return image; }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
QList<PinWindow *> pins() { QList<PinWindow *> found; for (auto *w : QApplication::topLevelWidgets()) if (auto *p = qobject_cast<PinWindow *>(w)) found.append(p); return found; }
QString stateOf(const CapturePipelineReceipt &r, const QString &id) {
  for (const auto &a : r->pipeline.orderedResults) if (a.action == id) return a.state == ActionState::Succeeded ? "succeeded" : a.state == ActionState::Failed ? "failed" : a.state == ActionState::Cancelled ? "cancelled" : "skipped";
  return "absent";
}
void wheel(PinWindow &w, int delta, Qt::KeyboardModifiers mods = {}) {
  QWheelEvent e(QPointF(4, 4), w.mapToGlobal(QPointF(4, 4)), {}, QPoint(0, delta), Qt::NoButton, mods, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&w, &e);
}
}
class CapturePinPrintTest final : public QObject {
  Q_OBJECT
private slots:
  void cleanup() { for (auto *p : pins()) delete p; }
  void bothActionsAreSelectableAndLabelled() {
    const auto actions = CaptureActions::selectableActions();
    QVERIFY(actions.contains("pin")); QVERIFY(actions.contains("print"));
    QCOMPARE(CaptureActions::actionLabel("pin"), QString("Pin image on screen")); QCOMPARE(CaptureActions::actionLabel("print"), QString("Print image"));
  }
  void pinShowsACopyOfTheFinalMediaAndLaterActionsRun() {
    QTemporaryDir dir; int copied = 0; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"pin", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto r = receipt(done); QCOMPARE(stateOf(r, "pin"), QString("succeeded")); QCOMPARE(stateOf(r, "image-clipboard"), QString("succeeded")); QCOMPARE(copied, 1);
    QCOMPARE(pins().size(), 1); auto *pin = pins().first(); QVERIFY(pin->isVisible());
    QCOMPARE(pin->image(), r->workingMedia); QCOMPARE(pin->zoom(), 1.0); QCOMPARE(pin->size(), QSize(12, 8));
  }
  void closingAPinFreesItAndSeveralMayBeOpen() {
    QPointer<PinWindow> a = new PinWindow(pixels()), b = new PinWindow(pixels()); a->show(); b->show();
    QCOMPARE(pins().size(), 2); a->close(); QTRY_VERIFY(a.isNull()); QVERIFY(!b.isNull()); QCOMPARE(pins().size(), 1);
  }
  void pinRunsOnTheGuiThreadAndHonoursInjectedFailure() {
    QTemporaryDir dir; bool gui = false; CaptureActionPorts ports;
    ports.pin = [&](const QImage &, const CancellationToken &) { gui = QThread::currentThread() == qApp->thread(); return ActionOutcome{ActionState::Failed, "pin-unavailable", {}}; };
    ports.print = [](const LoadedSource &, const CancellationToken &) { return ActionOutcome{}; };
    CapturePipeline second(pixels(), dir.path(), "capture", {"pin", "print"}, nullptr, ports);
    QSignalSpy done(&second, &CapturePipeline::finished); second.start(); QTRY_COMPARE(done.count(), 1);
    QVERIFY(gui); QCOMPARE(stateOf(receipt(done), "pin"), QString("failed")); QCOMPARE(stateOf(receipt(done), "print"), QString("succeeded"));
  }
  void printOutcomesMapAndLaterActionsStillRun_data() {
    QTest::addColumn<int>("state"); QTest::addColumn<QString>("code"); QTest::addColumn<QString>("expected");
    QTest::newRow("success") << int(ActionState::Succeeded) << QString() << "succeeded";
    QTest::newRow("cancel") << int(ActionState::Cancelled) << QString() << "cancelled";
    QTest::newRow("failure") << int(ActionState::Failed) << QString("print-failed") << "failed";
  }
  void printOutcomesMapAndLaterActionsStillRun() {
    QFETCH(int, state); QFETCH(QString, code); QFETCH(QString, expected);
    QTemporaryDir dir; int copied = 0; bool gui = false, sized = false; CaptureActionPorts ports;
    ports.print = [&](const LoadedSource &source, const CancellationToken &) {
      gui = QThread::currentThread() == qApp->thread(); sized = source.image.size() == QSize(12, 8) && source.document;
      ActionOutcome o; o.state = ActionState(state); if (!code.isEmpty()) o.diagnostic = code; return o; };
    ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    CapturePipeline pipeline(pixels(), dir.path(), "capture", {"print", "image-clipboard"}, nullptr, ports);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto r = receipt(done); QVERIFY(gui && sized); QCOMPARE(stateOf(r, "print"), expected); QCOMPARE(copied, 1); QCOMPARE(stateOf(r, "image-clipboard"), QString("succeeded"));
    if (!code.isEmpty()) for (const auto &a : r->pipeline.orderedResults) if (a.action == "print") { QVERIFY(a.diagnostic.has_value()); QCOMPARE(*a.diagnostic, code); }
    QCOMPARE(r->pipeline.state, PipelineState::Completed);
  }
  void pinWindowZoomAndOpacityAreBounded() {
    PinWindow w(pixels()); w.show();
    for (int i = 0; i < 80; ++i) wheel(w, 120); QCOMPARE(w.zoom(), PinWindow::kMaxZoom); QCOMPARE(w.size(), QSize(48, 32));
    for (int i = 0; i < 200; ++i) wheel(w, -120); QCOMPARE(w.zoom(), PinWindow::kMinZoom);
    for (int i = 0; i < 40; ++i) wheel(w, -120, Qt::ControlModifier); QCOMPARE(w.pinOpacity(), PinWindow::kMinOpacity);
    for (int i = 0; i < 40; ++i) wheel(w, 120, Qt::ControlModifier); QCOMPARE(w.pinOpacity(), PinWindow::kMaxOpacity);
  }
  void largeImagesShrinkToFitButSmallOnesNeverGrow() {
    QImage big(8000, 6000, QImage::Format_RGB32); big.fill(Qt::red);
    PinWindow w(big); QVERIFY(w.zoom() < 1.0); QVERIFY(w.zoom() >= PinWindow::kMinZoom); PinWindow small(pixels()); QCOMPARE(small.zoom(), 1.0);
  }
  void escapeClosesAndCopyPutsTheImageOnTheClipboard() {
    QPointer<PinWindow> w = new PinWindow(pixels()); w->show(); w->copyToClipboard();
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(12, 8));
    QTest::keyClick(w.data(), Qt::Key_Escape); QTRY_VERIFY(w.isNull());
  }
  void saveWritesAPngOffTheGuiThread() {
    QTemporaryDir dir; PinWindow w(pixels()); QSignalSpy saved(&w, &PinWindow::saveFinished); const auto path = dir.filePath("pin.png");
    w.saveTo(path); QTRY_COMPARE(saved.count(), 1); QVERIFY(saved.first().first().toBool()); QCOMPARE(QImage(path).size(), QSize(12, 8));
  }
};
int main(int argc, char **argv) { QApplication app(argc, argv); CapturePinPrintTest test; return QTest::qExec(&test, argc, argv); }
#include "CapturePinPrintTest.moc"
