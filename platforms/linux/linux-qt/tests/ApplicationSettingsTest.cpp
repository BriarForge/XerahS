#include "settings/ApplicationSettings.h"
#include "workflows/WorkflowShortcuts.h"
#include "XdgAutostart.h"
#include "SettingsDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
using namespace xerahs::settings;
class ApplicationSettingsTest : public QObject {
  Q_OBJECT
private slots:
  void oldMaximumHotkeysSurviveNewRecordingAction() {
    auto old = defaultSettings("/tmp"); auto bindings = old.value("hotkeys").toObject(); bindings.remove("start-region-recording");
    while (bindings.size() < maximumHotkeyBindings - 1) bindings.insert(QUuid::createUuid().toString(QUuid::WithoutBraces), "");
    old.insert("hotkeys", bindings);
    const auto repaired = repairSettings(old, "/tmp"); QVERIFY(repaired.diagnostics.isEmpty());
    QCOMPARE(repaired.values.value("hotkeys").toObject().size(), maximumHotkeyBindings);
    QCOMPARE(repairSettings(repaired.values, "/tmp").values, repaired.values);
    QVERIFY(hotkeys(repaired.values).validate().ok);
  }
  void audioSettingsCompatibleRoundTrip() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    auto old = defaultSettings(dir.path()); old.remove("systemAudioDevice"); old.remove("microphoneDevice");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", old}}).toJson()); file.close();
    ApplicationSettingsStore store(path, dir.path()); QVERIFY(store.current().diagnostics.isEmpty());
    QCOMPARE(store.current().values.value("systemAudioDevice").toString(), QString{});
    QCOMPARE(store.current().values.value("microphoneDevice").toString(), QString{});
    auto values = store.current().values; values.insert("systemAudioDevice", "device.name:speakers.monitor"); values.insert("microphoneDevice", "node.name:mic");
    QVERIFY(store.save(values).isEmpty()); ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.current().values, values);
    const auto reset = resetSection(values, Section::Recording, dir.path());
    QCOMPARE(reset.value("systemAudioDevice").toString(), QString{}); QCOMPARE(reset.value("microphoneDevice").toString(), QString{});
    QCOMPARE(reset.value("saveFolder"), values.value("saveFolder"));
    values.insert("microphoneDevice", false); QVERIFY(!repairSettings(values, dir.path()).diagnostics.isEmpty());
  }
  void portableBindings() {
    auto bindings = HotkeyBindings::defaults(); QVERIFY(bindings.validate().ok);
    QCOMPARE(bindings.entries.value("capture-region"), "Print");
    QCOMPARE(normalizeTrigger("Shift+Ctrl+F1"), "Ctrl+Shift+F1");
    for (const auto &invalid : {"Ctrl+Ctrl+Print", "Super+Print", "Ctrl+", "0x1234", "Ctrl+F36"})
      QVERIFY(!bindings.assign("capture-region", invalid).ok);
    QVERIFY(!bindings.assign("unstable", "F1").ok);
    QVERIFY(!bindings.assign("capture-region", "Alt+F4").ok);
    QCOMPARE(bindings.entries.value("capture-region"), "Print");
    QVERIFY(bindings.assign("capture-region", "").ok);
  }
  void collisionsAcrossWorkflowAndBuiltin() {
    auto bindings = HotkeyBindings::defaults(); const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto conflict = bindings.assign(id, "Print"); QVERIFY(!conflict.ok);
    QCOMPARE(conflict.conflictingAction, "capture-region"); QVERIFY(!bindings.entries.contains(id));
    QVERIFY(bindings.assign(id, "F1").ok);
    const auto reverse = bindings.assign("capture-region", "F1"); QVERIFY(!reverse.ok); QCOMPARE(reverse.conflictingAction, id);
    QCOMPARE(bindings.entries.value("capture-region"), "Print");
  }
  void repairsAreFieldSpecific() {
    auto input = defaultSettings("/tmp/captures");
    input.insert("saveFolder", "../bad"); input.insert("filenamePattern", "%rf{private}"); input.insert("showTrayIcon", "false");
    input.insert("showNotification", false); input.insert("future", QJsonObject{{"kept", true}});
    auto bindings = input.value("hotkeys").toObject(); bindings.insert("capture-region", "Alt+F4"); bindings.insert("future-action", QJsonObject{{"future", 1}}); input.insert("hotkeys", bindings);
    const auto repaired = repairSettings(input, "/tmp/captures"); QCOMPARE(repaired.diagnostics.size(), 4);
    QCOMPARE(repaired.values.value("saveFolder").toString(), "/tmp/captures");
    QVERIFY(repaired.values.value("showTrayIcon").toBool()); QVERIFY(!repaired.values.value("showNotification").toBool());
    QCOMPARE(repaired.values.value("future"), input.value("future"));
    QCOMPARE(repaired.values.value("hotkeys").toObject().value("future-action"), bindings.value("future-action"));
    QCOMPARE(hotkeys(repaired.values).entries.value("capture-region"), "");
    for (const auto &field : settingFields()) QCOMPARE(field.scope, "profile");
  }
  void outputDefaultsAndWorkflowPrecedence() {
    const auto workflow = xerahs::workflows::defaultCaptureWorkflows().entries.first();
    auto values = defaultSettings("/tmp/profile"); values.insert("filenamePattern", "profile-%width");
    const auto inherited = withApplicationDefaults(workflow, values, false);
    QCOMPARE(inherited.output.directory, "/tmp/profile"); QCOMPARE(inherited.output.pattern, "profile-%width");
    const auto saved = withApplicationDefaults(workflow, values, true);
    QCOMPARE(saved.output.directory, "/tmp/profile"); QCOMPARE(saved.output.pattern, workflow.output.pattern);
    auto explicitWorkflow = workflow; explicitWorkflow.output.directory = "/tmp/workflow"; explicitWorkflow.output.pattern = "workflow-%height";
    const auto explicitOutput = withApplicationDefaults(explicitWorkflow, values, true);
    QCOMPARE(explicitOutput.output.directory, "/tmp/workflow"); QCOMPARE(explicitOutput.output.pattern, "workflow-%height");
    const auto preview = xerahs::workflows::previewCaptureOutput(inherited.output);
    QVERIFY(preview.diagnostic.isEmpty()); QCOMPARE(preview.filename, "profile-1920.png");
  }
  void resetPreviewDoesNotPersist() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    auto input = store.current().values; input.insert("startAtLogin", true); input.insert("playCaptureSound", true); input.insert("unknown", 4);
    QVERIFY(store.save(input).isEmpty());
    const auto preview = resetPreview(input, Section::General, dir.path()); QCOMPARE(preview.size(), 1); QVERIFY(preview.first().contains("startAtLogin"));
    const auto reset = resetSection(input, Section::General, dir.path()); QVERIFY(!reset.value("startAtLogin").toBool());
    QVERIFY(reset.value("playCaptureSound").toBool()); QCOMPARE(reset.value("unknown").toInt(), 4);
    ApplicationSettingsStore reopened(dir.filePath("settings.json"), dir.path()); QVERIFY(reopened.current().values.value("startAtLogin").toBool());
  }
  void atomicStoreUnknownAndFailures() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    ApplicationSettingsStore store(path, dir.path()); auto values = store.current().values; values.insert("future", QJsonObject{{"x", 1}});
    QVERIFY(store.save(values).isEmpty());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
    QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock()); values.insert("showTrayIcon", false); QVERIFY(!store.save(values).isEmpty()); lock.unlock();
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
    ApplicationSettingsStore stale(path, dir.path()); QVERIFY(store.save(values).isEmpty()); QVERIFY(!stale.save(stale.current().values).isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); const auto envelope = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    QCOMPARE(envelope.value("previous").toObject().value("showTrayIcon"), QJsonValue(true));
    QCOMPARE(envelope.value("values").toObject().value("future"), values.value("future"));
    // Future envelopes are never downgraded or overwritten.
    QVERIFY(file.open(QIODevice::WriteOnly)); auto future = envelope; future.insert("version", 2); future.insert("extension", 7); file.write(QJsonDocument(future).toJson()); file.close();
    ApplicationSettingsStore newer(path, dir.path()); QVERIFY(!newer.current().writable); QVERIFY(!newer.save(values).isEmpty());
    ApplicationSettingsStore blocked(dir.filePath("blocked"), dir.path()); QVERIFY(QDir().mkdir(dir.filePath("blocked"))); QVERIFY(!blocked.save(values).isEmpty()); QVERIFY(QFileInfo(dir.filePath("blocked")).isDir());
  }
  void autostart() {
    QTemporaryDir dir; const auto old = qgetenv("XDG_CONFIG_HOME"); qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
    const auto path = dir.filePath("autostart/com.xerahs.app.desktop");
    QVERIFY(xerahs::app::setAutostart(true, "/opt/XerahS With Spaces/xerahs").isEmpty());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
    QVERIFY(bytes.contains("Exec=\"/opt/XerahS With Spaces/xerahs\"")); QVERIFY(bytes.contains("Name=XerahS"));
    QVERIFY(!xerahs::app::setAutostart(true, "/tmp/bad\nExec=bad").isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes); file.close();
    QVERIFY(xerahs::app::setAutostart(false, "").isEmpty()); QVERIFY(!QFile::exists(path));
    QVERIFY(xerahs::app::setAutostart(false, "").isEmpty());
    if (old.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", old);
  }
  void combinedPlanAndAuthority() {
    using namespace xerahs::workflows;
    const auto catalog = defaultCaptureWorkflows(); const auto plan = captureShortcutPlan(catalog, implementedCaptureActions(), HotkeyBindings::defaults());
    QVERIFY(plan.diagnostic.isEmpty()); QCOMPARE(plan.entries.size(), catalog.entries.size() + xerahs::settings::captureHotkeyActions().size());
    WorkflowShortcutState state(plan); QStringList ids;
    for (const auto &entry : plan.entries) ids.append(entry.id);
    QVERIFY(state.setBound(ids)); QCOMPARE(state.press("capture-region"), "capture-region"); QVERIFY(state.press("capture-region").isEmpty());
    state.release("capture-region"); QCOMPARE(state.press("capture-region"), "capture-region");
    QVERIFY(state.press("capture-unknown").isEmpty()); QVERIFY(!state.setBound({"capture-unknown"}));
    auto bindings = HotkeyBindings::defaults(); bindings.entries.insert(catalog.entries.first().id.toString(QUuid::WithoutBraces), "Print");
    QVERIFY(!captureShortcutPlan(catalog, implementedCaptureActions(), bindings).diagnostic.isEmpty());
  }
  void sharedActionRoutingRefusesBusyModalAndDisabled() {
    QAction region("Capture region", this); QSignalSpy invoked(&region, &QAction::triggered);
    const QMap<QString, QAction *> actions{{"capture-region", &region}}; int refusals = 0; bool busy = false;
    auto available = [&] { return !busy && !QApplication::activeModalWidget(); };
    auto refused = [&] { ++refusals; };
    QVERIFY(xerahs::app::routeCaptureShortcut("capture-region", actions, available, refused)); QCOMPARE(invoked.size(), 1);
    busy = true; QVERIFY(xerahs::app::routeCaptureShortcut("capture-region", actions, available, refused)); QCOMPARE(refusals, 1); QCOMPARE(invoked.size(), 1);
    busy = false; QDialog modal; modal.setModal(true); modal.show(); QTRY_VERIFY(QApplication::activeModalWidget());
    QVERIFY(xerahs::app::routeCaptureShortcut("capture-region", actions, available, refused)); QCOMPARE(refusals, 2); modal.hide();
    region.setEnabled(false); QVERIFY(xerahs::app::routeCaptureShortcut("capture-region", actions, available, refused)); QCOMPARE(refusals, 3);
    region.setEnabled(true); QCoreApplication::processEvents(); QCOMPARE(invoked.size(), 1);
    QVERIFY(!xerahs::app::routeCaptureShortcut("unknown", actions, available, refused));
  }
  void dialogCancelAndConflict() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {});
    QVERIFY(!dialog.accessibleName().isEmpty());
    const auto fields = dialog.findChildren<QLineEdit *>(); QVERIFY(fields.size() >= 3);
    QLineEdit *folder = nullptr;
    for (auto *field : fields) if (field->accessibleName() == "Default save folder") folder = field;
    QVERIFY(folder); folder->setText("/tmp/unapplied");
    auto *table = dialog.findChild<QTableWidget *>();
    for (int row = 0; row < table->rowCount(); ++row)
      if (table->item(row, 0)->data(Qt::UserRole).toString() == "capture-region") table->selectRow(row);
    for (auto *field : fields) if (field->accessibleName() == "Preferred trigger for selected action") field->setText("Ctrl+Print");
    for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text().startsWith("Set &preferred")) button->click();
    bool conflictShown = false;
    for (auto *label : dialog.findChildren<QLabel *>()) if (label->text().contains("hotkey-conflict")) conflictShown = true;
    QVERIFY(conflictShown);
    QTimer::singleShot(0, [] { if (auto *preview = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) preview->done(QMessageBox::Cancel); });
    for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == "&Reset section…") button->click();
    QCOMPARE(folder->text(), "/tmp/unapplied");
    dialog.reject(); QVERIFY(!QFile::exists(dir.filePath("settings.json")));
    QCOMPARE(store.current().values.value("saveFolder").toString(), dir.path());
    QCOMPARE(table->rowCount(), plan.entries.size());
    for (auto *field : fields) QVERIFY(!field->accessibleName().isEmpty());
  }
};
QTEST_MAIN(ApplicationSettingsTest)
#include "ApplicationSettingsTest.moc"
