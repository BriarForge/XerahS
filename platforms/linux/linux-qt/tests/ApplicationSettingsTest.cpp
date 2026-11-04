#include "settings/ApplicationSettings.h"
#include "settings/ReferenceMigration.h"
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
#include <QFileDialog>
#include <QPointer>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QSet>
#include <QUuid>
#include <clocale>
#include <limits>
#include <string>
#include <QRegularExpression>
#include <QtTest>
using namespace xerahs::settings;
class ApplicationSettingsTest : public QObject {
  Q_OBJECT
private slots:
  void explicitMigrationPreviewBackupRestoreAndDismissControls() {
    struct TestPaths { bool old = QStandardPaths::isTestModeEnabled(); TestPaths() { QStandardPaths::setTestModeEnabled(true); } ~TestPaths() { QStandardPaths::setTestModeEnabled(old); } } testPaths;
    QTemporaryDir source, native;
    QFile input(source.filePath("ApplicationConfig.json")); QVERIFY(input.open(QIODevice::WriteOnly)); input.write("{\"ApplicationVersion\":\"0.19.0\",\"ShowTray\":false}"); input.close();
    ConfigurationPaths paths{native.filePath("app.json"), native.filePath("editor.json"), native.filePath("workflows.json"), native.filePath("destinations.json"), native.path()};
    ApplicationSettingsStore store(paths.application, native.path()); QVERIFY(store.save(store.current().values).isEmpty());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    xerahs::app::SettingsDialog dialog(store, native.path(), {}, shortcuts, [] {}, nullptr, paths); dialog.show();
    auto *restore = dialog.findChild<QPushButton *>("restorePreviousSettings"); QVERIFY(restore); QVERIFY(!restore->isEnabled());
    QVERIFY(!ReferenceMigration(paths).backupAvailable());
    dialog.findChild<QPushButton *>("migrateReferenceSettings")->click();
    auto *picker = dialog.findChild<QFileDialog *>("referenceSettingsFolderPicker"); QVERIFY(picker);
    QMetaObject::invokeMethod(picker, "fileSelected", Qt::DirectConnection, Q_ARG(QString, source.path())); picker->reject();
    auto *preview = dialog.findChild<QMessageBox *>("configurationImportPreview"); QVERIFY(preview); QVERIFY(!ReferenceMigration(paths).backupAvailable());
    preview->done(QMessageBox::Apply); QVERIFY(restore->isEnabled()); QVERIFY(!store.current().values["showTrayIcon"].toBool());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    restore->click(); auto *confirm = dialog.findChild<QMessageBox *>("migrationBackupConfirmation"); QVERIFY(confirm); confirm->done(QMessageBox::Apply);
    QVERIFY(store.current().values["showTrayIcon"].toBool()); QVERIFY(restore->isEnabled());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<QPushButton *>("dismissMigrationBackup")->click(); confirm = dialog.findChild<QMessageBox *>("migrationBackupConfirmation"); QVERIFY(confirm); confirm->done(QMessageBox::Apply);
    QVERIFY(!restore->isEnabled()); QVERIFY(!ReferenceMigration(paths).backupAvailable());
  }

