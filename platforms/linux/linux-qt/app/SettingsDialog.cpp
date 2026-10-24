#include "SettingsDialog.h"
#include "RecordingDevices.h"
#include "XdgAutostart.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace xerahs::app {
using namespace xerahs::settings;
namespace {
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
    std::function<void()> applied, QWidget *parent) : QDialog(parent) {
  setWindowTitle("XerahS Settings"); setAccessibleName("XerahS Settings"); setModal(true); resize(820, 520);
  auto draft = std::make_shared<QJsonObject>(store.current().values);
  auto *layout = new QVBoxLayout(this);
  auto *scope = new QLabel("All preferences apply to this user profile. Saved workflow filename patterns and explicit folders override application defaults. Hotkeys are preferred triggers only; the desktop and user decide the final binding.", this);
  scope->setWordWrap(true); layout->addWidget(scope);
  auto *tabs = new QTabWidget(this); tabs->setAccessibleName("Settings sections"); layout->addWidget(tabs);
  auto *general = new QWidget(tabs); auto *form = new QFormLayout(general); tabs->addTab(general, "&General");
  auto *folder = new QLineEdit(general); folder->setMaxLength(32768); folder->setAccessibleName("Default save folder"); form->addRow("&Default save folder", folder);
  auto *browse = new QPushButton("&Browse…", general); form->addRow(browse);
  connect(browse, &QPushButton::clicked, this, [this, folder] {
    const auto path = QFileDialog::getExistingDirectory(this, "Default save folder", folder->text()); if (!path.isEmpty()) folder->setText(path);
  });
  auto *pattern = new QLineEdit(general); pattern->setMaxLength(4096); pattern->setAccessibleName("Filename pattern"); form->addRow("&Filename pattern", pattern);
  auto *login = new QCheckBox("Start XerahS at &login", general); login->setAccessibleName("Start XerahS at login"); form->addRow(login);
  auto *tray = new QCheckBox("Show &tray icon", general); tray->setAccessibleName("Show tray icon"); form->addRow(tray);
  auto *capture = new QWidget(tabs); auto *captureForm = new QFormLayout(capture); tabs->addTab(capture, "&Capture");
  auto *sound = new QCheckBox("Play capture &sound", capture); sound->setAccessibleName("Play capture sound"); captureForm->addRow(sound);
  auto *notification = new QCheckBox("Show &notification after capture", capture); notification->setAccessibleName("Show notification after capture"); captureForm->addRow(notification);
  const QVector<QPair<QString, QString>> sources{{"Full screen", "full-screen"}, {"Current monitor", "monitor"}, {"Active window", "active-window"}};
  auto *delayedBox = new QGroupBox("Delayed capture", capture); auto *delayedForm = new QFormLayout(delayedBox); captureForm->addRow(delayedBox);
  auto *delaySeconds = spin(delayedBox, "Delayed capture seconds", 1, 60, " s");
  auto delaySources = sources; delaySources.append(qMakePair(QString("Region"), QString("region")));
  auto *delaySource = choice(delayedBox, "Delayed capture source", delaySources);
  delayedForm->addRow("Delay &seconds", delaySeconds); delayedForm->addRow("Capture &source", delaySource);
  auto *autoBox = new QGroupBox("Automatic capture", capture); auto *autoForm = new QFormLayout(autoBox); captureForm->addRow(autoBox);
  auto *autoSource = choice(autoBox, "Automatic capture source", sources);
  auto *autoInterval = spin(autoBox, "Automatic capture interval seconds", 1, 24 * 60 * 60, " s");
  auto *autoKind = choice(autoBox, "Automatic capture bound", {{"Number of captures", "count"}, {"Duration", "time"}});
  auto *autoCount = spin(autoBox, "Automatic capture count", 1, 10000);
  auto *autoDuration = spin(autoBox, "Automatic capture duration minutes", 1, 1440, " min");
  autoForm->addRow("Capture s&ource", autoSource); autoForm->addRow("&Interval", autoInterval); autoForm->addRow("Stop &after", autoKind);
  autoForm->addRow("Number of captures", autoCount); autoForm->addRow("Duration", autoDuration);
  auto syncBound = [=] { const bool count = autoKind->currentData().toString() == "count"; autoCount->setEnabled(count); autoDuration->setEnabled(!count); };
  connect(autoKind, &QComboBox::currentIndexChanged, this, syncBound);
  auto *scrollBox = new QGroupBox("Scrolling capture", capture); auto *scrollForm = new QFormLayout(scrollBox); captureForm->addRow(scrollBox);
  auto *scrollSettle = spin(scrollBox, "Scrolling capture settle delay milliseconds", 0, 5000, " ms");
  auto *scrollSteps = spin(scrollBox, "Scrolling capture wheel steps per frame", 1, 50);
  auto *scrollHeader = spin(scrollBox, "Scrolling capture header band pixels", 0, 2000, " px");
  auto *scrollFooter = spin(scrollBox, "Scrolling capture footer band pixels", 0, 2000, " px");
  auto *scrollHeight = spin(scrollBox, "Scrolling capture maximum height pixels", 1000, 100000, " px");
  auto *scrollFrames = spin(scrollBox, "Scrolling capture maximum frames", 2, 500);
  auto *scrollPointer = new QCheckBox("&Keep pointer over the region", scrollBox); scrollPointer->setAccessibleName("Keep pointer over the scrolling region");
  scrollForm->addRow("Settle &delay", scrollSettle); scrollForm->addRow("&Wheel steps per frame", scrollSteps);
  scrollForm->addRow("&Header to skip", scrollHeader); scrollForm->addRow("F&ooter to skip", scrollFooter);
  scrollForm->addRow("Maximum &height", scrollHeight); scrollForm->addRow("Maximum &frames", scrollFrames); scrollForm->addRow(scrollPointer);
  auto *hotkeyPage = new QWidget(tabs); auto *hotkeyLayout = new QVBoxLayout(hotkeyPage); tabs->addTab(hotkeyPage, "&Hotkeys");
  auto *table = new QTableWidget(hotkeyPage); table->setColumnCount(3); table->setHorizontalHeaderLabels({"Action", "Preferred trigger", "Currently bound by desktop"});
  table->setAccessibleName("Capture and workflow hotkeys"); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch); hotkeyLayout->addWidget(table);
  auto *trigger = new QLineEdit(hotkeyPage); trigger->setAccessibleName("Preferred trigger for selected action"); trigger->setPlaceholderText("Ctrl+Print, Shift+F1, or blank to clear; modifiers Ctrl, Alt, Shift, Meta"); hotkeyLayout->addWidget(trigger);
  auto *assign = new QPushButton("Set &preferred trigger", hotkeyPage); hotkeyLayout->addWidget(assign);
  auto *desktop = new QPushButton("Change in &desktop settings… (closes unapplied draft)", hotkeyPage); hotkeyLayout->addWidget(desktop);
  auto *status = new QLabel(this); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); status->setAccessibleName("Settings validation and conflicts");
  status->setText((store.current().diagnostics + (plan.diagnostic.isEmpty() ? QStringList{} : QStringList{plan.diagnostic + ": reload Capture workflows before enabling shortcuts"})).join('\n')); layout->addWidget(status);
  auto refresh = [=, &shortcuts] {
    const QSignalBlocker blocker(table);
    folder->setText(draft->value("saveFolder").toString()); pattern->setText(draft->value("filenamePattern").toString());
    login->setChecked(draft->value("startAtLogin").toBool()); tray->setChecked(draft->value("showTrayIcon").toBool());
    sound->setChecked(draft->value("playCaptureSound").toBool()); notification->setChecked(draft->value("showNotification").toBool());
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
      table->setItem(row++, 2, new QTableWidgetItem(shortcuts.bindings().value(entry.id, "Unbound in this session")));
    }
    if (table->rowCount()) { table->selectRow(0); trigger->setText(table->item(0, 1)->text()); }
  };
  auto collect = [=] {
    draft->insert("saveFolder", folder->text()); draft->insert("filenamePattern", pattern->text());
    draft->insert("startAtLogin", login->isChecked()); draft->insert("showTrayIcon", tray->isChecked());
    draft->insert("playCaptureSound", sound->isChecked()); draft->insert("showNotification", notification->isChecked());
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
    if (!result.ok) { status->setText(result.diagnostic + (result.conflictingAction.isEmpty() ? "" : ": " + result.conflictingAction) + ". Change or clear the conflicting binding, then retry."); return; }
    auto object = draft->value("hotkeys").toObject(); object.insert(id, bindings.entries.value(id)); draft->insert("hotkeys", object);
    table->item(row, 1)->setText(bindings.entries.value(id)); status->setText("Preference updated in draft. Apply to save; change in desktop settings to request binding.");
  });
  connect(desktop, &QPushButton::clicked, this, [this, &shortcuts] {
    // Close the modal first: setup uses the same busy/modal admission as capture.
    reject(); QTimer::singleShot(0, &shortcuts, [&shortcuts] { shortcuts.changeInDesktopSettings(); });
  });
  connect(&shortcuts, &WorkflowShortcutActions::bindingsChanged, this, [=, &shortcuts] {
    for (int row = 0; row < table->rowCount(); ++row) table->item(row, 2)->setText(shortcuts.bindings().value(table->item(row, 0)->data(Qt::UserRole).toString(), "Unbound in this session"));
  });
  auto *recordingPage = new QWidget(tabs); auto *recordingForm = new QFormLayout(recordingPage); tabs->addTab(recordingPage, "&Recording");
  RecordingDevices devices;
  auto *systemAudio = new QComboBox(recordingPage); auto *microphone = new QComboBox(recordingPage);
  for (auto *combo : {systemAudio, microphone}) {
    combo->addItem("None", QString{});
    for (const auto &device : devices.list()) combo->addItem(device.label + (device.monitor ? " (system monitor)" : ""), device.id);
  }
  systemAudio->setAccessibleName("System audio device"); microphone->setAccessibleName("Microphone");
  recordingForm->addRow("System audio device", systemAudio); recordingForm->addRow("Microphone", microphone);
  auto refreshAudio = [=] {
    for (const auto &entry : {qMakePair(systemAudio, QString("systemAudioDevice")), qMakePair(microphone, QString("microphoneDevice"))}) {
      const auto id = draft->value(entry.second).toString();
      if (entry.first->findData(id) < 0) entry.first->addItem("Missing device: " + id, id);
      const QSignalBlocker blocker(entry.first); entry.first->setCurrentIndex(entry.first->findData(id));
    }
  };
  refreshAudio();
  connect(systemAudio, &QComboBox::currentIndexChanged, this, [=] { draft->insert("systemAudioDevice", systemAudio->currentData().toString()); });
  connect(microphone, &QComboBox::currentIndexChanged, this, [=] { draft->insert("microphoneDevice", microphone->currentData().toString()); });
  auto *audioNote = new QLabel("Only these explicit inputs are captured. A missing input produces video without audio and a notification. GIF never contains audio.", recordingPage);
  audioNote->setWordWrap(true); recordingForm->addRow(audioNote);
  auto *reset = new QPushButton("&Reset section…", this); layout->addWidget(reset);
  connect(reset, &QPushButton::clicked, this, [=] {
    collect(); const auto section = static_cast<Section>(tabs->currentIndex()); const auto changes = resetPreview(*draft, section, defaultFolder);
    if (changes.isEmpty()) { status->setText("This section already has default values."); return; }
    if (QMessageBox::question(this, "Reset profile section", "Preview of changes (other sections and user files are kept):\n" + changes.join('\n') + "\n\nReset this draft? Apply saves it; Cancel discards it.", QMessageBox::Reset | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Reset) return;
    *draft = resetSection(*draft, section, defaultFolder); refresh(); refreshAudio();
  });
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
    status->setText("Settings saved. Preferred hotkeys require desktop confirmation; use Change in desktop settings."); return true;
  };
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [save] { save(); });
  connect(buttons, &QDialogButtonBox::accepted, this, [this, save] { if (save()) accept(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  refresh();
}
}
