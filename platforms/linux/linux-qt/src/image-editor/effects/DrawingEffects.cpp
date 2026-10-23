// EDITOR-EFFECTS-001: the Drawings category. EE-003, EE-007, EE-008..EE-010, EE-016, EE-018.
// Behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3); each effect's census
// source path is recorded in EffectSpec::referencePath.
//
// Declared deviations from the reference (EE-018: none of these is a silent change):
//  - Randomness comes only from EffectContext::seed (SplitMix64), never Random.Shared or the clock.
//  - A FilePath that is empty, missing, unreadable, not an image or larger than 16384 px a side returns
//    effect-invalid-input; the reference silently returns an unchanged copy.
//  - Text uses QPainter/QFont. A font family that is not installed falls back to the application default
//    font (QGuiApplication::font()); with no QGuiApplication the text effects return effect-unavailable.
//  - Text-watermark variables: only %width, %height and %n are expanded. Clock, user, machine and
//    environment-variable tokens stay literal so apply() stays pure and repeatable (EE-008, EE-016).
//  - File paths are used literally; environment variables are not expanded (EE-016).
//  - Qt has one smooth image sampler, so every non-"nearest_neighbor" interpolation mode uses it.
//  - Snowflake glow is a radial-gradient falloff standing in for Skia's blur mask filter; the gradient
//    overlay applies the W3C Overlay blend maths directly.
#include "Effect.h"
#include "EffectSupport.h"

#include <QBrush>
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRadialGradient>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace xerahs::effects {
namespace {
using namespace support;
const QString kReferenceRoot = QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/");

// ---- shared helpers ------------------------------------------------------------
QStringList placementChoices() {
  return {"top_left", "top_center", "top_right", "middle_left", "middle_center", "middle_right",
          "bottom_left", "bottom_center", "bottom_right"};
}

QImage copyOf(const QImage &source) {
  QImage result = source.copy();
  result.setColorSpace(source.colorSpace());
  return result;
}
QImage transparentLike(const QImage &source) {
  QImage layer = makeResult(source);
  if (!layer.isNull()) layer.fill(Qt::transparent);
  return layer;
}
void initPainter(QPainter &painter, bool smooth = true) {
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
}
// The effect's drawing surface: the source itself, or (Background) an empty layer that the source
// is composited over once the drawing is done.
QImage startCanvas(const QImage &source, bool background) {
  return background ? transparentLike(source) : copyOf(source);
}
void finishCanvas(QImage &canvas, const QImage &source, bool background) {
  if (!background) return;
  QPainter painter(&canvas);
  painter.drawImage(0, 0, source);
}

void blit(QImage &destination, const QImage &source, int x, int y) {
  for (int row = 0; row < source.height(); ++row) {
    const int dy = y + row;
    if (dy < 0 || dy >= destination.height()) continue;
    std::memcpy(destination.scanLine(dy) + qsizetype(x) * 4, source.constScanLine(row), size_t(source.width()) * 4);
  }
}

QPoint placementPosition(const QString &placement, QPoint offset, QSize background, QSize object) {
  const int midX = int(std::nearbyintf(background.width() / 2.f - object.width() / 2.f));
  const int midY = int(std::nearbyintf(background.height() / 2.f - object.height() / 2.f));
  const int right = background.width() - object.width();
  const int bottom = background.height() - object.height();
  const int index = placementChoices().indexOf(placement);
  switch (index) {
  case 1: return {midX, offset.y()};
  case 2: return {right - offset.x(), offset.y()};
  case 3: return {offset.x(), midY};
  case 4: return {midX, midY};
  case 5: return {right - offset.x(), midY};
  case 6: return {offset.x(), bottom - offset.y()};
  case 7: return {midX, bottom - offset.y()};
  case 8: return {right - offset.x(), bottom - offset.y()};
  default: return offset;
  }
}
bool contains(const QRect &outer, const QRect &inner) {
  return inner.left() >= outer.left() && inner.top() >= outer.top() && inner.x() + inner.width() <= outer.x() + outer.width() &&
         inner.y() + inner.height() <= outer.y() + outer.height();
}
bool intersects(const QRect &a, const QRect &b) {
  return a.left() < b.x() + b.width() && a.x() + a.width() > b.left() && a.top() < b.y() + b.height() &&
         a.y() + a.height() > b.top();
}

int nextInt(SplitMix64 &rng, int min, int max) {  // reference NextInt: exclusive max
  if (min == max) return min;
  if (min > max) std::swap(min, max);
  return rng.range(min, max);
}

EffectResult identityCopy(const QImage &source, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  return {copyOf(source), {}};
}

// ---- border / candy cane --------------------------------------------------------
EffectResult border(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const bool outside = choiceOf(params, "type") == "outside";
  const int size = intOf(params, "size");
  const QString dash = choiceOf(params, "dash_style");
  const QColor color = colorOf(params, "color");
  const int newWidth = outside ? source.width() + size * 2 : source.width();
  const int newHeight = outside ? source.height() + size * 2 : source.height();
  QImage result(newWidth, newHeight, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  result.fill(Qt::transparent);
  blit(result, source, outside ? size : 0, outside ? size : 0);
  if (context.cancelled()) return {{}, kCancelled};

  QPainter painter(&result);
  initPainter(painter);
  QPen pen(color, size, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
  // Skia intervals are absolute; QPen dash patterns are in pen widths, so the same ratios apply.
  if (dash == "dash") pen.setDashPattern({3, 1});
  else if (dash == "dot") pen.setDashPattern({1, 1});
  else if (dash == "dash_dot") pen.setDashPattern({3, 1, 1, 1});
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  const double half = size / 2.0;
  painter.drawRect(outside ? QRectF(half, half, newWidth - size, newHeight - size)
                           : QRectF(half, half, source.width() - size, source.height() - size));
  return {result, {}};
}

float smoothStep(float edge0, float edge1, float x) {
  float t = (x - edge0) / (edge1 - edge0);
  t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
  return t * t * (3.f - 2.f * t);
}

EffectResult candyCaneBorder(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int bw = std::clamp(intOf(params, "border_width"), 1, 500);
  const float sw = std::clamp(floatOf(params, "stripe_width"), 1.f, 200.f);
  const float glossAmount = std::clamp(floatOf(params, "gloss"), 0.f, 100.f) / 100.f;
  const bool rounded = boolOf(params, "rounded_corners");
  const QColor a = colorOf(params, "color_a"), b = colorOf(params, "color_b");
  const int newWidth = source.width() + bw * 2, newHeight = source.height() + bw * 2;
  QImage result(newWidth, newHeight, QImage::Format_ARGB32);
  if (result.isNull()) return {{}, kFailed};
  result.setColorSpace(source.colorSpace());
  result.fill(Qt::transparent);

  const float aR = a.red() / 255.f, aG = a.green() / 255.f, aB = a.blue() / 255.f;
  const float bR = b.red() / 255.f, bG = b.green() / 255.f, bB = b.blue() / 255.f;
  const float cornerRadius = rounded ? bw * 0.4f : 0.f;
  const float imgW = float(source.width()), imgH = float(source.height());
  const float innerL = float(bw), innerT = float(bw);
  const float innerR = float(bw + source.width() - 1), innerB = float(bw + source.height() - 1);

  const bool done = runRows(context, newHeight, 0, 95, [&](int y) {
    auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
    const bool inTop = y < bw, inBottom = y >= bw + source.height();
    for (int x = 0; x < newWidth; ++x) {
      const bool inLeft = x < bw, inRight = x >= bw + source.width();
      if (!inTop && !inBottom && !inLeft && !inRight) continue;
      const float clampedX = std::clamp(float(x), innerL, innerR);
      const float clampedY = std::clamp(float(y), innerT, innerB);
      const float distToInner = std::sqrt((x - clampedX) * (x - clampedX) + (y - clampedY) * (y - clampedY));
      const float crossRatio = std::clamp(distToInner / bw, 0.f, 1.f);
      float along;
      if (clampedY == innerT && clampedX > innerL) along = clampedX - innerL;
      else if (clampedX == innerR && clampedY > innerT) along = imgW + (clampedY - innerT);
      else if (clampedY == innerB && clampedX < innerR) along = imgW + imgH + (innerR - clampedX);
      else if (clampedX == innerL && clampedY < innerB) along = imgW * 2.f + imgH + (innerB - clampedY);
      else if (clampedX == innerR && clampedY == innerT) along = imgW;
      else if (clampedX == innerR && clampedY == innerB) along = imgW + imgH;
      else if (clampedX == innerL && clampedY == innerB) along = imgW * 2.f + imgH;
      else along = 0.f;

      if (rounded && cornerRadius > 0) {
        const bool inCornerX = (x < cornerRadius) || (x > newWidth - 1 - cornerRadius);
        const bool inCornerY = (y < cornerRadius) || (y > newHeight - 1 - cornerRadius);
        if (inCornerX && inCornerY) {
          const float outerCx = x < cornerRadius ? cornerRadius : newWidth - 1 - cornerRadius;
          const float outerCy = y < cornerRadius ? cornerRadius : newHeight - 1 - cornerRadius;
          const float distToOuter = std::sqrt((x - outerCx) * (x - outerCx) + (y - outerCy) * (y - outerCy));
          if (distToOuter > cornerRadius) {
            row[x] = 0;
            continue;
          }
        }
      }

      const float stripePhase = (along + crossRatio * bw) / sw;
      float stripeT = (std::sin(stripePhase * 3.14159265358979f * 2.f) + 1.f) * 0.5f;
      stripeT = stripeT < 0.5f ? smoothStep(0.15f, 0.45f, stripeT) : 1.f - smoothStep(0.55f, 0.85f, 1.f - stripeT);
      float r = aR + (bR - aR) * stripeT, g = aG + (bG - aG) * stripeT, bl = aB + (bB - aB) * stripeT;
      if (glossAmount > 0.001f) {
        const float centered = (crossRatio - 0.5f) * 2.f;
        const float glossHighlight = std::exp(-3.f * centered * centered);
        const float darkEdge = 1.f - 0.15f * (1.f - glossHighlight);
        const float combined = darkEdge + glossHighlight * 0.25f * glossAmount;
        r = std::clamp(r * combined, 0.f, 1.f);
        g = std::clamp(g * combined, 0.f, 1.f);
        bl = std::clamp(bl * combined, 0.f, 1.f);
      }
      row[x] = qRgba(int(std::nearbyintf(r * 255.f)), int(std::nearbyintf(g * 255.f)), int(std::nearbyintf(bl * 255.f)), 255);
    }
  });
  if (!done) return {{}, kCancelled};
  blit(result, source, bw, bw);
  return {result, {}};
}

// ---- backgrounds ----------------------------------------------------------------
EffectResult drawBackground(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const QRgb color = colorOf(params, "color").rgba();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, source.height(), 0, 100, [&](int y) {
        const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < source.width(); ++x) out[x] = sourceOverTop(in[x], color);
      }))
    return {{}, kCancelled};
  return {result, {}};
}

EffectResult drawCheckerboard(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const int size = intOf(params, "size");
  const QRgb c1 = colorOf(params, "color").rgba(), c2 = colorOf(params, "color2").rgba();
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, source.height(), 0, 100, [&](int y) {
        const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
          // Cell (0,0) and its diagonal use Color; the other two cells use Color 2.
          const QRgb back = (((x / size) + (y / size)) % 2 == 0) ? c1 : c2;
          out[x] = sourceOverTop(in[x], back);
        }
      }))
    return {{}, kCancelled};
  return {result, {}};
}

