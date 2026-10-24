// EDITOR-EFFECTS-001: helpers shared by the built-in effect files. EE-007, EE-018.
#include "EffectSupport.h"

#include <QFileInfo>
#include <QImageReader>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace xerahs::effects::support {
namespace {
bool sizeAcceptable(const QSize &size) {
  return size.isValid() && size.width() > 0 && size.height() > 0 && size.width() <= kMaxImageSide &&
         size.height() <= kMaxImageSide;
}

}

QImage flipAxisImage(const QImage &source, Qt::Orientation axis) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  return source.flipped(axis);
#else
  return source.mirrored(axis == Qt::Horizontal, axis == Qt::Vertical);
#endif
}

// Skia clamps a filtered channel to the byte gamut and rounds it to the nearest byte.
int filteredByte(float value) { return int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f)); }

bool validateImageFile(const QString &path) {
  if (path.trimmed().isEmpty()) return false;
  const QFileInfo info(path);
  if (!info.exists() || !info.isFile()) return false;
  QImageReader reader(path);
  if (!reader.canRead()) return false;
  // Some formats do not report a size before decoding; loadImageFile re-checks after the read.
  const QSize size = reader.size();
  return !size.isValid() || sizeAcceptable(size);
}

QImage loadImageFile(const QString &path) {
  if (!validateImageFile(path)) return {};
  QImageReader reader(path);
  QImage image = reader.read();
  if (image.isNull() || !sizeAcceptable(image.size())) return {};
  return image.convertToFormat(QImage::Format_ARGB32);
}

EffectResult convolve3x3(const QImage &source, const std::array<float, 9> &kernel, float gain, float bias,
                         bool convolveAlpha, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  QImage result = makeResult(source);
  if (result.isNull()) return {{}, kFailed};
  const int width = source.width();
  const int height = source.height();
  const bool done = runRows(context, height, 0, 100, [&](int y) {
    const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      float sum[4] = {0.f, 0.f, 0.f, 0.f};
      for (int j = 0; j < 3; ++j) {
        const int sy = std::clamp(y + j - 1, 0, height - 1);
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(sy));
        for (int i = 0; i < 3; ++i) {
          const int sx = std::clamp(x + i - 1, 0, width - 1);
          const QRgb pixel = row[sx];
          const float coefficient = kernel[size_t(j * 3 + i)];
          sum[0] += coefficient * float(qRed(pixel));
          sum[1] += coefficient * float(qGreen(pixel));
          sum[2] += coefficient * float(qBlue(pixel));
          sum[3] += coefficient * float(qAlpha(pixel));
        }
      }
      const int alpha = convolveAlpha ? filteredByte(sum[3] * gain + bias) : qAlpha(input[x]);
      output[x] = qRgba(filteredByte(sum[0] * gain + bias), filteredByte(sum[1] * gain + bias),
                        filteredByte(sum[2] * gain + bias), alpha);
    }
  });
  if (!done || context.cancelled()) return {{}, kCancelled};
  return {result, {}};
}

