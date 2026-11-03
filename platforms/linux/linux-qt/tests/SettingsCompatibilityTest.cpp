#include "image-editor/EditorPreferenceStore.h"
#include "settings/ApplicationSettings.h"
#include "actions/PipelineJournal.h"
#include "uploads/UploadDestination.h"
#include "workflows/CaptureWorkflows.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs;
using D = compatibility::ImportDisposition;
namespace {
QByteArray editorEnvelope(const QJsonObject &current, const QJsonObject &extra = {}) {
  auto root = extra; root.insert("format", "com.xerahs.app/linux-qt/editor-settings");
  root.insert("version", 1); root.insert("current", current);
  return QJsonDocument(root).toJson();
}
QByteArray appEnvelope(const QJsonObject &values, const QJsonObject &extra = {}) {
  auto root = extra; root.insert("format", "com.xerahs.app/linux-qt/application-settings");
  root.insert("version", 1); root.insert("values", values);
  return QJsonDocument(root).toJson();
}
bool put(const QString &path, const QByteArray &bytes) {
  QFile file(path); return file.open(QIODevice::WriteOnly) &&
      file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
}
class SettingsCompatibilityTest : public QObject {
  Q_OBJECT
private slots:
  void preferencesReportEveryDispositionWithoutValues() {
    const QJsonObject input{{"Thickness", 8}, {"BlurStrength", "private-invalid-value"},
      {"FillColorHex", "#abcdef"}, {"BorderColor", "computed"},
      {"FutureLabel", QJsonObject{{"note", "private-safe-value"}}},
      {"RecentImageFiles", QJsonArray{"/private/not-applied.png"}},
      {"ToolbarItems", QJsonArray{QJsonObject{{"Id", "future-tool"}, {"Hotkey", "opaque-inactive"}}}}};
    QTemporaryDir dir; const auto path = dir.filePath("editor.json"); QVERIFY(put(path, editorEnvelope(input)));
    editor::FilePreferenceStorage storage(path); editor::EditorPreferenceStore store(storage);
    QVERIFY(!store.preferences().readOnly);
    const auto &report = store.importReport();
    for (auto d : {D::Retained, D::Transformed, D::Defaulted, D::Quarantined, D::Rejected, D::Ignored}) QVERIFY(report.count(d) > 0);
    QVERIFY(!report.summary().contains("private")); QVERIFY(!report.summary().contains("FutureLabel"));
    QCOMPARE(store.preferences().values.value("BlurStrength"), QJsonValue(30));
    QVERIFY(store.preferences().recent.paths().isEmpty());
    const auto toolbar = store.preferences().values.value("ToolbarItems").toArray();
    QCOMPARE(toolbar.first().toObject().value("Hotkey"), QJsonValue("opaque-inactive"));
  }
  void settingsReportRepairsAndTransforms() {
    auto values = settings::defaultSettings("/tmp"); values.remove("showTrayIcon");
    values.insert("playCaptureSound", "private-invalid"); values.insert("legacy.option", 9);
    auto hotkeys = values.value("hotkeys").toObject(); hotkeys.insert("capture-region", "Shift+Ctrl+F1"); values.insert("hotkeys", hotkeys);
    const auto read = settings::repairSettings(values, "/tmp", {"legacy.option"});
    QVERIFY(read.writable);
    for (auto d : {D::Retained, D::Transformed, D::Defaulted, D::Quarantined, D::Rejected}) QVERIFY(read.report.count(d) > 0);
    QVERIFY(!read.report.summary().contains("private"));
    QCOMPARE(read.values.value("hotkeys").toObject().value("capture-region"), QJsonValue("Ctrl+Shift+F1"));
  }
  void safeExtensionsRoundTripThroughBothStores() {
    QTemporaryDir dir; const auto editorPath = dir.filePath("editor.json"), appPath = dir.filePath("app.json");
    const QJsonObject opaque{{"note", QJsonArray{"unchanged", 42}}};
    QVERIFY(put(editorPath, editorEnvelope({{"FutureLabel", opaque}}, {{"extension", opaque}})));
    editor::FilePreferenceStorage storage(editorPath); editor::EditorPreferenceStore editorStore(storage);
    auto changed = editor::changeEditorPreferences(editorStore.preferences(), {{"Thickness", 9}});
    QVERIFY(changed.preferences); const auto saved = editorStore.save(*changed.preferences); QVERIFY(saved.saved);
    QCOMPARE(saved.report.target, QString("com.xerahs.app/linux-qt/editor-settings")); QCOMPARE(saved.report.version, 1);
    editor::EditorPreferenceStore editorRestart(storage);
    QCOMPARE(editorRestart.preferences().values.value("FutureLabel"), QJsonValue(opaque));
    QCOMPARE(QJsonDocument::fromJson(get(editorPath)).object().value("extension"), QJsonValue(opaque));
    QVERIFY(put(appPath, appEnvelope({{"FutureLabel", opaque}}, {{"extension", opaque}})));
    settings::ApplicationSettingsStore appStore(appPath, dir.path()); auto next = appStore.current().values;
    next.insert("showTrayIcon", false); compatibility::ExportReport report; QVERIFY(appStore.save(next, &report).isEmpty());
    QCOMPARE(report.target, QString("com.xerahs.app/linux-qt/application-settings")); QCOMPARE(report.version, 1);
    QCOMPARE(report.omissions, QStringList{"profile-local-save-folder"});
    settings::ApplicationSettingsStore appRestart(appPath, dir.path());
    QCOMPARE(appRestart.current().values.value("FutureLabel"), QJsonValue(opaque));
    QCOMPARE(QJsonDocument::fromJson(get(appPath)).object().value("extension"), QJsonValue(opaque));
  }
  void editorPreferenceSaveReportsProfileLocalOmissions() {
    QTemporaryDir dir; const auto path = dir.filePath("editor.json"); editor::FilePreferenceStorage storage(path); editor::EditorPreferenceStore store(storage);
    auto changed = editor::changeEditorPreferences(store.preferences(), {{"RememberWindowState", true}});
    QVERIFY(changed.preferences);
    auto saved = store.save(*changed.preferences); QVERIFY(saved.saved);
    QVERIFY(saved.report.omissions.contains("profile-local-window-state"));
    auto recent = *changed.preferences; QVERIFY(recent.recent.setPreferences(true, 10)); QVERIFY(recent.recent.remember("/home/someone/a.png"));
    saved = store.save(recent); QVERIFY(saved.saved); QVERIFY(saved.report.omissions.contains("profile-local-recent-paths"));
    for (const auto &code : saved.report.omissions) QVERIFY(!code.contains("someone"));
  }
  void unsafeExtensionsBlockBothStoresWithoutMutatingOrReportingValues() {
    for (const auto *key : {"password", "trustedConsent", "unknownAuthentication", "unknownArguments", "$ref", "criticalFeature", "assetPath"}) {
      QTemporaryDir dir; const auto editorPath = dir.filePath("editor.json"), appPath = dir.filePath("app.json");
      const QJsonObject unsafe{{"Future", QJsonObject{{key, "../private-secret-value"}}}, {"Thickness", 9}};
      const auto editorBytes = editorEnvelope(unsafe); QVERIFY(put(editorPath, editorBytes));
      editor::FilePreferenceStorage storage(editorPath); editor::EditorPreferenceStore editorStore(storage);
      QVERIFY(editorStore.preferences().readOnly); QVERIFY(editorStore.importReport().count(D::Rejected) > 0);
      QCOMPARE(editorStore.preferences().values.value("Thickness"), QJsonValue(4));
      QVERIFY(!editorStore.preferences().values.contains("Future")); QVERIFY(!editorStore.save(editor::loadEditorPreferences({})).saved);
      QCOMPARE(get(editorPath), editorBytes); QVERIFY(!editorStore.diagnostics().join(' ').contains("private"));
      const auto appBytes = appEnvelope(unsafe); QVERIFY(put(appPath, appBytes));
      settings::ApplicationSettingsStore appStore(appPath, dir.path()); QVERIFY(!appStore.current().writable);
      QVERIFY(appStore.current().report.count(D::Rejected) > 0); QVERIFY(!appStore.current().values.contains("Future"));
      QVERIFY(!appStore.save(settings::defaultSettings(dir.path())).isEmpty()); QCOMPARE(get(appPath), appBytes);
      QVERIFY(!appStore.current().diagnostics.join(' ').contains("private"));
      QCOMPARE(appStore.current().diagnostics, QStringList{"settings-unsafe-unknown"});
      QVERIFY(put(appPath, appEnvelope({}, unsafe))); settings::ApplicationSettingsStore envelopeStore(appPath, dir.path());
      QVERIFY(!envelopeStore.current().writable);
      QVERIFY(put(editorPath, editorEnvelope({}, unsafe))); editor::EditorPreferenceStore envelopeEditor(storage);
      QVERIFY(envelopeEditor.preferences().readOnly);
    }
  }
  void structurallyInvalidRecoverySnapshotsAreNotSilentlyDiscarded() {
    QTemporaryDir dir; const auto editorPath = dir.filePath("editor.json"), appPath = dir.filePath("app.json");
    const QJsonObject extra{{"previous", "private-invalid-record"}};
    const auto editorBytes = editorEnvelope({}, extra), appBytes = appEnvelope({}, extra);
    QVERIFY(put(editorPath, editorBytes)); QVERIFY(put(appPath, appBytes));
    editor::FilePreferenceStorage storage(editorPath); editor::EditorPreferenceStore preferences(storage);
    settings::ApplicationSettingsStore appStore(appPath, dir.path());
    QVERIFY(preferences.preferences().readOnly); QVERIFY(!appStore.current().writable);
    QVERIFY(preferences.importReport().count(D::Rejected) > 0); QVERIFY(appStore.current().report.count(D::Rejected) > 0);
    QVERIFY(!preferences.save(editor::loadEditorPreferences({})).saved);
    QVERIFY(!appStore.save(settings::defaultSettings(dir.path())).isEmpty());
    QCOMPARE(get(editorPath), editorBytes); QCOMPARE(get(appPath), appBytes);
    QVERIFY(!preferences.diagnostics().join(' ').contains("private"));
    QVERIFY(!appStore.current().diagnostics.join(' ').contains("private"));
  }
  void readerAdversarialMatrix_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("code");
    QTest::newRow("malformed") << QByteArray("{broken") << QString("settings-invalid-json");
    QTest::newRow("duplicates") << QByteArray("{\"version\":1,\"version\":1}") << QString("settings-duplicate-key");
    QTest::newRow("invalid-utf8") << QByteArray("{\"a\":\"") + QByteArray(1, char(0xff)) + "\"}" << QString("settings-invalid-encoding");
    QTest::newRow("depth") << QByteArray(65, '[') + "0" + QByteArray(65, ']') << QString("settings-too-deep");
    QByteArray elements("["); for (int i = 0; i < 100001; ++i) elements += "0,"; elements += "0]";
    QTest::newRow("elements") << elements << QString("settings-too-many-elements");
    QTest::newRow("bytes") << QByteArray(1048577, ' ') << QString("settings-too-large");
    QTest::newRow("compressed-not-supported") << QByteArray::fromHex("1f8b0800000000000003") << QString("settings-invalid-encoding");
  }
  void readerAdversarialMatrix() {
    QFETCH(QByteArray, bytes); QFETCH(QString, code);
    QTemporaryDir dir; const auto editorPath = dir.filePath("editor.json"), appPath = dir.filePath("app.json");
    QVERIFY(put(editorPath, bytes)); editor::FilePreferenceStorage storage(editorPath); editor::EditorPreferenceStore editorStore(storage);
    QVERIFY(editorStore.preferences().readOnly); QCOMPARE(editorStore.diagnostics(), QStringList{code});
    QVERIFY(editorStore.importReport().count(D::Rejected) > 0); QCOMPARE(get(editorPath), bytes);
    QVERIFY(put(appPath, bytes)); settings::ApplicationSettingsStore appStore(appPath, dir.path());
    QVERIFY(!appStore.current().writable); QCOMPARE(appStore.current().diagnostics, QStringList{code});
    QVERIFY(appStore.current().report.count(D::Rejected) > 0); QCOMPARE(get(appPath), bytes);
  }
  void nativeStoreReadersShareTheLexicalBoundary() {
    const QByteArray duplicate("{\"version\":1,\"version\":1}"), deep = QByteArray(65, '[') + "0" + QByteArray(65, ']'), badUtf8 = QByteArray("{\"a\":\"") + char(0xff) + "\"}";
    for (const auto &bytes : {duplicate, deep, badUtf8}) {
      QVERIFY(!workflows::parseCaptureWorkflows(bytes).catalog);
      QVERIFY(!actions::parsePipelineJournal(bytes, "00000000-0000-4000-8000-000000000001").record);
      QVERIFY(!uploads::readDestination(bytes).destination);
    }
  }
  void embeddedPathsAreRefusedWithoutOpeningThem() {
    auto preferences = editor::loadEditorPreferences({{"RememberRecentImagePaths", true}, {"ExtensionAsset", QJsonObject{{"assetPath", "/private/../secret.png"}}}});
    QVERIFY(preferences.readOnly); QCOMPARE(preferences.diagnostics, QStringList{"settings-path-traversal"});
    QVERIFY(preferences.report.count(D::Rejected) > 0);
    const auto kept = editor::loadEditorPreferences({{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{"/linked/../one.png"}}});
    QVERIFY(!kept.readOnly); QCOMPARE(kept.recent.paths(), QStringList{"/linked/../one.png"});  // kernel meaning preserved, never opened
    const auto app = settings::repairSettings({{"saveFolder", "/private/../secret"}}, "/tmp");
    QCOMPARE(app.values.value("saveFolder"), QJsonValue("/tmp")); QVERIFY(app.report.count(D::Rejected) > 0);
    QCOMPARE(app.diagnostics, QStringList{"saveFolder: invalid value repaired"});
  }
  void localeNewlineAndFontNormalizationFixtures() {
    struct Restore { QLocale locale; ~Restore() { QLocale::setDefault(locale); } } restore;
    QJsonObject expected;
    for (const auto &locale : {QLocale(QLocale::German), QLocale(QLocale::Arabic), QLocale(QLocale::Turkish)})
      for (bool crlf : {false, true}) for (bool composed : {false, true}) {
        QLocale::setDefault(locale); QTemporaryDir dir; const auto path = dir.filePath("editor.json");
        auto bytes = editorEnvelope({{"TextFontFamily", composed ? QString::fromUtf8("Café") : QString::fromUtf8("Cafe\xcc\x81")}, {"BlurStrength", 12.5}});
        if (crlf) bytes.replace("\n", "\r\n"); QVERIFY(put(path, bytes));
        editor::FilePreferenceStorage storage(path); editor::EditorPreferenceStore store(storage); QVERIFY(!store.preferences().readOnly);
        const auto canonical = editor::serializeEditorPreferences(store.preferences());
        if (expected.isEmpty()) expected = canonical; else QCOMPARE(canonical, expected);
        QVERIFY(store.save(store.preferences()).saved); const auto written = get(path);
        QVERIFY(!written.contains('\r')); QVERIFY(written.contains("12.5"));
        QCOMPARE(canonical.value("TextFontFamily").toString(), QString::fromUtf8("Café"));
        const auto appPath = dir.filePath("app.json"); auto appBytes = appEnvelope({{"autoCapture.intervalMs", 1234}});
        if (crlf) appBytes.replace("\n", "\r\n"); QVERIFY(put(appPath, appBytes));
        settings::ApplicationSettingsStore appStore(appPath, "/tmp"); QVERIFY(appStore.current().writable);
        QCOMPARE(appStore.current().values.value("autoCapture.intervalMs"), QJsonValue(1234));
      }
  }
};
QTEST_GUILESS_MAIN(SettingsCompatibilityTest)
#include "SettingsCompatibilityTest.moc"
