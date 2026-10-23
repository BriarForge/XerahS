// CORE-008: first-run setup guide pages.
#include "OnboardingWizard.h"

#include "settings/ApplicationSettings.h"
#include "settings/HotkeyBindings.h"
#include "workflows/CaptureWorkflows.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWizardPage>

namespace xerahs::app {
namespace {

constexpr int kHideBeforeCaptureMs = 300;
constexpr int kCaptureWatchdogMs = 120000;

class OnboardingPage final : public QWizardPage {
public:
  std::function<bool()> validate;
  std::function<void()> init, done;
  bool validatePage() override {
    if (validate && !validate()) return false;
    if (done) done();
    return true;
  }
  void initializePage() override { if (init) init(); }
};

QLabel *textLabel(const QString &text, QWidget *parent, const QString &name = {}) {
  auto *label = new QLabel(text, parent);
  label->setWordWrap(true);
  label->setTextFormat(Qt::PlainText);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
  if (!name.isEmpty()) label->setAccessibleName(name);
  return label;
}

QPushButton *commandButton(const QString &text, const QString &description, QWidget *parent) {
  auto *button = new QPushButton(text, parent);
  button->setAccessibleName(QString(text).remove(QLatin1Char('&')).remove(QStringLiteral("…")).trimmed());
  button->setAccessibleDescription(description);
  return button;
}

QString saveProblem(const QStringList &diagnostics) {
  QStringList lines;
  for (const auto &d : diagnostics) {
    if (d.startsWith(u"saveFolder:")) lines << QStringLiteral("The save folder is not usable. Choose an absolute folder path you can write to.");
    else if (d.startsWith(u"filenamePattern:")) lines << QStringLiteral("The filename pattern is not valid. Use tokens such as %y%mo%d or %ra{10}, or reset it to the default.");
  }
  return lines.join(QLatin1Char('\n'));
}

}  // namespace

QString describeDefaultWorkflow(const OnboardingWorkflowInfo &info) {
  QStringList parts;
  const auto &a = info.actions;
  if (a.contains(u"save")) parts << QStringLiteral("saved to ") + info.folder;
  if (a.contains(u"save-as")) parts << QStringLiteral("saved as a new PNG you choose");
  if (a.contains(u"image-clipboard")) parts << QStringLiteral("copied to the clipboard");
  if (a.contains(u"file-clipboard")) parts << QStringLiteral("copied to the clipboard as a file");
  if (a.contains(u"path-clipboard")) parts << QStringLiteral("have their file path copied to the clipboard");
  if (a.contains(u"annotation")) parts << QStringLiteral("opened for annotation");
  if (a.contains(u"reveal")) parts << QStringLiteral("shown in your file manager");
  if (a.contains(u"pin")) parts << QStringLiteral("pinned on screen");
  if (a.contains(u"print")) parts << QStringLiteral("sent to print");
  const bool upload = a.contains(u"upload");
  if (upload) parts << QStringLiteral("uploaded to your default destination");
  if (parts.isEmpty()) return QStringLiteral("Captures run no actions; nothing is saved, copied or uploaded.");
  QString joined = parts.size() == 1 ? parts.first() : parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + parts.last();
  return QStringLiteral("Captures are ") + joined + (upload ? QStringLiteral(".") : QStringLiteral("; nothing is uploaded."));
}

OnboardingWizard::OnboardingWizard(OnboardingPorts ports, OnboardingState initial, QString notice, QWidget *parent)
    : QWizard(parent), m_ports(std::move(ports)), m_state(std::move(initial)) {
  setWindowTitle(QStringLiteral("XerahS setup guide"));
  setAccessibleName(QStringLiteral("XerahS setup guide"));
  setAccessibleDescription(QStringLiteral("Optional first-run setup. Every step can be skipped and you can finish later."));
  setWindowModality(Qt::NonModal);
  setOption(QWizard::NoCancelButton, true);
  setOption(QWizard::HaveCustomButton1, true);
  setOption(QWizard::HaveCustomButton2, true);
  setButtonText(QWizard::CustomButton1, QStringLiteral("&Skip this step"));
  setButtonText(QWizard::CustomButton2, QStringLiteral("Finish &later"));
  setButtonLayout({QWizard::CustomButton2, QWizard::Stretch, QWizard::CustomButton1, QWizard::BackButton, QWizard::NextButton, QWizard::FinishButton});
  button(QWizard::CustomButton1)->setAccessibleName(QStringLiteral("Skip this step"));
  button(QWizard::CustomButton2)->setAccessibleName(QStringLiteral("Finish later"));
  button(QWizard::CustomButton2)->setAccessibleDescription(QStringLiteral("Closes the guide and keeps your place. Reopen it from Setup guide."));
  resize(640, 460);

  m_watchdog = new QTimer(this);
  m_watchdog->setSingleShot(true);
  connect(m_watchdog, &QTimer::timeout, this, [this] {
    reportCaptureResult(false, QStringLiteral("No result was reported. Check the XerahS window and your desktop's permission prompts, then try again."));
  });
  buildPages(notice);

  const int start = m_state.finished ? 0 : std::max(0, int(onboardingPages().indexOf(m_state.page)));
  if (m_state.finished) m_state.page = onboardingPages().first();
  setStartId(start);
  connect(this, &QWizard::currentIdChanged, this, [this](int id) {
    if (id < 0) return;  // closing the wizard
    m_state.page = onboardingPages().value(id);
    button(QWizard::CustomButton1)->setEnabled(skippable(m_state.page));
    changed();
  });
  connect(this, &QDialog::accepted, this, [this] {
    markCompleted(QStringLiteral("summary"));
    m_state.finished = true;
    changed();
  });
  connect(button(QWizard::CustomButton1), &QPushButton::clicked, this, &OnboardingWizard::skipCurrent);
  connect(button(QWizard::CustomButton2), &QPushButton::clicked, this, &OnboardingWizard::finishLater);
}

QString OnboardingWizard::currentPageName() const { return onboardingPages().value(currentId()); }

bool OnboardingWizard::skippable(const QString &page) const {
  return page == u"capture-permission" || page == u"save-location" || page == u"hotkeys" || page == u"uploads";
}

void OnboardingWizard::skipCurrent() {
  const auto page = currentPageName();
  if (!skippable(page) || m_testPending) return;
  m_skipping = true;
  next();
  m_skipping = false;
  markSkipped(page);
}

void OnboardingWizard::finishLater() {
  changed();
  reject();
}

void OnboardingWizard::markCompleted(const QString &page) {
  m_state.skipped.removeAll(page);
  if (!m_state.completed.contains(page)) m_state.completed.append(page);
  changed();
}

void OnboardingWizard::markSkipped(const QString &page) {
  m_state.completed.removeAll(page);
  if (!m_state.skipped.contains(page)) m_state.skipped.append(page);
  changed();
}

void OnboardingWizard::changed() {
  if (m_ports.persist) m_ports.persist(m_state);
}

void OnboardingWizard::startTestCapture() {
  if (m_testPending) return;
  if (!m_ports.testCapture) { m_testStatus->setText(QStringLiteral("Capture is not available in this session.")); return; }
  m_testPending = true;
  m_testButton->setEnabled(false);
  button(QWizard::CustomButton1)->setEnabled(false);
  m_testStatus->setText(QStringLiteral("Capturing the full screen. This window hides first; your desktop may ask for permission."));
  m_watchdog->start(kCaptureWatchdogMs);
  hide();
  QTimer::singleShot(kHideBeforeCaptureMs, this, [this] { if (m_testPending && m_ports.testCapture) m_ports.testCapture(); });
}

void OnboardingWizard::reportCaptureResult(bool success, const QString &message) {
  if (!m_testPending) return;
  m_testPending = false;
  m_watchdog->stop();
  m_testStatus->setText((success ? QStringLiteral("Capture worked. ") : QStringLiteral("Capture did not succeed. ")) + message);
  m_testButton->setEnabled(true);
  button(QWizard::CustomButton1)->setEnabled(skippable(currentPageName()));
  show();
  raise();
  activateWindow();
}

void OnboardingWizard::buildPages(const QString &notice) {
  const auto addPage = [this](const QString &id, const QString &title, const QString &subtitle) -> OnboardingPage * {
    auto *page = new OnboardingPage;
    page->setTitle(title);
    page->setSubTitle(subtitle);
    page->setAccessibleName(title);
    new QVBoxLayout(page);
    setPage(int(onboardingPages().indexOf(id)), page);
    page->done = [this, id] { if (!m_skipping) markCompleted(id); };
    return page;
  };
  const auto layoutOf = [](OnboardingPage *page) { return qobject_cast<QVBoxLayout *>(page->layout()); };

  // 1. Welcome
  auto *welcome = addPage(QStringLiteral("welcome"), QStringLiteral("Welcome to XerahS"),
      QStringLiteral("A few optional steps. Nothing here is required."));
  if (!notice.isEmpty()) {
    auto *warning = textLabel(notice, welcome, QStringLiteral("Setup guide progress warning"));
    layoutOf(welcome)->addWidget(warning);
  }
  layoutOf(welcome)->addWidget(textLabel(QStringLiteral(
      "XerahS already works as it is: it can capture your screen, save the image to your Pictures folder and copy it to the clipboard, "
      "with nothing uploaded. These steps explain permissions and let you change the defaults if you want to.\n\n"
      "You can skip any step, choose Finish later to stop and keep your place, and reopen this guide any time from Setup guide."), welcome));

  // 2. Screen capture permission
  auto *permission = addPage(QStringLiteral("capture-permission"), QStringLiteral("Screen capture permission"),
      QStringLiteral("Your desktop, not XerahS, decides whether capture is allowed."));
  layoutOf(permission)->addWidget(textLabel(QStringLiteral(
      "XerahS captures through your desktop's screenshot portal. The first time you capture, the desktop may ask you to allow it. "
      "XerahS cannot grant or bypass that prompt. If you deny it, XerahS reports the denial and you can allow it in your desktop's privacy settings.\n\n"
      "Test capture now takes one real full-screen capture with your current defaults, so it saves a file and uses the clipboard as the default workflow does. "
      "This window hides while it runs."), permission));
  m_testButton = commandButton(QStringLiteral("&Test capture now"), QStringLiteral("Takes one real full-screen capture and reports whether it worked or was denied."), permission);
  layoutOf(permission)->addWidget(m_testButton);
  m_testStatus = textLabel(QStringLiteral("No test capture has been run."), permission, QStringLiteral("Test capture result"));
  layoutOf(permission)->addWidget(m_testStatus);
  connect(m_testButton, &QPushButton::clicked, this, &OnboardingWizard::startTestCapture);

  // 3. Save folder and filename pattern: the same profile fields and validation as Settings.
  auto *save = addPage(QStringLiteral("save-location"), QStringLiteral("Save folder and filename"),
      QStringLiteral("These are the same preferences as Settings, General."));
  auto *form = new QFormLayout;
  auto *folder = new QLineEdit(save);
  folder->setMaxLength(32768);
  folder->setAccessibleName(QStringLiteral("Default save folder"));
  auto *browse = commandButton(QStringLiteral("&Browse…"), QStringLiteral("Choose the default save folder."), save);
  auto *folderRow = new QHBoxLayout;
  folderRow->addWidget(folder, 1);
  folderRow->addWidget(browse);
  form->addRow(QStringLiteral("Default save &folder"), folderRow);
  auto *pattern = new QLineEdit(save);
  pattern->setMaxLength(4096);
  pattern->setAccessibleName(QStringLiteral("Filename pattern"));
  form->addRow(QStringLiteral("Filename &pattern"), pattern);
  layoutOf(save)->addLayout(form);
  auto *preview = textLabel({}, save, QStringLiteral("Filename preview"));
  auto *problem = textLabel({}, save, QStringLiteral("Save location problem"));
  layoutOf(save)->addWidget(preview);
  layoutOf(save)->addWidget(problem);
  const auto refreshPreview = [folder, pattern, preview] {
    xerahs::workflows::CaptureOutput output;
    output.directory = folder->text();
    output.pattern = pattern->text();
    const auto result = xerahs::workflows::previewCaptureOutput(output);
    preview->setText(result.diagnostic.isEmpty() ? QStringLiteral("Example file name: ") + result.filename : QStringLiteral("Pattern problem: ") + result.diagnostic);
  };
  connect(pattern, &QLineEdit::textChanged, this, refreshPreview);
  connect(folder, &QLineEdit::textChanged, this, refreshPreview);
  connect(browse, &QPushButton::clicked, this, [this, folder] {
    const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("Default save folder"), folder->text());
    if (!path.isEmpty()) folder->setText(path);
  });
  save->init = [this, folder, pattern, problem] {
    const auto values = m_ports.settings ? m_ports.settings() : QJsonObject{};
    folder->setText(values.value(QStringLiteral("saveFolder")).toString());
    pattern->setText(values.value(QStringLiteral("filenamePattern")).toString());
    problem->clear();
  };
  save->validate = [this, folder, pattern, problem] {
    if (m_skipping) return true;
    const auto values = m_ports.settings ? m_ports.settings() : QJsonObject{};
    auto draft = values;
    draft.insert(QStringLiteral("saveFolder"), folder->text());
    draft.insert(QStringLiteral("filenamePattern"), pattern->text());
    const auto message = saveProblem(xerahs::settings::repairSettings(draft, m_ports.defaultFolder).diagnostics);
    if (!message.isEmpty()) { problem->setText(message); folder->setFocus(); return false; }
    if (draft != values && m_ports.saveSettings) {
      const auto error = m_ports.saveSettings(draft);
      if (!error.isEmpty()) { problem->setText(error); return false; }
    }
    problem->clear();
    return true;
  };

