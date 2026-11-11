#include "LegacyHistoryWindow.h"
#include "NativeAccessibility.h"
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
namespace xerahs::app {
namespace {
QString text(const char *source) { return QCoreApplication::translate("LegacyHistoryWindow", source); }
constexpr qsizetype pageSize = 200;
}
LegacyHistoryWindow::LegacyHistoryWindow(QWidget *parent, Picker picker) : QDialog(parent, Qt::Window), m_picker(std::move(picker)) {
  setObjectName("legacyHistoryWindow"); setWindowTitle(text("Legacy history preview"));
  setAttribute(Qt::WA_DeleteOnClose); resize(900, 560);
  new NativeAccessibility(*this, text("Read a legacy JSON history file without changing saved history. File references and URLs are text only."), false);
  if (!m_picker) m_picker = [](QWidget *owner) {
    return QFileDialog::getOpenFileName(owner, text("Choose legacy JSON history"), {}, text("JSON history (*.json);;All files (*)"));
  };
  auto *layout = new QVBoxLayout(this);
  auto *note = new QLabel(text("Private, read-only preview. Your source and saved history stay unchanged. File references and URLs are text only."), this);
  note->setWordWrap(true); note->setTextFormat(Qt::PlainText); layout->addWidget(note);
  m_table = new QTableWidget(this); m_table->setObjectName("legacyHistoryRecords"); m_table->setAccessibleName(text("Legacy history records"));
  m_table->setColumnCount(7); m_table->setHorizontalHeaderLabels({text("Record"), text("File name"), text("Date as recorded"), text("Type"), text("Host"), text("Local reference"), text("URL")});
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); layout->addWidget(m_table, 1);
  m_status = new QLabel(this);
  m_status->setObjectName("legacyHistoryStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  auto *row = new QHBoxLayout;
  m_choose = new QPushButton(text("&Choose JSON history…"), this); m_choose->setObjectName("chooseLegacyHistory"); row->addWidget(m_choose);
  row->addStretch();
  m_previous = new QPushButton(text("&Previous"), this); m_previous->setObjectName("previousLegacyHistory"); row->addWidget(m_previous);
  m_next = new QPushButton(text("&Next"), this); m_next->setObjectName("nextLegacyHistory"); row->addWidget(m_next);
  auto *close = new QPushButton(text("&Close"), this); row->addWidget(close); layout->addLayout(row);
  connect(m_choose, &QPushButton::clicked, this, [this] { choose(); });
  connect(m_previous, &QPushButton::clicked, this, [this] { --m_page; rebuild(); });
  connect(m_next, &QPushButton::clicked, this, [this] { ++m_page; rebuild(); });
  connect(close, &QPushButton::clicked, this, &QDialog::close);
  rebuild(); m_status->setText(text("Choose a legacy JSON history file. XML and SQLite are not supported here.")); m_choose->setFocus();
}
LegacyHistoryWindow::~LegacyHistoryWindow() { m_work.cancel(); }
void LegacyHistoryWindow::closeEvent(QCloseEvent *event) { m_work.cancel(); QDialog::closeEvent(event); }
void LegacyHistoryWindow::choose() {
  QPointer<LegacyHistoryWindow> alive(this);
  const auto picker = m_picker;
  const auto path = picker(this);
  if (!alive) return;
  if (path.isEmpty()) return;
  m_choose->setEnabled(false); m_previous->setEnabled(false); m_next->setEnabled(false);
  m_status->setText(text("Reading legacy history…"));
  m_work = OwnedWork::run<history::LegacyHistoryPreview>(this, [path](const OwnedWork::Context &context) {
    history::LegacyHistoryPreview result;
    QFile file(path);
    if (!QFileInfo(path).isFile()) { result.code = "legacy-history-unreadable"; return result; }
    if (!file.open(QIODevice::ReadOnly)) { result.code = "legacy-history-unreadable"; return result; }
    const auto bytes = file.read(history::kLegacyHistoryMaxBytes + 1);
    if (file.error() != QFileDevice::NoError) { result.code = "legacy-history-unreadable"; return result; }
    if (context.cancelled()) { result.code = "cancelled"; return result; }
    return history::previewLegacyHistory(bytes);
  }, [this](history::LegacyHistoryPreview result) {
    m_preview = std::move(result); m_page = 0; m_choose->setEnabled(true); rebuild();
  });
}
void LegacyHistoryWindow::rebuild() {
  const auto first = m_page * pageSize;
  const auto count = qMin(pageSize, qsizetype(m_preview.records.size()) - first);
  m_table->setRowCount(int(count));
  const QStringList keys{"FileName", "DateTime", "Type", "Host", "FilePath", "URL"};
  for (qsizetype row = 0; row < count; ++row) {
    const auto &record = m_preview.records[size_t(first + row)];
    m_table->setItem(int(row), 0, new QTableWidgetItem(QString::number(record.index + 1)));
    for (int col = 0; col < keys.size(); ++col) m_table->setItem(int(row), col + 1, new QTableWidgetItem(record.fields.value(keys[col]).toString()));
  }
  m_previous->setEnabled(m_page > 0); m_next->setEnabled(first + count < qsizetype(m_preview.records.size()));
  if (!m_preview.code.isEmpty()) m_status->setText(text("Could not preview history: ") + m_preview.code);
  else if (!m_preview.records.empty() || !m_preview.quarantine.isEmpty()) {
    auto summary = m_preview.report;
    if (summary.records.size() > 10) summary.records.resize(10);
    auto message = text("Source unchanged. Legacy JSON has no task IDs or operation states. ") + summary.detailedSummary();
    if (m_preview.report.records.size() > 10) message += text("\nShowing the first 10 record findings.");
    m_status->setText(message);
  } else m_status->setText(text("No legacy history records. Source unchanged."));
}
}
