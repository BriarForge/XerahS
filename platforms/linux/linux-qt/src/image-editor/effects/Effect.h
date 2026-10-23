// EDITOR-EFFECTS-001: the image-effect framework every effect implements.
// EE-001..EE-003, EE-008..EE-010, EE-016, EE-018.
//
// Pixel contract (EE-009): apply() receives and returns QImage::Format_ARGB32
// (straight, non-premultiplied alpha) in the source's colour space, which the
// caller preserves. An effect that needs premultiplied maths converts
// internally and converts back. An effect never changes the colour space tag.
// Size-changing effects (spec.changesSize) are routed by the editor through
// EDITOR-CANVAS-001's geometry and history rules (EE-006); apply() just returns
// the new-size image.
//
// Execution contract:
//  - apply() is pure: no globals, no I/O beyond declared FilePath parameters,
//    no network (EE-016), deterministic for the same input, parameters and seed.
//  - It runs on a worker thread (the editor uses OwnedWork). It polls
//    context.cancelled() at least once per row or tile and then returns
//    {{}, "effect-cancelled"}. It calls context.progress(0..100) when the work is
//    long enough for it to matter.
//  - Random or procedural effects take randomness only from context.seed (EE-008).
//  - Failure returns an empty image with a stable code; it never throws past apply().
//
// Parameters (EE-003): resolveParameters() is the only place values are
// validated. Missing → default. Wrong type, out of range, an unknown enum
// choice or an unknown parameter id → typed error naming the parameter. Values are
// never silently clamped. Serialisation is the JSON form of the resolved values.
#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <functional>
#include <memory>

namespace xerahs::effects {

enum class Category { Adjustments, Drawings, Filters, Manipulations };
enum class Mode { Previewable, Immediate };
enum class ParamKind { Float, Int, Bool, Color, Enum, Text, FilePath };

struct ParamSpec {
  QString id;              // stable, from the census (e.g. "amount")
  QString label;           // accessibility label (EE-003)
  ParamKind kind = ParamKind::Float;
  QVariant defaultValue;   // double, int, bool, QColor, QString (enum choice id / text / path)
  double minimum = 0, maximum = 0;  // inclusive bounds for Float/Int
  QStringList choices;     // stable choice ids for Enum
};

struct EffectSpec {
  QString id;              // stable census id (e.g. "alpha", "anamorphic_lens_flare")
  QString displayName;
  Category category = Category::Filters;
  Mode mode = Mode::Previewable;
  QVector<ParamSpec> parameters;
  bool changesSize = false;
  bool usesSeed = false;
  QString referencePath;   // provenance: census path of the behavioural reference
};

using ParamValues = QMap<QString, QVariant>;

struct EffectContext {
  std::function<bool()> cancelled = [] { return false; };
  std::function<void(int percent)> progress = [](int) {};
  quint64 seed = 0;
};

struct EffectResult {
  QImage image;            // Format_ARGB32; null on failure
  QString error;           // "" | effect-cancelled | effect-invalid-input | effect-unavailable | effect-failed
  bool ok() const { return error.isEmpty() && !image.isNull(); }
};

class Effect {
public:
  virtual ~Effect() = default;
  virtual const EffectSpec &spec() const = 0;
  // params are already resolved by resolveParameters().
  virtual EffectResult apply(const QImage &source, const ParamValues &params, const EffectContext &context) const = 0;
};

struct ResolvedParams {
  ParamValues values;
  QString error;           // "" | effect-unknown-parameter | effect-invalid-parameter
  QString parameter;       // the offending parameter id
  bool ok() const { return error.isEmpty(); }
};
ResolvedParams resolveParameters(const EffectSpec &spec, const ParamValues &input);
QJsonObject serializeParameters(const EffectSpec &spec, const ParamValues &resolved);
ResolvedParams deserializeParameters(const EffectSpec &spec, const QJsonObject &json);

// All built-in effects. Registration is explicit (no static-initialiser magic):
// each category file exposes registerXxxEffects(EffectRegistry &), called from
// builtinEffects().
class EffectRegistry {
public:
  bool add(std::unique_ptr<Effect> effect);  // false on a duplicate or empty id
  const Effect *find(const QString &id) const;
  QVector<const EffectSpec *> specs() const;  // ordered by category, then display name
  int size() const { return int(m_effects.size()); }
private:
  QMap<QString, std::shared_ptr<Effect>> m_effects;
};
const EffectRegistry &builtinEffects();

// Runs resolve + apply with the pixel contract enforced: converts the source to
// ARGB32, checks the result format, size (when !changesSize) and colour space,
// and maps any violation to effect-failed.
EffectResult runEffect(const Effect &effect, const QImage &source, const ParamValues &input, const EffectContext &context);

}  // namespace xerahs::effects