  // 4. Hotkeys
  auto *hotkeys = addPage(QStringLiteral("hotkeys"), QStringLiteral("Hotkeys"),
      QStringLiteral("Preferred triggers for capture actions. The desktop makes the final binding."));
  auto *table = new QTableWidget(hotkeys);
  table->setColumnCount(2);
  table->setHorizontalHeaderLabels({QStringLiteral("Action"), QStringLiteral("Preferred trigger")});
  table->setAccessibleName(QStringLiteral("Bindable capture actions"));
  table->setAccessibleDescription(QStringLiteral("Read-only list of capture actions and their preferred triggers."));
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layoutOf(hotkeys)->addWidget(table);
  layoutOf(hotkeys)->addWidget(textLabel(QStringLiteral(
      "Nothing is bound until you confirm it with your desktop. Enable workflow shortcuts asks the desktop to register them; "
      "Settings lets you change the preferred triggers."), hotkeys));
  auto *shortcuts = commandButton(QStringLiteral("&Enable workflow shortcuts…"), QStringLiteral("Opens the desktop's shortcut registration for XerahS workflows."), hotkeys);
  auto *settingsButton = commandButton(QStringLiteral("Open &Settings…"), QStringLiteral("Opens Settings to change preferred triggers."), hotkeys);
  auto *hotkeyButtons = new QHBoxLayout;
  hotkeyButtons->addWidget(shortcuts);
  hotkeyButtons->addWidget(settingsButton);
  hotkeyButtons->addStretch();
  layoutOf(hotkeys)->addLayout(hotkeyButtons);
  connect(shortcuts, &QPushButton::clicked, this, [this] { if (m_ports.runCommand) m_ports.runCommand(QStringLiteral("enable-workflow-shortcuts")); });
  connect(settingsButton, &QPushButton::clicked, this, [this] { if (m_ports.runCommand) m_ports.runCommand(QStringLiteral("settings")); });
  hotkeys->init = [this, table] {
    const auto bindings = xerahs::settings::hotkeys(m_ports.settings ? m_ports.settings() : QJsonObject{});
    const auto actions = xerahs::settings::captureHotkeyActions();
    table->setRowCount(actions.size());
    int row = 0;
    for (const auto &action : actions) {
      table->setItem(row, 0, new QTableWidgetItem(action.label));
      const auto trigger = bindings.entries.value(action.id);
      table->setItem(row++, 1, new QTableWidgetItem(trigger.isEmpty() ? QStringLiteral("Not set") : trigger));
    }
    if (row) table->selectRow(0);
  };

