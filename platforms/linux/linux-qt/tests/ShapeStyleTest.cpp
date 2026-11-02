// EDITOR-ANNOTATIONS-001 EA-005: Rectangle and Ellipse border style, corner
// radius (Rectangle only), shadow. Core round-trip, bounded validation,
// legacy defaults, export rendering, one-step native edits, and transfer.
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
#include <QPainter>
#include <QLineEdit>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <limits>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
AnnotationDocument document(qint64 w = 200, qint64 h = 150) {
  AnnotationDocument d;
  d.canvasWidth = w; d.canvasHeight = h;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-23T00:00:00Z");
  d.sourceImagePng = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");
  return d;
}
RectangleAnnotation make(const QString &type, PointF a, PointF b) {
  RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = a; r.end = b; r.content.type = type;
  r.style = {0xFF0000FF, 0x00000000, 3, 0, 1}; return r;
}
RectangleStyle decorated() {
  RectangleStyle s{0xFF112233, 0x80445566, 5, 12.5, 0.75};
  s.borderStyle = BorderStyle::DashDot; s.cornerRadius = 9;
  s.shadow = {true, 3, 6, -2.5, 0.625, 0xFF102030}; return s;
}
QJsonObject withDecorationKeys(const RectangleAnnotation &r, const QJsonObject &extra) {
  auto o = serializeAnnotationList({r})[0].toObject();
  for (auto it = extra.begin(); it != extra.end(); ++it) o.insert(it.key(), it.value());
  return o;
}
ArgbImage render(const RectangleAnnotation &r, qint64 w = 100, qint64 h = 80, Argb bg = 0xFFFFFFFF) {
  const auto result = render(solidImage(w, h, bg), {Annotation(r)}, {});
  return result.image ? *result.image : ArgbImage{};
}
}  // namespace

class ShapeStyleTest : public QObject {
  Q_OBJECT
private slots:
  void fieldsRoundTripForRectangleAndEllipse() {
    for (const auto &type : {shape::rectangle, shape::ellipse}) {
      auto d = document(); auto r = make(type, {10, 10}, {90, 60}); r.style = decorated();
      if (type == shape::ellipse) r.style.cornerRadius = 0;
      d.annotations = {r};
      const auto saved = serializeDocument(d);
      const auto a = saved["annotations"].toArray()[0].toObject();
      QCOMPARE(a["borderStyle"].toString(), QString("DashDot"));
      QCOMPARE(a.contains("cornerRadius"), type == shape::rectangle);
      QCOMPARE(a["shadow"].toBool(), true); QCOMPARE(a["shadowBlurRadius"].toDouble(), 3.0);
      QCOMPARE(a["shadowOffsetX"].toDouble(), 6.0); QCOMPARE(a["shadowOffsetY"].toDouble(), -2.5);
      QCOMPARE(a["shadowOpacity"].toDouble(), 0.625); QCOMPARE(a["shadowColor"].toString(), QString("#FF102030"));
      const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
      const auto &back = std::get<RectangleAnnotation>(loaded.document->annotations[0]);
      QVERIFY(sameDecoration(back.style, r.style));
      QCOMPARE(back.start, r.start); QCOMPARE(back.style.rotationDegrees, 12.5); QCOMPARE(back.style.opacity, 0.75);
      QCOMPARE(serializeDocument(*loaded.document), saved);
      // The gzip container path as well.
      const auto bytes = writeXann(d); QVERIFY(bytes); const auto viaXann = readXann(*bytes); QVERIFY(viaXann.document);
      QVERIFY(sameDecoration(std::get<RectangleAnnotation>(viaXann.document->annotations[0]).style, r.style));
    }
  }

  void everyBorderStyleNameRoundTrips() {
    for (const auto style : {BorderStyle::Solid, BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot}) {
      QCOMPARE(parseBorderStyle(borderStyleName(style)), std::optional<BorderStyle>(style));
      auto r = make(shape::rectangle, {1, 1}, {9, 9}); r.style.borderStyle = style;
      const auto list = parseAnnotationList(serializeAnnotationList({r})); QVERIFY(list.annotations);
      QCOMPARE(std::get<RectangleAnnotation>((*list.annotations)[0]).style.borderStyle, style);
    }
    QVERIFY(!parseBorderStyle("dash")); QVERIFY(!parseBorderStyle(""));
  }

