#include "LegacyHistoryWindow.h"
#include "NativeAccessibility.h"
#include "NativePipelineJournal.h"
#include "LegacyHistorySource.h"
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
namespace xerahs::app {
namespace {
QString text(const char *source) { return QCoreApplication::translate("LegacyHistoryWindow", source); }
constexpr qsizetype pageSize = 200;
}
LegacyHistoryWindow::LegacyHistoryWindow(QWidget *parent, Picker picker, QString archivePath)
    : QDialog(parent, Qt::Window), m_picker(std::move(picker)), m_archivePath(std::move(archivePath)) {
  if (m_archivePath.isEmpty()) m_archivePath = QDir::cleanPath(NativePipelineJournal::defaultDirectory() + "/../legacy-history.json");
  setObjectName("legacyHistoryWindow"); setWindowTitle(text("Legacy history"));
  setAttribute(Qt::WA_DeleteOnClose); resize(900, 560);
  new NativeAccessibility(*this, text("Review and import legacy history privately. Source history remains unchanged. File references and URLs are text only."), false);
  if (!m_picker) m_picker = [](QWidget *owner) {
    return QFileDialog::getOpenFileName(owner, text("Choose legacy history"), {}, text("History files (*.json *.xml *.db *.sqlite *.sqlite3);;All files (*)"));
  };
  auto *layout = new QVBoxLayout(this);
  auto *note = new QLabel(text("Review a private snapshot before importing. Source files stay unchanged; imports are separate from capture results. File references and URLs are text only."), this);
  note->setWordWrap(true); note->setTextFormat(Qt::PlainText); layout->addWidget(note);
  m_table = new QTableWidget(this); m_table->setObjectName("legacyHistoryRecords"); m_table->setAccessibleName(text("Legacy history records"));
  m_table->setColumnCount(8); m_table->setHorizontalHeaderLabels({text("Snapshot / Record"), text("File name"), text("Date as recorded"), text("Type"), text("Host"), text("Local reference"), text("URL"), text("Source task ID")});
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); layout->addWidget(m_table, 1);
  m_status = new QLabel(this);
  m_status->setObjectName("legacyHistoryStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  auto *row = new QHBoxLayout;
  m_choose = new QPushButton(text("&Choose history…"), this); m_choose->setObjectName("chooseLegacyHistory"); row->addWidget(m_choose);
  m_saved = new QPushButton(text("&Saved history"), this); m_saved->setObjectName("savedLegacyHistory"); row->addWidget(m_saved);
  row->addStretch(); layout->addLayout(row);
  auto *importRow = new QHBoxLayout;
  m_import = new QPushButton(text("&Import reviewed snapshot…"), this); m_import->setObjectName("importLegacyHistory"); importRow->addWidget(m_import);
  m_undo = new QPushButton(text("&Undo last import…"), this); m_undo->setObjectName("undoLegacyHistory"); importRow->addWidget(m_undo); importRow->addStretch(); layout->addLayout(importRow);
  auto *recovery = new QHBoxLayout;
  m_recover = new QPushButton(text("Review &recovery snapshot"), this); m_recover->setObjectName("recoverLegacyHistory"); recovery->addWidget(m_recover);
  m_removeRecovery = new QPushButton(text("Remove recovery snapshot…"), this); m_removeRecovery->setObjectName("removeLegacyHistoryRecovery"); recovery->addWidget(m_removeRecovery); recovery->addStretch(); layout->addLayout(recovery);
  auto *navigation = new QHBoxLayout; navigation->addStretch();
  m_previous = new QPushButton(text("&Previous"), this); m_previous->setObjectName("previousLegacyHistory"); navigation->addWidget(m_previous);
  m_next = new QPushButton(text("&Next"), this); m_next->setObjectName("nextLegacyHistory"); navigation->addWidget(m_next);
  auto *close = new QPushButton(text("&Close"), this); navigation->addWidget(close); layout->addLayout(navigation);
  connect(m_choose, &QPushButton::clicked, this, [this] { choose(); });
  connect(m_saved, &QPushButton::clicked, this, [this] { loadArchive(); });
  connect(m_import, &QPushButton::clicked, this, [this] { importSelected(); });
  connect(m_undo, &QPushButton::clicked, this, [this] { undoImport(); });
  connect(m_recover, &QPushButton::clicked, this, [this] { reviewRecovery(); });
  connect(m_removeRecovery, &QPushButton::clicked, this, [this] { removeRecovery(); });
  connect(m_previous, &QPushButton::clicked, this, [this] { --m_page; rebuild(); });
  connect(m_next, &QPushButton::clicked, this, [this] { ++m_page; rebuild(); });
  connect(close, &QPushButton::clicked, this, &QDialog::close);
  rebuild(); loadArchive(); m_choose->setFocus();
}
LegacyHistoryWindow::~LegacyHistoryWindow() { m_work.cancel(); }
void LegacyHistoryWindow::closeEvent(QCloseEvent *event) { m_work.cancel(); QDialog::closeEvent(event); }
void LegacyHistoryWindow::choose() {
  QPointer<LegacyHistoryWindow> alive(this);
  const auto picker = m_picker;
  const auto path = picker(this);
  if (!alive) return;
  if (path.isEmpty()) return;
  setBusy(true);
  m_status->setText(text("Reading legacy history…"));
  m_work = OwnedWork::run<LegacyHistorySourceRead>(this, [path](const OwnedWork::Context &context) {
    return readLegacyHistorySource(path, [&] { return context.cancelled(); }, [&](int percent) { context.progress(percent, 100); });
  }, [this](LegacyHistorySourceRead result) {
    m_preview = std::move(result.preview); m_source = std::move(result.source); m_savedView = false;
    m_page = 0; setBusy(false); rebuild();
  }, [this](qint64 done, qint64) { m_status->setText(text("Reading legacy history… %1%").arg(done)); });
}
void LegacyHistoryWindow::setBusy(bool busy) {
  m_choose->setEnabled(!busy); m_saved->setEnabled(!busy);
  m_import->setEnabled(!busy && m_source.has_value() && (!m_preview.records.empty() || !m_preview.quarantine.isEmpty()) && m_preview.ok() && m_archive.code.isEmpty());
  m_undo->setEnabled(!busy && !m_archive.lastImportId.isEmpty() && m_archive.code.isEmpty());
  m_recover->setEnabled(!busy && m_archive.recoverySource.has_value() && m_archive.code.isEmpty());
  m_removeRecovery->setEnabled(!busy && !m_archive.recoveryImportId.isEmpty() && m_archive.code.isEmpty());
  m_previous->setEnabled(!busy && m_page > 0);
  m_next->setEnabled(!busy && (m_page + 1) * pageSize < qsizetype(m_preview.records.size()));
}
void LegacyHistoryWindow::loadArchive() {
  setBusy(true); m_status->setText(text("Loading saved legacy history…"));
  const auto path = m_archivePath;
  m_work = OwnedWork::run<history::LegacyHistoryArchiveRead>(this,
      [path](const OwnedWork::Context &) { return history::LegacyHistoryArchive(path).read(); },
      [this](history::LegacyHistoryArchiveRead result) {
    m_archive = std::move(result); m_preview = m_archive.preview; m_preview.code = m_archive.code;
    m_source.reset(); m_savedView = true; m_page = 0; setBusy(false); rebuild();
  });
}
void LegacyHistoryWindow::importSelected() {
  QPointer<LegacyHistoryWindow> alive(this);
  const auto answer = QMessageBox::question(this, text("Import legacy history"),
      text("Import %1 reviewed records and retain %2 quarantined records in private storage? Source files remain unchanged. Original snapshot bytes are kept for recovery. The exact same snapshot is not imported twice. Distinct snapshots are kept separately, even when records look identical.")
          .arg(m_preview.records.size()).arg(m_preview.quarantine.size()), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (!alive || answer != QMessageBox::Yes) return;
  setBusy(true); m_status->setText(text("Saving reviewed history…"));
  const auto source = *m_source; const auto expected = m_archive.snapshot;
  writeArchive([source, expected](const history::LegacyHistoryArchive &store, const OwnedWork::Context &context) {
    return store.importSnapshot(source, expected, true, [&] { return context.cancelled(); });
  });
}
void LegacyHistoryWindow::undoImport() {
  QPointer<LegacyHistoryWindow> alive(this);
  const auto answer = QMessageBox::question(this, text("Undo last legacy import"),
      text("Remove the last imported snapshot from saved history? Other imports and source files stay unchanged. Its private source backup remains available for recovery."),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (!alive || answer != QMessageBox::Yes) return;
  setBusy(true); m_status->setText(text("Undoing legacy import…"));
  const auto id = m_archive.lastImportId; const auto expected = m_archive.snapshot;
  writeArchive([id, expected](const history::LegacyHistoryArchive &store, const OwnedWork::Context &context) {
    return store.undo(id, expected, true, [&] { return context.cancelled(); });
  });
}
void LegacyHistoryWindow::reviewRecovery() {
  const auto source = *m_archive.recoverySource;
  setBusy(true); m_status->setText(text("Reviewing the private recovery snapshot…"));
  m_work = OwnedWork::run<LegacyHistorySourceRead>(this, [source](const OwnedWork::Context &) {
    return LegacyHistorySourceRead{history::previewLegacyHistorySnapshot(source), source};
  }, [this](LegacyHistorySourceRead result) {
    m_source = std::move(result.source); m_preview = std::move(result.preview); m_savedView = false;
    m_page = 0; setBusy(false); rebuild();
  });
}
void LegacyHistoryWindow::removeRecovery() {
  QPointer<LegacyHistoryWindow> alive(this);
  const auto answer = QMessageBox::question(this, text("Remove legacy recovery snapshot"),
      text("Permanently remove the last undone import's private backup? XerahS cannot restore it afterwards. Source files and other imports stay unchanged."),
      QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
  if (!alive || answer != QMessageBox::Yes) return;
  setBusy(true); m_status->setText(text("Removing the private recovery snapshot…"));
  const auto id = m_archive.recoveryImportId; const auto expected = m_archive.snapshot;
  writeArchive([id, expected](const history::LegacyHistoryArchive &store, const OwnedWork::Context &context) {
    return store.removeRecovery(id, expected, true, [&] { return context.cancelled(); });
  });
}
void LegacyHistoryWindow::writeArchive(std::function<QString(const history::LegacyHistoryArchive &, const OwnedWork::Context &)> change) {
  const auto path = m_archivePath;
  m_work = OwnedWork::run<history::LegacyHistoryArchiveRead>(this, [path, change = std::move(change)](const OwnedWork::Context &context) {
    history::LegacyHistoryArchive store(path); const auto code = change(store, context);
    if (!code.isEmpty()) { history::LegacyHistoryArchiveRead failed; failed.code = code; return failed; }
    return store.read();
  }, [this](history::LegacyHistoryArchiveRead result) {
    if (!result.code.isEmpty()) { setBusy(false); m_status->setText(text("Could not finish or verify the history change. Reload Saved history before retrying: ") + result.code); return; }
    m_archive = std::move(result); m_preview = m_archive.preview; m_source.reset(); m_savedView = true; m_page = 0; setBusy(false); rebuild();
  });
}
void LegacyHistoryWindow::rebuild() {
  const auto first = m_page * pageSize;
  const auto count = qMin(pageSize, qsizetype(m_preview.records.size()) - first);
  m_table->setRowCount(int(count));
  const QStringList keys{"FileName", "DateTime", "Type", "Host", "FilePath", "URL"};
  for (qsizetype row = 0; row < count; ++row) {
    const auto &record = m_preview.records[size_t(first + row)];
    auto *identity = new QTableWidgetItem(record.archiveImportId.isEmpty() ? QString::number(record.index + 1)
        : record.archiveImportId.left(8) + " / " + QString::number(record.index + 1));
    identity->setToolTip(record.archiveImportId); m_table->setItem(int(row), 0, identity);
    for (int col = 0; col < keys.size(); ++col) m_table->setItem(int(row), col + 1, new QTableWidgetItem(record.fields.value(keys[col]).toString()));
    m_table->setItem(int(row), 7, new QTableWidgetItem(record.format == history::LegacyHistoryFormat::Sqlite ? record.fields.value("Id").toString() : text("Not recorded")));
  }
  m_previous->setEnabled(m_page > 0); m_next->setEnabled(first + count < qsizetype(m_preview.records.size()));
  if (!m_preview.code.isEmpty()) m_status->setText(text("Could not preview history: ") + m_preview.code);
  else if (!m_preview.records.empty() || !m_preview.quarantine.isEmpty()) {
    auto summary = m_preview.report;
    if (summary.records.size() > 10) summary.records.resize(10);
    const auto identity = m_preview.format == history::LegacyHistoryFormat::Json ? text("Legacy JSON has no task IDs or operation states. ")
        : m_preview.format == history::LegacyHistoryFormat::Xml ? text("Legacy XML has no task IDs or operation states. ")
        : text("Supplied source task IDs are retained; missing operation states are not inferred. ");
    auto message = (m_savedView ? text("Saved imported history. Source backups are retained privately. ") : text("Reviewed snapshot; nothing saved until you explicitly import. ")) + identity + summary.detailedSummary();
    if (m_preview.report.records.size() > 10) message += text("\nShowing the first 10 record findings.");
    m_status->setText(message);
  } else m_status->setText(text("No legacy history records. Source unchanged."));
}
}
