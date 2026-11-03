#include "image-editor/EditorPreferences.h"
#include "image-editor/EditorPreferenceStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QLockFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <clocale>
#include <limits>

using namespace xerahs::editor;
namespace {
QByteArray envelope(const QJsonValue &current, const QJsonValue &previous = QJsonObject{}) {
  return QJsonDocument(QJsonObject{{"format", "com.xerahs.app/linux-qt/editor-settings"},
                                   {"version", 1}, {"current", current}, {"previous", previous}}).toJson();
}
class MemoryStorage final : public PreferenceStorage {
public:
  PreferenceRead artifact;
  bool failReplace = false;
  int replacements = 0;
  PreferenceRead read() override { return artifact; }
  PreferenceWrite replace(const PreferenceRead &expected, const QByteArray &bytes) override {
    ++replacements;
    if (failReplace) return {false, QStringLiteral("injected-write-failed")};
    if (artifact.kind != expected.kind || artifact.bytes != expected.bytes)
      return {false, QStringLiteral("settings-changed-externally")};
    artifact = {PreferenceRead::Kind::Readable, bytes, {}};
    return {true, {}};
  }
};
QJsonObject section(const MemoryStorage &storage, const QString &name) {
  return QJsonDocument::fromJson(storage.artifact.bytes).object().value(name).toObject();
}
bool privateWrite(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) &&
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) &&
      file.write(bytes) == bytes.size();
}
}

