// EDITOR-ANNOTATIONS-001 EA-011: Highlight, Blur, Pixelate, Magnify and
// Spotlight apply only inside their declared geometry (rectangle or ellipse,
// rotated), clip safely at the canvas edge, render deterministically from the
// immutable source, and keep strength / isEllipse / blurAmount through .xann.
#include "EditorCanvas.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"
#include "image-editor/ShapePainter.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QPainter>
#include <QTest>
#include <cmath>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
// Deterministic busy pixels, every channel at least 60, so any blur, pixelate,
// magnify, highlight or dim visibly changes nearly every pixel it touches.
ArgbImage noise(qint64 width, qint64 height) {
  ArgbImage image = solidImage(width, height, 0xFFFFFFFF);
  quint32 state = 12345;
  for (auto &pixel : image.pixels) {
    state = state * 1664525u + 1013904223u;
    const quint32 r = 60 + ((state >> 8) % 196), g = 60 + ((state >> 16) % 196), b = 60 + ((state >> 24) % 196);
    pixel = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  return image;
}
RectangleAnnotation effect(const QString &type, PointF start, PointF end, bool ellipse = false, double rotation = 0, double strength = 0) {
  RectangleAnnotation a;
  a.id = QUuid::createUuid();
  a.start = start; a.end = end;
  a.style = {0x00000000, 0x00000000, 1, rotation, 1};
  a.content.type = type; a.content.isEllipse = ellipse; a.content.strength = strength;
  return a;
}
const QStringList kEffects = {shape::highlight, shape::blur, shape::pixelate, shape::magnify, shape::spotlight};

// Signed distance-like test independent of the renderer: > 0 outside the shape.
// Positive `margin` shrinks nothing; callers compare against +-margin pixels.
double outsideness(const RectangleAnnotation &s, double x, double y) {
  const double cx = (s.left() + s.right()) / 2, cy = (s.top() + s.bottom()) / 2;
  const double rad = -s.style.rotationDegrees * std::acos(-1.0) / 180.0;
  const double dx = x - cx, dy = y - cy;
  const double lx = dx * std::cos(rad) - dy * std::sin(rad), ly = dx * std::sin(rad) + dy * std::cos(rad);
  const double hw = (s.right() - s.left()) / 2, hh = (s.bottom() - s.top()) / 2;
  if (!s.content.isEllipse) return std::max(std::abs(lx) - hw, std::abs(ly) - hh);
  // Scaled radial distance converted back to pixels (exact on the axes, close elsewhere).
  const double r = std::hypot(lx / hw, ly / hh);
  return (r - 1) * std::min(hw, hh);
}
bool same(const ArgbImage &a, const ArgbImage &b) { return a.width == b.width && a.height == b.height && a.pixels == b.pixels; }
QByteArray pngOf(const QImage &image) {
  QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return png;
}
AnnotationDocument baseDocument(qint64 w = 200, qint64 h = 150) {
  AnnotationDocument d; d.canvasWidth = w; d.canvasHeight = h; d.createdAt = d.modifiedAt = QStringLiteral("2026-11-03T00:00:00Z");
  QImage image(int(w), int(h), QImage::Format_ARGB32); image.fill(Qt::white); d.sourceImagePng = pngOf(image);
  return d;
}
}  // namespace

