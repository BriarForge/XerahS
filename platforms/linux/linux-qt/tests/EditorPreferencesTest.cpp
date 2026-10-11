#include "image-editor/EditorPreferences.h"
#include "image-editor/EditorPreferenceStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
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
