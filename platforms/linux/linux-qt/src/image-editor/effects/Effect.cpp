// EDITOR-EFFECTS-001: EE-002, EE-003, EE-008..EE-010, EE-016, EE-018.
#include "Effect.h"

#include <QColorSpace>
#include <QJsonValue>
#include <algorithm>
#include <cmath>
#include <limits>

namespace xerahs::effects {
namespace {
bool numeric(const QVariant &value) {
  switch (value.typeId()) {
  case QMetaType::Double:
  case QMetaType::Float:
  case QMetaType::Int:
  case QMetaType::UInt:
  case QMetaType::LongLong:
  case QMetaType::ULongLong: return true;
  default: return false;
  }
}

bool normalize(const ParamSpec &spec, const QVariant &value, QVariant &normalized) {
  normalized = value;
  switch (spec.kind) {
  case ParamKind::Float:
  case ParamKind::Int: {
    if (!numeric(value)) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < spec.minimum || number > spec.maximum) return false;
    if (spec.kind == ParamKind::Int) {
      // JSON has one numeric type: integral JSON numbers are valid Int values.
      if (std::trunc(number) != number || number < std::numeric_limits<int>::min() ||
          number > std::numeric_limits<int>::max()) return false;
      normalized = int(number);
    } else {
      normalized = number;
    }
    return true;
  }
  case ParamKind::Bool: return value.typeId() == QMetaType::Bool;
  case ParamKind::Color: return value.typeId() == QMetaType::QColor && value.value<QColor>().isValid();
  case ParamKind::Enum:
    return value.typeId() == QMetaType::QString && spec.choices.contains(value.toString());
  case ParamKind::Text:
  case ParamKind::FilePath: return value.typeId() == QMetaType::QString;
  }
  return false;
}
}

ResolvedParams resolveParameters(const EffectSpec &spec, const ParamValues &input) {
  for (auto it = input.cbegin(); it != input.cend(); ++it) {
    if (std::none_of(spec.parameters.cbegin(), spec.parameters.cend(),
                     [&](const ParamSpec &p) { return p.id == it.key(); }))
      return {{}, QStringLiteral("effect-unknown-parameter"), it.key()};
  }
  ResolvedParams result;
  for (const auto &parameter : spec.parameters) {
    QVariant normalized;
    if (!normalize(parameter, input.value(parameter.id, parameter.defaultValue), normalized))
      return {{}, QStringLiteral("effect-invalid-parameter"), parameter.id};
    result.values.insert(parameter.id, normalized);
  }
  return result;
}

QJsonObject serializeParameters(const EffectSpec &spec, const ParamValues &resolved) {
  QJsonObject json;
  for (const auto &parameter : spec.parameters) {
    const auto value = resolved.value(parameter.id);
    if (parameter.kind == ParamKind::Color) {
      // Store floating RGBA components to retain QColor precision and alpha.
      const QColor color = value.value<QColor>().toRgb();
      json.insert(parameter.id, QJsonObject{{"r", color.redF()}, {"g", color.greenF()},
                                           {"b", color.blueF()}, {"a", color.alphaF()}});
    } else {
      json.insert(parameter.id, QJsonValue::fromVariant(value));
    }
  }
  return json;
}

ResolvedParams deserializeParameters(const EffectSpec &spec, const QJsonObject &json) {
  ParamValues input;
  for (auto it = json.begin(); it != json.end(); ++it) {
    const auto parameter = std::find_if(spec.parameters.cbegin(), spec.parameters.cend(),
                                       [&](const ParamSpec &p) { return p.id == it.key(); });
    QVariant value = it.value().toVariant();
    if (parameter != spec.parameters.cend() && parameter->kind == ParamKind::Color && it.value().isObject()) {
      const QJsonObject color = it.value().toObject();
      bool valid = color.size() == 4;
      for (const QString &key : {QStringLiteral("r"), QStringLiteral("g"), QStringLiteral("b"), QStringLiteral("a")}) {
        const QJsonValue component = color.value(key);
        valid = valid && component.isDouble() && std::isfinite(component.toDouble()) &&
                component.toDouble() >= 0 && component.toDouble() <= 1;
      }
      if (valid) value = QColor::fromRgbF(color["r"].toDouble(), color["g"].toDouble(),
                                        color["b"].toDouble(), color["a"].toDouble());
    }
    input.insert(it.key(), value);
  }
  return resolveParameters(spec, input);
}

bool EffectRegistry::add(std::unique_ptr<Effect> effect) {
  if (!effect) return false;
  const QString id = effect->spec().id;
  if (id.isEmpty() || m_effects.contains(id)) return false;
  m_effects.insert(id, std::shared_ptr<Effect>(std::move(effect)));
  return true;
}

const Effect *EffectRegistry::find(const QString &id) const {
  return m_effects.value(id).get();
}

QVector<const EffectSpec *> EffectRegistry::specs() const {
  QVector<const EffectSpec *> result;
  for (const auto &effect : m_effects) result.append(&effect->spec());
  std::sort(result.begin(), result.end(), [](const EffectSpec *a, const EffectSpec *b) {
    if (a->category != b->category) return a->category < b->category;
    const int compared = QString::compare(a->displayName, b->displayName, Qt::CaseSensitive);
    return compared == 0 ? a->id < b->id : compared < 0;
  });
  return result;
}

void registerAdjustmentEffects(EffectRegistry &registry);
void registerMoreAdjustmentEffects(EffectRegistry &registry);
void registerDrawingEffects(EffectRegistry &registry);
void registerManipulationEffects(EffectRegistry &registry);
void registerFilterEffects1(EffectRegistry &registry);
void registerManipulationEffects2(EffectRegistry &registry);
void registerFilterEffects3(EffectRegistry &registry);
const EffectRegistry &builtinEffects() {
  static const EffectRegistry registry = [] {
    EffectRegistry result;
    registerAdjustmentEffects(result);
    registerMoreAdjustmentEffects(result);
    registerDrawingEffects(result);
    registerManipulationEffects(result);
    registerFilterEffects1(result);
    registerManipulationEffects2(result);
    registerFilterEffects3(result);
    return result;
  }();
  return registry;
}

EffectResult runEffect(const Effect &effect, const QImage &source, const ParamValues &input,
                       const EffectContext &context) {
  try {
    if (context.cancelled()) return {{}, QStringLiteral("effect-cancelled")};
    if (source.isNull()) return {{}, QStringLiteral("effect-invalid-input")};
    const auto params = resolveParameters(effect.spec(), input);
    if (!params.ok()) return {{}, QStringLiteral("effect-invalid-input")};
    const QImage normalized = source.convertToFormat(QImage::Format_ARGB32);
    if (normalized.isNull()) return {{}, QStringLiteral("effect-failed")};
    EffectResult result = effect.apply(normalized, params.values, context);
    if (context.cancelled()) return {{}, QStringLiteral("effect-cancelled")};
    if (!result.error.isEmpty()) {
      if (result.error != QStringLiteral("effect-cancelled") &&
          result.error != QStringLiteral("effect-invalid-input") &&
          result.error != QStringLiteral("effect-unavailable")) result.error = QStringLiteral("effect-failed");
      return {{}, result.error};
    }
    if (result.image.isNull() || result.image.format() != QImage::Format_ARGB32 ||
        (!effect.spec().changesSize && result.image.size() != source.size()) ||
        result.image.colorSpace() != source.colorSpace()) return {{}, QStringLiteral("effect-failed")};
    return result;
  } catch (...) {
    return {{}, QStringLiteral("effect-failed")};
  }
}
}  // namespace xerahs::effects