class EffectRegionTest : public QObject {
  Q_OBJECT
private slots:
  void effectsChangeOnlyInsideTheirGeometry_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<bool>("ellipse"); QTest::addColumn<double>("rotation");
    for (const auto &type : kEffects)
      for (const bool ellipse : {false, true})
        for (const double rotation : {0.0, 30.0})
          QTest::newRow(qPrintable(type + (ellipse ? "-ellipse" : "-rect") + (rotation ? "-rotated" : ""))) << type << ellipse << rotation;
  }
  void effectsChangeOnlyInsideTheirGeometry() {
    QFETCH(QString, type); QFETCH(bool, ellipse); QFETCH(double, rotation);
    const ArgbImage source = noise(160, 120);
    const auto s = effect(type, {50, 30}, {110, 90}, ellipse, rotation, type == shape::magnify ? 3 : type == shape::blur ? 3 : type == shape::pixelate ? 6 : 0);
    const auto result = render(source, {s});
    QVERIFY(result.image);
    const bool dimsOutside = type == shape::spotlight;
    qint64 insideChanged = 0, insideTotal = 0;
    for (qint64 y = 0; y < source.height; ++y)
      for (qint64 x = 0; x < source.width; ++x) {
        const double o = outsideness(s, x + 0.5, y + 0.5);
        const bool differs = result.image->at(x, y) != source.at(x, y);
        if (o > 1.5) { QVERIFY2(differs == dimsOutside, qPrintable(QString("outside pixel %1,%2").arg(x).arg(y))); }
        else if (o < -3) {
          ++insideTotal; if (differs) ++insideChanged;
          if (dimsOutside) QVERIFY2(!differs, qPrintable(QString("inside spotlight pixel %1,%2").arg(x).arg(y)));
        }
      }
    QVERIFY(insideTotal > 500);
    if (!dimsOutside) QVERIFY2(insideChanged * 10 > insideTotal * 8, qPrintable(QString("%1/%2 changed").arg(insideChanged).arg(insideTotal)));
  }

  void ellipseCornersAreNotAffected() {
    const ArgbImage source = noise(100, 100);
    const auto rect = render(source, {effect(shape::pixelate, {10, 10}, {90, 90}, false, 0, 8)});
    const auto round = render(source, {effect(shape::pixelate, {10, 10}, {90, 90}, true, 0, 8)});
    QVERIFY(rect.image && round.image);
    QVERIFY(rect.image->at(12, 12) != source.at(12, 12));   // corner belongs to the rectangle
    QCOMPARE(round.image->at(12, 12), source.at(12, 12));   // but not to the ellipse
    QCOMPARE(round.image->at(50, 50), rect.image->at(50, 50));
  }

  void geometryBeyondTheCanvasClipsSafely_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<bool>("ellipse"); QTest::addColumn<double>("rotation");
    for (const auto &type : kEffects) {
      QTest::newRow(qPrintable(type + "-rect")) << type << false << 0.0;
      QTest::newRow(qPrintable(type + "-ellipse-rotated")) << type << true << 37.0;
    }
  }
  void geometryBeyondTheCanvasClipsSafely() {
    QFETCH(QString, type); QFETCH(bool, ellipse); QFETCH(double, rotation);
    const ArgbImage source = noise(64, 48);
    const std::vector<std::pair<PointF, PointF>> boxes = {
        {{-30, -20}, {30, 20}},          // top-left corner
        {{40, 30}, {120, 90}},           // bottom-right corner
        {{-500, -500}, {500, 500}},      // swallows the canvas
        {{-1e9, -1e9}, {1e9, 1e9}},      // absurdly large but finite
        {{-90, -90}, {-10, -10}},        // entirely outside
        {{500, 10}, {600, 40}}};
    for (const auto &[start, end] : boxes) {
      const auto s = effect(type, start, end, ellipse, rotation, 4);
      const auto first = render(source, {s});
      QVERIFY2(first.image, "render must not fail on a clipped region");
      QCOMPARE(first.image->pixels.size(), source.pixels.size());
      const auto second = render(source, {s});
      QVERIFY(second.image && same(*first.image, *second.image));  // deterministic
      if (type != shape::spotlight && outsideness(s, 32, 24) > 0 && start.x < 0 && end.x < 0)
        QVERIFY(same(*first.image, source));  // a region wholly off the canvas changes nothing
    }
  }

  void clippedBlurMatchesTheSameBlurInsideTheCanvas() {
    // The blur inside the canvas must not depend on how far the region hangs off it.
    const ArgbImage source = noise(100, 80);
    const auto inside = render(source, {effect(shape::blur, {0, 10}, {40, 60}, false, 0, 4)});
    const auto hanging = render(source, {effect(shape::blur, {-60, 10}, {40, 60}, false, 0, 4)});
    QVERIFY(inside.image && hanging.image);
    for (qint64 y = 0; y < 80; ++y) for (qint64 x = 0; x < 100; ++x) QCOMPARE(hanging.image->at(x, y), inside.image->at(x, y));
    // And an interior region sees the same pixels as the larger region around it.
    const auto big = render(source, {effect(shape::blur, {10, 10}, {90, 70}, false, 0, 4)});
    const auto small = render(source, {effect(shape::blur, {30, 20}, {60, 50}, false, 0, 4)});
    QVERIFY(big.image && small.image);
    for (qint64 y = 20; y < 50; ++y) for (qint64 x = 30; x < 60; ++x) QCOMPARE(small.image->at(x, y), big.image->at(x, y));
  }

  void overlappingEffectsSampleTheImmutableSource_data() {
    QTest::addColumn<QString>("lower"); QTest::addColumn<QString>("upper");
    for (const QString &a : {shape::highlight, shape::blur, shape::pixelate, shape::magnify})
      for (const QString &b : {shape::highlight, shape::blur, shape::pixelate, shape::magnify})
        QTest::newRow(qPrintable(a + "-under-" + b)) << a << b;
  }
  void overlappingEffectsSampleTheImmutableSource() {
    QFETCH(QString, lower); QFETCH(QString, upper);
    const ArgbImage source = noise(120, 90);
    const auto a = effect(lower, {20, 15}, {80, 65}, false, 0, 5);
    const auto b = effect(upper, {50, 30}, {105, 80}, true, 0, 5);
    const auto stacked = render(source, {a, b});
    const auto onlyB = render(source, {b});
    const auto onlyA = render(source, {a});
    const auto reordered = render(source, {b, a});
    QVERIFY(stacked.image && onlyA.image && onlyB.image && reordered.image);
    int checked = 0;
    for (qint64 y = 0; y < 90; ++y)
      for (qint64 x = 0; x < 120; ++x) {
        // The top effect shows only the source, whatever lies beneath it...
        if (outsideness(b, x + 0.5, y + 0.5) < -2) { QCOMPARE(stacked.image->at(x, y), onlyB.image->at(x, y)); ++checked; }
        // ...and so does the other one when the order is reversed.
        if (outsideness(a, x + 0.5, y + 0.5) < -2) QCOMPARE(reordered.image->at(x, y), onlyA.image->at(x, y));
        // Where only the lower effect reaches, it is unaffected by the upper one.
        if (outsideness(a, x + 0.5, y + 0.5) < -2 && outsideness(b, x + 0.5, y + 0.5) > 2) QCOMPARE(stacked.image->at(x, y), onlyA.image->at(x, y));
      }
    QVERIFY(checked > 500);
  }

  void repaintingOverAnEffectNeverSamplesItsOwnPreview_data() {
    QTest::addColumn<QString>("type");
    for (const QString &t : {shape::highlight, shape::blur, shape::pixelate, shape::magnify}) QTest::newRow(qPrintable(t)) << t;
  }
  void repaintingOverAnEffectNeverSamplesItsOwnPreview() {
    QFETCH(QString, type);
    const ArgbImage argb = noise(90, 70);
    QImage source(90, 70, QImage::Format_ARGB32);
    for (int y = 0; y < 70; ++y) for (int x = 0; x < 90; ++x) source.setPixel(x, y, argb.at(x, y));
    const QImage pristine = source;
    const auto s = effect(type, {15, 10}, {75, 60}, true, 20, 4);
    QImage canvas = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    { QPainter p(&canvas); paintShape(p, s, source); }
    const QImage once = canvas;
    { QPainter p(&canvas); paintShape(p, s, source); }  // the preview already shows the effect
    // Deep inside the region the result never depends on what was on the canvas
    // (edge pixels are antialiased against the canvas beneath, by design).
    QImage garbage(90, 70, QImage::Format_ARGB32_Premultiplied); garbage.fill(QColor(200, 10, 90));
    { QPainter p(&garbage); paintShape(p, s, source); }
    int deep = 0;
    for (int y = 0; y < 70; ++y) for (int x = 0; x < 90; ++x)
      if (outsideness(s, x + 0.5, y + 0.5) < -2) { QCOMPARE(canvas.pixel(x, y), once.pixel(x, y)); QCOMPARE(garbage.pixel(x, y), once.pixel(x, y)); ++deep; }
    QVERIFY(deep > 300);
    QCOMPARE(source, pristine);                           // the source is never modified
    QVERIFY(once != source.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    // The canvas preview and the exporter run the same painter: identical pixels.
    const auto exported = render(argb, {s});
    QVERIFY(exported.image);
    const QImage exportedImage = once.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < 70; ++y) for (int x = 0; x < 90; ++x) QCOMPARE(exported.image->at(x, y), exportedImage.pixel(x, y));
  }

  void spotlightBlurSamplesTheSourceAndLeavesTheRegion() {
    const ArgbImage source = noise(100, 80);
    auto cover = effect(shape::rectangle, {0, 0}, {100, 80}); cover.style = {0xFF0000FF, 0xFF0000FF, 1, 0, 1};
    auto spot = effect(shape::spotlight, {30, 20}, {70, 60}, true, 0, 50); spot.content.blurAmount = 5;
    const auto alone = render(source, {spot});
    const auto covered = render(source, {cover, spot});
    const auto plain = render(source, {effect(shape::spotlight, {30, 20}, {70, 60}, true, 0, 50)});
    QVERIFY(alone.image && covered.image && plain.image);
    int differsFromPlain = 0, outsideSeen = 0;
    for (qint64 y = 0; y < 80; ++y) for (qint64 x = 0; x < 100; ++x) {
      const double o = outsideness(spot, x + 0.5, y + 0.5);
      if (o > 1.5) { QCOMPARE(covered.image->at(x, y), alone.image->at(x, y)); ++outsideSeen; if (alone.image->at(x, y) != plain.image->at(x, y)) ++differsFromPlain; }
      if (o < -2) { QCOMPARE(alone.image->at(x, y), source.at(x, y)); QCOMPARE(covered.image->at(x, y), Argb(0xFF0000FF)); }
    }
    QVERIFY(outsideSeen > 1000 && differsFromPlain > outsideSeen * 8 / 10);  // blurAmount really blurs the dimmed area
  }

  void renderingIsByteDeterministic() {
    const ArgbImage source = noise(120, 90);
    std::vector<Annotation> list;
    for (const auto &type : kEffects) list.push_back(effect(type, {10.5, 7.25}, {100.75, 80.5}, true, 17, 5));
    const auto first = render(source, list);
    QVERIFY(first.image);
    for (int n = 0; n < 3; ++n) { const auto again = render(source, list); QVERIFY(again.image && same(*first.image, *again.image)); }
  }

  void optionsRoundTripThroughXannAndDefaultsAreOmitted() {
    AnnotationDocument d = baseDocument();
    for (const auto &type : kEffects) {
      auto a = effect(type, {10, 10}, {60, 50}, true, 0, 7.5);
      if (type == shape::spotlight) a.content.blurAmount = 12.25;
      d.annotations.push_back(a);
      d.annotations.push_back(effect(type, {70, 10}, {120, 50}));  // defaults
    }
    const auto json = serializeDocument(d);
    const auto written = json.value("annotations").toArray();
    for (int i = 0; i < written.size(); ++i) {
      const auto o = written.at(i).toObject();
      const bool custom = i % 2 == 0;
      QCOMPARE(o.contains("isEllipse"), custom);                       // default false is omitted
      QCOMPARE(o.contains("blurAmount"), custom && o.value("type") == "spotlight");
      if (custom) QCOMPARE(o.value("isEllipse").toBool(), true);
    }
    const auto gzip = writeXann(d); QVERIFY(gzip);
    const auto read = readXann(*gzip); QVERIFY(read.document);
    QCOMPARE(serializeDocument(*read.document), json);
    const auto *spot = std::get_if<RectangleAnnotation>(&read.document->annotations[8]);
    QVERIFY(spot); QCOMPARE(spot->type(), shape::spotlight);
    QVERIFY(spot->content.isEllipse); QCOMPARE(spot->content.blurAmount, 12.25); QCOMPARE(spot->content.strength, 7.5);
  }

  void referenceStyleJsonIsReadAndUnknownFieldsSurvive() {
    AnnotationDocument d = baseDocument();
    d.annotations.push_back(effect(shape::spotlight, {10, 10}, {60, 50}));
    d.annotations.push_back(effect(shape::blur, {10, 10}, {60, 50}));
    d.annotations.push_back(effect(shape::rectangle, {10, 10}, {60, 50}));
    QJsonObject payload = serializeDocument(d);
    QJsonArray list = payload.value("annotations").toArray();
    // Hand-written as the reference (camelCase) format spells them.
    QJsonObject spot = list[0].toObject(); spot.insert("isEllipse", true); spot.insert("blurAmount", 7.5); spot.insert("strength", 40); spot.insert("futureSpotlightField", QJsonObject{{"a", 1}});
    QJsonObject blur = list[1].toObject(); blur.insert("isEllipse", true); blur.insert("blurAmount", 9); blur.insert("futureBlurField", "x");
    QJsonObject rect = list[2].toObject(); rect.insert("isEllipse", true); rect.insert("blurAmount", 3);
    list = QJsonArray{spot, blur, rect}; payload.insert("annotations", list);
    const auto parsed = parseDocument(payload); QVERIFY(parsed.document);
    const auto *s = std::get_if<RectangleAnnotation>(&parsed.document->annotations[0]);
    const auto *b = std::get_if<RectangleAnnotation>(&parsed.document->annotations[1]);
    const auto *r = std::get_if<RectangleAnnotation>(&parsed.document->annotations[2]);
    QVERIFY(s && b && r);
    QVERIFY(s->content.isEllipse); QCOMPARE(s->content.blurAmount, 7.5); QCOMPARE(s->content.strength, 40.0);
    QVERIFY(b->content.isEllipse); QCOMPARE(b->content.blurAmount, 0.0);   // blur has no blurAmount concept: preserved, not interpreted
    QVERIFY(!r->content.isEllipse);                                         // a Rectangle has neither field
    const auto out = serializeDocument(*parsed.document).value("annotations").toArray();
    QCOMPARE(out[0].toObject().value("futureSpotlightField").toObject().value("a").toInt(), 1);
    QCOMPARE(out[1].toObject().value("futureBlurField").toString(), QString("x"));
    QCOMPARE(out[1].toObject().value("blurAmount").toInt(), 9);
    QCOMPARE(out[2].toObject().value("isEllipse").toBool(), true);
    QCOMPARE(out[2].toObject().value("blurAmount").toInt(), 3);
    QCOMPARE(out[0].toObject().value("isEllipse").toBool(), true);
    QCOMPARE(out[0].toObject().value("blurAmount").toDouble(), 7.5);
  }

  void invalidOptionsAreRejectedWithTheDocumentDiagnostic_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<QString>("key"); QTest::addColumn<QJsonValue>("value");
    QTest::newRow("ellipse-string") << shape::blur << "isEllipse" << QJsonValue("yes");
    QTest::newRow("ellipse-number") << shape::highlight << "isEllipse" << QJsonValue(1);
    QTest::newRow("ellipse-null") << shape::spotlight << "isEllipse" << QJsonValue(QJsonValue::Null);
    QTest::newRow("blur-negative") << shape::spotlight << "blurAmount" << QJsonValue(-0.5);
    QTest::newRow("blur-too-large") << shape::spotlight << "blurAmount" << QJsonValue(kMaxBlurAmount + 1);
    QTest::newRow("blur-string") << shape::spotlight << "blurAmount" << QJsonValue("3");
  }
  void invalidOptionsAreRejectedWithTheDocumentDiagnostic() {
    QFETCH(QString, type); QFETCH(QString, key); QFETCH(QJsonValue, value);
    AnnotationDocument d = baseDocument(); d.annotations.push_back(effect(type, {10, 10}, {60, 50}));
    QJsonObject payload = serializeDocument(d); QJsonArray list = payload.value("annotations").toArray();
    QJsonObject o = list[0].toObject(); o.insert(key, value); list[0] = o; payload.insert("annotations", list);
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
    // The boundary values are accepted.
    if (key == "blurAmount") { o.insert(key, kMaxBlurAmount); list[0] = o; payload.insert("annotations", list); QVERIFY(parseDocument(payload).document); }
  }

  void sessionRejectsOutOfRangeBlurAmountAndNormalisesIrrelevantOptions() {
    EditorSession session(baseDocument());
    ShapeContent bad; bad.type = shape::spotlight; bad.blurAmount = kMaxBlurAmount + 1;
    QVERIFY(!session.createShape({{10, 10}, {60, 50}, {}}, bad, session.stateId(), session.selection()));
    QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(session.undoCount(), 0);
    ShapeContent rect; rect.type = shape::rectangle; rect.isEllipse = true; rect.blurAmount = 5;
    QVERIFY(session.createShape({{10, 10}, {60, 50}, {}}, rect, session.stateId(), session.selection()));
    QVERIFY(!session.selectedRectangle()->content.isEllipse); QCOMPARE(session.selectedRectangle()->content.blurAmount, 0.0);
  }

  void nativeEditIsOneHistoryOperationAndReloads_data() {
    QTest::addColumn<QString>("type");
    for (const auto &type : kEffects) QTest::newRow(qPrintable(type)) << type;
  }
  // Highlight has no strength, so its dialog offers only the region shape.
  void highlightEdit(EditorCanvas &canvas, EditorSession &session, const QJsonObject &before) {
    auto *input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    QCOMPARE(input->textValue(), QString("Rectangle")); input->setTextValue("Ellipse"); input->reject(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input); input->accept();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    input->setTextValue("Ellipse"); input->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.undoCount(), 2); QVERIFY(session.selectedRectangle()->content.isEllipse);
    const auto after = serializeDocument(session.document());
    const auto reloaded = parseDocument(after); QVERIFY(reloaded.document); QCOMPARE(serializeDocument(*reloaded.document), after);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), after);
  }
  void nativeEditIsOneHistoryOperationAndReloads() {
    QFETCH(QString, type);
    EditorSession session(baseDocument(400, 300)); ShapeContent content; content.type = type; content.strength = 0;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document());
    QImage image(400, 300, QImage::Format_ARGB32); image.fill(Qt::white);
    EditorCanvas canvas(session, image);
    // Cancel changes nothing.
    QVERIFY(canvas.editShapeContent(&canvas)); auto *input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    if (type == shape::highlight) { highlightEdit(canvas, session, before); return; }
    auto *ellipse = input->findChild<QCheckBox *>("effectEllipseCheck"); QVERIFY(ellipse); QVERIFY(!ellipse->isChecked());
    ellipse->setChecked(true); input->reject(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // Unchanged Apply is not a mutation.
    QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input); input->accept();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // One Apply changes shape, strength and blur together: one operation.
    QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    ellipse = input->findChild<QCheckBox *>("effectEllipseCheck"); QVERIFY(ellipse); ellipse->setChecked(true);
    auto *blur = input->findChild<QDoubleSpinBox *>("effectBlurAmountSpin");
    QCOMPARE(blur != nullptr, type == shape::spotlight);
    if (blur) blur->setValue(6.5);
    input->setDoubleValue(type == shape::magnify ? 3.5 : 10.25);
    input->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.undoCount(), 2);
    const auto *edited = session.selectedRectangle(); QVERIFY(edited);
    QVERIFY(edited->content.isEllipse); QCOMPARE(edited->content.strength, type == shape::magnify ? 3.5 : 10.25);
    QCOMPARE(edited->content.blurAmount, type == shape::spotlight ? 6.5 : 0.0);
    const auto after = serializeDocument(session.document());
    const auto reloaded = parseDocument(after); QVERIFY(reloaded.document);
    QCOMPARE(serializeDocument(*reloaded.document), after);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), after);
    // Unticking returns to the default and the key leaves the file.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    input->findChild<QCheckBox *>("effectEllipseCheck")->setChecked(false); input->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QVERIFY(!serializeDocument(session.document()).value("annotations").toArray()[0].toObject().contains("isEllipse"));
  }
};

QTEST_MAIN(EffectRegionTest)
#include "EffectRegionTest.moc"
