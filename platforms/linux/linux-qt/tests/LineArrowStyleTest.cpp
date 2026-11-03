// EDITOR-ANNOTATIONS-001 EA-006: Line and Arrow ordered endpoints, optional
// curve, border style, thickness, arrow style, shadow, opacity and direction
// through move, resize, copy, serialization and rendering.
#include "EditorCanvas.h"
#include "RectanglePropertiesDialog.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/AnnotationTransfer.h"
#include "image-editor/EditorSession.h"
#include "image-editor/ShapePainter.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QPainter>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <limits>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
AnnotationDocument document(qint64 w = 200, qint64 h = 150) {
  AnnotationDocument d;
  d.canvasWidth = w; d.canvasHeight = h;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-11-03T00:00:00Z");
  d.sourceImagePng = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  return d;
}
// A Line or Arrow dragged from `a` to `b`, built the way the canvas builds it.
RectangleAnnotation make(const QString &type, PointF a, PointF b, double width = 4) {
  const auto geometry = shapeGeometry(type, {a, b});
  RectangleAnnotation r; r.id = QUuid::createUuid(); r.content.type = type;
  r.start = geometry->start; r.end = geometry->end; r.content.points = geometry->points;
  r.style = {0xFF000000, 0x00000000, width, 0, 1};
  return r;
}
RectangleAnnotation curved(const QString &type, PointF a, PointF b, PointF anchor) {
  auto r = make(type, a, b); [&] { QVERIFY(setLineCurve(r, anchor)); }(); return r;
}
RectangleStyle decorated(const QString &type) {
  RectangleStyle s{0xFF112233, 0x00000000, 5, 0, 0.75};
  s.borderStyle = BorderStyle::DashDot; s.shadow = {true, 3, 6, -2.5, 0.625, 0xFF102030};
  if (type == shape::arrow) s.arrowStyle = ArrowStyle::Double;
  return s;
}
ArgbImage render(const RectangleAnnotation &r, qint64 w = 100, qint64 h = 80, Argb bg = 0xFFFFFFFF) {
  const auto result = render(solidImage(w, h, bg), {Annotation(r)}, {});
  return result.image ? *result.image : ArgbImage{};
}
bool dark(const ArgbImage &img, qint64 x, qint64 y) { return (img.at(x, y) & 0xFF) < 0x80; }
int darkCount(const ArgbImage &img, qint64 x0, qint64 x1, qint64 y0, qint64 y1) {
  int n = 0; for (qint64 y = y0; y < y1; ++y) for (qint64 x = x0; x < x1; ++x) n += dark(img, x, y); return n;
}
}  // namespace

