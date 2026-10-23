// EDITOR-ANNOTATIONS-001 shape tools: gesture geometry, .xann persistence,
// export rendering and native canvas creation for every box-bounded type.
#include "EditorCanvas.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"
#include "image-editor/ShapePainter.h"

#include <QApplication>
#include <QTest>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
AnnotationDocument document(qint64 width = 200, qint64 height = 150) {
  AnnotationDocument d;
  d.canvasWidth = width;
  d.canvasHeight = height;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-23T00:00:00Z");
  d.sourceImagePng = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  return d;
}
RectangleAnnotation shapeOf(const QString &type, PointF start, PointF end, std::vector<PointF> points = {}) {
  RectangleAnnotation a;
  a.id = QUuid::createUuid();
  a.start = start; a.end = end;
  a.style = {0xFFFF0000, 0x00000000, 4, 0, 1};
  a.content.type = type; a.content.points = std::move(points);
  return a;
}
// Vertical stripes give blur and pixelate something to change.
ArgbImage stripes(qint64 width, qint64 height) {
  ArgbImage image = solidImage(width, height, 0xFFFFFFFF);
  for (qint64 y = 0; y < height; ++y)
    for (qint64 x = 0; x < width; ++x) if ((x / 2) % 2 == 0) image.pixels[std::size_t(y * width + x)] = 0xFF000000;
  return image;
}
bool same(const ArgbImage &a, const ArgbImage &b, qint64 left, qint64 top, qint64 right, qint64 bottom) {
  for (qint64 y = top; y < bottom; ++y)
    for (qint64 x = left; x < right; ++x) if (a.at(x, y) != b.at(x, y)) return false;
  return true;
}
}  // namespace

class AnnotationShapesTest : public QObject {
  Q_OBJECT
private slots:
  void lineGeometryKeepsDirectionAndPadsStraightLines() {
    const auto line = shapeGeometry(shape::arrow, {{90, 40}, {10, 40}});
    QVERIFY(line);
    QCOMPARE(line->start.x, 10.0); QCOMPARE(line->end.x, 90.0); QCOMPARE(line->end.y, 41.0);
    QCOMPARE(line->points.size(), std::size_t(2));
    QCOMPARE(line->points[0].x, 1.0); QCOMPARE(line->points[1].x, 0.0);  // tail on the right, head on the left
    QVERIFY(!shapeGeometry(shape::line, {{5, 5}, {5.2, 5.1}}));          // a click is not a line
    const auto freehand = shapeGeometry(shape::freehand, {{0, 0}, {0.2, 0.1}, {5, 5}, {10, 0}});
    QVERIFY(freehand); QCOMPARE(freehand->points.size(), std::size_t(3));
    const auto step = shapeGeometry(shape::step, {{50, 50}, {50, 50}});
    QVERIFY(step); QCOMPARE(step->start.x, 34.0); QCOMPARE(step->end.y, 66.0);
    QVERIFY(!shapeGeometry(shape::ellipse, {{5, 5}, {5, 30}}));
  }

  void everyShapeTypeRoundTripsThroughXann() {
    auto d = document();
    auto arrow = shapeOf(shape::arrow, {10, 10}, {60, 40}, {{1, 0}, {0, 1}});
    auto text = shapeOf(shape::text, {20, 20}, {120, 60}); text.content.text = "Hello\nworld"; text.content.fontSize = 18;
    auto step = shapeOf(shape::step, {50, 50}, {82, 82}); step.content.stepValue = 3;
    auto blur = shapeOf(shape::blur, {0, 0}, {40, 40}); blur.content.strength = 5;
    d.annotations = {arrow, text, step, blur, shapeOf(shape::ellipse, {1, 1}, {9, 9}), shapeOf(shape::pixelate, {2, 2}, {30, 30}),
                     shapeOf(shape::highlight, {3, 3}, {20, 9}), shapeOf(shape::freehand, {0, 0}, {10, 10}, {{0, 0}, {0.5, 1}, {1, 0}}),
                     shapeOf(shape::line, {0, 0}, {10, 1}, {{0, 0}, {1, 0}})};
    const auto parsed = parseDocument(serializeDocument(d));
    QVERIFY2(parsed.document, qPrintable(parsed.error.value_or(QString())));
    QCOMPARE(serializeDocument(*parsed.document), serializeDocument(d));
    const auto &back = std::get<RectangleAnnotation>(parsed.document->annotations[1]);
    QCOMPARE(back.type(), shape::text); QCOMPARE(back.content.text, QString("Hello\nworld")); QCOMPARE(back.content.fontSize, 18.0);
    QCOMPARE(std::get<RectangleAnnotation>(parsed.document->annotations[2]).content.stepValue, qint64(3));
    QCOMPARE(std::get<RectangleAnnotation>(parsed.document->annotations[0]).content.points[1].y, 1.0);
    // Rectangle JSON is unchanged so ES vectors and older readers still match.
    const auto rectangle = serializeAnnotationList({shapeOf(shape::rectangle, {0, 0}, {5, 5})})[0].toObject();
    QVERIFY(!rectangle.contains("points")); QVERIFY(!rectangle.contains("text")); QVERIFY(!rectangle.contains("strength"));
  }

  void malformedShapeContentIsRejected() {
    auto d = document(); d.annotations = {shapeOf(shape::line, {0, 0}, {10, 10}, {{0, 0}, {1, 1}})};
    auto payload = serializeDocument(d);
    QVERIFY(parseDocument(payload).document);
    auto annotations = payload.value("annotations").toArray(); auto line = annotations[0].toObject();
    line.insert("points", QJsonArray{QJsonObject{{"x", 0}, {"y", 0}}}); annotations[0] = line; payload.insert("annotations", annotations);
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
    line.insert("points", QJsonArray{QJsonObject{{"x", 0}, {"y", 0}}, QJsonObject{{"x", 1}, {"y", 1}}}); line.insert("fontSize", -1);
    annotations[0] = line; payload.insert("annotations", annotations);
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
  }

