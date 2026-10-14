// Native input tests exercise the actual canvas and shared session model.
#include "EditorCanvas.h"
#include "NativeAnnotationTools.h"

#include <QAction>
#include <QAccessible>
#include <QLabel>
#include <QKeyEvent>
#include <QToolBar>
#include <QToolButton>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
AnnotationDocument document() {
  AnnotationDocument d;
  d.canvasWidth = 800;
  d.canvasHeight = 600;
  return d;
}
QImage image() {
  QImage image(800, 600, QImage::Format_ARGB32);
  image.fill(Qt::white);
  return image;
}
void wheel(EditorCanvas &canvas, QPointF at, Qt::KeyboardModifiers modifiers, QPoint delta = {0, 120}) {
  QWheelEvent event(at, canvas.viewport()->mapToGlobal(at.toPoint()), {}, delta, Qt::NoButton, modifiers,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas.viewport(), &event);
}
}  // namespace

class EditorCanvasTest : public QObject {
  Q_OBJECT
private slots:
  void explicitToolsAreCheckedAccessibleAndSelectNeverCreates() {
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto tools = annotationToolActions(canvas, &canvas); QToolBar bar;
    bar.addAction(tools["select-tool"]); bar.addAction(tools["rectangle-tool"]); bar.addAction(tools["crop-tool"]); bar.show();
    std::unique_ptr<QLabel> instructions(annotationToolInstructions(canvas, nullptr));
    QVERIFY(tools["rectangle-tool"]->isChecked()); QVERIFY(instructions->text().contains("stays active"));
    auto *button = qobject_cast<QToolButton *>(bar.widgetForAction(tools["select-tool"])); QVERIFY(button);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Select"));
    QTest::mouseClick(button, Qt::LeftButton); QVERIFY(accessible->state().checked);
    QCOMPARE(canvas.tool(), EditorCanvas::Tool::Select); QVERIFY(!tools["rectangle-tool"]->isChecked());
    QVERIFY(instructions->text().contains("Blank canvas clears selection")); QVERIFY(canvas.accessibleDescription().contains("Select:"));
    const QPoint a = canvas.viewState().toView({160, 140}).toPoint(), b = canvas.viewState().toView({240, 220}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a); QVERIFY(!canvas.interactionActive());
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b); QVERIFY(session.document().annotations.empty()); QVERIFY(!session.dirty());
    tools["rectangle-tool"]->trigger();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, b); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, a);
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), size_t(1)); QVERIFY(tools["rectangle-tool"]->isChecked());
    const auto id = annotationId(session.document().annotations[0]);
    tools["select-tool"]->trigger();
    const QPoint inside = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, inside); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, inside + QPoint(10, 0));
    QCOMPARE(session.undoCount(), 2); QCOMPARE(session.selection(), QSet<QUuid>{id});
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({400, 400}).toPoint());
    QVERIFY(session.selection().isEmpty()); QCOMPARE(session.document().annotations.size(), size_t(1));
    tools["crop-tool"]->setChecked(true); QVERIFY(tools["crop-tool"]->isChecked()); QVERIFY(!tools["select-tool"]->isChecked());
    tools["crop-tool"]->setChecked(false); QCOMPARE(canvas.tool(), EditorCanvas::Tool::Rectangle); QVERIFY(tools["rectangle-tool"]->isChecked());
    QCOMPARE(source, image());
  }
  void switchingToolsCancelsGesturesAndLiveKeyboardEditsAreBlocked() {
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    const auto id = *session.createRectangle({150, 130}, {250, 230}); session.markSaved();
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto tools = annotationToolActions(canvas, &canvas); const auto before = serializeDocument(session.document());
    const QPoint at = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, at); QVERIFY(canvas.interactionActive());
    QTest::keyClick(&canvas, Qt::Key_Delete); QTest::keyClick(&canvas, Qt::Key_Right); QTest::keyClick(&canvas, Qt::Key_R);
    tools["select-tool"]->trigger(); QVERIFY(!canvas.interactionActive());
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at + QPoint(20, 0));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), QSet<QUuid>{id}); QVERIFY(!session.dirty());
    tools["rectangle-tool"]->trigger(); const QPoint blank = canvas.viewState().toView({400, 400}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, blank); QVERIFY(canvas.interactionActive());
    tools["select-tool"]->trigger(); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, blank + QPoint(20, 10));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty());
    canvas.setEnabled(false); tools["crop-tool"]->setChecked(true); QCOMPARE(canvas.tool(), EditorCanvas::Tool::Select);
    QVERIFY(tools["select-tool"]->isChecked()); QVERIFY(!tools["crop-tool"]->isChecked());
  }
  void failedGeometryEditReportsANativeFailureWithoutMutation() {
    const QImage source = image(); EditorSession session(document()); QVERIFY(session.createRectangle({150, 130}, {151, 230}));
    session.markSaved(); EditorCanvas canvas(session, source); canvas.resize(420, 340); canvas.show(); QTest::qWait(10);
    QSignalSpy rejected(&canvas, &EditorCanvas::editRejected); const auto before = serializeDocument(session.document());
    QTest::keyClick(&canvas, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(rejected.size(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty());
    QVERIFY(annotationEditMessage(rejected[0][0].toString()).contains("The document was kept"));
  }
  void failureObserverCanDestroyTheCanvasWithoutASecondNotification() {
    const QImage source = image(); EditorSession session(document()); QVERIFY(session.createRectangle({150, 130}, {151, 230})); session.markSaved();
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(420, 340); canvas->show();
    int rejected = 0, changed = 0;
    connect(canvas.get(), &EditorCanvas::changed, [&] { ++changed; });
    connect(canvas.get(), &EditorCanvas::editRejected, [&] { ++rejected; canvas.reset(); });
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier); QApplication::sendEvent(canvas.get(), &event);
    QVERIFY(!canvas); QCOMPARE(rejected, 1); QCOMPARE(changed, 0); QVERIFY(!session.dirty()); QCOMPARE(session.undoCount(), 1);
  }
  void drawAndMoveUseImagePixelsAfterZoom() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    canvas.zoomIn();
    canvas.zoomIn();
    const QPoint a = canvas.viewState().toView({160, 140}).toPoint();
    const QPoint b = canvas.viewState().toView({240, 220}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(session.undoCount(), 1);
    const auto &r = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QVERIFY(std::abs(r.left() - 160) <= 1 / canvas.viewState().zoom());
    QVERIFY(std::abs(r.top() - 140) <= 1 / canvas.viewState().zoom());
    const double original = r.left();
    const QPoint middle = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, middle);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, middle + QPoint(25, 0));
    QCOMPARE(session.undoCount(), 2);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), original + 25 / 1.5625);
    QVERIFY(session.undo());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), original);
  }

  void zoomPanAndScrollDoNotDirtySavedDocument() {
    const QImage source = image();
    EditorSession session(document());
    QVERIFY(session.createRectangle({150, 130}, {250, 230}));
    session.markSaved();
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF anchor(190, 160);
    const QPointF logical = canvas.viewState().toImage(anchor);
    wheel(canvas, anchor, Qt::ControlModifier);
    QCOMPARE(canvas.viewState().toImage(anchor), logical);
    wheel(canvas, anchor, Qt::NoModifier);
    canvas.horizontalScrollBar()->setValue(canvas.horizontalScrollBar()->value() + 20);
    QTest::mousePress(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(130, 112));
    canvas.zoomToFit();
    canvas.resetZoom();
    QCOMPARE(session.undoCount(), 1);
    QCOMPARE(session.redoCount(), 0);
    QVERIFY(!session.dirty());
    QCOMPARE(source, image());
  }

  void nativePinchKeepsPointerAnchor() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF anchor(190, 160);
    const QPointF logical = canvas.viewState().toImage(anchor);
    const double previous = canvas.viewState().zoom();
    QNativeGestureEvent event(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, anchor, anchor,
                              canvas.viewport()->mapToGlobal(anchor.toPoint()), .2, {});
    QApplication::sendEvent(canvas.viewport(), &event);
    QVERIFY(canvas.viewState().zoom() > previous);
    QVERIFY(QLineF(canvas.viewState().toImage(anchor), logical).length() < 1e-10);
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(!session.dirty());
  }

  void cancelledPanAndDrawRestoreCommittedState() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF offset = canvas.viewState().offset();
    QTest::keyPress(&canvas, Qt::Key_Space);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    QTest::mouseMove(canvas.viewport(), QPoint(190, 170));
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(190, 170));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.viewState().offset(), offset);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    QTest::mouseMove(canvas.viewport(), QPoint(190, 170));
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(190, 170));
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(session.document().annotations.empty());
    QVERIFY(!session.dirty());
  }

  void rotatedHitTestingUsesDisplayedShape() {
    const QImage source = image();
    EditorSession session(document());
    const auto id = session.createRectangle({150, 180}, {350, 200});
    QVERIFY(id);
    QVERIFY(session.rotateSelection(90));
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(520, 440);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    // Inside the visible vertical rectangle, outside its unrotated bounds.
    const QPoint at = canvas.viewState().toView({250, 120}).toPoint();
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(session.selection(), QSet<QUuid>{*id});
    QCOMPARE(session.undoCount(), 2);
  }

  void hitTargetsStaySixScreenPixelsAtLowZoom() {
    const QImage source = image();
    EditorSession session(document());
    const auto id = session.createRectangle({200, 200}, {300, 300});
    QVERIFY(id);
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.zoomToFit();
    for (int n = 0; n < 4; ++n) canvas.zoomOut();
    QVERIFY(canvas.viewState().zoom() < .25);
    // Five screen pixels from the rectangle remains a usable click target.
    const QPoint at = canvas.viewState().toView({200, 250}).toPoint() - QPoint(5, 0);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(session.selection(), QSet<QUuid>{*id});
    QCOMPARE(session.undoCount(), 1);
  }

  void rotatedPreviewUsesViewportTransform() {
    const QImage source = image();
    EditorSession session(document());
    QVERIFY(session.createRectangle({150, 180}, {350, 200}));
    session.setFillColor(0xFF00FF00);
    session.rotateSelection(90);
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(520, 440);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    canvas.zoomIn();
    const QPoint inside = canvas.viewState().toView({250, 150}).toPoint();
    const QPoint outside = canvas.viewState().toView({200, 190}).toPoint();
    const QImage preview = canvas.viewport()->grab().toImage();
    QCOMPARE(preview.pixelColor(inside), QColor(Qt::green));
    QCOMPARE(preview.pixelColor(outside), QColor(Qt::white));
  }

  void cropToolUsesImageCoordinatesAndCancellationIsLossless() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10);
    canvas.resetZoom(); canvas.zoomIn(); canvas.setCropTool(true);
    QSignalSpy requested(&canvas, &EditorCanvas::cropRequested);
    const QPoint a = canvas.viewState().toView({150, 130}).toPoint();
    const QPoint b = canvas.viewState().toView({250, 230}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(requested.size(), 0);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(requested.size(), 1);
    const QPointF start = requested[0][0].toPointF(), end = requested[0][1].toPointF();
    QVERIFY(QLineF(start, QPointF(150, 130)).length() < 1);
    QVERIFY(QLineF(end, QPointF(250, 230)).length() < 1);
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(session.document().annotations.empty());  // the window commits the crop, not a rectangle annotation
  }
};

QTEST_MAIN(EditorCanvasTest)
#include "EditorCanvasTest.moc"