class LineArrowStyleTest : public QObject {
  Q_OBJECT
private slots:
  void fieldsRoundTripForLineAndArrow_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("line") << shape::line; QTest::newRow("arrow") << shape::arrow;
  }
  void fieldsRoundTripForLineAndArrow() {
    QFETCH(QString, type);
    auto d = document(); auto r = curved(type, {10, 90}, {120, 30}, {40, 20}); r.style = decorated(type);
    d.annotations = {r};
    const auto saved = serializeDocument(d);
    const auto a = saved["annotations"].toArray()[0].toObject();
    QCOMPARE(a["borderStyle"].toString(), QString("dashDot")); QCOMPARE(a["shadowEnabled"].toBool(), true);
    QCOMPARE(a["shadowBlurRadius"].toDouble(), 3.0); QCOMPARE(a["shadowOffsetX"].toDouble(), 6.0);
    QCOMPARE(a["shadowOffsetY"].toDouble(), -2.5); QCOMPARE(a["shadowOpacity"].toDouble(), 0.625);
    QCOMPARE(a["shadowColor"].toString(), QString("#FF102030")); QCOMPARE(a["opacity"].toDouble(), 0.75);
    QCOMPARE(a["strokeWidth"].toDouble(), 5.0);
    QCOMPARE(a.contains("style"), type == shape::arrow);
    if (type == shape::arrow) QCOMPARE(a["style"].toString(), QString("double"));
    QVERIFY(!a.contains("cornerRadius")); QCOMPARE(a["points"].toArray().size(), 3);
    const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
    const auto &back = std::get<RectangleAnnotation>(loaded.document->annotations[0]);
    QVERIFY(sameDecoration(back.style, r.style));
    QCOMPARE(back.content.points, r.content.points); QCOMPARE(back.start, r.start); QCOMPARE(back.end, r.end);
    QCOMPARE(back.style.opacity, 0.75); QCOMPARE(back.style.strokeWidth, 5.0);
    QCOMPARE(serializeDocument(*loaded.document), saved);
    const auto bytes = writeXann(d); QVERIFY(bytes); const auto viaXann = readXann(*bytes); QVERIFY(viaXann.document);
    QCOMPARE(std::get<RectangleAnnotation>(viaXann.document->annotations[0]).content.points, r.content.points);
  }

  void everyArrowStyleNameRoundTripsAndReadsCaseInsensitively() {
    for (const auto style : {ArrowStyle::Classic, ArrowStyle::Modern, ArrowStyle::Double, ArrowStyle::Basic, ArrowStyle::Line}) {
      QCOMPARE(parseArrowStyle(arrowStyleName(style)), std::optional<ArrowStyle>(style));
      QCOMPARE(parseArrowStyle(arrowStyleName(style).toUpper()), std::optional<ArrowStyle>(style));
      auto r = make(shape::arrow, {10, 10}, {60, 40}); r.style.arrowStyle = style;
      const auto list = parseAnnotationList(serializeAnnotationList({r})); QVERIFY(list.annotations);
      QCOMPARE(std::get<RectangleAnnotation>((*list.annotations)[0]).style.arrowStyle, style);
    }
    QVERIFY(!parseArrowStyle("fancy"));
    QCOMPARE(arrowStyleName(ArrowStyle::Classic), QString("classic")); QCOMPARE(ArrowStyle{}, ArrowStyle::Classic);
  }

  void olderDocumentsLoadWithDefaultsAndWriteNoNewFields() {
    auto d = document(); d.annotations = {make(shape::line, {10, 10}, {90, 60}), make(shape::arrow, {10, 70}, {90, 20})};
    const auto saved = serializeDocument(d);
    for (const auto &item : saved["annotations"].toArray()) {
      for (const auto &key : {"borderStyle", "style", "cornerRadius", "shadowEnabled", "shadowBlurRadius", "shadowColor", "shadowOffsetX",
                              "shadowOffsetY", "shadowOpacity", "curvePoint", "curvePointActivated"})
        QVERIFY2(!item.toObject().contains(key), key);
      QCOMPARE(item.toObject()["points"].toArray().size(), 2);
    }
    const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
    for (const auto &a : loaded.document->annotations) {
      const auto &r = std::get<RectangleAnnotation>(a);
      QCOMPARE(r.style.borderStyle, BorderStyle::Solid); QCOMPARE(r.style.arrowStyle, ArrowStyle::Classic);
      QVERIFY(!r.style.shadow.enabled); QVERIFY(!lineHasCurve(r)); QVERIFY(!hasDecoration(r.type(), r.style));
    }
    QCOMPARE(serializeDocument(*loaded.document), saved);  // byte-identical at default
    // A document written before EA-006 (two points, plain keys) is untouched by it.
    const auto old = parseAnnotationList(saved["annotations"].toArray()); QVERIFY(old.annotations);
    QCOMPARE(serializeAnnotationList(*old.annotations), saved["annotations"].toArray());
  }

  void outOfRangeAndNonFiniteValuesAreRejected_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<QString>("key"); QTest::addColumn<QJsonValue>("value");
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto &type : {shape::line, shape::arrow}) {
      const auto row = [&](const char *name, const char *key, QJsonValue v) { QTest::newRow(qPrintable(type + "-" + name)) << type << QString(key) << v; };
      row("border-name", "borderStyle", "dashed"); row("border-number", "borderStyle", 2);
      row("shadow-string", "shadowEnabled", "yes"); row("blur-negative", "shadowBlurRadius", -1); row("blur-inf", "shadowBlurRadius", inf);
      row("offset-huge", "shadowOffsetX", 10001); row("opacity-high", "shadowOpacity", 1.5); row("color-bad", "shadowColor", "#123456");
      row("curve-flag-string", "curvePointActivated", "true");
      row("curve-point-shape", "curvePoint", QJsonObject{{"x", 1}});
      row("curve-point-nonnumber", "curvePoint", QJsonObject{{"x", "a"}, {"y", 2}});
    }
    QTest::newRow("arrow-style-name") << shape::arrow << QString("style") << QJsonValue("fancy");
    QTest::newRow("arrow-style-number") << shape::arrow << QString("style") << QJsonValue(3);
  }
  void outOfRangeAndNonFiniteValuesAreRejected() {
    QFETCH(QString, type); QFETCH(QString, key); QFETCH(QJsonValue, value);
    auto o = serializeAnnotationList({make(type, {1, 1}, {9, 9})})[0].toObject();
    if (key.startsWith("curve")) o.insert("curvePointActivated", true), o.insert("curvePoint", QJsonObject{{"x", 5}, {"y", 5}});
    o.insert(key, value);
    const auto parsed = parseAnnotationList(QJsonArray{o});
    QVERIFY2(!parsed.annotations, "must reject"); QCOMPARE(parsed.diagnostic, diagnostic::documentInvalid);
  }
  void curveAnchorOutOfRangeIsRejected() {
    for (const double bad : {2e6, -2e6}) {
      auto o = serializeAnnotationList({curved(shape::line, {10, 10}, {90, 60}, {50, 10})})[0].toObject();
      auto points = o["points"].toArray(); points[2] = QJsonObject{{"x", bad}, {"y", 0.5}}; o["points"] = points;
      QVERIFY(!parseAnnotationList(QJsonArray{o}).annotations);
    }
    auto o = serializeAnnotationList({make(shape::line, {10, 10}, {90, 60})})[0].toObject();
    o.insert("curvePointActivated", true); o.insert("curvePoint", QJsonObject{{"x", 5e9}, {"y", 5}});
    QVERIFY(!parseAnnotationList(QJsonArray{o}).annotations);  // 5e9 px over an 80 px box exceeds the bound
    // The boundary is accepted.
    auto edge = serializeAnnotationList({curved(shape::line, {10, 10}, {90, 60}, {50, 10})})[0].toObject();
    auto pts = edge["points"].toArray(); pts[2] = QJsonObject{{"x", 1000000}, {"y", -1000000}}; edge["points"] = pts;
    QVERIFY(parseAnnotationList(QJsonArray{edge}).annotations);
  }

  void referenceStyleXannJsonLoadsAndRoundTrips() {
    // Hand-written in the shape the reference XerahS (camelCase JSON) emits: enum
    // names in any case, curvePoint in document pixels plus curvePointActivated.
    const auto make = [&](const QString &type, const QString &style, bool activated) {
      QJsonObject o{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"type", type}, {"zIndex", 0}, {"visible", true}, {"opacity", 0.9},
          {"start", QJsonObject{{"x", 10}, {"y", 20}}}, {"end", QJsonObject{{"x", 110}, {"y", 80}}},
          {"points", QJsonArray{QJsonObject{{"x", 0}, {"y", 1}}, QJsonObject{{"x", 1}, {"y", 0}}}},
          {"strokeColor", "#FF112233"}, {"fillColor", "#00000000"}, {"strokeWidth", 3}, {"rotationDegrees", 0},
          {"borderStyle", "Dash"}, {"shadowEnabled", true}, {"shadowColor", "#80000000"}, {"shadowBlurRadius", 4}, {"shadowOpacity", 1},
          {"shadowOffsetX", 3}, {"shadowOffsetY", 3}, {"curvePoint", QJsonObject{{"x", 30}, {"y", 30}}}, {"curvePointActivated", activated},
          {"vendor", "kept"}};
      if (!style.isEmpty()) o.insert("style", style);
      return o;
    };
    const auto list = parseAnnotationList(QJsonArray{make("line", {}, true), make("arrow", "Double", true), make("arrow", "basic", false)});
    QVERIFY(list.annotations);
    const auto &line = std::get<RectangleAnnotation>((*list.annotations)[0]);
    QCOMPARE(line.style.borderStyle, BorderStyle::Dash); QCOMPARE(line.style.shadow, (ShapeShadow{true, 4, 3, 3, 1, 0x80000000}));
    QVERIFY(lineHasCurve(line)); QCOMPARE(lineAnchor(line)->x, 30.0); QCOMPARE(lineAnchor(line)->y, 30.0);
    QCOMPARE(lineStart(line).x, 10.0); QCOMPARE(lineStart(line).y, 80.0); QCOMPARE(lineEnd(line).x, 110.0);  // direction kept
    const auto &doubleArrow = std::get<RectangleAnnotation>((*list.annotations)[1]);
    QCOMPARE(doubleArrow.style.arrowStyle, ArrowStyle::Double); QVERIFY(lineHasCurve(doubleArrow));
    const auto &basic = std::get<RectangleAnnotation>((*list.annotations)[2]);
    QCOMPARE(basic.style.arrowStyle, ArrowStyle::Basic); QVERIFY(!lineHasCurve(basic));  // curve not activated
    const auto out = serializeAnnotationList(*list.annotations);
    const auto l = out[0].toObject(), a = out[1].toObject(), b = out[2].toObject();
    QCOMPARE(l["points"].toArray().size(), 3); QVERIFY(!l.contains("curvePoint")); QVERIFY(!l.contains("curvePointActivated"));
    QCOMPARE(l["vendor"].toString(), QString("kept")); QVERIFY(!l.contains("style"));  // a Line has no arrow style
    QCOMPARE(a["style"].toString(), QString("double")); QCOMPARE(a["borderStyle"].toString(), QString("dash"));
    QCOMPARE(b["points"].toArray().size(), 2); QVERIFY(b.contains("curvePoint")); QCOMPARE(b["curvePointActivated"].toBool(), false);  // inactive: kept verbatim
    const auto again = parseAnnotationList(out); QVERIFY(again.annotations);
    QCOMPARE(serializeAnnotationList(*again.annotations), out);
    QCOMPARE(ShapeShadow{}, (ShapeShadow{false, 4, 3, 3, 1, 0x80000000}));
  }

  void dashedLineAndArrowShaftPaintGapsAndSolidDoesNot() {
    for (const auto &type : {shape::line, shape::arrow}) {
      auto r = make(type, {10, 20}, {90, 20}, 4); r.style.arrowStyle = ArrowStyle::Line;  // open head keeps the shaft visible
      const auto gaps = [&](const ArgbImage &img) { int n = 0; for (qint64 x = 14; x < 60; ++x) n += !dark(img, x, 20); return n; };
      QCOMPARE(gaps(render(r)), 0);
      for (const auto style : {BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot}) {
        r.style.borderStyle = style; QVERIFY2(gaps(render(r)) > 4, qPrintable(type + borderStyleName(style)));
      }
      r.style.borderStyle = BorderStyle::Dash; const auto dash = render(r); r.style.borderStyle = BorderStyle::Dot;
      QVERIFY(dash.pixels != render(r).pixels);
    }
    // The head stays solid under a dashed style.
    auto arrow = make(shape::arrow, {10, 20}, {90, 20}, 4); arrow.style.borderStyle = BorderStyle::Dot; const auto img = render(arrow);
    QVERIFY(darkCount(img, 75, 90, 15, 26) > 60);
  }

  void thicknessScalesTheStroke() {
    auto thin = make(shape::line, {10, 40}, {90, 40}, 2), thick = make(shape::line, {10, 40}, {90, 40}, 10);
    QVERIFY(darkCount(render(thick), 40, 41, 0, 80) >= 9); QVERIFY(darkCount(render(thin), 40, 41, 0, 80) <= 3);
  }

  void arrowHeadShapeDiffersPerStyle() {
    const auto draw = [](ArrowStyle style) { auto r = make(shape::arrow, {10, 40}, {90, 40}, 4); r.style.arrowStyle = style; return render(r); };
    const auto classic = draw(ArrowStyle::Classic), modern = draw(ArrowStyle::Modern), dbl = draw(ArrowStyle::Double),
        basic = draw(ArrowStyle::Basic), line = draw(ArrowStyle::Line);
    for (const auto *img : {&classic, &modern, &dbl, &basic, &line}) QVERIFY(dark(*img, 90 - 1, 40) || dark(*img, 88, 40));  // tip at the end point
    // Classic and Double: head half width 8, so |dy| = 6 is filled 8 px behind the tip, never by the 4 px shaft.
    QVERIFY(dark(classic, 70, 46)); QVERIFY(!dark(classic, 30, 46));
    // Double adds the same head at the start (tip at x=10, base at x=34).
    QVERIFY(dark(dbl, 30, 46)); QVERIFY(!dark(classic, 30, 46)); QVERIFY(dark(dbl, 70, 46));
    // Modern tapers: thin near the start, the shaft grows toward the head.
    QVERIFY(!dark(modern, 16, 43)); QVERIFY(dark(modern, 55, 43)); QVERIFY(!dark(classic, 55, 44));
    // Basic is a filled triangle; Line is an open chevron: its inside is empty.
    QVERIFY(darkCount(basic, 69, 91, 30, 51) > darkCount(line, 69, 91, 30, 51));
    QVERIFY(dark(basic, 82, 44));
    // All five are distinct pictures.
    const std::vector<const ArgbImage *> all{&classic, &modern, &dbl, &basic, &line};
    for (std::size_t i = 0; i < all.size(); ++i) for (std::size_t j = i + 1; j < all.size(); ++j) QVERIFY(all[i]->pixels != all[j]->pixels);
  }

  void headFollowsDirectionOfAReverseDrag() {
    auto r = make(shape::arrow, {90, 40}, {10, 40}, 4);  // dragged right to left
    QCOMPARE(r.content.points[0].x, 1.0); QCOMPARE(r.content.points[1].x, 0.0);
    const auto img = render(r);
    QVERIFY(dark(img, 14, 40)); QVERIFY(dark(img, 28, 46)); QVERIFY(!dark(img, 84, 46));  // head on the left, plain shaft on the right
  }

  void curvePassesThroughTheAnchorAndBulgesTowardIt() {
    auto r = make(shape::line, {10, 60}, {90, 60}, 3);
    const auto straight = render(r, 100, 100);
    QVERIFY(!dark(straight, 50, 30)); QVERIFY(dark(straight, 50, 60));
    QVERIFY(setLineCurve(r, PointF{50, 30}));
    QVERIFY(lineHasCurve(r)); QCOMPARE(r.content.points.size(), size_t(3));
    // Endpoints keep their document positions; the box grows to hold the curve exactly.
    QVERIFY(std::abs(lineStart(r).x - 10) < 1e-9 && std::abs(lineStart(r).y - 60) < 1e-9);
    QVERIFY(std::abs(lineEnd(r).x - 90) < 1e-9 && std::abs(lineEnd(r).y - 60) < 1e-9);
    QVERIFY(std::abs(lineAnchor(r)->x - 50) < 1e-9 && std::abs(lineAnchor(r)->y - 30) < 1e-9);
    QVERIFY(std::abs(r.top() - 30) < 1e-9); QVERIFY(std::abs(r.bottom() - 60) < 1e-9 || r.bottom() >= 60);
    const auto bent = render(r, 100, 100);
    QVERIFY(dark(bent, 50, 30)); QVERIFY(!dark(bent, 50, 60)); QVERIFY(dark(bent, 10, 60)); QVERIFY(dark(bent, 90, 60));
    // Removing the curve restores the straight line exactly and the tight box.
    QVERIFY(setLineCurve(r, std::nullopt)); QVERIFY(!lineHasCurve(r)); QCOMPARE(r.content.points.size(), size_t(2));
    QCOMPARE(render(r, 100, 100).pixels, straight.pixels);
  }

  void curvedArrowHeadFollowsTheCurveAndMirrorsAreSymmetric() {
    auto up = curved(shape::arrow, {10, 60}, {90, 60}, {50, 20}), down = curved(shape::arrow, {10, 60}, {90, 60}, {50, 100});
    auto flat = make(shape::arrow, {10, 60}, {90, 60});
    const auto a = render(up, 100, 120), b = render(down, 100, 120), c = render(flat, 100, 120);
    QVERIFY(a.pixels != c.pixels);
    int differing = 0;
    for (qint64 y = 0; y < 120; ++y) for (qint64 x = 0; x < 100; ++x) differing += dark(a, x, y) != dark(b, x, 119 - y);
    QVERIFY2(differing < 40, qPrintable(QString::number(differing)));  // vertical mirror (anti-aliasing noise only)
    // The end tangent is steep (down-right), so the cap hugs the end point from above, not from the left.
    QVERIFY(darkCount(a, 84, 100, 40, 60) > darkCount(c, 84, 100, 40, 60));
  }

  void modernArrowStaysStraightEvenWithAStoredAnchor() {
    auto r = curved(shape::arrow, {10, 60}, {90, 60}, {50, 20});
    r.style.arrowStyle = ArrowStyle::Modern;
    QVERIFY(!lineHasCurve(r)); QVERIFY(!lineAnchor(r));
    auto straight = make(shape::arrow, {10, 60}, {90, 60}); straight.style.arrowStyle = ArrowStyle::Modern;
    // The stored anchor is preserved in the document but not drawn.
    QCOMPARE(r.content.points.size(), size_t(3));
    const auto list = parseAnnotationList(serializeAnnotationList({r})); QVERIFY(list.annotations);
    QCOMPARE(std::get<RectangleAnnotation>((*list.annotations)[0]).content.points, r.content.points);
    QVERIFY(darkCount(render(r, 100, 120), 0, 100, 0, 40) == 0);
  }

  void shadowOffsetAndSilhouetteIncludeTheHead() {
    auto line = make(shape::line, {20, 20}, {80, 20}, 4); line.style.shadow = {true, 0, 0, 10, 1.0, 0xFF0000FF};
    auto img = render(line);
    QCOMPARE(img.at(50, 20), Argb(0xFF000000)); QCOMPARE(img.at(50, 30), Argb(0xFF0000FF)); QCOMPARE(img.at(50, 10), Argb(0xFFFFFFFF));
    auto arrow = make(shape::arrow, {10, 40}, {70, 40}, 4); arrow.style.shadow = {true, 0, 12, 0, 1.0, 0xFF0000FF};
    img = render(arrow);
    QCOMPARE(img.at(40, 40), Argb(0xFF000000));
    QCOMPARE(img.at(80, 40), Argb(0xFF0000FF));   // the shaft's shadow beyond the tip
    QCOMPARE(img.at(62, 47), Argb(0xFF0000FF));   // the head's shadow, below the shaft line
    arrow.style.shadow.enabled = false; QCOMPARE(render(arrow).at(80, 40), Argb(0xFFFFFFFF));
    // Blur softens and opacity scales it.
    arrow.style.shadow = {true, 6, 0, 10, 1.0, 0xFF000000}; img = render(arrow, 100, 90);
    bool partial = false; for (qint64 x = 20; x < 60; ++x) { const auto v = img.at(x, 52) & 0xFF; partial |= v > 0x10 && v < 0xF0; }
    QVERIFY(partial);
  }

  void opacityAppliesToLineAndArrow() {
    for (const auto &type : {shape::line, shape::arrow}) {
      auto r = make(type, {10, 40}, {90, 40}, 6); r.style.opacity = 0.5; r.style.strokeColor = 0xFF000000;
      const auto v = render(r).at(40, 40) & 0xFF; QVERIFY(v > 0x70 && v < 0x90);
    }
  }

  void canvasPaintEqualsExport() {
    auto line = curved(shape::line, {10, 60}, {90, 60}, {50, 20}); line.style = decorated(shape::line);
    auto arrow = curved(shape::arrow, {15, 15}, {85, 70}, {20, 60}); arrow.style = decorated(shape::arrow);
    for (const auto &r : {line, arrow}) {
      const auto img = render(r, 100, 100);
      QImage canvas(100, 100, QImage::Format_ARGB32_Premultiplied); canvas.fill(Qt::white);
      { QPainter p(&canvas); paintShape(p, r, QImage()); }
      for (int y = 0; y < 100; ++y) for (int x = 0; x < 100; ++x) QCOMPARE(canvas.pixel(x, y), QRgb(img.at(x, y)));
    }
  }

  void transferAndDuplicatePreserveEveryField() {
    auto d = document(); auto line = curved(shape::line, {10, 90}, {120, 30}, {40, 20}), arrow = curved(shape::arrow, {20, 20}, {130, 100}, {120, 20});
    line.style = decorated(shape::line); arrow.style = decorated(shape::arrow); d.annotations = {line, arrow};
    const auto projected = selectedAnnotationTransfer(d, {line.id, arrow.id}, true); QVERIFY(projected.ok());
    const auto parsed = parseAnnotationClipboard(projected.bytes); QVERIFY(parsed.ok());
    EditorSession session(d);
    const auto pasted = session.insertAnnotations(parsed.annotations, session.stateId()); QVERIFY(pasted.changed);
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), size_t(4));
    for (int i = 0; i < 2; ++i) {
      const auto &src = std::get<RectangleAnnotation>(session.document().annotations[std::size_t(i)]);
      const auto &copy = std::get<RectangleAnnotation>(session.document().annotations[std::size_t(i + 2)]);
      QVERIFY(copy.id != src.id); QVERIFY(sameDecoration(copy.style, src.style)); QCOMPARE(copy.content.points, src.content.points);
      QCOMPARE(copy.style.opacity, src.style.opacity); QCOMPARE(copy.style.strokeWidth, src.style.strokeWidth);
      QCOMPARE(copy.end.x - copy.start.x, src.end.x - src.start.x);  // same size, offset position
      QVERIFY(lineHasCurve(copy));
    }
    session.select({arrow.id}); QVERIFY(session.duplicateSelection().changed);
    const auto &dup = std::get<RectangleAnnotation>(session.document().annotations.back());
    QVERIFY(sameDecoration(dup.style, arrow.style)); QCOMPARE(dup.content.points, arrow.content.points);
  }

  void moveAndResizeKeepDirectionAndControlGeometry() {
    auto d = document(); auto r = curved(shape::arrow, {120, 20}, {20, 100}, {110, 90});  // right-to-left, bottom-left end
    r.style = decorated(shape::arrow); d.annotations = {r};
    EditorSession session(d); session.select({r.id});
    const auto before = lineAnchor(r).value(); const auto s0 = lineStart(r), e0 = lineEnd(r);
    QVERIFY(session.moveSelection(7, -3));
    auto moved = *session.selectedRectangle();
    QCOMPARE(moved.content.points, r.content.points);
    QVERIFY(std::abs(lineStart(moved).x - (s0.x + 7)) < 1e-9 && std::abs(lineEnd(moved).y - (e0.y - 3)) < 1e-9);
    QVERIFY(std::abs(lineAnchor(moved)->x - (before.x + 7)) < 1e-9 && std::abs(lineAnchor(moved)->y - (before.y - 3)) < 1e-9);
    QVERIFY(lineStart(moved).x > lineEnd(moved).x);  // still points right-to-left
    QVERIFY(session.resizeRectangle(r.id, RectangleHandle::BottomRight, {30, 20}, session.stateId()));
    const auto resized = *session.selectedRectangle();
    QCOMPARE(resized.content.points, r.content.points);  // fractions of the bounds: the curve scales with the box
    QVERIFY(resized.right() > moved.right() && resized.bottom() > moved.bottom());
    QVERIFY(lineStart(resized).x > lineEnd(resized).x); QVERIFY(lineStart(resized).y < lineEnd(resized).y);
    QVERIFY(sameDecoration(resized.style, r.style));
    QVERIFY(session.undo()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d));
  }

  void creationDefaultsCarryArrowStyleAndOnlyArrowsKeepIt() {
    EditorSession session(document());
    auto style = decorated(shape::arrow); style.arrowStyle = ArrowStyle::Line; QVERIFY(session.setCreationStyle(style));
    for (const auto &type : {shape::arrow, shape::line, shape::rectangle}) {
      ShapeContent c; c.type = type;
      if (type == shape::rectangle) { QVERIFY(session.createShape({{10, 10}, {60, 50}, session.toolStyle()}, c, session.stateId(), session.selection())); continue; }
      const auto g = shapeGeometry(type, {{10, 10}, {60, 50}}); c.points = g->points;
      QVERIFY(session.createShape({g->start, g->end, session.toolStyle()}, c, session.stateId(), session.selection()));
    }
    const auto &doc = session.document().annotations;
    QCOMPARE(std::get<RectangleAnnotation>(doc[0]).style.arrowStyle, ArrowStyle::Line);
    QCOMPARE(std::get<RectangleAnnotation>(doc[1]).style.arrowStyle, ArrowStyle::Classic);
    QCOMPARE(std::get<RectangleAnnotation>(doc[1]).style.borderStyle, BorderStyle::DashDot); QVERIFY(std::get<RectangleAnnotation>(doc[1]).style.shadow.enabled);
    QCOMPARE(std::get<RectangleAnnotation>(doc[2]).style.arrowStyle, ArrowStyle::Classic);
    auto later = style; later.arrowStyle = ArrowStyle::Basic;
    QVERIFY(session.editDecorationDefaults(later, session.stateId(), session.selection(), session.toolStyle()).accepted);
    QCOMPARE(session.toolStyle().arrowStyle, ArrowStyle::Basic);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.arrowStyle, ArrowStyle::Line);
  }

  void invalidCurveEditIsRejectedWithoutMutation() {
    auto r = make(shape::line, {10, 10}, {90, 60}); auto d = document(); d.annotations = {r};
    EditorSession session(d); session.select({r.id});
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto &bad : {std::vector<PointF>{{0, 0}, {1, 1}, {nan, 0}}, std::vector<PointF>{{0, 0}, {1, 1}, {2e6, 0}}, std::vector<PointF>{{0, 0}},
                            std::vector<PointF>{{0, 0}, {1, 1}, {0, 0}, {1, 1}}}) {
      RectangleProperties p{r.start, r.end, r.style}; p.points = bad;
      QVERIFY(!session.editRectangleProperties(r.id, p, state));
    }
    RectangleAnnotation rotated = r; rotated.style.rotationDegrees = 30;
    QVERIFY(setLineCurve(rotated, PointF{50, 5}));  // rotated shapes keep their box
    QCOMPARE(rotated.start, r.start); QCOMPARE(rotated.end, r.end);
    QVERIFY(!setLineCurve(rotated, PointF{nan, 1}));
    QVERIFY(!setLineCurve(r, PointF{1e308, 1e308}));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
  }

  void nativeEditIsOneUndoStepAndReloadPreservesFields_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("line") << shape::line; QTest::newRow("arrow") << shape::arrow;
  }
  void nativeEditIsOneUndoStepAndReloadPreservesFields() {
    QFETCH(QString, type);
    auto d = document(); auto r = make(type, {110, 70}, {30, 20}); d.annotations = {r};
    EditorSession session(d); session.select({r.id}); session.markSaved();
    QImage image(200, 150, QImage::Format_ARGB32); image.fill(Qt::white);
    EditorCanvas canvas(session, image); canvas.show(); canvas.setTool(EditorCanvas::Tool::Select);
    const auto before = serializeDocument(session.document());
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      auto *border = dialog->findChild<QComboBox *>("shapeBorderStyle"); QVERIFY(border); border->setCurrentIndex(border->findText("dashDotDot"));
      QVERIFY(!dialog->findChild<QLineEdit *>("shapeCornerRadius"));
      auto *arrowStyle = dialog->findChild<QComboBox *>("shapeArrowStyle"); QCOMPARE(arrowStyle != nullptr, type == shape::arrow);
      if (arrowStyle) arrowStyle->setCurrentIndex(arrowStyle->findText("basic"));
      dialog->findChild<QCheckBox *>("shapeCurve")->setChecked(true);
      dialog->findChild<QLineEdit *>("shapeCurveX")->setText("40"); dialog->findChild<QLineEdit *>("shapeCurveY")->setText("75");
      dialog->findChild<QCheckBox *>("shapeShadow")->setChecked(true);
      dialog->findChild<QLineEdit *>("shapeShadowBlur")->setText("5"); dialog->findChild<QLineEdit *>("shapeShadowOffsetX")->setText("7");
      dialog->findChild<QLineEdit *>("shapeShadowOffsetY")->setText("-3"); dialog->findChild<QLineEdit *>("shapeShadowOpacity")->setText("0.4");
      dialog->findChild<QLineEdit *>("shapeShadowColor")->setText("#FF334455");
      dialog->accept();
    });
    timer.start(5); const bool changed = canvas.editRectangleProperties(&canvas); timer.stop();
    QVERIFY(acted); QVERIFY(changed); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    const auto edited = *session.selectedRectangle();
    QCOMPARE(edited.style.borderStyle, BorderStyle::DashDotDot); QCOMPARE(edited.style.arrowStyle, type == shape::arrow ? ArrowStyle::Basic : ArrowStyle::Classic);
    QCOMPARE(edited.style.shadow, (ShapeShadow{true, 5, 7, -3, 0.4, 0xFF334455}));
    QVERIFY(lineHasCurve(edited)); QVERIFY(std::abs(lineAnchor(edited)->x - 40) < 1e-9 && std::abs(lineAnchor(edited)->y - 75) < 1e-9);
    QVERIFY(std::abs(lineStart(edited).x - 110) < 1e-9 && std::abs(lineEnd(edited).x - 30) < 1e-9);  // direction kept
    const auto saved = serializeDocument(session.document());
    const auto reloaded = parseDocument(saved); QVERIFY(reloaded.document);
    const auto &back = std::get<RectangleAnnotation>(reloaded.document->annotations[0]);
    QVERIFY(sameDecoration(back.style, edited.style)); QCOMPARE(back.content.points, edited.content.points);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), saved);
  }

  void choosingModernInTheDialogStraightensTheArrowInOneStep() {
    auto r = curved(shape::arrow, {110, 70}, {30, 20}, {40, 75}); auto d = document(); d.annotations = {r};
    EditorSession session(d); session.select({r.id});
    QImage image(200, 150, QImage::Format_ARGB32); image.fill(Qt::white);
    EditorCanvas canvas(session, image); canvas.show(); canvas.setTool(EditorCanvas::Tool::Select);
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      QVERIFY(dialog->findChild<QCheckBox *>("shapeCurve")->isChecked());
      auto *arrowStyle = dialog->findChild<QComboBox *>("shapeArrowStyle"); arrowStyle->setCurrentIndex(arrowStyle->findText("modern"));
      dialog->accept();
    });
    timer.start(5); QVERIFY(canvas.editRectangleProperties(&canvas)); timer.stop();
    QVERIFY(acted); QCOMPARE(session.undoCount(), 1);
    const auto after = *session.selectedRectangle();
    QCOMPARE(after.style.arrowStyle, ArrowStyle::Modern); QCOMPARE(after.content.points.size(), size_t(2));
    QVERIFY(std::abs(lineStart(after).x - 110) < 1e-9 && std::abs(lineEnd(after).y - 20) < 1e-9);
    QVERIFY(session.undo()); QVERIFY(lineHasCurve(*session.selectedRectangle()));
  }

  void defaultsDialogOffersArrowStyle() {
    RectangleStyle base; base.strokeWidth = 7;
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      auto *arrowStyle = dialog->findChild<QComboBox *>("shapeArrowStyle"); QVERIFY(arrowStyle); arrowStyle->setCurrentIndex(arrowStyle->findText("double"));
      QVERIFY(!dialog->findChild<QCheckBox *>("shapeCurve"));  // a curve belongs to one object, not to the defaults
      dialog->accept();
    });
    timer.start(5); const auto chosen = chooseDecorationDefaults(base, nullptr); timer.stop();
    QVERIFY(acted); QVERIFY(chosen); QCOMPARE(chosen->arrowStyle, ArrowStyle::Double); QCOMPARE(chosen->strokeWidth, 7.0);
  }
};

QTEST_MAIN(LineArrowStyleTest)
#include "LineArrowStyleTest.moc"
