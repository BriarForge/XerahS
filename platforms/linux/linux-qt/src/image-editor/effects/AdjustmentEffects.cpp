// EDITOR-EFFECTS-001: EE-003, EE-009, EE-010, EE-016, EE-018.
// Matrix behaviour ported from ShareX ImageEditor (ShareX Team, GPLv3);
// individual census source paths are recorded in EffectSpec::referencePath.
#include "Effect.h"

#include <QColorSpace>
#include <algorithm>
#include <array>
#include <cmath>

namespace xerahs::effects {
namespace {
enum class Adjustment { Alpha, Brightness, Contrast, Grayscale, Invert, Saturation };
using Matrix = std::array<std::array<float, 5>, 4>;

Matrix matrixFor(Adjustment adjustment, float amount) {
  Matrix matrix{{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 1, 0}}};
  switch (adjustment) {
  case Adjustment::Alpha: matrix[3][3] = amount / 100.f; break;
  case Adjustment::Brightness:
    for (int c = 0; c < 3; ++c) matrix[c][4] = amount / 100.f;
    break;
  case Adjustment::Contrast: {
    float scale = (100.f + amount) / 100.f;
    scale *= scale;
    for (int c = 0; c < 3; ++c) {
      matrix[c][c] = scale;
      matrix[c][4] = 0.5f * (1.f - scale);
    }
    break;
  }
  case Adjustment::Grayscale:
  case Adjustment::Saturation: {
    const bool gray = adjustment == Adjustment::Grayscale;
    const float saturation = gray ? 1.f - amount / 100.f : 1.f + amount / 100.f;
    const float blend = 1.f - saturation;
    const std::array<float, 3> weights = gray ? std::array<float, 3>{0.2126f, 0.7152f, 0.0722f}
                                             : std::array<float, 3>{0.3086f, 0.6094f, 0.0820f};
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 3; ++col) matrix[row][col] = blend * weights[col] + (row == col ? saturation : 0.f);
    break;
  }
  case Adjustment::Invert:
    for (int c = 0; c < 3; ++c) {
      matrix[c][c] = -1;
      matrix[c][4] = 1;
    }
    break;
  }
  return matrix;
}

class MatrixAdjustment final : public Effect {
public:
  MatrixAdjustment(Adjustment adjustment, const QString &id, const QString &name, const QString &reference,
                   const QString &parameter = {}, const QString &label = {},
                   double minimum = 0, double maximum = 0, double initial = 0)
      : m_adjustment(adjustment) {
    m_spec.id = id;
    m_spec.displayName = name;
    m_spec.category = Category::Adjustments;
    m_spec.mode = adjustment == Adjustment::Invert ? Mode::Immediate : Mode::Previewable;
    if (!parameter.isEmpty()) m_spec.parameters.append({parameter, label, ParamKind::Float, initial, minimum, maximum, {}});
    m_spec.referencePath = QStringLiteral("ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects/Adjustments/") + reference;
  }
  const EffectSpec &spec() const override { return m_spec; }
  EffectResult apply(const QImage &source, const ParamValues &params, const EffectContext &context) const override {
    try {
      if (context.cancelled()) return {{}, QStringLiteral("effect-cancelled")};
      const float amount = m_spec.parameters.isEmpty() ? 0.f : params.value(m_spec.parameters.front().id).toFloat();
      const Matrix matrix = matrixFor(m_adjustment, amount);
      QImage result(source.size(), QImage::Format_ARGB32);
      if (result.isNull()) return {{}, QStringLiteral("effect-failed")};
      result.setColorSpace(source.colorSpace());
      context.progress(0);
      for (int y = 0; y < source.height(); ++y) {
        if (context.cancelled()) return {{}, QStringLiteral("effect-cancelled")};
        const auto *input = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *output = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
          const std::array<float, 4> channels{float(qRed(input[x])), float(qGreen(input[x])),
                                             float(qBlue(input[x])), float(qAlpha(input[x]))};
          std::array<int, 4> transformed;
          for (int row = 0; row < 4; ++row) {
            float value = matrix[row][4] * 255.f;
            for (int col = 0; col < 4; ++col) value += matrix[row][col] * channels[col];
            // Matrix outputs clip to the byte gamut, rounded to nearest byte.
            transformed[row] = int(std::floor(std::clamp(value, 0.f, 255.f) + 0.5f));
          }
          output[x] = qRgba(transformed[0], transformed[1], transformed[2], transformed[3]);
        }
        const int before = int(qint64(y) * 100 / source.height());
        const int after = int(qint64(y + 1) * 100 / source.height());
        if (after != before) context.progress(after);
      }
      if (context.cancelled()) return {{}, QStringLiteral("effect-cancelled")};
      return {result, {}};
    } catch (...) {
      return {{}, QStringLiteral("effect-failed")};
    }
  }
private:
  Adjustment m_adjustment;
  EffectSpec m_spec;
};
}

void registerAdjustmentEffects(EffectRegistry &registry) {
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Alpha, "alpha", "Alpha", "AlphaImageEffect.cs",
                                                 "amount", "Alpha", 0, 100, 100));
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Brightness, "brightness", "Brightness", "BrightnessImageEffect.cs",
                                                 "amount", "Amount", -100, 100, 0));
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Contrast, "contrast", "Contrast", "ContrastImageEffect.cs",
                                                 "amount", "Amount", -100, 100, 0));
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Grayscale, "grayscale", "Grayscale", "GrayscaleImageEffect.cs",
                                                 "strength", "Strength", 0, 100, 100));
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Invert, "invert", "Invert", "InvertImageEffect.cs"));
  registry.add(std::make_unique<MatrixAdjustment>(Adjustment::Saturation, "saturation", "Saturation", "SaturationImageEffect.cs",
                                                 "amount", "Amount", -100, 100, 0));
}
}  // namespace xerahs::effects