EffectResult drawBackgroundImage(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const QImage background = loadImageFile(params.value("image_file_path").toString());
  if (background.isNull()) return {{}, kInvalidInput};
  const bool center = boolOf(params, "center"), tile = boolOf(params, "tile");
  QImage result = transparentLike(source);
  if (result.isNull()) return {{}, kFailed};
  {
    QPainter painter(&result);
    initPainter(painter);
    if (tile) {
      int tileX = 0, tileY = 0;
      if (center) {
        tileX = (result.width() - background.width()) / 2 % background.width();
        tileY = (result.height() - background.height()) / 2 % background.height();
      }
      painter.translate(tileX, tileY);
      painter.fillRect(QRect(-tileX, -tileY, result.width(), result.height()), QBrush(background));
    } else {
      const float aspect = float(background.width()) / background.height();
      int width = result.width();
      int height = int(width / aspect);
      if (height < result.height()) {
        height = result.height();
        width = int(height * aspect);
      }
      const int x = center ? (result.width() - width) / 2 : 0;
      const int y = center ? (result.height() - height) / 2 : 0;
      painter.drawImage(QRectF(x, y, width, height), background);
    }
    if (context.cancelled()) return {{}, kCancelled};
    painter.drawImage(0, 0, source);
  }
  return {result, {}};
}

// ---- draw image -----------------------------------------------------------------
QImage rotateFlip(const QImage &image, const QString &mode) {
  auto rotate90 = [](const QImage &i) { return i.transformed(QTransform().rotate(90)); };
  if (mode == "rotate_90") return rotate90(image);
  if (mode == "rotate_180") return rotate90(rotate90(image));
  if (mode == "rotate_270") return rotate90(rotate90(rotate90(image)));
  if (mode == "flip_x") return image.flipped(Qt::Horizontal);
  if (mode == "flip_y") return image.flipped(Qt::Vertical);
  if (mode == "rotate_90_flip_x") return rotate90(image).flipped(Qt::Horizontal);
  if (mode == "rotate_90_flip_y") return rotate90(image).flipped(Qt::Vertical);
  return image;
}
QSize applyAspectRatio(int width, int height, const QImage &bitmap) {
  if (width == 0) return {int(std::nearbyintf(float(height) / bitmap.height() * bitmap.width())), height};
  if (height == 0) return {width, int(std::nearbyintf(float(width) / bitmap.width() * bitmap.height()))};
  return {width, height};
}