QImage approximateGaussianBlur(const QImage &source, float sigma, BlurBorder border, const EffectContext &context,
                               int progressLo, int progressHi) {
  if (context.cancelled() || source.isNull()) return {};
  const int width = source.width(), height = source.height();
  const int span = std::max(1, progressHi - progressLo);
  auto phase = [&](int index) { return progressLo + span * index / 3; };
  if (!(sigma > 0.f)) {
    QImage copy(source.size(), QImage::Format_ARGB32);
    if (copy.isNull()) return {};
    copy.setColorSpace(source.colorSpace());
    if (!runRows(context, height, progressLo, progressHi, [&](int y) {
          std::memcpy(copy.scanLine(y), source.constScanLine(y), size_t(width) * 4);
        }))
      return {};
    return copy;
  }
  const int radius = std::max(1, int(std::ceil(double(sigma) * 3.0)));
  std::vector<float> kernel(size_t(radius * 2 + 1));
  const float sigmaSq = sigma * sigma;
  float weight = 0.f;
  for (int i = -radius; i <= radius; ++i) {
    const float value = std::exp(-0.5f * float(i * i) / sigmaSq);
    kernel[size_t(i + radius)] = value;
    weight += value;
  }
  for (float &value : kernel) value /= weight;

  std::vector<float> in(size_t(width) * size_t(height) * 4), rowBuffer(size_t(width) * 4);
  if (!runRows(context, height, phase(0), phase(1), [&](int y) {
        const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        float *dst = in.data() + size_t(y) * size_t(width) * 4;
        for (int x = 0; x < width; ++x) {
          const float alpha = qAlpha(row[x]) / 255.f;
          dst[x * 4] = qRed(row[x]) / 255.f * alpha;
          dst[x * 4 + 1] = qGreen(row[x]) / 255.f * alpha;
          dst[x * 4 + 2] = qBlue(row[x]) / 255.f * alpha;
          dst[x * 4 + 3] = alpha;
        }
      }))
    return {};

  auto sample = [&](const std::vector<float> &buffer, int x, int y, int widthLimit, int heightLimit, int channel) {
    if (x < 0 || y < 0 || x >= widthLimit || y >= heightLimit) {
      if (border == BlurBorder::Decal) return 0.f;
      x = std::clamp(x, 0, widthLimit - 1);
      y = std::clamp(y, 0, heightLimit - 1);
    }
    return buffer[(size_t(y) * size_t(widthLimit) + size_t(x)) * 4 + size_t(channel)];
  };
  // Horizontal results replace a row only after all its source samples have been read.
  if (!runRows(context, height, phase(1), phase(2), [&](int y) {
        for (int x = 0; x < width; ++x) {
          float *dst = rowBuffer.data() + size_t(x) * 4;
          for (int c = 0; c < 4; ++c) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; ++k) sum += kernel[size_t(k + radius)] * sample(in, x + k, y, width, height, c);
            dst[c] = sum;
          }
        }
        std::memcpy(in.data() + size_t(y) * size_t(width) * 4, rowBuffer.data(), rowBuffer.size() * sizeof(float));
      }))
    return {};
  QImage result = makeResult(source);
  if (result.isNull()) return {};
  if (!runRows(context, height, phase(2), progressHi, [&](int y) {
        for (int x = 0; x < width; ++x) {
          float blurred[4]{0, 0, 0, 0};
          for (int c = 0; c < 4; ++c) {
            float sum = 0.f;
            for (int k = -radius; k <= radius; ++k)
              sum += kernel[size_t(k + radius)] * sample(in, x, y + k, width, height, c);
            blurred[c] = sum;
          }
          auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
          const float alpha = blurred[3];
          if (alpha <= 1e-6f) {
            row[x] = 0;
            continue;
          }
          auto channel = [&](float premul) {
            const float straight = std::clamp(premul / alpha, 0.f, 1.f);
            return int(std::nearbyintf(std::clamp(straight, 0.f, 1.f) * 255.f));
          };
          row[x] = qRgba(channel(blurred[0]), channel(blurred[1]), channel(blurred[2]), int(std::nearbyintf(std::clamp(alpha, 0.f, 1.f) * 255.f)));
        }
      }))
    return {};
  return result;
}

QRgb bilinear(const QImage &image, float x, float y) {
  const int w = image.width(), h = image.height();
  x = std::clamp(x, 0.f, float(w - 1));
  y = std::clamp(y, 0.f, float(h - 1));
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const int x1 = x0 < w - 1 ? x0 + 1 : x0;
  const int y1 = y0 < h - 1 ? y0 + 1 : y0;
  const float tx = x - float(x0), ty = y - float(y0);
  const auto *row0 = reinterpret_cast<const QRgb *>(image.constScanLine(y0));
  const auto *row1 = reinterpret_cast<const QRgb *>(image.constScanLine(y1));
  const QRgb c00 = row0[x0], c10 = row0[x1], c01 = row1[x0], c11 = row1[x1];
  auto ch = [&](int (*part)(QRgb)) {
    return toByte(lerp(lerp(float(part(c00)), float(part(c10)), tx), lerp(float(part(c01)), float(part(c11)), tx), ty));
  };
  return qRgba(ch(qRed), ch(qGreen), ch(qBlue), ch(qAlpha));
}

