// EDITOR-EFFECTS-001: EE-002, EE-003, EE-008..EE-010, EE-018.
#include "image-editor/effects/Effect.h"
#include <QColorSpace>
#include <QJsonDocument>
#include <QTest>
#include <limits>
#include <stdexcept>

using namespace xerahs::effects;
namespace {
EffectSpec parameterSpec() {
  EffectSpec spec;
  spec.parameters = {
    {"float", "Float", ParamKind::Float, 0.5, -1, 1, {}},
    {"int", "Integer", ParamKind::Int, 2, -10, 10, {}},
    {"bool", "Boolean", ParamKind::Bool, false, 0, 0, {}},
    {"color", "Colour", ParamKind::Color, QColor::fromRgbF(0.2, 0.4, 0.6, 0.5), 0, 0, {}},
    {"enum", "Choice", ParamKind::Enum, QStringLiteral("one"), 0, 0, {"one", "two"}},
    {"text", "Text", ParamKind::Text, QStringLiteral("hello"), 0, 0, {}},
    {"path", "File", ParamKind::FilePath, QStringLiteral("/tmp/example.png"), 0, 0, {}}};
  return spec;
}
class FakeEffect final : public Effect {
public:
  EffectSpec metadata;
  std::function<EffectResult(const QImage &, const ParamValues &, const EffectContext &)> work =
    [](const QImage &image, const ParamValues &, const EffectContext &) { return EffectResult{image, {}}; };
  const EffectSpec &spec() const override { return metadata; }
  EffectResult apply(const QImage &image, const ParamValues &params, const EffectContext &context) const override {
    return work(image, params, context);
  }
};
std::unique_ptr<Effect> fake(const QString &id, const QString &name, Category category) {
  auto effect = std::make_unique<FakeEffect>();
  effect->metadata.id = id;
  effect->metadata.displayName = name;
  effect->metadata.category = category;
  return effect;
}
QImage sourceImage() {
  QImage image(3, 2, QImage::Format_ARGB32);
  image.fill(qRgba(64, 100, 180, 128));
  image.setColorSpace(QColorSpace::DisplayP3);
  return image;
}
}
class EffectFrameworkTest : public QObject {
  Q_OBJECT
private slots:
  void defaultsAndNumericNormalization() {
    const auto spec = parameterSpec();
    const auto defaults = resolveParameters(spec, {});
    QVERIFY(defaults.ok());
    QCOMPARE(defaults.values.size(), 7);
    for (const auto &p : spec.parameters) QCOMPARE(defaults.values.value(p.id), p.defaultValue);
    const auto bounds = resolveParameters(spec, {{"float", -1.0}, {"int", 10.0}});
    QVERIFY(bounds.ok());
    QCOMPARE(bounds.values["float"].toDouble(), -1.0);
    QCOMPARE(bounds.values["int"].typeId(), int(QMetaType::Int));
    QVERIFY(resolveParameters(spec, {{"float", 1}, {"int", -10}}).ok());
  }
  void invalidValues_data() {
    QTest::addColumn<QString>("id");
    QTest::addColumn<QVariant>("value");
    QTest::newRow("float-string") << QString("float") << QVariant("0.5");
    QTest::newRow("float-bool") << QString("float") << QVariant(true);
    QTest::newRow("float-high") << QString("float") << QVariant(1.001);
    QTest::newRow("float-low") << QString("float") << QVariant(-1.001);
    QTest::newRow("float-nan") << QString("float") << QVariant(std::numeric_limits<double>::quiet_NaN());
    QTest::newRow("float-infinite") << QString("float") << QVariant(std::numeric_limits<double>::infinity());
    QTest::newRow("int-fraction") << QString("int") << QVariant(1.5);
    QTest::newRow("int-high") << QString("int") << QVariant(11);
    QTest::newRow("int-string") << QString("int") << QVariant("2");
    QTest::newRow("bool-number") << QString("bool") << QVariant(1);
    QTest::newRow("color-string") << QString("color") << QVariant("red");
    QTest::newRow("color-invalid") << QString("color") << QVariant(QColor());
    QTest::newRow("enum-unknown") << QString("enum") << QVariant("three");
    QTest::newRow("enum-number") << QString("enum") << QVariant(1);
    QTest::newRow("text-number") << QString("text") << QVariant(1);
    QTest::newRow("path-bool") << QString("path") << QVariant(false);
    QTest::newRow("null") << QString("float") << QVariant();
  }
  void invalidValues() {
    QFETCH(QString, id);
    QFETCH(QVariant, value);
    const auto result = resolveParameters(parameterSpec(), {{id, value}});
    QVERIFY(!result.ok());
    QCOMPARE(result.error, QStringLiteral("effect-invalid-parameter"));
    QCOMPARE(result.parameter, id);
    QVERIFY(result.values.isEmpty());
  }
  void unknownAndInvalidDefault() {
    auto spec = parameterSpec();
    auto result = resolveParameters(spec, {{"future", 1}});
    QCOMPARE(result.error, QStringLiteral("effect-unknown-parameter"));
    QCOMPARE(result.parameter, QStringLiteral("future"));
    spec.parameters[0].defaultValue = 2.0;
    result = resolveParameters(spec, {});
    QCOMPARE(result.error, QStringLiteral("effect-invalid-parameter"));
    QCOMPARE(result.parameter, QStringLiteral("float"));
  }
  void jsonRoundTripAndInvalidJson() {
    const auto spec = parameterSpec();
    const auto resolved = resolveParameters(spec, {{"enum", QString("two")}, {"text", QString::fromUtf8("é\n\"")},
                                                  {"bool", true}, {"int", -3}});
    const auto json = serializeParameters(spec, resolved.values);
    const auto decoded = deserializeParameters(spec, QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
    QVERIFY(decoded.ok());
    QCOMPARE(decoded.values, resolved.values);
    QCOMPARE(serializeParameters(spec, decoded.values), json);
    QCOMPARE(deserializeParameters(spec, {}).values, resolveParameters(spec, {}).values);
    auto bad = json;
    bad["int"] = 2.5;
    QCOMPARE(deserializeParameters(spec, bad).parameter, QStringLiteral("int"));
    bad = json;
    bad["color"] = QJsonObject{{"r", 2}, {"g", 0}, {"b", 0}, {"a", 1}};
    QCOMPARE(deserializeParameters(spec, bad).parameter, QStringLiteral("color"));
    bad = json;
    bad["color"] = QJsonObject{{"r", 0}, {"g", 0}, {"b", 0}};
    QVERIFY(!deserializeParameters(spec, bad).ok());
    bad = json;
    bad["bool"] = QJsonValue::Null;
    QCOMPARE(deserializeParameters(spec, bad).parameter, QStringLiteral("bool"));
    const auto unknown = deserializeParameters(spec, {{"unknown", true}});
    QCOMPARE(unknown.error, QStringLiteral("effect-unknown-parameter"));
    QCOMPARE(unknown.parameter, QStringLiteral("unknown"));
  }
  void registryRejectsDuplicatesAndOrders() {
    EffectRegistry registry;
    QVERIFY(!registry.add(nullptr));
    QVERIFY(!registry.add(fake("", "Empty", Category::Adjustments)));
    QVERIFY(registry.add(fake("filter", "A", Category::Filters)));
    QVERIFY(registry.add(fake("z", "Z", Category::Adjustments)));
    QVERIFY(registry.add(fake("a", "A", Category::Adjustments)));
    QVERIFY(registry.add(fake("b", "A", Category::Adjustments)));
    const auto *original = registry.find("a");
    QVERIFY(!registry.add(fake("a", "Replacement", Category::Drawings)));
    QCOMPARE(registry.find("a"), original);
    QVERIFY(!registry.find("absent"));
    QCOMPARE(registry.size(), 4);
    QStringList ids;
    for (const auto *spec : registry.specs()) ids << spec->id;
    QCOMPARE(ids, (QStringList{"a", "b", "z", "filter"}));
    QCOMPARE(&builtinEffects(), &builtinEffects());
  }
  void runnerNormalizesAndResolves() {
    FakeEffect effect;
    effect.metadata = parameterSpec();
    bool called = false;
    effect.work = [&](const QImage &image, const ParamValues &params, const EffectContext &) {
      called = true;
      if (image.format() != QImage::Format_ARGB32 || params != resolveParameters(effect.spec(), {}).values)
        return EffectResult{{}, "effect-failed"};
      return EffectResult{image, {}};
    };
    const auto source = sourceImage().convertToFormat(QImage::Format_RGBA8888);
    const auto result = runEffect(effect, source, {}, {});
    QVERIFY(result.ok());
    QVERIFY(called);
    QCOMPARE(result.image.colorSpace(), source.colorSpace());
    QCOMPARE(result.image, source.convertToFormat(QImage::Format_ARGB32));
    called = false;
    QCOMPARE(runEffect(effect, source, {{"float", 10}}, {}).error, QStringLiteral("effect-invalid-input"));
    QVERIFY(!called);
    QCOMPARE(runEffect(effect, {}, {}, {}).error, QStringLiteral("effect-invalid-input"));
  }
  void runnerRejectsViolationsAndExceptions() {
    FakeEffect effect;
    const auto source = sourceImage();
    effect.work = [](const QImage &image, const ParamValues &, const EffectContext &) {
      return EffectResult{image.convertToFormat(QImage::Format_RGB32), {}};
    };
    QCOMPARE(runEffect(effect, source, {}, {}).error, QStringLiteral("effect-failed"));
    effect.work = [](const QImage &image, const ParamValues &, const EffectContext &) {
      return EffectResult{image.scaled(1, 1), {}};
    };
    QCOMPARE(runEffect(effect, source, {}, {}).error, QStringLiteral("effect-failed"));
    effect.metadata.changesSize = true;
    QVERIFY(runEffect(effect, source, {}, {}).ok());
    effect.work = [](QImage image, const ParamValues &, const EffectContext &) {
      image.setColorSpace(QColorSpace::SRgb);
      return EffectResult{image, {}};
    };
    QCOMPARE(runEffect(effect, source, {}, {}).error, QStringLiteral("effect-failed"));
    effect.work = [](const QImage &, const ParamValues &, const EffectContext &) { return EffectResult{}; };
    QCOMPARE(runEffect(effect, source, {}, {}).error, QStringLiteral("effect-failed"));
    effect.work = [](const QImage &, const ParamValues &, const EffectContext &) -> EffectResult { throw std::runtime_error("test"); };
    QCOMPARE(runEffect(effect, source, {}, {}).error, QStringLiteral("effect-failed"));
    for (const QString &code : {QString("effect-unavailable"), QString("effect-invalid-input"),
                                QString("effect-cancelled"), QString("unexpected")}) {
      effect.work = [code](const QImage &image, const ParamValues &, const EffectContext &) { return EffectResult{image, code}; };
      const auto result = runEffect(effect, source, {}, {});
      QCOMPARE(result.error, code == "unexpected" ? QString("effect-failed") : code);
      QVERIFY(result.image.isNull());
    }
  }
  void cancellationDiscardsEvenSuccessfulResult() {
    FakeEffect effect;
    bool cancelled = true;
    bool called = false;
    EffectContext context{[&] { return cancelled; }};
    effect.work = [&](const QImage &image, const ParamValues &, const EffectContext &) {
      called = true;
      cancelled = true;
      return EffectResult{image, {}};
    };
    auto result = runEffect(effect, sourceImage(), {}, context);
    QCOMPARE(result.error, QStringLiteral("effect-cancelled"));
    QVERIFY(!called && result.image.isNull());
    cancelled = false;
    result = runEffect(effect, sourceImage(), {}, context);
    QVERIFY(called && result.image.isNull());
    QCOMPARE(result.error, QStringLiteral("effect-cancelled"));
  }
  void seedIsPassedUnchangedAndRepeatable() {
    FakeEffect effect;
    effect.metadata.usesSeed = true;
    effect.work = [](QImage image, const ParamValues &, const EffectContext &context) {
      image.fill(QRgb(context.seed & 0xffffffff));
      return EffectResult{image, {}};
    };
    EffectContext context;
    context.seed = 0x12345678abcdef01ULL;
    const auto a = runEffect(effect, sourceImage(), {}, context);
    const auto b = runEffect(effect, sourceImage(), {}, context);
    QVERIFY(a.ok() && b.ok());
    QCOMPARE(a.image, b.image);
    QCOMPARE(a.image.pixel(0, 0), QRgb(0xabcdef01));
    ++context.seed;
    QVERIFY(runEffect(effect, sourceImage(), {}, context).image != a.image);
  }
};
QTEST_GUILESS_MAIN(EffectFrameworkTest)
#include "EffectFrameworkTest.moc"
