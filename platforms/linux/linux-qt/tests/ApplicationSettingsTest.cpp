#include "settings/ApplicationSettings.h"
#include "workflows/WorkflowShortcuts.h"
#include "XdgAutostart.h"
#include "SettingsDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QSpinBox>
#include <QTabWidget>
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
  void scheduledCaptureChoicesDefaultsRoundTripAndRepair() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    const auto defaults = defaultSettings(dir.path());
    QCOMPARE(defaults.value("delayedCapture.seconds").toInt(), 3);
    QCOMPARE(defaults.value("delayedCapture.source").toString(), QString("full-screen"));
    QCOMPARE(defaults.value("autoCapture.source").toString(), QString("full-screen"));
    QCOMPARE(defaults.value("autoCapture.intervalMs").toInt(), 5000);
    QCOMPARE(defaults.value("autoCapture.boundKind").toString(), QString("count"));
    QCOMPARE(defaults.value("autoCapture.count").toInt(), 10);
    QCOMPARE(defaults.value("autoCapture.durationMinutes").toInt(), 60);
    const QStringList keys{"delayedCapture.seconds", "delayedCapture.source", "autoCapture.source", "autoCapture.intervalMs",
      "autoCapture.boundKind", "autoCapture.count", "autoCapture.durationMinutes"};
    for (const auto &key : keys) QVERIFY(!defaults.contains(key + "Absolute"));
    QVERIFY(!defaults.contains("autoCapture.endMs"));
    // Old files without the keys load with defaults and no diagnostics.
    auto old = defaults; for (const auto &key : keys) old.remove(key);
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", old}}).toJson()); file.close();
    ApplicationSettingsStore store(path, dir.path()); QVERIFY(store.current().diagnostics.isEmpty());
    for (const auto &key : keys) QCOMPARE(store.current().values.value(key), defaults.value(key));
    auto values = store.current().values;
    values.insert("delayedCapture.seconds", 60); values.insert("delayedCapture.source", "region"); values.insert("autoCapture.source", "active-window");
    values.insert("autoCapture.intervalMs", 1000); values.insert("autoCapture.boundKind", "time"); values.insert("autoCapture.count", 10000); values.insert("autoCapture.durationMinutes", 1440);
    QVERIFY(store.save(values).isEmpty());
    ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.current().values, values); QVERIFY(reopened.current().diagnostics.isEmpty());
    const QVector<QPair<QString, QJsonValue>> invalid{{"delayedCapture.seconds", 0}, {"delayedCapture.seconds", 61}, {"delayedCapture.seconds", 2.5}, {"delayedCapture.seconds", "3"},
      {"delayedCapture.source", "window"}, {"delayedCapture.source", 1}, {"autoCapture.source", "region"}, {"autoCapture.intervalMs", 999},
      {"autoCapture.intervalMs", 86400001}, {"autoCapture.boundKind", "end"}, {"autoCapture.count", 0}, {"autoCapture.count", 10001},
      {"autoCapture.durationMinutes", 0}, {"autoCapture.durationMinutes", 1441}, {"autoCapture.durationMinutes", true}};
    for (const auto &entry : invalid) {
      auto bad = values; bad.insert(entry.first, entry.second);
      const auto repaired = repairSettings(bad, dir.path());
      QVERIFY2(repaired.diagnostics.join('\n').contains(entry.first), qPrintable(entry.first));
      QCOMPARE(repaired.values.value(entry.first), defaults.value(entry.first));
      QVERIFY(!store.save(bad).isEmpty());
    }
    // Resetting the Capture section resets these too; other sections are kept.
    values.insert("saveFolder", dir.path() + "/kept");
    const auto reset = resetSection(values, Section::Capture, dir.path());
    for (const auto &key : keys) QCOMPARE(reset.value(key), defaults.value(key));
    QCOMPARE(reset.value("saveFolder"), values.value("saveFolder"));
    QVERIFY(resetPreview(values, Section::Capture, dir.path()).join('\n').contains("autoCapture.count"));
  }
  void scrollingCaptureSettingsDefaultsRoundTripAndRepair() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    const auto defaults = defaultSettings(dir.path());
    QCOMPARE(defaults.value("scrollingCapture.settleMs").toInt(), 300);
    QCOMPARE(defaults.value("scrollingCapture.wheelSteps").toInt(), 5);
    QCOMPARE(defaults.value("scrollingCapture.maxHeight").toInt(), 30000);
    QCOMPARE(defaults.value("scrollingCapture.maxFrames").toInt(), 40);
    QCOMPARE(defaults.value("scrollingCapture.positionPointer").toBool(), true);
    QVERIFY(!defaults.contains("scrollingCapture.alignmentThreshold"));
    const QStringList keys{"scrollingCapture.settleMs", "scrollingCapture.wheelSteps", "scrollingCapture.headerBand", "scrollingCapture.footerBand",
      "scrollingCapture.maxHeight", "scrollingCapture.maxFrames", "scrollingCapture.positionPointer"};
    auto old = defaults; for (const auto &key : keys) old.remove(key);
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", old}}).toJson()); file.close();
    ApplicationSettingsStore store(path, dir.path()); QVERIFY(store.current().diagnostics.isEmpty());
    for (const auto &key : keys) QCOMPARE(store.current().values.value(key), defaults.value(key));
    auto values = store.current().values;
    values.insert("scrollingCapture.settleMs", 0); values.insert("scrollingCapture.wheelSteps", 50); values.insert("scrollingCapture.headerBand", 2000);
    values.insert("scrollingCapture.footerBand", 12); values.insert("scrollingCapture.maxHeight", 100000); values.insert("scrollingCapture.maxFrames", 2);
    values.insert("scrollingCapture.positionPointer", false);
    QVERIFY(store.save(values).isEmpty());
    ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.current().values, values); QVERIFY(reopened.current().diagnostics.isEmpty());
    const QVector<QPair<QString, QJsonValue>> invalid{{"scrollingCapture.settleMs", -1}, {"scrollingCapture.settleMs", 5001}, {"scrollingCapture.wheelSteps", 0},
      {"scrollingCapture.wheelSteps", 51}, {"scrollingCapture.headerBand", -1}, {"scrollingCapture.headerBand", 2001}, {"scrollingCapture.footerBand", 2001},
      {"scrollingCapture.maxHeight", 999}, {"scrollingCapture.maxHeight", 100001}, {"scrollingCapture.maxFrames", 1}, {"scrollingCapture.maxFrames", 501},
      {"scrollingCapture.maxFrames", 2.5}, {"scrollingCapture.settleMs", "300"}, {"scrollingCapture.positionPointer", 1}};
    for (const auto &entry : invalid) {
      auto bad = values; bad.insert(entry.first, entry.second);
      const auto repaired = repairSettings(bad, dir.path());
      QVERIFY2(repaired.diagnostics.join('\n').contains(entry.first), qPrintable(entry.first));
      QCOMPARE(repaired.values.value(entry.first), defaults.value(entry.first));
      QVERIFY(!store.save(bad).isEmpty());
    }
    const auto reset = resetSection(values, Section::Capture, dir.path());
    for (const auto &key : keys) QCOMPARE(reset.value(key), defaults.value(key));
    QVERIFY(resetPreview(values, Section::Capture, dir.path()).join('\n').contains("scrollingCapture.maxFrames"));
  }
  void captureTabShowsAppliesCancelsAndResetsScheduleAndScrollingChoices() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    auto stored = store.current().values;
    stored.insert("delayedCapture.seconds", 12); stored.insert("delayedCapture.source", "region"); stored.insert("autoCapture.source", "monitor");
    stored.insert("autoCapture.intervalMs", 7000); stored.insert("autoCapture.boundKind", "time"); stored.insert("autoCapture.count", 20);
    stored.insert("autoCapture.durationMinutes", 15); stored.insert("scrollingCapture.settleMs", 450); stored.insert("scrollingCapture.wheelSteps", 8);
    stored.insert("scrollingCapture.headerBand", 64); stored.insert("scrollingCapture.footerBand", 32); stored.insert("scrollingCapture.maxHeight", 20000);
    stored.insert("scrollingCapture.maxFrames", 25); stored.insert("scrollingCapture.positionPointer", false);
    QVERIFY(store.save(stored).isEmpty());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    auto spinNamed = [](QWidget &w, const QString &name) { for (auto *b : w.findChildren<QSpinBox *>()) if (b->accessibleName() == name) return b; return static_cast<QSpinBox *>(nullptr); };
    auto comboNamed = [](QWidget &w, const QString &name) { for (auto *b : w.findChildren<QComboBox *>()) if (b->accessibleName() == name) return b; return static_cast<QComboBox *>(nullptr); };
    int applied = 0;
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [&] { ++applied; });
      QStringList groups; for (auto *g : dialog.findChildren<QGroupBox *>()) groups << g->title();
      QCOMPARE(groups, (QStringList{"Delayed capture", "Automatic capture", "Scrolling capture"}));
      QCOMPARE(spinNamed(dialog, "Delayed capture seconds")->value(), 12);
      QCOMPARE(comboNamed(dialog, "Delayed capture source")->currentData().toString(), QString("region"));
      QCOMPARE(comboNamed(dialog, "Automatic capture source")->currentData().toString(), QString("monitor"));
      QCOMPARE(spinNamed(dialog, "Automatic capture interval seconds")->value(), 7);
      QCOMPARE(comboNamed(dialog, "Automatic capture bound")->currentData().toString(), QString("time"));
      QCOMPARE(spinNamed(dialog, "Automatic capture duration minutes")->value(), 15);
      QCOMPARE(spinNamed(dialog, "Scrolling capture settle delay milliseconds")->value(), 450);
      QCOMPARE(spinNamed(dialog, "Scrolling capture wheel steps per frame")->value(), 8);
      QCOMPARE(spinNamed(dialog, "Scrolling capture header band pixels")->value(), 64);
      QCOMPARE(spinNamed(dialog, "Scrolling capture footer band pixels")->value(), 32);
      QCOMPARE(spinNamed(dialog, "Scrolling capture maximum height pixels")->value(), 20000);
      QCOMPARE(spinNamed(dialog, "Scrolling capture maximum frames")->value(), 25);
      for (auto *c : dialog.findChildren<QCheckBox *>()) if (c->accessibleName() == "Keep pointer over the scrolling region") QVERIFY(!c->isChecked());
      for (auto *w : dialog.findChildren<QAbstractSpinBox *>()) QVERIFY(!w->accessibleName().isEmpty());
      // Cancel discards edits.
      spinNamed(dialog, "Scrolling capture maximum frames")->setValue(99);
      dialog.reject();
    }
    QCOMPARE(store.current().values.value("scrollingCapture.maxFrames").toInt(), 25);
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [&] { ++applied; });
      QCOMPARE(spinNamed(dialog, "Scrolling capture maximum frames")->value(), 25);
      spinNamed(dialog, "Scrolling capture maximum frames")->setValue(60);
      spinNamed(dialog, "Delayed capture seconds")->setValue(9);
      comboNamed(dialog, "Automatic capture bound")->setCurrentIndex(0);
      spinNamed(dialog, "Automatic capture count")->setValue(33);
      for (auto *b : dialog.findChildren<QPushButton *>()) if (b->text().remove(QChar(0x26)) == "Apply") b->click();
      QCOMPARE(applied, 1);
      QCOMPARE(store.current().values.value("scrollingCapture.maxFrames").toInt(), 60);
      QCOMPARE(store.current().values.value("delayedCapture.seconds").toInt(), 9);
      QCOMPARE(store.current().values.value("autoCapture.boundKind").toString(), QString("count"));
      QCOMPARE(store.current().values.value("autoCapture.count").toInt(), 33);
      QCOMPARE(store.current().values.value("autoCapture.intervalMs").toInt(), 7000);
      // Reset Capture previews, then resets the controls without saving.
      auto *tabs = dialog.findChild<QTabWidget *>(); tabs->setCurrentIndex(1);
      QString preview;
      QTimer::singleShot(0, [&preview] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { preview = box->text(); box->button(QMessageBox::Reset)->click(); } });
      for (auto *b : dialog.findChildren<QPushButton *>()) if (b->text() == "&Reset section…") b->click();
      QVERIFY(preview.contains("scrollingCapture.maxFrames")); QVERIFY(preview.contains("delayedCapture.seconds"));
      QCOMPARE(spinNamed(dialog, "Scrolling capture maximum frames")->value(), 40);
      QCOMPARE(spinNamed(dialog, "Scrolling capture settle delay milliseconds")->value(), 300);
      QCOMPARE(spinNamed(dialog, "Delayed capture seconds")->value(), 3);
      QCOMPARE(comboNamed(dialog, "Automatic capture bound")->currentData().toString(), QString("count"));
      QCOMPARE(spinNamed(dialog, "Automatic capture interval seconds")->value(), 5);
      QCOMPARE(store.current().values.value("scrollingCapture.maxFrames").toInt(), 60);  // not saved until Apply
      for (auto *b : dialog.findChildren<QPushButton *>()) if (b->text().remove(QChar(0x26)) == "Apply") b->click();
      QCOMPARE(store.current().values.value("scrollingCapture.maxFrames").toInt(), 40);
      QCOMPARE(store.current().values.value("autoCapture.intervalMs").toInt(), 5000);
    }
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
