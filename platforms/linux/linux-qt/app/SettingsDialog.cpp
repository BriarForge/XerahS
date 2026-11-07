#include "NativeAccessibility.h"
#include "NativeWindowPlacement.h"
#include "SettingsDialog.h"
#include "settings/ReferenceMigration.h"
#include "RecordingDevices.h"
#include "XdgAutostart.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QFile>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace xerahs::app {
using namespace xerahs::settings;
namespace {
// CORE-009: every user-visible Preferences string goes through the translator under one context.
QString text(const char *source) { return QCoreApplication::translate("SettingsDialog", source); }
}  // namespace
namespace {
// Keyboard-only Hotkeys use (SETCAT-011): a QDialog would treat a bare Enter as OK.
class KeyFilter final : public QObject {
public:
  KeyFilter(QObject *parent, std::function<bool(QKeyEvent *)> handler) : QObject(parent), m_handler(std::move(handler)) {}
  bool eventFilter(QObject *, QEvent *event) override {
    return event->type() == QEvent::KeyPress && m_handler(static_cast<QKeyEvent *>(event));
  }
private:
  std::function<bool(QKeyEvent *)> m_handler;
};
QSpinBox *spin(QWidget *parent, const QString &name, int minimum, int maximum, const QString &suffix = {}) {
  auto *box = new QSpinBox(parent); box->setRange(minimum, maximum); box->setSuffix(suffix); box->setAccessibleName(name);
  box->setKeyboardTracking(false); if (auto *edit = box->findChild<QLineEdit *>()) edit->setAccessibleName(name); return box;
}
QComboBox *choice(QWidget *parent, const QString &name, const QVector<QPair<QString, QString>> &items) {
  auto *box = new QComboBox(parent); box->setAccessibleName(name);
  for (const auto &item : items) box->addItem(item.first, item.second);
  return box;
}
void select(QComboBox *box, const QJsonValue &value) { const int index = box->findData(value.toString()); if (index >= 0) box->setCurrentIndex(index); }
}
SettingsDialog::SettingsDialog(ApplicationSettingsStore &store, QString defaultFolder,
    const xerahs::workflows::WorkflowShortcutPlan &plan, WorkflowShortcutActions &shortcuts,
    std::function<void()> applied, QWidget *parent, ConfigurationPaths transferPaths,
    std::function<void()> imported) : QDialog(parent) {
  new NativeWindowPlacement(*this);
  new NativeAccessibility(*this, text("Review settings sections and validation. Apply saves the draft; Cancel discards unapplied edits."), false);
  setWindowTitle(text("XerahS Settings")); setAccessibleName(text("XerahS Settings")); setModal(true); resize(820, 520);
  auto draft = std::make_shared<QJsonObject>(store.current().values);
  auto *layout = new QVBoxLayout(this);
  auto *scope = new QLabel(text("All preferences apply to this user profile. Saved workflow filename patterns and explicit folders override application defaults. Hotkeys are preferred triggers only; the desktop and user decide the final binding."), this);
  scope->setWordWrap(true); layout->addWidget(scope);
  auto *report = new QLabel(store.current().report.summary(), this);
  report->setObjectName(QStringLiteral("settingsImportReport")); report->setTextFormat(Qt::PlainText);
  report->setWordWrap(true); report->setAccessibleName(text("Settings import report")); layout->addWidget(report);
  auto *tabs = new QTabWidget(this); tabs->setAccessibleName(text("Settings sections")); layout->addWidget(tabs);
  auto *general = new QWidget(tabs); auto *form = new QFormLayout(general);
  auto *generalScroll = new QScrollArea(tabs); generalScroll->setAccessibleName(text("General settings")); generalScroll->setFocusPolicy(Qt::NoFocus); generalScroll->setWidgetResizable(true); generalScroll->setFrameShape(QFrame::NoFrame);
  generalScroll->setWidget(general); tabs->addTab(generalScroll, text("&General"));
  auto *nativeChoices = new QLabel(text("Native accessibility: desktop input, font scaling, notification delivery and Do Not Disturb remain controlled by your desktop. "
      "Editor Appearance can follow desktop theme, high contrast and reduced motion without changing saved desktop choices. "
      "Degraded: reduced-contrast colour substitutions are unavailable; the selected native/editor theme is retained. "
      "When desktop appearance hints are unavailable, XerahS retains Qt's native palette and existing motion settings."), general);
  nativeChoices->setObjectName("nativeAccessibilityDisposition"); nativeChoices->setWordWrap(true);
  nativeChoices->setTextFormat(Qt::PlainText); nativeChoices->setAccessibleName(text("Native accessibility preferences and degraded appearance support"));
  nativeChoices->setAccessibleDescription(nativeChoices->text()); form->addRow(nativeChoices);
  auto *folder = new QLineEdit(general); folder->setMaxLength(32768); folder->setAccessibleName(text("Default save folder")); form->addRow(text("&Default save folder"), folder);
  auto *browse = new QPushButton(text("&Browse…"), general); form->addRow(browse);
  connect(browse, &QPushButton::clicked, this, [this, folder] {
    const auto path = QFileDialog::getExistingDirectory(this, text("Default save folder"), folder->text()); if (!path.isEmpty()) folder->setText(path);
  });
  auto *pattern = new QLineEdit(general); pattern->setMaxLength(4096); pattern->setAccessibleName(text("Filename pattern")); form->addRow(text("&Filename pattern"), pattern);
  auto *login = new QCheckBox(text("Start XerahS at &login"), general); login->setAccessibleName(text("Start XerahS at login")); form->addRow(login);
  auto *tray = new QCheckBox(text("Show &tray icon"), general); tray->setAccessibleName(text("Show tray icon")); form->addRow(tray);
  auto *exportSettings = new QPushButton(text("Export settings…"), general);
  exportSettings->setObjectName("exportSettings"); exportSettings->setAccessibleName(exportSettings->text()); form->addRow(exportSettings);
  auto *importSettings = new QPushButton(text("Import settings…"), general);
  importSettings->setObjectName("importSettings"); importSettings->setAccessibleName(importSettings->text()); form->addRow(importSettings);
  exportSettings->setEnabled(transferPaths.configured()); importSettings->setEnabled(transferPaths.configured());
  auto *migrateSettings = new QPushButton(text("Import from XerahS (previous app)…"), general);
  migrateSettings->setObjectName("migrateReferenceSettings"); migrateSettings->setAccessibleName(migrateSettings->text()); form->addRow(migrateSettings);
  migrateSettings->setEnabled(transferPaths.configured());
  auto *restoreSettings = new QPushButton(text("Restore previous settings…"), general);
  restoreSettings->setObjectName("restorePreviousSettings"); restoreSettings->setAccessibleName(restoreSettings->text()); form->addRow(restoreSettings);
  auto *dismissBackup = new QPushButton(text("Dismiss previous settings backup…"), general);
  dismissBackup->setObjectName("dismissMigrationBackup"); dismissBackup->setAccessibleName(dismissBackup->text()); form->addRow(dismissBackup);
  auto refreshBackup = [=] { const bool available = transferPaths.configured() && ReferenceMigration(transferPaths).backupAvailable(); restoreSettings->setEnabled(available); dismissBackup->setEnabled(available); };
  refreshBackup();
  auto *transferNote = new QLabel(text("Transfer saved application preferences, editor preferences, workflows and destinations. "
      "Exports omit credentials, local folders, devices, recent paths and window placement. Import replaces these four stores after preview; history and user files are kept."), general);
  transferNote->setWordWrap(true); form->addRow(transferNote);
  auto *capture = new QWidget(tabs); auto *captureForm = new QFormLayout(capture);
  auto *captureScroll = new QScrollArea(tabs); captureScroll->setFocusPolicy(Qt::NoFocus); captureScroll->setWidgetResizable(true); captureScroll->setFrameShape(QFrame::NoFrame);
  captureScroll->setWidget(capture); tabs->addTab(captureScroll, text("&Capture"));
  auto *sound = new QCheckBox(text("Play capture &sound"), capture); sound->setAccessibleName(text("Play capture sound")); captureForm->addRow(sound);
  auto *notification = new QCheckBox(text("Show &notification after capture"), capture); notification->setAccessibleName(text("Show notification after capture")); captureForm->addRow(notification);
  auto *privateNotification = new QCheckBox(text("Hide &private details in notifications"), capture);
  privateNotification->setAccessibleName(text("Hide private details in notifications"));
  privateNotification->setToolTip(text("Notifications show only the operation and its result. Paths, URLs and recognized text stay in the XerahS window."));
  captureForm->addRow(privateNotification);
  auto *quickConfirm = new QCheckBox(text("Confirm region on pointer &release"), capture);
  quickConfirm->setAccessibleName(text("Confirm region on pointer release")); captureForm->addRow(quickConfirm);
  auto *snapping = new QCheckBox(text("Snap clicks to detected &windows or controls"), capture);
  snapping->setAccessibleName(text("Snap clicks to detected windows or controls")); captureForm->addRow(snapping);
  auto *regionHelp = new QLabel(text("Region options apply to the next session. Disable quick confirmation to adjust after release, then press Enter to capture. Arrow keys move by 1 physical pixel; Shift+Arrow by 10; Alt+Arrow resizes. Space starts and finishes a keyboard selection. Escape cancels. Drag freely to override snapping; discovery depends on desktop support."), capture);
  regionHelp->setWordWrap(true); captureForm->addRow(regionHelp);
  const QVector<QPair<QString, QString>> sources{{text("Full screen"), "full-screen"}, {text("Current monitor"), "monitor"}, {text("Active window"), "active-window"}};
  auto *delayedBox = new QGroupBox(text("Delayed capture"), capture); auto *delayedForm = new QFormLayout(delayedBox); captureForm->addRow(delayedBox);
  auto *delaySeconds = spin(delayedBox, text("Delayed capture seconds"), 1, 60, " s");
  auto delaySources = sources; delaySources.append(qMakePair(text("Region"), QString("region")));
  auto *delaySource = choice(delayedBox, text("Delayed capture source"), delaySources);
  delayedForm->addRow(text("Delay &seconds"), delaySeconds); delayedForm->addRow(text("Capture &source"), delaySource);
  auto *autoBox = new QGroupBox(text("Automatic capture"), capture); auto *autoForm = new QFormLayout(autoBox); captureForm->addRow(autoBox);
  auto *autoSource = choice(autoBox, text("Automatic capture source"), sources);
  auto *autoInterval = spin(autoBox, text("Automatic capture interval seconds"), 1, 24 * 60 * 60, " s");
  auto *autoKind = choice(autoBox, text("Automatic capture bound"), {{text("Number of captures"), "count"}, {text("Duration"), "time"}});
  auto *autoCount = spin(autoBox, text("Automatic capture count"), 1, 10000);
  auto *autoDuration = spin(autoBox, text("Automatic capture duration minutes"), 1, 1440, " min");
  autoForm->addRow(text("Capture s&ource"), autoSource); autoForm->addRow(text("&Interval"), autoInterval); autoForm->addRow(text("Stop &after"), autoKind);
  autoForm->addRow(text("Number of captures"), autoCount); autoForm->addRow(text("Duration"), autoDuration);
  auto syncBound = [=] { const bool count = autoKind->currentData().toString() == "count"; autoCount->setEnabled(count); autoDuration->setEnabled(!count); };
  connect(autoKind, &QComboBox::currentIndexChanged, this, syncBound);
  auto *scrollBox = new QGroupBox(text("Scrolling capture"), capture); auto *scrollForm = new QFormLayout(scrollBox); captureForm->addRow(scrollBox);
  auto *scrollSettle = spin(scrollBox, text("Scrolling capture settle delay milliseconds"), 0, 5000, " ms");
  auto *scrollSteps = spin(scrollBox, text("Scrolling capture wheel steps per frame"), 1, 50);
  auto *scrollHeader = spin(scrollBox, text("Scrolling capture header band pixels"), 0, 2000, " px");
  auto *scrollFooter = spin(scrollBox, text("Scrolling capture footer band pixels"), 0, 2000, " px");
  auto *scrollHeight = spin(scrollBox, text("Scrolling capture maximum height pixels"), 1000, 100000, " px");
  auto *scrollFrames = spin(scrollBox, text("Scrolling capture maximum frames"), 2, 500);
  auto *scrollPointer = new QCheckBox(text("&Keep pointer over the region"), scrollBox); scrollPointer->setAccessibleName(text("Keep pointer over the scrolling region"));
  scrollForm->addRow(text("Settle &delay"), scrollSettle); scrollForm->addRow(text("&Wheel steps per frame"), scrollSteps);
  scrollForm->addRow(text("&Header to skip"), scrollHeader); scrollForm->addRow(text("F&ooter to skip"), scrollFooter);
  scrollForm->addRow(text("Maximum &height"), scrollHeight); scrollForm->addRow(text("Maximum &frames"), scrollFrames); scrollForm->addRow(scrollPointer);
  auto *hotkeyPage = new QWidget(tabs); auto *hotkeyLayout = new QVBoxLayout(hotkeyPage); tabs->addTab(hotkeyPage, text("&Hotkeys"));
  auto *table = new QTableWidget(hotkeyPage); table->setColumnCount(3); table->setHorizontalHeaderLabels({text("Action"), text("Preferred trigger"), text("Currently bound by desktop")});
  table->setAccessibleName(text("Capture and workflow hotkeys")); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); hotkeyLayout->addWidget(table);
  auto *trigger = new QLineEdit(hotkeyPage); trigger->setAccessibleName(text("Preferred trigger for selected action")); trigger->setPlaceholderText(text("Ctrl+Print, Shift+F1, or blank to clear; modifiers Ctrl, Alt, Shift, Meta")); hotkeyLayout->addWidget(trigger);
  auto *assign = new QPushButton(text("Set &preferred trigger"), hotkeyPage); hotkeyLayout->addWidget(assign);
  auto *replace = new QPushButton(text("&Replace conflicting binding"), hotkeyPage); replace->setEnabled(false);
  replace->setToolTip(text("Moves the trigger to the selected action and clears the action that held it."));
  hotkeyLayout->addWidget(replace);
  auto *desktop = new QPushButton(text("Change in &desktop settings… (closes unapplied draft)"), hotkeyPage); hotkeyLayout->addWidget(desktop);
  auto *status = new QLabel(this); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); status->setAccessibleName(text("Settings validation and conflicts"));
  status->setText((store.current().diagnostics + (plan.diagnostic.isEmpty() ? QStringList{} : QStringList{plan.diagnostic + text(": reload Capture workflows before enabling shortcuts")})).join('\n')); layout->addWidget(status);
  auto refresh = [=, &shortcuts] {
    const QSignalBlocker blocker(table);
    folder->setText(draft->value("saveFolder").toString()); pattern->setText(draft->value("filenamePattern").toString());
    login->setChecked(draft->value("startAtLogin").toBool()); tray->setChecked(draft->value("showTrayIcon").toBool());
    quickConfirm->setChecked(draft->value("regionCapture.quickConfirm").toBool());
    snapping->setChecked(draft->value("regionCapture.snapping").toBool());
    sound->setChecked(draft->value("playCaptureSound").toBool()); notification->setChecked(draft->value("showNotification").toBool());
    privateNotification->setChecked(draft->value("privateNotifications").toBool());
    delaySeconds->setValue(draft->value("delayedCapture.seconds").toInt()); select(delaySource, draft->value("delayedCapture.source"));
    select(autoSource, draft->value("autoCapture.source")); autoInterval->setValue(draft->value("autoCapture.intervalMs").toInt() / 1000);
    select(autoKind, draft->value("autoCapture.boundKind")); autoCount->setValue(draft->value("autoCapture.count").toInt());
    autoDuration->setValue(draft->value("autoCapture.durationMinutes").toInt()); syncBound();
    scrollSettle->setValue(draft->value("scrollingCapture.settleMs").toInt()); scrollSteps->setValue(draft->value("scrollingCapture.wheelSteps").toInt());
    scrollHeader->setValue(draft->value("scrollingCapture.headerBand").toInt()); scrollFooter->setValue(draft->value("scrollingCapture.footerBand").toInt());
    scrollHeight->setValue(draft->value("scrollingCapture.maxHeight").toInt()); scrollFrames->setValue(draft->value("scrollingCapture.maxFrames").toInt());
    scrollPointer->setChecked(draft->value("scrollingCapture.positionPointer").toBool());
    table->setRowCount(plan.entries.size()); int row = 0;
    for (const auto &entry : plan.entries) {
      auto *item = new QTableWidgetItem(entry.description); item->setData(Qt::UserRole, entry.id); table->setItem(row, 0, item);
      table->setItem(row, 1, new QTableWidgetItem(hotkeys(*draft).entries.value(entry.id)));
      table->setItem(row++, 2, new QTableWidgetItem(shortcuts.bindings().value(entry.id, text("Unbound in this session"))));
    }
    if (table->rowCount()) { table->selectRow(0); trigger->setText(table->item(0, 1)->text()); }
  };
  auto collect = [=] {
    draft->insert("saveFolder", folder->text()); draft->insert("filenamePattern", pattern->text());
    draft->insert("startAtLogin", login->isChecked()); draft->insert("showTrayIcon", tray->isChecked());
    draft->insert("regionCapture.quickConfirm", quickConfirm->isChecked());
    draft->insert("regionCapture.snapping", snapping->isChecked());
    draft->insert("playCaptureSound", sound->isChecked()); draft->insert("showNotification", notification->isChecked());
    draft->insert("privateNotifications", privateNotification->isChecked());
    draft->insert("delayedCapture.seconds", delaySeconds->value()); draft->insert("delayedCapture.source", delaySource->currentData().toString());
    draft->insert("autoCapture.source", autoSource->currentData().toString());
    // Sub-second stored intervals are kept until the user edits the field.
    if (autoInterval->value() != draft->value("autoCapture.intervalMs").toInt() / 1000) draft->insert("autoCapture.intervalMs", autoInterval->value() * 1000);
    draft->insert("autoCapture.boundKind", autoKind->currentData().toString()); draft->insert("autoCapture.count", autoCount->value());
    draft->insert("autoCapture.durationMinutes", autoDuration->value());
    draft->insert("scrollingCapture.settleMs", scrollSettle->value()); draft->insert("scrollingCapture.wheelSteps", scrollSteps->value());
    draft->insert("scrollingCapture.headerBand", scrollHeader->value()); draft->insert("scrollingCapture.footerBand", scrollFooter->value());
    draft->insert("scrollingCapture.maxHeight", scrollHeight->value()); draft->insert("scrollingCapture.maxFrames", scrollFrames->value());
    draft->insert("scrollingCapture.positionPointer", scrollPointer->isChecked());
  };
  connect(table, &QTableWidget::itemSelectionChanged, this, [=] { const int row = table->currentRow(); if (row >= 0 && table->item(row, 1)) trigger->setText(table->item(row, 1)->text()); });
  connect(assign, &QPushButton::clicked, this, [=] {
    const int row = table->currentRow(); if (row < 0) return;
    auto bindings = hotkeys(*draft); const auto id = table->item(row, 0)->data(Qt::UserRole).toString();
    const auto result = bindings.assign(id, trigger->text());
    replace->setEnabled(false);
    if (!result.ok) {
      if (!result.reservedBy.isEmpty()) { status->setText(result.diagnostic + ": " + result.reservedBy + text(". Choose another trigger or change the desktop shortcut.")); return; }
      replace->setEnabled(!result.conflictingAction.isEmpty());
      status->setText(result.diagnostic + (result.conflictingAction.isEmpty() ? "" : ": " + result.conflictingAction) + text(". Change or clear the conflicting binding, then retry; or press Replace to move the trigger here (keep: choose another trigger; cancel: Escape)."));
      return;
    }
    auto object = draft->value("hotkeys").toObject(); object.insert(id, bindings.entries.value(id)); draft->insert("hotkeys", object);
    table->item(row, 1)->setText(bindings.entries.value(id)); status->setText(text("Preference updated in draft. Apply to save; change in desktop settings to request binding."));
  });
  connect(replace, &QPushButton::clicked, this, [=] {
    const int row = table->currentRow(); if (row < 0) return;
    auto bindings = hotkeys(*draft); const auto id = table->item(row, 0)->data(Qt::UserRole).toString();
    const auto result = bindings.reassign(id, trigger->text()); replace->setEnabled(false);
    if (!result.ok) { status->setText(result.diagnostic); return; }
    draft->insert("hotkeys", [&] { QJsonObject o; for (auto it = bindings.entries.cbegin(); it != bindings.entries.cend(); ++it) o.insert(it.key(), it.value()); return o; }());
    for (int r = 0; r < table->rowCount(); ++r) if (auto *cell = table->item(r, 1)) cell->setText(bindings.entries.value(table->item(r, 0)->data(Qt::UserRole).toString()));
    status->setText(text("Trigger moved from %1 to the selected action in draft. Apply to save.").arg(result.conflictingAction));
  });
  // Enter on an action moves to its trigger; Enter on the trigger assigns it; Escape on an
  // edited trigger cancels the edit (a second Escape closes the dialog and discards the draft).
  table->installEventFilter(new KeyFilter(table, [=](QKeyEvent *event) {
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) return false;
    trigger->setFocus(); trigger->selectAll(); return true;
  }));
  trigger->installEventFilter(new KeyFilter(trigger, [=](QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { assign->click(); return true; }
    const int row = table->currentRow();
    if (event->key() == Qt::Key_Escape && row >= 0 && table->item(row, 1) && trigger->text() != table->item(row, 1)->text()) {
      trigger->setText(table->item(row, 1)->text()); status->setText(text("Edit cancelled; the preferred trigger is unchanged.")); return true;
    }
    return false;
  }));
  connect(desktop, &QPushButton::clicked, this, [this, &shortcuts] {
    // Close the modal first: setup uses the same busy/modal admission as capture.
    reject(); QTimer::singleShot(0, &shortcuts, [&shortcuts] { shortcuts.changeInDesktopSettings(); });
  });
  connect(&shortcuts, &WorkflowShortcutActions::bindingsChanged, this, [=, &shortcuts] {
    for (int row = 0; row < table->rowCount(); ++row) table->item(row, 2)->setText(shortcuts.bindings().value(table->item(row, 0)->data(Qt::UserRole).toString(), text("Unbound in this session")));
  });
  auto *recordingPage = new QWidget(tabs); auto *recordingForm = new QFormLayout(recordingPage); tabs->addTab(recordingPage, text("&Recording"));
  RecordingDevices devices;
  auto *systemAudio = new QComboBox(recordingPage); auto *microphone = new QComboBox(recordingPage);
  for (auto *combo : {systemAudio, microphone}) {
    combo->addItem(text("None"), QString{});
    for (const auto &device : devices.list()) combo->addItem(device.label + (device.monitor ? text(" (system monitor)") : ""), device.id);
  }
  systemAudio->setAccessibleName(text("System audio device")); microphone->setAccessibleName(text("Microphone"));
  recordingForm->addRow(text("System audio device"), systemAudio); recordingForm->addRow(text("Microphone"), microphone);
  auto refreshAudio = [=] {
    for (const auto &entry : {qMakePair(systemAudio, QString("systemAudioDevice")), qMakePair(microphone, QString("microphoneDevice"))}) {
      const auto id = draft->value(entry.second).toString();
      if (entry.first->findData(id) < 0) entry.first->addItem(text("Missing device: ") + id, id);
      const QSignalBlocker blocker(entry.first); entry.first->setCurrentIndex(entry.first->findData(id));
    }
  };
  refreshAudio();
  connect(systemAudio, &QComboBox::currentIndexChanged, this, [=] { draft->insert("systemAudioDevice", systemAudio->currentData().toString()); });
  connect(microphone, &QComboBox::currentIndexChanged, this, [=] { draft->insert("microphoneDevice", microphone->currentData().toString()); });
  auto *audioNote = new QLabel(text("Only these explicit inputs are captured. A missing input produces video without audio and a notification. GIF never contains audio."), recordingPage);
  audioNote->setWordWrap(true); recordingForm->addRow(audioNote);
  auto *resetRow = new QHBoxLayout(); layout->addLayout(resetRow);
  auto *reset = new QPushButton(text("&Reset section…"), this); resetRow->addWidget(reset);
  auto *resetEverything = new QPushButton(text("Reset &all sections…"), this); resetEverything->setAccessibleName(text("Reset all settings sections")); resetRow->addWidget(resetEverything); resetRow->addStretch();
  // SETCAT-010: both resets only change this profile's draft after a preview; Cancel discards them.
  auto resetDraft = [=](bool everything) {
    collect(); const auto section = static_cast<Section>(tabs->currentIndex());
    const auto changes = everything ? resetAllPreview(*draft, defaultFolder) : resetPreview(*draft, section, defaultFolder);
    if (changes.isEmpty()) { status->setText(everything ? text("All sections already have default values.") : text("This section already has default values.")); return; }
    const QString kept = everything ? text("History, credentials, presets, workflows and your files are not changed.") : text("Other sections and user files are kept.");
    if (QMessageBox::question(this, everything ? text("Reset all profile sections") : text("Reset profile section"), text("Preview of changes (") + kept + text("):\n") + changes.join('\n') + text("\n\nReset this draft? Apply saves it; Cancel discards it."), QMessageBox::Reset | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Reset) return;
    *draft = everything ? resetAll(*draft, defaultFolder) : resetSection(*draft, section, defaultFolder); refresh(); refreshAudio();
  };
  connect(reset, &QPushButton::clicked, this, [=] { resetDraft(false); });
  connect(resetEverything, &QPushButton::clicked, this, [=] { resetDraft(true); });
  // Owned nonblocking pickers/preview: owner deletion cancels without publishing.
  auto published = [=, &store, &shortcuts] {
    store = ApplicationSettingsStore(transferPaths.application, defaultFolder);
    *draft = store.current().values; refresh(); refreshAudio(); refreshBackup();
    QPointer<SettingsDialog> owner(this);
    shortcuts.invalidate(); if (!owner) return;
    if (imported) imported();
    if (!owner) return;
    if (applied) applied();
    if (!owner) return;
    status->setText(text("Settings imported or restored. Credentials were not imported. Preferred hotkeys need desktop confirmation."));
  };
  auto previewTransfer = [=](const ConfigurationPlan &plan, const QString &title, const QString &description, std::function<QString()> commit) {
    report->setText(plan.report.detailedSummary() + "\n" + plan.code);
    if (!plan.valid()) { status->setText(plan.code); return; }
    auto *confirm = new QMessageBox(QMessageBox::Question, title,
        description + text("\nUnapplied preferences will be discarded. History, credentials and user files are kept. "
        "Secrets must be authorized separately.\n") + plan.report.detailedSummary(), QMessageBox::Apply | QMessageBox::Cancel, this);
    confirm->setTextFormat(Qt::PlainText); confirm->setDetailedText(plan.preview()); confirm->setDefaultButton(QMessageBox::Cancel);
    confirm->setObjectName("configurationImportPreview"); confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm, &QMessageBox::finished, this, [=](int result) {
      if (result != QMessageBox::Apply) return;
      const auto code = commit(); refreshBackup();
      if (!code.isEmpty()) { status->setText(code); return; }
      published();
    });
    confirm->open();
  };
  auto transfer = [=](bool importing) {
    auto *picker = new QFileDialog(this, importing ? text("Import settings") : text("Export saved settings"));
    picker->setObjectName("configurationFilePicker"); picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setAcceptMode(importing ? QFileDialog::AcceptOpen : QFileDialog::AcceptSave);
    picker->setFileMode(importing ? QFileDialog::ExistingFile : QFileDialog::AnyFile);
    picker->setNameFilter(text("XerahS configuration") + " (*.json)"); picker->setDefaultSuffix("json"); picker->setWindowModality(Qt::WindowModal);
    connect(picker, &QFileDialog::fileSelected, this, [=](const QString &path) {
      ConfigurationTransfer transfer(transferPaths);
      if (!importing) {
        compatibility::ExportReport omissions; const auto code = transfer.exportFile(path, &omissions);
        status->setText(code.isEmpty() ? text("Saved settings exported. Omissions: ") + omissions.omissions.join(", ") : code); return;
      }
      QFile input(path);
      if (!input.open(QIODevice::ReadOnly) || input.size() > 8 * 1024 * 1024) { status->setText("configuration-read-failed-or-too-large"); return; }
      const auto plan = transfer.inspect(input.read(8 * 1024 * 1024 + 1));
      previewTransfer(plan, text("Import settings preview"), text("Replace saved application, editor, workflow and destination settings?"),
          [=] { return ConfigurationTransfer(transferPaths).apply(plan, true); });
    });
    picker->open();
  };
  connect(exportSettings, &QPushButton::clicked, this, [transfer] { transfer(false); });
  connect(importSettings, &QPushButton::clicked, this, [transfer] { transfer(true); });
  connect(migrateSettings, &QPushButton::clicked, this, [=] {
    auto *picker = new QFileDialog(this, text("Choose previous XerahS settings folder"), ReferenceMigration::defaultSourceFolder());
    picker->setObjectName("referenceSettingsFolderPicker"); picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setFileMode(QFileDialog::Directory); picker->setOption(QFileDialog::ShowDirsOnly); picker->setWindowModality(Qt::WindowModal);
    connect(picker, &QFileDialog::fileSelected, this, [=](const QString &folder) {
      const auto plan = ReferenceMigration(transferPaths).inspectFolder(folder);
      if (plan.completed) { status->setText("migration-already-completed: source fingerprint matches; no changes or duplicates."); return; }
      previewTransfer(plan.configuration, text("Import from previous XerahS preview"),
          text("Merge supported previous-app settings? Unsupported records stay in the source. A recoverable native-settings backup will be retained until you dismiss it."),
          [=] { return ReferenceMigration(transferPaths).apply(folder, plan, true); });
    });
    picker->open();
  });
  auto backupAction = [=](bool restore) {
    const auto revision = ReferenceMigration(transferPaths).backupRevision();
    if (revision.isEmpty()) { status->setText("migration-backup-unavailable"); return; }
    auto *confirm = new QMessageBox(QMessageBox::Question, restore ? text("Restore previous settings") : text("Dismiss previous settings backup"),
        restore ? text("Replace current application, editor, workflow and destination settings with the saved pre-migration backup? History, credentials and user files are kept.")
                : text("Permanently discard the pre-migration settings backup? The migration completion record is retained to prevent duplicates."),
        QMessageBox::Apply | QMessageBox::Cancel, this);
    confirm->setObjectName("migrationBackupConfirmation"); confirm->setDefaultButton(QMessageBox::Cancel); confirm->setAttribute(Qt::WA_DeleteOnClose);
    connect(confirm, &QMessageBox::finished, this, [=](int result) {
      if (result != QMessageBox::Apply) return;
      const auto code = restore ? ReferenceMigration(transferPaths).restore(true, revision) : ReferenceMigration(transferPaths).dismissBackup(true, revision);
      refreshBackup(); if (!code.isEmpty()) { status->setText(code); return; }
      if (restore) published(); else status->setText(text("Previous settings backup dismissed. Completion detection remains available."));
    }); confirm->open();
  };
  connect(restoreSettings, &QPushButton::clicked, this, [=] { backupAction(true); });
  connect(dismissBackup, &QPushButton::clicked, this, [=] { backupAction(false); });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this); layout->addWidget(buttons);
  auto save = [=, &store, &shortcuts]() {
    collect(); const auto repaired = repairSettings(*draft, defaultFolder);
    if (!repaired.diagnostics.isEmpty()) { status->setText(repaired.diagnostics.join('\n')); return false; }
    const bool oldLogin = store.current().values.value("startAtLogin").toBool();
    const bool newLogin = draft->value("startAtLogin").toBool();
    QString error;
    if (oldLogin != newLogin) error = setAutostart(newLogin, QCoreApplication::applicationFilePath());
    if (!error.isEmpty()) { status->setText(error); return false; }
    const bool hotkeysChanged = store.current().values.value("hotkeys") != draft->value("hotkeys");
    error = store.save(*draft);
    if (!error.isEmpty()) {
      if (oldLogin != newLogin) { const auto rollback = setAutostart(oldLogin, QCoreApplication::applicationFilePath()); if (!rollback.isEmpty()) error += "\n" + rollback; }
      status->setText(error); return false;
    }
    if (hotkeysChanged) shortcuts.invalidate();
    if (applied) applied();
    status->setText(text("Settings saved. Preferred hotkeys require desktop confirmation; use Change in desktop settings.")); return true;
  };
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [save] { save(); });
  connect(buttons, &QDialogButtonBox::accepted, this, [this, save] { if (save()) accept(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  refresh();
}
}
