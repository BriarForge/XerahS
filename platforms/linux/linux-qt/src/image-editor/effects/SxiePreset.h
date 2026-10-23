// COMPAT-SXIE-001, EE-003, EE-008, EE-015..EE-016.
#pragma once
#include "Effect.h"
#include <QJsonArray>
namespace xerahs::effects {
struct PresetStep {
  QString id;
  ParamValues parameters;
  quint64 seed = 0;
  bool supported = false, enabled = true;
  QJsonObject legacy, extra;
  QStringList diagnostics;
};
struct EffectPreset {
  QString name;
  QVector<PresetStep> steps;
  QMap<QString, QByteArray> assets;
  QJsonObject legacy, extra;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
quint64 defaultEffectSeed(const QString &id);
EffectPreset readSxiePreset(const QByteArray &, const EffectRegistry & = builtinEffects());
EffectPreset readXsiePreset(const QByteArray &, const EffectRegistry & = builtinEffects());
QByteArray writeXsiePreset(const EffectPreset &, const EffectRegistry & = builtinEffects());
}
