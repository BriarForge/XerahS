// EDITOR-ANNOTATIONS-001 shape tools: gesture geometry, .xann persistence,
// export rendering and native canvas creation for every box-bounded type.
#include "EditorCanvas.h"
#include "TextContentDialog.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/AnnotationTransfer.h"
#include "image-editor/CanvasOperations.h"
#include "image-editor/EditorSession.h"
#include "image-editor/FreehandPath.h"
#include "image-editor/ShapePainter.h"

#include <QApplication>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>
#include <cmath>
#include <limits>
#include <tuple>

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

// EA-007 helpers.
double segDistance(const PointF &p, const PointF &a, const PointF &b) {
  const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
  const double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
// Largest distance from any raw sample to the simplified polyline.
double maxDeviation(const std::vector<PointF> &raw, const std::vector<PointF> &simple) {
  double worst = 0;
  for (const PointF &p : raw) {
    double best = 1e18;
    for (std::size_t i = 0; i + 1 < simple.size(); ++i) best = std::min(best, segDistance(p, simple[i], simple[i + 1]));
    worst = std::max(worst, best);
  }
  return worst;
}
bool isSubsequence(const std::vector<PointF> &raw, const std::vector<PointF> &simple) {
  std::size_t at = 0;
  for (const PointF &p : raw) if (at < simple.size() && p == simple[at]) ++at;
  return at == simple.size();
}
// Paints one shape the way the canvas does (document coordinates under a view scale).
QImage paintedAt(const RectangleAnnotation &shape, int w, int h, double scale) {
  QImage image(int(std::lround(w * scale)), int(std::lround(h * scale)), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.scale(scale, scale);
  paintShape(painter, shape, QImage());
  return image;
}
double coverage(const QImage &image) {  // sum of alpha in pixel units
  double sum = 0;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) sum += qAlpha(image.pixel(x, y)) / 255.0;
  return sum;
}
}  // namespace