  // 5. Uploads
  auto *uploads = addPage(QStringLiteral("uploads"), QStringLiteral("Uploads (optional)"),
      QStringLiteral("Nothing is uploaded unless you ask for it."));
  layoutOf(uploads)->addWidget(textLabel(QStringLiteral(
      "XerahS uploads a file only when you add an upload destination and then choose Upload for that capture or file. "
      "Without a destination, uploads are simply unavailable, and everything else keeps working.\n\n"
      "Open Upload destinations to add one, or import a ShareX custom uploader (.sxcu) from the Import button there. "
      "Secrets are kept in your system keyring, never in plain files."), uploads));
  auto *destinations = commandButton(QStringLiteral("Open &Upload destinations…"), QStringLiteral("Opens the Upload destinations window."), uploads);
  auto *importer = commandButton(QStringLiteral("&Import a .sxcu uploader…"), QStringLiteral("Opens Upload destinations, where the Import button reads a .sxcu file."), uploads);
  layoutOf(uploads)->addWidget(destinations);
  layoutOf(uploads)->addWidget(importer);
  for (auto *b : {destinations, importer})
    connect(b, &QPushButton::clicked, this, [this] { if (m_ports.runCommand) m_ports.runCommand(QStringLiteral("upload-destinations")); });

  // 6. Telemetry
  auto *telemetry = addPage(QStringLiteral("telemetry"), QStringLiteral("Telemetry and privacy"),
      QStringLiteral("There is nothing to opt out of."));
  layoutOf(telemetry)->addWidget(textLabel(QStringLiteral(
      "XerahS has no telemetry, analytics, crash reporting or automatic update checks. It sends nothing about you or your captures anywhere. "
      "The only network traffic is an upload you start yourself to a destination you added.\n\n"
      "Captures, history and settings stay on this computer, in files only you can read."), telemetry));