float clamp01(float v) { return v <= 0.f ? 0.f : (v >= 1.f ? 1.f : v); }

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float smoothStep(float e0, float e1, float v) {
  if (std::fabs(e1 - e0) < 0.000001f) return v >= e1 ? 1.f : 0.f;
  const float t = clamp01((v - e0) / (e1 - e0));
  return t * t * (3.f - 2.f * t);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int toByte(float v) {  // ClampToByte: MathF.Round is half to even
  if (std::isnan(v) || v <= 0.f) return 0;
  if (v >= 255.f) return 255;
  return int(std::nearbyintf(v));
}

float hash01(int x, int y, int seed) {
  uint32_t h = 2166136261u;
  h = (h ^ uint32_t(x)) * 16777619u;
  h = (h ^ uint32_t(y)) * 16777619u;
  h = (h ^ uint32_t(seed)) * 16777619u;
  h ^= h >> 13;
  h *= 1274126177u;
  h ^= h >> 16;
  return float(h & 0x00FFFFFFu) / 16777215.f;
}

float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

float valueNoise(float x, float y, int seed) {
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const float tx = fade(x - float(x0)), ty = fade(y - float(y0));
  const float v00 = hash01(x0, y0, seed), v10 = hash01(x0 + 1, y0, seed);
  const float v01 = hash01(x0, y0 + 1, seed), v11 = hash01(x0 + 1, y0 + 1, seed);
  return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

float fractalNoise(float x, float y, int octaves, float lacunarity, float gain, int seed) {
  float value = 0.f, amplitude = 1.f, frequency = 1.f, normalization = 0.f;
  for (int i = 0; i < octaves; ++i) {
    value += valueNoise(x * frequency, y * frequency, seed + i * 1619) * amplitude;
    normalization += amplitude;
    frequency *= lacunarity;
    amplitude *= gain;
  }
  return normalization > 0.f ? value / normalization : 0.f;
}

float luminance(QRgb c) { return 0.2126f * float(qRed(c)) + 0.7152f * float(qGreen(c)) + 0.0722f * float(qBlue(c)); }

float luminance01(QRgb c) { return luminance(c) / 255.f; }

float smoothStepRange(float edge0, float edge1, float x) {
  const float range = edge1 - edge0;
  if (range <= 0.f) return x >= edge1 ? 1.f : 0.f;
  const float t = std::clamp((x - edge0) / range, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}

QRgb bilinear(const Pixels &p, float x, float y) {
  const int right = p.w - 1, bottom = p.h - 1;
  x = std::clamp(x, 0.f, float(right));
  y = std::clamp(y, 0.f, float(bottom));
  const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
  const int x1 = x0 < right ? x0 + 1 : right, y1 = y0 < bottom ? y0 + 1 : bottom;
  const float tx = x - float(x0), ty = y - float(y0);
  const QRgb c00 = p.at(x0, y0), c10 = p.at(x1, y0), c01 = p.at(x0, y1), c11 = p.at(x1, y1);
  auto channel = [&](int (*f)(QRgb)) {
    return toByte(lerp(lerp(float(f(c00)), float(f(c10)), tx), lerp(float(f(c01)), float(f(c11)), tx), ty));
  };
  return qRgba(channel(qRed), channel(qGreen), channel(qBlue), channel(qAlpha));
}

FloatImage toPremul(const QImage &image) {
  FloatImage f;
  f.w = image.width();
  f.h = image.height();
  f.d.resize(size_t(f.w) * size_t(f.h) * 4);
  const Pixels p(image);
  for (size_t i = 0; i < size_t(f.w) * size_t(f.h); ++i) {
    const QRgb c = p.data[i];
    const float a = float(qAlpha(c)) / 255.f;
    f.d[i * 4] = float(qRed(c)) * a;
    f.d[i * 4 + 1] = float(qGreen(c)) * a;
    f.d[i * 4 + 2] = float(qBlue(c)) * a;
    f.d[i * 4 + 3] = float(qAlpha(c));
  }
  return f;
}

QRgb fromPremulPixel(const float *v) {
  const float a = v[3];
  if (a <= 0.f) return 0;
  const float inv = 255.f / a;
  return qRgba(toByte(v[0] * inv), toByte(v[1] * inv), toByte(v[2] * inv), toByte(a));
}

QImage fromPremul(const FloatImage &f, const QImage &like) {
  QImage out = makeResult(like);
  if (out.isNull()) throw std::bad_alloc();
  auto *dst = reinterpret_cast<QRgb *>(out.bits());
  for (size_t i = 0; i < size_t(f.w) * size_t(f.h); ++i) dst[i] = fromPremulPixel(&f.d[i * 4]);
  return out;
}

bool gaussianBlur255(FloatImage &f, float sigma, bool clampEdges, const EffectContext &context) {
  if (sigma <= 0.0001f || f.w <= 0 || f.h <= 0) return true;
  const int radius = std::max(1, int(std::ceil(sigma * 3.f)));
  std::vector<float> kernel(size_t(radius) * 2 + 1);
  float sum = 0.f;
  for (int i = -radius; i <= radius; ++i) {
    const float v = std::exp(-float(i * i) / (2.f * sigma * sigma));
    kernel[size_t(i + radius)] = v;
    sum += v;
  }
  for (float &v : kernel) v /= sum;
  // One full float image plus a row/column scratch, preserving tap order.
  std::vector<float> tmp(size_t(std::max(f.w, f.h)) * 4);
  const int w = f.w, h = f.h;
  for (int y = 0; y < h; ++y) {
    if (context.cancelled()) return false;
    for (int x = 0; x < w; ++x) {
      float acc[4]{};
      for (int k = -radius; k <= radius; ++k) {
        int sx = x + k;
        if (sx < 0 || sx >= w) {
          if (!clampEdges) continue;
          sx = clampInt(sx, 0, w - 1);
        }
        const float *s = &f.d[(size_t(y) * size_t(w) + size_t(sx)) * 4];
        const float wt = kernel[size_t(k + radius)];
        for (int c = 0; c < 4; ++c) acc[c] += s[c] * wt;
      }
      std::memcpy(&tmp[size_t(x) * 4], acc, sizeof(acc));
    }
    std::memcpy(&f.d[size_t(y) * size_t(w) * 4], tmp.data(), size_t(w) * 4 * sizeof(float));
  }
  for (int x = 0; x < w; ++x) {
    if (context.cancelled()) return false;
    for (int y = 0; y < h; ++y)
      std::memcpy(&tmp[size_t(y) * 4], &f.d[(size_t(y) * size_t(w) + size_t(x)) * 4], 4 * sizeof(float));
    for (int y = 0; y < h; ++y) {
      float acc[4]{};
      for (int k = -radius; k <= radius; ++k) {
        int sy = y + k;
        if (sy < 0 || sy >= h) {
          if (!clampEdges) continue;
          sy = clampInt(sy, 0, h - 1);
        }
        const float *s = &tmp[size_t(sy) * 4];
        const float wt = kernel[size_t(k + radius)];
        for (int c = 0; c < 4; ++c) acc[c] += s[c] * wt;
      }
      std::memcpy(&f.d[(size_t(y) * size_t(w) + size_t(x)) * 4], acc, sizeof(acc));
    }
  }
  return true;
}

bool blurredClamp(const QImage &source, float radius, const EffectContext &context, QImage &out) {
  if (radius <= 0.01f) {
    out = source;
    return true;
  }
  FloatImage f = toPremul(source);
  if (!gaussianBlur255(f, std::max(0.001f, radius / 3.f), true, context)) return false;
  out = fromPremul(f, source);
  return true;
}

EffectResult identityCopy(const QImage &source, const EffectContext &context) {
  if (context.cancelled()) return {{}, kCancelled};
  return {copyOf(source), {}};
}

QImage copyOf(const QImage &source) {
  QImage result = source.copy();
  result.setColorSpace(source.colorSpace());
  return result;
}

int seedOf(const EffectContext &context) { return int(uint32_t(context.seed ^ (context.seed >> 32))); }

void hsvToRgb(float h, float s, float v, float &r, float &g, float &b) {
  if (s <= 0.0001f) {
    r = g = b = v;
    return;
  }
  const float scaled = h * 6.f;
  const int sector = int(std::floor(scaled));
  const float frac = scaled - float(sector);
  const float p = v * (1.f - s);
  const float q = v * (1.f - (s * frac));
  const float t = v * (1.f - (s * (1.f - frac)));
  switch (sector) {
  case 0: r = v; g = t; b = p; break;
  case 1: r = q; g = v; b = p; break;
  case 2: r = p; g = v; b = t; break;
  case 3: r = p; g = q; b = v; break;
  case 4: r = t; g = p; b = v; break;
  default: r = v; g = p; b = q; break;
  }
}

QRgb fromHsv(float h, float s, float v) {
  const float hue = h / 360.f, sat = s / 100.f, val = v / 100.f;
  float r = val, g = val, b = val;
  if (sat != 0.f) {
    const float scaled = hue * 6.f;
    const int sextant = int(scaled);
    const float fract = scaled - float(sextant);
    const float p = val * (1.f - sat), q = val * (1.f - sat * fract), t = val * (1.f - sat * (1.f - fract));
    switch (sextant) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    default: r = val; g = p; b = q; break;
    }
  }
  return qRgba(clampInt(int(r * 255.f), 0, 255), clampInt(int(g * 255.f), 0, 255), clampInt(int(b * 255.f), 0, 255), 255);
}

float screenClamped(float b, float s) { return 1.f - (1.f - clamp01(b)) * (1.f - clamp01(s)); }

float screenOverlay(float source, float overlay) { return 1.f - (1.f - source) * (1.f - clamp01(overlay)); }

void rgbToHsl(int r, int g, int b, float &h, float &s, float &l) {
  const float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
  const float maxv = std::max(rf, std::max(gf, bf)), minv = std::min(rf, std::min(gf, bf));
  l = (maxv + minv) / 2.f;
  const float d = maxv - minv;
  if (d == 0.f) {
    h = 0.f;
    s = 0.f;
  } else {
    s = l > 0.5f ? d / (2.f - maxv - minv) : d / (maxv + minv);
    if (maxv == rf) h = (gf - bf) / d + (gf < bf ? 6.f : 0.f);
    else if (maxv == gf) h = (bf - rf) / d + 2.f;
    else h = (rf - gf) / d + 4.f;
    h *= 60.f;
  }
  s *= 100.f;
  l *= 100.f;
}

float hueToRgb(float p, float q, float t) {
  if (t < 0.f) t += 1.f;
  if (t > 1.f) t -= 1.f;
  if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
  if (t < 0.5f) return q;
  if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
  return p;
}

QRgb hslToRgb(float h, float s, float l, int a) {
  s = std::clamp(s, 0.f, 100.f) / 100.f;
  l = std::clamp(l, 0.f, 100.f) / 100.f;
  float rf, gf, bf;
  if (s == 0.f) {
    rf = gf = bf = l;
  } else {
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    const float hk = h / 360.f;
    rf = hueToRgb(p, q, hk + 1.f / 3.f);
    gf = hueToRgb(p, q, hk);
    bf = hueToRgb(p, q, hk - 1.f / 3.f);
  }
  return qRgba(toByte(rf * 255.f), toByte(gf * 255.f), toByte(bf * 255.f), a);
}

void toHsl(QRgb c, float &h, float &s, float &l) {
  const float r = float(qRed(c)) / 255.f, g = float(qGreen(c)) / 255.f, b = float(qBlue(c)) / 255.f;
  const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
  l = (mx + mn) / 2.f;
  if (mx == mn) {
    h = s = 0.f;
  } else {
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h /= 6.f;
  }
  h *= 360.f;
  s *= 100.f;
  l *= 100.f;
}

QRgb fromHsl(float h, float s, float l, int alpha) {
  while (h < 0.f) h += 360.f;
  while (h >= 360.f) h -= 360.f;
  s = std::clamp(s, 0.f, 100.f);
  l = std::clamp(l, 0.f, 100.f);
  float hue = h / 360.f;
  const float sat = s / 100.f, lum = l / 100.f;
  float r = lum, g = lum, b = lum;
  const float v = lum <= 0.5f ? lum * (1.f + sat) : lum + sat - lum * sat;
  if (v > 0.f) {
    const float m = lum + lum - v;
    const float sv = (v - m) / v;
    hue *= 6.f;
    const int sextant = int(hue);
    const float fract = hue - float(sextant);
    const float vsf = v * sv * fract, mid1 = m + vsf, mid2 = v - vsf;
    switch (sextant) {
    case 0: r = v; g = mid1; b = m; break;
    case 1: r = mid2; g = v; b = m; break;
    case 2: r = m; g = v; b = mid1; break;
    case 3: r = m; g = mid2; b = v; break;
    case 4: r = mid1; g = m; b = v; break;
    default: r = v; g = m; b = mid2; break;
    }
  }
  return qRgba(clampInt(int(r * 255.f), 0, 255), clampInt(int(g * 255.f), 0, 255), clampInt(int(b * 255.f), 0, 255), alpha);
}

void boxHorizontal(const std::vector<float> &in, std::vector<float> &out, int width, int y, int left, int right) {
  const float *src = in.data() + size_t(y) * size_t(width) * 4;
  float *dst = out.data() + size_t(y) * size_t(width) * 4;
  const float scale = 1.f / float(left + right + 1);
  for (int c = 0; c < 4; ++c) {
    double sum = 0;
    for (int k = 0; k <= std::min(right, width - 1); ++k) sum += src[k * 4 + c];
    for (int x = 0; x < width; ++x) {
      dst[x * 4 + c] = float(sum) * scale;
      if (x + right + 1 < width) sum += src[(x + right + 1) * 4 + c];
      if (x - left >= 0) sum -= src[(x - left) * 4 + c];
    }
  }
}

void boxVertical(const std::vector<float> &in, std::vector<float> &out, int width, int height, int left, int right,
                 const EffectContext &context, int lo, int hi, bool &cancelled) {
  std::vector<double> sums(size_t(width) * 4, 0.0);
  const size_t stride = size_t(width) * 4;
  for (int k = 0; k <= std::min(right, height - 1); ++k)
    for (size_t i = 0; i < stride; ++i) sums[i] += in[size_t(k) * stride + i];
  const float scale = 1.f / float(left + right + 1);
  cancelled = !runRows(context, height, lo, hi, [&](int y) {
    float *dst = out.data() + size_t(y) * stride;
    for (size_t i = 0; i < stride; ++i) dst[i] = float(sums[i]) * scale;
    if (y + right + 1 < height)
      for (size_t i = 0; i < stride; ++i) sums[i] += in[size_t(y + right + 1) * stride + i];
    if (y - left >= 0)
      for (size_t i = 0; i < stride; ++i) sums[i] -= in[size_t(y - left) * stride + i];
  });
}

void fillRect(QImage &image, float left, float top, float right, float bottom, QRgb color) {
  const int x0 = std::max(0, int(std::ceil(left - 0.5f)));
  const int x1 = std::min(image.width(), int(std::ceil(right - 0.5f)));
  const int y0 = std::max(0, int(std::ceil(top - 0.5f)));
  const int y1 = std::min(image.height(), int(std::ceil(bottom - 0.5f)));
  for (int y = y0; y < y1; ++y) {
    auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = x0; x < x1; ++x) line[x] = color;
  }
}

void copyRows(QImage &dest, const QImage &source, int offsetX, int offsetY) {
  if (offsetX < 0 || offsetX + source.width() > dest.width()) return;
  for (int y = 0; y < source.height(); ++y) {
    const int dy = y + offsetY;
    if (dy < 0 || dy >= dest.height()) continue;
    auto *line = reinterpret_cast<QRgb *>(dest.scanLine(dy));
    std::memcpy(line + offsetX, source.constScanLine(y), size_t(source.width()) * sizeof(QRgb));
  }
}

QImage borderedCanvas(const QImage &source, int border) {
  QImage out(source.width() + border * 2, source.height() + border * 2, QImage::Format_ARGB32);
  out.setColorSpace(source.colorSpace());
  out.fill(0);
  return out;
}

QRgb sourceOverBorder(QRgb dst, QRgb src) {
  const float sa = float(qAlpha(src)) / 255.f, da = float(qAlpha(dst)) / 255.f;
  const float oa = sa + da * (1.f - sa);
  if (oa <= 0.f) return 0;
  auto ch = [&](int s, int d) { return toByte((float(s) * sa + float(d) * da * (1.f - sa)) / oa); };
  return qRgba(ch(qRed(src), qRed(dst)), ch(qGreen(src), qGreen(dst)), ch(qBlue(src), qBlue(dst)), toByte(oa * 255.f));
}

void copyInterior(const QImage &source, QImage &out, int border) {
  for (int y = 0; y < source.height(); ++y) {
    const auto *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
    auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y + border)) + border;
    for (int x = 0; x < source.width(); ++x) dst[x] = (qAlpha(in[x]) == 255 || qAlpha(dst[x]) == 0) ? in[x] : sourceOverBorder(dst[x], in[x]);
  }
}

