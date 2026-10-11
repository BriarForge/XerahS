#include "image-editor/EditorAppearance.h"
#include <QJsonArray>
#include <QTest>
#include <cmath>
#include <limits>
using namespace xerahs::editor;
class EditorAppearanceTest : public QObject {
  Q_OBJECT
private slots:
  void appearanceFieldsValidateIndependentlyAndPreserveSavedAlpha() {
    const auto defaults = loadEditorPreferences({});
    QCOMPARE(defaults.values.value("AccentColorHex"), QJsonValue("#FF3E83F2"));
    QVERIFY(defaults.values.value("UseSystemTheme").toBool()); QVERIFY(defaults.values.value("UseSystemAccentColor").toBool());
    const auto valid = loadEditorPreferences({{"AccentColorHex", "#1234abcd"}, {"AccentColor", "computed"},
        {"UseSystemTheme", false}, {"UseSystemAccentColor", false}, {"Theme", "Light"}});
    QCOMPARE(valid.values.value("AccentColorHex"), QJsonValue("#1234ABCD"));
    QVERIFY(!serializeEditorPreferences(valid).contains("AccentColor")); QVERIFY(valid.diagnostics.isEmpty());
    for (const auto *field : {"UseSystemTheme", "UseSystemAccentColor", "AccentColorHex"}) {
      const auto invalid = loadEditorPreferences({{field, 1}, {"Thickness", 9}, {"Theme", "Light"}});
      QCOMPARE(invalid.fields.value(field), PreferenceFieldState::Invalid);
      QCOMPARE(invalid.diagnostics, QStringList{QStringLiteral("invalid-") + field});
      QCOMPARE(invalid.values.value("Thickness"), QJsonValue(9)); QCOMPARE(invalid.values.value("Theme"), QJsonValue("Light"));
      QVERIFY(!changeEditorPreferences(valid, {{field, 1}, {"Theme", "Dark"}}).preferences);
    }
    for (const auto &color : {"#3e83f2", "#003e83f2", "#ff000000", "#ffffffff"}) {
      const auto loaded = loadEditorPreferences({{"AccentColorHex", color}}); QVERIFY(loaded.diagnostics.isEmpty());
      QCOMPARE(loadEditorPreferences(serializeEditorPreferences(loaded)).values.value("AccentColorHex"), loaded.values.value("AccentColorHex"));
    }
  }
  void desktopHintsRejectInvalidIndividualValues() {
    const auto valid = decodeDesktopAppearance({{"color-scheme", 1}, {"accent-color", QJsonArray{0, 0.5, 1}}, {"contrast", 1}, {"reduced-motion", 1}});
    QCOMPARE(valid.colorScheme, DesktopColorScheme::Dark); QCOMPARE(valid.accent, std::optional<Argb>(0xFF0080FF));
    QVERIFY(valid.highContrast && valid.reducedMotion);
    for (const auto &accent : {QJsonValue(QJsonArray{-0.1, 0, 1}), QJsonValue(QJsonArray{0, 1.01, 1}),
        QJsonValue(QJsonArray{0, "0.5", 1}), QJsonValue(QJsonArray{0, 1}), QJsonValue("blue"),
        QJsonValue(QJsonArray{0, std::numeric_limits<double>::infinity(), 1})}) {
      const auto bad = decodeDesktopAppearance({{"color-scheme", 2}, {"accent-color", accent}, {"contrast", 1}});
      QVERIFY(!bad.accent); QCOMPARE(bad.colorScheme, DesktopColorScheme::Light); QVERIFY(bad.highContrast);
    }
    for (const auto &value : {QJsonValue("1"), QJsonValue(true), QJsonValue(-1), QJsonValue(3), QJsonValue(1.5)}) {
      const auto bad = decodeDesktopAppearance({{"color-scheme", value}, {"contrast", value}, {"reduced-motion", value}});
      QCOMPARE(bad.colorScheme, DesktopColorScheme::Unspecified); QVERIFY(!bad.highContrast && !bad.reducedMotion);
    }
  }
  void manualSystemAndDesktopPrecedenceNeverRewritePreferences() {
    AppearancePalette native; native.window = 0xFFF1F2F3; native.base = 0xFFFFFFFF;
    native.highlight = 0xFF006000; DesktopAppearance desktop; desktop.colorScheme = DesktopColorScheme::Dark; desktop.accent = 0xFFFF8800;
    auto prefs = loadEditorPreferences({{"Theme", "Light"}, {"UseSystemTheme", false}, {"UseSystemAccentColor", false}, {"AccentColorHex", "#FF006000"}});
    const auto before = serializeEditorPreferences(prefs);
    auto resolved = resolveEditorAppearance(prefs, desktop, native);
    QCOMPARE(resolved.palette.window, Argb(0xFFF0F0F0)); QCOMPARE(resolved.palette.highlight, Argb(0xFF006000));
    prefs.values.insert("UseSystemTheme", true); prefs.values.insert("UseSystemAccentColor", true);
    resolved = resolveEditorAppearance(prefs, desktop, native);
    QCOMPARE(resolved.palette.window, Argb(0xFF292929)); QCOMPARE(resolved.palette.highlight, Argb(0xFFFF8800));
    desktop.colorScheme = DesktopColorScheme::Unspecified; desktop.accent.reset();
    resolved = resolveEditorAppearance(prefs, desktop, native);
    QCOMPARE(resolved.palette.window, native.window); QCOMPARE(resolved.palette.highlight, native.highlight);
    prefs.values.insert("Theme", "System"); prefs.values.insert("UseSystemTheme", false);
    QCOMPARE(resolveEditorAppearance(prefs, desktop, native).palette.window, native.window);
    prefs.values.insert("Theme", "Light"); prefs.values.insert("UseSystemAccentColor", false);
    QCOMPARE(serializeEditorPreferences(prefs), before);
  }
  void independentContrastReferenceAndAccessibleResolution() {
    // Published sRGB analytical cases, independent of the resolver's choices.
    QCOMPARE(appearanceContrastRatio(0xFF000000, 0xFFFFFFFF), 21.0);
    QVERIFY(std::abs(appearanceContrastRatio(0xFFFF0000, 0xFFFFFFFF) - 3.9984767707539985) < 1e-12);
    QVERIFY(std::abs(appearanceContrastRatio(0xFF777777, 0xFFFFFFFF) - 4.478089453577214) < 1e-12);
    for (const auto *theme : {"Light", "Dark", "System"}) for (bool high : {false, true})
      for (const auto *accent : {"#00000000", "#8000FF00", "#FFFFFFFF", "#FF000000", "#FF777777", "#FF3E83F2"}) {
        const auto prefs = loadEditorPreferences({{"Theme", theme}, {"UseSystemTheme", false}, {"UseSystemAccentColor", false}, {"AccentColorHex", accent}});
        const auto before = serializeEditorPreferences(prefs); DesktopAppearance desktop; desktop.highContrast = high; desktop.reducedMotion = true;
        const auto r = resolveEditorAppearance(prefs, desktop, {}); const auto &p = r.palette; const double min = high ? 7 : 4.5;
        QVERIFY(appearanceContrastRatio(p.windowText, p.window) >= min); QVERIFY(appearanceContrastRatio(p.text, p.base) >= min);
        QVERIFY(appearanceContrastRatio(p.text, p.alternateBase) >= min); QVERIFY(appearanceContrastRatio(p.buttonText, p.button) >= min);
        QVERIFY(appearanceContrastRatio(p.highlightedText, p.highlight) >= min); QVERIFY(appearanceContrastRatio(p.placeholderText, p.base) >= min);
        QVERIFY(appearanceContrastRatio(p.link, p.base) >= min);
        for (auto background : {p.window, p.base, p.button}) QVERIFY(appearanceContrastRatio(p.highlight, background) >= 3);
        QVERIFY(r.reducedMotion); QCOMPARE(r.highContrast, high); QCOMPARE(serializeEditorPreferences(prefs), before);
      }
  }
};
QTEST_GUILESS_MAIN(EditorAppearanceTest)
#include "EditorAppearanceTest.moc"