  // 7. Summary
  auto *summary = addPage(QStringLiteral("summary"), QStringLiteral("Summary"),
      QStringLiteral("What you set up, what you skipped, and what a capture does now."));
  auto *report = textLabel({}, summary, QStringLiteral("Setup summary"));
  layoutOf(summary)->addWidget(report);
  summary->init = [this, report] {
    static const QMap<QString, QString> names{{"welcome", "Welcome"}, {"capture-permission", "Screen capture permission"},
        {"save-location", "Save folder and filename"}, {"hotkeys", "Hotkeys"}, {"uploads", "Uploads"}, {"telemetry", "Telemetry and privacy"}};
    QStringList lines;
    for (const auto &id : onboardingPages()) {
      if (id == u"summary") continue;
      lines << names.value(id) + QStringLiteral(": ") +
          (m_state.completed.contains(id) ? QStringLiteral("reviewed") : m_state.skipped.contains(id) ? QStringLiteral("skipped") : QStringLiteral("not reviewed"));
    }
    const auto info = m_ports.defaultWorkflow ? m_ports.defaultWorkflow() : OnboardingWorkflowInfo{};
    lines << QString() << describeDefaultWorkflow(info);
    lines << QStringLiteral("You can change any of this later in Settings, and reopen this guide from Setup guide.");
    report->setText(lines.join(QLatin1Char('\n')));
  };
}

}  // namespace xerahs::app
