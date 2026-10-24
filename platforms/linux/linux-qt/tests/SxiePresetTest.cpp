// COMPAT-SXIE-001, EE-003..EE-008, EE-015..EE-016, ROOT-SECURITY-001.
#include "PresetDialog.h"
#include "EditorSource.h"
#include "image-editor/EditorSession.h"
#include "image-editor/effects/SxieArchive.h"
#include "image-editor/effects/CensusCatalog.h"
#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QTest>
#include <QtEndian>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>
#include <QPushButton>
#include <QSignalSpy>
#include <QFileDialog>
#include <zlib.h>
#include <cmath>
#include <algorithm>
using namespace xerahs::effects;
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
class Failing final : public Effect {
  EffectSpec m_spec;
public:
  Failing() { m_spec.id = "failing"; m_spec.displayName = "Failing"; }
  const EffectSpec &spec() const override { return m_spec; }
  EffectResult apply(const QImage &, const ParamValues &, const EffectContext &) const override { return {{}, "effect-failed"}; }
};
void put16(QByteArray &b, quint16 n) { b.append(char(n)); b.append(char(n >> 8)); }
void put32(QByteArray &b, quint32 n) { put16(b, n); put16(b, n >> 16); }
void set16(QByteArray &b, int p, quint16 n) { b[p] = char(n); b[p + 1] = char(n >> 8); }
void set32(QByteArray &b, int p, quint32 n) { set16(b, p, n); set16(b, p + 2, n >> 16); }
QByteArray zip(const QVector<QPair<QByteArray, QByteArray>> &entries, bool compressed = true) {
  QByteArray b, central;
  for (const auto &entry : entries) {
    const auto &name = entry.first; const auto &data = entry.second; QByteArray packed = data;
    if (compressed) {
      packed.resize(compressBound(data.size())); z_stream stream{};
      deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
      stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.data())); stream.avail_in = data.size();
      stream.next_out = reinterpret_cast<Bytef *>(packed.data()); stream.avail_out = packed.size();
      if (deflate(&stream, Z_FINISH) != Z_STREAM_END) qFatal("fixture compression failed");
      packed.resize(stream.total_out); deflateEnd(&stream);
    }
    const auto offset = b.size(); const auto crc = crc32(0, reinterpret_cast<const Bytef *>(data.constData()), data.size());
    put32(b, 0x04034b50); put16(b, 20); put16(b, 0); put16(b, compressed ? 8 : 0); put32(b, 0);
    put32(b, crc); put32(b, packed.size()); put32(b, data.size()); put16(b, name.size()); put16(b, 0); b += name; b += packed;
    put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, compressed ? 8 : 0); put32(central, 0);
    put32(central, crc); put32(central, packed.size()); put32(central, data.size()); put16(central, name.size()); put16(central, 0); put16(central, 0);
    put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset); central += name;
  }
  const auto offset = b.size(); b += central; put32(b, 0x06054b50); put16(b, 0); put16(b, 0);
  put16(b, entries.size()); put16(b, entries.size()); put32(b, central.size()); put32(b, offset); put16(b, 0); return b;
}
QByteArray fixture() { QFile f(QStringLiteral(SXIE_FIXTURE)); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
QByteArray configZip(QJsonObject root) { return zip({{"Config.json", QJsonDocument(root).toJson(QJsonDocument::Compact)}}); }
}
class SxiePresetTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
  void archiveValid() {
    for (bool compressed : {false, true}) {
      const auto a = readSxieArchive(zip({{"Config.json", "{}"}, {"assets/x", "abc"}, {"empty", ""}}, compressed));
      QVERIFY2(a.ok(), qPrintable(a.error)); QCOMPARE(a.entries.value("assets/x"), QByteArray("abc")); QCOMPARE(a.entries.size(), 3);
    }
  }
  void paths_data() {
    QTest::addColumn<QByteArray>("name");
    for (const auto &n : {"../x", "/x", "C:/x", "a\\b", "a/../b", "./x"}) QTest::newRow(n) << QByteArray(n);
  }
  void paths() { QFETCH(QByteArray, name); QCOMPARE(readSxieArchive(zip({{name, "x"}})).error, QString("sxie-zip-path")); }
  void archiveRefusals() {
    QCOMPARE(readSxieArchive(zip({{"x", QByteArray(100000, 'x')}})).error, QString("sxie-zip-resource-limit"));
    QCOMPARE(readSxieArchive(zip({{"x", "a"}, {"x", "b"}})).error, QString("sxie-zip-duplicate"));
    QVector<QPair<QByteArray, QByteArray>> many;
    for (int i = 0; i < 257; ++i) many.append({QByteArray::number(i), ""});
    QCOMPARE(readSxieArchive(zip(many)).error, QString("sxie-zip-entry-limit"));
    const auto original = zip({{"x", "abc"}}, false); auto b = original; const int cd = b.indexOf("PK\1\2");
    set16(b, 6, 1); set16(b, cd + 8, 1); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-encrypted"));
    b = original; b.remove(cd + 10, 4); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-directory"));
    b = original; set16(b, b.size() - 18, 1); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-multidisk"));
    b = original; set32(b, cd + 24, 0xffffffff); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip64"));
    b = original; set32(b, cd + 24, 32 * 1024 * 1024 + 1); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-resource-limit"));
    b = original; b[31] = 'z'; QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-crc"));
    b = original; set16(b, cd + 10, 99); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-compression"));
    b = original; set16(b, 6, 2); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-local"));
    b = original; set32(b, cd + 42, cd); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-local"));
    QVERIFY(!readSxieArchive(original.left(10)).ok());
    // Four bounded entries can still exceed the total expansion budget.
    QVector<QPair<QByteArray, QByteArray>> total;
    for (int i = 0; i < 5; ++i) total.append({QByteArray::number(i), QByteArray(27 * 1024 * 1024, 'x')});
    // Stored data isolates the total-size guard from the ratio guard.
    QCOMPARE(readSxieArchive(zip(total, false)).error, QString("sxie-zip-resource-limit"));
  }
  void descriptorsAndStructuralRefusals() {
    const auto original = zip({{"x", "abc"}}, false); auto b = original; const auto cd = b.indexOf("PK\1\2");
    QByteArray descriptor; put32(descriptor, 0x08074b50); put32(descriptor, crc32(0, reinterpret_cast<const Bytef *>("abc"), 3)); put32(descriptor, 3); put32(descriptor, 3);
    b.insert(cd, descriptor); set16(b, 6, 8); set16(b, cd + descriptor.size() + 8, 8);
    set32(b, 14, 0); set32(b, 18, 0); set32(b, 22, 0); set32(b, b.size() - 6, cd + descriptor.size());
    QVERIFY(readSxieArchive(b).ok()); auto bad = b; bad[cd + 8] = 'z'; QCOMPARE(readSxieArchive(bad).error, QString("sxie-zip-descriptor"));
    b = original; set16(b, cd + 6, 45); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip64"));
    b = original; set16(b, 4, 45); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip64"));
    b = original; set16(b, cd + 8, 16); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-flags"));
    b = original; set32(b, cd + 20, 2); set32(b, 18, 2); QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-size"));
    b = zip({{"x", "abc"}}); b[31] = 0; QVERIFY(!readSxieArchive(b).ok());
    const auto inner = zip({{"inner", "x"}}, false); b = zip({{"outer", inner}, {"inner", "x"}}, false);
    int directory = b.indexOf("PK\1\2", 35 + inner.size() + 36);
    QVERIFY(directory > 0); const auto next = directory + 46 + 5; set32(b, next + 42, 35);
    QCOMPARE(readSxieArchive(b).error, QString("sxie-zip-overlap"));
    QCOMPARE(readSxiePreset(zip({{"Config.json", ""}, {"config.json", "{}"}})).error, QString("sxie-config-duplicate"));
  }
  void mappingAndRoundTrip() {
    const auto p = readSxiePreset(fixture()); QVERIFY2(p.ok(), qPrintable(p.error)); QCOMPARE(p.steps.size(), 4);
    QCOMPARE(p.steps[0].id, QString("brightness")); QCOMPARE(p.steps[0].parameters.value("amount").toDouble(), 25.0);
    QCOMPARE(p.steps[1].parameters.value("amount").toDouble(), 75.0);
    QCOMPARE(p.steps[2].parameters.value("amount").toDouble(), 0.5);
    for (int i = 0; i < 3; ++i) QVERIFY(p.steps[i].supported);
    QVERIFY(!p.steps[3].supported); QVERIFY(p.steps[0].diagnostics.contains("preset-property-unsupported:Future"));
    const auto bytes = writeXsiePreset(p); const auto again = readXsiePreset(bytes); QVERIFY(again.ok());
    QCOMPARE(writeXsiePreset(again), bytes); QCOMPARE(again.legacy, p.legacy);
    for (int i = 0; i < 4; ++i) QCOMPARE(QJsonDocument(again.steps[i].legacy).toJson(QJsonDocument::Compact), QJsonDocument(p.steps[i].legacy).toJson(QJsonDocument::Compact));
  }
  void additionalExplicitMappingsAndNativeUnknowns() {
    const QList<QPair<QString, QString>> names{{"Inverse", "invert"}, {"BlackWhite", "black_and_white"}, {"Polaroid", "polaroid"},
        {"Emboss", "emboss"}, {"MeanRemoval", "mean_removal"}, {"Smooth", "smooth"}, {"EdgeDetect", "edge_detect"},
        {"Sharpen", "sharpen"}, {"Hue", "hue"}, {"Pixelate", "pixelate"}};
    for (const auto &n : names) {
      const auto p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib." + n.first}}}}}));
      QVERIFY(p.ok()); QCOMPARE(p.steps[0].id, n.second); QVERIFY2(p.steps[0].supported, qPrintable(n.first));
      QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(p))), writeXsiePreset(p));
    }
    auto root = QJsonDocument::fromJson(writeXsiePreset(readSxiePreset(fixture()))).object();
    root["FutureRoot"] = QJsonObject{{"data", QJsonArray{true, 12, "keep"}}};
    auto steps = root["steps"].toArray(); auto step = steps[0].toObject(); step["FutureStep"] = QJsonArray{1, "keep"}; steps[0] = step; root["steps"] = steps;
    const auto p = readXsiePreset(QJsonDocument(root).toJson()); QVERIFY(p.ok());
    QCOMPARE(QJsonDocument::fromJson(writeXsiePreset(p)).object(), root);
    step["parameters"] = QJsonObject{{"amount", 25}, {"unknown", QJsonObject{{"keep", 3}}}}; steps[0] = step; root["steps"] = steps;
    const auto unknown = readXsiePreset(QJsonDocument(root).toJson()); QVERIFY(unknown.ok()); QVERIFY(!unknown.steps[0].supported);
    QCOMPARE(QJsonDocument::fromJson(writeXsiePreset(unknown)).object()["steps"].toArray()[0].toObject()["parameters"], step["parameters"]);
  }
  void legacyCensus_data() {
    QTest::addColumn<QJsonObject>("entry");
    QFile file(QStringLiteral(SXIE_WATERMARK_FIXTURE)); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(file.readAll()); QVERIFY(fixture.isObject());
    const auto cases = fixture.object().value("cases").toArray(); QCOMPARE(cases.size(), 38);
    for (const auto &value : cases) {
      const auto entry = value.toObject(); QTest::newRow(qPrintable(entry.value("name").toString())) << entry;
    }
  }
  void legacyCensus() {
    QFETCH(QJsonObject, entry);
    auto legacy = entry.value("legacy").toObject();
    legacy["Future"] = QJsonObject{{"nested", QJsonArray{1, true, "keep"}}};
    QImage stamp(2, 2, QImage::Format_ARGB32); stamp.fill(Qt::red);
    QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(stamp.save(&buffer, "PNG"));
    const auto root = QJsonObject{{"Name", "Watermarks"}, {"Effects", QJsonArray{legacy}}};
    const auto p = readSxiePreset(zip({{"Config.json", QJsonDocument(root).toJson()}, {"Assets/stamp.png", png}}));
    QVERIFY2(p.ok(), qPrintable(p.error)); QCOMPARE(p.steps.size(), 1);
    const auto &step = p.steps[0]; QCOMPARE(step.id, entry.value("id").toString());
    if (!step.id.isEmpty()) {
      const auto &catalog = censusCatalog();
      const auto spec = std::find_if(catalog.begin(), catalog.end(), [&](const CensusEffect &effect) { return effect.id == step.id; });
      QVERIFY(spec != catalog.end());
      for (auto it = step.parameters.begin(); it != step.parameters.end(); ++it)
        QVERIFY2(std::any_of(spec->parameters.begin(), spec->parameters.end(), [&](const CensusParameter &param) { return param.id == it.key(); }), qPrintable(it.key()));
    }
    const auto expected = entry.value("parameters").toObject();
    for (auto it = expected.begin(); it != expected.end(); ++it) {
      QVERIFY2(step.parameters.contains(it.key()), qPrintable(it.key()));
      const auto actual = step.parameters.value(it.key());
      if (it.value().isString() && it.value().toString().startsWith('#'))
        QCOMPARE(actual.value<QColor>().rgba(), QColor(it.value().toString()).rgba());
      else if (it.value().isDouble()) QVERIFY2(std::abs(actual.toDouble() - it.value().toDouble()) < 1e-9, qPrintable(it.key()));
      else QCOMPARE(QJsonValue::fromVariant(actual), it.value());
    }
    QCOMPARE(step.legacy, legacy);
    if (!step.id.isEmpty()) QVERIFY(step.diagnostics.contains("preset-property-unsupported:Future"));
    // Supported exactly when the census effect is registered and its assets are present;
    // fixture renderer:true entries must stay supported as more effects land.
    const bool registered = !step.id.isEmpty() && xerahs::effects::builtinEffects().find(step.id) != nullptr;
    const bool invalid = std::any_of(step.diagnostics.cbegin(), step.diagnostics.cend(),
                                     [](const QString &d) { return d.startsWith("effect-invalid-parameter"); });
    QCOMPARE(step.supported, registered && !invalid && !step.diagnostics.contains("preset-asset-effect-unsupported"));
    if (entry.value("renderer").toBool()) QVERIFY(step.supported);
    const auto bytes = writeXsiePreset(p); const auto again = readXsiePreset(bytes);
    QVERIFY(again.ok()); QCOMPARE(writeXsiePreset(again), bytes); QCOMPARE(again.steps[0].legacy, legacy);
    if (!step.supported) {
      QVERIFY(step.diagnostics.contains("preset-effect-unsupported") || step.diagnostics.contains("preset-asset-effect-unsupported") || invalid);
      return;
    }
    // Size and pixel expectations are hand-written only for fixture-declared renderers.
    if (!entry.value("renderer").toBool()) return;
    const auto sourceSize = entry.value("source").toArray();
    QImage image(sourceSize[0].toInt(), sourceSize[1].toInt(), QImage::Format_ARGB32); image.fill(QColor(entry.value("fill").toString()));
    const auto check = entry.value("check").toString();
    if (check == "auto-crop") for (int y = 3; y < 6; ++y) for (int x = 4; x < 8; ++x) image.setPixelColor(x, y, Qt::red);
    if (check == "flip") image.setPixelColor(0, 0, Qt::red);
    const auto loaded = prepareCapturedSource(image); QVERIFY(loaded.document);
    const auto edit = preparePresetEdit(*loaded.document, image, again, builtinEffects());
    QVERIFY2(!edit.result.error, qPrintable(edit.result.error.value_or(QString())));
    const auto size = entry.value("size").toArray(); QCOMPARE(edit.source.size(), QSize(size[0].toInt(), size[1].toInt()));
    for (const auto &value : entry.value("pixels").toArray()) {
      const auto point = value.toArray(); const auto actual = edit.source.pixelColor(point[0].toInt(), point[1].toInt());
      const auto color = QColor(point[2].toString()); const int tolerance = point[3].toInt();
      for (const int difference : {actual.red() - color.red(), actual.green() - color.green(), actual.blue() - color.blue(), actual.alpha() - color.alpha()})
        QVERIFY2(std::abs(difference) <= tolerance, qPrintable(actual.name(QColor::HexArgb) + " != " + color.name(QColor::HexArgb)));
    }
    if (check == "flip") QCOMPARE(edit.source.pixelColor(image.width() - 1, image.height() - 1), QColor(Qt::red));
    if (check == "watermark" || check == "text") {
      int red = 0, green = 0, blue = 0, white = 0;
      for (int y = 0; y < edit.source.height(); ++y) for (int x = 0; x < edit.source.width(); ++x) {
        const auto c = edit.source.pixelColor(x, y); if (c.alpha() < 128) continue;
        QVERIFY(x >= 7 && x < 80 && y >= 7 && y < 70);
        red += c.red() > 200 && c.green() < 50 && c.blue() < 50;
        green += c.green() > 200 && c.red() < 50 && c.blue() < 50;
        blue += c.blue() > 200 && c.red() < 50 && c.green() < 50;
        white += c.red() > 200 && c.green() > 200 && c.blue() > 200;
      }
      QVERIFY(red > 5); QVERIFY(green > 2); QVERIFY(blue > 2);
      if (check == "watermark") QVERIFY(white > 5);
    }
    QCOMPARE(step.parameters, again.steps[0].parameters);
  }
  void colorFormatsAndRefusals() {
    const QList<QPair<QJsonValue, QColor>> colors{
      {QJsonValue(-2146360781.0), QColor::fromRgba(0x80112233)}, {QJsonValue(2148606515.0), QColor::fromRgba(0x80112233)},
      {"#80112233", QColor::fromRgba(0x80112233)}, {"128, 17, 34, 51", QColor::fromRgba(0x80112233)},
      {"17, 34, 51", QColor(17, 34, 51)}, {"Transparent", QColor(255, 255, 255, 0)}, {"Red", QColor(Qt::red)}, {"LightGray", QColor(211, 211, 211)}};
    for (const auto &item : colors) {
      const auto p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib.DrawBackground"}, {"Color", item.first}}}}}));
      QVERIFY(p.ok()); QVERIFY(p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value("color").value<QColor>(), item.second);
      QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(p))), writeXsiePreset(p));
    }
    for (const auto &value : QJsonArray{4294967296.0, -2147483649.0, 1.5, true, QJsonObject{{"R", 1}}, "#1234", "300, 1, 1", "no-such-color"}) {
      const auto p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib.DrawBackground"}, {"Color", value}}}}}));
      QVERIFY(p.ok()); QVERIFY(!p.steps[0].supported); QVERIFY(p.steps[0].diagnostics.contains("effect-invalid-parameter:color"));
      QCOMPARE(p.steps[0].legacy.value("Color"), value);
    }
  }
  void unsupportedPropertiesAndStrictConversions() {
    const QList<QJsonObject> cases{
      {{"$type", "ShareX.ImageEffectsLib.DrawBackground"}, {"UseGradient", true}},
      {{"$type", "ShareX.ImageEffectsLib.DrawBorder"}, {"DashStyle", "DashDotDot"}},
      {{"$type", "ShareX.ImageEffectsLib.Canvas"}, {"MarginMode", "PercentageOfCanvas"}},
      {{"$type", "ShareX.ImageEffectsLib.Canvas"}, {"Margin", "1, 2, 3"}},
      {{"$type", "ShareX.ImageEffectsLib.Canvas"}, {"Margin", QJsonObject{{"Left", 1.5}, {"Top", 0}, {"Right", 0}, {"Bottom", 0}}}},
      {{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"TextFont", "Arial, 12pt, style=Underline"}},
      {{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"TextFont", "Arial, 1000pt"}},
      {{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"Offset", QJsonObject{{"X", 1.5}, {"Y", 2}}}},
      {{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"Placement", 0}},
      {{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"DrawBorder", "true"}},
      {{"$type", "ShareX.ImageEffectsLib.Resize"}, {"Mode", "ResizeIfBigger"}},
      {{"$type", "ShareX.ImageEffectsLib.Rotate"}, {"Upsize", false}, {"Clip", false}},
      {{"$type", "ShareX.ImageEffectsLib.AutoCrop"}, {"Padding", 2}},
      {{"$type", "ShareX.ImageEffectsLib.AutoCrop"}, {"Sides", "Top"}},
      {{"$type", "ShareX.ImageEffectsLib.ReplaceColor"}, {"AutoSourceColor", true}},
      {{"$type", "ShareX.ImageEffectsLib.ReplaceColor"}, {"Threshold", 20}},
      {{"$type", "ShareX.ImageEffectsLib.Crop"}, {"Margin", "-1, 0, 0, 0"}},
      {{"$type", "ShareX.ImageEffectsLib.RoundedCorners"}, {"CornerRadius", -1}}};
    for (const auto &legacy : cases) {
      const auto p = readSxiePreset(configZip({{"Effects", QJsonArray{legacy}}}));
      QVERIFY2(p.ok(), qPrintable(p.error)); QVERIFY2(!p.steps[0].supported, qPrintable(QJsonDocument(legacy).toJson()));
      QVERIFY(!p.steps[0].diagnostics.isEmpty()); QCOMPARE(p.steps[0].legacy, legacy);
      QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(p))), writeXsiePreset(p));
    }
    auto p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib.DrawText"}}}}}));
    QVERIFY(p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value("font_size").toDouble(), 15.0);
    QCOMPARE(p.steps[0].parameters.value("draw_text_shadow").toBool(), true);
    p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib.Resize"}, {"Width", 8}, {"Height", 6}}}}}));
    QVERIFY(p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value("maintain_aspect_ratio").toBool(), false);
    p = readSxiePreset(configZip({{"Effects", QJsonArray{QJsonObject{{"$type", "ShareX.ImageEffectsLib.Canvas"}, {"Margin", "3"}}}}}));
    QVERIFY(p.steps[0].supported);
    for (const auto &key : {"left", "top", "right", "bottom"}) QCOMPARE(p.steps[0].parameters.value(key).toInt(), 3);
  }
  void legacyEnumValues() {
    const auto import = [](QJsonObject effect) {
      return readSxiePreset(configZip({{"Effects", QJsonArray{effect}}}));
    };
    const QList<int> flags{1, 2, 4, 16, 32, 64, 256, 512, 1024};
    const QStringList placements{"TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight"};
    const QStringList native{"top_left", "top_center", "top_right", "middle_left", "middle_center", "middle_right", "bottom_left", "bottom_center", "bottom_right"};
    for (int i = 0; i < flags.size(); ++i) for (const QJsonValue &value : {QJsonValue(flags[i]), QJsonValue(placements[i])}) {
      const auto p = import({{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"Placement", value}});
      QVERIFY(p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value("placement").toString(), native[i]);
    }
    for (int i = 0; i < 4; ++i) {
      const QStringList names{"Solid", "Dash", "Dot", "DashDot"}, values{"solid", "dash", "dot", "dash_dot"};
      for (const QJsonValue &value : {QJsonValue(i), QJsonValue(names[i])}) {
        const auto p = import({{"$type", "ShareX.ImageEffectsLib.DrawBorder"}, {"DashStyle", value}});
        QVERIFY(p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value("dash_style").toString(), values[i]);
      }
    }
    // File effect parameters are still converted when a missing embedded asset prevents execution.
    const QList<QPair<QString, QStringList>> enums{
      {"SizeMode", {"dont_resize", "absolute_size", "percentage_of_watermark", "percentage_of_canvas"}},
      {"RotateFlip", {"none", "rotate_90", "rotate_180", "rotate_270", "flip_x", "rotate_90_flip_x", "flip_y", "rotate_90_flip_y"}},
      {"InterpolationMode", {"high_quality_bicubic", "bicubic", "high_quality_bilinear", "bilinear", "nearest_neighbor"}},
      {"CompositingMode", {"source_over", "source_copy"}}};
    const QStringList ids{"size_mode", "rotate_flip", "interpolation_mode", "compositing_mode"};
    for (int row = 0; row < enums.size(); ++row) {
      for (int i = 0; i < enums[row].second.size(); ++i) {
        const auto p = import({{"$type", "ShareX.ImageEffectsLib.DrawImage"}, {enums[row].first, i}});
        QVERIFY(!p.steps[0].supported); QCOMPARE(p.steps[0].parameters.value(ids[row]).toString(), enums[row].second[i]);
      }
      for (const QJsonValue &invalid : {QJsonValue(-1), QJsonValue(1.5), QJsonValue(enums[row].second.size()), QJsonValue("BadValue")}) {
        const auto p = import({{"$type", "ShareX.ImageEffectsLib.DrawImage"}, {enums[row].first, invalid}});
        QVERIFY(!p.steps[0].supported); QVERIFY(p.steps[0].diagnostics.contains("effect-invalid-parameter:" + ids[row]));
      }
    }
    for (const auto &pair : QList<QPair<QString, double>>{{"12pt", 16}, {"12px", 12}, {"0.25in", 24}, {"75doc", 24}, {"6.35mm", 24}}) {
      const auto p = import({{"$type", "ShareX.ImageEffectsLib.DrawText"}, {"TextFont", "Arial, " + pair.first}});
      QVERIFY(p.steps[0].supported); QVERIFY(std::abs(p.steps[0].parameters.value("font_size").toDouble() - pair.second) < 1e-9);
    }
    // The legacy Shadow has a Darkness property with no native equivalent, so a non-neutral value
    // refuses the step. The legacy Glow has no Darkness property: the unknown property is only
    // reported and the step follows the registry.
    const auto shadow = import({{"$type", "ShareX.ImageEffectsLib.Shadow"}, {"Darkness", 4}, {"Unknown", QJsonArray{1, "keep"}}});
    const auto glow = import({{"$type", "ShareX.ImageEffectsLib.Glow"}, {"Darkness", 4}, {"Unknown", QJsonArray{1, "keep"}}});
    QVERIFY(!shadow.steps[0].supported);
    QCOMPARE(glow.steps[0].supported, xerahs::effects::builtinEffects().find(glow.steps[0].id) != nullptr);
    for (const auto &p : {shadow, glow}) {
      QVERIFY(p.steps[0].diagnostics.contains("preset-property-unsupported:Darkness"));
      QVERIFY(p.steps[0].diagnostics.contains("preset-property-unsupported:Unknown"));
      QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(p))), writeXsiePreset(p));
      QCOMPARE(p.steps[0].legacy.value("Unknown"), QJsonValue(QJsonArray{1, "keep"}));
    }
  }
  void exportDialogWritesPrivateNativeJson() {
    QTemporaryDir temp; QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::white);
    const auto loaded = prepareCapturedSource(image); QVERIFY(loaded.document);
    PresetDialog d(*loaded.document, image, {}, std::make_shared<EffectRegistry>(builtinEffects()));
    const auto p = readSxiePreset(fixture()); d.setPreset(p); d.show();
    auto *button = d.findChild<QPushButton *>("presetExport"); QVERIFY(button); button->click();
    auto *picker = d.findChild<QFileDialog *>(); QVERIFY(picker);
    const auto path = temp.filePath("export.xsie");
    QVERIFY(QMetaObject::invokeMethod(picker, "fileSelected", Qt::DirectConnection, Q_ARG(QString, path))); picker->reject();
    QTRY_VERIFY(d.findChild<QLabel *>("presetStatus")->text().contains("Preset exported"));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll();
    QCOMPARE(writeXsiePreset(readXsiePreset(bytes)), writeXsiePreset(p));
    QVERIFY(!(file.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
  }
  void invalidValuesAndAssets() {
    auto root = readSxiePreset(fixture()).legacy; auto steps = root.value("Effects").toArray(); auto s = steps[0].toObject(); s["Value"] = 2.0; steps[0] = s; root["Effects"] = steps;
    const auto p = readSxiePreset(configZip(root)); QVERIFY(p.ok()); QVERIFY(!p.steps[0].supported);
    QVERIFY(p.steps[0].diagnostics.contains("effect-invalid-parameter:amount")); QCOMPARE(p.steps[0].parameters.value("amount").toDouble(), 200.0);
    QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(p))), writeXsiePreset(p));
    QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::red); QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(image.save(&buffer, "PNG"));
    auto a = readSxieArchive(fixture()); const auto data = zip({{"Config.json", a.entries["Config.json"]}, {"Assets/x.png", png}});
    const auto withAsset = readSxiePreset(data); QVERIFY(withAsset.ok()); QCOMPARE(withAsset.assets["Assets/x.png"], png);
    QCOMPARE(writeXsiePreset(readXsiePreset(writeXsiePreset(withAsset))), writeXsiePreset(withAsset));
    QCOMPARE(readSxiePreset(zip({{"Config.json", "{}"}, {"Assets/x.png", "bad"}})).error, QString("sxie-asset-invalid"));
    auto huge = png; qToBigEndian<quint32>(8193, huge.data() + 16);
    qToBigEndian<quint32>(crc32(0, reinterpret_cast<const Bytef *>(huge.constData() + 12), 17), huge.data() + 29);
    QCOMPARE(readSxiePreset(zip({{"Config.json", a.entries["Config.json"]}, {"Assets/huge.png", huge}})).error, QString("sxie-asset-invalid"));
    auto native = QJsonDocument::fromJson(writeXsiePreset(withAsset)).object(); native["assets"] = QJsonObject{{"x", "!!!"}};
    QVERIFY(!readXsiePreset(QJsonDocument(native).toJson()).ok());
  }
  void embeddedDrawImageAndPathRefusal() {
    QImage stamp(2, 2, QImage::Format_ARGB32); stamp.fill(Qt::red);
    QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); QVERIFY(stamp.save(&buffer, "PNG"));
    const QJsonObject effect{{"$type", "ShareX.ImageEffectsLib.DrawImage, ShareX.ImageEffectsLib"},
        {"ImageLocation", "Assets/stamp.png"}, {"Offset", "1, 2"}, {"SizeMode", 0}, {"Placement", 1}, {"RotateFlip", "None"}, {"Opacity", 100}};
    QJsonObject root{{"Name", "Embedded"}, {"Effects", QJsonArray{effect}}};
    auto preset = readSxiePreset(zip({{"Config.json", QJsonDocument(root).toJson()}, {"Assets/stamp.png", png}}));
    QVERIFY2(preset.ok(), qPrintable(preset.error)); QVERIFY2(preset.steps[0].supported, qPrintable(preset.steps[0].diagnostics.join(",")));
    const auto bytes = writeXsiePreset(preset); preset = readXsiePreset(bytes); QCOMPARE(writeXsiePreset(preset), bytes);
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::blue); const auto loaded = prepareCapturedSource(image); QVERIFY(loaded.document);
    const auto edit = preparePresetEdit(*loaded.document, image, preset, builtinEffects());
    QVERIFY2(!edit.result.error, edit.result.error ? qPrintable(*edit.result.error) : "");
    QCOMPARE(edit.source.pixelColor(1, 2), QColor(Qt::red)); QCOMPARE(edit.source.pixelColor(0, 0), QColor(Qt::blue));
    const auto operation = edit.result.document->extra.value("effectOperations").toArray().last().toObject();
    QCOMPARE(operation.value("parameters").toObject().value("image_location").toString(), QString("asset:Assets/stamp.png"));
    QCOMPARE(edit.result.document->extra.value("effectPresetAssets").toObject().value("Assets/stamp.png").toString().toLatin1(), png.toBase64());
    QVERIFY(!QJsonDocument(serializeDocument(*edit.result.document)).toJson().contains("/tmp/"));
    // An imported path can only address an embedded asset, never a host file.
    for (const auto &path : {"/etc/passwd", "../stamp.png", "https://example.com/x", "C:\\x"}) {
      auto e = effect; e["ImageLocation"] = path; root["Effects"] = QJsonArray{e};
      const auto bad = readSxiePreset(configZip(root)); QVERIFY(bad.ok()); QVERIFY(!bad.steps[0].supported);
    }
    auto native = QJsonDocument::fromJson(bytes).object(); auto steps = native["steps"].toArray(); auto s = steps[0].toObject();
    auto params = s["parameters"].toObject(); params["image_location"] = "/etc/passwd"; s["parameters"] = params; steps[0] = s; native["steps"] = steps;
    const auto badNative = readXsiePreset(QJsonDocument(native).toJson()); QVERIFY(badNative.ok()); QVERIFY(!badNative.steps[0].supported);
  }
  void presetDialogLoadApplyCancelAndTeardown() {
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::green); const auto loaded = prepareCapturedSource(image); QVERIFY(loaded.document);
    EditorSession session(*loaded.document); const auto state = session.stateId();
    auto registry = std::make_shared<EffectRegistry>(builtinEffects()); int commits = 0;
    PresetDialog dialog(*loaded.document, image, [&](PreparedCanvasEdit edit) { ++commits; return session.commitCanvas(*edit.result.document, state); }, registry);
    dialog.show(); dialog.loadFile(QStringLiteral(SXIE_FIXTURE));
    auto *list = dialog.findChild<QListWidget *>("presetSteps"); QVERIFY(list); QTRY_COMPARE(list->count(), 4);
    QVERIFY(list->item(0)->text().contains("Supported:")); QVERIFY(list->item(3)->text().contains("Unsupported:"));
    dialog.accept(); QTRY_COMPARE(dialog.result(), int(QDialog::Accepted)); QCOMPARE(commits, 1); QCOMPARE(session.undoCount(), 1);
    PresetDialog cancelled(*loaded.document, image, [&](PreparedCanvasEdit) { ++commits; return true; }, registry);
    cancelled.loadFile(QStringLiteral(SXIE_FIXTURE)); cancelled.reject(); QTest::qWait(50); QCOMPARE(commits, 1);
    auto *abandoned = new PresetDialog(*loaded.document, image, {}, registry); abandoned->loadFile(QStringLiteral(SXIE_FIXTURE)); delete abandoned; QTest::qWait(50);
    PresetDialog missing(*loaded.document, image, {}, registry); missing.loadFile("/missing-preset.sxie");
    QTRY_VERIFY(missing.findChild<QLabel *>()->text().contains("preset-read-failed"));
  }
  void transactionalHistory() {
    QImage image(8, 6, QImage::Format_ARGB32); image.fill(QColor(70, 90, 130)); const auto loaded = prepareCapturedSource(image); QVERIFY(loaded.document);
    EditorSession session(*loaded.document); const auto before = serializeDocument(session.document());
    auto p = readSxiePreset(fixture()); const auto edit = preparePresetEdit(session.document(), image, p, builtinEffects());
    QVERIFY(!edit.result.error); QVERIFY(session.commitCanvas(*edit.result.document, session.stateId())); QCOMPARE(session.undoCount(), 1);
    const auto after = serializeDocument(session.document()); QVERIFY(before != after);
    QCOMPARE(session.document().extra.value("effectPresetOperations").toArray().last().toObject().value("legacy").toObject(), p.legacy);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), after);
    p.steps[2].parameters["amount"] = 500.0;
    const auto failed = preparePresetEdit(session.document(), edit.source, p, builtinEffects()); QVERIFY(failed.result.error); QVERIFY(!failed.result.document); QVERIFY(failed.source.isNull());
    QCOMPARE(serializeDocument(session.document()), after); QCOMPARE(session.undoCount(), 1);
    auto registry = builtinEffects(); QVERIFY(registry.add(std::make_unique<Failing>()));
    auto executionFailure = readSxiePreset(fixture()); PresetStep fail; fail.id = "failing"; fail.supported = true; executionFailure.steps.append(fail);
    const auto failedExecution = preparePresetEdit(session.document(), edit.source, executionFailure, registry);
    QCOMPARE(failedExecution.result.error.value_or(QString()), QString("effect-failed")); QVERIFY(!failedExecution.result.document); QVERIFY(failedExecution.source.isNull());
    QCOMPARE(serializeDocument(session.document()), after); QCOMPARE(session.undoCount(), 1);
    const auto cancelled = preparePresetEdit(session.document(), edit.source, readSxiePreset(fixture()), builtinEffects(), {[] { return true; }});
    QVERIFY(cancelled.result.error); QCOMPARE(serializeDocument(session.document()), after);
  }
};
QTEST_MAIN(SxiePresetTest)
#include "SxiePresetTest.moc"
