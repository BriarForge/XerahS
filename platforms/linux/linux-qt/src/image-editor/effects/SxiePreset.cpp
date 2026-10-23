// COMPAT-SXIE-001, EE-003, EE-007..EE-008, EE-015..EE-016, ROOT-SECURITY-001.
// Behavioural reference: ShareX.ImageEffectsLib/{ImageEffectPackager,ImageEffectPreset,
// Adjustments/{Brightness,Alpha,Grayscale,Saturation,Gamma}}.cs (ShareX Team, GPLv3).
#include "SxiePreset.h"
#include "SxieArchive.h"
#include <QBuffer>
#include <QImageReader>
#include <QJsonDocument>
#include <cmath>
namespace xerahs::effects {
quint64 defaultEffectSeed(const QString &id) {
  quint64 seed = 14695981039346656037ull;
  for (const auto c : id.toUtf8()) { seed ^= quint8(c); seed *= 1099511628211ull; }
  return seed;
}
namespace {
struct Mapping { const char *type, *id, *property, *parameter; double scale, offset, initial; };
const Mapping mappings[] = {
  {"ShareX.ImageEffectsLib.Brightness", "brightness", "Value", "amount", 100, 0, 0},
  {"ShareX.ImageEffectsLib.Alpha", "alpha", "Value", "amount", 100, 0, 1},
  {"ShareX.ImageEffectsLib.Gamma", "gamma", "Value", "amount", 1, 0, 1},
  {"ShareX.ImageEffectsLib.Hue", "hue", "Angle", "amount", 1, 0, 0},
  {"ShareX.ImageEffectsLib.Pixelate", "pixelate", "Size", "size", 1, 0, 10},
  {"ShareX.ImageEffectsLib.Inverse", "invert", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.BlackWhite", "black_and_white", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.Polaroid", "polaroid", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.Emboss", "emboss", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.MeanRemoval", "mean_removal", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.Smooth", "smooth", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.EdgeDetect", "edge_detect", "", "", 1, 0, 0},
  {"ShareX.ImageEffectsLib.Sharpen", "sharpen", "", "strength", 1, 0, 100}
};
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

bool enumValue(const QJsonValue &value, const QStringList &legacy, const QStringList &native, QString *result) {
  int index = value.isDouble() && value.toDouble() == value.toInt(-1) ? value.toInt(-1) : legacy.indexOf(value.toString());
  if (index < 0 || index >= native.size()) return false;
  *result = native[index]; return true;
}
bool pairValue(const QJsonValue &value, const QString &first, const QString &second, int *a, int *b) {
  if (value.isString()) {
    const auto parts = value.toString().split(','); bool okA = false, okB = false;
    if (parts.size() != 2) return false;
    *a = parts[0].trimmed().toInt(&okA); *b = parts[1].trimmed().toInt(&okB); return okA && okB;
  }
  const auto o = value.toObject();
  if (o.size() != 2 || !o.value(first).isDouble() || !o.value(second).isDouble()) return false;
  *a = o.value(first).toInt(); *b = o.value(second).toInt();
  return o.value(first).toDouble() == *a && o.value(second).toDouble() == *b;
}
void mapDrawImage(PresetStep &step, const EffectRegistry &registry, const QMap<QString, QByteArray> &assets) {
  step.id = "draw_image"; step.seed = defaultEffectSeed(step.id); bool valid = true;
  const QMap<QString, QString> direct{{"Tile", "tile"}, {"AutoHide", "auto_hide"}, {"Opacity", "opacity"}};
  const QMap<QString, QPair<QStringList, QStringList>> enums{
    {"Placement", {{"TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight"},
                   {"top_left", "top_center", "top_right", "middle_left", "middle_center", "middle_right", "bottom_left", "bottom_center", "bottom_right"}}},
    {"SizeMode", {{"DontResize", "AbsoluteSize", "PercentageOfWatermark", "PercentageOfCanvas"}, {"dont_resize", "absolute_size", "percentage_of_watermark", "percentage_of_canvas"}}},
    {"RotateFlip", {{"None", "Rotate90", "Rotate180", "Rotate270", "FlipX", "Rotate90FlipX", "FlipY", "Rotate90FlipY"}, {"none", "rotate_90", "rotate_180", "rotate_270", "flip_x", "rotate_90_flip_x", "flip_y", "rotate_90_flip_y"}}},
    {"InterpolationMode", {{"HighQualityBicubic", "Bicubic", "HighQualityBilinear", "Bilinear", "NearestNeighbor"}, {"high_quality_bicubic", "bicubic", "high_quality_bilinear", "bilinear", "nearest_neighbor"}}},
    {"CompositingMode", {{"SourceOver", "SourceCopy"}, {"source_over", "source_copy"}}}
  };
  const QMap<QString, QString> enumIds{{"Placement", "placement"}, {"SizeMode", "size_mode"}, {"RotateFlip", "rotate_flip"}, {"InterpolationMode", "interpolation_mode"}, {"CompositingMode", "compositing_mode"}};
  for (auto it = step.legacy.begin(); it != step.legacy.end(); ++it) {
    if (it.key() == "$type" || it.key() == "Enabled") continue;
    if (it.key() == "ImageLocation") {
      // This is an archive key lookup, never a host path or URL expansion.
      const auto key = it.value().toString();
      if (!it.value().isString() || !assets.contains(key)) { valid = false; step.diagnostics.append("preset-asset-missing"); }
      else step.parameters.insert("image_location", "asset:" + key);
    } else if (direct.contains(it.key())) step.parameters.insert(direct.value(it.key()), it.value().toVariant());
    else if (enums.contains(it.key())) {
      QString choice; const auto choices = enums.value(it.key());
      auto value = it.value();
      if (it.key() == "Placement" && value.isDouble()) {
        const QList<int> flags{1, 2, 4, 16, 32, 64, 256, 512, 1024};
        const auto index = flags.indexOf(value.toInt(-1));
        if (index >= 0 && value.toDouble() == flags[index]) value = choices.first[index];
        else value = QJsonValue();
      }
      if (!enumValue(value, choices.first, choices.second, &choice)) { valid = false; step.diagnostics.append("effect-invalid-parameter:" + enumIds.value(it.key())); }
      else step.parameters.insert(enumIds.value(it.key()), choice);
    } else if (it.key() == "Offset" || it.key() == "Size") {
      int a = 0, b = 0; const bool offset = it.key() == "Offset";
      if (!pairValue(it.value(), offset ? "X" : "Width", offset ? "Y" : "Height", &a, &b)) { valid = false; step.diagnostics.append("effect-invalid-parameter:" + it.key()); }
      else { step.parameters.insert(offset ? "offset_x" : "size_width", a); step.parameters.insert(offset ? "offset_y" : "size_height", b); }
    } else step.diagnostics.append("preset-property-unsupported:" + it.key());
  }
  if (valid) validateStep(step, registry, assets);
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
    if (type == "ShareX.ImageEffectsLib.DrawImage") {
      mapDrawImage(step, registry, preset.assets); preset.steps.append(step); continue;
    }
    const Mapping *mapping = nullptr;
    for (const auto &m : mappings) if (type == QLatin1String(m.type)) { mapping = &m; break; }
    if (!mapping) step.diagnostics.append("preset-effect-unsupported");
    else {
      step.id = QLatin1String(mapping->id); step.seed = defaultEffectSeed(step.id);
      const auto v = step.legacy.value(QLatin1String(mapping->property));
      if (!*mapping->property) {
        if (*mapping->parameter) step.parameters.insert(QLatin1String(mapping->parameter), mapping->initial);
        validateStep(step, registry, preset.assets);
      } else if (!v.isUndefined() && !v.isDouble()) step.diagnostics.append("effect-invalid-parameter:" + QString::fromLatin1(mapping->parameter));
      else {
        step.parameters.insert(QLatin1String(mapping->parameter), v.toDouble(mapping->initial) * mapping->scale + mapping->offset);
        validateStep(step, registry, preset.assets);
      }
      for (auto it = step.legacy.begin(); it != step.legacy.end(); ++it)
        if (it.key() != "$type" && it.key() != "Enabled" && it.key() != QLatin1String(mapping->property)) {
          step.diagnostics.append("preset-property-unsupported:" + it.key());
          // Alpha addition cannot be approximated by the native multiplier.
          if ((step.id == "alpha" && it.key() == "Addition" && it.value() != QJsonValue(0)) ||
              (step.id == "pixelate" && it.key() == "BorderSize" && it.value() != QJsonValue(0))) step.supported = false;
        }
    }
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