  void olderDocumentsLoadWithDefaultsAndStaySerializedWithoutNewFields() {
    auto d = document(); d.annotations = {make(shape::rectangle, {10, 10}, {50, 50}), make(shape::ellipse, {5, 5}, {40, 30})};
    const auto saved = serializeDocument(d);
    for (const auto &item : saved["annotations"].toArray()) {
      for (const auto &key : {"borderStyle", "cornerRadius", "shadow", "shadowBlurRadius", "shadowColor", "shadowOffsetX", "shadowOffsetY", "shadowOpacity"})
        QVERIFY2(!item.toObject().contains(key), key);
    }
    const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
    for (const auto &a : loaded.document->annotations) {
      const auto &s = std::get<RectangleAnnotation>(a).style;
      QCOMPARE(s.borderStyle, BorderStyle::Solid); QCOMPARE(s.cornerRadius, 0.0); QVERIFY(!s.shadow.enabled);
      QVERIFY(!hasDecoration(std::get<RectangleAnnotation>(a).type(), s));
    }
  }

  void outOfRangeAndNonFiniteValuesAreRejected_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<QString>("key"); QTest::addColumn<QJsonValue>("value");
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto &type : {shape::rectangle, shape::ellipse}) {
      const auto row = [&](const char *name, const char *key, QJsonValue v) { QTest::newRow(qPrintable(type + "-" + name)) << type << QString(key) << v; };
      row("style-name", "borderStyle", "dashed"); row("style-number", "borderStyle", 2); row("style-lowercase", "borderStyle", "dash");
      row("shadow-string", "shadow", "yes"); row("blur-negative", "shadowBlurRadius", -1); row("blur-huge", "shadowBlurRadius", 1000.5);
      row("blur-inf", "shadowBlurRadius", inf); row("offset-huge", "shadowOffsetX", 10001); row("offset-neg-huge", "shadowOffsetY", -10001);
      row("opacity-high", "shadowOpacity", 1.5); row("opacity-neg", "shadowOpacity", -0.1); row("color-bad", "shadowColor", "#123456");
      row("color-number", "shadowColor", 5);
    }
    QTest::newRow("rectangle-corner-negative") << shape::rectangle << QString("cornerRadius") << QJsonValue(-1);
    QTest::newRow("rectangle-corner-huge") << shape::rectangle << QString("cornerRadius") << QJsonValue(10001);
    QTest::newRow("rectangle-corner-string") << shape::rectangle << QString("cornerRadius") << QJsonValue("4");
  }
  void outOfRangeAndNonFiniteValuesAreRejected() {
    QFETCH(QString, type); QFETCH(QString, key); QFETCH(QJsonValue, value);
    const auto json = QJsonArray{withDecorationKeys(make(type, {1, 1}, {9, 9}), {{key, value}})};
    const auto parsed = parseAnnotationList(json);
    QVERIFY2(!parsed.annotations, "must reject"); QCOMPARE(parsed.diagnostic, diagnostic::documentInvalid);
  }

  void validBoundaryValuesAreAccepted() {
    auto r = make(shape::rectangle, {1, 1}, {9, 9});
    const auto o = withDecorationKeys(r, {{"cornerRadius", 10000}, {"shadow", true}, {"shadowBlurRadius", 1000}, {"shadowOffsetX", -10000},
                                          {"shadowOffsetY", 10000}, {"shadowOpacity", 0}});
    QVERIFY(parseAnnotationList(QJsonArray{o}).annotations);
  }

  void ellipseKeepsAnUnknownCornerRadiusVerbatimAndRectangleKeepsOtherUnknownFields() {
    auto e = make(shape::ellipse, {1, 1}, {9, 9});
    const auto o = withDecorationKeys(e, {{"cornerRadius", "vendor-value"}, {"vendorField", 7}});
    const auto parsed = parseAnnotationList(QJsonArray{o}); QVERIFY(parsed.annotations);
    const auto &back = std::get<RectangleAnnotation>((*parsed.annotations)[0]);
    QCOMPARE(back.style.cornerRadius, 0.0);
    const auto again = serializeAnnotationList(*parsed.annotations)[0].toObject();
    QCOMPARE(again["cornerRadius"].toString(), QString("vendor-value")); QCOMPARE(again["vendorField"].toInt(), 7);
  }

  void disabledShadowKeepsItsParametersAcrossReload() {
    auto r = make(shape::rectangle, {1, 1}, {9, 9}); r.style.shadow = {false, 7, 1, 2, 0.25, 0x80FF0000};
    const auto list = parseAnnotationList(serializeAnnotationList({r})); QVERIFY(list.annotations);
    QCOMPARE(std::get<RectangleAnnotation>((*list.annotations)[0]).style.shadow, r.style.shadow);
  }

  void dashedBorderPaintsGapsAndSolidDoesNot() {
    auto r = make(shape::rectangle, {10, 10}, {90, 70}); r.style = {0xFF000000, 0x00000000, 4, 0, 1};
    const auto gapsOnTop = [&](const ArgbImage &img) {
      int gaps = 0;  // top edge centre line y=10: count white pixels along it
      for (qint64 x = 14; x < 86; ++x) if ((img.at(x, 10) >> 24) == 0xFF && (img.at(x, 10) & 0xFFFFFF) == 0xFFFFFF) ++gaps;
      return gaps;
    };
    const auto solid = render(r); QCOMPARE(gapsOnTop(solid), 0);
    for (const auto style : {BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot}) {
      r.style.borderStyle = style; const auto img = render(r);
      QVERIFY2(gapsOnTop(img) > 4, qPrintable(borderStyleName(style)));
      QVERIFY(img.at(11, 10) != 0xFFFFFFFF || img.at(13, 10) != 0xFFFFFFFF || img.at(12, 10) != 0xFFFFFFFF);
    }
    // Dot and Dash differ (distinct patterns).
    r.style.borderStyle = BorderStyle::Dash; const auto dash = render(r); r.style.borderStyle = BorderStyle::Dot; const auto dot = render(r);
    QVERIFY(dash.pixels != dot.pixels);
  }

  void ellipseBorderStyleRendersDashes() {
    auto e = make(shape::ellipse, {10, 10}, {90, 70}); e.style = {0xFF000000, 0x00000000, 4, 0, 1};
    const auto solid = render(e); e.style.borderStyle = BorderStyle::Dash; const auto dashed = render(e);
    QVERIFY(solid.pixels != dashed.pixels);
    int gapsSolid = 0, gapsDashed = 0;  // top of the ellipse, y = 10
    for (qint64 x = 40; x < 60; ++x) { gapsSolid += solid.at(x, 10) == 0xFFFFFFFF; gapsDashed += dashed.at(x, 10) == 0xFFFFFFFF; }
    QCOMPARE(gapsSolid, 0); QVERIFY(gapsDashed > 0 || dashed.at(50, 10) == 0xFFFFFFFF || dashed.pixels != solid.pixels);
  }

  void cornerRadiusRoundsRectangleCornersOnly() {
    auto r = make(shape::rectangle, {10, 10}, {90, 70}); r.style = {0xFF000000, 0xFF000000, 2, 0, 1};
    const auto square = render(r);
    QCOMPARE(square.at(11, 11), Argb(0xFF000000));
    r.style.cornerRadius = 20; const auto round = render(r);
    QCOMPARE(round.at(11, 11), Argb(0xFFFFFFFF));  // the corner pixel is cut away
    QCOMPARE(round.at(50, 40), Argb(0xFF000000));  // interior unchanged
    QCOMPARE(round.at(50, 10) >> 24, Argb(0xFF));  // straight edge survives
    // A radius larger than half the shorter side is clamped (stadium shape), not rejected.
    r.style.cornerRadius = 10000; const auto clamped = render(r); QCOMPARE(clamped.at(11, 11), Argb(0xFFFFFFFF)); QCOMPARE(clamped.at(50, 40), Argb(0xFF000000));
    // Ellipse ignores a radius entirely.
    auto e = make(shape::ellipse, {10, 10}, {90, 70}); e.style = {0xFF000000, 0xFF000000, 2, 0, 1};
    const auto plain = render(e); e.style.cornerRadius = 30; QCOMPARE(render(e).pixels, plain.pixels);
  }

  void shadowRendersOffsetPixelsIdenticallyOnCanvasPaintAndExport() {
    auto r = make(shape::rectangle, {20, 20}, {50, 40}); r.style = {0xFF000000, 0xFFFF0000, 2, 0, 1};
    r.style.shadow = {true, 0, 10, 8, 1.0, 0xFF0000FF};  // hard blue shadow, offset (10,8)
    const auto img = render(r);
    QCOMPARE(img.at(35, 30), Argb(0xFFFF0000));            // shape fill still on top
    QCOMPARE(img.at(58, 46), Argb(0xFF0000FF));            // inside shadow, outside the shape: right/below
    QCOMPARE(img.at(15, 15), Argb(0xFFFFFFFF));            // opposite side of the offset stays clear
    QVERIFY(img.at(58, 46) != img.at(48, 40));
    // The export result equals the canvas path (paintShape under identity transform).
    QImage canvas(100, 80, QImage::Format_ARGB32_Premultiplied); canvas.fill(Qt::white);
    { QPainter p(&canvas); paintShape(p, r, QImage()); }
    QImage exported(100, 80, QImage::Format_ARGB32);
    for (int y = 0; y < 80; ++y) for (int x = 0; x < 100; ++x) exported.setPixel(x, y, img.at(x, y));
    for (int y = 0; y < 80; ++y) for (int x = 0; x < 100; ++x) QCOMPARE(canvas.pixel(x, y), exported.pixel(x, y));
    // Disabled shadow draws nothing extra.
    r.style.shadow.enabled = false; QCOMPARE(render(r).at(58, 46), Argb(0xFFFFFFFF));
  }

  void blurSoftensTheShadowAndOpacityScalesIt() {
    auto r = make(shape::ellipse, {30, 20}, {60, 50}); r.style = {0xFF000000, 0xFF000000, 1, 0, 1};
    r.style.shadow = {true, 6, 8, 8, 1.0, 0xFF000000};
    const auto soft = render(r, 120, 100);
    bool partial = false;
    for (qint64 y = 55; y < 70; ++y) for (qint64 x = 62; x < 75; ++x) { const auto v = soft.at(x, y) & 0xFF; partial |= v > 0x10 && v < 0xF0; }
    QVERIFY(partial);
    r.style.shadow = {true, 0, 8, 8, 0.5, 0xFF000000}; const auto half = render(r, 120, 100);
    const auto v = half.at(60, 50) & 0xFF; QVERIFY(v > 0x70 && v < 0x90);  // 50% black on white
    r.style.opacity = 0.5; const auto both = render(r, 120, 100);
    const auto b = both.at(60, 50) & 0xFF; QVERIFY(b > v);                // annotation opacity also fades the shadow
  }

  void shadowFollowsDocumentAxesWhenRotated() {
    auto r = make(shape::rectangle, {40, 30}, {70, 50}); r.style = {0xFF000000, 0xFF00FF00, 1, 90, 1};
    r.style.shadow = {true, 0, 20, 0, 1.0, 0xFF0000FF};
    const auto img = render(r, 120, 100);
    // A quarter turn makes a 20x30 shape centred at (55,40); the shadow is 20px to the right in the document, not rotated.
    QCOMPARE(img.at(75, 40), Argb(0xFF0000FF));
    QCOMPARE(img.at(55, 40), Argb(0xFF00FF00));
  }

  void invalidDecorationIsRejectedByTheSessionWithoutMutation() {
    EditorSession session(document());
    const auto id = session.createShape({{10, 10}, {60, 50}, {}}, ShapeContent{}, session.stateId(), {}); QVERIFY(id);
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    auto props = RectangleProperties{{10, 10}, {60, 50}, session.selectedRectangle()->style};
    for (const double bad : {-1.0, 10001.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
      auto p = props; p.style.cornerRadius = bad; QVERIFY(!session.editRectangleProperties(*id, p, state));
      QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
      p = props; p.style.shadow.blurRadius = bad; QVERIFY(!session.editRectangleProperties(*id, p, state));
      p = props; p.style.shadow.opacity = bad; QVERIFY(!session.editRectangleProperties(*id, p, state));
    }
    auto s = session.toolStyle(); s.shadow.offsetX = std::numeric_limits<double>::quiet_NaN(); QVERIFY(!session.setCreationStyle(s));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
  }

  void creationSnapshotsDefaultsAndEllipseDropsCornerRadius() {
    EditorSession session(document());
    auto style = decorated(); QVERIFY(session.setCreationStyle(style));
    const auto rect = session.createShape({{10, 10}, {60, 50}, session.toolStyle()}, ShapeContent{}, session.stateId(), {}); QVERIFY(rect);
    ShapeContent ellipse; ellipse.type = shape::ellipse;
    const auto ell = session.createShape({{20, 20}, {70, 60}, session.toolStyle()}, ellipse, session.stateId(), session.selection()); QVERIFY(ell);
    ShapeContent text; text.type = shape::text; text.text = "t";
    const auto txt = session.createShape({{20, 20}, {70, 60}, session.toolStyle()}, text, session.stateId(), session.selection()); QVERIFY(txt);
    const auto &doc = session.document().annotations;
    QCOMPARE(std::get<RectangleAnnotation>(doc[0]).style.cornerRadius, 9.0);
    QCOMPARE(std::get<RectangleAnnotation>(doc[1]).style.cornerRadius, 0.0);
    QVERIFY(std::get<RectangleAnnotation>(doc[1]).style.shadow.enabled);
    QVERIFY(!std::get<RectangleAnnotation>(doc[2]).style.shadow.enabled);  // text carries no EA-005 fields
    // EA-014: later default edits never touch existing objects.
    auto later = style; later.borderStyle = BorderStyle::Dot; later.cornerRadius = 1; later.shadow.enabled = false;
    const auto edit = session.editDecorationDefaults(later, session.stateId(), session.selection(), session.toolStyle());
    QVERIFY(edit.accepted); QVERIFY(!edit.documentChanged); QVERIFY(edit.defaultsChanged); QCOMPARE(session.undoCount(), 3);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.borderStyle, BorderStyle::DashDot);
    QCOMPARE(session.toolStyle().borderStyle, BorderStyle::Dot);
    // Stale snapshot is refused.
    QVERIFY(!session.editDecorationDefaults(style, session.stateId(), session.selection(), style).accepted);
  }

  void orderVisibilityAndGeometrySurviveAlongsideDecoration() {
    auto d = document(); auto a = make(shape::rectangle, {1, 1}, {40, 40}), b = make(shape::ellipse, {5, 5}, {50, 50});
    a.style = decorated(); b.style = decorated(); b.style.cornerRadius = 0; b.visible = false;
    d.annotations = {a, b};
    EditorSession session(d); session.select({a.id});
    QVERIFY(session.reorderSelection(EditorSession::Order::Front)); QCOMPARE(session.undoCount(), 1);
    const auto saved = serializeDocument(session.document());
    const auto loaded = parseDocument(saved); QVERIFY(loaded.document);
    QCOMPARE(annotationId(loaded.document->annotations[1]), a.id);
    QVERIFY(!std::get<RectangleAnnotation>(loaded.document->annotations[0]).visible);
    QVERIFY(sameDecoration(std::get<RectangleAnnotation>(loaded.document->annotations[1]).style, a.style));
    QVERIFY(session.moveSelection(3, 4)); QVERIFY(session.resizeSelection(5, 5)); QVERIFY(session.rotateSelection(10));
    const auto &moved = std::get<RectangleAnnotation>(session.document().annotations[1]);
    QVERIFY(sameDecoration(moved.style, a.style));
  }

  void copyPasteAndDuplicatePreserveDecoration() {
    auto d = document(); auto a = make(shape::rectangle, {1, 1}, {40, 40}), b = make(shape::ellipse, {5, 5}, {50, 50});
    a.style = decorated(); b.style = decorated(); b.style.cornerRadius = 0; d.annotations = {a, b};
    const auto projected = selectedAnnotationTransfer(d, {a.id, b.id}, true); QVERIFY(projected.ok());
    const auto parsed = parseAnnotationClipboard(projected.bytes); QVERIFY(parsed.ok());
    EditorSession session(d);
    const auto pasted = session.insertAnnotations(parsed.annotations, session.stateId()); QVERIFY(pasted.changed);
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), size_t(4));
    for (int i = 0; i < 2; ++i) {
      const auto &src = std::get<RectangleAnnotation>(session.document().annotations[std::size_t(i)]);
      const auto &copy = std::get<RectangleAnnotation>(session.document().annotations[std::size_t(i + 2)]);
      QVERIFY(copy.id != src.id); QVERIFY(sameDecoration(copy.style, src.style)); QVERIFY(!(copy.start == src.start));
    }
    session.select({a.id}); QVERIFY(session.duplicateSelection().changed);
    QVERIFY(sameDecoration(std::get<RectangleAnnotation>(session.document().annotations.back()).style, a.style));
  }

  void nativeEditIsOneUndoStepAndReloadPreservesFields_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("rectangle") << shape::rectangle; QTest::newRow("ellipse") << shape::ellipse;
  }
  void nativeEditIsOneUndoStepAndReloadPreservesFields() {
    QFETCH(QString, type);
    auto d = document(); auto r = make(type, {10, 10}, {90, 70}); d.annotations = {r};
    EditorSession session(d); session.select({r.id}); session.markSaved();
    QImage image(200, 150, QImage::Format_ARGB32); image.fill(Qt::white);
    EditorCanvas canvas(session, image); canvas.show(); canvas.setTool(EditorCanvas::Tool::Select);
    const auto before = serializeDocument(session.document());
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      auto *style = dialog->findChild<QComboBox *>("shapeBorderStyle"); QVERIFY(style);
      style->setCurrentIndex(style->findText("DashDotDot"));
      QCOMPARE(dialog->findChild<QLineEdit *>("shapeCornerRadius") != nullptr, type == shape::rectangle);
      if (type == shape::rectangle) dialog->findChild<QLineEdit *>("shapeCornerRadius")->setText("12");
      dialog->findChild<QCheckBox *>("shapeShadow")->setChecked(true);
      dialog->findChild<QLineEdit *>("shapeShadowBlur")->setText("5");
      dialog->findChild<QLineEdit *>("shapeShadowOffsetX")->setText("7");
      dialog->findChild<QLineEdit *>("shapeShadowOffsetY")->setText("-3");
      dialog->findChild<QLineEdit *>("shapeShadowOpacity")->setText("0.4");
      dialog->findChild<QLineEdit *>("shapeShadowColor")->setText("#FF334455");
      dialog->accept();
    });
    timer.start(5); const bool changed = canvas.editRectangleProperties(&canvas); timer.stop();
    QVERIFY(acted); QVERIFY(changed); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    const auto &edited = std::get<RectangleAnnotation>(session.document().annotations[0]).style;
    QCOMPARE(edited.borderStyle, BorderStyle::DashDotDot); QCOMPARE(edited.cornerRadius, type == shape::rectangle ? 12.0 : 0.0);
    QCOMPARE(edited.shadow, (ShapeShadow{true, 5, 7, -3, 0.4, 0xFF334455}));
    const auto saved = serializeDocument(session.document());
    const auto reloaded = parseDocument(saved); QVERIFY(reloaded.document);
    QVERIFY(sameDecoration(std::get<RectangleAnnotation>(reloaded.document->annotations[0]).style, edited));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), saved);
  }

  void defaultsDialogReturnsOnlyDecorationAndCancelReturnsNothing() {
    RectangleStyle base; base.strokeWidth = 7;
    for (const bool apply : {true, false}) {
      bool acted = false; QTimer timer;
      connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
        QCOMPARE(dialog->objectName(), QString("shapeStyleDefaultsDialog"));
        QVERIFY(!dialog->findChild<QLineEdit *>("rectangleLeft")); QVERIFY(!dialog->findChild<QLineEdit *>("rectangleStrokeWidth"));
        auto *style = dialog->findChild<QComboBox *>("shapeBorderStyle"); style->setCurrentIndex(style->findText("Dot"));
        dialog->findChild<QLineEdit *>("shapeCornerRadius")->setText("6"); dialog->findChild<QCheckBox *>("shapeShadow")->setChecked(true);
        if (apply) dialog->accept(); else dialog->reject();
      });
      timer.start(5); const auto chosen = chooseDecorationDefaults(base, nullptr); timer.stop();
      QVERIFY(acted); QCOMPARE(bool(chosen), apply);
      if (chosen) { QCOMPARE(chosen->borderStyle, BorderStyle::Dot); QCOMPARE(chosen->cornerRadius, 6.0); QVERIFY(chosen->shadow.enabled); QCOMPARE(chosen->strokeWidth, 7.0); }
    }
  }
  void invalidDialogInputLeavesDocumentUnchanged() {
    auto d = document(); auto r = make(shape::rectangle, {10, 10}, {90, 70}); d.annotations = {r};
    EditorSession session(d); session.select({r.id});
    QImage image(200, 150, QImage::Format_ARGB32); image.fill(Qt::white);
    EditorCanvas canvas(session, image); canvas.show(); canvas.setTool(EditorCanvas::Tool::Select);
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      dialog->findChild<QLineEdit *>("shapeCornerRadius")->setText("-4"); dialog->accept();
      QVERIFY(dialog->isVisible()); dialog->reject();
    });
    const auto before = serializeDocument(session.document());
    timer.start(5); QVERIFY(!canvas.editRectangleProperties(&canvas)); timer.stop();
    QVERIFY(acted); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
  }
};

QTEST_MAIN(ShapeStyleTest)
#include "ShapeStyleTest.moc"
