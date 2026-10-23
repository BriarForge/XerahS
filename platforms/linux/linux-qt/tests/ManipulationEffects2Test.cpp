// EDITOR-EFFECTS-001: EE-003, EE-006, EE-008..EE-010, EE-016, EE-018.
#include "image-editor/effects/Effect.h"
#include "image-editor/CanvasOperations.h"
#include <QColorSpace>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>

using namespace xerahs::effects;
namespace ed = xerahs::editor;
namespace {
const QStringList ids{"tile_repeat", "crop_image", "resize_canvas", "auto_crop_image", "glass_sphere", "wave_distortion",
    "symmetry", "skew", "fisheye_lens", "twirl", "tile_shuffle", "mirror_tiles", "kaleidoscope", "pinch_bulge",
    "fold_crease_warp", "cylinder_wrap", "ripple_refraction", "resize_image", "displacement_map", "rotate_custom_angle",
    "polar_warp", "rotate_3d", "liquify_push_smudge", "page_curl", "perspective_warp", "rotate_3d_box"};
QImage fixture(int w = 128, int h = 112) {
  QImage s(w, h, QImage::Format_ARGB32); s.setColorSpace(QColorSpace::DisplayP3);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    s.setPixel(x, y, qRgba((x * 23 + y * 7) % 256, (x * 3 + y * 17) % 256, (x * 11 + y * 5) % 256, 100 + (x + y) % 156));
  return s;
}
QImage ramp(int w = 9, int h = 9) {
  QImage s(w, h, QImage::Format_ARGB32); s.setColorSpace(QColorSpace::DisplayP3);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) s.setPixel(x, y, qRgba(x * 20, y * 20, 40, 255));
  return s;
}
QImage solid(int w, int h, QRgb v) { auto s = fixture(w, h); s.fill(v); return s; }
EffectResult run(const QString &id, const QImage &s, const ParamValues &p = {}, const EffectContext &c = {}) {
  const auto *effect = builtinEffects().find(id);
  return effect ? runEffect(*effect, s, p, c) : EffectResult{{}, "missing-effect"};
}
QImage canvas(const QImage &s, const ed::CanvasOperation &op) {
  ed::ArgbImage input{s.width(), s.height(), {}};
  for (int y = 0; y < s.height(); ++y) for (int x = 0; x < s.width(); ++x) input.pixels.push_back(s.pixel(x, y));
  ed::AnnotationDocument doc; doc.canvasWidth = s.width(); doc.canvasHeight = s.height();
  const auto result = ed::applyCanvasOperation(doc, input, op);
  if (result.error) return {};
  if (!result.image) return s;
  QImage out(int(result.image->width), int(result.image->height), QImage::Format_ARGB32); out.setColorSpace(s.colorSpace());
  for (int y = 0; y < out.height(); ++y) for (int x = 0; x < out.width(); ++x) out.setPixel(x, y, result.image->at(x, y));
  return out;
}
int changed(const QImage &a, const QImage &b) {
  if (a.size() != b.size()) return -1;
  int n = 0; for (int y = 0; y < a.height(); ++y) for (int x = 0; x < a.width(); ++x) n += a.pixel(x, y) != b.pixel(x, y);
  return n;
}
}
class ManipulationEffects2Test : public QObject {
  Q_OBJECT
  QTemporaryDir dir;
  QString mapPath() const { return dir.filePath("map.png"); }
private slots:
  void initTestCase() {
    QVERIFY(dir.isValid());
    QVERIFY(solid(3, 2, qRgba(255, 0, 0, 255)).save(mapPath()));
  }
  void contracts_data() {
    QTest::addColumn<QString>("id");
    for (const auto &id : ids) QTest::newRow(qPrintable(id)) << id;
  }
  void contracts() {
    QFETCH(QString, id);
    const auto *effect = builtinEffects().find(id); QVERIFY(effect);
    const auto &spec = effect->spec();
    QCOMPARE(spec.category, Category::Manipulations); QCOMPARE(spec.mode, Mode::Previewable);
    QCOMPARE(spec.usesSeed, id == "tile_shuffle");
    const QStringList sized{"crop_image", "auto_crop_image", "resize_canvas", "resize_image", "rotate_custom_angle", "skew", "rotate_3d", "rotate_3d_box"};
    QCOMPARE(spec.changesSize, sized.contains(id));
    QVERIFY(spec.referencePath.endsWith("ImageEffect.cs"));
    const auto defaults = resolveParameters(spec, {}); QVERIFY(defaults.ok());
    for (const auto &p : spec.parameters) {
      if (p.kind == ParamKind::Int || p.kind == ParamKind::Float) {
        QVERIFY(p.defaultValue.toDouble() >= p.minimum); QVERIFY(p.defaultValue.toDouble() <= p.maximum);
        QVERIFY(!resolveParameters(spec, {{p.id, p.minimum - 1}}).ok());
        QVERIFY(!resolveParameters(spec, {{p.id, p.maximum + 1}}).ok());
      } else if (p.kind == ParamKind::Enum) {
        QVERIFY(p.choices.contains(p.defaultValue.toString()));
        QVERIFY(!resolveParameters(spec, {{p.id, QString("invalid-choice")}}).ok());
      }
    }
    QVERIFY(!resolveParameters(spec, {{"unknown", 1}}).ok());
    auto s = fixture();
    // Top-left-matching border makes default auto-crop observable.
    for (int x = 0; x < s.width(); ++x) { s.setPixel(x, 0, 0); s.setPixel(x, s.height() - 1, 0); }
    for (int y = 0; y < s.height(); ++y) { s.setPixel(0, y, 0); s.setPixel(s.width() - 1, y, 0); }
    ParamValues p; if (id == "displacement_map") p["map_file_path"] = mapPath();
    int last = 0; bool monotonic = true;
    EffectContext progress;
    progress.progress = [&](int percent) { monotonic &= percent >= last && percent >= 0 && percent <= 100; last = percent; };
    const auto result = run(id, s.convertToFormat(QImage::Format_RGBA8888), p, progress);
    QVERIFY(monotonic); QCOMPARE(last, 100);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QCOMPARE(result.image.format(), QImage::Format_ARGB32); QCOMPARE(result.image.colorSpace(), s.colorSpace());
    if (!spec.changesSize) QCOMPARE(result.image.size(), s.size());
    const QStringList unchanged{"resize_canvas", "resize_image", "rotate_custom_angle", "skew", "rotate_3d", "perspective_warp"};
    // Zero-rotation extrusion leaves the front face unchanged on opaque input; antialiased side strokes can affect alpha edges.
    if (unchanged.contains(id)) QCOMPARE(result.image, s);
    else QVERIFY2(result.image != s, qPrintable(id));
    EffectContext cancelled; cancelled.cancelled = [] { return true; };
    const auto stopped = run(id, s, p, cancelled);
    QCOMPARE(stopped.error, QString("effect-cancelled")); QVERIFY(stopped.image.isNull());
    // Cancel after work has begun, including conversion/geometry and row-map paths.
    int polls = 0; cancelled.cancelled = [&] { return ++polls >= 5; };
    const auto mid = run(id, s, p, cancelled);
    QCOMPARE(mid.error, QString("effect-cancelled")); QVERIFY(mid.image.isNull());
  }
  void neutralSettings() {
    const auto s = fixture();
    const QMap<QString, ParamValues> neutral{
      {"crop_image", {{"width", s.width()}, {"height", s.height()}}},
      {"resize_canvas", {}}, {"resize_image", {}}, {"rotate_custom_angle", {{"angle", 0.0}}},
      {"skew", {}}, {"perspective_warp", {}}, {"rotate_3d", {}}, {"rotate_3d_box", {{"depth", 0.0}}},
      {"fisheye_lens", {{"strength", 0.0}}}, {"pinch_bulge", {{"strength", 0.0}}},
      {"twirl", {{"angle", 0.0}}}, {"ripple_refraction", {{"amplitude", 0.0}}},
      {"cylinder_wrap", {{"curvature", 0.0}}}, {"fold_crease_warp", {{"fold_depth", 0.0}}},
      {"liquify_push_smudge", {{"distance", 0.0}}},
      {"mirror_tiles", {{"columns", 1}, {"rows", 1}, {"mirror_alternate_columns", false}, {"mirror_alternate_rows", false}}},
      {"displacement_map", {{"map_file_path", mapPath()}, {"amount_x", 0.0}, {"amount_y", 0.0}}}};
    for (auto i = neutral.begin(); i != neutral.end(); ++i) {
      const auto out = run(i.key(), s, i.value()); QVERIFY2(out.ok(), qPrintable(i.key())); QCOMPARE(out.image, s);
    }
    // Auto-crop has an image-dependent neutral case: foreground reaches every edge.
    QCOMPARE(run("auto_crop_image", s).image, s);
    QCOMPARE(run("auto_crop_image", solid(8, 8, 0)).image, solid(8, 8, 0));
    // Glass sphere's refraction formula is not identity at 1; wave's declared minimum is 1.
  }
  void canvasEquivalence() {
    const auto s = fixture(17, 11);
    ed::CanvasOperation op; op.action = ed::CanvasAction::Crop; op.start = {2, 3}; op.end = {14, 9};
    QCOMPARE(run("crop_image", s, {{"x", 2}, {"y", 3}, {"width", 12}, {"height", 6}}).image, canvas(s, op));
    QCOMPARE(run("crop_image", s, {{"x", 1000}}).image, s);
    op.action = ed::CanvasAction::ResizeImage; op.width = 7; op.height = 5; op.lockAspect = false; op.interpolation = ed::Interpolation::Bilinear;
    QCOMPARE(run("resize_image", s, {{"width", 7}, {"height", 5}}).image, canvas(s, op));
    // Largest-dimension aspect anchor: max(34/17,11/11)=2, hence 34x22.
    op.width = 34; op.height = 22;
    QCOMPARE(run("resize_image", s, {{"width", 34}, {"height", 11}, {"maintain_aspect_ratio", true}}).image, canvas(s, op));
    op.width = 34; op.height = 22;
    QCOMPARE(run("resize_image", s, {{"height", 22}, {"maintain_aspect_ratio", true}}).image, canvas(s, op));
    op.action = ed::CanvasAction::RotateCustom; op.interpolation = ed::Interpolation::Nearest;
    for (double angle : {-90., 0., 33., 90., 180., 270.}) for (bool expand : {false, true}) {
      op.rotationDegrees = angle; op.expandCanvas = expand;
      QCOMPARE(run("rotate_custom_angle", s, {{"angle", angle}, {"auto_resize", expand}}).image, canvas(s, op));
    }
    for (auto anchor : {ed::CanvasAnchor::TopLeft, ed::CanvasAnchor::Center, ed::CanvasAnchor::BottomRight}) {
      op.action = ed::CanvasAction::ResizeCanvas; op.width = 23; op.height = 15; op.anchor = anchor; op.fill = 0x80553311;
      const int left = anchor == ed::CanvasAnchor::TopLeft ? 0 : anchor == ed::CanvasAnchor::Center ? 3 : 6;
      const int top = anchor == ed::CanvasAnchor::TopLeft ? 0 : anchor == ed::CanvasAnchor::Center ? 2 : 4;
      QCOMPARE(run("resize_canvas", s, {{"left", left}, {"right", 6 - left}, {"top", top}, {"bottom", 4 - top},
                                         {"background_color", QColor::fromRgba(op.fill)}}).image, canvas(s, op));
    }
    op.action = ed::CanvasAction::ResizeCanvas; op.width = 13; op.height = 7; op.anchor = ed::CanvasAnchor::Center;
    QCOMPARE(run("resize_canvas", s, {{"left", -2}, {"right", -2}, {"top", -2}, {"bottom", -2},
        {"background_color", QColor::fromRgba(op.fill)}}).image, canvas(s, op));
    auto bordered = solid(9, 7, qRgba(10, 20, 30, 255));
    for (int y = 2; y < 6; ++y) for (int x = 1; x < 7; ++x) bordered.setPixel(x, y, qRgba(100, 90, 80, 255));
    op.action = ed::CanvasAction::AutoCrop; op.autoCrop.border = ed::CropBorder::TopLeftColor; op.autoCrop.tolerance = 3;
    op.autoCrop.includeAnnotations = false;
    const auto cropped = run("auto_crop_image", bordered, {{"tolerance", 3}}).image;
    QCOMPARE(cropped, canvas(bordered, op)); QCOMPARE(cropped.size(), QSize(6, 4));
    QCOMPARE(cropped.pixel(0, 0), bordered.pixel(1, 2));
  }
  void pixelMaps() {
    const auto s = ramp();
    // Resize 9->3 maps destination centre .5 to source centre 1.5, i.e. source pixel 1.
    QCOMPARE(run("resize_image", s, {{"width", 3}, {"height", 3}}).image.pixel(0, 0), s.pixel(1, 1));
    // Crop indices shift by the requested left/top without interpolation.
    QCOMPARE(run("crop_image", s, {{"x", 2}, {"y", 3}, {"width", 4}, {"height", 4}}).image.pixel(1, 1), s.pixel(3, 4));
    // Fisheye: radius=9, strength=100 -> exponent=3.4, dx=2 -> sx=4+9*(2/9)^3.4.
    auto out = run("fisheye_lens", s, {{"strength", 100.0}}).image;
    QCOMPARE(qRed(out.pixel(6, 4)), int(std::nearbyint(20 * (4 + 9 * std::pow(2.f / 9, 3.4f)))));
    // Bulge: radius=4.5, amount=1+.35*(1-(2/4.5)^2), sx=4+2/amount.
    out = run("pinch_bulge", s).image;
    QCOMPARE(qRed(out.pixel(6, 4)), int(std::nearbyint(20 * (4 + 2 / (1 + .35f * (1 - std::pow(2.f / 4.5f, 2)))))));
    // Twirl: radius=4.5, theta=-(pi/2)*(1-2/4.5), src=(4+2cos(theta),4+2sin(theta)).
    out = run("twirl", s).image;
    const float theta = -float(M_PI / 2) * (1 - 2.f / 4.5f);
    QCOMPARE(qRed(out.pixel(6, 4)), int(std::nearbyint(20 * (4 + 2 * std::cos(theta)))));
    QCOMPARE(qGreen(out.pixel(6, 4)), int(std::nearbyint(20 * (4 + 2 * std::sin(theta)))));
    // Ripple at r=2, wavelength=8: phase=pi/2, offset=2; sx=8.
    out = run("ripple_refraction", s, {{"wavelength", 8.0}, {"amplitude", 2.0}, {"refraction", 0.0}}).image;
    QCOMPARE(out.pixel(6, 4), s.pixel(8, 4));
    // Liquify centre has influence=1; distance=2, smudge=0 -> sample(2,4).
    out = run("liquify_push_smudge", s, {{"distance", 2.0}, {"smudge", 0.0}}).image;
    QCOMPARE(out.pixel(4, 4), s.pixel(2, 4));
    // Cartesian-to-polar: output(x=0,y=8) maps to centre+(radius=4,0).
    out = run("polar_warp", s).image; QCOMPARE(out.pixel(0, 8), s.pixel(8, 4));
    out = run("polar_warp", s, {{"mode", QString("polar_to_cartesian")}}).image;
    QCOMPARE(out.pixel(8, 4), s.pixel(0, 8)); QCOMPARE(out.pixel(0, 0), QRgb(0));
    // Eight segments fold angle pi to local angle 0, mapping (2,4) to (6,4).
    out = run("kaleidoscope", s).image; QCOMPARE(out.pixel(2, 4), s.pixel(6, 4));
    // Vertical cylinder edge has angle=-maxAngle; sx=0 and shade=1-.35*(1-cos(maxAngle))*.65.
    out = run("cylinder_wrap", s).image;
    const float shade = 1 - .35f * (1 - std::cos(.12f + .65f * 1.28f)) * .65f;
    QCOMPARE(qGreen(out.pixel(0, 4)), int(std::nearbyint(80 * shade)));
    // Fold at x=0: offset=0 and shade=1+.4*.08.
    out = run("fold_crease_warp", s).image; QCOMPARE(qGreen(out.pixel(0, 4)), int(std::nearbyint(80 * 1.032f)));
    // Glass: centre=(4.5,4.5), radius=2.25; preserve outside sphere.
    out = run("glass_sphere", s).image; QCOMPARE(out.pixel(0, 0), s.pixel(0, 0));
    const float d = std::sqrt(2.5f), radius = 2.25f, a = std::asin(d / radius);
    const float ratio = radius * std::sin(a * 1.5f) / std::sin(a + .0001f) / d;
    QCOMPARE(qRed(out.pixel(6, 5)), int(std::nearbyint(20 * (4.5f + 1.5f * ratio))));
    // Wave: wavelength=12, y=3 -> sin(pi/2)=1, x'=x+2, beyond edge transparent.
    out = run("wave_distortion", s, {{"amplitude", 2}, {"wavelength", 12}}).image;
    QCOMPARE(out.pixel(2, 3), s.pixel(4, 3)); QCOMPARE(out.pixel(8, 3), QRgb(0));
    // Mirror tiles: x=.5 centre -> u=(.5/9)*3=1/6; sample x=(1/6)*8.
    out = run("mirror_tiles", s).image; QCOMPARE(qRed(out.pixel(0, 0)), 27);
    QCOMPARE(out.pixel(0, 0), out.pixel(5, 0));
    // Repeat tiles: first pixel maps to .5*3-.5=1 on a 9px source.
    out = run("tile_repeat", s).image; QCOMPARE(out.pixel(0, 0), s.pixel(1, 1));
    // Symmetry copies opposite whole pixel centres (x'=w-1-x).
    for (const QString direction : {"left_to_right", "right_to_left", "top_to_bottom", "bottom_to_top"}) {
      out = run("symmetry", s, {{"direction", direction}}).image;
      const bool horizontal = direction.contains("left");
      QCOMPARE(out.pixel(0, 0), horizontal ? out.pixel(8, 0) : out.pixel(0, 8));
    }
  }
  void projectivePixelsAndBounds() {
    const auto s = ramp(8, 6);
    // Uniform perspective offsets (1,0) produce translation: dst(3,2)=src(2,2).
    ParamValues translated{{"top_left_x", 1}, {"top_right_x", 1}, {"bottom_right_x", 1}, {"bottom_left_x", 1}};
    auto out = run("perspective_warp", s, translated).image;
    QCOMPARE(out.size(), s.size()); QCOMPARE(out.pixel(3, 2), s.pixel(2, 2)); QCOMPARE(out.pixel(0, 2), QRgb(0));
    // At 45 degrees horizontal shear, inverse boundary map gives sx=3.5-2.5=1; nearest samples pixel 1.
    out = run("skew", s, {{"horizontally", 45.0}, {"auto_resize", false}}).image;
    QCOMPARE(out.size(), s.size()); QVERIFY(qRed(out.pixel(3, 2)) == 20);
    QVERIFY(run("skew", s, {{"horizontally", 45.0}}).image.width() > s.width());
    // Z rotation 180 maps boundary (x+.5,y+.5) to (w-x-.5,h-y-.5).
    out = run("rotate_3d", s, {{"rotate_z", 180.0}, {"auto_resize", false}}).image;
    QCOMPARE(out.size(), s.size()); QCOMPARE(out.pixel(2, 2), s.pixel(5, 3));
    // Depth-zero box has the same geometry; cubic/bilinear front sampling differs from the plain plane's nearest sampling.
    const ParamValues angles{{"rotate_x", 25.0}, {"rotate_y", -30.0}, {"rotate_z", 10.0}, {"auto_resize", false}};
    const auto flat = run("rotate_3d", s, angles);
    ParamValues box = angles; box["depth"] = 0.0;
    const auto extruded = run("rotate_3d_box", s, box);
    QVERIFY(flat.ok()); QVERIFY(extruded.ok());
    // Side strokes can touch the silhouette, but the front interior must agree.
    QCOMPARE(flat.image.size(), extruded.image.size());
    QVERIFY(std::abs(qRed(flat.image.pixel(3, 2)) - qRed(extruded.image.pixel(3, 2))) <= 20);
    box["depth"] = 15.0;
    QVERIFY(run("rotate_3d_box", s, box).image != extruded.image);
    // Every corner at the same point is singular; reference retains the source.
    const auto tiny = ramp(3, 3);
    QCOMPARE(run("perspective_warp", tiny, {{"top_right_x", -2}, {"bottom_right_x", -2},
          {"bottom_right_y", -2}, {"bottom_left_y", -2}}).image, tiny);
  }
  void displacementFileAndPixels() {
    const auto s = ramp();
    // Red=255 -> dx=+amountX; green=0 -> dy=-amountY. Clamp at source edges.
    auto out = run("displacement_map", s, {{"map_file_path", mapPath()}, {"amount_x", 1.5}, {"amount_y", 2.0}});
    QVERIFY(out.ok()); QCOMPARE(out.image.pixel(2, 4), qRgba(70, 40, 40, 255));
    QCOMPARE(out.image.pixel(8, 0), s.pixel(8, 0));
    for (const QString path : {QString(), dir.filePath("missing.png"), dir.path()}) {
      auto result = run("displacement_map", s, {{"map_file_path", path}});
      QCOMPARE(result.error, QString("effect-invalid-input")); QVERIFY(result.image.isNull());
      // Missing map remains invalid even when displacement amounts are neutral.
      QCOMPARE(run("displacement_map", s, {{"map_file_path", path}, {"amount_x", 0.0}, {"amount_y", 0.0}}).error,
               QString("effect-invalid-input"));
    }
    // Straight-alpha bilinear interpolation at x=.5 averages channels independently: red/blue=128, alpha=128.
    auto alpha = solid(2, 1, qRgba(0, 0, 255, 255)); alpha.setPixel(0, 0, qRgba(255, 0, 0, 0));
    const auto interpolated = run("displacement_map", alpha,
        {{"map_file_path", mapPath()}, {"amount_x", .5}, {"amount_y", 0.0}});
    QCOMPARE(interpolated.image.pixel(0, 0), qRgba(128, 0, 128, 128));
    // Symmetry draws the reflected half over existing pixels, preserving reference alpha compositing.
    alpha.setPixel(0, 0, qRgba(255, 0, 0, 128));
    QCOMPARE(run("symmetry", alpha).image.pixel(1, 0), qRgba(128, 0, 127, 255));
    const QString bad = dir.filePath("corrupt.png"); QFile f(bad); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("invalid"); f.close();
    QCOMPARE(run("displacement_map", s, {{"map_file_path", bad}}).error, QString("effect-invalid-input"));
  }
  void seededShuffleAndRegions() {
    const auto s = fixture(64, 64);
    EffectContext a; a.seed = 41; EffectContext b; b.seed = 42;
    const auto first = run("tile_shuffle", s, {}, a), repeat = run("tile_shuffle", s, {}, a), other = run("tile_shuffle", s, {}, b);
    QVERIFY(first.ok()); QCOMPARE(first.image, repeat.image); QVERIFY(first.image != other.image);
    // Each destination tile comes from one source tile; opaque source keeps the exact multiset at gap=0.
    const auto opaque = ramp(8, 8);
    const auto shuffled = run("tile_shuffle", opaque, {{"columns", 2}, {"rows", 2}}, a).image;
    QList<QRgb> before, after;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) { before << opaque.pixel(x, y); after << shuffled.pixel(x, y); }
    std::sort(before.begin(), before.end()); std::sort(after.begin(), after.end()); QCOMPARE(before, after);
    const QColor gap(25, 50, 75, 255);
    const auto gapped = run("tile_shuffle", s, {{"gap", 4.0}, {"gap_color", gap}}, a).image;
    QCOMPARE(gapped.pixel(0, 0), gap.rgba()); QCOMPARE(gapped.pixel(16, 20), gap.rgba());
    QVERIFY(gapped.pixel(8, 8) != gap.rgba());
    // Localized warps preserve corners and alter a proper subset of a textured image.
    for (const QString id : {"glass_sphere", "twirl", "pinch_bulge", "liquify_push_smudge", "page_curl"}) {
      const auto out = run(id, s).image;
      const int n = changed(s, out); QVERIFY2(n > 0 && n < s.width() * s.height(), qPrintable(id));
      QCOMPARE(out.pixel(0, 0), s.pixel(0, 0));
    }
    // Flat areas remain flat under unshaded inverse maps, including interpolation of alpha.
    const auto flat = solid(64, 64, qRgba(90, 110, 130, 160));
    for (const QString id : {"fisheye_lens", "twirl", "pinch_bulge", "liquify_push_smudge", "kaleidoscope", "mirror_tiles", "ripple_refraction"})
      QCOMPARE(run(id, flat).image, flat);
    // Fold shading creates variance on flat input; cylinder shades edges while preserving the centre column.
    const auto gray = solid(65, 65, qRgba(100, 100, 100, 255));
    const auto folded = run("fold_crease_warp", gray).image;
    int minimum = 255, maximum = 0;
    for (int x = 0; x < gray.width(); ++x) { minimum = std::min(minimum, qRed(folded.pixel(x, 32))); maximum = std::max(maximum, qRed(folded.pixel(x, 32))); }
    QVERIFY(maximum - minimum > 5);
    const auto cylinder = run("cylinder_wrap", gray).image;
    QCOMPARE(cylinder.pixel(32, 32), gray.pixel(32, 32)); QVERIFY(qRed(cylinder.pixel(0, 32)) < 100);
    // Page curl affects only its selected corner; the opposite corner and central region remain untouched.
    const auto curl = run("page_curl", gray).image;
    QCOMPARE(curl.pixel(0, 0), gray.pixel(0, 0)); QCOMPARE(curl.pixel(32, 32), gray.pixel(32, 32));
    QVERIFY(curl.pixel(64, 64) != gray.pixel(64, 64));
    const auto upper = run("page_curl", gray, {{"corner", QString("top_left")}}).image;
    QVERIFY(upper.pixel(0, 0) != gray.pixel(0, 0)); QCOMPARE(upper.pixel(64, 64), gray.pixel(64, 64));
  }
  void smallImagesAndGeometryLimits() {
    for (const QSize size : {QSize(1, 1), QSize(1, 9), QSize(9, 1), QSize(2, 2)}) {
      const auto s = fixture(size.width(), size.height());
      for (const auto &id : ids) {
        ParamValues p; if (id == "displacement_map") p["map_file_path"] = mapPath();
        const auto result = run(id, s, p); QVERIFY2(result.ok(), qPrintable(id + ":" + result.error));
        QCOMPARE(result.image.colorSpace(), s.colorSpace());
      }
    }
    const auto s = fixture(8, 8);
    QCOMPARE(run("resize_canvas", s, {{"left", -100}}).image, s);
    // Entire source outside new canvas produces only the chosen fill, through shared pad/crop operations.
    const QColor fill(1, 2, 3, 4);
    const auto out = run("resize_canvas", s, {{"left", -12}, {"right", 20}, {"background_color", fill}});
    QVERIFY(out.ok()); QCOMPARE(out.image, solid(16, 8, fill.rgba()));
  }
};
QTEST_GUILESS_MAIN(ManipulationEffects2Test)
#include "ManipulationEffects2Test.moc"
