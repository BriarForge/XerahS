#include "image-editor/EditorViewport.h"
#include "image-editor/EditorSession.h"

#include <QTest>

#include <limits>

using namespace xerahs::editor;

class EditorViewportTest : public QObject {
  Q_OBJECT
private slots:
  void zoomKeepsImagePointUnderPointer() {
    EditorViewport view({800, 600});
    view.setViewSize({400, 300});
    const QPointF anchor(113, 79);
    const QPointF logical = view.toImage(anchor);
    QVERIFY(view.zoomAt(2.5, anchor));
    QCOMPARE(view.toImage(anchor), logical);
    QCOMPARE(view.toView(logical), anchor);
    QVERIFY(view.zoomAt(.5, anchor));
    QCOMPARE(view.toImage(anchor), logical);
    QVERIFY(view.pan({30, -12}));
    QCOMPARE(view.toView(logical), anchor + QPointF(30, -12));
  }

  void fitAndResetDoNotResampleOrAlterHistory() {
    AnnotationDocument document;
    document.canvasWidth = 800;
    document.canvasHeight = 600;
    EditorSession session(document);
    const auto id = session.createRectangle({10, 20}, {50, 60});
    QVERIFY(id);
    session.markSaved();
    EditorViewport view({800, 600});
    view.setViewSize({400, 400});
    QVERIFY(view.fit());
    QCOMPARE(view.zoom(), .5);
    QCOMPARE(view.offset(), QPointF(0, 50));
    QVERIFY(view.reset());
    QCOMPARE(view.zoom(), 1);
    view.zoomBy(1.25, {200, 200});
    view.pan({30, -12});
    QCOMPARE(session.undoCount(), 1);
    QCOMPARE(session.redoCount(), 0);
    QVERIFY(!session.dirty());
    QCOMPARE(session.selection(), QSet<QUuid>{*id});
    const auto &r = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QCOMPARE(r.start, (PointF{10, 20}));
    QCOMPARE(r.end, (PointF{50, 60}));
    QCOMPARE(session.document().canvasWidth, 800);
    QCOMPARE(session.document().canvasHeight, 600);
  }

  void zoomLimitsStillAllowPanAndFiniteTransforms() {
    EditorViewport view({800, 600});
    view.setViewSize({400, 300});
    for (double requested : {1e-200, 1e200}) {
      view.zoomAt(requested, {200, 150});
      QCOMPARE(view.zoom(), requested < 1 ? EditorViewport::minimumZoom : EditorViewport::maximumZoom);
      QVERIFY(view.pan({-1e100, -1e100}));
      QCOMPARE(view.offset(), view.minimumOffset());
      QVERIFY(view.pan({1e100, 1e100}));
      QCOMPARE(view.offset(), view.maximumOffset());
      const auto logical = view.toImage({200, 150});
      QVERIFY(std::isfinite(logical.x()) && std::isfinite(logical.y()));
    }
  }

  void invalidInputLeavesViewUnchanged() {
    EditorViewport view({100, 80});
    view.setViewSize({400, 300});
    view.fit();
    const double zoom = view.zoom();
    const QPointF offset = view.offset();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double scale : {0.0, -1.0, nan, inf}) QVERIFY(!view.zoomAt(scale, {50, 40}));
    QVERIFY(!view.zoomAt(2, {nan, 40}));
    QVERIFY(!view.zoomBy(inf, {50, 40}));
    QVERIFY(!view.pan({inf, 10}));
    QVERIFY(!view.setViewSize({0, 0}));
    QVERIFY(!view.setViewSize({nan, 300}));
    QCOMPARE(view.zoom(), zoom);
    QCOMPARE(view.offset(), offset);
  }
};

QTEST_GUILESS_MAIN(EditorViewportTest)
#include "EditorViewportTest.moc"