EffectResult drawImage(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage overlay = loadImageFile(params.value("image_location").toString());
  if (overlay.isNull()) return {{}, kInvalidInput};
  const int opacity = intOf(params, "opacity");
  const QString sizeMode = choiceOf(params, "size_mode");
  const int sizeW = intOf(params, "size_width"), sizeH = intOf(params, "size_height");
  if (opacity < 1 || (sizeMode != "dont_resize" && sizeW <= 0 && sizeH <= 0)) return identityCopy(source, context);
  overlay = rotateFlip(overlay, choiceOf(params, "rotate_flip"));

  QSize imageSize;
  if (sizeMode == "absolute_size")
    imageSize = applyAspectRatio(sizeW == -1 ? source.width() : sizeW, sizeH == -1 ? source.height() : sizeH, overlay);
  else if (sizeMode == "percentage_of_watermark")
    imageSize = applyAspectRatio(int(std::nearbyintf(sizeW / 100.f * overlay.width())),
                                 int(std::nearbyintf(sizeH / 100.f * overlay.height())), overlay);
  else if (sizeMode == "percentage_of_canvas")
    imageSize = applyAspectRatio(int(std::nearbyintf(sizeW / 100.f * source.width())),
                                 int(std::nearbyintf(sizeH / 100.f * source.height())), overlay);
  else
    imageSize = overlay.size();
  if (imageSize.width() <= 0 || imageSize.height() <= 0) return identityCopy(source, context);

  const QPoint position = placementPosition(choiceOf(params, "placement"),
                                            QPoint(intOf(params, "offset_x"), intOf(params, "offset_y")),
                                            source.size(), imageSize);
  const QRect rect(position, imageSize);
  if (boolOf(params, "auto_hide") && !contains(QRect(QPoint(0, 0), source.size()), rect)) return identityCopy(source, context);

  QImage result = copyOf(source);
  {
    QPainter painter(&result);
    initPainter(painter, choiceOf(params, "interpolation_mode") != "nearest_neighbor");
    if (choiceOf(params, "compositing_mode") == "source_copy") painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.setOpacity(opacity / 100.0);
    if (boolOf(params, "tile")) {
      painter.translate(rect.left(), rect.top());
      painter.fillRect(QRect(0, 0, rect.width(), rect.height()), QBrush(overlay));  // tiles at native size
    } else {
      painter.drawImage(QRectF(rect), overlay);
    }
  }
  return {result, {}};
}

