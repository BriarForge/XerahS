#include "image-editor/ToolbarPreferences.h"
#include "image-editor/EditorPreferences.h"
#include "image-editor/EditorPreferenceStore.h"
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::editor;
namespace {
QJsonObject item(QString id, QString key = {}, bool visible = true, QJsonValue group = QJsonValue::Null) {
  return {{"Id", id}, {"IsVisible", visible}, {"BeginGroup", group}, {"Hotkey", key}};
}
}
class ToolbarPreferencesTest : public QObject {
  Q_OBJECT
private slots:
  void defaultsAndExplicitEmptyRemainDistinct() {
    const auto defaults = loadEditorPreferences({});
    const auto placements = effectiveToolbarItems(defaults.values.value("ToolbarItems").toArray());
    QStringList visible;
    for (const auto &placement : placements) if (placement.visible) visible.append(placement.id);
    QCOMPARE(visible, QStringList({"save", "save-as", "undo", "redo", "delete-selection", "rotate-selection",
        "stroke-colour", "fill-colour", "increase-stroke", "decrease-stroke", "zoom-in", "zoom-out", "reset-zoom", "fit-image"}));
    QCOMPARE(defaults.fields.value("ToolbarItems"), PreferenceFieldState::Missing);
    auto empty = loadEditorPreferences({{"ToolbarItems", QJsonArray{}}});
    QVERIFY(empty.diagnostics.isEmpty()); QVERIFY(effectiveToolbarItems(empty.values.value("ToolbarItems").toArray()).isEmpty());
    QVERIFY(serializeEditorPreferences(empty).value("ToolbarItems").toArray().isEmpty());
    const auto nullable = validateToolbarItems(QJsonArray{item("undo"), item("redo", "", true, false), item("save", "", false, true)});
    QVERIFY(nullable.valid); const auto effective = effectiveToolbarItems(nullable.items);
    QVERIFY(effective[0].beginGroup && !effective[1].beginGroup && effective[2].beginGroup && !effective[2].visible);
  }
  void canonicalGesturesRejectAmbiguousOrMalformedText() {
    for (const auto &example : {std::pair{"shift+ctrl+j", "Ctrl+Shift+J"}, {"alt+ctrl+F6", "Ctrl+Alt+F6"},
        {"Ctrl++", "Ctrl+Plus"}, {"Ctrl+Shift+Plus", "Ctrl+Plus"}, {"Ctrl+=", "Ctrl+Equal"}, {"f35", "F35"}, {"Shift+,", "Shift+Comma"}}) {
      const auto key = parseToolbarHotkey(QString::fromLatin1(example.first)); QVERIFY(key);
      QCOMPARE(key->text, QString::fromLatin1(example.second));
    }
    for (const auto *text : {" Ctrl+J", "Ctrl+J ", "Ctrl+Ctrl+J", "Ctrl+", "Ctrl+++",
        "Ctrl+J,Ctrl+K", "F0", "F36", "AltGr+J", "Ctrl+É", "Ctrl+Unknown", "Control+J"}) QVERIFY(!parseToolbarHotkey(QString::fromUtf8(text)));
    QVERIFY(parseToolbarHotkey("")); QVERIFY(parseToolbarHotkey("Ctrl+Alt+J"));
  }
  void protectedCatalogueAndNativeReservedKeysCannotBeReassigned() {
    for (const auto *key : {"Ctrl+S", "Ctrl+Shift+S", "Ctrl+Z", "Ctrl+Y", "Ctrl+Shift+Z", "Ctrl+N", "Ctrl+O",
        "Ctrl+C", "Ctrl+V", "Ctrl+P", "Ctrl+U", "Ctrl+H", "Shift+Delete", "Home", "PageUp", "Ctrl+9", "Ctrl+W"})
      QCOMPARE(toolbarHotkeyDiagnostic(*parseToolbarHotkey(key), "crop-image"), QString("toolbar-protected-hotkey"));
    for (const auto *key : {"Escape", "Enter", "Return", "Tab", "Shift+Tab", "Space", "Ctrl+Left", "Alt+Right",
        "Backspace", "Ctrl+Delete", "R", "Shift+R", "Ctrl+BracketLeft", "Alt+F4", "Ctrl+Q", "Meta+J", "Ctrl+Alt+F2"})
      QCOMPARE(toolbarHotkeyDiagnostic(*parseToolbarHotkey(key), "crop-image"), QString("toolbar-reserved-hotkey"));
    QVERIFY(toolbarHotkeyDiagnostic(*parseToolbarHotkey("Ctrl+S"), "save").isEmpty());
    QVERIFY(knownToolbarAction("copy-rendered-image"));
    for (const auto &entry : {std::pair{"copy-annotation", "Ctrl+Shift+C"}, std::pair{"cut-annotation", "Ctrl+X"}, std::pair{"paste", "Ctrl+V"}, std::pair{"duplicate-selection", "Ctrl+D"}}) {
      QVERIFY(knownToolbarAction(entry.first)); QVERIFY(validateToolbarItems(QJsonArray{item(entry.first, entry.second)}).valid);
    }
    QCOMPARE(toolbarHotkeyDiagnostic(*parseToolbarHotkey("Ctrl+C"), "copy-annotation"), QString("toolbar-protected-hotkey"));
    QVERIFY(knownToolbarAction("print"));
    QVERIFY(validateToolbarItems(QJsonArray{item("print", "Ctrl+Shift+P")}).valid);
    QCOMPARE(toolbarHotkeyDiagnostic(*parseToolbarHotkey("Ctrl+P"), "print"), QString("toolbar-protected-hotkey"));
    QVERIFY(validateToolbarItems(QJsonArray{item("copy-rendered-image", "Ctrl+C")}).valid);
    QCOMPARE(toolbarHotkeyDiagnostic(*parseToolbarHotkey("Ctrl+Shift+C"), "copy-rendered-image"), QString("toolbar-protected-hotkey"));
    QVERIFY(toolbarHotkeyDiagnostic(*parseToolbarHotkey("F6"), "fit-image").isEmpty());
    QVERIFY(toolbarHotkeyDiagnostic(*parseToolbarHotkey("Ctrl+Alt+J"), "crop-image").isEmpty());
  }
  void collectionTypesBoundsDuplicatesAndRecoveryAreIndependent() {
    for (const auto &value : {QJsonValue(true), QJsonValue(QJsonObject{}), QJsonValue(QJsonArray{3}),
        QJsonValue(QJsonArray{item("save"), item("save")}), QJsonValue(QJsonArray{item("save", "F6"), item("undo", "f6")}),
        QJsonValue(QJsonArray{QJsonObject{{"Id", "save"}, {"IsVisible", "true"}}}),
        QJsonValue(QJsonArray{item("save", "", true, 1)}), QJsonValue(QJsonArray{item("save", "Ctrl+O")})}) {
      const auto loaded = loadEditorPreferences({{"ToolbarItems", value}, {"Theme", "Light"}, {"Thickness", 9}});
      QCOMPARE(loaded.fields.value("ToolbarItems"), PreferenceFieldState::Invalid);
      QCOMPARE(loaded.diagnostics, QStringList{"invalid-ToolbarItems"});
      QCOMPARE(loaded.values.value("Theme"), QJsonValue("Light")); QCOMPARE(loaded.values.value("Thickness"), QJsonValue(9));
      QCOMPARE(loaded.values.value("ToolbarItems"), QJsonValue(defaultToolbarItems()));
    }
    QJsonArray bounded;
    for (int i = 0; i < maximumToolbarItems; ++i) bounded.append(item(QString("future-%1").arg(i), "future chord syntax"));
    QVERIFY(validateToolbarItems(bounded).valid); bounded.append(item("one-too-many")); QVERIFY(!validateToolbarItems(bounded).valid);
    QVERIFY(!validateToolbarItems(QJsonArray{item(QString(81, 'a'))}).valid);
    QVERIFY(!validateToolbarItems(QJsonArray{item("save", QString(65, 'x'))}).valid);
    const auto rejected = changeEditorPreferences(loadEditorPreferences({}), {{"ToolbarItems", QJsonArray{item("crop-image", "Ctrl+S")}}, {"Theme", "Light"}});
    QVERIFY(!rejected.preferences); QCOMPARE(rejected.diagnostics, QStringList{"toolbar-protected-hotkey"});
  }
  void futureEntriesAndExtensionFieldsSurviveSaveResetAndRestartWithoutActivation() {
    const QJsonObject future{{"Id", "future-tool"}, {"IsVisible", true}, {"Hotkey", "Escape"},
        {"BeginGroup", true}, {"future-data", QJsonObject{{"a", 7}}}};
    auto save = item("save", "F6", false, true); save.insert("extension", QJsonArray{1, "retained"});
    const QJsonArray original{future, save};
    const auto loaded = loadEditorPreferences({{"ToolbarItems", original}, {"future-pref", 42}});
    QCOMPARE(loaded.diagnostics, QStringList{"toolbar-unknown-action"});
    const auto active = effectiveToolbarItems(loaded.values.value("ToolbarItems").toArray());
    QCOMPARE(active.size(), 1); QCOMPARE(active[0].id, QString("save")); QCOMPARE(active[0].hotkey.text, QString("F6"));
    const auto reset = resetToolbarItems(original);
    QVERIFY(validateToolbarChange(original, reset).isEmpty()); QCOMPARE(reset.at(reset.size() - 1).toObject(), future);
    QCOMPARE(reset[0].toObject().value("extension"), save.value("extension"));
    QVERIFY(!changeEditorPreferences(loaded, {{"ToolbarItems", QJsonArray{save}}}).preferences);
    auto changedFuture = future; changedFuture.insert("IsVisible", false);
    QVERIFY(!changeEditorPreferences(loaded, {{"ToolbarItems", QJsonArray{changedFuture, save}}}).preferences);
    QVERIFY(!changeEditorPreferences(loadEditorPreferences({}), {{"ToolbarItems", QJsonArray{future}}}).preferences);
    QTemporaryDir temporary; FilePreferenceStorage storage(temporary.filePath("native/settings.json"));
    EditorPreferenceStore store(storage); QVERIFY(store.save(loaded).saved);
    auto changed = changeEditorPreferences(store.preferences(), {{"Theme", "Light"}, {"ToolbarItems", QJsonArray{save, future}}});
    QVERIFY(changed.preferences); QVERIFY(store.save(*changed.preferences).saved);
    EditorPreferenceStore restarted(storage);
    QCOMPARE(restarted.preferences().values.value("ToolbarItems"), QJsonValue(QJsonArray{save, future}));
    QCOMPARE(restarted.preferences().values.value("future-pref"), QJsonValue(42));
    QCOMPARE(restarted.diagnostics(), QStringList{"toolbar-unknown-action"});
    QFile file(temporary.filePath("native/settings.json")); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto previous = QJsonDocument::fromJson(file.readAll()).object().value("previous").toObject();
    QCOMPARE(previous.value("ToolbarItems"), QJsonValue(original));
    const auto recovered = loadEditorPreferences({{"ToolbarItems", QJsonArray{future, item("save", "Escape")}}, {"Thickness", 9}});
    QCOMPARE(recovered.fields.value("ToolbarItems"), PreferenceFieldState::Invalid);
    QCOMPARE(recovered.values.value("Thickness"), QJsonValue(9));
    const auto recoveredItems = recovered.values.value("ToolbarItems").toArray();
    QCOMPARE(recoveredItems.at(recoveredItems.size() - 1).toObject(), future);
    QVERIFY(validateToolbarItems(recoveredItems).valid);
    QJsonArray nearLimit;
    for (int i = 0; i < maximumToolbarItems - 1; ++i) nearLimit.append(item(QString("future-%1").arg(i)));
    nearLimit.append(item("save", "Escape"));
    const auto repaired = loadEditorPreferences({{"ToolbarItems", nearLimit}}).values.value("ToolbarItems").toArray();
    QCOMPARE(repaired.size(), maximumToolbarItems); QVERIFY(validateToolbarItems(repaired).valid);
    QCOMPARE(repaired.at(1), nearLimit.at(0)); QCOMPARE(repaired.at(repaired.size() - 1), nearLimit.at(nearLimit.size() - 2));
    const auto boundedReset = resetToolbarItems(repaired);
    QCOMPARE(boundedReset, repaired);
  }
};
QTEST_GUILESS_MAIN(ToolbarPreferencesTest)
#include "ToolbarPreferencesTest.moc"
