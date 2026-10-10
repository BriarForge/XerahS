// Native input tests exercise the actual canvas and shared session model.
#include "EditorCanvas.h"

#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QTest>
#include <QWheelEvent>

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
};

QTEST_MAIN(EditorCanvasTest)
#include "EditorCanvasTest.moc"
