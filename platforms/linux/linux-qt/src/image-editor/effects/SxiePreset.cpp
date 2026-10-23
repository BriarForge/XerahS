// COMPAT-SXIE-001, EE-003, EE-007..EE-008, EE-015..EE-016, ROOT-SECURITY-001.
// Behavioural reference: ShareX.ImageEffectsLib/{ImageEffectPackager,ImageEffectPreset,
// {Adjustments,Drawings,Filters,Manipulations}/*.cs}. ShareX Team, GPLv3.
#include "SxiePreset.h"
#include "SxieArchive.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <limits>
namespace xerahs::effects {
quint64 defaultEffectSeed(const QString &id) {
  quint64 seed = 14695981039346656037ull;
  for (const auto c : id.toUtf8()) { seed ^= quint8(c); seed *= 1099511628211ull; }
  return seed;
}
namespace {
enum class Conversion { Direct, Number, Color, Enum, Point, Size, Margins, Font, Sides, Asset, Unsupported, TintMatrix };
enum class Choices { None, Placement, Border, Dash, SizeMode, RotateFlip, Interpolation, Compositing };
struct PropertyMapping {
  const char *property, *parameters;
  Conversion conversion;
  QJsonValue initial;
  double scale = 1, offset = 0;
  Choices choices = Choices::None;
};
struct Mapping {
  const char *type, *id;
  QVector<PropertyMapping> properties;
  ParamValues defaults;
};
#include "SxieMappingTable.cpp"
EffectPreset fail(const QString &code) { EffectPreset p; p.error = code; return p; }
bool validateAsset(const QByteArray &bytes) {
  QBuffer buffer; buffer.setData(bytes); if (!buffer.open(QIODevice::ReadOnly)) return false;
  QImageReader reader(&buffer); reader.setAutoTransform(false);
  if (!QList<QByteArray>{"png", "jpeg", "bmp", "gif", "webp"}.contains(reader.format())) return false;
  const auto size = reader.size();
  if (!size.isValid() || size.width() > 8192 || size.height() > 8192 || qint64(size.width()) * size.height() > 16 * 1024 * 1024) return false;
  const auto image = reader.read();
  return !image.isNull() && image.size() == size;
}
void validateStep(PresetStep &step, const EffectRegistry &registry, const QMap<QString, QByteArray> &assets) {
  const auto *effect = registry.find(step.id);
  if (!effect) { step.supported = false; step.diagnostics.append("preset-effect-unsupported"); return; }
  for (const auto &param : effect->spec().parameters) if (param.kind == ParamKind::FilePath &&
      !(step.id == "draw_image" && param.id == "image_location" &&
        step.parameters.value(param.id).toString().startsWith("asset:") && assets.contains(step.parameters.value(param.id).toString().mid(6)))) {
    step.supported = false; step.diagnostics.append("preset-asset-effect-unsupported"); return;
  }
  const auto resolved = resolveParameters(effect->spec(), step.parameters);
  if (!resolved.ok()) { step.supported = false; step.diagnostics.append(resolved.error + ":" + resolved.parameter); }
  else { step.supported = true; step.parameters = resolved.values; }
}

bool integral(const QJsonValue &value, int *result) {
  if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < std::numeric_limits<int>::min() ||
      value.toDouble() > std::numeric_limits<int>::max() || std::trunc(value.toDouble()) != value.toDouble()) return false;
  *result = int(value.toDouble()); return true;
}
bool components(const QJsonValue &value, const QStringList &names, QVector<int> *result) {
  if (value.isString()) {
    const auto parts = value.toString().split(',');
    if (parts.size() != names.size() && !(names.size() == 4 && parts.size() == 1)) return false;
    for (int i = 0; i < names.size(); ++i) {
      bool ok; int n = parts[parts.size() == 1 ? 0 : i].trimmed().toInt(&ok);
      if (!ok) return false;
      result->append(n);
    }
    return true;
  }
  const auto object = value.toObject();
  if (!value.isObject() || object.size() != names.size()) return false;
  for (const auto &name : names) { int n; if (!integral(object.value(name), &n)) return false; result->append(n); }
  return true;
}
bool legacyColor(const QJsonValue &value, QColor *color) {
  if (value.isDouble()) {
    const double n = value.toDouble();
    if (std::trunc(n) != n || n < -2147483648.0 || n > 4294967295.0) return false;
    *color = QColor::fromRgba(QRgb(qint64(n))); return true;
  }
  if (!value.isString()) return false;
  const auto text = value.toString().trimmed();
  // ColorConverter writes known names or invariant A, R, G, B / R, G, B strings.
  if (text.contains(',')) {
    const auto parts = text.split(',');
    if (parts.size() != 3 && parts.size() != 4) return false;
    QVector<int> c;
    for (const auto &part : parts) { bool ok; int n = part.trimmed().toInt(&ok); if (!ok || n < 0 || n > 255) return false; c.append(n); }
    *color = parts.size() == 4 ? QColor(c[1], c[2], c[3], c[0]) : QColor(c[0], c[1], c[2]);
  } else if (text.compare("Transparent", Qt::CaseInsensitive) == 0) *color = QColor(255, 255, 255, 0);
  else if (text.compare("LightGray", Qt::CaseInsensitive) == 0 || text.compare("LightGrey", Qt::CaseInsensitive) == 0) *color = QColor(211, 211, 211);
  else {
    if (text.startsWith('#') && text.size() != 7 && text.size() != 9) return false;
    *color = QColor(text);
  }
  return color->isValid();
}
bool choice(const QJsonValue &value, Choices kind, QString *result) {
  QStringList legacy, native; QList<int> numbers;
  switch (kind) {
  case Choices::Placement:
    legacy = {"TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight"};
    native = {"top_left", "top_center", "top_right", "middle_left", "middle_center", "middle_right", "bottom_left", "bottom_center", "bottom_right"};
    numbers = {1, 2, 4, 16, 32, 64, 256, 512, 1024}; break;
  case Choices::Border: legacy = {"Outside", "Inside"}; native = {"outside", "inside"}; break;
  case Choices::Dash: legacy = {"Solid", "Dash", "Dot", "DashDot"}; native = {"solid", "dash", "dot", "dash_dot"}; break;
  case Choices::SizeMode:
    legacy = {"DontResize", "AbsoluteSize", "PercentageOfWatermark", "PercentageOfCanvas"};
    native = {"dont_resize", "absolute_size", "percentage_of_watermark", "percentage_of_canvas"}; break;
  case Choices::RotateFlip:
    legacy = {"None", "Rotate90", "Rotate180", "Rotate270", "FlipX", "Rotate90FlipX", "FlipY", "Rotate90FlipY"};
    native = {"none", "rotate_90", "rotate_180", "rotate_270", "flip_x", "rotate_90_flip_x", "flip_y", "rotate_90_flip_y"}; break;
  case Choices::Interpolation:
    legacy = {"HighQualityBicubic", "Bicubic", "HighQualityBilinear", "Bilinear", "NearestNeighbor"};
    native = {"high_quality_bicubic", "bicubic", "high_quality_bilinear", "bilinear", "nearest_neighbor"}; break;
  case Choices::Compositing: legacy = {"SourceOver", "SourceCopy"}; native = {"source_over", "source_copy"}; break;
  case Choices::None: return false;
  }
  int index = -1, n;
  if (integral(value, &n)) index = numbers.isEmpty() ? n : numbers.indexOf(n);
  else if (value.isString()) index = legacy.indexOf(value.toString());
  if (index < 0 || index >= native.size()) return false;
  *result = native[index]; return true;
}
bool sides(const QJsonValue &value, int *flags) {
  if (integral(value, flags)) return *flags >= 0 && *flags <= 15;
  if (!value.isString()) return false;
  const QMap<QString, int> names{{"None", 0}, {"Top", 1}, {"Bottom", 2}, {"Left", 4}, {"Right", 8}};
  *flags = 0;
  for (const auto &part : value.toString().split(',')) {
    if (!names.contains(part.trimmed())) return false;
    *flags |= names.value(part.trimmed());
  }
  return true;
}
bool font(const QJsonValue &value, PresetStep &step, const QString &property) {
  if (!value.isString()) return false;
  static const QRegularExpression pattern(QStringLiteral("^(.+),\\s*([0-9]+(?:\\.[0-9]+)?)(pt|px|in|doc|mm|world|display)(?:,\\s*style=(.*))?$"));
  const auto match = pattern.match(value.toString());
  if (!match.hasMatch()) return false;
  const QMap<QString, double> units{{"pt", 96.0 / 72}, {"px", 1}, {"in", 96}, {"doc", 96.0 / 300}, {"mm", 96.0 / 25.4}, {"world", 1}, {"display", 1}};
  bool bold = false, italic = false;
  for (const auto &part : match.captured(4).split(',')) {
    const auto style = part.trimmed();
    if (style == "Bold") bold = true;
    else if (style == "Italic") italic = true;
    else if (!style.isEmpty() && style != "Regular") {
      step.diagnostics.append("preset-property-unsupported:" + property + ".Style"); return false;
    }
  }
  step.parameters.insert("font_family", match.captured(1).trimmed());
  step.parameters.insert("font_size", match.captured(2).toDouble() * units.value(match.captured(3)));
  step.parameters.insert("bold", bold); step.parameters.insert("italic", italic); return true;
}
bool convertProperty(const PropertyMapping &rule, const QJsonValue &value, PresetStep &step,
                     const QMap<QString, QByteArray> &assets) {
  const auto ids = QString::fromLatin1(rule.parameters).split('|');
  switch (rule.conversion) {
  case Conversion::Direct: step.parameters.insert(ids[0], value.toVariant()); return true;
  case Conversion::Number:
    if (!value.isDouble()) return false;
    if (!ids[0].isEmpty()) step.parameters.insert(ids[0], value.toDouble() * rule.scale + rule.offset);
    return true;
  case Conversion::Color: {
    QColor color; if (!legacyColor(value, &color)) return false;
    step.parameters.insert(ids[0], color); return true;
  }
  case Conversion::Enum: {
    QString result; if (!choice(value, rule.choices, &result)) return false;
    step.parameters.insert(ids[0], result); return true;
  }
  case Conversion::Point:
  case Conversion::Size:
  case Conversion::Margins: {
    const QStringList names = rule.conversion == Conversion::Margins ? QStringList{"Left", "Top", "Right", "Bottom"} :
        rule.conversion == Conversion::Size ? QStringList{"Width", "Height"} : QStringList{"X", "Y"};
    QVector<int> numbers; if (!components(value, names, &numbers)) return false;
    if (rule.scale < 0) for (const int number : numbers) if (number < 0) return false;
    for (int i = 0; i < ids.size(); ++i) step.parameters.insert(ids[i], numbers[i] * rule.scale);
    return true;
  }
  case Conversion::Font: return font(value, step, QString::fromLatin1(rule.property));
  case Conversion::Sides: {
    int flags; if (!sides(value, &flags)) return false;
    for (int i = 0; i < 4; ++i) step.parameters.insert(ids[i], bool(flags & (1 << i)));
    return true;
  }
  case Conversion::Asset: {
    // Exact archive-key lookup only; never expand a host path, URL or environment token.
    const auto key = value.toString();
    if (!value.isString() || !assets.contains(key)) { step.diagnostics.append("preset-asset-missing"); return false; }
    step.parameters.insert(ids[0], "asset:" + key); return true;
  }
  case Conversion::TintMatrix: {
    QColor color; if (!legacyColor(value, &color)) return false;
    const auto amount = step.legacy.value("Value");
    if (!amount.isUndefined() && !amount.isDouble()) return false;
    const double strength = amount.toDouble(0);
    const double weights[]{.212671, .715160, .072169};
    const double channels[]{color.redF(), color.greenF(), color.blueF()};
    const char *ids[3][3]{{"rr", "rg", "rb"}, {"gr", "gg", "gb"}, {"br", "bg", "bb"}};
    for (int row = 0; row < 3; ++row) for (int col = 0; col < 3; ++col)
      step.parameters.insert(ids[row][col], strength * channels[row] * weights[col] + (row == col ? 1 - strength : 0));
    return true;
  }
  case Conversion::Unsupported: return true;
  }
  return false;
}
bool neutralProperty(const PropertyMapping &rule, const QJsonValue &value, const PresetStep &step) {
  if (rule.initial.isNull() || rule.initial.isUndefined()) return true;
  const QString property = QLatin1String(rule.property);
  if (property == "Sides") { int flags; return sides(value, &flags) && flags == 15; }
  if (property == "Clip") {
    // Upsize ignores Clip; otherwise only the clipping variant matches auto_resize=false.
    return value.isBool() && (step.legacy.value("Upsize").toBool(true) || value.toBool());
  }
  if (value == rule.initial) return true;
  // These enums all assign their neutral/default member the integral value zero.
  int n;
  return rule.initial.isString() && integral(value, &n) && n == 0;
}
void mapStep(PresetStep &step, const Mapping &mapping, const EffectRegistry &registry,
             const QMap<QString, QByteArray> &assets) {
  step.id = QLatin1String(mapping.id); step.seed = defaultEffectSeed(step.id);
  if (step.id.isEmpty()) {
    step.diagnostics.append("preset-effect-unsupported");
    for (auto it = step.legacy.begin(); it != step.legacy.end(); ++it)
      if (it.key() != "$type" && it.key() != "Enabled") step.diagnostics.append("preset-property-unsupported:" + it.key());
    return;
  }
  step.parameters = mapping.defaults;
  bool valid = true; QStringList known{"$type", "Enabled"};
  for (const auto &rule : mapping.properties) {
    const QString property = QLatin1String(rule.property); known.append(property);
    const bool present = step.legacy.contains(property);
    const auto value = present ? step.legacy.value(property) : rule.initial;
    if (rule.conversion == Conversion::Unsupported) {
      const bool neutral = neutralProperty(rule, value, step);
      if (present || !neutral) step.diagnostics.append("preset-property-unsupported:" + property);
      if (!neutral) valid = false;
    } else if (!convertProperty(rule, value, step, assets)) {
      valid = false;
      step.diagnostics.append("effect-invalid-parameter:" + (QString::fromLatin1(rule.parameters).isEmpty() ? property : QString::fromLatin1(rule.parameters).section('|', 0, 0)));
    }
  }
  for (auto it = step.legacy.begin(); it != step.legacy.end(); ++it)
    if (!known.contains(it.key())) step.diagnostics.append("preset-property-unsupported:" + it.key());
  if (step.id == "resize_image") {
    const double w = step.parameters.value("width").toDouble(), h = step.parameters.value("height").toDouble();
    step.parameters.insert("maintain_aspect_ratio", (w == 0) != (h == 0));
  }
  validateStep(step, registry, assets);
  step.supported = step.supported && valid;
}

}
EffectPreset readSxiePreset(const QByteArray &bytes, const EffectRegistry &registry) {
  const auto archive = readSxieArchive(bytes); if (!archive.ok()) return fail(archive.error);
  QByteArray config; bool foundConfig = false; EffectPreset preset;
  for (auto it = archive.entries.cbegin(); it != archive.entries.cend(); ++it) {
    if (it.key().compare("Config.json", Qt::CaseInsensitive) == 0) {
      if (foundConfig) return fail("sxie-config-duplicate");
      foundConfig = true; config = it.value();
    } else if (!it.key().endsWith('/')) {
      if (!validateAsset(it.value())) return fail("sxie-asset-invalid");
      preset.assets.insert(it.key(), it.value());
    }
  }
  if (config.size() > 4 * 1024 * 1024) return fail("sxie-config-size");
  const auto json = QJsonDocument::fromJson(config);
  if (!json.isObject() || !json.object().value("Effects").isArray()) return fail("sxie-config-invalid");
  preset.legacy = json.object(); preset.name = preset.legacy.value("Name").toString();
  const auto steps = preset.legacy.value("Effects").toArray();
  if (steps.size() > 256) return fail("preset-step-limit");
  for (const auto &value : steps) {
    if (!value.isObject()) return fail("sxie-effect-invalid");
    PresetStep step; step.legacy = value.toObject();
    if (step.legacy.contains("Enabled") && !step.legacy.value("Enabled").isBool()) return fail("sxie-effect-invalid");
    step.enabled = step.legacy.value("Enabled").toBool(true);
    const auto type = step.legacy.value("$type").toString().section(',', 0, 0).trimmed();
    const Mapping *mapping = nullptr;
    for (const auto &m : mappings) if (type == QLatin1String(m.type)) { mapping = &m; break; }
    if (!mapping) step.diagnostics.append("preset-effect-unsupported");
    else mapStep(step, *mapping, registry, preset.assets);
    preset.steps.append(step);
  }
  return preset;
}
QByteArray writeXsiePreset(const EffectPreset &preset, const EffectRegistry &registry) {
  QJsonArray steps;
  for (const auto &s : preset.steps) {
    const auto *effect = registry.find(s.id);
    const auto params = effect && s.supported ? serializeParameters(effect->spec(), s.parameters) : QJsonObject::fromVariantMap(s.parameters);
    QJsonObject entry = s.extra;
    const QJsonObject fields{{"id", s.id}, {"parameters", params}, {"seed", QString::number(s.seed)},
        {"enabled", s.enabled}, {"supported", s.supported}, {"legacy", s.legacy}, {"diagnostics", QJsonArray::fromStringList(s.diagnostics)}};
    for (auto it = fields.begin(); it != fields.end(); ++it) entry.insert(it.key(), it.value());
    steps.append(entry);
  }
  QJsonObject assets;
  for (auto it = preset.assets.begin(); it != preset.assets.end(); ++it) assets.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
  QJsonObject root = preset.extra;
  const QJsonObject fields{{"format", "com.xerahs.app/effect-preset"}, {"version", 1},
      {"name", preset.name}, {"steps", steps}, {"assets", assets}, {"legacy", preset.legacy}};
  for (auto it = fields.begin(); it != fields.end(); ++it) root.insert(it.key(), it.value());
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
EffectPreset readXsiePreset(const QByteArray &bytes, const EffectRegistry &registry) {
  if (bytes.size() > 192 * 1024 * 1024) return fail("preset-size-limit");
  const auto json = QJsonDocument::fromJson(bytes); const auto root = json.object();
  if (!json.isObject() || root.value("format") != "com.xerahs.app/effect-preset" || root.value("version") != 1 ||
      !root.value("steps").isArray() || !root.value("assets").isObject() || !root.value("legacy").isObject()) return fail("preset-format-invalid");
  EffectPreset p; p.name = root.value("name").toString(); p.legacy = root.value("legacy").toObject();
  p.extra = root;
  for (const auto &key : {"format", "version", "name", "steps", "assets", "legacy"}) p.extra.remove(key);
  if (root.value("steps").toArray().size() > 256) return fail("preset-step-limit");
  qint64 total = 0; const auto assets = root.value("assets").toObject();
  if (assets.size() > 256) return fail("preset-asset-limit");
  for (auto it = assets.begin(); it != assets.end(); ++it) {
    const auto decoded = QByteArray::fromBase64Encoding(it.value().toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    total += decoded.decoded.size();
    if (!it.value().isString() || !decoded || decoded.decoded.size() > 32 * 1024 * 1024 || total > 128 * 1024 * 1024 || !validateAsset(decoded.decoded)) return fail("preset-asset-invalid");
    p.assets.insert(it.key(), decoded.decoded);
  }
  for (const auto &value : root.value("steps").toArray()) {
    const auto o = value.toObject(); PresetStep s; bool seedOk = false;
    if (!value.isObject() || !o.value("id").isString() || !o.value("parameters").isObject() || !o.value("enabled").isBool() || !o.value("supported").isBool() || !o.value("legacy").isObject()) return fail("preset-step-invalid");
    s.id = o.value("id").toString(); s.seed = o.value("seed").toString().toULongLong(&seedOk);
    if (!seedOk || QString::number(s.seed) != o.value("seed").toString()) return fail("preset-seed-invalid");
    s.extra = o;
    for (const auto &key : {"id", "parameters", "seed", "enabled", "supported", "legacy", "diagnostics"}) s.extra.remove(key);
    s.enabled = o.value("enabled").toBool(); s.legacy = o.value("legacy").toObject();
    for (const auto &d : o.value("diagnostics").toArray()) s.diagnostics.append(d.toString());
    const auto params = o.value("parameters").toObject();
    s.parameters = params.toVariantMap();
    if (o.value("supported").toBool()) {
      const auto *effect = registry.find(s.id);
      if (effect) {
        const auto resolved = deserializeParameters(effect->spec(), params);
        if (resolved.ok()) { s.parameters = resolved.values; validateStep(s, registry, p.assets); }
        else s.diagnostics.append(resolved.error + ":" + resolved.parameter);
      } else s.diagnostics.append("preset-effect-unsupported");
    }
    p.steps.append(s);
  }
  return p;
}
}