  void exportedShapesChangeOnlyTheirOwnArea_data() {
    QTest::addColumn<QString>("type");
    for (const auto &type : {shape::ellipse, shape::line, shape::arrow, shape::freehand, shape::text, shape::step, shape::highlight,
                             shape::blur, shape::pixelate})
      QTest::newRow(qPrintable(type)) << type;
  }
  void exportedShapesChangeOnlyTheirOwnArea() {
    QFETCH(QString, type);
    const ArgbImage source = type == shape::blur || type == shape::pixelate ? stripes(120, 80) : solidImage(120, 80, 0xFFFFFFFF);
    auto shape = shapeOf(type, {40, 20}, {80, 60}, isPathShape(type) ? std::vector<PointF>{{0, 0}, {0.5, 1}, {1, 0}} : std::vector<PointF>{});
    if (type == shape::text) shape.content.text = "XerahS";
    if (type == shape::step) shape.content.stepValue = 1;
    const auto result = render(source, {shape});
    QVERIFY(result.image);
    QCOMPARE(result.compositedAnnotations, std::vector<QUuid>{shape.id});
    QVERIFY(!same(*result.image, source, 40, 20, 80, 60));      // drawn inside its bounds
    QVERIFY(same(*result.image, source, 0, 0, 30, 80));         // untouched far outside
    QVERIFY(same(*result.image, source, 90, 0, 120, 80));
  }

  void blurSamplesTheSourceRatherThanEarlierAnnotations() {
    const ArgbImage source = solidImage(60, 40, 0xFFFFFFFF);
    auto cover = shapeOf(shape::rectangle, {0, 0}, {60, 40}); cover.style.fillColor = 0xFF0000FF; cover.style.strokeColor = 0xFF0000FF;
    const auto blur = shapeOf(shape::blur, {10, 10}, {50, 30});
    const auto result = render(source, {cover, blur});
    QVERIFY(result.image);
    QCOMPARE(result.image->at(30, 20), Argb(0xFFFFFFFF));  // EA-011: immutable source pixels
    QCOMPARE(result.image->at(5, 5), Argb(0xFF0000FF));
  }

  void stepNumbersFollowTheHighestExistingStep() {
    auto d = document();
    auto one = shapeOf(shape::step, {0, 0}, {10, 10}); one.content.stepValue = 1;
    auto four = shapeOf(shape::step, {20, 0}, {30, 10}); four.content.stepValue = 4;
    QCOMPARE(nextStepValue(d), qint64(1));
    d.annotations = {one, four};
    QCOMPARE(nextStepValue(d), qint64(5));
  }

  void shapeContentEditsAreOneUndoableStep() {
    EditorSession session(document());
    ShapeContent text; text.type = shape::text; text.text = "Before";
    const auto id = session.createShape({{10, 10}, {100, 40}, {}}, text, session.stateId(), session.selection());
    QVERIFY(id); session.select({*id});
    auto changed = text; changed.text = "After"; changed.fontSize = 30;
    QVERIFY(session.editShapeContent(*id, changed, session.stateId()));
    const auto &edited = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QCOMPARE(edited.content.text, QString("After")); QCOMPARE(edited.content.fontSize, 30.0); QCOMPARE(session.undoCount(), 2);
    QVERIFY(!session.editShapeContent(*id, changed, session.stateId()));  // no-op records no history
    auto empty = changed; empty.text.clear();
    QVERIFY(!session.editShapeContent(*id, empty, session.stateId())); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
    auto wrongType = changed; wrongType.type = shape::step;
    QVERIFY(!session.editShapeContent(*id, wrongType, session.stateId()));
    QVERIFY(session.undo());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).content.text, QString("Before"));
  }

  void canvasDragCreatesEachToolsShapeAsOneHistoryStep_data() {
    QTest::addColumn<int>("tool"); QTest::addColumn<QString>("type");
    QTest::newRow("ellipse") << int(EditorCanvas::Tool::Ellipse) << shape::ellipse;
    QTest::newRow("arrow") << int(EditorCanvas::Tool::Arrow) << shape::arrow;
    QTest::newRow("freehand") << int(EditorCanvas::Tool::Freehand) << shape::freehand;
    QTest::newRow("step") << int(EditorCanvas::Tool::Step) << shape::step;
    QTest::newRow("blur") << int(EditorCanvas::Tool::Blur) << shape::blur;
  }
  void canvasDragCreatesEachToolsShapeAsOneHistoryStep() {
    QFETCH(int, tool); QFETCH(QString, type);
    QImage source(400, 300, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(document(400, 300));
    EditorCanvas canvas(session, source); canvas.resize(600, 450); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    canvas.setTool(EditorCanvas::Tool(tool));
    const QPoint press = canvas.viewState().toView({100, 80}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, press);
    QTest::mouseMove(canvas.viewport(), press + QPoint(40, 60));
    QTest::mouseMove(canvas.viewport(), press + QPoint(90, 30));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, press + QPoint(90, 30));
    QCOMPARE(session.document().annotations.size(), std::size_t(1));
    QCOMPARE(session.undoCount(), 1);
    const auto &created = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QCOMPARE(created.type(), type);
    if (type == shape::step) QCOMPARE(created.content.stepValue, qint64(1));
    if (type == shape::freehand) QVERIFY(created.content.points.size() >= 3);
    QCOMPARE(canvas.tool(), EditorCanvas::Tool(tool));  // the tool stays active
  }
};

QTEST_MAIN(AnnotationShapesTest)
#include "AnnotationShapesTest.moc"