  void configurationControlsPreviewCancelConfirmAndOwnerTeardown() {
    QTemporaryDir dir;
    ConfigurationPaths paths{dir.filePath("app.json"), dir.filePath("editor.json"), dir.filePath("workflows.json"), dir.filePath("destinations.json"), dir.path()};
    ConfigurationTransfer transfer(paths); const auto exportPath = dir.filePath("export.json"); QVERIFY(transfer.exportFile(exportPath).isEmpty());
    ApplicationSettingsStore store(paths.application, dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    int imported = 0;
    auto *dialog = new xerahs::app::SettingsDialog(store, dir.path(), {}, shortcuts, [] {}, nullptr, paths, [&] { ++imported; });
    dialog->show();
    dialog->findChild<QPushButton *>("exportSettings")->click();
    auto *exportPicker = dialog->findChild<QFileDialog *>("configurationFilePicker"); QVERIFY(exportPicker);
    const auto uiExport = dir.filePath("ui-export.json");
    QMetaObject::invokeMethod(exportPicker, "fileSelected", Qt::DirectConnection, Q_ARG(QString, uiExport)); exportPicker->reject();
    QVERIFY(QFile::exists(uiExport)); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto choose = [&] {
      dialog->findChild<QPushButton *>("importSettings")->click();
      auto *picker = dialog->findChild<QFileDialog *>("configurationFilePicker"); QVERIFY(picker);
      QMetaObject::invokeMethod(picker, "fileSelected", Qt::DirectConnection, Q_ARG(QString, exportPath));
      picker->reject();
    };
    choose(); auto *preview = dialog->findChild<QMessageBox *>("configurationImportPreview"); QVERIFY(preview);
    QVERIFY(!QFile::exists(paths.application)); preview->done(QMessageBox::Cancel); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(imported, 0); QVERIFY(!QFile::exists(paths.application));
    choose(); preview = dialog->findChild<QMessageBox *>("configurationImportPreview"); QVERIFY(preview); preview->done(QMessageBox::Apply);
    QCOMPARE(imported, 1); QVERIFY(QFile::exists(paths.application));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    choose(); QPointer<QMessageBox> orphan = dialog->findChild<QMessageBox *>("configurationImportPreview"); QVERIFY(orphan);
    delete dialog; QVERIFY(!orphan); QCOMPARE(imported, 1);
  }

  void nativeAccessibilityFallbackIsVisibleAndDoesNotPersistDesktopChoices() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [] {}); dialog.show();
    auto *note = dialog.findChild<QLabel *>("nativeAccessibilityDisposition"); QVERIFY(note); QVERIFY(note->isVisible());
    QVERIFY(note->text().contains("Degraded:")); QVERIFY(note->text().contains("reduced-contrast"));
    QVERIFY(note->text().contains("Do Not Disturb")); QCOMPARE(note->accessibleDescription(), note->text());
    dialog.reject(); QVERIFY(!QFile::exists(dir.filePath("settings.json")));
  }
  void regionPreferencesDefaultsRepairRoundTripAndReset() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    const QStringList keys{"regionCapture.quickConfirm", "regionCapture.snapping"};
    auto old = defaultSettings(dir.path()); for (const auto &key : keys) { QCOMPARE(old.value(key), QJsonValue(true)); old.remove(key); }
    old.insert("future.region", "kept");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", old}}).toJson()); file.close();
    ApplicationSettingsStore store(path, dir.path()); QVERIFY(store.current().diagnostics.isEmpty());
    auto values = store.current().values;
    for (const auto &key : keys) {
      QCOMPARE(values.value(key), QJsonValue(true)); values.insert(key, false);
      for (const auto bad : {QJsonValue("false"), QJsonValue(0), QJsonValue(QJsonValue::Null), QJsonValue(QJsonArray{})}) {
        auto input = values; input.insert(key, bad); const auto repaired = repairSettings(input, dir.path());
        QVERIFY(repaired.diagnostics.join('\n').contains(key)); QCOMPARE(repaired.values.value(key), QJsonValue(true));
        QVERIFY(!store.save(input).isEmpty());
      }
    }
    QVERIFY(!regionSessionSettings(values).quickConfirm); QVERIFY(!regionSessionSettings(values).snapping);
    auto mixed = values; mixed.insert("regionCapture.snapping", true);
    QVERIFY(!regionSessionSettings(mixed).quickConfirm); QVERIFY(regionSessionSettings(mixed).snapping);
    mixed.insert("regionCapture.quickConfirm", true); mixed.insert("regionCapture.snapping", false);
    QVERIFY(regionSessionSettings(mixed).quickConfirm); QVERIFY(!regionSessionSettings(mixed).snapping);
    QVERIFY(regionSessionSettings({}).quickConfirm); QVERIFY(regionSessionSettings({{"regionCapture.snapping", "false"}}).snapping);
    QVERIFY(store.save(values).isEmpty()); ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.current().values, values);
    const auto reset = resetSection(values, Section::Capture, dir.path()); for (const auto &key : keys) QCOMPARE(reset.value(key), QJsonValue(true));
    QCOMPARE(reset.value("future.region"), QJsonValue("kept")); QCOMPARE(reset.value("saveFolder"), values.value("saveFolder"));
    QVERIFY(resetPreview(values, Section::Capture, dir.path()).join('\n').contains("regionCapture.quickConfirm"));
  }
  void regionPreferencesDialogApplyCancelAndReset() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; }); int applied = 0;
    auto boxes = [](QDialog &dialog) { QList<QCheckBox *> result;
      for (auto *box : dialog.findChildren<QCheckBox *>()) if (box->accessibleName() == "Confirm region on pointer release" || box->accessibleName() == "Snap clicks to detected windows or controls") result.append(box); return result; };
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [&] { ++applied; });
      auto *report = dialog.findChild<QLabel *>("settingsImportReport"); QVERIFY(report); QCOMPARE(report->text(), store.current().report.summary());
      const auto controls = boxes(dialog); QCOMPARE(controls.size(), 2); for (auto *box : controls) { QVERIFY(box->isChecked()); box->setChecked(false); }
      dialog.reject(); QCOMPARE(applied, 0); QVERIFY(!QFile::exists(dir.filePath("settings.json")));
    }
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [&] { ++applied; });
      const auto controls = boxes(dialog); for (auto *box : controls) box->setChecked(false);
      dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); QCOMPARE(applied, 1);
      QVERIFY(!regionSessionSettings(store.current().values).quickConfirm); QVERIFY(!regionSessionSettings(store.current().values).snapping);
      dialog.findChild<QTabWidget *>()->setCurrentIndex(1);
      QTimer::singleShot(0, [] { if (auto *preview = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) preview->button(QMessageBox::Reset)->click(); });
      for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == "&Reset section…") button->click();
      for (auto *box : controls) QVERIFY(box->isChecked());
      QVERIFY(!regionSessionSettings(store.current().values).quickConfirm);
      dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); QCOMPARE(applied, 2);
      QVERIFY(regionSessionSettings(store.current().values).quickConfirm); QVERIFY(regionSessionSettings(store.current().values).snapping);
    }
  }

  void privateNotificationsCheckboxAppliesAndCancels() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; }); int applied = 0;
    auto checkbox = [](QDialog &dialog) { for (auto *candidate : dialog.findChildren<QCheckBox *>()) if (candidate->accessibleName() == "Hide private details in notifications") return candidate; return static_cast<QCheckBox *>(nullptr); };
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [&] { ++applied; });
      auto *box = checkbox(dialog); QVERIFY(box); QVERIFY(!box->isChecked()); box->setChecked(true);
      dialog.reject(); QCOMPARE(applied, 0); QVERIFY(!store.current().values.value("privateNotifications").toBool());
    }
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [&] { ++applied; });
      auto *box = checkbox(dialog); QVERIFY(box); box->setChecked(true);
      dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click(); QCOMPARE(applied, 1);
      QVERIFY(store.current().values.value("privateNotifications").toBool());
    }
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [&] { ++applied; });
      auto *box = checkbox(dialog); QVERIFY(box); QVERIFY(box->isChecked());
      dialog.reject();
    }
  }

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
  // SETCAT-003: every field-state class, secret-free diagnostics, unrelated fields survive.
  void fieldStatesDistinguishAbsentValidInvalidUnknownDeprecatedAndNewer() {
    QTemporaryDir dir; auto input = defaultSettings(dir.path());
    input.remove("showTrayIcon");
    input.insert("showNotification", "secret-token-AAAA");
    input.insert("legacy.option", "secret-legacy-BBBB"); input.insert("future.option", QJsonObject{{"label", "future-CCCC"}});
    auto bindings = input.value("hotkeys").toObject(); bindings.insert("capture-region", "Alt+F4"); bindings.insert("future-action", "secret-action-DDDD"); bindings.remove("capture-monitor");
    input.insert("hotkeys", bindings);
    const auto read = repairSettings(input, dir.path(), {"legacy.option"});
    QCOMPARE(read.fields.value("showTrayIcon"), FieldState::Missing);
    QCOMPARE(read.fields.value("saveFolder"), FieldState::Valid);
    QCOMPARE(read.fields.value("showNotification"), FieldState::Invalid);
    QCOMPARE(read.fields.value("future.option"), FieldState::Unknown);
    QCOMPARE(read.fields.value("legacy.option"), FieldState::Deprecated);
    QCOMPARE(read.fields.value("hotkeys"), FieldState::Valid);
    QCOMPARE(read.fields.value("hotkeys.capture-region"), FieldState::Invalid);
    QCOMPARE(read.fields.value("hotkeys.future-action"), FieldState::Unknown);
    QCOMPARE(read.fields.value("hotkeys.capture-monitor"), FieldState::Missing);
    QCOMPARE(read.fields.value("hotkeys.capture-full-screen"), FieldState::Valid);
    for (const auto &field : settingFields()) QVERIFY2(read.fields.contains(field.key), qPrintable(field.key));
    // Without a declared deprecated key the same field is merely unknown.
    QVERIFY(deprecatedSettingKeys().isEmpty());
    QCOMPARE(repairSettings(input, dir.path()).fields.value("legacy.option"), FieldState::Unknown);
    // Only Invalid repairs produce diagnostics; they name fields and never repeat a value.
    QCOMPARE(read.diagnostics.size(), 2);
    const auto text = read.diagnostics.join('\n');
    QVERIFY(text.contains("showNotification")); QVERIFY(text.contains("hotkeys.capture-region"));
    QVERIFY(!text.contains("secret")); QVERIFY(!text.contains("AAAA"));
    // One failing field never discards unrelated valid, unknown or deprecated ones.
    QCOMPARE(read.values.value("saveFolder"), input.value("saveFolder"));
    QCOMPARE(read.values.value("legacy.option"), input.value("legacy.option"));
    QCOMPARE(read.values.value("future.option"), input.value("future.option"));
    QCOMPARE(read.values.value("hotkeys").toObject().value("future-action"), QJsonValue("secret-action-DDDD"));
    QVERIFY(read.values.value("showNotification").toBool());
    QSet<QString> names; for (auto s : {FieldState::Missing, FieldState::Valid, FieldState::Invalid, FieldState::Unknown, FieldState::Deprecated, FieldState::NewerVersion}) names.insert(fieldStateName(s));
    QCOMPARE(names.size(), 6);
  }
  void storeReportsMissingFileAndNewerVersionFieldStates() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    ApplicationSettingsStore fresh(path, dir.path());
    for (const auto &field : settingFields()) QCOMPARE(fresh.current().fields.value(field.key), FieldState::Missing);
    QVERIFY(fresh.current().diagnostics.isEmpty());
    QJsonObject values = defaultSettings(dir.path()); values.insert("showNotification", "secret-token-EEEE");
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"version", 2}, {"values", values}, {"extension", 7}}).toJson()); file.close();
    QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
    ApplicationSettingsStore newer(path, dir.path());
    QVERIFY(!newer.current().writable); QCOMPARE(newer.current().fields.value("version"), FieldState::NewerVersion);
    for (const auto &field : settingFields()) QCOMPARE(newer.current().fields.value(field.key), FieldState::NewerVersion);
    QCOMPARE(newer.current().diagnostics, QStringList{"settings-version-unsupported"});
    QVERIFY(!newer.save(defaultSettings(dir.path())).isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
    // A malformed version is invalid, not newer.
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", "two"}, {"values", values}}).toJson()); file.close();
    ApplicationSettingsStore bad(path, dir.path()); QVERIFY(!bad.current().writable);
    QCOMPARE(bad.current().fields.value("version"), FieldState::Invalid);
    // A file on disk reports field-level states through the real store.
    QVERIFY(file.open(QIODevice::WriteOnly)); values.insert("future.option", 1); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", values}}).toJson()); file.close();
    ApplicationSettingsStore disk(path, dir.path());
    QCOMPARE(disk.current().fields.value("showNotification"), FieldState::Invalid);
    QCOMPARE(disk.current().fields.value("future.option"), FieldState::Unknown);
    QVERIFY(!disk.current().diagnostics.join('\n').contains("EEEE"));
  }
  // SETCAT-004: locale-independent UTF-8 JSON, and the documented retention rule.
  void saveEncodingIsLocaleIndependentUtf8() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    const QLocale oldLocale; const std::string oldC = std::setlocale(LC_ALL, nullptr);
    QLocale::setDefault(QLocale(QLocale::Arabic, QLocale::Egypt));
    // Best effort: a comma-decimal C locale if the host has one installed; the Qt default above always differs from C.
    for (const char *name : {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"}) if (std::setlocale(LC_ALL, name)) break;
    {
      ApplicationSettingsStore store(path, dir.path()); auto values = store.current().values;
      const QString folder = dir.filePath(QString::fromUtf8("Übersicht/日本語"));
      values.insert("saveFolder", folder); values.insert("autoCapture.intervalMs", 5000); values.insert("scrollingCapture.maxHeight", 100000);
      values.insert("future.ratio", 1.5); values.insert("future.negative", -0.25); values.insert("future.text", QString::fromUtf8("ß—é"));
      QVERIFY2(store.save(values).isEmpty(), "save under non-C locale");
      QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
      QVERIFY(!bytes.startsWith("\xEF\xBB\xBF")); QVERIFY(!bytes.contains('\r'));
      QVERIFY(bytes.contains("1.5")); QVERIFY(!bytes.contains("1,5")); QVERIFY(bytes.contains("-0.25"));
      QVERIFY(bytes.contains("5000")); QVERIFY(!bytes.contains("5,000")); QVERIFY(bytes.contains("100000"));
      for (const char16_t digit : {u'\u0665', u'\u0660', u'\u0661'}) QVERIFY(!QString::fromUtf8(bytes).contains(QChar(digit)));
      const auto text = QString::fromUtf8(bytes); QVERIFY(!text.contains(QChar(0xFFFD))); QCOMPARE(text.toUtf8(), bytes); // valid UTF-8 only
      QJsonParseError error; const auto parsed = QJsonDocument::fromJson(bytes, &error); QCOMPARE(error.error, QJsonParseError::NoError);
      QCOMPARE(parsed.object().value("values").toObject().value("saveFolder").toString(), folder);
      ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.current().values, values);
    }
    std::setlocale(LC_ALL, oldC.c_str()); QLocale::setDefault(oldLocale);
  }
  void retentionKeepsExactlyOnePreviousVersionInTheSameAtomicFile() {
    QCOMPARE(retainedPreviousVersions, 1);
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    ApplicationSettingsStore store(path, dir.path()); QVERIFY(store.previousValues().isEmpty());
    QList<QJsonObject> generations;
    for (int i = 0; i < 4; ++i) {
      auto values = store.current().values; values.insert("delayedCapture.seconds", 10 + i); values.insert("future.generation", i);
      QVERIFY(store.save(values).isEmpty()); generations.append(store.current().values);
      if (i == 0) QVERIFY(store.previousValues().isEmpty()); // nothing existed before the first save
      else QCOMPARE(store.previousValues(), generations[i - 1]);
    }
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto envelope = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    QCOMPARE(envelope.value("previous").toObject(), generations[2]); QCOMPARE(envelope.value("values").toObject(), generations[3]);
    QVERIFY(!envelope.value("previous").toObject().contains("previous")); // never a chain of history
    // No dated copies, backup folders or temporary files accumulate beside the settings file.
    QStringList files = QDir(dir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); files.removeAll("settings.json.lock");
    QCOMPARE(files, QStringList{"settings.json"});
    // The retained version is reloadable after a restart and can be restored by saving it back.
    ApplicationSettingsStore reopened(path, dir.path()); QCOMPARE(reopened.previousValues(), generations[2]);
    QVERIFY(reopened.save(reopened.previousValues()).isEmpty()); QCOMPARE(reopened.current().values.value("delayedCapture.seconds").toInt(), 12);
    QCOMPARE(reopened.previousValues(), generations[3]);
  }
  // SETCAT-005: boundary and malformed-value matrix for every declared field.
  void valueMatrixCoversEveryDeclaredFieldFamily() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    const QJsonValue nan(std::numeric_limits<double>::quiet_NaN()), inf(std::numeric_limits<double>::infinity());
    const QJsonValue longText(QString(4097, QChar('a'))), embeddedNul(QString("a") + QChar(0) + "b");
    const QVector<QJsonValue> wrongTypes{QJsonValue("true"), QJsonValue(0), QJsonValue(1), QJsonValue(QJsonValue::Null), QJsonValue(QJsonArray{}), QJsonValue(QJsonObject{}), QJsonValue(1.5)};
    QMap<QString, QPair<QVector<QJsonValue>, QVector<QJsonValue>>> matrix; // key -> good, bad
    for (const char *key : {"startAtLogin", "showTrayIcon", "playCaptureSound", "showNotification", "privateNotifications", "regionCapture.quickConfirm", "regionCapture.snapping", "scrollingCapture.positionPointer"})
      matrix[key] = {{true, false}, wrongTypes + QVector<QJsonValue>{"", nan, inf, "yes"}};
    const QMap<QString, QPair<int, int>> ranges{{"delayedCapture.seconds", {1, 60}}, {"autoCapture.intervalMs", {1000, 86400000}}, {"autoCapture.count", {1, 10000}},
      {"autoCapture.durationMinutes", {1, 1440}}, {"scrollingCapture.settleMs", {0, 5000}}, {"scrollingCapture.wheelSteps", {1, 50}}, {"scrollingCapture.headerBand", {0, 2000}},
      {"scrollingCapture.footerBand", {0, 2000}}, {"scrollingCapture.maxHeight", {1000, 100000}}, {"scrollingCapture.maxFrames", {2, 500}}};
    for (auto it = ranges.cbegin(); it != ranges.cend(); ++it) {
      const auto [low, high] = it.value();
      matrix[it.key()] = {{low, high, (low + high) / 2}, {low - 1, high + 1, low + 0.5, QString::number(low), true, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}, nan, inf, -std::numeric_limits<double>::infinity(), 1e300, -1e300, std::numeric_limits<int>::min()}};
    }
    const QVector<QJsonValue> badEnum{"", " ", "Full-Screen", "FULL-SCREEN", "fullscreen", "full_screen", "monitor ", "scrolling", 1, true, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}, embeddedNul, longText};
    matrix["delayedCapture.source"] = {{"full-screen", "monitor", "active-window", "region"}, badEnum};
    matrix["autoCapture.source"] = {{"full-screen", "monitor", "active-window"}, badEnum + QVector<QJsonValue>{"region"}};
    matrix["autoCapture.boundKind"] = {{"count", "time"}, badEnum};
    matrix["saveFolder"] = {{dir.path(), "/tmp/captures", QString::fromUtf8("/tmp/Übersicht/日本語"), "/tmp/with space"},
      {"", "relative/dir", "../bad", "/a/../b", "/a/./b", "~/captures", "C:\\Users\\me\\Pictures", "\\\\server\\share", "/tmp/new\nline", "/tmp/tab\there", embeddedNul, QString("/") + QString(32768, QChar('d')), 5, true, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}}};
    matrix["filenamePattern"] = {{"x", "profile-%width", "Shot %y-%mo-%d", xerahs::workflows::CaptureOutput{}.pattern},
      {"", "%rf{private}", QString(4097, QChar('p')), embeddedNul, 7, false, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}}};
    for (const char *key : {"systemAudioDevice", "microphoneDevice"})
      matrix[key] = {{"", "device.name:alsa_output.pci.monitor", QString::fromUtf8("node.name:Mikrofon Übersicht"), QString(4096, QChar('d'))},
        {longText, embeddedNul, 5, true, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}}};
    // hotkeys has its own matrix below; here only the container shape.
    QJsonObject tooMany; for (int i = 0; i < maximumHotkeyBindings + 1; ++i) tooMany.insert(QUuid::createUuid().toString(QUuid::WithoutBraces), "");
    matrix["hotkeys"] = {{QJsonObject{}, defaultSettings(dir.path()).value("hotkeys")}, {"Print", 5, true, QJsonValue(QJsonValue::Null), QJsonArray{"Print"}, tooMany}};
    for (const auto &field : settingFields()) QVERIFY2(matrix.contains(field.key), qPrintable("no matrix row for " + field.key));
    QCOMPARE(matrix.size(), settingFields().size());
    const auto baseline = defaultSettings(dir.path());
    for (auto it = matrix.cbegin(); it != matrix.cend(); ++it) {
      const auto key = it.key();
      for (const auto &good : it.value().first) {
        auto input = baseline; input.insert(key, good); const auto read = repairSettings(input, dir.path());
        QVERIFY2(read.diagnostics.isEmpty(), qPrintable(key + " rejected a boundary value: " + read.diagnostics.join(';')));
        QCOMPARE(read.fields.value(key), FieldState::Valid); if (key != "hotkeys") QCOMPARE(read.values.value(key), good);
        ApplicationSettingsStore store(path, dir.path()); QVERIFY2(store.save(input).isEmpty(), qPrintable(key));
        QFile::remove(path);
      }
      for (const auto &bad : it.value().second) {
        auto input = baseline; input.insert(key, bad); input.insert("future.keep", 1);
        const auto read = repairSettings(input, dir.path());
        QVERIFY2(read.diagnostics.join('\n').contains(key), qPrintable(key + " accepted a malformed value " + QString::fromUtf8(QJsonDocument(QJsonArray{bad}).toJson(QJsonDocument::Compact))));
        QCOMPARE(read.fields.value(key), FieldState::Invalid); QCOMPARE(read.values.value(key), baseline.value(key)); // repaired to the default
        auto expected = baseline; expected.insert("future.keep", 1); QCOMPARE(read.values, expected); // nothing else changes
        QCOMPARE(read.diagnostics.size(), 1);
        ApplicationSettingsStore store(path, dir.path()); QVERIFY2(!store.save(input).isEmpty(), qPrintable(key + " saved a malformed value"));
        QVERIFY(!QFile::exists(path));
        // The same value read from disk repairs the same way without discarding unrelated fields.
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", input}}).toJson()); file.close();
        ApplicationSettingsStore disk(path, dir.path()); QCOMPARE(disk.current().values, expected); QVERIFY(disk.current().writable); QFile::remove(path);
      }
    }
  }
  void hotkeyMatrixRepairsEachBindingWithoutDiscardingOthers() {
    QTemporaryDir dir; const auto baseline = defaultSettings(dir.path());
    for (const QString &trigger : {"Ctrl+Shift+F12", "F35", "Meta+Space", "Alt+Return", "Ctrl+9", "Shift+Up", ""}) {
      auto input = baseline; auto bindings = input.value("hotkeys").toObject(); bindings.insert("capture-active-window", trigger); input.insert("hotkeys", bindings);
      const auto read = repairSettings(input, dir.path()); QVERIFY2(read.diagnostics.isEmpty(), qPrintable(trigger));
      QCOMPARE(read.values.value("hotkeys").toObject().value("capture-active-window").toString(), trigger);
    }
    { auto input = baseline; auto bindings = input.value("hotkeys").toObject(); bindings.insert("capture-active-window", "Shift+Ctrl+F1"); input.insert("hotkeys", bindings);
      QCOMPARE(repairSettings(input, dir.path()).values.value("hotkeys").toObject().value("capture-active-window").toString(), "Ctrl+Shift+F1"); }
    const QVector<QJsonValue> bad{"Ctrl+Ctrl+Print", "Super+Print", "Ctrl+", "+", "0x1234", "Ctrl+F36", "<Control>F1", "ctrl+f1", "Ctrl+Alt+F4", "Alt+F4", "Alt+Tab", "Meta+L", "Ctrl+Alt+Delete",
      QString(129, QChar('F')), QString("F1") + QChar(0), 5, true, QJsonValue(QJsonValue::Null), QJsonArray{}, QJsonObject{}};
    for (const auto &trigger : bad) {
      auto input = baseline; auto bindings = input.value("hotkeys").toObject(); bindings.insert("capture-active-window", trigger); input.insert("hotkeys", bindings);
      const auto read = repairSettings(input, dir.path());
      QCOMPARE(read.diagnostics.size(), 1); QVERIFY(read.diagnostics.first().startsWith("hotkeys.capture-active-window"));
      QCOMPARE(read.fields.value("hotkeys.capture-active-window"), FieldState::Invalid);
      auto repaired = read.values.value("hotkeys").toObject(); QCOMPARE(repaired.value("capture-active-window").toString(), QString{});
      repaired.remove("capture-active-window"); auto expected = baseline.value("hotkeys").toObject(); expected.remove("capture-active-window"); QCOMPARE(repaired, expected); // others intact
    }
    // Bound: more than the allowed number of bindings is a collection error repaired as one field.
    QJsonObject many; for (int i = 0; i <= maximumHotkeyBindings; ++i) many.insert(QUuid::createUuid().toString(QUuid::WithoutBraces), "");
    auto input = baseline; input.insert("hotkeys", many); const auto read = repairSettings(input, dir.path());
    QCOMPARE(read.fields.value("hotkeys"), FieldState::Invalid); QCOMPARE(read.values.value("hotkeys"), baseline.value("hotkeys"));
    QJsonObject exact; for (int i = 0; i < maximumHotkeyBindings; ++i) exact.insert(QUuid::createUuid().toString(QUuid::WithoutBraces), "");
    input.insert("hotkeys", exact); QCOMPARE(repairSettings(input, dir.path()).fields.value("hotkeys"), FieldState::Valid);
    // Two actions asking for the same trigger: the first (key order) keeps it, the other is cleared with a diagnostic.
    auto clash = baseline; auto bindings = clash.value("hotkeys").toObject(); bindings.insert("capture-scrolling", "Ctrl+Print"); clash.insert("hotkeys", bindings);
    const auto clashRead = repairSettings(clash, dir.path()); QCOMPARE(clashRead.diagnostics.size(), 1);
    QVERIFY(clashRead.diagnostics.join('\n').contains("hotkeys.")); QVERIFY(!clashRead.diagnostics.join('\n').contains("Ctrl+Print"));
  }
  // SETCAT-006: portable representations are what gets stored; native values are refused or never reach the file.
  void storedValuesUsePortableRepresentationsAndNoNativeValuesLeak() {
    // Hotkeys: portable modifier/key names only; XDG accelerators, Qt key codes and platform spellings are refused.
    auto bindings = HotkeyBindings::defaults();
    for (const auto &native : {"<Control><Shift>F1", "Control+F1", "Super+F1", "Win+F1", "Cmd+F1", "Ctrl+0x70", "16777264", "Qt::Key_F1", "Ctrl+KP_Enter", "ctrl+f1", "XF86AudioPlay", "CTRL+F1"})
      QVERIFY2(!bindings.assign("capture-active-window", native).ok, native);
    QCOMPARE(normalizeTrigger("Meta+Shift+Alt+Ctrl+F1"), "Ctrl+Alt+Shift+Meta+F1");
    QVERIFY(bindings.assign("capture-active-window", "Shift+Ctrl+F1").ok);
    QCOMPARE(bindings.entries.value("capture-active-window"), "Ctrl+Shift+F1");
    for (const auto &action : captureHotkeyActions()) QVERIFY2(action.preferred.isEmpty() || normalizeTrigger(action.preferred) == action.preferred, qPrintable(action.id));
    // Action identity is a stable name or a UUID, never a numeric index or native shortcut handle.
    for (const auto &id : {"0", "1", "{capture-region}", "capture region", "Capture-Region", "capture-region\n"}) QVERIFY2(!stableActionId(id), id);
    // Paths are absolute POSIX paths; Windows spellings are not portable on this platform.
    for (const auto &path : {"C:\\Users\\me", "\\\\host\\share\\x", "file:///tmp/x", "~/x"})
      QVERIFY2(!repairSettings([&] { auto v = defaultSettings("/tmp"); v.insert("saveFolder", path); return v; }(), "/tmp").diagnostics.isEmpty(), path);
    // Through the real dialog and store: monitors and devices are saved as semantic names/IDs.
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    ApplicationSettingsStore store(path, dir.path());
    auto values = store.current().values; values.insert("systemAudioDevice", "device.name:speakers.monitor"); values.insert("microphoneDevice", "node.name:Studio mic");
    values.insert("delayedCapture.source", "monitor"); values.insert("autoCapture.source", "active-window"); QVERIFY(store.save(values).isEmpty());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {});
      for (auto *combo : dialog.findChildren<QComboBox *>()) if (combo->accessibleName() == "System audio device" || combo->accessibleName() == "Microphone")
        QVERIFY2(combo->currentIndex() >= 0 && !combo->currentData().toString().isEmpty() && combo->currentData().typeId() == QMetaType::QString, qPrintable(combo->accessibleName())); // an ID, not a list index
      auto *buttons = dialog.findChild<QDialogButtonBox *>(); buttons->button(QDialogButtonBox::Apply)->click();
    }
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
    const auto stored = QJsonDocument::fromJson(bytes).object().value("values").toObject();
    QCOMPARE(stored.value("systemAudioDevice"), QJsonValue("device.name:speakers.monitor")); QCOMPARE(stored.value("microphoneDevice"), QJsonValue("node.name:Studio mic"));
    QCOMPARE(stored.value("delayedCapture.source"), QJsonValue("monitor"));
    QCOMPARE(stored.value("saveFolder"), QJsonValue(dir.path())); QVERIFY(!dir.path().contains('\\'));
    // Only declared portable fields are stored; the saved hotkeys object holds action IDs and normalized triggers.
    QSet<QString> declared; for (const auto &field : settingFields()) declared.insert(field.key);
    for (auto it = stored.begin(); it != stored.end(); ++it) QVERIFY2(declared.contains(it.key()), qPrintable(it.key()));
    const auto savedBindings = stored.value("hotkeys").toObject();
    for (auto it = savedBindings.begin(); it != savedBindings.end(); ++it) { QVERIFY(stableActionId(it.key())); QVERIFY(it.value().isString()); QCOMPARE(normalizeTrigger(it.value().toString()), it.value().toString()); }
    // Desktop-side text (XDG accelerator syntax, window or output handles) is transient and never in the file.
    for (const char *native : {"<Control>", "<Shift>", "<Alt>", "LOGO+", "CTRL+", "winId", "xid", "wl_output", "HWND", "CGDirectDisplayID", "portal"}) QVERIFY2(!bytes.contains(native), native);
  }
  // SETCAT-009: scopes are declared, precedence is inspectable and the one resolver serves UI, CLI and automation.
  void scopeAndPrecedenceAreDeclaredInspectableAndSharedByUiAndCli() {
    const auto scopes = settingScopes();
    for (const auto &field : settingFields()) {
      QVERIFY2(scopes.contains(field.scope), qPrintable(field.key)); QVERIFY2(field.overriddenBy.isEmpty() || scopes.contains(field.overriddenBy), qPrintable(field.key));
      const auto order = precedenceOrder(field.key); QCOMPARE(order.last(), QString("default")); QVERIFY(order.contains(field.scope));
      QCOMPARE(order.size(), field.overriddenBy.isEmpty() ? 2 : 3);
    }
    QCOMPARE(precedenceOrder("saveFolder"), (QStringList{"workflow", "profile", "default"}));
    QCOMPARE(precedenceOrder("filenamePattern"), (QStringList{"workflow", "profile", "default"}));
    QCOMPARE(precedenceOrder("showTrayIcon"), (QStringList{"profile", "default"}));
    QVERIFY(precedenceOrder("not-a-setting").isEmpty());
    // The declared order is what the resolver does for every combination of explicit/blank and saved/seeded.
    const auto base = xerahs::workflows::defaultCaptureWorkflows().entries.first();
    auto values = defaultSettings("/tmp/profile"); values.insert("filenamePattern", "profile-%width");
    for (const bool explicitDirectory : {false, true}) for (const bool saved : {false, true}) {
      auto workflow = base; workflow.output.directory = explicitDirectory ? "/tmp/workflow" : QString{}; workflow.output.pattern = "workflow-%height";
      const auto where = describeOutputPrecedence(workflow, saved); const auto resolved = withApplicationDefaults(workflow, values, saved);
      QCOMPARE(where.directory, explicitDirectory ? QString("workflow") : QString("profile"));
      QCOMPARE(where.pattern, saved ? QString("workflow") : QString("profile"));
      QCOMPARE(resolved.output.directory, where.directory == "workflow" ? QString("/tmp/workflow") : QString("/tmp/profile"));
      QCOMPARE(resolved.output.pattern, where.pattern == "workflow" ? QString("workflow-%height") : QString("profile-%width"));
      QCOMPARE(withApplicationDefaults(resolved, values, saved).output.directory, resolved.output.directory); // resolving twice is stable
    }
    // CLI: --run-workflow carries only a workflow UUID; the primary resolves it with the same function as every UI capture.
    auto read = [](const QString &name) { QFile file(QStringLiteral(XERAHS_APP_DIR) + "/" + name); if (!file.open(QIODevice::ReadOnly)) return QString(); return QString::fromUtf8(file.readAll()); };
    const auto capture = read("features/CaptureFeature.cpp");
    QCOMPARE(capture.count("workflows->resolve("), 1); // only inside effectiveWorkflow()
    QVERIFY(capture.contains("choice.workflow = xerahs::settings::withApplicationDefaults(*choice.workflow,"));
    QVERIFY(capture.contains("incoming.action == ActivationAction::RunWorkflow ? effectiveWorkflow(ctx, QUuid(incoming.workflowId)) : effectiveWorkflow(ctx)"));
    QVERIFY(capture.contains("setDefaultWorkflowResolver([&ctx] { return effectiveWorkflow(ctx); })"));
    const auto main = read("main.cpp"); const auto cliPart = main.left(main.indexOf("xerahs::settings::ApplicationSettingsStore settings("));
    QVERIFY(main.contains("ApplicationSettingsStore settings(") && cliPart.contains("run-workflow"));
    for (const char *forbidden : {"withApplicationDefaults", "repairSettings", "settings::", "output.directory", "output.pattern"}) QVERIFY2(!cliPart.contains(forbidden), forbidden);
    QDir appDir(QStringLiteral(XERAHS_APP_DIR)); QStringList callers;
    for (const auto &entry : appDir.entryInfoList({"*.cpp"}, QDir::Files) + QDir(appDir.filePath("features")).entryInfoList({"*.cpp"}, QDir::Files)) {
      QFile file(entry.filePath()); QVERIFY(file.open(QIODevice::ReadOnly)); if (QString::fromUtf8(file.readAll()).contains("withApplicationDefaults(")) callers.append(entry.fileName());
    }
    callers.sort(); QCOMPARE(callers, (QStringList{"CaptureFeature.cpp", "OnboardingFeature.cpp"}));
  }
  // SETCAT-010: Recording reset previews and cancels like the other sections; a full reset only rewrites this profile's settings.
  void recordingSectionResetPreviewsCancelsAndApplies() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json"); ApplicationSettingsStore store(path, dir.path());
    auto values = store.current().values; values.insert("systemAudioDevice", "device.name:sys.monitor"); values.insert("microphoneDevice", "node.name:mic"); values.insert("future.keep", 3);
    QVERIFY(store.save(values).isEmpty());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {});
    auto *tabs = dialog.findChild<QTabWidget *>(); QCOMPARE(tabs->tabText(3), QString("&Recording")); tabs->setCurrentIndex(3);
    QComboBox *system = nullptr, *mic = nullptr;
    for (auto *combo : dialog.findChildren<QComboBox *>()) { if (combo->accessibleName() == "System audio device") system = combo; if (combo->accessibleName() == "Microphone") mic = combo; }
    QVERIFY(system && mic); QCOMPARE(system->currentData().toString(), QString("device.name:sys.monitor")); QCOMPARE(mic->currentData().toString(), QString("node.name:mic"));
    QPushButton *reset = nullptr; for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == "&Reset section…") reset = button;
    QVERIFY(reset);
    QString preview; auto answer = [&](QMessageBox::StandardButton choice) {
      QTimer::singleShot(0, [&preview, choice] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { preview = box->text(); if (auto *chosen = box->button(choice)) chosen->click(); else box->done(choice); } });
    };
    answer(QMessageBox::Cancel); reset->click();
    QVERIFY(preview.contains("systemAudioDevice (profile)")); QVERIFY(preview.contains("microphoneDevice (profile)")); QVERIFY(preview.contains("Other sections and user files are kept"));
    QVERIFY(!preview.contains("saveFolder")); QVERIFY(!preview.contains("delayedCapture")); // only this section is affected
    QCOMPARE(mic->currentData().toString(), QString("node.name:mic")); // Cancel leaves the draft untouched
    answer(QMessageBox::Reset); reset->click();
    QCOMPARE(system->currentData().toString(), QString()); QCOMPARE(mic->currentData().toString(), QString());
    QCOMPARE(store.current().values.value("microphoneDevice"), QJsonValue("node.name:mic")); // nothing persisted before Apply
    auto *buttons = dialog.findChild<QDialogButtonBox *>(); buttons->button(QDialogButtonBox::Apply)->click();
    QCOMPARE(store.current().values.value("microphoneDevice"), QJsonValue("")); QCOMPARE(store.current().values.value("systemAudioDevice"), QJsonValue(""));
    QCOMPARE(store.current().values.value("future.keep"), QJsonValue(3)); QCOMPARE(store.current().values.value("saveFolder"), values.value("saveFolder"));
    QCOMPARE(store.current().values.value("hotkeys"), values.value("hotkeys"));
  }
  void fullResetOnlyRewritesSettingsAndKeepsHistoryCredentialsPresetsWorkflowsAndFiles() {
    QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("captures")));
    // Stand-ins for the sibling stores and user files; the reset path has no handle to any of them.
    const QMap<QString, QByteArray> siblings{{"history.json", "{\"history\":[1,2,3]}"}, {"credentials.json", "{\"secret-ref\":\"keyring\"}"}, {"presets.json", "{\"presets\":[\"a\"]}"},
      {"workflows.json", "{\"workflows\":[\"w\"]}"}, {"captures/shot.png", "PNGDATA"}};
    QMap<QString, QDateTime> times; QMap<QString, QByteArray> originals;
    for (auto it = siblings.cbegin(); it != siblings.cend(); ++it) {
      QFile file(dir.filePath(it.key())); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(it.value()); file.close(); times.insert(it.key(), QFileInfo(file).lastModified());
    }
    const auto path = dir.filePath("settings.json"); ApplicationSettingsStore store(path, dir.path());
    auto values = store.current().values;
    values.insert("saveFolder", dir.filePath("captures")); values.insert("filenamePattern", "x-%width"); values.insert("playCaptureSound", true); values.insert("delayedCapture.seconds", 9);
    values.insert("scrollingCapture.maxFrames", 99); values.insert("microphoneDevice", "node.name:mic"); values.insert("future.keep", QJsonObject{{"a", 1}});
    auto bindings = values.value("hotkeys").toObject(); bindings.insert("capture-region", "F9"); values.insert("hotkeys", bindings);
    QVERIFY(store.save(values).isEmpty());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    QFile settingsFile(path); QVERIFY(settingsFile.open(QIODevice::ReadOnly)); const auto savedBytes = settingsFile.readAll(); settingsFile.close();
    auto findReset = [](QDialog &dialog) { for (auto *button : dialog.findChildren<QPushButton *>()) if (button->accessibleName() == "Reset all settings sections") return button; return static_cast<QPushButton *>(nullptr); };
    QString preview; auto answer = [&](QMessageBox::StandardButton choice) {
      QTimer::singleShot(0, [&preview, choice] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { preview = box->text(); if (auto *chosen = box->button(choice)) chosen->click(); else box->done(choice); } });
    };
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {}); auto *reset = findReset(dialog); QVERIFY(reset);
      answer(QMessageBox::Cancel); reset->click();
      for (const char *key : {"saveFolder", "filenamePattern", "playCaptureSound", "delayedCapture.seconds", "scrollingCapture.maxFrames", "microphoneDevice", "hotkeys"}) QVERIFY2(preview.contains(key), key);
      QVERIFY(preview.contains("History, credentials, presets, workflows and your files are not changed"));
      answer(QMessageBox::Reset); reset->click(); // reset the draft, then close without applying
      dialog.reject();
    }
    QVERIFY(settingsFile.open(QIODevice::ReadOnly)); QCOMPARE(settingsFile.readAll(), savedBytes); settingsFile.close(); // Cancel persisted nothing
    {
      xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {}); auto *reset = findReset(dialog);
      answer(QMessageBox::Reset); reset->click();
      dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    }
    const auto after = store.current().values; const auto defaults = defaultSettings(dir.path());
    for (const auto &field : settingFields()) QCOMPARE(after.value(field.key), defaults.value(field.key));
    QCOMPARE(after.value("future.keep"), values.value("future.keep")); // unknown fields survive a reset
    QCOMPARE(after, resetAll(values, dir.path()));
    QCOMPARE(store.previousValues(), values); // and the pre-reset profile is recoverable
    for (auto it = siblings.cbegin(); it != siblings.cend(); ++it) {
      QFile file(dir.filePath(it.key())); QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(it.key())); QCOMPARE(file.readAll(), it.value()); QCOMPARE(QFileInfo(file).lastModified(), times.value(it.key()));
    }
    QStringList names = QDir(dir.path()).entryList(QDir::Files); names.removeAll("settings.json.lock"); names.sort();
    QCOMPARE(names, (QStringList{"credentials.json", "history.json", "presets.json", "settings.json", "workflows.json"}));
    QCOMPARE(QDir(dir.filePath("captures")).entryList(QDir::Files), QStringList{"shot.png"});
  }
  // SETCAT-011: the Hotkeys page works with the keyboard alone.
  void hotkeysAreOperableByKeyboardAlone() {
    QTemporaryDir dir; ApplicationSettingsStore store(dir.filePath("settings.json"), dir.path());
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    xerahs::app::SettingsDialog dialog(store, dir.path(), plan, shortcuts, [] {});
    dialog.show(); QVERIFY(QTest::qWaitForWindowExposed(&dialog)); dialog.activateWindow(); QApplication::setActiveWindow(&dialog);
    auto *tabs = dialog.findChild<QTabWidget *>(); auto *table = dialog.findChild<QTableWidget *>(); QLineEdit *trigger = nullptr; QLabel *status = nullptr;
    for (auto *field : dialog.findChildren<QLineEdit *>()) if (field->accessibleName() == "Preferred trigger for selected action") trigger = field;
    for (auto *label : dialog.findChildren<QLabel *>()) if (label->accessibleName() == "Settings validation and conflicts") status = label;
    QVERIFY(tabs && table && trigger && status);
    auto key = [&](Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) { QWidget *target = QApplication::focusWidget() ? QApplication::focusWidget() : static_cast<QWidget *>(&dialog); QTest::keyClick(target, k, m); };
    auto rowOf = [&](const QString &id) { for (int row = 0; row < table->rowCount(); ++row) if (table->item(row, 0)->data(Qt::UserRole).toString() == id) return row; return -1; };
    auto selectByArrows = [&](const QString &id) { const int target = rowOf(id); QVERIFY(target >= 0); QVERIFY(table->hasFocus());
      for (int guard = 0; table->currentRow() != target && guard < 200; ++guard) key(table->currentRow() < target ? Qt::Key_Down : Qt::Key_Up);
      QCOMPARE(table->currentRow(), target); };
    // Reach the page: the tab bar is a focus stop and Left/Right switch pages; Tab then enters the page's first control, the action list.
    tabs->tabBar()->setFocus(); QTRY_VERIFY(tabs->tabBar()->hasFocus());
    key(Qt::Key_Right); key(Qt::Key_Right); QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("&Hotkeys"));
    for (int guard = 0; !table->hasFocus() && guard < 5; ++guard) key(Qt::Key_Tab);
    QVERIFY(table->hasFocus());
    // Arrow to an action, Enter moves to its trigger, type, Enter assigns; the dialog stays open and nothing is persisted yet.
    selectByArrows("capture-full-screen"); key(Qt::Key_Return); QVERIFY(trigger->hasFocus());
    QTest::keyClicks(trigger, "Ctrl+F1"); QCOMPARE(trigger->text(), QString("Ctrl+F1")); key(Qt::Key_Return);
    QVERIFY(dialog.isVisible()); QCOMPARE(table->item(rowOf("capture-full-screen"), 1)->text(), QString("Ctrl+F1")); QVERIFY(status->text().contains("Preference updated"));
    QVERIFY(!QFile::exists(dir.filePath("settings.json")));
    // Shift+Tab back to the list, pick a second action and ask for the same trigger: the conflict is surfaced and nothing is replaced.
    key(Qt::Key_Backtab); QVERIFY(table->hasFocus());
    selectByArrows("capture-region"); QCOMPARE(table->item(rowOf("capture-region"), 1)->text(), QString("Print"));
    key(Qt::Key_Return); QVERIFY(trigger->hasFocus()); QTest::keyClicks(trigger, "Ctrl+F1"); key(Qt::Key_Return);
    QVERIFY(dialog.isVisible()); QVERIFY(status->text().contains("hotkey-conflict")); QVERIFY(status->text().contains("capture-full-screen"));
    QVERIFY(status->text().contains("Change or clear the conflicting binding"));
    QCOMPARE(table->item(rowOf("capture-region"), 1)->text(), QString("Print")); QCOMPARE(table->item(rowOf("capture-full-screen"), 1)->text(), QString("Ctrl+F1"));
    // Escape cancels the pending edit first, and only then closes the dialog and discards the draft.
    key(Qt::Key_Escape); QVERIFY(dialog.isVisible()); QCOMPARE(trigger->text(), QString("Print")); QVERIFY(trigger->hasFocus());
    key(Qt::Key_Escape); QVERIFY(!dialog.isVisible()); QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(!QFile::exists(dir.filePath("settings.json"))); QCOMPARE(store.current().values.value("hotkeys"), defaultSettings(dir.path()).value("hotkeys"));
  }
  // SETCAT-012: a saved or imported settings object is only ever data; it cannot pre-grant consent, confirmation or credential authority.
  void savedOrImportedSettingsCannotGrantConsentConfirmationOrCredentialAuthority() {
    QTemporaryDir dir; const auto path = dir.filePath("settings.json");
    // The declared catalogue offers no field that could carry authority.
    const QRegularExpression authority("consent|permission|credential|secret|token|password|apikey|api-key|authori[sz]|cookie|bypass|skipconfirm|autoconfirm|trust|grant", QRegularExpression::CaseInsensitiveOption);
    for (const auto &field : settingFields()) QVERIFY2(!authority.match(field.key).hasMatch(), qPrintable(field.key));
    const QStringList forged{"portalConsent", "captureConsentGranted", "screencast.restoreToken", "remoteDesktop.consent", "permissions", "skipConfirmation", "confirmDestructive", "autoConfirm",
      "uploadAuthorized", "allowUploadWithoutConsent", "credentials", "oauthToken", "apiKey", "password", "cookies", "trustedExecutables", "consent"};
    auto poisoned = defaultSettings(dir.path());
    for (const auto &key : forged) poisoned.insert(key, key == "credentials" ? QJsonValue(QJsonObject{{"token", "secret-forged-ZZZZ"}}) : key == "permissions" ? QJsonValue(QJsonArray{"screen", "audio", "upload"}) : QJsonValue(true));
    auto bindings = poisoned.value("hotkeys").toObject(); bindings.insert("consent-granted", "F5"); bindings.insert("grant-portal", "F6"); poisoned.insert("hotkeys", bindings);
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(QJsonObject{{"version", 1}, {"values", poisoned}}).toJson()); file.close();
    ApplicationSettingsStore imported(path, dir.path()); const auto clean = repairSettings(defaultSettings(dir.path()), dir.path());
    QVERIFY(!imported.current().writable);
    QCOMPARE(imported.current().diagnostics, QStringList{"settings-unsafe-unknown"});
    QVERIFY(imported.current().report.count(xerahs::compatibility::ImportDisposition::Rejected) > 0);
    for (const auto &key : forged) {
      QVERIFY(!imported.current().values.contains(key));
      auto single = defaultSettings(dir.path()); single.insert(key, poisoned.value(key));
      QVERIFY2(!repairSettings(single, dir.path()).writable, qPrintable(key));
    }
    // Every consumer sees exactly what it sees with clean settings.
    const auto a = regionSessionSettings(imported.current().values), b = regionSessionSettings(clean.values);
    QCOMPARE(a.quickConfirm, b.quickConfirm); QCOMPARE(a.snapping, b.snapping);
    QCOMPARE(hotkeys(imported.current().values).entries, hotkeys(clean.values).entries); // forged action IDs never become bindings
    QVERIFY(!hotkeys(imported.current().values).entries.contains("consent-granted"));
    QVERIFY(HotkeyBindings::defaults().assign("consent-granted", "F5").diagnostic == "hotkey-action-invalid");
    const auto catalog = xerahs::workflows::defaultCaptureWorkflows();
    for (const bool saved : {false, true}) for (const auto &workflow : catalog.entries) {
      const auto x = withApplicationDefaults(workflow, imported.current().values, saved), y = withApplicationDefaults(workflow, clean.values, saved);
      QCOMPARE(x.actions, y.actions); QCOMPARE(x.capture, y.capture); QCOMPARE(x.chooseActions, y.chooseActions);
      QCOMPARE(x.output.directory, y.output.directory); QCOMPARE(x.output.pattern, y.output.pattern); QCOMPARE(x.actions, workflow.actions); QCOMPARE(x.chooseActions, workflow.chooseActions);
    }
    // COMPCAT-005 rejects authority-bearing unknowns; the original file remains recoverable, never an active settings candidate.
    QVERIFY(file.open(QIODevice::ReadOnly)); const auto original = file.readAll(); file.close();
    QVERIFY(!imported.save(defaultSettings(dir.path())).isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
    auto reset = resetAll(imported.current().values, dir.path()); for (const auto &key : forged) QVERIFY(!reset.contains(key));
    for (const auto &field : settingFields()) QCOMPARE(repairSettings(reset, dir.path()).fields.value(field.key), FieldState::Valid);
    auto bad = poisoned; bad.insert("showNotification", "secret-forged-ZZZZ"); const auto read = repairSettings(bad, dir.path());
    QVERIFY(!read.diagnostics.join('\n').contains("ZZZZ")); QVERIFY(!read.diagnostics.join('\n').contains("credentials"));
    // The settings window shows no control that could grant or remove consent, confirmation or credentials.
    xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    const auto plan = xerahs::workflows::captureShortcutPlan(xerahs::workflows::defaultCaptureWorkflows(), xerahs::workflows::implementedCaptureActions(), HotkeyBindings::defaults());
    xerahs::app::SettingsDialog dialog(imported, dir.path(), plan, shortcuts, [] {});
    for (auto *widget : dialog.findChildren<QWidget *>()) QVERIFY2(!authority.match(widget->accessibleName()).hasMatch(), qPrintable(widget->accessibleName()));
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
    for (const auto &key : forged) QVERIFY(!imported.current().values.contains(key)); // read-only Apply grants no authority
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original);
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