// ---- line / shape / gradient ----------------------------------------------------
EffectResult drawLine(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float thickness = floatOf(params, "thickness");
  const QColor color = colorOf(params, "color");
  if (thickness <= 0 || color.alpha() == 0) return identityCopy(source, context);
  QImage result = copyOf(source);
  QPainter painter(&result);
  initPainter(painter);
  painter.setPen(QPen(color, thickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.drawLine(QPointF(intOf(params, "start_x"), intOf(params, "start_y")),
                   QPointF(intOf(params, "end_x"), intOf(params, "end_y")));
  return {result, {}};
}

EffectResult drawShape(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  int width = intOf(params, "size_width"), height = intOf(params, "size_height");
  if (width == -1) width = source.width();
  if (height == -1) height = source.height();
  const QColor color = colorOf(params, "color");
  if (width <= 0 || height <= 0 || color.alpha() == 0) return identityCopy(source, context);
  const QPoint position = placementPosition(choiceOf(params, "placement"),
                                            QPoint(intOf(params, "offset_x"), intOf(params, "offset_y")),
                                            source.size(), QSize(width, height));
  const QRectF rect(position.x(), position.y(), width, height);
  QImage result = copyOf(source);
  QPainter painter(&result);
  initPainter(painter);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  const QString shape = choiceOf(params, "shape");
  if (shape == "rounded_rectangle") {
    const double radius = std::max(1.0, std::min(rect.width(), rect.height()) * 0.2);
    painter.drawRoundedRect(rect, radius, radius);
  } else if (shape == "ellipse") {
    painter.drawEllipse(rect);
  } else if (shape == "triangle") {
    const QPointF points[]{{rect.center().x(), rect.top()}, {rect.right(), rect.bottom()}, {rect.left(), rect.bottom()}};
    painter.drawPolygon(points, 3);
  } else if (shape == "diamond") {
    const QPointF points[]{{rect.center().x(), rect.top()}, {rect.right(), rect.center().y()},
                           {rect.center().x(), rect.bottom()}, {rect.left(), rect.center().y()}};
    painter.drawPolygon(points, 4);
  } else {
    painter.drawRect(rect);
  }
  return {result, {}};
}

EffectResult gradientOverlay(const QImage &source, const ParamValues &params, const EffectContext &context) {
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (alpha <= 0.f) return identityCopy(source, context);
  const QColor c1 = colorOf(params, "color1"), c2 = colorOf(params, "color2");
  const int w = source.width(), h = source.height();
  const float cx = w / 2.f, cy = h / 2.f;
  const float rad = intOf(params, "angle") * 3.14159265358979f / 180.f;
  const float diagonal = std::sqrt(float(w) * w + float(h) * h) / 2.f;
  const float sx = cx - std::cos(rad) * diagonal, sy = cy - std::sin(rad) * diagonal;
  const float ex = cx + std::cos(rad) * diagonal, ey = cy + std::sin(rad) * diagonal;
  const float dx = ex - sx, dy = ey - sy;
  const float lengthSquared = dx * dx + dy * dy;
  const float a1 = float(int(c1.alpha() * alpha)), a2 = float(int(c2.alpha() * alpha));
  const float rgb1[3]{float(c1.red()), float(c1.green()), float(c1.blue())};
  const float rgb2[3]{float(c2.red()), float(c2.green()), float(c2.blue())};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  if (!runRows(context, h, 0, 100, [&](int y) {
        const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *out = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < w; ++x) {
          float t = lengthSquared > 0 ? ((x + 0.5f - sx) * dx + (y + 0.5f - sy) * dy) / lengthSquared : 0.f;
          t = std::clamp(t, 0.f, 1.f);
          const float as = (a1 + (a2 - a1) * t) / 255.f;
          const float ab = qAlpha(in[x]) / 255.f;
          const float ar = as + ab * (1.f - as);
          if (ar <= 0.f) {
            out[x] = 0;
            continue;
          }
          const float backdrop[3]{qRed(in[x]) / 255.f, qGreen(in[x]) / 255.f, qBlue(in[x]) / 255.f};
          int channels[3];
          for (int c = 0; c < 3; ++c) {
            const float cs = (rgb1[c] + (rgb2[c] - rgb1[c]) * t) / 255.f;
            const float cb = backdrop[c];
            // Overlay = HardLight with the operands swapped.
            const float blended = cb <= 0.5f ? cs * 2.f * cb : cs + (2.f * cb - 1.f) - cs * (2.f * cb - 1.f);
            const float value = ((1.f - ab) * as * cs + (1.f - as) * ab * cb + as * ab * blended) / ar;
            channels[c] = int(std::nearbyintf(std::clamp(value, 0.f, 1.f) * 255.f));
          }
          out[x] = qRgba(channels[0], channels[1], channels[2], int(std::nearbyintf(ar * 255.f)));
        }
      }))
    return {{}, kCancelled};
  return {result, {}};
}

// ---- seeded scatter effects -----------------------------------------------------
const QColor kConfettiColors[]{{255, 56, 96}, {255, 165, 0}, {255, 215, 0}, {0, 200, 83}, {0, 176, 255},
                               {156, 39, 176}, {255, 64, 129}, {0, 230, 118}, {255, 234, 0}, {29, 233, 182}};

EffectResult confetti(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int count = intOf(params, "count");
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (count <= 0 || alpha <= 0.f) return identityCopy(source, context);
  const int minSize = std::max(2, std::min(intOf(params, "min_size"), intOf(params, "max_size")));
  const int maxSize = std::max(minSize, intOf(params, "max_size"));
  const bool background = boolOf(params, "background");
  QImage canvas = startCanvas(source, background);
  if (canvas.isNull()) return {{}, kFailed};
  SplitMix64 rng(context.seed);
  {
    QPainter painter(&canvas);
    initPainter(painter);
    painter.setPen(Qt::NoPen);
    context.progress(0);
    for (int i = 0; i < count; ++i) {
      if (context.cancelled()) return {{}, kCancelled};
      const float x = float(rng.range(0, std::max(1, source.width())));
      const float y = float(rng.range(0, std::max(1, source.height())));
      const float w = float(rng.range(minSize, maxSize + 1));
      const float h = float(rng.range(minSize, maxSize + 1));
      const float angle = float(rng.range(0, 360));
      QColor color = kConfettiColors[rng.range(0, 10)];
      color.setAlpha(int(255 * alpha));
      painter.setBrush(color);
      painter.save();
      painter.translate(x, y);
      painter.rotate(angle);
      switch (rng.range(0, 3)) {
      case 0: painter.drawRect(QRectF(-w / 2, -h / 2, w, h)); break;
      case 1: painter.drawEllipse(QPointF(0, 0), w / 2, h / 2); break;
      default: {
        const QPointF points[]{{0, -h / 2}, {w / 2, h / 2}, {-w / 2, h / 2}};
        painter.drawPolygon(points, 3);
      }
      }
      painter.restore();
      context.progress(int(qint64(i + 1) * 90 / count));
    }
  }
  finishCanvas(canvas, source, background);
  context.progress(100);
  return {canvas, {}};
}

void drawSnowflake(QPainter &painter, float cx, float cy, float size, float alpha, float rotation) {
  const int a = int(255 * alpha);
  painter.save();
  painter.translate(cx, cy);
  painter.rotate(rotation);
  painter.translate(-cx, -cy);

  // Soft glow: a radial falloff standing in for Skia's blur mask filter (sigma = size * 0.4).
  const float glowAlpha = float(int(a * 0.3f));
  const double glowRadius = size * 0.6 + size * 0.8;
  QRadialGradient glow(QPointF(cx, cy), glowRadius);
  const double solid = (size * 0.6) / glowRadius;
  glow.setColorAt(0, QColor(200, 220, 255, int(glowAlpha)));
  glow.setColorAt(solid * 0.5, QColor(200, 220, 255, int(glowAlpha)));
  glow.setColorAt(std::min(1.0, solid + 0.01), QColor(200, 220, 255, int(glowAlpha * 0.4f)));
  glow.setColorAt(1, QColor(200, 220, 255, 0));
  painter.setPen(Qt::NoPen);
  painter.setBrush(glow);
  painter.drawEllipse(QPointF(cx, cy), glowRadius, glowRadius);

  QPen line(QColor(240, 248, 255, a), std::max(1.f, size * 0.1f), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(line);
  const float halfSize = size / 2.f;
  for (int branch = 0; branch < 6; ++branch) {
    const float angle = branch * 60.f * 3.14159265358979f / 180.f;
    const float c = std::cos(angle), s = std::sin(angle);
    painter.drawLine(QPointF(cx, cy), QPointF(cx + c * halfSize, cy + s * halfSize));
    if (size >= 6) {
      const float branchLength = halfSize * 0.35f;
      const float midX = cx + c * halfSize * 0.55f, midY = cy + s * halfSize * 0.55f;
      for (int side = -1; side <= 1; side += 2) {
        const float sideAngle = angle + side * 3.14159265358979f / 3.f;
        painter.drawLine(QPointF(midX, midY),
                         QPointF(midX + std::cos(sideAngle) * branchLength, midY + std::sin(sideAngle) * branchLength));
      }
    }
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(255, 255, 255, a));
  const double dot = std::max(1.f, size * 0.08f);
  painter.drawEllipse(QPointF(cx, cy), dot, dot);
  painter.restore();
}

EffectResult snowflakes(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int count = intOf(params, "count");
  const float alpha = std::clamp(floatOf(params, "opacity") / 100.f, 0.f, 1.f);
  if (count <= 0 || alpha <= 0.f) return identityCopy(source, context);
  const int minSize = std::max(2, std::min(intOf(params, "min_size"), intOf(params, "max_size")));
  const int maxSize = std::max(minSize, intOf(params, "max_size"));
  const bool randomAngle = boolOf(params, "random_angle"), background = boolOf(params, "background");
  QImage canvas = startCanvas(source, background);
  if (canvas.isNull()) return {{}, kFailed};
  SplitMix64 rng(context.seed);
  {
    QPainter painter(&canvas);
    initPainter(painter);
    for (int i = 0; i < count; ++i) {
      if (context.cancelled()) return {{}, kCancelled};
      const float x = float(rng.range(0, std::max(1, source.width())));
      const float y = float(rng.range(0, std::max(1, source.height())));
      const float size = float(rng.range(minSize, maxSize + 1));
      const float flakeAlpha = alpha * (0.5f + rng.unit() * 0.5f);
      const float angle = randomAngle ? rng.unit() * 360.f : 0.f;
      drawSnowflake(painter, x, y, size, flakeAlpha, angle);
      context.progress(int(qint64(i + 1) * 90 / count));
    }
  }
  finishCanvas(canvas, source, background);
  context.progress(100);
  return {canvas, {}};
}

EffectResult fakeHair(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const int count = intOf(params, "count");
  const float length = std::clamp(floatOf(params, "length"), 5.f, 2000.f);
  const float thickness = std::clamp(floatOf(params, "thickness"), 0.2f, 20.f);
  const float curliness = std::clamp(floatOf(params, "curliness"), 0.f, 100.f) / 100.f;
  const QColor hairColor = colorOf(params, "hair_color");
  if (count <= 0) return identityCopy(source, context);
  QImage result = copyOf(source);
  SplitMix64 rng(context.seed);
  auto clampByte = [](float v) { return v <= 0.f ? 0 : (v >= 255.f ? 255 : int(std::nearbyintf(v))); };
  QPainter painter(&result);
  initPainter(painter);
  for (int i = 0; i < count; ++i) {
    if (context.cancelled()) return {{}, kCancelled};
    const float startX = rng.unit() * source.width(), startY = rng.unit() * source.height();
    const float baseAngle = rng.unit() * 3.14159265358979f * 2.f;
    const float variation = (rng.unit() - 0.5f) * 0.3f;
    const int hR = clampByte(hairColor.red() + hairColor.red() * variation);
    const int hG = clampByte(hairColor.green() + hairColor.green() * variation);
    const int hB = clampByte(hairColor.blue() + hairColor.blue() * variation);
    const int hA = clampByte(180 + rng.unit() * 75.f);
    const float hairThickness = thickness * (0.6f + rng.unit() * 0.8f);
    const float hairLength = length * (0.5f + rng.unit() * 0.7f);
    const int segments = std::max(8, int(hairLength / 6.f));
    const float segmentLength = hairLength / segments;
    const float maxTurn = curliness * 0.35f;
    QPainterPath path;
    float px = startX, py = startY, angle = baseAngle;
    path.moveTo(px, py);
    for (int s = 1; s <= segments; ++s) {
      angle += (rng.unit() - 0.5f) * 2.f * maxTurn;
      px += std::cos(angle) * segmentLength;
      py += std::sin(angle) * segmentLength;
      path.lineTo(px, py);
    }
    painter.strokePath(path, QPen(QColor(hR, hG, hB, hA), hairThickness, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (hairThickness > 0.8f) {
      painter.save();
      painter.translate(0.3, -0.3);
      painter.strokePath(path, QPen(QColor(clampByte(hR + 60), clampByte(hG + 50), clampByte(hB + 40), hA / 4),
                                    hairThickness * 0.3f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.restore();
    }
  }
  return {result, {}};
}

// The reference draws a vector arrow (no embedded cursor image), so no asset is needed.
EffectResult fakeCursor(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  static const QPointF outline[]{{0, 0}, {0, 20.4}, {5.7, 15.6}, {9.6, 23.4}, {12.6, 22.2}, {8.7, 14.4}, {15.3, 14.4}};
  const int count = std::clamp(intOf(params, "count"), 1, 200);
  const bool randomSize = boolOf(params, "random_size"), randomAngle = boolOf(params, "random_angle");
  const float sizeMin = floatOf(params, "random_size_min"), sizeMax = floatOf(params, "random_size_max");
  const int angleMin = std::min(intOf(params, "random_angle_min"), intOf(params, "random_angle_max"));
  const int angleMax = std::max(intOf(params, "random_angle_min"), intOf(params, "random_angle_max"));
  const QColor fill = colorOf(params, "fill_color"), borderColor = colorOf(params, "border_color");
  QImage result = copyOf(source);
  SplitMix64 rng(context.seed);
  QPainter painter(&result);
  initPainter(painter);
  for (int i = 0; i < count; ++i) {
    if (context.cancelled()) return {{}, kCancelled};
    float scale = randomSize ? sizeMin + rng.unit() * (std::max(sizeMin, sizeMax) - sizeMin) : floatOf(params, "scale");
    scale = std::clamp(scale, 0.5f, 5.f);
    const float cursorWidth = 15.3f * scale, cursorHeight = 23.4f * scale;
    const float x = float(rng.range(0, std::max(1, source.width() - int(cursorWidth))));
    const float y = float(rng.range(0, std::max(1, source.height() - int(cursorHeight))));
    painter.save();
    painter.translate(x, y);
    if (randomAngle) {
      const int angle = angleMin == angleMax ? angleMin : rng.range(angleMin, angleMax);
      painter.translate(cursorWidth / 2.f, cursorHeight / 2.f);
      painter.rotate(angle);
      painter.translate(-cursorWidth / 2.f, -cursorHeight / 2.f);
    }
    painter.scale(scale, scale);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(borderColor, 1.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
    painter.drawPolygon(outline, 7);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPolygon(outline, 7);
    painter.restore();
  }
  return {result, {}};
}

// ---- particles ------------------------------------------------------------------
EffectResult drawParticles(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const QString folder = params.value("image_folder").toString().trimmed();
  const QFileInfo folderInfo(folder);
  if (folder.isEmpty() || !folderInfo.isDir()) return {{}, kInvalidInput};
  QStringList files;
  for (const QFileInfo &entry : QDir(folder).entryInfoList(QDir::Files, QDir::Name)) {
    const QString suffix = entry.suffix().toLower();
    if (suffix == "png" || suffix == "jpg" || suffix == "jpeg") files.append(entry.absoluteFilePath());
  }
  if (files.isEmpty()) return {{}, kInvalidInput};
  for (const QString &file : files)  // every candidate must be a valid image, so a seed never hides a bad file
    if (!validateImageFile(file)) return {{}, kInvalidInput};

  const int imageCount = intOf(params, "image_count");
  const bool background = boolOf(params, "background"), randomSize = boolOf(params, "random_size");
  const bool randomAngle = boolOf(params, "random_angle"), randomOpacity = boolOf(params, "random_opacity");
  const bool noOverlap = boolOf(params, "no_overlap"), edgeOverlap = boolOf(params, "edge_overlap");
  const int overlapOffset = intOf(params, "no_overlap_offset");
  const int sizeMin = intOf(params, "random_size_min"), sizeMax = intOf(params, "random_size_max");
  const int angleMin = intOf(params, "random_angle_min"), angleMax = intOf(params, "random_angle_max");
  const int opacityMin = intOf(params, "random_opacity_min"), opacityMax = intOf(params, "random_opacity_max");

  QImage canvas = startCanvas(source, background);
  if (canvas.isNull()) return {{}, kFailed};
  SplitMix64 rng(context.seed);
  QMap<QString, QImage> cache;
  QVector<QRect> placed;
  {
    QPainter painter(&canvas);
    initPainter(painter);
    for (int i = 0; i < imageCount; ++i) {
      if (context.cancelled()) return {{}, kCancelled};
      const QString file = files[rng.range(0, files.size())];
      if (!cache.contains(file)) {
        const QImage loaded = loadImageFile(file);
        if (loaded.isNull()) return {{}, kInvalidInput};
        cache.insert(file, loaded);
      }
      const QImage &particle = cache[file];

      int width, height;
      if (randomSize) {
        const int size = nextInt(rng, std::min(sizeMin, sizeMax), std::max(sizeMin, sizeMax));
        width = height = size;
        if (particle.width() > particle.height())
          height = int(std::nearbyint(size * (double(particle.height()) / particle.width())));
        else if (particle.width() < particle.height())
          width = int(std::nearbyint(size * (double(particle.width()) / particle.height())));
      } else {
        width = particle.width();
        height = particle.height();
      }
      if (width < 1 || height < 1) continue;

      const int minX = edgeOverlap ? -width + 1 : 0, minY = edgeOverlap ? -height + 1 : 0;
      const int maxX = canvas.width() - (edgeOverlap ? 0 : width) - 1;
      const int maxY = canvas.height() - (edgeOverlap ? 0 : height) - 1;
      QRect rect;
      int attempts = 0;
      bool gaveUp = false;
      do {
        if (++attempts > 1000) {
          gaveUp = true;
          break;
        }
        const int x = nextInt(rng, std::min(minX, maxX), std::max(minX, maxX));
        const int y = nextInt(rng, std::min(minY, maxY), std::max(minY, maxY));
        rect = QRect(x, y, width, height);
      } while (noOverlap && std::any_of(placed.cbegin(), placed.cend(), [&](const QRect &other) {
                 return intersects(other, rect.adjusted(-overlapOffset, -overlapOffset, overlapOffset, overlapOffset));
               }));
      if (gaveUp) continue;
      placed.append(rect);

      int alpha = 255;
      if (randomOpacity) {
        const int opacity = std::clamp(nextInt(rng, std::min(opacityMin, opacityMax), std::max(opacityMin, opacityMax)), 0, 100);
        alpha = int(std::nearbyintf(opacity / 100.f * 255));
      }
      painter.save();
      if (randomAngle) {
        const double mx = rect.left() + rect.width() / 2.0, my = rect.top() + rect.height() / 2.0;
        const int rotate = nextInt(rng, std::min(angleMin, angleMax), std::max(angleMin, angleMax));
        painter.translate(mx, my);
        painter.rotate(rotate);
        painter.translate(-mx, -my);
      }
      painter.setOpacity(alpha / 255.0);
      painter.drawImage(QRectF(rect), particle);
      painter.restore();
      context.progress(int(qint64(i + 1) * 90 / imageCount));
    }
  }
  finishCanvas(canvas, source, background);
  context.progress(100);
  return {canvas, {}};
}

// ---- text -----------------------------------------------------------------------
// Declared fallback (EE-018): an uninstalled family uses the application default font.
QFont resolveFont(const QString &family, float size, bool bold, bool italic) {
  QFont font;
  const QString wanted = family.trimmed();
  const QStringList installed = QFontDatabase::families();
  const bool available = !wanted.isEmpty() && installed.contains(wanted, Qt::CaseInsensitive);
  font.setFamily(available ? wanted : QGuiApplication::font().family());
  font.setPixelSize(std::max(1, int(std::lround(size))));
  font.setBold(bold);
  font.setItalic(italic);
  return font;
}
QStringList splitLines(QString text) {
  text.replace("\r\n", "\n");
  return text.split('\n');
}

EffectResult drawText(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const QString text = params.value("text").toString();
  const float fontSize = floatOf(params, "font_size");
  if (text.trimmed().isEmpty() || fontSize < 1.f) return identityCopy(source, context);
  if (!qGuiApp) return {{}, kUnavailable};

  const QFont font = resolveFont(params.value("font_family").toString(), fontSize, boolOf(params, "bold"), boolOf(params, "italic"));
  const QFontMetricsF metrics(font);
  const QStringList lines = splitLines(text);
  const double lineHeight = std::max(metrics.descent() + metrics.ascent() + metrics.leading(), double(font.pixelSize()));
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  for (int i = 0; i < lines.size(); ++i)
    if (!lines[i].isEmpty()) path.addText(0, metrics.ascent() + i * lineHeight, font, lines[i]);
  if (path.isEmpty()) return identityCopy(source, context);
  const int angle = intOf(params, "angle");
  if (angle != 0) path = QTransform().rotate(angle).map(path);
  const QRectF bounds = path.boundingRect();
  if (bounds.isEmpty()) return identityCopy(source, context);

  const QSize textSize(int(std::ceil(bounds.width())) + 1, int(std::ceil(bounds.height())) + 1);
  const QPoint position = placementPosition(choiceOf(params, "placement"),
                                            QPoint(intOf(params, "offset_x"), intOf(params, "offset_y")),
                                            source.size(), textSize);
  const QRect rect(position, textSize);
  if (boolOf(params, "auto_hide") && !contains(QRect(QPoint(0, 0), source.size()), rect)) return identityCopy(source, context);
  path = QTransform::fromTranslate(rect.left() - bounds.left(), rect.top() - bounds.top()).map(path);

  QImage result = copyOf(source);
  QPainter painter(&result);
  initPainter(painter);
  const bool outline = boolOf(params, "outline");
  const int outlineSize = intOf(params, "outline_size");
  auto stroke = [&](const QPainterPath &p, const QColor &color) {
    if (color.alpha() == 0) return;
    painter.strokePath(p, QPen(color, std::max(1, outlineSize), Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
  };
  auto fill = [&](const QPainterPath &p, const QColor &color) {
    if (color.alpha() == 0) return;
    painter.fillPath(p, color);
  };
  const QColor shadowColor = colorOf(params, "shadow_color");
  if (boolOf(params, "shadow") && shadowColor.alpha() > 0) {
    const QPainterPath shadowPath =
        QTransform::fromTranslate(intOf(params, "shadow_offset_x"), intOf(params, "shadow_offset_y")).map(path);
    if (outline && outlineSize > 0) stroke(shadowPath, shadowColor);
    else fill(shadowPath, shadowColor);
  }
  if (outline && outlineSize > 0) stroke(path, colorOf(params, "outline_color"));
  fill(path, colorOf(params, "color"));
  return {result, {}};
}

QString expandWatermarkVariables(QString text, QSize imageSize) {
  text.replace("%width", QString::number(imageSize.width()), Qt::CaseInsensitive);
  text.replace("%height", QString::number(imageSize.height()), Qt::CaseInsensitive);
  text.replace("%n", "\n", Qt::CaseInsensitive);
  return text;
}

EffectResult textWatermark(const QImage &source, const ParamValues &params, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  const float fontSize = floatOf(params, "font_size");
  if (fontSize < 1.f) return identityCopy(source, context);
  const QString text = expandWatermarkVariables(params.value("text").toString(), source.size());
  if (text.trimmed().isEmpty()) return identityCopy(source, context);
  if (!qGuiApp) return {{}, kUnavailable};

  const int padLeft = std::max(0, intOf(params, "padding_left")), padTop = std::max(0, intOf(params, "padding_top"));
  const int padRight = std::max(0, intOf(params, "padding_right")), padBottom = std::max(0, intOf(params, "padding_bottom"));
  const QFont font = resolveFont(params.value("font_family").toString(), fontSize, boolOf(params, "bold"), boolOf(params, "italic"));
  const QFontMetricsF metrics(font);
  const QStringList lines = splitLines(text);
  const double rawTextHeight = std::max(metrics.descent() + metrics.ascent(), double(font.pixelSize()));
  const double lineHeight = std::max(rawTextHeight + metrics.leading(), double(font.pixelSize()));
  double maxTextWidth = 0;
  for (const QString &line : lines) maxTextWidth = std::max(maxTextWidth, metrics.horizontalAdvance(line));
  const double totalTextHeight = rawTextHeight + std::max(0, int(lines.size()) - 1) * lineHeight;
  const QSize watermarkSize(int(std::ceil(maxTextWidth + padLeft + padRight)), int(std::ceil(totalTextHeight + padTop + padBottom)));
  if (watermarkSize.width() <= 0 || watermarkSize.height() <= 0) return identityCopy(source, context);

  const QPoint position = placementPosition(choiceOf(params, "placement"),
                                            QPoint(intOf(params, "offset_x"), intOf(params, "offset_y")),
                                            source.size(), watermarkSize);
  const QRect rect(position, watermarkSize);
  if (boolOf(params, "auto_hide") && !contains(QRect(QPoint(0, 0), source.size()), rect)) return identityCopy(source, context);

  QImage result = copyOf(source);
  QPainter painter(&result);
  initPainter(painter);
  const double radius = std::min<double>(std::max(0, intOf(params, "corner_radius")), std::min(rect.width(), rect.height()) / 2.0);
  const QRectF backgroundRect(rect);
  const QColor backgroundColor = colorOf(params, "background_color"), borderColor = colorOf(params, "border_color");
  if (boolOf(params, "draw_background") && backgroundColor.alpha() > 0) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundColor);
    painter.drawRoundedRect(backgroundRect, radius, radius);
  }
  if (boolOf(params, "draw_border") && borderColor.alpha() > 0) {
    painter.setPen(QPen(borderColor, std::max(1, intOf(params, "border_size"))));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(backgroundRect, radius, radius);
  }
  painter.setFont(font);
  const double textX = rect.left() + padLeft;
  const double baseline = rect.top() + padTop + metrics.ascent();
  auto drawLines = [&](double x, double y, const QColor &color) {
    painter.setPen(color);
    for (int i = 0; i < lines.size(); ++i)
      if (!lines[i].isEmpty()) painter.drawText(QPointF(x, y + i * lineHeight), lines[i]);
  };
  const QColor shadowColor = colorOf(params, "text_shadow_color");
  if (boolOf(params, "draw_text_shadow") && shadowColor.alpha() > 0)
    drawLines(textX + intOf(params, "text_shadow_offset_x"), baseline + intOf(params, "text_shadow_offset_y"), shadowColor);
  drawLines(textX, baseline, colorOf(params, "text_color"));
  return {result, {}};
}

// ---- registration -----------------------------------------------------------------
void add(EffectRegistry &registry, const QString &id, const QString &name, const QString &reference,
         QVector<ParamSpec> parameters, ApplyFn fn, bool usesSeed = false, bool changesSize = false) {
  EffectSpec spec;
  spec.id = id;
  spec.displayName = name;
  spec.category = Category::Drawings;
  spec.mode = Mode::Previewable;
  spec.parameters = std::move(parameters);
  spec.usesSeed = usesSeed;
  spec.changesSize = changesSize;
  spec.referencePath = kReferenceRoot + reference;
  registry.add(std::make_unique<FunctionEffect>(std::move(spec), std::move(fn)));
}
}  // namespace

void registerDrawingEffects(EffectRegistry &registry) {
  const QStringList shapes{"rectangle", "rounded_rectangle", "ellipse", "triangle", "diamond"};
  const QStringList rotateFlips{"none", "rotate_90", "rotate_180", "rotate_270", "flip_x", "rotate_90_flip_x", "flip_y", "rotate_90_flip_y"};
  add(registry, "border", "Border", "Filters/BorderImageEffect.cs",
      {enumParam("type", "Type", "outside", {"outside", "inside"}), intParam("size", "Size", 5, 1, 100),
       enumParam("dash_style", "Dash style", "solid", {"solid", "dash", "dot", "dash_dot"}),
       colorParam("color", "Color", QColor(0, 0, 0))},
      border, false, true);
  add(registry, "candy_cane_border", "Candy Cane Border", "Filters/CandyCaneBorderImageEffect.cs",
      {intParam("border_width", "Border Width", 10, 1, 200), floatParam("stripe_width", "Stripe Width", 12, 2, 60),
       colorParam("color_a", "Color A", QColor(220, 20, 20)), colorParam("color_b", "Color B", QColor(255, 255, 255)),
       floatParam("gloss", "Gloss", 50, 0, 100), boolParam("rounded_corners", "Rounded Corners", true)},
      candyCaneBorder, false, true);
  add(registry, "confetti", "Confetti", "Drawings/ConfettiImageEffect.cs",
      {intParam("count", "Count", 150, 10, 1000), intParam("min_size", "Min size", 4, 2, 40),
       intParam("max_size", "Max size", 12, 4, 80), floatParam("opacity", "Opacity", 90, 0, 100),
       boolParam("background", "Background", false)},
      confetti, true);
  add(registry, "draw_background", "Background", "Drawings/DrawBackgroundEffect.cs",
      {colorParam("color", "Color", QColor(0, 0, 0))}, drawBackground);
  add(registry, "draw_background_image", "Background image", "Drawings/DrawBackgroundImageEffect.cs",
      {filePathParam("image_file_path", "Image file path"), boolParam("center", "Center", true), boolParam("tile", "Tile", false)},
      drawBackgroundImage);
  add(registry, "draw_checkerboard", "Checkerboard", "Drawings/DrawCheckerboardEffect.cs",
      {intParam("size", "Size", 10, 1, 100), colorParam("color", "Color", QColor(211, 211, 211)),
       colorParam("color2", "Color 2", QColor(255, 255, 255))},
      drawCheckerboard);
  add(registry, "draw_image", "Image", "Drawings/DrawImageEffect.cs",
      {filePathParam("image_location", "Image location"), enumParam("placement", "Placement", "top_left", placementChoices()),
       intParam("offset_x", "Offset X", 0, -10000, 10000), intParam("offset_y", "Offset Y", 0, -10000, 10000),
       enumParam("size_mode", "Size mode", "dont_resize",
                 {"dont_resize", "absolute_size", "percentage_of_watermark", "percentage_of_canvas"}),
       intParam("size_width", "Size width", 0, -1, 10000), intParam("size_height", "Size height", 0, -1, 10000),
       enumParam("rotate_flip", "Rotate / flip", "none", rotateFlips), boolParam("tile", "Tile", false),
       boolParam("auto_hide", "Auto hide", false),
       enumParam("interpolation_mode", "Interpolation mode", "high_quality_bicubic",
                 {"high_quality_bicubic", "bicubic", "high_quality_bilinear", "bilinear", "nearest_neighbor"}),
       enumParam("compositing_mode", "Compositing mode", "source_over", {"source_over", "source_copy"}),
       intParam("opacity", "Opacity", 100, 0, 100)},
      drawImage);
  add(registry, "draw_line", "Line", "Drawings/DrawLineEffect.cs",
      {intParam("start_x", "Start X", 0, -10000, 10000), intParam("start_y", "Start Y", 0, -10000, 10000),
       intParam("end_x", "End X", 200, -10000, 10000), intParam("end_y", "End Y", 0, -10000, 10000),
       colorParam("color", "Color", QColor(255, 255, 255)), floatParam("thickness", "Thickness", 4, 0.5, 100)},
      drawLine);
  add(registry, "draw_particles", "Particles", "Drawings/DrawParticlesEffect.cs",
      {filePathParam("image_folder", "Image folder"), intParam("image_count", "Image count", 1, 1, 1000),
       boolParam("background", "Background", false), boolParam("random_size", "Random size", false),
       intParam("random_size_min", "Random size min", 64, 1, 10000), intParam("random_size_max", "Random size max", 128, 1, 10000),
       boolParam("random_angle", "Random angle", false), intParam("random_angle_min", "Random angle min", 0, 0, 360),
       intParam("random_angle_max", "Random angle max", 360, 0, 360), boolParam("random_opacity", "Random opacity", false),
       intParam("random_opacity_min", "Random opacity min", 0, 0, 100),
       intParam("random_opacity_max", "Random opacity max", 100, 0, 100), boolParam("no_overlap", "No overlap", false),
       intParam("no_overlap_offset", "No overlap offset", 0, 0, 1000), boolParam("edge_overlap", "Edge overlap", false)},
      drawParticles, true);
  add(registry, "draw_shape", "Shape", "Drawings/DrawShapeEffect.cs",
      {enumParam("shape", "Shape", "rectangle", shapes), enumParam("placement", "Placement", "top_left", placementChoices()),
       intParam("offset_x", "Offset X", 0, -10000, 10000), intParam("offset_y", "Offset Y", 0, -10000, 10000),
       intParam("size_width", "Size width", 100, -1, 10000), intParam("size_height", "Size height", 100, -1, 10000),
       colorParam("color", "Color", QColor(255, 255, 255))},
      drawShape);
  add(registry, "draw_text", "Text", "Drawings/DrawTextEffect.cs",
      {textParam("text", "Text", "Text"), enumParam("placement", "Placement", "top_left", placementChoices()),
       intParam("offset_x", "Offset X", 0, -10000, 10000), intParam("offset_y", "Offset Y", 0, -10000, 10000),
       intParam("angle", "Angle", 0, -360, 360), boolParam("auto_hide", "Auto hide", false),
       textParam("font_family", "Font family", "Arial"), floatParam("font_size", "Font size", 36, 1, 500),
       boolParam("bold", "Bold", false), boolParam("italic", "Italic", false),
       colorParam("color", "Color", QColor(235, 235, 235)), boolParam("outline", "Outline", false),
       intParam("outline_size", "Outline size", 5, 1, 100), colorParam("outline_color", "Outline color", QColor(235, 0, 0)),
       boolParam("shadow", "Shadow", false), intParam("shadow_offset_x", "Shadow offset X", 0, -1000, 1000),
       intParam("shadow_offset_y", "Shadow offset Y", 5, -1000, 1000),
       colorParam("shadow_color", "Shadow color", QColor(0, 0, 0, 125))},
      drawText);
  add(registry, "fake_cursor", "Fake cursor", "Drawings/FakeCursorImageEffect.cs",
      {intParam("count", "Count", 1, 1, 200), floatParam("scale", "Scale", 1, 0.5, 5), boolParam("random_size", "Random size", false),
       floatParam("random_size_min", "Random size min", 0.5, 0.5, 5), floatParam("random_size_max", "Random size max", 2, 0.5, 5),
       boolParam("random_angle", "Random angle", false), intParam("random_angle_min", "Random angle min", 0, 0, 360),
       intParam("random_angle_max", "Random angle max", 360, 0, 360), colorParam("fill_color", "Fill color", QColor(255, 255, 255)),
       colorParam("border_color", "Border color", QColor(0, 0, 0))},
      fakeCursor, true);
  add(registry, "fake_hair", "Fake Hair", "Filters/FakeHairImageEffect.cs",
      {intParam("count", "Hair Count", 1, 1, 100), floatParam("length", "Length", 180, 20, 600),
       floatParam("thickness", "Thickness", 1, 0.5, 4), floatParam("curliness", "Curliness", 30, 0, 100),
       colorParam("hair_color", "Hair Color", QColor(20, 15, 10, 220))},
      fakeHair, true);
  add(registry, "gradient_overlay", "Gradient overlay", "Drawings/GradientOverlayImageEffect.cs",
      {colorParam("color1", "Start color", QColor(255, 0, 128)), colorParam("color2", "End color", QColor(0, 128, 255)),
       intParam("angle", "Angle", 0, 0, 360), floatParam("opacity", "Opacity", 50, 0, 100)},
      gradientOverlay);
  add(registry, "snowflakes", "Snowflakes", "Drawings/SnowflakesImageEffect.cs",
      {intParam("count", "Count", 80, 10, 500), intParam("min_size", "Min size", 3, 2, 20),
       intParam("max_size", "Max size", 14, 4, 60), floatParam("opacity", "Opacity", 85, 0, 100),
       boolParam("random_angle", "Random angle", true), boolParam("background", "Background", false)},
      snowflakes, true);
  add(registry, "text_watermark", "Text watermark", "Drawings/TextWatermarkEffect.cs",
      {textParam("text", "Text", "Text watermark"), enumParam("placement", "Placement", "bottom_right", placementChoices()),
       intParam("offset_x", "Offset X", 5, -10000, 10000), intParam("offset_y", "Offset Y", 5, -10000, 10000),
       boolParam("auto_hide", "Auto hide", false), textParam("font_family", "Font family", "Arial"),
       floatParam("font_size", "Font size", 15, 1, 500), boolParam("bold", "Bold", false), boolParam("italic", "Italic", false),
       colorParam("text_color", "Text color", QColor(235, 235, 235)), boolParam("draw_text_shadow", "Draw text shadow", false),
       colorParam("text_shadow_color", "Text shadow color", QColor(0, 0, 0)),
       intParam("text_shadow_offset_x", "Text shadow offset X", -1, -1000, 1000),
       intParam("text_shadow_offset_y", "Text shadow offset Y", -1, -1000, 1000),
       intParam("corner_radius", "Corner radius", 4, 0, 100), intParam("padding_left", "Padding left", 5, 0, 100),
       intParam("padding_top", "Padding top", 5, 0, 100), intParam("padding_right", "Padding right", 5, 0, 100),
       intParam("padding_bottom", "Padding bottom", 5, 0, 100), boolParam("draw_border", "Draw border", true),
       colorParam("border_color", "Border color", QColor(0, 0, 0)), intParam("border_size", "Border size", 1, 0, 50),
       boolParam("draw_background", "Draw background", true),
       colorParam("background_color", "Background color", QColor(42, 47, 56))},
      textWatermark);
}
}  // namespace xerahs::effects
