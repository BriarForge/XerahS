#include "CaptureWorkflows.h"
#include "WorkflowStoragePath.h"
#include "CaptureActions.h"
#include "naming/CounterStore.h"
#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>
namespace xerahs::app {
using namespace xerahs::workflows;
QString NativeCaptureWorkflows::defaultPath() {
  return nativeCaptureWorkflowPath();
}
xerahs::editor::PreferenceWrite NativeCaptureWorkflows::save(const CaptureWorkflows &draft, quint64 revision) {
  const auto result = m_store.save(draft, revision); if (result.saved) emit changed(); return result;
}
WorkflowChoice NativeCaptureWorkflows::resolve(const QUuid &requested) const {
  return m_store.resolve(requested, CaptureActions::selectableActions());
}
QString workflowMessage(const QString &code) {
  if (code == "workflow-action-unavailable") return "This workflow contains unavailable actions. Remove those actions to run it, or keep it for a later version.";
  if (code == "workflow-capture-unavailable") return "This capture type is unavailable. The workflow has been kept.";
  if (code == "workflow-newer-version") return "These workflows need a newer XerahS version. The file has been kept unchanged.";
  if (code == "workflow-upgrade-conflict") return "A version 1 workflow has an unknown output field. It has been kept unchanged; explicit migration is required before assigning output settings.";
  if (code == "workflow-directory-invalid") return "Use an absolute save folder without . or .. components, or leave it blank for the standard capture folder.";
  if (code == "workflow-pattern-invalid") return "Use a filename pattern of at most 4096 characters with valid Unicode text.";
  if (code.startsWith("filename-")) return "The filename pattern cannot run: " + code.mid(9) + ". Random-file tokens have no file-read permission.";
  if (code == "workflow-changed") return "Workflows changed elsewhere. Reload before saving or running.";
  if (code == "workflow-not-found") return "The selected workflow no longer exists. Reload and select another workflow.";
  if (code == "workflow-invalid" || code == "workflow-too-large") return "Workflows are invalid or exceed limits. Use unique IDs, names of 1–128 characters and at most 64 workflows. The saved file has been kept.";
  if (code == "workflow-read-only") return "Workflows could not be loaded safely. Restore a compatible file and Reload; the current file has been kept.";
  return editorPreferenceDiagnosticMessage(code);
}
namespace {
class WorkflowDialog final : public QDialog {
public:
  WorkflowDialog(NativeCaptureWorkflows &host, QWidget *parent, std::function<bool()> canRun)
      : QDialog(parent), m_host(host), m_canRun(std::move(canRun)) {
    setObjectName("captureWorkflowsDialog"); setWindowTitle("Capture workflows"); setAccessibleName(windowTitle());
    new NativeEditorAppearance(*this);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel("Save named capture workflows and choose a default. Each workflow captures a region, the full screen, the monitor under the pointer or the active window. Capture region, full screen, monitor and active window in the window, tray and command line use the default workflow's actions and output. Actions run in the standard order; unchecked actions do not run.", this);
    description->setWordWrap(true); layout->addWidget(description);
    m_list = new QListWidget(this); m_list->setObjectName("captureWorkflowList"); m_list->setAccessibleName("Saved capture workflows"); layout->addWidget(m_list);
    auto *row = new QHBoxLayout; layout->addLayout(row);
    auto *add = new QPushButton("New", this); add->setObjectName("newCaptureWorkflow"); row->addWidget(add);
    auto *remove = new QPushButton("Delete…", this); remove->setObjectName("deleteCaptureWorkflow"); row->addWidget(remove);
    auto *makeDefault = new QPushButton("Make default", this); makeDefault->setObjectName("defaultCaptureWorkflow"); row->addWidget(makeDefault);
    auto *reload = new QPushButton("Reload", this); reload->setObjectName("reloadCaptureWorkflows"); row->addWidget(reload);
    m_default = new QLabel(this); m_default->setObjectName("captureDefaultWorkflow"); m_default->setTextFormat(Qt::PlainText); layout->addWidget(m_default);
    auto *form = new QFormLayout; layout->addLayout(form);
    m_name = new QLineEdit(this); m_name->setObjectName("captureWorkflowName"); m_name->setAccessibleName("Workflow name"); m_name->setMaxLength(128); form->addRow("Name", m_name);
    m_id = new QLineEdit(this); m_id->setObjectName("captureWorkflowId"); m_id->setAccessibleName("Workflow ID for CLI"); m_id->setReadOnly(true); form->addRow("Workflow ID", m_id);
    m_capture = new QComboBox(this); m_capture->setObjectName("captureWorkflowSource"); m_capture->setAccessibleName("Capture source"); form->addRow("Capture", m_capture);
    auto *folderRow = new QHBoxLayout;
    m_directory = new QLineEdit(this); m_directory->setObjectName("captureWorkflowSaveDirectory"); m_directory->setAccessibleName("Save folder"); m_directory->setMaxLength(32768);
    m_directory->setPlaceholderText("Standard capture folder"); folderRow->addWidget(m_directory);
    m_browse = new QPushButton("Browse…", this); m_browse->setObjectName("browseCaptureWorkflowDirectory"); folderRow->addWidget(m_browse); form->addRow("Save folder", folderRow);
    m_pattern = new QLineEdit(this); m_pattern->setObjectName("captureWorkflowFilenamePattern"); m_pattern->setAccessibleName("Filename pattern"); m_pattern->setMaxLength(4096); form->addRow("Filename pattern", m_pattern);
    m_preview = new QLineEdit(this); m_preview->setObjectName("captureWorkflowFilenamePreview"); m_preview->setAccessibleName("Example PNG filename"); m_preview->setReadOnly(true); form->addRow("Example PNG name", m_preview);
    m_patternHelp = new QLabel(this); m_patternHelp->setObjectName("captureWorkflowPatternHelp"); m_patternHelp->setTextFormat(Qt::PlainText); m_patternHelp->setWordWrap(true); layout->addWidget(m_patternHelp);
    m_actions = new QListWidget(this); m_actions->setObjectName("captureWorkflowActions"); m_actions->setAccessibleName("Actions after capture"); layout->addWidget(m_actions);
    m_choose = new QCheckBox("Ask which actions to run after each capture", this); m_choose->setObjectName("captureWorkflowChooseActions"); layout->addWidget(m_choose);
    auto *dependencies = new QLabel("Save uses the folder and filename pattern above; blank folder uses the standard capture folder. Save As asks for a new PNG path. Existing files are kept; a fixed name can report a collision. File/path clipboard and Show in folder need a successful Save or Save As.", this);
    dependencies->setWordWrap(true); layout->addWidget(dependencies);
    m_status = new QLabel(this); m_status->setObjectName("captureWorkflowStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(buttons);
    m_save = buttons->addButton("Save changes", QDialogButtonBox::ActionRole); m_save->setObjectName("saveCaptureWorkflows");
    m_run = buttons->addButton("Run selected", QDialogButtonBox::ActionRole); m_run->setObjectName("runCaptureWorkflow"); m_run->setToolTip("Save changes before running the selected workflow.");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) { if (!m_loading) showEntry(row); });
    connect(m_name, &QLineEdit::textEdited, this, [this] { updateEntry(); });
    connect(m_choose, &QCheckBox::toggled, this, [this] { updateEntry(); });
    connect(m_capture, &QComboBox::currentIndexChanged, this, [this] { updateEntry(); });
    connect(m_actions, &QListWidget::itemChanged, this, [this] { updateEntry(); });
    connect(m_directory, &QLineEdit::textChanged, this, [this] { updateEntry(); });
    connect(m_pattern, &QLineEdit::textChanged, this, [this] { updateEntry(); });
    connect(m_browse, &QPushButton::clicked, this, [this] { browseDirectory(); });
    connect(add, &QPushButton::clicked, this, [this] {
      if (!m_catalog || m_catalog->entries.size() >= 64) return;
      CaptureWorkflow w; w.id = QUuid::createUuid(); w.name = "New workflow"; w.actions = {"save"};
      m_catalog->entries.append(w); m_dirty = true; refreshList(m_catalog->entries.size() - 1);
    });
    connect(makeDefault, &QPushButton::clicked, this, [this] {
      const auto index = m_list->currentRow(); if (!m_catalog || index < 0) return;
      m_catalog->defaultId = m_catalog->entries[index].id; m_dirty = true; refreshList(index);
    });
    connect(remove, &QPushButton::clicked, this, [this] { deleteEntry(); });
    connect(reload, &QPushButton::clicked, this, [this] { if (!m_dirty || confirm("Discard unsaved workflow changes and reload?")) load(); });
    connect(m_save, &QPushButton::clicked, this, [this] { save(); });
    connect(m_run, &QPushButton::clicked, this, [this] {
      if (m_dirty || !m_catalog || !m_canRun()) return;
      const auto index = m_list->currentRow(); if (index < 0) return;
      const auto choice = m_host.resolve(m_catalog->entries[index].id);
      if (!choice.workflow) { m_status->setText(workflowMessage(choice.diagnostic)); return; }
      selected = choice.workflow; accept();
    });
    auto *timer = new QTimer(this); timer->setInterval(200); connect(timer, &QTimer::timeout, this, [this] { availability(); }); timer->start();
    load(); resize(680, 810);
  }
  std::optional<CaptureWorkflow> selected;
private:
  bool confirm(const QString &question) {
    const QPointer<WorkflowDialog> owner(this); QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Capture workflows", question, QMessageBox::Discard | QMessageBox::Cancel, this));
    box->setTextFormat(Qt::PlainText); box->setDefaultButton(QMessageBox::Cancel); box->setEscapeButton(QMessageBox::Cancel);
    const int answer = box->exec(); if (box) box->setParent(nullptr); delete box.data(); return owner && answer == QMessageBox::Discard;
  }
  void browseDirectory() {
    const QPointer<WorkflowDialog> owner(this); QPointer<QFileDialog> picker(new QFileDialog(this, "Choose workflow save folder"));
    picker->setObjectName("captureWorkflowDirectoryPicker"); picker->setFileMode(QFileDialog::Directory); picker->setOption(QFileDialog::ShowDirsOnly);
    if (!m_directory->text().isEmpty()) picker->setDirectory(m_directory->text());
    const int answer = picker->exec(); const auto files = picker ? picker->selectedFiles() : QStringList{};
    if (picker) picker->setParent(nullptr);
    delete picker.data();
    if (owner && answer == QDialog::Accepted && files.size() == 1) m_directory->setText(files.first());
  }
  void load() {
    m_host.reload(); m_catalog = m_host.store().current().catalog; m_revision = m_host.store().revision(); m_dirty = false;
    int selected = 0;
    if (m_catalog) for (int i = 0; i < m_catalog->entries.size(); ++i) if (m_catalog->entries[i].id == m_catalog->defaultId) selected = i;
    refreshList(selected); m_status->setText(m_catalog ? "Changes affect future captures. Close keeps your last saved workflows." : workflowMessage(m_host.store().current().diagnostic));
  }
  void refreshList(int index) {
    m_loading = true; m_list->clear();
    if (m_catalog) for (const auto &w : m_catalog->entries) m_list->addItem(w.name + (w.id == m_catalog->defaultId ? " (default)" : ""));
    m_list->setCurrentRow(index); m_loading = false; showEntry(index);
    if (m_dirty) m_status->setText("Unsaved changes. Save changes before running a workflow.");
  }
  void showEntry(int index) {
    m_loading = true; m_actions->clear(); const bool exists = m_catalog && index >= 0 && index < m_catalog->entries.size();
    m_name->setEnabled(exists); m_capture->setEnabled(exists); m_actions->setEnabled(exists); m_choose->setEnabled(exists);
    m_directory->setEnabled(exists); m_pattern->setEnabled(exists); m_browse->setEnabled(exists);
    if (exists) {
      const auto &w = m_catalog->entries[index]; m_name->setText(w.name); m_choose->setChecked(w.chooseActions);
      m_capture->clear(); for (const auto &source : implementedCaptureSources()) m_capture->addItem(captureSourceLabel(source), source);
      if (m_capture->findData(w.capture) < 0) m_capture->addItem("Unavailable: " + w.capture, w.capture);
      m_capture->setCurrentIndex(m_capture->findData(w.capture));
      m_id->setText(w.id.toString(QUuid::WithoutBraces));
      m_directory->setText(w.output.directory); m_pattern->setText(w.output.pattern); showPreview(w.output, w.id);
      const auto available = CaptureActions::selectableActions(); auto displayed = available;
      for (const auto &action : w.actions) if (!displayed.contains(action)) displayed.append(action);
      for (const auto &action : displayed) {
        auto *item = new QListWidgetItem(available.contains(action) ? CaptureActions::actionLabel(action) : "Unavailable: " + action, m_actions);
        item->setData(Qt::UserRole, action); item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(w.actions.contains(action) ? Qt::Checked : Qt::Unchecked);
      }
      for (const auto &entry : m_catalog->entries) if (entry.id == m_catalog->defaultId) m_default->setText("Default capture: " + entry.name);
    } else { m_name->clear(); m_id->clear(); m_capture->clear(); m_default->clear(); m_directory->clear(); m_pattern->clear(); m_preview->clear(); m_patternHelp->clear(); }
    m_loading = false; availability();
  }
  void updateEntry() {
    if (m_loading || !m_catalog) return;
    const int index = m_list->currentRow(); if (index < 0) return;
    auto &w = m_catalog->entries[index]; w.name = m_name->text(); w.chooseActions = m_choose->isChecked(); w.actions.clear();
    if (m_capture->currentIndex() >= 0) w.capture = m_capture->currentData().toString();
    w.output.directory = m_directory->text(); w.output.pattern = m_pattern->text(); showPreview(w.output, w.id);
    for (int row = 0; row < m_actions->count(); ++row) if (m_actions->item(row)->checkState() == Qt::Checked) w.actions.append(m_actions->item(row)->data(Qt::UserRole).toString());
    m_list->item(index)->setText(w.name + (w.id == m_catalog->defaultId ? " (default)" : ""));
    if (w.id == m_catalog->defaultId) m_default->setText("Default capture: " + w.name);
    m_dirty = true; m_status->setText("Unsaved changes. Save changes to run this workflow."); availability();
  }
  void showPreview(const CaptureOutput &output, const QUuid &id = {}) {
    // FN-015: preview shows the stored next counter without reserving it.
    auto context = captureOutputExample();
    if (!id.isNull()) {
      const auto peek = xerahs::naming::CounterStore(defaultCounterPath()).peek(id.toString(QUuid::WithoutBraces));
      if (peek.error == xerahs::naming::CounterError::None) context.counter = peek.value;
    }
    const auto preview = previewCaptureOutput(output, context); m_preview->setText(preview.filename);
    QString hint = preview.diagnostic.isEmpty() ? QStringLiteral("Example: 2 Jan 2026, 15:04:05, 1920×1080. %y/%mo/%d = year/month/day; %h/%mi/%s = hour/minute/second; %width/%height = pixel dimensions; %ra{10} = 10 random letters/digits; %i{3} = counter (also %ia, %iA, %ix, %iX) showing the next value; %% = literal %. Unknown tokens are kept. PNG extension is added automatically. Random-file reads are unavailable.") : workflowMessage(preview.diagnostic);
    if (preview.error) hint += " Token " + preview.error->token.value_or(QString{}) + ", character " + QString::number(preview.error->offset) + " (zero-based).";
    m_patternHelp->setText(hint);
  }
  void availability() {
    const auto index = m_list->currentRow(); const bool exists = m_catalog && index >= 0 && index < m_catalog->entries.size();
    m_save->setEnabled(m_catalog && m_dirty && !m_saving);
    findChild<QPushButton *>("newCaptureWorkflow")->setEnabled(m_catalog && m_catalog->entries.size() < 64 && !m_saving);
    findChild<QPushButton *>("deleteCaptureWorkflow")->setEnabled(exists && m_catalog->entries.size() > 1 && !m_saving);
    findChild<QPushButton *>("defaultCaptureWorkflow")->setEnabled(exists && m_catalog->entries[index].id != m_catalog->defaultId && !m_saving);
    m_run->setEnabled(exists && !m_dirty && !m_saving && m_canRun() && workflowAvailability(m_catalog->entries[index], CaptureActions::selectableActions()).isEmpty());
  }
  void save() {
    if (!m_catalog || !m_dirty || m_saving) return;
    const auto draft = *m_catalog; const QPointer<WorkflowDialog> owner(this); m_saving = true; m_save->setEnabled(false); if (!owner) return;
    const auto written = m_host.save(draft, m_revision); if (!owner) return; m_saving = false;
    if (!written.saved) { m_status->setText(workflowMessage(written.diagnostic)); availability(); return; }
    m_revision = m_host.store().revision(); m_catalog = m_host.store().current().catalog; m_dirty = false; refreshList(m_list->currentRow()); m_status->setText("Workflows saved. Run selected starts the selected capture.");
  }
  void deleteEntry() {
    const int index = m_list->currentRow(); if (!m_catalog || index < 0) return;
    if (m_catalog->entries.size() == 1) { m_status->setText("Keep at least one capture workflow."); return; }
    const QPointer<WorkflowDialog> owner(this); if (!confirm("Delete this workflow? Saved captures will be kept. Deleting the default selects the first remaining workflow.")) return;
    if (!owner) return;
    const bool wasDefault = m_catalog->entries[index].id == m_catalog->defaultId; m_catalog->entries.removeAt(index);
    if (wasDefault) m_catalog->defaultId = m_catalog->entries.first().id;
    m_dirty = true; refreshList(qMin(index, m_catalog->entries.size() - 1));
  }
  NativeCaptureWorkflows &m_host;
  std::function<bool()> m_canRun;
  std::optional<CaptureWorkflows> m_catalog;
  quint64 m_revision = 0;
  bool m_loading = false, m_dirty = false, m_saving = false;
  QListWidget *m_list = nullptr, *m_actions = nullptr;
  QLineEdit *m_name = nullptr, *m_id = nullptr, *m_directory = nullptr, *m_pattern = nullptr, *m_preview = nullptr;
  QCheckBox *m_choose = nullptr;
  QComboBox *m_capture = nullptr;
  QLabel *m_default = nullptr, *m_status = nullptr, *m_patternHelp = nullptr;
  QPushButton *m_save = nullptr, *m_run = nullptr, *m_browse = nullptr;
};
}
std::optional<CaptureWorkflow> manageCaptureWorkflows(NativeCaptureWorkflows &host, QWidget *parent, const std::function<bool()> &canRun) {
  const QPointer<QWidget> owner(parent); const bool hadParent = parent;
  QPointer<WorkflowDialog> dialog(new WorkflowDialog(host, parent, canRun)); dialog->exec();
  const auto selected = dialog ? dialog->selected : std::optional<CaptureWorkflow>{};
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
  return !hadParent || owner ? selected : std::optional<CaptureWorkflow>{};
}
} // namespace xerahs::app
