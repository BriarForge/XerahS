// EDITOR-EFFECTS-001, EE-008..EE-010: regression lock, not a correctness oracle.
#include "image-editor/effects/EffectSupport.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPainter>
#include <QPainterPath>
#include <QTest>
using namespace xerahs::effects;
namespace {
QImage fixture(int index) {
  const QSize sizes[]{QSize(32, 24), QSize(64, 48), QSize(17, 9)};
  QImage image(sizes[index], QImage::Format_ARGB32);
  quint32 state = 0x12345678;
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      state = state * 1664525u + 1013904223u;
      const int n = int(state >> 24);
      const int r = x * 255 / (image.width() - 1), g = y * 255 / (image.height() - 1);
      image.setPixel(x, y, index == 1 ? qRgba((r * 3 + n) / 4, (g * 3 + n) / 4, (r + g + n * 2) / 4, 255)
          : qRgba(r, g, (x * 31 + y * 17) % 256, (x * 19 + y * 23) % 256));
    }
  return image;
}
QString hash(const QImage &image) {
  QCryptographicHash digest(QCryptographicHash::Sha256);
  digest.addData(QByteArray::number(image.width()) + "x" + QByteArray::number(image.height()) + ":");
  for (int y = 0; y < image.height(); ++y)
    for (int x = 0; x < image.width(); ++x) {
      const QRgb p = image.pixel(x, y);
      const char rgba[]{char(qRed(p)), char(qGreen(p)), char(qBlue(p)), char(qAlpha(p))};
      digest.addData(QByteArrayView(rgba, 4));
    }
  return QString::fromLatin1(digest.result().toHex());
}
// The glyph plan is frozen independently of the renderer: rational averages
// of the input fixture determine its characters and RGBA values. Only glyph
// rasterization is delegated to the host's native Qt/font stack. A workstation
// font's pixel hash cannot describe every supported Linux installation.
QImage nativeAsciiGolden(const QJsonObject &golden, int index) {
  const auto fixtures = golden.value("fixtures").toArray();
  if (fixtures.size() != 3) return {};
  const auto plan = fixtures[index].toObject();
  QImage expected(plan.value("width").toInt(), plan.value("height").toInt(), QImage::Format_ARGB32_Premultiplied);
  if (expected.isNull()) return {};
  expected.fill(QColor(golden.value("background").toString()));
  QPainter painter(&expected);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  font.setPixelSize(golden.value("font_pixel_size").toInt());
  painter.setFont(font);
  const auto cells = plan.value("cells").toArray();
  if (cells.size() != ((expected.width() + 7) / 8) * ((expected.height() + 7) / 8)) return {};
  for (const auto &value : cells) {
    const auto cell = value.toObject(); const auto rgba = cell.value("rgba").toArray();
    if (rgba.size() != 4 || cell.value("glyph").toString().size() != 1) return {};
    painter.setPen(QColor(rgba[0].toInt(), rgba[1].toInt(), rgba[2].toInt(), rgba[3].toInt()));
    painter.drawText(QPointF(cell.value("x").toInt(), float(cell.value("y").toInt()) + float(golden.value("baseline_offset").toDouble())), cell.value("glyph").toString());
  }
  painter.end();
  return expected.convertToFormat(QImage::Format_ARGB32);
}
// Fixed default drawing instructions, independent of the effect registry and
// its parameter/placement helpers. Native Qt supplies the environmental glyph
// outlines and metrics; text, colors, sizes, padding and placement stay locked.
QImage nativeTextGolden(const QImage &source, bool watermark) {
  QFont font;
  font.setFamily(QFontDatabase::families().contains("Arial", Qt::CaseInsensitive) ? QStringLiteral("Arial") : QGuiApplication::font().family());
  font.setPixelSize(watermark ? 15 : 36); font.setBold(false); font.setItalic(false);
  const QFontMetricsF metrics(font);
  QImage expected = source.copy();
  QPainter painter(&expected); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::SmoothPixmapTransform);
  if (!watermark) {
    QPainterPath text; text.setFillRule(Qt::WindingFill); text.addText(0, metrics.ascent(), font, "Text");
    const auto bounds = text.boundingRect();
    text.translate(-bounds.left(), -bounds.top()); painter.fillPath(text, QColor(235, 235, 235));
  } else {
    const int width = int(std::ceil(metrics.horizontalAdvance("Text watermark") + 10));
    const int height = int(std::ceil(std::max(metrics.ascent() + metrics.descent(), 15.0) + 10));
    const QRectF box(source.width() - width - 5, source.height() - height - 5, width, height);
    const double radius = std::min(4.0, std::min(width, height) / 2.0);
    painter.setPen(Qt::NoPen); painter.setBrush(QColor(42, 47, 56)); painter.drawRoundedRect(box, radius, radius);
    painter.setPen(QPen(Qt::black, 1)); painter.setBrush(Qt::NoBrush); painter.drawRoundedRect(box, radius, radius);
    painter.setFont(font); painter.setPen(QColor(235, 235, 235));
    painter.drawText(QPointF(box.left() + 5, box.top() + 5 + metrics.ascent()), "Text watermark");
  }
  painter.end();
  return expected;
}
// Original two-buffer algorithms, retained independently to check scratch-buffer reuse.
std::vector<float> referenceBlur(std::vector<float> input, int w, int h, float sigma, bool clampEdges, bool normalized) {
  const int radius = normalized ? std::max(1, int(std::ceil(double(sigma) * 3.0)))
                                : std::max(1, int(std::ceil(sigma * 3.f)));
  std::vector<float> kernel(size_t(radius * 2 + 1));
  const float sigmaSq = sigma * sigma;
  float weight = 0.f;
  for (int k = -radius; k <= radius; ++k) {
    const float v = normalized ? std::exp(-0.5f * float(k * k) / sigmaSq)
                               : std::exp(-float(k * k) / (2.f * sigma * sigma));
    kernel[size_t(k + radius)] = v;
    weight += v;
  }
  for (float &v : kernel) v /= weight;
  std::vector<float> tmp(input.size());
  for (int pass = 0; pass < 2; ++pass) {
    const auto &src = pass == 0 ? input : tmp;
    auto &dst = pass == 0 ? tmp : input;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 4; ++c) {
          float sum = 0.f;
          for (int k = -radius; k <= radius; ++k) {
            int sx = x + (pass == 0 ? k : 0), sy = y + (pass == 1 ? k : 0);
            float sample = 0.f;
            if (sx < 0 || sx >= w || sy < 0 || sy >= h) {
              if (!clampEdges && !normalized) continue;
              if (clampEdges) { sx = std::clamp(sx, 0, w - 1); sy = std::clamp(sy, 0, h - 1); }
            }
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) sample = src[(size_t(sy) * w + sx) * 4 + c];
            sum += normalized ? kernel[size_t(k + radius)] * sample : sample * kernel[size_t(k + radius)];
          }
          dst[(size_t(y) * w + x) * 4 + c] = sum;
        }
  }
  return input;
}

}
class EffectGoldenHashTest : public QObject {
  Q_OBJECT
private slots:
  void blurScratchMatchesOriginal() {
    using namespace support;
    for (const QSize size : {QSize(1, 1), QSize(1, 9), QSize(17, 1), QSize(17, 9)}) {
      const QImage source = fixture(2).copy(QRect(QPoint(), size));
      for (float sigma : {0.01f, 0.3f, 1.f, 1.3333334f, 3.25f, 12.f}) {
        for (bool clampEdges : {false, true}) {
          FloatImage floats = toPremul(source);
          const auto expected = referenceBlur(floats.d, floats.w, floats.h, sigma, clampEdges, false);
          QVERIFY(gaussianBlur255(floats, sigma, clampEdges, {}));
          QVERIFY(floats.d == expected);
          std::vector<float> normalized(size_t(source.width()) * source.height() * 4);
          for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x) {
              const QRgb p = source.pixel(x, y);
              float *v = &normalized[(size_t(y) * source.width() + x) * 4];
              const float a = qAlpha(p) / 255.f;
              v[0] = qRed(p) / 255.f * a; v[1] = qGreen(p) / 255.f * a;
              v[2] = qBlue(p) / 255.f * a; v[3] = a;
            }
          normalized = referenceBlur(normalized, source.width(), source.height(), sigma, clampEdges, true);
          const auto actual = approximateGaussianBlur(source, sigma, clampEdges ? BlurBorder::Clamp : BlurBorder::Decal, {});
          QVERIFY(!actual.isNull());
          for (int y = 0; y < actual.height(); ++y)
            for (int x = 0; x < actual.width(); ++x) {
              const float *v = &normalized[(size_t(y) * actual.width() + x) * 4];
              auto ch = [&](float p) { return int(std::nearbyintf(std::clamp(p / v[3], 0.f, 1.f) * 255.f)); };
              const QRgb pixel = v[3] <= 1e-6f ? 0 : qRgba(ch(v[0]), ch(v[1]), ch(v[2]),
                  int(std::nearbyintf(std::clamp(v[3], 0.f, 1.f) * 255.f)));
              QCOMPARE(actual.pixel(x, y), pixel);
            }
        }
      }
    }
  }
  void allocationFailureStaysTyped() {
    support::FunctionEffect effect({}, [](const QImage &, const ParamValues &, const EffectContext &) -> EffectResult {
      throw std::bad_alloc();
    });
    const auto result = effect.apply(fixture(0), {}, {});
    QCOMPARE(result.error, support::kFailed);
    QVERIFY(result.image.isNull());
  }
  void everyRegisteredEffect() {
    QFile nativeFile(QFileInfo(EFFECT_GOLDEN_FIXTURE).dir().filePath("ascii-art-native-glyphs.json"));
    QVERIFY(nativeFile.open(QIODevice::ReadOnly));
    QJsonParseError nativeError;
    const auto nativeGolden = QJsonDocument::fromJson(nativeFile.readAll(), &nativeError).object();
    QCOMPARE(nativeError.error, QJsonParseError::NoError);
    const auto nativeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    qInfo() << "Native ASCII golden font:" << QFontInfo(nativeFont).family() << "Qt:" << qVersion();
    QJsonObject actual, nativeOutputs;
    for (const auto *spec : builtinEffects().specs()) {
      QJsonArray outputs, native;
      for (int i = 0; i < 3; ++i) {
        EffectContext context;
        context.seed = 0;
        // Keep the reference and effect in the same native-font context. In
        // Qt 6.4, painting another point size can change cached glyph outlines;
        // pre-rendering every reference upfront would alter that environment.
        QImage reference;
        if (spec->id == "ascii_art") reference = nativeAsciiGolden(nativeGolden, i);
        else if (spec->id == "draw_text" || spec->id == "text_watermark")
          reference = nativeTextGolden(fixture(i), spec->id == "text_watermark");
        if (spec->id == "ascii_art" || spec->id == "draw_text" || spec->id == "text_watermark") {
          QVERIFY(!reference.isNull()); QCOMPARE(reference.size(), fixture(i).size());
          native.append(QJsonObject{{"error", ""}, {"sha256", hash(reference)}});
        }
        const auto result = runEffect(*builtinEffects().find(spec->id), fixture(i), {}, context);
        outputs.append(QJsonObject{{"error", result.error}, {"sha256", hash(result.image)}});
      }
      actual.insert(spec->id, outputs);
      if (!native.isEmpty()) nativeOutputs.insert(spec->id, native);
    }
    QFile file(EFFECT_GOLDEN_FIXTURE);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const auto expected = document.object().value("effects").toObject();
    const auto nativeOracles = document.object().value("native_font_oracles").toObject();
    QCOMPARE(nativeOracles.keys(), QStringList({"ascii_art", "draw_text", "text_watermark"}));
    QCOMPARE(nativeOutputs.keys(), nativeOracles.keys());
    // Every locked effect must still exist and match. Effects registered after the lock
    // was taken are covered by their own tests until the lock is deliberately refreshed.
    QStringList mismatches;
    for (auto it = expected.begin(); it != expected.end(); ++it) {
      if (!actual.contains(it.key())) { mismatches.append(QStringLiteral("locked effect missing: ") + it.key()); continue; }
      const QJsonValue oracle = nativeOutputs.contains(it.key()) ? nativeOutputs.value(it.key()) : it.value();
      if (actual.value(it.key()) != oracle) mismatches.append(it.key());
    }
    QVERIFY2(mismatches.isEmpty(), qPrintable(mismatches.join(", ")));
  }
};
QTEST_MAIN(EffectGoldenHashTest)
#include "EffectGoldenHashTest.moc"