class AnnotationShapesTest : public QObject {
  Q_OBJECT
private slots:
  void invalidCreationContentPreservesSession_data() {
    QTest::addColumn<QString>("type");
    QTest::addColumn<bool>("font");
    QTest::addColumn<double>("value");
    for (const auto &type : {shape::text, shape::speechBalloon, shape::step, shape::blur, shape::pixelate, shape::magnify, shape::spotlight}) {
      const bool font = isTextShape(type);
      const double maximum = font ? 2000 : 1000;
      const QList<double> values{-1, maximum + 0.5, std::numeric_limits<double>::infinity(),
                                -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
      for (int i = 0; i < values.size(); ++i)
        QTest::newRow(qPrintable(type + QString::number(i))) << type << font << values[i];
    }
  }
  void invalidCreationContentPreservesSession() {
    QFETCH(QString, type); QFETCH(bool, font); QFETCH(double, value);
    EditorSession session(document());
    const auto existing = session.createShape({{5, 5}, {20, 20}, {}}, ShapeContent{}, session.stateId(), {});
    QVERIFY(existing); QVERIFY(session.undo());
    const auto state = session.stateId();
    const auto selection = session.selection();
    const auto before = serializeDocument(session.document());
    ShapeContent content; content.type = type;
    if (font) content.fontSize = value; else content.strength = value;
    QVERIFY(!session.createShape({{10, 10}, {60, 50}, {}}, content, state, selection));
    QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
    QCOMPARE(session.stateId(), state); QCOMPARE(session.selection(), selection);
    QCOMPARE(serializeDocument(session.document()), before);
    QCOMPARE(session.undoCount(), 0); QVERIFY(session.redo());
  }

  void validCreationContentRoundTrips() {
    for (const auto &type : {shape::text, shape::speechBalloon, shape::step, shape::blur, shape::pixelate, shape::magnify, shape::spotlight}) {
      const bool font = isTextShape(type);
      for (const auto value : {0.0, 0.125, font ? 2000.0 : 1000.0}) {
        EditorSession session(document());
        ShapeContent content; content.type = type;
        if (font) content.fontSize = value; else content.strength = value;
        const auto id = session.createShape({{10, 10}, {60, 50}, {}}, content, session.stateId(), {});
        QVERIFY(id); QCOMPARE(session.undoCount(), 1);
        const auto saved = serializeDocument(session.document());
        const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
        QCOMPARE(serializeDocument(*loaded.document), saved);
        const auto &stored = std::get<RectangleAnnotation>(loaded.document->annotations.back()).content;
        QCOMPARE(font ? stored.fontSize : stored.strength, value);
        QVERIFY(session.undo()); QVERIFY(session.document().annotations.empty());
        QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), saved);
        const auto state = session.stateId();
        auto invalid = content;
        if (font) invalid.fontSize = std::numeric_limits<double>::quiet_NaN();
        else invalid.strength = std::numeric_limits<double>::infinity();
        QVERIFY(!session.editShapeContent(*id, invalid, state));
        QCOMPARE(session.stateId(), state); QCOMPARE(session.undoCount(), 1);
        QCOMPARE(serializeDocument(session.document()), saved);
      }
    }
  }

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


  void freehandSimplificationRemovesJitterButKeepsOrderEndpointsAndCorners() {
    // An L: 60 px right then 60 px down, sampled every 1 px with +/-0.2 px jitter (peak-to-peak under the 0.5 px tolerance).
    std::vector<PointF> raw;
    for (int i = 0; i <= 60; ++i) raw.push_back({double(i), 100 + ((i % 2) ? 0.2 : -0.2)});
    for (int i = 1; i <= 60; ++i) raw.push_back({60 + ((i % 2) ? 0.2 : -0.2), 100.0 + i});
    const auto simple = simplifyFreehand(raw);
    QVERIFY(simple.size() < raw.size() / 4);
    QVERIFY(isSubsequence(raw, simple));                      // order preserved, only raw samples
    QCOMPARE(simple.front(), raw.front()); QCOMPARE(simple.back(), raw.back());
    QVERIFY(maxDeviation(raw, simple) <= kFreehandToleranceDocumentPixels + 1e-9);
    bool corner = false;                                      // a kept vertex near (60,100)
    for (const PointF &p : simple) corner = corner || std::hypot(p.x - 60, p.y - 100) <= 1.5;
    QVERIFY(corner);
    // Zigzag with sharp 90 degree turns every 10 px keeps every apex.
    std::vector<PointF> zig;
    for (int k = 0; k < 5; ++k) { const double x = k * 10; zig.push_back({x, 0}); zig.push_back({x + 5, 10}); }
    const auto kept = simplifyFreehand(zig);
    QCOMPARE(kept.size(), zig.size());
    // A straight line collapses to its endpoints; duplicates are dropped.
    const auto line = simplifyFreehand({{0, 0}, {0, 0}, {3, 0.1}, {6, -0.1}, {10, 0}});
    QCOMPARE(line.size(), std::size_t(2));
    QCOMPARE(simplifyFreehand({{1, 1}, {1, 1}}).size(), std::size_t(1));
  }

  void freehandSimplificationNeverExceedsToleranceOnCurves() {
    std::vector<PointF> arc;  // radius 80 quarter circle, 0.7 px steps
    for (int i = 0; i <= 160; ++i) { const double a = i * 0.0125 * M_PI / 1.0 / 2 / 1.0; arc.push_back({80 * std::cos(a), 80 * std::sin(a)}); }
    const auto simple = simplifyFreehand(arc);
    QVERIFY(simple.size() < arc.size());
    QVERIFY(isSubsequence(arc, simple));
    QVERIFY(maxDeviation(arc, simple) <= kFreehandToleranceDocumentPixels + 1e-9);
  }

  void freehandGestureAdmissionIsUnchanged() {
    QVERIFY(!shapeGeometry(shape::freehand, {}));
    QVERIFY(!shapeGeometry(shape::freehand, {{1, 1}}));
    QVERIFY(!shapeGeometry(shape::freehand, {{1, 1}, {1, 1}, {1.2, 1.1}}));  // a click
    QVERIFY(!shapeGeometry(shape::freehand, {{0, 0}, {std::numeric_limits<double>::quiet_NaN(), 3}, {9, 9}}));
    QVERIFY(!shapeGeometry(shape::freehand, {{0, 0}, {std::numeric_limits<double>::infinity(), 3}}));
    QVERIFY(shapeGeometry(shape::freehand, {{0, 0}, {40, 0}}));              // a straight stroke is valid
  }

  void freehandStrokeIsIdenticalAtEveryZoomAndExportScale() {
    std::vector<PointF> raw{{10, 20}, {40, 20}, {40, 60}, {70, 30}, {90, 30}};
    const auto geometry = shapeGeometry(shape::freehand, raw);
    QVERIFY(geometry);
    auto freehand = shapeOf(shape::freehand, geometry->start, geometry->end, geometry->points);
    freehand.style = {0xFF0000FF, 0x00000000, 6, 0, 1};
    const QImage one = paintedAt(freehand, 100, 80, 1.0);
    const double base = coverage(one);
    QVERIFY(base > 500);
    for (const double scale : {0.5, 2.0, 3.0}) {
      const double scaled = coverage(paintedAt(freehand, 100, 80, scale)) / (scale * scale);
      // Stroke width is in document units: coverage scales with area, within 3 %.
      QVERIFY2(std::abs(scaled - base) / base < 0.03, qPrintable(QString("scale %1: %2 vs %3").arg(scale).arg(scaled).arg(base)));
    }
    // Export path (AnnotationRenderer) at 1x matches the canvas painter within 1 % coverage.
    auto d = document(100, 80);
    d.annotations = {freehand};
    ArgbImage white = solidImage(100, 80, 0xFFFFFFFF);
    const auto exported = renderDocument(white, d);
    QVERIFY(exported.image);
    double inked = 0;  // blue stroke on white: red channel drops from 255 to 0
    for (qint64 y = 0; y < 80; ++y) for (qint64 x = 0; x < 100; ++x) inked += (255 - qRed(exported.image->at(x, y))) / 255.0;
    QVERIFY2(std::abs(inked - base) / base < 0.01, qPrintable(QString("export %1 vs canvas %2").arg(inked).arg(base)));
    // Round caps and joins: the stroke end extends half the width beyond the last point.
    QVERIFY(qAlpha(one.pixel(92, 30)) > 200);
    QVERIFY(qAlpha(one.pixel(94, 30)) == 0 || qAlpha(one.pixel(94, 30)) < 255);
  }

  void freehandSimplifiedPointsRoundTripAndCreateOneHistoryStep() {
    std::vector<PointF> raw;
    for (int i = 0; i <= 50; ++i) raw.push_back({double(i), 20 + ((i % 2) ? 0.2 : -0.2)});
    for (int i = 1; i <= 30; ++i) raw.push_back({50.0, 20.0 + i});
    const auto geometry = shapeGeometry(shape::freehand, raw);
    QVERIFY(geometry); QVERIFY(geometry->points.size() <= 4);
    EditorSession session(document(200, 150));
    ShapeContent content; content.type = shape::freehand; content.points = geometry->points;
    QVERIFY(session.createShape({geometry->start, geometry->end, {0xFFFF0000, 0, 4, 0, 1}}, content, session.stateId(), session.selection()));
    QCOMPARE(session.undoCount(), 1);
    const auto parsed = parseDocument(serializeDocument(session.document()));
    QVERIFY(parsed.document);
    QCOMPARE(std::get<RectangleAnnotation>(parsed.document->annotations[0]).content.points, geometry->points);
    QVERIFY(session.undo()); QVERIFY(session.document().annotations.empty());
  }

  void stepStylesMapValuesLikeTheReference() {
    // EA-009 vectors from the reference StepTypeFormatter: bijective base 26, subtractive Roman, M repeated.
    struct Vector { qint64 value; const char *upper, *lower, *roman, *lowerRoman; };
    const Vector vectors[] = {
        {0, "0", "0", "0", "0"}, {-5, "-5", "-5", "-5", "-5"}, {1, "A", "a", "I", "i"}, {4, "D", "d", "IV", "iv"}, {9, "I", "i", "IX", "ix"},
        {26, "Z", "z", "XXVI", "xxvi"}, {27, "AA", "aa", "XXVII", "xxvii"}, {52, "AZ", "az", "LII", "lii"},
        {702, "ZZ", "zz", "DCCII", "dccii"}, {703, "AAA", "aaa", "DCCIII", "dcciii"},
        {1994, "BXR", "bxr", "MCMXCIV", "mcmxciv"}, {3999, "EWU", "ewu", "MMMCMXCIX", "mmmcmxcix"}, {4000, "EWV", "ewv", "MMMM", "mmmm"}};
    for (const auto &v : vectors) {
      QCOMPARE(formatStepValue(v.value, StepStyle::Numeric), QString::number(v.value));
      QCOMPARE(formatStepValue(v.value, StepStyle::UppercaseLetter), QString(v.upper));
      QCOMPARE(formatStepValue(v.value, StepStyle::LowercaseLetter), QString(v.lower));
      QCOMPARE(formatStepValue(v.value, StepStyle::UppercaseRoman), QString(v.roman));
      QCOMPARE(formatStepValue(v.value, StepStyle::LowercaseRoman), QString(v.lowerRoman));
    }
  }
  void stepStyleNamesFollowTheReferenceSpelling() {
    for (const auto style : {StepStyle::Numeric, StepStyle::UppercaseLetter, StepStyle::LowercaseLetter, StepStyle::UppercaseRoman, StepStyle::LowercaseRoman}) {
      const auto parsed = parseStepStyle(stepStyleName(style)); QVERIFY(parsed); QVERIFY(*parsed == style);
    }
    QCOMPARE(stepStyleName(StepStyle::UppercaseLetter), QString("uppercaseLetter"));
    QCOMPARE(stepStyleName(StepStyle::LowercaseRoman), QString("lowercaseRoman"));
    QVERIFY(parseStepStyle("UPPERCASEROMAN") && *parseStepStyle("UPPERCASEROMAN") == StepStyle::UppercaseRoman);
    QVERIFY(!parseStepStyle("greek")); QVERIFY(!parseStepStyle(""));
  }
  void stepStyleRoundTripsPerObjectAndRejectsUnknownNames() {
    auto d = document();
    qint64 value = 1;
    for (const auto style : {StepStyle::Numeric, StepStyle::UppercaseLetter, StepStyle::LowercaseLetter, StepStyle::UppercaseRoman, StepStyle::LowercaseRoman}) {
      auto step = shapeOf(shape::step, {10.0 * double(value), 10}, {10.0 * double(value) + 8, 18});
      step.content.stepValue = value * 27; step.content.stepStyle = style; ++value; d.annotations.push_back(step);
    }
    const auto json = serializeAnnotationList(d.annotations);
    QVERIFY(!json[0].toObject().contains("stepType"));  // numeric is the legacy default and is omitted
    QCOMPARE(json[1].toObject().value("stepType").toString(), QString("uppercaseLetter"));
    QCOMPARE(json[4].toObject().value("stepType").toString(), QString("lowercaseRoman"));
    const auto parsed = parseDocument(serializeDocument(d)); QVERIFY(parsed.document);
    for (std::size_t i = 0; i < 5; ++i) {
      const auto &back = std::get<RectangleAnnotation>(parsed.document->annotations[i]);
      QVERIFY(back.content.stepStyle == std::get<RectangleAnnotation>(d.annotations[i]).content.stepStyle);
      QCOMPARE(back.content.stepValue, std::get<RectangleAnnotation>(d.annotations[i]).content.stepValue);
      QVERIFY(!back.extra.contains("stepType"));
    }
    QCOMPARE(serializeDocument(*parsed.document), serializeDocument(d));
    // A reference file spells the value case-insensitively; an unknown name or type is invalid, and stepType on another tool stays inert.
    auto payload = serializeDocument(d); auto list = payload["annotations"].toArray(); auto first = list[0].toObject();
    first["stepType"] = "UppercaseRoman"; list[0] = first; payload["annotations"] = list;
    const auto reference = parseDocument(payload); QVERIFY(reference.document);
    QVERIFY(std::get<RectangleAnnotation>(reference.document->annotations[0]).content.stepStyle == StepStyle::UppercaseRoman);
    for (const QJsonValue &bad : {QJsonValue("greek"), QJsonValue(2), QJsonValue(true), QJsonValue("")}) {
      first["stepType"] = bad; list[0] = first; payload["annotations"] = list; QVERIFY(!parseDocument(payload).document);
    }
    auto rect = shapeOf(shape::rectangle, {0, 0}, {5, 5}); auto rectJson = serializeAnnotationList({rect})[0].toObject(); rectJson["stepType"] = "greek";
    const auto kept = parseAnnotationList(QJsonArray{rectJson}); QVERIFY(kept.annotations);
    QCOMPARE(serializeAnnotationList(*kept.annotations)[0].toObject().value("stepType").toString(), QString("greek"));
  }
  void stepStartAndResetAffectOnlyFutureAllocation() {
    auto d = document(); StepSequence sequence;
    QCOMPARE(nextStepValue(d, sequence), qint64(1));
    sequence.start = 4; QCOMPARE(nextStepValue(d, sequence), qint64(4));  // explicit start on an empty document
    auto two = shapeOf(shape::step, {0, 0}, {10, 10}); two.content.stepValue = 2;
    auto seven = shapeOf(shape::step, {20, 0}, {30, 10}); seven.content.stepValue = 7;
    d.annotations = {two, seven};
    QCOMPARE(nextStepValue(d, sequence), qint64(8));  // one past the highest, above the start
    sequence.start = 10; QCOMPARE(nextStepValue(d, sequence), qint64(10));  // never below the start
    sequence.start = 1;
    resetStepSequence(sequence, d);
    QCOMPARE(nextStepValue(d, sequence), qint64(1));  // reset: next allocation is the start value
    QCOMPARE(std::get<RectangleAnnotation>(d.annotations[0]).content.stepValue, qint64(2));  // existing objects keep values
    QCOMPARE(std::get<RectangleAnnotation>(d.annotations[1]).content.stepValue, qint64(7));
    auto fresh = shapeOf(shape::step, {40, 0}, {50, 10}); fresh.content.stepValue = 1; d.annotations.push_back(fresh);
    QCOMPARE(nextStepValue(d, sequence), qint64(2));  // numbering continues from the new Step
    d.annotations.pop_back(); QCOMPARE(nextStepValue(d, sequence), qint64(1));  // deleting or undoing it is deterministic
    sequence.start = 5; QCOMPARE(nextStepValue(d, sequence), qint64(5));
    sequence.start = 99; QCOMPARE(nextStepValue(d, sequence), qint64(10));  // out-of-range starts clamp to the reference 1..10
    sequence.start = -3; QCOMPARE(nextStepValue(d, sequence), qint64(1));
    // The legacy overload is the default sequence.
    QCOMPARE(nextStepValue(d), qint64(8));
  }
  void stepStyleIsOneUndoableContentEditAndPaintsTheFormattedLabel() {
    EditorSession session(document()); ShapeContent content; content.type = shape::step; content.stepValue = 27;
    QVERIFY(session.createShape({{20, 20}, {70, 70}, {0xFFFF0000, 0xFF0000FF, 4, 0, 1}}, content, session.stateId(), session.selection()));
    const auto before = session.undoCount(); const auto id = std::get<RectangleAnnotation>(session.document().annotations[0]).id;
    content.stepStyle = StepStyle::UppercaseLetter;
    QVERIFY(session.editShapeContent(id, content, session.stateId()));
    QCOMPARE(session.undoCount(), before + 1);
    QVERIFY(session.selectedRectangle()->content.stepStyle == StepStyle::UppercaseLetter);
    // The label really changes: "27" and "AA" paint different pixels in the same circle.
    QImage numeric(100, 100, QImage::Format_ARGB32), lettered = numeric; numeric.fill(Qt::white); lettered.fill(Qt::white);
    auto step = *session.selectedRectangle(); step.content.stepStyle = StepStyle::Numeric;
    { QPainter p(&numeric); paintShape(p, step, numeric); }
    step.content.stepStyle = StepStyle::UppercaseRoman; step.content.stepValue = 4000;  // MMMM must shrink to fit, not overflow the circle
    { QPainter p(&lettered); paintShape(p, step, lettered); }
    QVERIFY(numeric != lettered);
    for (int x = 0; x < 100; ++x) for (int y : {5, 90}) QCOMPARE(lettered.pixel(x, y), QColor(Qt::white).rgba());  // nothing leaks above or below the circle
    QVERIFY(session.undo()); QVERIFY(std::get<RectangleAnnotation>(session.document().annotations[0]).content.stepStyle == StepStyle::Numeric);
    QVERIFY(session.redo()); QVERIFY(std::get<RectangleAnnotation>(session.document().annotations[0]).content.stepStyle == StepStyle::UppercaseLetter);
  }

  void stepValuesStayStableAcrossDeleteReorderUndoRedoAndReload() {
    EditorSession session(document(300, 100));
    for (int i = 0; i < 4; ++i) {
      ShapeContent content; content.type = shape::step; content.stepValue = nextStepValue(session.document()); content.stepStyle = i % 2 ? StepStyle::LowercaseRoman : StepStyle::UppercaseLetter;
      QVERIFY(session.createShape({{10.0 + 40 * i, 10}, {40.0 + 40 * i, 40}, {}}, content, session.stateId(), session.selection()));
    }
    const auto values = [&] { QList<qint64> out; for (const auto &a : session.document().annotations) out << std::get<RectangleAnnotation>(a).content.stepValue; return out; };
    QCOMPARE(values(), (QList<qint64>{1, 2, 3, 4}));
    const auto second = std::get<RectangleAnnotation>(session.document().annotations[1]).id;
    session.select({second}); QVERIFY(session.deleteSelection());
    QCOMPARE(values(), (QList<qint64>{1, 3, 4}));  // deleting never renumbers the survivors
    QCOMPARE(nextStepValue(session.document()), qint64(5));
    session.select({std::get<RectangleAnnotation>(session.document().annotations[0]).id}); QVERIFY(session.reorderSelection(EditorSession::Order::Front));
    QCOMPARE(values(), (QList<qint64>{3, 4, 1}));  // reordering moves values with their objects
    const auto expected = serializeAnnotationList(session.document().annotations);
    QVERIFY(session.undo()); QCOMPARE(values(), (QList<qint64>{1, 3, 4}));
    QVERIFY(session.undo()); QCOMPARE(values(), (QList<qint64>{1, 2, 3, 4}));
    QVERIFY(session.redo()); QVERIFY(session.redo()); QCOMPARE(serializeAnnotationList(session.document().annotations), expected);
    const auto reloaded = parseAnnotationList(expected); QVERIFY(reloaded.annotations);
    QCOMPARE(serializeAnnotationList(*reloaded.annotations), expected);
    QCOMPARE(std::get<RectangleAnnotation>((*reloaded.annotations)[0]).content.stepValue, qint64(3));
    QVERIFY(std::get<RectangleAnnotation>((*reloaded.annotations)[0]).content.stepStyle == StepStyle::UppercaseLetter);
  }
  // EA-010 helpers and tests for the Emoji and Cursor assets (Image is covered above).
  static QByteArray pngOf(const QImage &image) {
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return png;
  }
  static QByteArray ihdrOnlyPng(quint32 width, quint32 height) {  // sound header and IEND, no pixel data to decode
    QByteArray png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
    for (quint32 v : {width, height}) for (int shift = 24; shift >= 0; shift -= 8) png.append(char((v >> shift) & 0xFF));
    return png + QByteArray(9, '\0') + QByteArray("\0\0\0\0IEND\xAE\x42\x60\x82", 12);
  }
  void emojiAndCursorAssetsPreserveAlpha_data() { QTest::addColumn<QString>("type"); QTest::newRow("emoji") << shape::emoji; QTest::newRow("cursor") << shape::cursor; }
  void emojiAndCursorAssetsPreserveAlpha() {
    QFETCH(QString, type);
    QImage asset(8, 8, QImage::Format_ARGB32); asset.fill(Qt::transparent);
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 8; ++x) asset.setPixel(x, y, qRgba(255, 0, 0, 128));  // translucent top half
    for (int y = 4; y < 8; ++y) for (int x = 0; x < 8; ++x) asset.setPixel(x, y, qRgba(0, 0, 255, 255));  // opaque bottom half
    auto d = document(100, 100); auto annotation = shapeOf(type, {10, 10}, {50, 50}); annotation.content.imageKey = "alpha";
    d.embeddedImages = {{"alpha", pngOf(asset)}}; d.annotations = {annotation};
    const auto reloaded = parseDocument(serializeDocument(d)); QVERIFY(reloaded.document);
    QCOMPARE(reloaded.document->embeddedImages, d.embeddedImages);  // bytes, and so alpha, survive the round trip untouched
    QCOMPARE(QImage::fromData(reloaded.document->embeddedImages[0].second, "PNG").pixelColor(0, 0).alpha(), 128);
    const auto result = renderDocument(solidImage(100, 100, 0xFFFFFFFF), *reloaded.document); QVERIFY(result.image);
    const Argb half = result.image->at(30, 20);  // white under 50% red
    QVERIFY(qAbs(qRed(half) - 255) <= 1); QVERIFY(qAbs(qGreen(half) - 127) <= 2); QVERIFY(qAbs(qBlue(half) - 127) <= 2); QCOMPARE(qAlpha(half), 255);
    QCOMPARE(result.image->at(30, 40), Argb(0xFF0000FF));  // opaque pixels replace the source
    QCOMPARE(result.image->at(5, 5), Argb(0xFFFFFFFF));    // outside the object nothing changes
    auto clear = annotation; d.annotations = {clear}; d.embeddedImages = {{"alpha", pngOf([] { QImage i(8, 8, QImage::Format_ARGB32); i.fill(Qt::transparent); return i; }())}};
    const auto empty = renderDocument(solidImage(100, 100, 0xFF336699), d); QVERIFY(empty.image);
    QVERIFY(same(*empty.image, solidImage(100, 100, 0xFF336699), 0, 0, 100, 100));  // a fully transparent asset leaves the source exact
  }
  void emojiAndCursorAssetsKeepAspectRatioAtEverySize_data() { emojiAndCursorAssetsPreserveAlpha_data(); }
  void emojiAndCursorAssetsKeepAspectRatioAtEverySize() {
    QFETCH(QString, type);
    for (const QSize size : {QSize(20, 10), QSize(10, 20), QSize(7, 7)}) {
      QImage asset(size, QImage::Format_ARGB32); asset.fill(QColor(0, 200, 0));
      auto d = document(100, 100); auto annotation = shapeOf(type, {10, 10}, {50, 50}); annotation.content.imageKey = "a";
      d.embeddedImages = {{"a", pngOf(asset)}}; d.annotations = {annotation};
      const auto result = renderDocument(solidImage(100, 100, 0xFFFFFFFF), d); QVERIFY(result.image);
      QCOMPARE(result.image->at(30, 30), Argb(0xFF00C800));  // the centre is covered
      // The 40x40 box is letterboxed on the short side, never stretched.
      const Argb band = size.width() == size.height() ? Argb(0xFF00C800) : Argb(0xFFFFFFFF);
      if (size.width() > size.height()) { QCOMPARE(result.image->at(30, 15), band); QCOMPARE(result.image->at(30, 45), band); QCOMPARE(result.image->at(12, 30), Argb(0xFF00C800)); }
      else if (size.width() < size.height()) { QCOMPARE(result.image->at(15, 30), band); QCOMPARE(result.image->at(45, 30), band); QCOMPARE(result.image->at(30, 12), Argb(0xFF00C800)); }
      else { QCOMPARE(result.image->at(12, 12), band); QCOMPARE(result.image->at(48, 48), band); }
    }
  }
  void resizeKeepsEmojiAndCursorAssetsAndTheirAspectRatio_data() { emojiAndCursorAssetsPreserveAlpha_data(); }
  void resizeKeepsEmojiAndCursorAssetsAndTheirAspectRatio() {
    QFETCH(QString, type);
    QImage asset(20, 10, QImage::Format_ARGB32); asset.fill(QColor(0, 200, 0)); const auto png = pngOf(asset);
    EditorSession session(document(200, 200)); ShapeContent content; content.type = type; content.imageKey = "k";
    const auto id = session.createShape({{10, 10}, {50, 50}, {}}, content, session.stateId(), session.selection(), {{"k", png}});
    QVERIFY(id); const auto undo = session.undoCount();
    QVERIFY(session.resizeRectangle(*id, RectangleHandle::BottomRight, {60, 20}, session.stateId()));
    QCOMPARE(session.undoCount(), undo + 1);
    const auto &resized = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QCOMPARE(resized.end.x, 110.0); QCOMPARE(resized.end.y, 70.0); QCOMPARE(resized.content.imageKey, QString("k"));
    QCOMPARE(session.document().embeddedImages.size(), std::size_t(1)); QCOMPARE(session.document().embeddedImages[0].second, png);  // pixels untouched by resize
    const auto result = renderDocument(solidImage(200, 200, 0xFFFFFFFF), session.document()); QVERIFY(result.image);
    // 100x60 box, 2:1 asset: a centred 100x50 band; the 5 px bands above and below stay white.
    QCOMPARE(result.image->at(60, 40), Argb(0xFF00C800)); QCOMPARE(result.image->at(60, 12), Argb(0xFFFFFFFF)); QCOMPARE(result.image->at(60, 68), Argb(0xFFFFFFFF));
    QVERIFY(session.undo()); QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).end.x, 50.0);
    QCOMPARE(session.document().embeddedImages[0].second, png);
  }
  void oversizedOrUnsafeEmojiAndCursorAssetsAreRejectedBeforeDecode_data() { emojiAndCursorAssetsPreserveAlpha_data(); }
  void oversizedOrUnsafeEmojiAndCursorAssetsAreRejectedBeforeDecode() {
    QFETCH(QString, type);
    // Creation: only a sound, bounded PNG header enters the document, and a refusal changes nothing.
    EditorSession session(document(200, 200)); ShapeContent content; content.type = type; content.imageKey = "k";
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount();
    for (const auto &bad : {QByteArray(), QByteArray("not a png at all, just text bytes......."), ihdrOnlyPng(0, 5), ihdrOnlyPng(200000, 1),
                            ihdrOnlyPng(16384, 16384), ihdrOnlyPng(5, 0xFFFFFFFFu), QByteArray(ihdrOnlyPng(4, 4)).chopped(4)}) {
      QVERIFY(!session.createShape({{10, 10}, {50, 50}, {}}, content, session.stateId(), session.selection(), {{"k", bad}}));
      QCOMPARE(session.editDiagnostic(), QString("annotation-asset-invalid")); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    }
    QVERIFY(session.createShape({{10, 10}, {50, 50}, {}}, content, session.stateId(), session.selection(), {{"k", ihdrOnlyPng(8000, 8000)}}));  // at the 64M-pixel bound
    // Import: a header over the document limits is document-too-large before any decode; renderers refuse over-limit decodes.
    auto d = document(100, 100); auto annotation = shapeOf(type, {10, 10}, {50, 50}); annotation.content.imageKey = "big"; d.annotations = {annotation};
    d.embeddedImages = {{"big", ihdrOnlyPng(200000, 1)}};
    QCOMPARE(parseDocument(serializeDocument(d)).error, std::optional<QString>(diagnostic::documentTooLarge));
    d.embeddedImages = {{"big", ihdrOnlyPng(16000, 16000)}};  // legal in a document, but more than the renderer will decode
    const auto parsed = parseDocument(serializeDocument(d)); QVERIFY(parsed.document);
    QVERIFY(decodeAsset(ihdrOnlyPng(16000, 16000)).isNull()); QVERIFY(!validEmbeddedPng(ihdrOnlyPng(16000, 16000)));
    const auto drawn = renderDocument(solidImage(100, 100, 0xFFFFFFFF), *parsed.document); QVERIFY(drawn.image);
    QVERIFY(!same(*drawn.image, solidImage(100, 100, 0xFFFFFFFF), 10, 10, 50, 50));  // visible placeholder instead of a crash or a decode
  }
  void cursorToolCreatesAnAlphaAssetOnTheCanvas() {
    QImage source(300, 200, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(document(300, 200)); EditorCanvas canvas(session, source); canvas.resize(500, 400); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom();
    canvas.setTool(EditorCanvas::Tool::Cursor);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({100, 80}).toPoint());
    QCOMPARE(session.undoCount(), 1); const auto &cursor = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QCOMPARE(cursor.type(), shape::cursor); QCOMPARE(session.document().embeddedImages.size(), std::size_t(1));
    const QImage decoded = QImage::fromData(session.document().embeddedImages[0].second, "PNG"); QVERIFY(!decoded.isNull());
    QVERIFY(decoded.hasAlphaChannel()); QCOMPARE(decoded.pixelColor(decoded.width() - 1, decoded.height() - 1).alpha(), 0);  // transparent corner survives
    QCOMPARE(cursor.content.imageKey, session.document().embeddedImages[0].first);
    QCOMPARE((cursor.end.x - cursor.start.x) / (cursor.end.y - cursor.start.y), double(decoded.width()) / decoded.height());  // click size keeps the asset ratio
  }

  // EA-019: one object of every one of the 17 drawable types, each with every field that applies to it set
  // away from its default, plus embedded assets, an unknown type and compatible unknown fields.
  void allSeventeenShapeTypesRoundTripEveryFieldThroughXann() {
    QImage pixel(3, 3, QImage::Format_ARGB32); pixel.fill(QColor(10, 200, 30, 90));
    QByteArray png; { QBuffer b(&png); QVERIFY(b.open(QIODevice::WriteOnly)); QVERIFY(pixel.save(&b, "PNG")); }
    auto d = document(300, 300); d.imagePath = "source.png"; d.extra = {{"vendor", QJsonObject{{"keep", true}}}};
    d.embeddedImages = {{"img-asset", png}, {"emoji-asset", png + QByteArray()}, {"cursor-asset", png}, {"unused-asset", png}};
    int n = 0;
    const auto base = [&](const QString &type, std::vector<PointF> points = {}) {
      auto a = shapeOf(type, {10.0 + n * 3, 20.5 + n}, {60.0 + n * 3, 90.25 + n}, std::move(points)); ++n;
      a.style = {0xCC112233, 0x44AABBCC, 3.5 + n, 12.5 + n, 0.75, BorderStyle::Solid, 0, {}, ArrowStyle::Classic};
      a.extra = {{"vendorField", QString("keep-%1").arg(type)}, {"vendorNumber", n}}; return a;
    };
    const auto decorate = [](RectangleAnnotation &a) {
      a.style.borderStyle = BorderStyle::DashDotDot; a.style.shadow = {true, 6.5, -2, 4.5, 0.4, 0x80102030};
    };
    std::vector<RectangleAnnotation> expected;
    { auto a = base(shape::rectangle); decorate(a); a.style.cornerRadius = 9; expected.push_back(a); }
    { auto a = base(shape::ellipse); decorate(a); expected.push_back(a); }
    { auto a = base(shape::line, {{0, 0}, {1, 1}, {0.4, 1.7}}); decorate(a); expected.push_back(a); }
    { auto a = base(shape::arrow, {{1, 0}, {0, 1}, {0.6, -0.5}}); decorate(a); a.style.arrowStyle = ArrowStyle::Double; expected.push_back(a); }
    { auto a = base(shape::freehand, {{0, 0}, {0.25, 0.9}, {0.5, 0.1}, {1, 1}}); expected.push_back(a); }
    { auto a = base(shape::text); a.content.text = "Line 1\nLíne 2 \xF0\x9F\x98\x80"; a.content.fontSize = 27.25; a.content.fontFamily = "Some Font That Is Not Installed";
      a.content.textBold = false; a.content.textItalic = true; a.content.textAlignment = "right"; a.content.textColor = 0x7F010203; a.content.textFillColor = 0x00000000;
      a.content.textBorderColor = 0x80FFEEDD; expected.push_back(a); }
    { auto a = base(shape::speechBalloon, {{0.2, 1.6}}); a.content.text = "Hi"; a.content.fontSize = 19.5; a.content.fontFamily = "Serif"; a.content.textBold = true; a.content.textItalic = false;
      a.content.textAlignment = "left"; a.content.textColor = 0xFF0A0B0C; a.content.textFillColor = 0x20304050; a.content.balloonBorderColor = 0x00ABCDEF; expected.push_back(a); }
    { auto a = base(shape::step); a.content.stepValue = 4000; a.content.stepStyle = StepStyle::UppercaseRoman; a.content.fontSize = 31.5; expected.push_back(a); }
    { auto a = base(shape::highlight); a.content.isEllipse = true; expected.push_back(a); }
    { auto a = base(shape::blur); a.content.strength = 12.5; a.content.isEllipse = true; expected.push_back(a); }
    { auto a = base(shape::pixelate); a.content.strength = 7.75; a.content.isEllipse = true; expected.push_back(a); }
    { auto a = base(shape::magnify); a.content.strength = 3.5; a.content.isEllipse = true; expected.push_back(a); }
    { auto a = base(shape::spotlight); a.content.strength = 42.5; a.content.isEllipse = true; a.content.blurAmount = 5.5; expected.push_back(a); }
    { auto a = base(shape::smartEraser); a.visible = false; expected.push_back(a); }
    { auto a = base(shape::image); a.content.imageKey = "img-asset"; expected.push_back(a); }
    { auto a = base(shape::emoji); a.content.imageKey = "emoji-asset"; expected.push_back(a); }
    { auto a = base(shape::cursor); a.content.imageKey = "cursor-asset"; expected.push_back(a); }
    QCOMPARE(int(expected.size()), int(shapeTypes().size()));  // every type this build can draw
    QStringList covered; for (const auto &a : expected) covered << a.type(); covered.sort(); auto all = shapeTypes(); all.sort(); QCOMPARE(covered, all);
    QCOMPARE(expected.size(), std::size_t(17));  // the contract's 17 drawable types
    for (const auto &a : expected) d.annotations.push_back(a);
    const auto future = QUuid::createUuid();
    d.annotations.insert(d.annotations.begin() + 5, UnsupportedAnnotation{future, {{"id", future.toString(QUuid::WithoutBraces)}, {"type", "future-widget"}, {"payload", QJsonObject{{"label", "future"}, {"n", 3}}}, {"zIndex", 5}}});
    const auto expectField = [](const RectangleAnnotation &back, const RectangleAnnotation &want) {
      const QByteArray tagBytes = want.type().toUtf8(); const char *tag = tagBytes.constData();
      QVERIFY2(back.id == want.id, tag); QVERIFY2(back.type() == want.type(), tag);
      QVERIFY2(back.start == want.start && back.end == want.end, tag); QVERIFY2(back.visible == want.visible, tag);
      QVERIFY2(back.style.strokeColor == want.style.strokeColor && back.style.fillColor == want.style.fillColor, tag);
      QVERIFY2(back.style.strokeWidth == want.style.strokeWidth && back.style.rotationDegrees == want.style.rotationDegrees && back.style.opacity == want.style.opacity, tag);
      if (hasShapeDecoration(want.type())) { QVERIFY2(sameDecoration(back.style, want.style), tag); QVERIFY2(back.style.borderStyle == want.style.borderStyle && back.style.shadow == want.style.shadow, tag); }
      if (want.type() == shape::rectangle) QVERIFY2(back.style.cornerRadius == want.style.cornerRadius, tag);
      if (want.type() == shape::arrow) QVERIFY2(back.style.arrowStyle == want.style.arrowStyle, tag);
      const auto &c = back.content, &w = want.content;
      QVERIFY2(c.points == w.points, tag); QVERIFY2(c.text == w.text && c.fontSize == w.fontSize && c.fontFamily == w.fontFamily, tag);
      QVERIFY2(c.textBold == w.textBold && c.textItalic == w.textItalic && c.textAlignment == w.textAlignment, tag);
      QVERIFY2(c.textColor == w.textColor && c.textFillColor == w.textFillColor && c.textBorderColor == w.textBorderColor && c.balloonBorderColor == w.balloonBorderColor, tag);
      QVERIFY2(c.stepValue == w.stepValue && c.stepStyle == w.stepStyle, tag);
      QVERIFY2(c.strength == w.strength && c.isEllipse == w.isEllipse && c.blurAmount == w.blurAmount && c.imageKey == w.imageKey, tag);
      QVERIFY2(back.extra == want.extra, tag);
    };
    const auto verify = [&](const AnnotationDocument &back) {
      QCOMPARE(back.annotations.size(), d.annotations.size()); QCOMPARE(back.extra, d.extra);
      // Assets are keyed JSON members: every key keeps its exact bytes (the member order is the key order).
      QCOMPARE(back.embeddedImages.size(), d.embeddedImages.size());
      for (const auto &[key, bytes] : d.embeddedImages) { const auto at = std::find_if(back.embeddedImages.begin(), back.embeddedImages.end(), [&](const auto &e) { return e.first == key; }); QVERIFY(at != back.embeddedImages.end()); QCOMPARE(at->second, bytes); }
      for (std::size_t i = 0; i < d.annotations.size(); ++i) {
        QCOMPARE(annotationId(back.annotations[i]), annotationId(d.annotations[i]));  // stable IDs in stable order
        if (const auto *unknown = std::get_if<UnsupportedAnnotation>(&d.annotations[i])) {
          const auto *kept = std::get_if<UnsupportedAnnotation>(&back.annotations[i]); QVERIFY(kept); QCOMPARE(kept->raw, unknown->raw); continue;  // verbatim, inert
        }
        const auto *got = std::get_if<RectangleAnnotation>(&back.annotations[i]); QVERIFY(got);
        expectField(*got, std::get<RectangleAnnotation>(d.annotations[i]));
        if (QTest::currentTestFailed()) return;
      }
    };
    // JSON document, then the gzip .xann bytes a save writes, then a second generation: all identical.
    const auto json = parseDocument(serializeDocument(d)); QVERIFY2(json.document, qPrintable(json.error.value_or(QString()))); verify(*json.document);
    if (QTest::currentTestFailed()) return;
    const auto bytes = writeXann(d); QVERIFY(bytes); const auto file = readXann(*bytes); QVERIFY(file.document); verify(*file.document);
    if (QTest::currentTestFailed()) return;
    QCOMPARE(serializeDocument(*file.document), serializeDocument(d)); QCOMPARE(serializeDocument(*parseDocument(serializeDocument(*file.document)).document), serializeDocument(d));
    // The unknown type stays inert through a rendering pass and an object-transfer pass.
    const auto rendered = renderDocument(solidImage(300, 300, 0xFFFFFFFF), *file.document); QVERIFY(rendered.image);
    QCOMPARE(serializeDocument(*file.document), serializeDocument(d));
    QSet<QUuid> everything; for (const auto &a : d.annotations) everything.insert(annotationId(a));
    const auto transferred = selectedAnnotationTransfer(d, everything, false); QVERIFY(!transferred.ok());  // unknown types are never transferred
  }

  // SCENARIOS.feature, scenario "A reverse arrow drag preserves direction".
  void scenarioReverseArrowDragKeepsTheHeadAtTheDragEnd() {
    QImage source(800, 600, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(document(800, 600)); EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom();
    QCOMPARE(canvas.viewState().zoom(), 1.0); canvas.setTool(EditorCanvas::Tool::Arrow);
    const auto view = [&](double x, double y) { return canvas.viewState().toView({x, y}).toPoint(); };
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, view(500, 400));
    QTest::mouseMove(canvas.viewport(), view(300, 250)); QTest::mouseMove(canvas.viewport(), view(100, 150));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, view(100, 150));
    QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(session.undoCount(), 1);  // one undo operation
    const auto &arrow = std::get<RectangleAnnotation>(session.document().annotations[0]); QCOMPARE(arrow.type(), shape::arrow);
    const auto absolute = [&](const PointF &p) { return PointF{arrow.start.x + p.x * (arrow.end.x - arrow.start.x), arrow.start.y + p.y * (arrow.end.y - arrow.start.y)}; };
    const auto tail = absolute(arrow.content.points[0]), head = absolute(arrow.content.points[1]);
    QVERIFY(std::abs(tail.x - 500) <= 1 && std::abs(tail.y - 400) <= 1); QVERIFY(std::abs(head.x - 100) <= 1 && std::abs(head.y - 150) <= 1);
    // The exported pixels agree: the head (wider than the shaft) is at 100,150.
    const auto painted = [&](const ArgbImage &image, int cx, int cy) {
      int n = 0; for (int y = cy - 14; y <= cy + 14; ++y) for (int x = cx - 14; x <= cx + 14; ++x) if (x >= 0 && y >= 0 && x < 800 && y < 600 && image.at(x, y) != 0xFFFFFFFF) ++n;
      return n;
    };
    const auto exported = renderDocument(solidImage(800, 600, 0xFFFFFFFF), session.document()); QVERIFY(exported.image);
    QVERIFY(painted(*exported.image, 100, 150) > painted(*exported.image, 500, 400) + 40);
    QVERIFY(session.undo()); QVERIFY(session.document().annotations.empty());
  }
  // EA-020: the annotation package must not reach the network by itself. Everything below runs in-process
  // (create, edit, copy, paste, save, reload, render); the network stack must still be absent afterwards.
  void annotationWorkNeverLoadsTheNetworkStack() {
    EditorSession session(document(200, 150)); ShapeContent content; content.type = shape::text; content.text = "No telemetry";
    const auto id = session.createShape({{10, 10}, {120, 60}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    session.select({*id}); QVERIFY(session.duplicateSelection().changed);
    const auto offer = selectedAnnotationTransfer(session.document(), session.selection(), true); QVERIFY(offer.ok());
    const auto incoming = parseAnnotationClipboard(offer.bytes); QVERIFY(incoming.ok());
    QVERIFY(session.insertAnnotations(incoming.annotations, session.stateId(), 10, QUuid::createUuid, incoming.assets).changed);
    const auto bytes = writeXann(session.document()); QVERIFY(bytes); QVERIFY(readXann(*bytes).document);
    QVERIFY(renderDocument(solidImage(200, 150, 0xFFFFFFFF), session.document()).image);
    QFile maps("/proc/self/maps"); QVERIFY(maps.open(QIODevice::ReadOnly)); const auto text = maps.readAll();
    for (const char *library : {"libQt6Network", "libcurl", "libssl"}) QVERIFY2(!text.contains(library), library);
  }
  // Scenario "Cancelling text input makes no mutation".
  void scenarioCancellingTextInputCreatesNothingAndKeepsHistory() {
    QImage source(800, 600, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(document(800, 600)); QVERIFY(session.createRectangle({10, 10}, {40, 40})); const auto before = serializeDocument(session.document()); const auto undo = session.undoCount();
    EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Text);
    for (const bool escape : {false, true}) {
      const auto view = [&](double x, double y) { return canvas.viewState().toView({x, y}).toPoint(); };
      QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, view(200, 200)); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, view(420, 300));
      auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog); QVERIFY(canvas.interactionActive());  // an in-progress, empty editor
      if (escape) QTest::keyClick(dialog, Qt::Key_Escape); else dialog->reject();
      QTRY_VERIFY(!canvas.interactionActive());
      QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QVERIFY(!session.dirty() || session.undoCount() == undo);
  }
  // Scenario "Effect annotation samples immutable source": a pixelate that overlaps its own old place while it moves.
  void scenarioOverlappingPixelateMovesWithoutAccumulatingItsOwnPixels() {
    QImage source(400, 300, QImage::Format_ARGB32);
    for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) source.setPixel(x, y, ((x / 3) + (y / 5)) % 2 ? qRgb(240, 30, 30) : qRgb(20, 20, 220));
    ArgbImage pixels; pixels.width = 400; pixels.height = 300; pixels.pixels.resize(400 * 300);
    for (int y = 0; y < 300; ++y) for (int x = 0; x < 400; ++x) pixels.pixels[std::size_t(y * 400 + x)] = source.pixel(x, y);
    EditorSession session(document(400, 300)); ShapeContent content; content.type = shape::pixelate; content.strength = 12;
    const auto id = session.createShape({{100, 80}, {260, 200}, {0xFFFF0000, 0, 2, 0, 1}}, content, session.stateId(), session.selection()); QVERIFY(id);
    session.select({*id}); const auto original = renderDocument(pixels, session.document()); QVERIFY(original.image);
    EditorCanvas canvas(session, source); canvas.resize(600, 450); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Select);
    const auto view = [&](double x, double y) { return canvas.viewState().toView({x, y}).toPoint(); };
    const auto undo = session.undoCount(); const auto before = serializeDocument(session.document());
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, view(180, 140)); QVERIFY(canvas.interactionActive());
    QTest::mouseMove(canvas.viewport(), view(200, 150)); QTest::mouseMove(canvas.viewport(), view(220, 170));  // +40,+30: overlaps the old region
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);  // the preview mutates nothing
    const auto preview = canvas.viewport()->grab().toImage();
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, view(220, 170));
    QCOMPARE(session.undoCount(), undo + 1);
    const auto &moved = std::get<RectangleAnnotation>(session.document().annotations[0]); QCOMPARE(moved.start.x, 140.0); QCOMPARE(moved.start.y, 110.0);
    const auto committed = renderDocument(pixels, session.document()); QVERIFY(committed.image);
    // The result equals a fresh render of the final geometry from the source only...
    auto fresh = session.document(); auto &freshShape = std::get<RectangleAnnotation>(fresh.annotations[0]); freshShape.start = {140, 110}; freshShape.end = {300, 230};
    const auto expected = renderDocument(pixels, fresh); QVERIFY(expected.image); QVERIFY(same(*committed.image, *expected.image, 0, 0, 400, 300));
    // ...and rendering is idempotent: nothing in the output feeds back into a second export.
    const auto again = renderDocument(pixels, session.document()); QVERIFY(again.image); QVERIFY(same(*again.image, *committed.image, 0, 0, 400, 300));
    // The preview already showed those pixels: sample the interior of the new region on both.
    const auto canvasPreview = preview; int compared = 0;
    for (int y = 125; y < 215; y += 15) for (int x = 160; x < 285; x += 15) {
      const QColor shown = canvasPreview.pixelColor(view(x, y)); const Argb want = committed.image->at(x, y);
      if (qAbs(shown.red() - qRed(want)) <= 2 && qAbs(shown.green() - qGreen(want)) <= 2 && qAbs(shown.blue() - qBlue(want)) <= 2) ++compared;
    }
    QVERIFY2(compared >= 40, qPrintable(QString::number(compared)));
    // Moving it back restores the original export exactly: no pixel was accumulated by the round trip.
    QVERIFY(session.undo()); const auto restored = renderDocument(pixels, session.document()); QVERIFY(restored.image); QVERIFY(same(*restored.image, *original.image, 0, 0, 400, 300));
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
                             shape::blur, shape::pixelate, shape::speechBalloon, shape::magnify})
      QTest::newRow(qPrintable(type)) << type;
  }
  void exportedShapesChangeOnlyTheirOwnArea() {
    QFETCH(QString, type);
    const ArgbImage source = type == shape::blur || type == shape::pixelate || type == shape::magnify ? stripes(120, 80) : solidImage(120, 80, 0xFFFFFFFF);
    auto shape = shapeOf(type, {40, 20}, {80, 60}, isPathShape(type) ? std::vector<PointF>{{0, 0}, {0.5, 1}, {1, 0}} : std::vector<PointF>{});
    if (type == shape::text || type == shape::speechBalloon) shape.content.text = "XerahS";
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

  void spotlightDimsOnlyOutsideItsEllipse() {
    const ArgbImage source = solidImage(100, 60, 0xFFFFFFFF);
    const auto result = render(source, {shapeOf(shape::spotlight, {30, 10}, {70, 50})});
    QVERIFY(result.image);
    QCOMPARE(result.image->at(50, 30), Argb(0xFFFFFFFF));
    QVERIFY(qRed(result.image->at(2, 2)) < 120);
  }

  void smartEraserFillsWithTheSurroundingColour() {
    ArgbImage source = solidImage(60, 40, 0xFF2040A0);
    for (qint64 y = 15; y < 25; ++y) for (qint64 x = 20; x < 40; ++x) source.pixels[std::size_t(y * 60 + x)] = 0xFFFF0000;
    const auto result = render(source, {shapeOf(shape::smartEraser, {20, 15}, {40, 25})});
    QVERIFY(result.image);
    QCOMPARE(result.image->at(30, 20), Argb(0xFF2040A0));
  }

  void embeddedAssetsRenderWithTheirAspectRatioOrAPlaceholder() {
    QImage green(20, 10, QImage::Format_ARGB32); green.fill(QColor(0, 200, 0));
    QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(green.save(&buffer, "PNG"));
    auto d = document(100, 100);
    auto image = shapeOf(shape::image, {0, 0}, {40, 40}); image.content.imageKey = "asset-1";
    d.embeddedImages = {{"asset-1", png}}; d.annotations = {image};
    const auto parsed = parseDocument(serializeDocument(d));
    QVERIFY(parsed.document);
    QCOMPARE(std::get<RectangleAnnotation>(parsed.document->annotations[0]).content.imageKey, QString("asset-1"));
    const auto result = renderDocument(solidImage(100, 100, 0xFFFFFFFF), d);
    QVERIFY(result.image);
    QCOMPARE(result.image->at(20, 20), Argb(0xFF00C800));  // centred 40x20 band
    QCOMPARE(result.image->at(20, 5), Argb(0xFFFFFFFF));   // letterboxed, aspect kept
    auto missing = image; missing.content.imageKey = "absent";
    const auto placeholder = render(solidImage(100, 100, 0xFFFFFFFF), {missing});
    QVERIFY(placeholder.image); QVERIFY(!same(*placeholder.image, solidImage(100, 100, 0xFFFFFFFF), 0, 0, 40, 40));
  }

  void cutOutRemovesTheStripAndClosesTheGap() {
    ArgbImage source = solidImage(10, 4, 0xFF000000);
    for (qint64 y = 0; y < 4; ++y) for (qint64 x = 0; x < 10; ++x) source.pixels[std::size_t(y * 10 + x)] = 0xFF000000 | Argb(x);
    auto d = document(10, 4);
    const auto left = shapeOf(shape::ellipse, {0, 0}, {2, 2});
    const auto right = shapeOf(shape::ellipse, {7, 0}, {9, 2});
    d.annotations = {left, right};
    CanvasOperation op; op.action = CanvasAction::CutOut; op.start = {3, 1}; op.end = {6, 2}; op.cutColumns = true;
    const auto result = applyCanvasOperation(d, source, op);
    QVERIFY(result.image && result.document);
    QCOMPARE(result.image->width, qint64(7)); QCOMPARE(result.document->canvasWidth, qint64(7));
    QCOMPARE(result.image->at(2, 0), Argb(0xFF000002)); QCOMPARE(result.image->at(3, 0), Argb(0xFF000006));
    QCOMPARE(std::get<RectangleAnnotation>(result.document->annotations[0]).start.x, 0.0);
    QCOMPARE(std::get<RectangleAnnotation>(result.document->annotations[1]).start.x, 4.0);
    op.start = {0, 0}; op.end = {10, 0};  // removing everything is rejected
    QVERIFY(applyCanvasOperation(d, source, op).error);
  }

  void cutOutToolRequestsTheDraggedStrip() {
    QImage source(400, 300, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorSession session(document(400, 300));
    EditorCanvas canvas(session, source); canvas.resize(600, 450); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    canvas.setTool(EditorCanvas::Tool::CutOut);
    QSignalSpy requested(&canvas, &EditorCanvas::cutOutRequested);
    const QPoint press = canvas.viewState().toView({100, 80}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, press);
    QTest::mouseMove(canvas.viewport(), press + QPoint(60, 10));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, press + QPoint(60, 10));
    QCOMPARE(requested.count(), 1); QVERIFY(requested[0][2].toBool());
    QVERIFY(session.document().annotations.empty());
  }

  void flippingMirrorsPathsInsteadOfTurningThemOver() {
    auto d = document(100, 100);
    d.annotations = {shapeOf(shape::arrow, {10, 10}, {50, 30}, {{0, 0}, {1, 1}})};
    CanvasOperation op; op.action = CanvasAction::FlipHorizontal;
    const auto result = applyCanvasOperation(d, solidImage(100, 100, 0xFFFFFFFF), op);
    QVERIFY(result.document);
    const auto &arrow = std::get<RectangleAnnotation>(result.document->annotations[0]);
    QCOMPARE(arrow.start.x, 50.0); QCOMPARE(arrow.style.rotationDegrees, 0.0);
    QCOMPARE(arrow.content.points[0].x, 1.0); QCOMPARE(arrow.content.points[1].x, 0.0); QCOMPARE(arrow.content.points[1].y, 1.0);
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
    QTest::newRow("spotlight") << int(EditorCanvas::Tool::Spotlight) << shape::spotlight;
    QTest::newRow("smart-eraser") << int(EditorCanvas::Tool::SmartEraser) << shape::smartEraser;
    QTest::newRow("cursor") << int(EditorCanvas::Tool::Cursor) << shape::cursor;
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
    if (type == shape::cursor) {
      QCOMPARE(session.document().embeddedImages.size(), std::size_t(1));
      QCOMPARE(session.document().embeddedImages[0].first, created.content.imageKey);
      QVERIFY(session.undo()); QVERIFY(session.document().embeddedImages.empty());  // one history step
    }
    QCOMPARE(canvas.tool(), EditorCanvas::Tool(tool));  // the tool stays active
  }
};

QTEST_MAIN(AnnotationShapesTest)
#include "AnnotationShapesTest.moc"