int xorshift(int state) {  // 32-bit xorshift with C# int semantics (arithmetic >> on the signed value)
  uint32_t u = uint32_t(state);
  u ^= u << 13;
  int s = int(u);
  s ^= s >> 17;
  u = uint32_t(s);
  u ^= u << 5;
  return int(u);
}

QRgb lerpColor(QRgb from, QRgb to, float t) {
  const float a = clamp01(t);
  return qRgba(toByte(lerp(float(qRed(from)), float(qRed(to)), a)), toByte(lerp(float(qGreen(from)), float(qGreen(to)), a)),
               toByte(lerp(float(qBlue(from)), float(qBlue(to)), a)), toByte(lerp(float(qAlpha(from)), float(qAlpha(to)), a)));
}

QRgb sourceOverTop(QRgb top, QRgb bottom) {
  const int ta = qAlpha(top);
  if (ta == 255) return top;
  if (ta == 0) return bottom;
  const float a1 = ta / 255.f, a2 = qAlpha(bottom) / 255.f;
  const float outA = a1 + a2 * (1.f - a1);
  if (outA <= 0.f) return 0;
  auto channel = [&](int t, int b) { return int(std::nearbyintf((t * a1 + b * a2 * (1.f - a1)) / outA)); };
  return qRgba(channel(qRed(top), qRed(bottom)), channel(qGreen(top), qGreen(bottom)),
               channel(qBlue(top), qBlue(bottom)), int(std::nearbyintf(outA * 255.f)));
}

}  // namespace xerahs::effects::support
