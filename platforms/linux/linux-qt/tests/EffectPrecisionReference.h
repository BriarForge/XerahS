#pragma once
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>
#include <vector>

// Independent, default-only drawing plans for the precision regression
// fixtures. No registry, product renderer, or product math/conversion helper is
// called. Geometry and colors come from the recorded default parameter
// equations. Qt paints premultiplied coverage; public integer qUnpremultiply
// defines export.
namespace precision_reference {
inline int byte(float v) {
  return v <= 0 ? 0 : v >= 255 ? 255 : int(std::nearbyintf(v));
}
inline QImage surface(QSize size) {
  QImage r(size, QImage::Format_ARGB32_Premultiplied);
  r.fill(0);
  return r;
}
inline QImage straight(const QImage &painted,
                       const QImage *original = nullptr) {
  QImage out(painted.size(), QImage::Format_ARGB32);
  for (int y = 0; y < out.height(); ++y)
    for (int x = 0; x < out.width(); ++x) {
      const auto p = painted.pixel(x, y);
      out.setPixel(x, y,
                   original && p == qPremultiply(original->pixel(x, y))
                       ? original->pixel(x, y)
                       : qUnpremultiply(p));
    }
  return out;
}
inline std::vector<float> blur(std::vector<float> data, int w, int h,
                               float sigma) {
  const int radius = std::max(1, int(std::ceil(sigma * 3.f)));
  std::vector<float> weights(size_t(2 * radius + 1));
  float total = 0;
  for (int k = -radius; k <= radius; ++k)
    total += weights[size_t(k + radius)] =
        std::exp(-float(k * k) / (2.f * sigma * sigma));
  for (auto &v : weights)
    v /= total;
  std::vector<float> horizontal(data.size()), result(data.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        float sum = 0;
        for (int k = -radius; k <= radius; ++k)
          if (x + k >= 0 && x + k < w)
            sum += data[(size_t(y) * w + x + k) * 4 + c] *
                   weights[size_t(k + radius)];
        horizontal[(size_t(y) * w + x) * 4 + c] = sum;
      }
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      for (int c = 0; c < 4; ++c) {
        float sum = 0;
        for (int k = -radius; k <= radius; ++k)
          if (y + k >= 0 && y + k < h)
            sum += horizontal[(size_t(y + k) * w + x) * 4 + c] *
                   weights[size_t(k + radius)];
        result[(size_t(y) * w + x) * 4 + c] = sum;
      }
  return result;
}
inline QImage blurred(const QImage &input, float sigma,
                      bool roundTripStraight = false) {
  const QImage source = roundTripStraight ? straight(input) : input;
  std::vector<float> data(size_t(source.width()) * source.height() * 4);
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x) {
      const auto p = source.pixel(x, y);
      float *v = &data[(size_t(y) * source.width() + x) * 4];
      const float factor = roundTripStraight ? float(qAlpha(p)) / 255.f : 1.f;
      v[0] = float(qRed(p)) * factor;
      v[1] = float(qGreen(p)) * factor;
      v[2] = float(qBlue(p)) * factor;
      v[3] = float(qAlpha(p));
    }
  data = blur(data, source.width(), source.height(), sigma);
  QImage out(source.size(), roundTripStraight
                                ? QImage::Format_ARGB32
                                : QImage::Format_ARGB32_Premultiplied);
  for (int y = 0; y < out.height(); ++y)
    for (int x = 0; x < out.width(); ++x) {
      const float *v = &data[(size_t(y) * out.width() + x) * 4];
      const int a = byte(v[3]);
      if (roundTripStraight) {
        const float inv = v[3] > 0 ? 255.f / v[3] : 0.f;
        out.setPixel(
            x, y,
            qRgba(byte(v[0] * inv), byte(v[1] * inv), byte(v[2] * inv), a));
      } else
        out.setPixel(x, y,
                     qRgba(std::min(byte(v[0]), a), std::min(byte(v[1]), a),
                           std::min(byte(v[2]), a), a));
    }
  return out;
}
struct Random {
  quint64 state = 0;
  quint64 next() {
    quint64 z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  int range(int lo, int hi) {
    const quint64 bound = quint64(hi - lo), threshold = (0 - bound) % bound;
    quint64 value;
    do {
      value = next();
    } while (value < threshold);
    return lo + int(value % bound);
  }
  float unit() { return float(next() >> 40) * (1.f / 16777216.f); }
};
inline float noise(int x, int y, int seed) {
  quint32 h = 2166136261u;
  for (quint32 value : {quint32(x), quint32(y), quint32(seed)})
    h = (h ^ value) * 16777619u;
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return float(h & 0xffffffu) / 16777215.f;
}
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline float field(float x, float y, int octaves, int seed) {
  float result = 0, norm = 0, amplitude = 1, frequency = 1;
  for (int octave = 0; octave < octaves; ++octave) {
    const float px = x * frequency, py = y * frequency;
    const int ix = int(std::floor(px)), iy = int(std::floor(py));
    auto fade = [](float t) {
      return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
    };
    const float tx = fade(px - float(ix)), ty = fade(py - float(iy));
    const int s = seed + octave * 1619;
    result += mix(mix(noise(ix, iy, s), noise(ix + 1, iy, s), tx),
                  mix(noise(ix, iy + 1, s), noise(ix + 1, iy + 1, s), tx), ty) *
              amplitude;
    norm += amplitude;
    frequency *= 2.f;
    amplitude *= .5f;
  }
  return result / norm;
}
inline QImage cursor(const QImage &source) {
  QImage out = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  Random random;
  const int x = random.range(0, std::max(1, source.width() - 15)),
            y = random.range(0, std::max(1, source.height() - 23));
  const QPointF polygon[]{{0, 0},       {0, 20.4},   {5.7, 15.6}, {9.6, 23.4},
                          {12.6, 22.2}, {8.7, 14.4}, {15.3, 14.4}};
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.translate(x, y);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(Qt::black, 1.5, Qt::SolidLine, Qt::FlatCap, Qt::RoundJoin));
  p.drawPolygon(polygon, 7);
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::white);
  p.drawPolygon(polygon, 7);
  p.end();
  return straight(out, &source);
}
inline QImage snow(const QImage &source) {
  QImage out = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  Random random;
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  for (int i = 0; i < 80; ++i) {
    const float x = float(random.range(0, source.width())),
                y = float(random.range(0, source.height())),
                size = float(random.range(3, 15));
    const float opacity = .85f * (.5f + random.unit() * .5f),
                rotation = random.unit() * 360.f;
    const int a = int(255 * opacity);
    p.save();
    p.translate(x, y);
    p.rotate(rotation);
    p.translate(-x, -y);
    const float ga = float(int(a * .3f));
    const double radius = size * .6 + size * .8, solid = (size * .6) / radius;
    QRadialGradient glow(QPointF(x, y), radius);
    glow.setColorAt(0, QColor(200, 220, 255, int(ga)));
    glow.setColorAt(solid * .5, QColor(200, 220, 255, int(ga)));
    glow.setColorAt(std::min(1., solid + .01),
                    QColor(200, 220, 255, int(ga * .4f)));
    glow.setColorAt(1, QColor(200, 220, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(QPointF(x, y), radius, radius);
    p.setPen(QPen(QColor(240, 248, 255, a), std::max(1.f, size * .1f),
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const float half = size / 2.f;
    for (int branch = 0; branch < 6; ++branch) {
      const float angle = branch * 60.f * 3.14159265358979f / 180.f,
                  c = std::cos(angle), s = std::sin(angle);
      p.drawLine(QPointF(x, y), QPointF(x + c * half, y + s * half));
      if (size >= 6) {
        const float length = half * .35f, mx = x + c * half * .55f,
                    my = y + s * half * .55f;
        for (int side : {-1, 1}) {
          const float theta = angle + side * 3.14159265358979f / 3.f;
          p.drawLine(QPointF(mx, my), QPointF(mx + std::cos(theta) * length,
                                              my + std::sin(theta) * length));
        }
      }
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, a));
    const double dot = std::max(1.f, size * .08f);
    p.drawEllipse(QPointF(x, y), dot, dot);
    p.restore();
  }
  p.end();
  return straight(out, &source);
}
inline QImage pointillism(const QImage &source) {
  QImage out = surface(source.size());
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setOpacity(51. / 255.);
  p.drawImage(0, 0, source);
  p.setOpacity(1);
  p.setPen(Qt::NoPen);
  for (int gy = 0; gy < source.height(); gy += 7)
    for (int gx = 0; gx < source.width(); gx += 7) {
      const int ix = gx / 7, iy = gy / 7;
      if (noise(ix, iy, 2026) > .72f)
        continue;
      const float amount = .65f * 7.f * .6f;
      const float x = std::clamp(
          float(gx) + 3.5f + (noise(ix, iy, 2026 ^ 111) * 2.f - 1.f) * amount,
          0.f, float(source.width() - 1));
      const float y = std::clamp(
          float(gy) + 3.5f + (noise(ix, iy, 2026 ^ 257) * 2.f - 1.f) * amount,
          0.f, float(source.height() - 1));
      const int x0 = int(std::floor(x)), y0 = int(std::floor(y)),
                x1 = std::min(x0 + 1, source.width() - 1),
                y1 = std::min(y0 + 1, source.height() - 1);
      auto channel = [&](int (*ch)(QRgb)) {
        return byte(mix(mix(float(ch(source.pixel(x0, y0))),
                            float(ch(source.pixel(x1, y0))), x - float(x0)),
                        mix(float(ch(source.pixel(x0, y1))),
                            float(ch(source.pixel(x1, y1))), x - float(x0)),
                        y - float(y0)));
      };
      const float r = float(channel(qRed)), g = float(channel(qGreen)),
                  b = float(channel(qBlue)), gray = (r + g + b) / 3.f;
      p.setBrush(QColor(byte(gray + (r - gray) * 1.2f),
                        byte(gray + (g - gray) * 1.2f),
                        byte(gray + (b - gray) * 1.2f), channel(qAlpha)));
      const float radius =
          std::max(1.f, 7.f * .36f * (.6f + noise(ix, iy, 2026 ^ 509) * .9f));
      p.drawEllipse(QPointF(x, y), radius, radius);
    }
  p.end();
  return straight(out);
}
inline QImage film(const QImage &source) {
  const int w = source.width() + 80, h = source.height() + 80;
  QImage out = surface(QSize(w, h));
  QPainter p(&out);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(15, 15, 15));
  for (const QRect r : {QRect(0, 0, w, 40), QRect(0, h - 40, w, 40),
                        QRect(0, 40, 40, source.height()),
                        QRect(w - 40, 40, 40, source.height())})
    p.drawRect(r);
  p.drawImage(40, 40, source);
  quint32 state = 7391;
  for (int i = 0; i < 10; ++i) {
    state = state * 1664525u + 1013904223u;
    const float x = float(state % quint32(w)) / float(w) * float(w);
    state = state * 1664525u + 1013904223u;
    const float alpha = float(state % 100u) / 100.f * .35f + .07f;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(QColor(140, 140, 140, int(alpha * 255.f)), 0));
    p.drawLine(QPointF(x, 0), QPointF(x, h));
  }
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(Qt::black);
  p.setCompositionMode(QPainter::CompositionMode_Clear);
  const float hole = 40.f * .55f, corner = hole * .15f,
              margin = (40.f - hole) / 2.f, step = hole * 1.75f;
  for (float x = step * .4f; x + hole <= float(w) - step * .2f; x += step) {
    p.drawRoundedRect(QRectF(x, margin, hole, hole), corner, corner);
    p.drawRoundedRect(QRectF(x, float(h - 40) + margin, hole, hole), corner,
                      corner);
  }
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);
  QPen line(QColor(60, 60, 60), 1.2);
  line.setJoinStyle(Qt::MiterJoin);
  p.setPen(line);
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(39.4, 39.4, source.width() + 1.2, source.height() + 1.2));
  p.end();
  return straight(out);
}
inline QImage gradient(const QImage &source) {
  QImage out = surface(source.size() + QSize(100, 100));
  const int inner[]{180, 80, 200}, mid[]{60, 20, 120}, outer[]{10, 10, 35};
  for (int y = 0; y < out.height(); ++y)
    for (int x = 0; x < out.width(); ++x) {
      const int distance = std::max({50 - x, x - 50 - source.width() + 1,
                                     50 - y, y - 50 - source.height() + 1, 0});
      if (!distance)
        continue;
      const float t = float(distance) / 50.f;
      float u = t < .5f ? t / .5f : (t - .5f) / .5f;
      u = u * u * (3.f - 2.f * u);
      const int *a = t < .5f ? inner : mid, *b = t < .5f ? mid : outer;
      out.setPixel(x, y,
                   qRgb(byte(float(a[0]) + float(b[0] - a[0]) * u),
                        byte(float(a[1]) + float(b[1] - a[1]) * u),
                        byte(float(a[2]) + float(b[2] - a[2]) * u)));
    }
  {
    QPainter p(&out);
    p.drawImage(50, 50, source);
  }
  QImage glow = surface(out.size());
  {
    QPainter p(&glow);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(QColor(180, 80, 200, 120), 3.);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(50, 50, source.width(), source.height()));
  }
  {
    QPainter p(&out);
    p.drawImage(0, 0, blurred(glow, 4));
  }
  return straight(out);
}
inline QImage wave(const QImage &source) {
  const int w = source.width(), h = source.height(),
            xr = std::max(1, w / (std::max(2, (w / 20 + 1) / 2 * 2) - 1)),
            yr = std::max(1, h / (std::max(2, (h / 20 + 1) / 2 * 2) - 1));
  auto f = [](int t, int range) {
    return double(float(
        (1. - std::cos(double(t) * 3.14159265358979323846 / double(range))) *
        15. / 2.));
  };
  QPolygonF polygon;
  for (int x = 15; x < w - 15; ++x)
    polygon << QPointF(x, f(x, xr));
  polygon << QPointF(w - 15, f(w - 15, xr));
  for (int y = 15; y < h - 15; ++y)
    polygon << QPointF(double(w - 15) + f(y, yr), y);
  polygon << QPointF(double(w - 15) + f(h - 15, yr), h - 15);
  for (int x = w - 15; x >= 15; --x)
    polygon << QPointF(x, double(h - 15) + f(x, xr));
  polygon << QPointF(15, double(h - 15) + f(15, xr));
  for (int y = h - 15; y >= 15; --y)
    polygon << QPointF(f(y, yr), y);
  polygon << QPointF(f(15, yr), 15);
  QPainterPath path;
  path.setFillRule(Qt::WindingFill);
  path.addPolygon(polygon);
  path.closeSubpath();
  QImage out = surface(source.size());
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);
  p.setBrush(QBrush(source));
  p.drawPath(path);
  p.end();
  return straight(out);
}
inline QImage polaroid(const QImage &source) {
  const int w = source.width() + 60, h = source.height() + 110;
  QImage out = surface(QSize(w + 50, h + 50));
  QImage shadow = surface(out.size());
  {
    QPainter p(&shadow);
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(25, 25);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 50));
    p.drawRect(QRectF(0, 0, w, h));
  }
  {
    QPainter p(&out);
    p.drawImage(0, 0, blurred(shadow, 7.5f));
  }
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.translate(25, 25);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(250, 249, 245));
  p.drawRect(QRectF(0, 0, w, h));
  QPen edge(QColor(200, 198, 190), 1.);
  edge.setJoinStyle(Qt::MiterJoin);
  p.setPen(edge);
  p.setBrush(Qt::NoBrush);
  p.drawRect(QRectF(0, 0, w, h));
  p.drawImage(30, 30, source);
  QPen inner(QColor(180, 175, 165, 100), 1.);
  inner.setJoinStyle(Qt::MiterJoin);
  p.setPen(inner);
  p.drawRect(QRectF(30, 30, source.width(), source.height()));
  p.end();
  return straight(out);
}
inline QImage stars(const QImage &source) {
  const int w = source.width() + 120, h = source.height() + 120;
  QImage base(w, h, QImage::Format_ARGB32);
  base.fill(0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      if (x >= 60 && x < 60 + source.width() && y >= 60 &&
          y < 60 + source.height())
        continue;
      float r = 5.f / 255.f, g = 5.f / 255.f, b = 18.f / 255.f;
      const float a =
          std::clamp(
              (field(float(x) * .012f, float(y) * .012f, 4, 4207) + .3f) * .7f,
              0.f, 1.f) *
          .45f;
      const float c =
          std::clamp((field(float(x) * .012f * .7f + 300.f,
                            float(y) * .012f * .7f + 500.f, 3, 4207 ^ 0x33) +
                      .2f) *
                         .5f,
                     0.f, 1.f) *
          .45f * .5f;
      r += 40.f / 255.f * a + .1f * c;
      g += 10.f / 255.f * a + .15f * c;
      b += 80.f / 255.f * a + .25f * c;
      const float value = noise(x, y, 4207 ^ 0xAA),
                  threshold = 1.f - .65f * .015f;
      if (value > threshold) {
        float brightness = (value - threshold) / (1.f - threshold);
        brightness *= brightness;
        const float hue = noise(x ^ 0x55, y ^ 0x33, 4207 ^ 0xBB);
        r += brightness * (.7f + hue * .3f);
        g += brightness * (.7f + (1.f - hue) * .3f);
        b += brightness * .9f;
      }
      base.setPixel(x, y,
                    qRgb(byte(r * 255.f), byte(g * 255.f), byte(b * 255.f)));
    }
  QImage out = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  QPainter p(&out);
  p.setRenderHint(QPainter::Antialiasing);
  for (int i = 0; i < int(float(w + h) * .15f * .65f); ++i) {
    const float x = noise(i, 0, 4207 ^ 0xDE) * float(w),
                y = noise(0, i, 4207 ^ 0xEF) * float(h);
    if (x >= 60 && x < 60 + source.width() && y >= 60 &&
        y < 60 + source.height())
      continue;
    const float brightness = noise(i, i, 4207 ^ 0xF1),
                size = .8f + brightness * 2.5f,
                temp = noise(i ^ 0x77, i ^ 0x99, 4207 ^ 0xF2);
    const int r = byte(200.f + 55.f * brightness),
              g = byte(190.f + 55.f * brightness * (.9f + temp * .1f)),
              b = byte(180.f + 75.f * brightness * (.5f + temp * .5f)),
              alpha = byte(160.f + 95.f * brightness);
    const double radius = double(size * 2.5f);
    const int pad = int(std::ceil(radius + 9.f)) + 1, side = pad * 2 + 1,
              ox = int(std::floor(x)) - pad, oy = int(std::floor(y)) - pad;
    QImage patch = surface(QSize(side, side));
    {
      QPainter q(&patch);
      q.setRenderHint(QPainter::Antialiasing);
      q.setPen(Qt::NoPen);
      q.setBrush(QColor(r, g, b, alpha / 2));
      q.drawEllipse(QPointF(double(x) - ox, double(y) - oy), radius, radius);
    }
    p.drawImage(ox, oy, blurred(patch, 3, true));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(r, g, b, alpha));
    p.drawEllipse(QPointF(x, y), double(size), double(size));
  }
  p.end();
  QImage result = straight(out);
  for (int y = 0; y < source.height(); ++y)
    for (int x = 0; x < source.width(); ++x) {
      const auto s = source.pixel(x, y), d = result.pixel(x + 60, y + 60);
      if (qAlpha(s) == 255 || qAlpha(d) == 0) {
        result.setPixel(x + 60, y + 60, s);
        continue;
      }
      const float sa = float(qAlpha(s)) / 255.f, da = float(qAlpha(d)) / 255.f,
                  a = sa + da * (1.f - sa);
      auto ch = [&](int s, int d) {
        return byte((float(s) * sa + float(d) * da * (1.f - sa)) / a);
      };
      result.setPixel(x + 60, y + 60,
                      qRgba(ch(qRed(s), qRed(d)), ch(qGreen(s), qGreen(d)),
                            ch(qBlue(s), qBlue(d)), byte(a * 255.f)));
    }
  return result;
}
inline QStringList ids() {
  return {"fake_cursor",      "film_strip_frame", "gradient_fade_border",
          "pointillism",      "polaroid_frame",   "snowflakes",
          "starfield_border", "wave_edge"};
}
inline QImage render(const QString &id, const QImage &source) {
  if (id == "fake_cursor")
    return cursor(source);
  if (id == "film_strip_frame")
    return film(source);
  if (id == "gradient_fade_border")
    return gradient(source);
  if (id == "pointillism")
    return pointillism(source);
  if (id == "polaroid_frame")
    return polaroid(source);
  if (id == "snowflakes")
    return snow(source);
  if (id == "starfield_border")
    return stars(source);
  if (id == "wave_edge")
    return wave(source);
  return {};
}
} // namespace precision_reference