class EditorPreferencesTest : public QObject {
  Q_OBJECT
private slots:
  void convenienceFieldsRecoverIndependentlyAndRoundTripWithUnknowns() {
    const auto defaults = loadEditorPreferences({});
    const auto convenience = conveniencePreferencesFromPreferences(defaults);
    QVERIFY(!convenience.autoCopy); QVERIFY(!convenience.autoClose);
    QVERIFY(!convenience.zoomToFit); QVERIFY(!convenience.exitConfirmation);
    const QJsonObject unknown{{"nested", QJsonArray{"keep", 42}}};
    for (const auto &key : conveniencePreferenceFields()) {
      QCOMPARE(defaults.fields.value(key), PreferenceFieldState::Missing);
      for (const auto &invalid : {QJsonValue("false"), QJsonValue(0), QJsonValue(QJsonValue::Null), QJsonValue(QJsonArray{})}) {
        QJsonObject stored{{"Thickness", 8}, {"FutureConvenience", unknown}};
        for (const auto &other : conveniencePreferenceFields()) stored.insert(other, true);
        stored.insert(key, invalid);
        const auto repaired = loadEditorPreferences(stored);
        QCOMPARE(repaired.fields.value(key), PreferenceFieldState::Invalid);
        QCOMPARE(repaired.values.value(key), QJsonValue(false));
        QCOMPARE(repaired.values.value("Thickness"), QJsonValue(8));
        QCOMPARE(repaired.diagnostics, QStringList{QStringLiteral("invalid-") + key});
        for (const auto &other : conveniencePreferenceFields())
          if (other != key) QCOMPARE(repaired.values.value(other), QJsonValue(true));
        const auto rejected = changeEditorPreferences(defaults, {{key, invalid}, {"Thickness", 9}});
        QVERIFY(!rejected.preferences);
      }
      for (bool value : {false, true}) {
        MemoryStorage storage;
        storage.artifact = {PreferenceRead::Kind::Readable,
            envelope(QJsonObject{{"FutureConvenience", unknown}, {"Thickness", 8}}), {}};
        EditorPreferenceStore store(storage);
        const auto changed = changeEditorPreferences(store.preferences(), {{key, value}});
        QVERIFY(changed.preferences); QVERIFY(store.save(*changed.preferences).saved);
        EditorPreferenceStore restarted(storage);
        QCOMPARE(restarted.preferences().values.value(key), QJsonValue(value));
        QCOMPARE(restarted.preferences().fields.value(key), PreferenceFieldState::Valid);
        QCOMPARE(restarted.preferences().values.value("Thickness"), QJsonValue(8));
        QCOMPARE(section(storage, "current").value("FutureConvenience"), QJsonValue(unknown));
        QCOMPARE(section(storage, "previous").value("FutureConvenience"), QJsonValue(unknown));
      }
    }
  }
  void windowFieldsRecoverIndependentlyAndRespectInclusiveBounds() {
    const auto defaults = windowPreferencesFromPreferences(loadEditorPreferences({}));
    QVERIFY(defaults.remember); QVERIFY(defaults.maximized); QCOMPARE(defaults.normalSize, QSizeF(1280, 720));
    for (const auto *key : {"WindowWidth", "WindowHeight"}) {
      for (double value : {1.0, 640.5, maximumEditorWindowExtent}) {
        auto loaded = loadEditorPreferences({{key, value}}); QCOMPARE(loaded.fields.value(key), PreferenceFieldState::Valid);
        QCOMPARE(loaded.values.value(key).toDouble(), value);
      }
      for (const auto &value : {QJsonValue(0), QJsonValue(-1), QJsonValue(maximumEditorWindowExtent + 1),
                               QJsonValue(std::numeric_limits<double>::infinity()), QJsonValue("640"), QJsonValue(true)}) {
        const auto loaded = loadEditorPreferences({{key, value}, {"Thickness", 9}, {"Theme", "Light"}});
        QCOMPARE(loaded.fields.value(key), PreferenceFieldState::Invalid);
        QCOMPARE(loaded.diagnostics, QStringList{QStringLiteral("invalid-") + key});
        QCOMPARE(loaded.values.value("Theme"), QJsonValue("Light")); QCOMPARE(loaded.values.value("Thickness"), QJsonValue(9));
      }
    }
    for (const auto *key : {"RememberWindowState", "IsWindowMaximized"}) {
      const auto invalid = loadEditorPreferences({{key, "false"}, {"WindowHeight", 800}});
      QCOMPARE(invalid.fields.value(key), PreferenceFieldState::Invalid);
      QCOMPARE(invalid.values.value("WindowHeight"), QJsonValue(800));
      QCOMPARE(invalid.diagnostics, QStringList{QStringLiteral("invalid-") + key});
      const auto valid = loadEditorPreferences({{key, false}}); QCOMPARE(valid.fields.value(key), PreferenceFieldState::Valid);
    }
    const auto invalidWidth = loadEditorPreferences({{"WindowWidth", "NaN"}, {"WindowHeight", 900}, {"IsWindowMaximized", false}});
    QCOMPARE(windowPreferencesFromPreferences(invalidWidth).normalSize, QSizeF(1280, 900));
    QVERIFY(!windowPreferencesFromPreferences(invalidWidth).maximized);
    const auto rejected = changeEditorPreferences(loadEditorPreferences({}), {{"RememberWindowState", false}, {"WindowWidth", 0}});
    QVERIFY(!rejected.preferences); QCOMPARE(rejected.diagnostics, QStringList{"invalid-WindowWidth"});
  }
  void disablingWindowMemoryPrunesBothVersionsAndRecoveryCannotReenableIt() {
    MemoryStorage storage; EditorPreferenceStore store(storage);
    auto before = changeEditorPreferences(store.preferences(), {{"WindowWidth", 900}, {"WindowHeight", 600}, {"IsWindowMaximized", false}});
    QVERIFY(before.preferences); QVERIFY(store.save(*before.preferences).saved);
    auto disabled = changeEditorPreferences(store.preferences(), {{"RememberWindowState", false}});
    QVERIFY(disabled.preferences); QVERIFY(store.save(*disabled.preferences).saved);
    for (const auto *version : {"current", "previous"}) {
      const auto values = section(storage, version); QVERIFY(!values.value("RememberWindowState").toBool());
      for (const auto *field : {"WindowWidth", "WindowHeight", "IsWindowMaximized"}) QVERIFY(!values.contains(field));
    }
    const auto previous = section(storage, "previous");
    storage.artifact.bytes = envelope(QJsonValue("corrupt current"), previous);
    EditorPreferenceStore recovered(storage); QVERIFY(!windowPreferencesFromPreferences(recovered.preferences()).remember);
    auto next = changeEditorPreferences(recovered.preferences(), {{"Thickness", 8}});
    QVERIFY(next.preferences); QVERIFY(recovered.save(*next.preferences).saved);
    QVERIFY(!section(storage, "current").contains("WindowWidth"));
    QCOMPARE(recovered.preferences().values.value("Thickness"), QJsonValue(8));
  }
  void colorsPreserveAlphaNormalizeAndExcludeComputedAliases() {
    auto defaults = loadEditorPreferences({});
    QCOMPARE(defaults.values.value("BorderColorHex"), QJsonValue("#FFFF0000"));
    QCOMPARE(defaults.values.value("FillColorHex"), QJsonValue("#00000000"));
    const QJsonObject extension{{"future", "retained"}};
    auto loaded = loadEditorPreferences({{"BorderColorHex", "#1234abcd"}, {"FillColorHex", "#ab12ef"},
        {"BorderColor", "computed"}, {"FillColor", QJsonObject{{"computed", true}}}, {"FutureColor", extension}, {"Thickness", 9}});
    QCOMPARE(loaded.values.value("BorderColorHex"), QJsonValue("#1234ABCD"));
    QCOMPARE(loaded.values.value("FillColorHex"), QJsonValue("#FFAB12EF"));
    QCOMPARE(loaded.fields.value("BorderColor"), PreferenceFieldState::NonPersistent);
    QCOMPARE(loaded.fields.value("FillColor"), PreferenceFieldState::NonPersistent);
    QVERIFY(loaded.diagnostics.isEmpty());
    const auto serialized = serializeEditorPreferences(loaded);
    QVERIFY(!serialized.contains("BorderColor")); QVERIFY(!serialized.contains("FillColor"));
    QCOMPARE(serialized.value("FutureColor").toObject(), extension);
    const auto style = drawingStyleFromPreferences(loaded);
    QCOMPARE(style.strokeColor, Argb{0x1234ABCD}); QCOMPARE(style.fillColor, Argb{0xFFAB12EF});
    QCOMPARE(style.strokeWidth, 9.0); QCOMPARE(style.opacity, 1.0); QCOMPARE(style.rotationDegrees, 0.0);
    MemoryStorage storage; EditorPreferenceStore store(storage); QVERIFY(store.save(loaded).saved);
    auto next = changeEditorPreferences(store.preferences(), {{"BorderColorHex", "#00ff0012"}, {"FillColorHex", "#7f010203"}});
    QVERIFY(next.preferences); QVERIFY(store.save(*next.preferences).saved);
    QCOMPARE(section(storage, "previous").value("BorderColorHex"), QJsonValue("#1234ABCD"));
    QVERIFY(!section(storage, "previous").contains("BorderColor"));
    EditorPreferenceStore restarted(storage);
    QCOMPARE(restarted.preferences().values.value("BorderColorHex"), QJsonValue("#00FF0012"));
    QCOMPARE(restarted.preferences().values.value("FillColorHex"), QJsonValue("#7F010203"));
  }
  // ESET-007: the canonical representation and numeric text never depend on the
  // process locale, whether Qt's default (de_DE decimal comma, Arabic-Indic digits)
  // or the C library's LC_NUMERIC (applied when the host has that locale installed).
  void colorAndNumberParseAndWriteAreLocaleIndependent_data() {
    QTest::addColumn<QLocale>("locale"); QTest::addColumn<QByteArray>("lcNumeric");
    QTest::newRow("de_DE") << QLocale(QLocale::German, QLocale::Germany) << QByteArray("de_DE.UTF-8");
    QTest::newRow("ar_EG") << QLocale(QLocale::Arabic, QLocale::Egypt) << QByteArray("ar_EG.UTF-8");
    QTest::newRow("fa_IR") << QLocale(QLocale::Persian, QLocale::Iran) << QByteArray("fa_IR.UTF-8");
    QTest::newRow("tr_TR") << QLocale(QLocale::Turkish, QLocale::Turkey) << QByteArray("tr_TR.UTF-8");
  }
  void colorAndNumberParseAndWriteAreLocaleIndependent() {
    QFETCH(QLocale, locale); QFETCH(QByteArray, lcNumeric);
    struct Restore {
      QLocale qt = QLocale(); QByteArray c = std::setlocale(LC_NUMERIC, nullptr);
      ~Restore() { QLocale::setDefault(qt); std::setlocale(LC_NUMERIC, c.constData()); }
    } restore;
    QLocale::setDefault(locale);
    qputenv("LC_NUMERIC", lcNumeric);
    const bool cApplied = std::setlocale(LC_NUMERIC, lcNumeric.constData()) != nullptr;  // Absent locales fall back to Qt-only coverage.
    qInfo() << "LC_NUMERIC applied:" << cApplied << lcNumeric;
    QVERIFY(QLocale().toString(12.5) != QStringLiteral("12.5") || locale.language() == QLocale::Turkish);  // Sanity: default really differs.
    const QJsonObject input{{"BorderColorHex", "#1234abcd"}, {"FillColorHex", "#ab12ef"}, {"BlurStrength", 12.5}, {"Thickness", 9}};
    auto loaded = loadEditorPreferences(input);
    QVERIFY(loaded.diagnostics.isEmpty());
    QCOMPARE(loaded.values.value("BorderColorHex"), QJsonValue("#1234ABCD"));
    QCOMPARE(loaded.values.value("FillColorHex"), QJsonValue("#FFAB12EF"));
    QCOMPARE(loaded.values.value("BlurStrength").toDouble(), 12.5);
    const auto style = drawingStyleFromPreferences(loaded);
    QCOMPARE(style.strokeColor, Argb{0x1234ABCD}); QCOMPARE(style.fillColor, Argb{0xFFAB12EF}); QCOMPARE(style.strokeWidth, 9.0);
    MemoryStorage storage; EditorPreferenceStore store(storage); QVERIFY(store.save(loaded).saved);
    const QString text = QString::fromUtf8(storage.artifact.bytes);
    QVERIFY(text.contains("\"#1234ABCD\"")); QVERIFY(text.contains("\"#FFAB12EF\""));
    QVERIFY2(text.contains("12.5"), qPrintable(text));
    for (const QChar c : text) QVERIFY2(c.unicode() < 0x80, "stored text must be ASCII only: no locale digits or separators");
    auto changed = changeEditorPreferences(store.preferences(), {{"BorderColorHex", "#00ff0012"}, {"BlurStrength", 0.25}});
    QVERIFY(changed.preferences); QVERIFY(store.save(*changed.preferences).saved);
    QVERIFY(QString::fromUtf8(storage.artifact.bytes).contains("0.25"));
    EditorPreferenceStore restarted(storage);
    QCOMPARE(restarted.preferences().values.value("BorderColorHex"), QJsonValue("#00FF0012"));
    QCOMPARE(restarted.preferences().values.value("BlurStrength").toDouble(), 0.25);
    // Locale digits and separators are never accepted as canonical colour text.
    for (const QString &bad : {QStringLiteral("#\u0661\u0662\u0663\u0664\u0665\u0666\u0667\u0668"), QStringLiteral("#12,34567")}) {
      const auto invalid = loadEditorPreferences({{"BorderColorHex", bad}, {"Thickness", 4}});
      QCOMPARE(invalid.diagnostics, QStringList{"invalid-BorderColorHex"});
      QCOMPARE(invalid.values.value("BorderColorHex"), QJsonValue("#FFFF0000"));
      QCOMPARE(invalid.values.value("Thickness"), QJsonValue(4));
    }
    const auto commaNumber = changeEditorPreferences(restarted.preferences(), {{"BlurStrength", "12,5"}});
    QVERIFY(!commaNumber.preferences); QCOMPARE(commaNumber.diagnostics, QStringList{"invalid-BlurStrength"});
  }
  // ESET-012: recent image paths are private local data. Every diagnostic the core load,
  // store recovery and failed save produce is a closed value-free code, never path text.
  void recentPathsNeverAppearInPreferenceDiagnostics() {
    const QString secret = QStringLiteral("/home/private-sentinel/secret-photo.png");
    const QRegularExpression code(QStringLiteral("^[A-Za-z0-9-]+$"));  // A path cannot match: no '/' or '.'.
    QStringList produced;
    auto collect = [&](const QStringList &diagnostics) { produced += diagnostics; };
    // Opt-out with stored paths, a bad list, an over-maximum list, and a newer record.
    collect(loadEditorPreferences({{"RecentImageFiles", QJsonArray{secret}}}).diagnostics);
    collect(loadEditorPreferences({{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret, "relative/secret.png"}}}).diagnostics);
    collect(loadEditorPreferences({{"RememberRecentImagePaths", true}, {"MaxRecentImageFiles", 1},
        {"RecentImageFiles", QJsonArray{secret, secret + "2"}}}).diagnostics);
    collect(loadEditorPreferences({{"schemaVersion", 2}, {"RecentImageFiles", QJsonArray{secret}}}).diagnostics);
    collect(loadEditorPreferences({{"schemaVersion", secret}, {"RecentImageFiles", QJsonArray{secret}}}).diagnostics);
    // Store recovery from a damaged current record whose previous record holds paths.
    MemoryStorage recovery; recovery.artifact = {PreferenceRead::Kind::Readable,
        envelope(QJsonValue(secret), QJsonObject{{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret}}}), {}};
    EditorPreferenceStore recovered(recovery); collect(recovered.diagnostics());
    QCOMPARE(recovered.preferences().recent.paths(), QStringList{secret});  // The data itself is intact and usable.
    MemoryStorage damaged; damaged.artifact = {PreferenceRead::Kind::Readable, QByteArray("{\"x\":\"") + secret.toUtf8(), {}};
    EditorPreferenceStore unreadable(damaged); collect(unreadable.diagnostics());
    // Failed saves and rejected changes while paths are present.
    MemoryStorage failing; failing.artifact = {PreferenceRead::Kind::Readable,
        envelope(QJsonObject{{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret}}}), {}};
    EditorPreferenceStore store(failing); collect(store.diagnostics());
    auto change = changeEditorPreferences(store.preferences(), {{"Thickness", 3}}); QVERIFY(change.preferences);
    failing.failReplace = true; collect({store.save(*change.preferences).diagnostic});
    failing.failReplace = false; failing.artifact.bytes += " "; collect({store.save(*change.preferences).diagnostic});
    const auto rejected = changeEditorPreferences(store.preferences(), {{"MaxRecentImageFiles", secret}, {"RecentImageFiles", QJsonArray{secret + "x"}}});
    QVERIFY(!rejected.preferences); collect(rejected.diagnostics);
    QVERIFY(!produced.isEmpty());
    for (const QString &diagnostic : produced) {
      QVERIFY2(!diagnostic.contains(QStringLiteral("private-sentinel")) && !diagnostic.contains(QStringLiteral("secret")), qPrintable(diagnostic));
      QVERIFY2(code.match(diagnostic).hasMatch(), qPrintable(diagnostic));
    }
    QVERIFY(produced.contains("recent-paths-ignored-without-opt-in")); QVERIFY(produced.contains("invalid-RecentImageFiles"));
    QVERIFY(produced.contains("settings-newer-version")); QVERIFY(produced.contains("recovered-previous-settings"));
  }
  void invalidColorsRecoverIndependentlyAndRejectWholeChange_data() {
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("name") << QJsonValue("red");
    QTest::newRow("short") << QJsonValue("#FFF");
    QTest::newRow("length") << QJsonValue("#FFFFFFFFF");
    QTest::newRow("invalid-digit") << QJsonValue("#00FF00ZZ");
    QTest::newRow("leading-space") << QJsonValue(" #FFFFFFFF");
    QTest::newRow("trailing-newline") << QJsonValue("#FFFFFFFF\n");
    QTest::newRow("number") << QJsonValue(0x123456);
    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
  }
  void invalidColorsRecoverIndependentlyAndRejectWholeChange() {
    QFETCH(QJsonValue, value);
    const auto loaded = loadEditorPreferences({{"BorderColorHex", value}, {"FillColorHex", "#80010203"}, {"Thickness", 10}});
    QCOMPARE(loaded.values.value("BorderColorHex"), QJsonValue("#FFFF0000"));
    QCOMPARE(loaded.values.value("FillColorHex"), QJsonValue("#80010203"));
    QCOMPARE(loaded.values.value("Thickness"), QJsonValue(10));
    QCOMPARE(loaded.diagnostics, QStringList{"invalid-BorderColorHex"});
    const auto invalidFill = loadEditorPreferences({{"BorderColorHex", "#40112233"}, {"FillColorHex", value}});
    QCOMPARE(invalidFill.values.value("BorderColorHex"), QJsonValue("#40112233"));
    QCOMPARE(invalidFill.values.value("FillColorHex"), QJsonValue("#00000000"));
    QCOMPARE(invalidFill.diagnostics, QStringList{"invalid-FillColorHex"});
    const auto changed = changeEditorPreferences(loadEditorPreferences({}), {{"Thickness", 12}, {"FillColorHex", value}});
    QVERIFY(!changed.preferences); QCOMPARE(changed.diagnostics, QStringList{"invalid-FillColorHex"});
  }
  void defaultAndInvalidFieldsRemainIsolated() {
    const auto defaults = loadEditorPreferences({});
    QVERIFY(!defaults.readOnly); QVERIFY(!defaults.recent.enabled());
    QCOMPARE(defaults.fields.value("Theme"), PreferenceFieldState::Missing);
    QVERIFY(defaults.diagnostics.isEmpty());
    const auto loaded = loadEditorPreferences({{"Theme", "Dark"}, {"BlurStrength", "NaN"}, {"Thickness", 8}});
    QCOMPARE(loaded.values.value("Theme").toString(), QStringLiteral("Dark"));
    QCOMPARE(loaded.values.value("BlurStrength").toInt(), 30);
    QCOMPARE(loaded.values.value("Thickness").toInt(), 8);
    QCOMPARE(loaded.fields.value("Thickness"), PreferenceFieldState::Valid);
    QCOMPARE(loaded.fields.value("BlurStrength"), PreferenceFieldState::Invalid);
    QCOMPARE(loaded.diagnostics, QStringList{"invalid-BlurStrength"});
    const auto unrelated = loadEditorPreferences({{"Theme", "Unrecognized"}, {"BlurStrength", 12.5}, {"Thickness", 8}});
    QCOMPARE(unrelated.values.value("Theme").toString(), QStringLiteral("Dark"));
    QCOMPARE(unrelated.values.value("BlurStrength").toDouble(), 12.5);
    QCOMPARE(unrelated.values.value("Thickness").toInt(), 8);
    QCOMPARE(unrelated.diagnostics, QStringList{"invalid-Theme"});
  }
  void documentedNumericBoundaries_data() {
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value"); QTest::addColumn<bool>("valid");
    QTest::newRow("blur-low") << QString("BlurStrength") << QJsonValue(0) << true;
    QTest::newRow("blur-high") << QString("BlurStrength") << QJsonValue(100) << true;
    QTest::newRow("blur-fraction") << QString("BlurStrength") << QJsonValue(30.5) << true;
    QTest::newRow("blur-under") << QString("BlurStrength") << QJsonValue(-0.01) << false;
    QTest::newRow("blur-over") << QString("BlurStrength") << QJsonValue(100.01) << false;
    QTest::newRow("blur-nonfinite") << QString("BlurStrength") << QJsonValue(std::numeric_limits<double>::infinity()) << false;
    QTest::newRow("thickness-low") << QString("Thickness") << QJsonValue(1) << true;
    QTest::newRow("thickness-high") << QString("Thickness") << QJsonValue(10000) << true;
    QTest::newRow("thickness-zero") << QString("Thickness") << QJsonValue(0) << false;
    QTest::newRow("thickness-over") << QString("Thickness") << QJsonValue(10001) << false;
    QTest::newRow("thickness-fraction") << QString("Thickness") << QJsonValue(8.5) << false;
    QTest::newRow("maximum-low") << QString("MaxRecentImageFiles") << QJsonValue(1) << true;
    QTest::newRow("maximum-high") << QString("MaxRecentImageFiles") << QJsonValue(100) << true;
    QTest::newRow("maximum-zero") << QString("MaxRecentImageFiles") << QJsonValue(0) << false;
    QTest::newRow("maximum-over") << QString("MaxRecentImageFiles") << QJsonValue(101) << false;
    QTest::newRow("maximum-fraction") << QString("MaxRecentImageFiles") << QJsonValue(10.5) << false;
    QTest::newRow("boolean-wrong-type") << QString("RememberRecentImagePaths") << QJsonValue("true") << false;
  }
  void documentedNumericBoundaries() {
    QFETCH(QString, field); QFETCH(QJsonValue, value); QFETCH(bool, valid);
    const auto loaded = loadEditorPreferences({{field, value}});
    QCOMPARE(loaded.fields.value(field), valid ? PreferenceFieldState::Valid : PreferenceFieldState::Invalid);
    QCOMPARE(loaded.diagnostics.isEmpty(), valid);
  }
  void unknownRoundTripAndAtomicChanges() {
    const QJsonObject future{{"enabled", true}, {"nested", QJsonArray{1, "future"}}};
    const auto before = loadEditorPreferences({{"Theme", "Light"}, {"FutureOption", future}});
    QCOMPARE(before.fields.value("FutureOption"), PreferenceFieldState::Unknown);
    const auto changed = changeEditorPreferences(before, {{"Theme", "Dark"}});
    QVERIFY(changed.preferences);
    QCOMPARE(serializeEditorPreferences(*changed.preferences).value("FutureOption").toObject(), future);
    const auto rejected = changeEditorPreferences(before, {{"Theme", "Dark"}, {"Thickness", -1}});
    QVERIFY(!rejected.preferences);
    QCOMPARE(before.values.value("Theme").toString(), QStringLiteral("Light"));
    QCOMPARE(rejected.diagnostics, QStringList{"invalid-Thickness"});
  }
  // ESET-002: Unknown is distinguished through the stored-file load path, stays
  // silent (no diagnostic), is not conflated with Invalid, and survives a save.
  void unknownFieldStateSurvivesStoreLoadWithoutDiagnosticOrInvalidConflation() {
    const QJsonObject future{{"enabled", true}};
    MemoryStorage storage; storage.artifact = {PreferenceRead::Kind::Readable,
        envelope(QJsonObject{{"Theme", "Light"}, {"FutureOption", future}, {"BlurStrength", "NaN"}}), {}};
    EditorPreferenceStore store(storage);
    const auto &loaded = store.preferences();
    QCOMPARE(loaded.fields.value("FutureOption"), PreferenceFieldState::Unknown);
    QCOMPARE(loaded.fields.value("Theme"), PreferenceFieldState::Valid);
    QCOMPARE(loaded.fields.value("BlurStrength"), PreferenceFieldState::Invalid);
    QCOMPARE(loaded.fields.value("Thickness"), PreferenceFieldState::Missing);
    QCOMPARE(store.diagnostics(), QStringList{"invalid-BlurStrength"});  // Unknown adds no diagnostic.
    QVERIFY(!loaded.readOnly);
    const auto changed = changeEditorPreferences(loaded, {{"Thickness", 7}}); QVERIFY(changed.preferences);
    QVERIFY(store.save(*changed.preferences).saved);
    EditorPreferenceStore restarted(storage);
    QCOMPARE(restarted.preferences().fields.value("FutureOption"), PreferenceFieldState::Unknown);
    QCOMPARE(section(storage, "current").value("FutureOption").toObject(), future);
    QCOMPARE(restarted.preferences().fields.value("BlurStrength"), PreferenceFieldState::Valid);  // Repaired by the save.
    QVERIFY(restarted.diagnostics().isEmpty());
  }
  // ESET-008: persisted Text, SpeechBalloon and Step defaults.
  void textDefaultsHaveDocumentedDefaultsAndResolveToCreationDefaults() {
    const auto loaded = loadEditorPreferences({});
    QCOMPARE(loaded.values.value("TextFontFamily"), QJsonValue(""));
    QCOMPARE(loaded.values.value("TextFontSize"), QJsonValue(24));
    QCOMPARE(loaded.values.value("TextBold"), QJsonValue(true));
    QCOMPARE(loaded.values.value("TextHorizontalAlignment"), QJsonValue("Center"));
    QCOMPARE(loaded.values.value("StepType"), QJsonValue("Numeric"));
    QCOMPARE(loaded.values.value("StepFontSize"), QJsonValue(0));
    const auto defaults = contentDefaultsFromPreferences(loaded, [](const QString &) { return true; });
    QCOMPARE(defaults.text.alignment, QStringLiteral("center")); QVERIFY(defaults.text.bold); QVERIFY(!defaults.text.italic);
    QVERIFY(!defaults.text.familyUnavailable); QVERIFY(!defaults.stepStyleInert); QCOMPARE(defaults.stepFontSize, 0.0);
    const auto custom = loadEditorPreferences({{"TextFontFamily", "Installed"}, {"TextFontSize", 17.25}, {"TextBold", false},
        {"TextItalic", true}, {"TextHorizontalAlignment", "Right"}, {"SpeechBalloonFontSize", 31.5},
        {"SpeechBalloonTextHorizontalAlignment", "Left"}, {"StepFontSize", 12.5}});
    QVERIFY(custom.diagnostics.isEmpty());
    const auto resolved = contentDefaultsFromPreferences(custom, [](const QString &f) { return f == "Installed"; });
    QCOMPARE(resolved.text.fontFamily, QStringLiteral("Installed")); QCOMPARE(resolved.text.fontSize, 17.25);
    QVERIFY(!resolved.text.bold); QVERIFY(resolved.text.italic); QCOMPARE(resolved.text.alignment, QStringLiteral("right"));
    QCOMPARE(resolved.balloon.fontSize, 31.5); QCOMPARE(resolved.balloon.alignment, QStringLiteral("left"));
    QCOMPARE(resolved.stepFontSize, 12.5);
  }
  void textDefaultBoundariesAndInvalidValuesRecoverPerField_data() {
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value"); QTest::addColumn<bool>("valid");
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto *size : {"TextFontSize", "SpeechBalloonFontSize"}) {
      QTest::newRow(qPrintable(QString(size) + "-min")) << QString(size) << QJsonValue(1) << true;
      QTest::newRow(qPrintable(QString(size) + "-fraction")) << QString(size) << QJsonValue(12.375) << true;
      QTest::newRow(qPrintable(QString(size) + "-max")) << QString(size) << QJsonValue(2000) << true;
      QTest::newRow(qPrintable(QString(size) + "-under")) << QString(size) << QJsonValue(0.99) << false;
      QTest::newRow(qPrintable(QString(size) + "-zero")) << QString(size) << QJsonValue(0) << false;
      QTest::newRow(qPrintable(QString(size) + "-over")) << QString(size) << QJsonValue(2000.01) << false;
      QTest::newRow(qPrintable(QString(size) + "-inf")) << QString(size) << QJsonValue(inf) << false;
      QTest::newRow(qPrintable(QString(size) + "-string")) << QString(size) << QJsonValue("24") << false;
    }
    QTest::newRow("step-size-auto") << QString("StepFontSize") << QJsonValue(0) << true;
    QTest::newRow("step-size-max") << QString("StepFontSize") << QJsonValue(2000) << true;
    QTest::newRow("step-size-negative") << QString("StepFontSize") << QJsonValue(-1) << false;
    QTest::newRow("step-size-over") << QString("StepFontSize") << QJsonValue(2001) << false;
    for (const auto *flag : {"TextBold", "TextItalic", "SpeechBalloonTextBold", "SpeechBalloonTextItalic"}) {
      QTest::newRow(qPrintable(QString(flag) + "-true")) << QString(flag) << QJsonValue(true) << true;
      QTest::newRow(qPrintable(QString(flag) + "-number")) << QString(flag) << QJsonValue(1) << false;
    }
    for (const auto *align : {"TextHorizontalAlignment", "SpeechBalloonTextHorizontalAlignment"}) {
      for (const auto *ok : {"Left", "Center", "Right"}) QTest::newRow(qPrintable(QString(align) + ok)) << QString(align) << QJsonValue(ok) << true;
      QTest::newRow(qPrintable(QString(align) + "-justify")) << QString(align) << QJsonValue("Justify") << false;
      QTest::newRow(qPrintable(QString(align) + "-lower")) << QString(align) << QJsonValue("left") << false;
    }
    for (const auto *family : {"TextFontFamily", "SpeechBalloonFontFamily"}) {
      QTest::newRow(qPrintable(QString(family) + "-empty")) << QString(family) << QJsonValue("") << true;
      QTest::newRow(qPrintable(QString(family) + "-256")) << QString(family) << QJsonValue(QString(256, 'x')) << true;
      QTest::newRow(qPrintable(QString(family) + "-257")) << QString(family) << QJsonValue(QString(257, 'x')) << false;
      QTest::newRow(qPrintable(QString(family) + "-control")) << QString(family) << QJsonValue("Bad\nFont") << false;
      QTest::newRow(qPrintable(QString(family) + "-number")) << QString(family) << QJsonValue(3) << false;
    }
    QTest::newRow("step-style-numeric") << QString("StepType") << QJsonValue("Numeric") << true;
    QTest::newRow("step-style-blocked") << QString("StepType") << QJsonValue("UppercaseRoman") << true;
    QTest::newRow("step-style-future") << QString("StepType") << QJsonValue("FutureStyle9") << true;
    QTest::newRow("step-style-empty") << QString("StepType") << QJsonValue("") << false;
    QTest::newRow("step-style-number") << QString("StepType") << QJsonValue(2) << false;
  }
  void textDefaultBoundariesAndInvalidValuesRecoverPerField() {
    QFETCH(QString, field); QFETCH(QJsonValue, value); QFETCH(bool, valid);
    const auto loaded = loadEditorPreferences({{field, value}, {"Thickness", 9}});
    QCOMPARE(loaded.fields.value(field), valid ? PreferenceFieldState::Valid : PreferenceFieldState::Invalid);
    QCOMPARE(loaded.diagnostics, valid ? QStringList{} : QStringList{"invalid-" + field});
    QCOMPARE(loaded.values.value("Thickness"), QJsonValue(9));
    QCOMPARE(loaded.values.value(field), valid ? value : loadEditorPreferences({}).values.value(field));
    const auto changed = changeEditorPreferences(loadEditorPreferences({}), {{field, value}});
    QCOMPARE(bool(changed.preferences), valid);
  }
  // ESET-006: the creation defaults beyond colour/thickness/text. Names follow the reference ImageEditorOptions.
  void creationDefaultsHaveDocumentedDefaultsAndResolveToCreationDefaults() {
    const auto loaded = loadEditorPreferences({});
    QVERIFY(loaded.diagnostics.isEmpty());
    QCOMPARE(loaded.values.value("LastUsedAnnotationTool"), QJsonValue("Rectangle"));
    QCOMPARE(loaded.values.value("BorderStyle"), QJsonValue("Solid")); QCOMPARE(loaded.values.value("ArrowStyle"), QJsonValue("Classic"));
    QCOMPARE(loaded.values.value("CursorType"), QJsonValue("Default")); QCOMPARE(loaded.values.value("CornerRadius"), QJsonValue(0));
    QCOMPARE(loaded.values.value("Shadow"), QJsonValue(false)); QCOMPARE(loaded.values.value("ShadowColorHex"), QJsonValue("#80000000"));
    const auto plain = drawingStyleFromPreferences(loaded);
    QVERIFY(!hasDecoration(shape::rectangle, plain)); QCOMPARE(plain.cornerRadius, 0.0);
    QVERIFY(plain.borderStyle == BorderStyle::Solid); QVERIFY(plain.arrowStyle == ArrowStyle::Classic); QVERIFY(!plain.shadow.enabled);
    const auto emphasis = contentDefaultsFromPreferences(loaded, [](const QString &) { return true; });
    QCOMPARE(emphasis.emphasis.blurStrength, 30.0); QCOMPARE(emphasis.emphasis.pixelateStrength, 20.0);
    QCOMPARE(emphasis.emphasis.magnifyStrength, 2.0); QCOMPARE(emphasis.emphasis.spotlightStrength, 30.0);
    QCOMPARE(emphasis.emphasis.spotlightBlur, 0.0); QVERIFY(!emphasis.emphasis.magnifyEllipse); QVERIFY(!emphasis.emphasis.spotlightEllipse);
    QCOMPARE(emphasis.cursorType, QStringLiteral("Default")); QVERIFY(!emphasis.cursorTypeInert);
    const QJsonObject custom{{"LastUsedAnnotationTool", "Spotlight"}, {"CornerRadius", 12}, {"Shadow", true}, {"ShadowColorHex", "#40112233"},
        {"ShadowBlurRadius", 9.5}, {"ShadowOpacity", 0.25}, {"ShadowOffsetX", -7}, {"ShadowOffsetY", 8.5}, {"BorderStyle", "DashDot"},
        {"ArrowStyle", "Modern"}, {"CursorType", "Arrow"}, {"BlurStrength", 12.5}, {"PixelateStrength", 40}, {"MagnifierStrength", 4},
        {"MagnifierEllipse", true}, {"SpotlightStrength", 55}, {"SpotlightBlur", 6}, {"SpotlightEllipse", true}};
    const auto loadedCustom = loadEditorPreferences(custom);
    QVERIFY(loadedCustom.diagnostics.isEmpty());
    for (auto it = custom.begin(); it != custom.end(); ++it) QCOMPARE(loadedCustom.fields.value(it.key()), PreferenceFieldState::Valid);
    const auto style = drawingStyleFromPreferences(loadedCustom);
    QCOMPARE(style.cornerRadius, 12.0); QVERIFY(style.borderStyle == BorderStyle::DashDot); QVERIFY(style.arrowStyle == ArrowStyle::Modern);
    QVERIFY(style.shadow.enabled); QCOMPARE(style.shadow.color, Argb{0x40112233}); QCOMPARE(style.shadow.blurRadius, 9.5);
    QCOMPARE(style.shadow.opacity, 0.25); QCOMPARE(style.shadow.offsetX, -7.0); QCOMPARE(style.shadow.offsetY, 8.5);
    QCOMPARE(lastAnnotationToolFromPreferences(loadedCustom), QStringLiteral("Spotlight"));
    const auto content = contentDefaultsFromPreferences(loadedCustom, [](const QString &) { return true; });
    QCOMPARE(content.emphasis.blurStrength, 12.5); QCOMPARE(content.emphasis.pixelateStrength, 40.0); QCOMPARE(content.emphasis.magnifyStrength, 4.0);
    QVERIFY(content.emphasis.magnifyEllipse); QCOMPARE(content.emphasis.spotlightStrength, 55.0); QCOMPARE(content.emphasis.spotlightBlur, 6.0);
    QVERIFY(content.emphasis.spotlightEllipse); QCOMPARE(content.cursorType, QStringLiteral("Arrow"));
    // drawingPreferenceValues is the inverse of drawingStyleFromPreferences.
    const auto roundTrip = loadEditorPreferences(drawingPreferenceValues(style));
    QVERIFY(roundTrip.diagnostics.isEmpty()); QVERIFY(sameDecoration(drawingStyleFromPreferences(roundTrip), style));
  }
  void creationEnumFieldsValidateRepairPerFieldAndKeepInertCursorsAsData_data() {
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value"); QTest::addColumn<bool>("valid");
    for (const auto *style : {"Solid", "Dash", "Dot", "DashDot", "DashDotDot"}) QTest::newRow(qPrintable(QString("border-") + style)) << QString("BorderStyle") << QJsonValue(style) << true;
    for (const auto *style : {"Classic", "Modern", "Double", "Basic", "Line"}) QTest::newRow(qPrintable(QString("arrow-") + style)) << QString("ArrowStyle") << QJsonValue(style) << true;
    for (const auto &name : cursorTypeNames()) QTest::newRow(qPrintable("cursor-" + name)) << QString("CursorType") << QJsonValue(name) << true;
    for (const auto *tool : {"Select", "Rectangle", "Ellipse", "Line", "Arrow", "Freehand", "Text", "SpeechBalloon", "Step", "Image", "Emoji", "Cursor",
                             "Highlight", "SmartEraser", "Blur", "Pixelate", "Magnify", "Spotlight", "Crop", "CutOut"})
      QTest::newRow(qPrintable(QString("tool-") + tool)) << QString("LastUsedAnnotationTool") << QJsonValue(tool) << true;
    QTest::newRow("border-lower") << QString("BorderStyle") << QJsonValue("solid") << false;
    QTest::newRow("border-unknown") << QString("BorderStyle") << QJsonValue("Wavy") << false;
    QTest::newRow("border-number") << QString("BorderStyle") << QJsonValue(1) << false;
    QTest::newRow("arrow-unknown") << QString("ArrowStyle") << QJsonValue("Fancy") << false;
    QTest::newRow("cursor-unknown") << QString("CursorType") << QJsonValue("Spinner") << false;
    QTest::newRow("tool-unknown") << QString("LastUsedAnnotationTool") << QJsonValue("Lasso") << false;
    QTest::newRow("tool-empty") << QString("LastUsedAnnotationTool") << QJsonValue("") << false;
    QTest::newRow("tool-number") << QString("LastUsedAnnotationTool") << QJsonValue(3) << false;
    for (const auto *flag : {"Shadow", "MagnifierEllipse", "SpotlightEllipse"}) {
      QTest::newRow(qPrintable(QString(flag) + "-true")) << QString(flag) << QJsonValue(true) << true;
      QTest::newRow(qPrintable(QString(flag) + "-string")) << QString(flag) << QJsonValue("true") << false;
      QTest::newRow(qPrintable(QString(flag) + "-number")) << QString(flag) << QJsonValue(1) << false;
    }
    QTest::newRow("shadow-colour-argb") << QString("ShadowColorHex") << QJsonValue("#80010203") << true;
    QTest::newRow("shadow-colour-rgb") << QString("ShadowColorHex") << QJsonValue("#010203") << true;
    QTest::newRow("shadow-colour-name") << QString("ShadowColorHex") << QJsonValue("black") << false;
    QTest::newRow("shadow-colour-short") << QString("ShadowColorHex") << QJsonValue("#123") << false;
  }
  void creationEnumFieldsValidateRepairPerFieldAndKeepInertCursorsAsData() {
    QFETCH(QString, field); QFETCH(QJsonValue, value); QFETCH(bool, valid);
    const auto loaded = loadEditorPreferences({{field, value}, {"Thickness", 9}, {"FutureCreation", 1}});
    QCOMPARE(loaded.fields.value(field), valid ? PreferenceFieldState::Valid : PreferenceFieldState::Invalid);
    QCOMPARE(loaded.diagnostics, valid ? QStringList{} : QStringList{"invalid-" + field});
    QCOMPARE(loaded.values.value("Thickness"), QJsonValue(9)); QCOMPARE(loaded.values.value("FutureCreation"), QJsonValue(1));
    if (!valid) QCOMPARE(loaded.values.value(field), loadEditorPreferences({}).values.value(field));
    QCOMPARE(bool(changeEditorPreferences(loadEditorPreferences({}), {{field, value}}).preferences), valid);
    if (field == u"CursorType" && valid) {
      const auto content = contentDefaultsFromPreferences(loaded, [](const QString &) { return true; });
      QCOMPARE(content.requestedCursorType, value.toString());
      QCOMPARE(content.cursorTypeInert, !cursorTypeImplemented(value.toString()));
      QCOMPARE(content.cursorType, cursorTypeImplemented(value.toString()) ? value.toString() : QStringLiteral("Default"));
      // An unavailable saved cursor survives an unrelated save untouched.
      const auto kept = changeEditorPreferences(loaded, {{"Thickness", 5}}); QVERIFY(kept.preferences);
      QCOMPARE(serializeEditorPreferences(*kept.preferences).value("CursorType"), value);
    }
    if (field == u"LastUsedAnnotationTool" && valid)
      QCOMPARE(lastAnnotationToolFromPreferences(loaded), rememberableAnnotationTool(value.toString()) ? value.toString() : QStringLiteral("Rectangle"));
    if (field == u"ShadowColorHex" && valid) QCOMPARE(loaded.values.value(field).toString().size(), 9);
  }
  void creationDefaultsPersistAtomicallyRoundTripUnknownsAndRepairTheDamagedFieldOnly() {
    MemoryStorage storage;
    storage.artifact = {PreferenceRead::Kind::Readable, envelope(QJsonObject{{"CornerRadius", 3}, {"FutureCreationOption", QJsonObject{{"x", 1}}}}), {}};
    EditorPreferenceStore store(storage);
    auto changed = changeEditorPreferences(store.preferences(), {{"CornerRadius", 8}, {"Shadow", true}, {"ShadowBlurRadius", 12}, {"BorderStyle", "Dot"},
        {"ArrowStyle", "Double"}, {"CursorType", "Hand"}, {"LastUsedAnnotationTool", "Blur"}, {"PixelateStrength", 33}, {"SpotlightEllipse", true}});
    QVERIFY(changed.preferences); storage.failReplace = true;
    const QByteArray original = storage.artifact.bytes;
    QVERIFY(!store.save(*changed.preferences).saved);  // a failed write publishes nothing
    QCOMPARE(storage.artifact.bytes, original); QCOMPARE(store.preferences().values.value("CornerRadius"), QJsonValue(3));
    storage.failReplace = false; QVERIFY(store.save(*changed.preferences).saved);
    const auto current = section(storage, "current");
    QCOMPARE(current.value("CornerRadius"), QJsonValue(8)); QCOMPARE(current.value("BorderStyle"), QJsonValue("Dot"));
    QCOMPARE(current.value("CursorType"), QJsonValue("Hand")); QCOMPARE(current.value("LastUsedAnnotationTool"), QJsonValue("Blur"));
    QCOMPARE(current.value("FutureCreationOption").toObject(), (QJsonObject{{"x", 1}}));
    QCOMPARE(section(storage, "previous").value("CornerRadius"), QJsonValue(3));
    EditorPreferenceStore restarted(storage);
    QCOMPARE(restarted.preferences().values.value("PixelateStrength"), QJsonValue(33));
    QCOMPARE(restarted.preferences().values.value("SpotlightEllipse"), QJsonValue(true));
    QCOMPARE(restarted.preferences().values.value("Shadow"), QJsonValue(true));
    // A damaged stored field is repaired alone and the rest of the creation defaults survive.
    const auto repaired = loadEditorPreferences({{"CornerRadius", -4}, {"BorderStyle", "Dash"}, {"ShadowOpacity", 0.5}, {"MagnifierStrength", "x"}});
    QCOMPARE(repaired.diagnostics, (QStringList{"invalid-CornerRadius", "invalid-MagnifierStrength"}));
    QCOMPARE(repaired.values.value("BorderStyle"), QJsonValue("Dash")); QCOMPARE(repaired.values.value("ShadowOpacity"), QJsonValue(0.5));
    const auto newer = loadEditorPreferences({{"schemaVersion", 2}, {"CornerRadius", 8}});
    QVERIFY(newer.readOnly); QVERIFY(!changeEditorPreferences(newer, {{"CornerRadius", 9}}).preferences);
  }
  void inertStepStylesAreNotImplementedButPreserved() {
    for (const auto *style : {"FutureStyle9", "uppercaseletter", "Roman"}) {
      const auto loaded = loadEditorPreferences({{"StepType", style}});
      const auto defaults = contentDefaultsFromPreferences(loaded, [](const QString &) { return true; });
      QVERIFY(defaults.stepStyleInert); QCOMPARE(defaults.requestedStepStyle, QString(style));
      const auto changed = changeEditorPreferences(loaded, {{"Thickness", 5}});
      QVERIFY(changed.preferences);
      QCOMPARE(serializeEditorPreferences(*changed.preferences).value("StepType"), QJsonValue(style));
    }
    QVERIFY(stepStyleImplemented("Numeric")); QVERIFY(!stepStyleImplemented("FutureStyle9")); QVERIFY(!stepStyleImplemented("uppercaseLetter"));
  }
  void everyReferenceStepStyleIsImplementedAndReachesCreationDefaults() {
    // EA-009: the ESET-008 StepType names are the reference StepType members and are active now.
    const std::pair<const char *, StepStyle> styles[] = {{"Numeric", StepStyle::Numeric}, {"UppercaseLetter", StepStyle::UppercaseLetter},
        {"LowercaseLetter", StepStyle::LowercaseLetter}, {"UppercaseRoman", StepStyle::UppercaseRoman}, {"LowercaseRoman", StepStyle::LowercaseRoman}};
    for (const auto &[name, style] : styles) {
      QVERIFY(stepStyleImplemented(name));
      const auto loaded = loadEditorPreferences({{"StepType", name}});
      const auto defaults = contentDefaultsFromPreferences(loaded, [](const QString &) { return true; });
      QVERIFY(!defaults.stepStyleInert); QCOMPARE(defaults.requestedStepStyle, QString(name)); QVERIFY(defaults.stepStyle == style);
      const auto changed = changeEditorPreferences(loaded, {{"Thickness", 5}});
      QVERIFY(changed.preferences);
      QCOMPARE(serializeEditorPreferences(*changed.preferences).value("StepType"), QJsonValue(name));
    }
    const auto inert = contentDefaultsFromPreferences(loadEditorPreferences({{"StepType", "FutureStyle9"}}), [](const QString &) { return true; });
    QVERIFY(inert.stepStyleInert); QVERIFY(inert.stepStyle == StepStyle::Numeric);
  }
  void missingFontResolvesToFallbackWithoutRewritingStoredPreference() {
    MemoryStorage storage;
    storage.artifact = {PreferenceRead::Kind::Readable, envelope(QJsonObject{{"TextFontFamily", "Segoe Missing"},
        {"SpeechBalloonFontFamily", "Segoe Missing"}, {"FutureOption", true}}), {}};
    const QByteArray original = storage.artifact.bytes;
    EditorPreferenceStore store(storage);
    QVERIFY(store.diagnostics().isEmpty());
    const auto defaults = contentDefaultsFromPreferences(store.preferences(), [](const QString &) { return false; });
    QVERIFY(defaults.text.familyUnavailable); QVERIFY(defaults.text.fontFamily.isEmpty());
    QCOMPARE(defaults.text.requestedFamily, QStringLiteral("Segoe Missing")); QVERIFY(defaults.balloon.familyUnavailable);
    EditorPreferenceStore again(storage); (void)contentDefaultsFromPreferences(again.preferences(), [](const QString &) { return false; });
    QCOMPARE(storage.artifact.bytes, original); QCOMPARE(storage.replacements, 0);
    // Saving an unrelated field keeps the unavailable family; only a chosen replacement changes it.
    auto unrelated = changeEditorPreferences(store.preferences(), {{"Thickness", 6}});
    QVERIFY(unrelated.preferences); QVERIFY(store.save(*unrelated.preferences).saved);
    QCOMPARE(section(storage, "current").value("TextFontFamily"), QJsonValue("Segoe Missing"));
    auto replacement = changeEditorPreferences(store.preferences(), {{"TextFontFamily", "Replacement"}});
    QVERIFY(replacement.preferences); QVERIFY(store.save(*replacement.preferences).saved);
    QCOMPARE(section(storage, "current").value("TextFontFamily"), QJsonValue("Replacement"));
    QCOMPARE(section(storage, "current").value("SpeechBalloonFontFamily"), QJsonValue("Segoe Missing"));
    QCOMPARE(section(storage, "previous").value("TextFontFamily"), QJsonValue("Segoe Missing"));
    QCOMPARE(section(storage, "current").value("FutureOption"), QJsonValue(true));
  }
  void textDefaultsHonourNewerVersionUnknownAndAtomicFailure() {
    const auto newer = loadEditorPreferences({{"schemaVersion", 2}, {"TextFontSize", 30}});
    QVERIFY(newer.readOnly); QVERIFY(!changeEditorPreferences(newer, {{"TextFontSize", 31}}).preferences);
    MemoryStorage storage;
    storage.artifact = {PreferenceRead::Kind::Readable, envelope(QJsonObject{{"TextFontSize", 30}, {"FutureTextOption", QJsonObject{{"x", 1}}}}), {}};
    EditorPreferenceStore store(storage); const QByteArray original = storage.artifact.bytes;
    auto changed = changeEditorPreferences(store.preferences(), {{"TextFontSize", 33.5}, {"StepType", "UppercaseLetter"}});
    QVERIFY(changed.preferences); storage.failReplace = true;
    QVERIFY(!store.save(*changed.preferences).saved);
    QCOMPARE(storage.artifact.bytes, original); QCOMPARE(store.preferences().values.value("TextFontSize"), QJsonValue(30));
    storage.failReplace = false; QVERIFY(store.save(*changed.preferences).saved);
    QCOMPARE(section(storage, "current").value("TextFontSize"), QJsonValue(33.5));
    QCOMPARE(section(storage, "current").value("FutureTextOption").toObject(), (QJsonObject{{"x", 1}}));
    QCOMPARE(section(storage, "previous").value("TextFontSize"), QJsonValue(30));
    // One invalid text field in a multi-field change rejects the whole change.
    const auto rejected = changeEditorPreferences(store.preferences(), {{"TextBold", false}, {"TextFontSize", 2001}});
    QVERIFY(!rejected.preferences); QCOMPARE(rejected.diagnostics, QStringList{"invalid-TextFontSize"});
  }
  void newerAndMalformedVersionsNeverDowngrade() {
    for (const auto &value : {QJsonValue(2), QJsonValue("1"), QJsonValue(0), QJsonValue(1.5)}) {
      const auto loaded = loadEditorPreferences({{"schemaVersion", value}, {"Theme", "Light"}});
      QVERIFY(loaded.readOnly); QVERIFY(!changeEditorPreferences(loaded, {{"Theme", "Dark"}}).preferences);
      QCOMPARE(loaded.fields.value("schemaVersion"), value == QJsonValue(2) ?
          PreferenceFieldState::NewerVersion : PreferenceFieldState::Invalid);
    }
    const auto legacy = loadEditorPreferences({{"Theme", "Light"}});
    QCOMPARE(legacy.fields.value("schemaVersion"), PreferenceFieldState::Missing);
    QCOMPARE(serializeEditorPreferences(legacy).value("schemaVersion"), QJsonValue(1));
  }
  void recentLoadKeepsOptInOrderingAndPlatformRules() {
    const QJsonArray paths{"/private/newest.png", "/private/oldest.png", "/private/newest.png"};
    auto loaded = loadEditorPreferences({{"RememberRecentImagePaths", true}, {"MaxRecentImageFiles", 2}, {"RecentImageFiles", paths}});
    QCOMPARE(loaded.recent.paths(), QStringList({"/private/newest.png", "/private/oldest.png"}));
    auto denied = loadEditorPreferences({{"RecentImageFiles", paths}});
    QVERIFY(denied.recent.paths().isEmpty());
    QCOMPARE(denied.diagnostics, QStringList{"recent-paths-ignored-without-opt-in"});
    const auto windows = loadEditorPreferences({{"RememberRecentImagePaths", true},
        {"RecentImageFiles", QJsonArray{"C:\\Images\\One.png", "c:\\images\\ONE.png"}}}, RecentPathRules::Windows);
    QCOMPARE(windows.recent.paths().size(), 1);
    const auto changed = changeEditorPreferences(windows, {{"Theme", "Light"}});
    QVERIFY(changed.preferences); QCOMPARE(changed.preferences->recent.pathRules(), RecentPathRules::Windows);
    QCOMPARE(changed.preferences->recent.paths().size(), 1);
    QVERIFY(loaded.recent.remember("/private/third.png"));
    QCOMPARE(serializeEditorPreferences(loaded).value("RecentImageFiles").toArray(),
             QJsonArray({"/private/third.png", "/private/newest.png"}));
  }
  void malformedRecentCollectionsAreIsolated() {
    for (const auto &value : {QJsonValue("/private/file.png"), QJsonValue(QJsonArray{"relative.png"}),
                             QJsonValue(QJsonArray{42}), QJsonValue(QJsonArray{"/one.png", "/two.png"})}) {
      const auto loaded = loadEditorPreferences({{"RememberRecentImagePaths", true}, {"MaxRecentImageFiles", 1},
          {"RecentImageFiles", value}, {"Thickness", 8}});
      QVERIFY(loaded.recent.paths().isEmpty()); QVERIFY(loaded.recent.enabled());
      QCOMPARE(loaded.values.value("Thickness").toInt(), 8);
      QCOMPARE(loaded.fields.value("RecentImageFiles"), PreferenceFieldState::Invalid);
      QCOMPARE(loaded.diagnostics, QStringList{"invalid-RecentImageFiles"});
    }
  }
  void missingReadDoesNotCreateOrWriteAnything() {
    MemoryStorage storage; EditorPreferenceStore store(storage);
    QVERIFY(!store.preferences().readOnly); QVERIFY(!store.preferences().recent.enabled());
    QCOMPARE(storage.replacements, 0); QCOMPARE(storage.artifact.kind, PreferenceRead::Kind::Missing);
    QVERIFY(store.diagnostics().isEmpty());
  }
  void failedAtomicReplacePreservesBothVersionsAndPublishedState() {
    MemoryStorage storage;
    storage.artifact = {PreferenceRead::Kind::Readable, envelope(QJsonObject{{"Theme", "Light"}, {"FutureOption", true}}), {}};
    EditorPreferenceStore store(storage); const QByteArray original = storage.artifact.bytes;
    const auto changed = changeEditorPreferences(store.preferences(), {{"Theme", "Dark"}});
    QVERIFY(changed.preferences); storage.failReplace = true;
    const auto written = store.save(*changed.preferences); QVERIFY(!written.saved);
    QCOMPARE(storage.artifact.bytes, original); QCOMPARE(store.preferences().values.value("Theme"), QJsonValue("Light"));
    storage.failReplace = false; QVERIFY(store.save(*changed.preferences).saved);
    QCOMPARE(section(storage, "current").value("Theme"), QJsonValue("Dark"));
    QCOMPARE(section(storage, "previous").value("Theme"), QJsonValue("Light"));
    QCOMPARE(section(storage, "current").value("FutureOption"), QJsonValue(true));
    EditorPreferenceStore restarted(storage); QCOMPARE(restarted.preferences().values.value("Theme"), QJsonValue("Dark"));
  }
  void removedPrivatePathsNeverReappearThroughRecovery_data() {
    QTest::addColumn<QString>("operation");
    for (const auto *operation : {"remove", "clear", "disable", "trim"}) QTest::newRow(operation) << QString(operation);
  }
  void removedPrivatePathsNeverReappearThroughRecovery() {
    QFETCH(QString, operation);
    MemoryStorage storage; EditorPreferenceStore store(storage);
    auto original = store.preferences(); QVERIFY(original.recent.setPreferences(true, 3));
    for (const auto &path : {"/private/oldest.png", "/private/middle.png", "/private/newest.png"}) QVERIFY(original.recent.remember(path));
    QVERIFY(store.save(original).saved);
    auto next = store.preferences();
    if (operation == u"remove") QVERIFY(next.recent.remove("/private/middle.png"));
    else if (operation == u"clear") next.recent.clear();
    else if (operation == u"disable") QVERIFY(next.recent.setPreferences(false, 3));
    else QVERIFY(next.recent.setPreferences(true, 1));
    QVERIFY(store.save(next).saved);
    const auto previous = section(storage, "previous");
    const auto previousPaths = previous.value("RecentImageFiles").toArray();
    for (const auto &path : previousPaths) QVERIFY(next.recent.paths().contains(path.toString()));
    if (operation == u"disable") QVERIFY(!previous.value("RememberRecentImagePaths").toBool());
    // The current record becomes unreadable, but the atomic envelope survives.
    storage.artifact.bytes = envelope(QJsonValue("corrupt current record"), previous);
    EditorPreferenceStore recovered(storage); QVERIFY(!recovered.preferences().readOnly);
    QCOMPARE(recovered.diagnostics(), QStringList{"recovered-previous-settings"});
    for (const auto &path : recovered.preferences().recent.paths()) QVERIFY(next.recent.paths().contains(path));
    if (operation == u"clear" || operation == u"disable") QVERIFY(recovered.preferences().recent.paths().isEmpty());
  }
  void compatibleEnvelopeExtensionsSurviveSaving() {
    MemoryStorage storage;
    auto doc = QJsonDocument::fromJson(envelope(QJsonObject{{"Theme", "Light"}})).object();
    const QJsonObject extension{{"future", QJsonArray{true, 1}}}; doc.insert("futureEnvelope", extension);
    storage.artifact = {PreferenceRead::Kind::Readable, QJsonDocument(doc).toJson(), {}};
    EditorPreferenceStore store(storage);
    const auto changed = changeEditorPreferences(store.preferences(), {{"Thickness", 8}});
    QVERIFY(changed.preferences); QVERIFY(store.save(*changed.preferences).saved);
    QCOMPARE(QJsonDocument::fromJson(storage.artifact.bytes).object().value("futureEnvelope").toObject(), extension);
  }
  void unreadableNewerAndOversizedArtifactsArePreserved() {
    const QByteArray newerEnvelope = "{\"format\":\"com.xerahs.app/linux-qt/editor-settings\",\"version\":2,\"current\":{}}";
    for (const auto &bytes : {QByteArray("{broken"), newerEnvelope,
                             envelope(QJsonObject{{"schemaVersion", 2}}, QJsonObject{{"Theme", "Light"}}),
                             QByteArray(maximumPreferenceBytes + 1, 'x')}) {
      MemoryStorage storage; storage.artifact = {PreferenceRead::Kind::Readable, bytes, {}};
      EditorPreferenceStore store(storage); QVERIFY(store.preferences().readOnly);
      QVERIFY(!store.diagnostics().isEmpty()); QVERIFY(!store.save(loadEditorPreferences({})).saved);
      QCOMPARE(storage.artifact.bytes, bytes); QCOMPARE(storage.replacements, 0);
    }
    MemoryStorage storage; storage.artifact = {PreferenceRead::Kind::Failed, {}, "settings-read-failed"};
    EditorPreferenceStore store(storage); QVERIFY(store.preferences().readOnly);
    QCOMPARE(store.diagnostics(), QStringList{"settings-read-failed"});
  }
  void oversizedWriteNeverReachesStorage() {
    MemoryStorage storage; EditorPreferenceStore store(storage);
    auto next = store.preferences(); next.values.insert("FutureOption", QString(maximumPreferenceBytes, QLatin1Char('x')));
    const auto written = store.save(next); QVERIFY(!written.saved); QCOMPARE(written.diagnostic, QStringLiteral("settings-too-large"));
    QCOMPARE(storage.replacements, 0); QCOMPARE(storage.artifact.kind, PreferenceRead::Kind::Missing);
  }
  void nativeFileStorageHasPrivateAtomicRestartAndExternalChangeGuards() {
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    const QString path = temporary.filePath("private/settings.json");
    FilePreferenceStorage storage(path); EditorPreferenceStore store(storage);
    QVERIFY(!QFileInfo::exists(path));
    const auto next = changeEditorPreferences(store.preferences(), {{"Theme", "Light"}}); QVERIFY(next.preferences);
    QVERIFY(store.save(*next.preferences).saved);
    QCOMPARE(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
    QCOMPARE(QFileInfo(QFileInfo(path).absolutePath()).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther), QFileDevice::Permissions{});
    FilePreferenceStorage reopened(path); EditorPreferenceStore restarted(reopened);
    QCOMPARE(restarted.preferences().values.value("Theme"), QJsonValue("Light"));
    QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
    const auto change = changeEditorPreferences(store.preferences(), {{"Theme", "Dark"}}); QVERIFY(change.preferences);
    const auto locked = store.save(*change.preferences); QVERIFY(!locked.saved); QCOMPARE(locked.diagnostic, QStringLiteral("settings-locked")); lock.unlock();
    const QByteArray external = envelope(QJsonObject{{"Theme", "System"}}); QVERIFY(privateWrite(path, external));
    const auto raced = store.save(*change.preferences); QVERIFY(!raced.saved); QCOMPARE(raced.diagnostic, QStringLiteral("settings-changed-externally"));
    QCOMPARE(reopened.read().bytes, external); QCOMPARE(store.preferences().values.value("Theme"), QJsonValue("Light"));
  }
  void nativeFileStorageRejectsUnsafeAndOversizedInputBeforeBuffering() {
    QTemporaryDir temporary; QVERIFY(temporary.isValid());
    const QString target = temporary.filePath("settings.json"); QVERIFY(privateWrite(target, envelope(QJsonObject{})));
    const QString alias = temporary.filePath("alias.json"); QVERIFY(QFile::link(target, alias));
    FilePreferenceStorage symlink(alias); QCOMPARE(symlink.read().diagnostic, QStringLiteral("settings-unsafe-path"));
    QVERIFY(!symlink.replace({}, "replacement").saved);
    FilePreferenceStorage relative("relative-settings.json"); QCOMPARE(relative.read().diagnostic, QStringLiteral("settings-unsafe-path"));
    QFile file(target); QVERIFY(file.open(QIODevice::ReadWrite)); QVERIFY(file.resize(maximumPreferenceBytes + 1)); file.close();
    FilePreferenceStorage oversized(target); const auto loaded = oversized.read();
    QCOMPARE(loaded.kind, PreferenceRead::Kind::Failed); QCOMPARE(loaded.diagnostic, QStringLiteral("settings-too-large")); QVERIFY(loaded.bytes.isEmpty());
    QVERIFY(QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
    QCOMPARE(oversized.read().diagnostic, QStringLiteral("settings-private-storage-required"));
  }
};
QTEST_GUILESS_MAIN(EditorPreferencesTest)
#include "EditorPreferencesTest.moc"
