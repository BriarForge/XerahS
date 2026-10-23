#include "CaptureHistoryWindow.h"
#include "PipelineResultDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <mutex>
namespace xerahs::app {
namespace {
constexpr int kThumbnail = 160;
QImage loadThumbnail(const QString &path) {
  QImageReader reader(path); reader.setAutoTransform(true);
  const QSize size = reader.size();
  // Bounded decode: never expand a hostile header into a huge allocation.
  if (!size.isValid() || qint64(size.width()) * size.height() > 268435456) return {};
  reader.setScaledSize(size.scaled(kThumbnail, kThumbnail, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
  return reader.read();
}
QIcon placeholder(const QString &text, const QPalette &palette) {
  QImage image(kThumbnail, kThumbnail * 3 / 4, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
  QPainter painter(&image); painter.setPen(QPen(palette.color(QPalette::Mid), 2, Qt::DashLine));
  painter.drawRect(image.rect().adjusted(2, 2, -3, -3)); painter.setPen(palette.color(QPalette::PlaceholderText));
  painter.drawText(image.rect(), Qt::AlignCenter, text);
  return QIcon(QPixmap::fromImage(image));
}
// History labels are deliberately local: workflows::captureSourceLabel names
// "monitor" "Monitor under pointer" and has no "window" entry, so sharing it
// would change labels already shown in history. Unknown ids stay visible.
QString sourceName(const QString &source) {
  if (source == "full-screen") return QStringLiteral("Full screen");
  if (source == "monitor") return QStringLiteral("Monitor");
  if (source == "window") return QStringLiteral("Window");
  if (source == "region") return QStringLiteral("Region");
  if (source == "active-window") return QStringLiteral("Active window");
  return source;
}
}
struct CaptureHistoryJob {
  enum Kind { List, Thumbnails, Erase } kind = List;
  std::mutex mutex;
  bool ready = false;
  JournalListing listing;
  QStringList paths;
  QHash<QString, QImage> thumbnails;
  QSet<QString> missing;
  JournalWrite erased;
};

CaptureHistoryWindow::CaptureHistoryWindow(PipelineJournalStorage storage, std::function<bool()> captureBusy, EditCallback edit, QWidget *parent)
    : QWidget(parent, Qt::Window), m_storage(std::move(storage)), m_captureBusy(std::move(captureBusy)), m_edit(std::move(edit)) {
  setObjectName(QStringLiteral("captureHistoryWindow")); setWindowTitle(QStringLiteral("Capture history"));
  setAttribute(Qt::WA_DeleteOnClose); resize(900, 620);
  auto *layout = new QVBoxLayout(this);
  m_search = new QLineEdit(this); m_search->setObjectName(QStringLiteral("captureHistorySearch"));
  m_search->setPlaceholderText(QStringLiteral("Search by file name, folder, workflow, source or date (yyyy-MM-dd)"));
  m_search->setAccessibleName(QStringLiteral("Search capture history")); m_search->setClearButtonEnabled(true);
  layout->addWidget(m_search);
  m_list = new QListWidget(this); m_list->setObjectName(QStringLiteral("captureHistoryItems"));
  m_list->setAccessibleName(QStringLiteral("Captures")); m_list->setViewMode(QListView::IconMode);
  m_list->setIconSize(QSize(kThumbnail, kThumbnail)); m_list->setGridSize(QSize(kThumbnail + 40, kThumbnail + 56));
  m_list->setResizeMode(QListView::Adjust); m_list->setMovement(QListView::Static); m_list->setWordWrap(true);
  m_list->setUniformItemSizes(true); m_list->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(m_list, 1);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("captureHistoryWindowStatus"));
  m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto *row = new QHBoxLayout; auto *second = new QHBoxLayout;
  const auto button = [this](QHBoxLayout *target, const char *name, const QString &text) {
    auto *b = new QPushButton(text, this); b->setObjectName(QLatin1String(name)); target->addWidget(b); return b;
  };
  m_open = button(row, "openHistoryCapture", QStringLiteral("&Open"));
  m_editButton = button(row, "editHistoryCapture", QStringLiteral("&Edit"));
  m_copyImage = button(row, "copyHistoryImage", QStringLiteral("Copy &image"));
  m_copyPath = button(row, "copyHistoryPath", QStringLiteral("Copy &path"));
  m_reveal = button(row, "revealHistoryCapture", QStringLiteral("Show in &folder"));
  auto *copyUrl = button(row, "copyHistoryUrl", QStringLiteral("Copy URL"));
  connect(copyUrl, &QPushButton::clicked, this, [this] {
    const auto *item = selectedItem(); if (!item) return;
    const auto entry = m_entries.value(item->taskId); if (!entry.parsed.record) return;
    const auto url = entry.parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject()
        .value("upload_result").toObject().value("url").toString();
    if (!url.isEmpty()) QApplication::clipboard()->setText(url);
  });
  row->addStretch();
  m_details = button(second, "historyCaptureDetails", QStringLiteral("&Details…"));
  m_trash = button(second, "trashHistoryCapture", QStringLiteral("Move to &trash…"));
  m_forget = button(second, "forgetHistoryCapture", QStringLiteral("Forget…"));
  second->addStretch();
  m_more = button(second, "moreCaptureHistory", QStringLiteral("Load &more"));
  m_refresh = button(second, "refreshCaptureHistoryWindow", QStringLiteral("&Refresh"));
  m_records = button(second, "openCaptureResultRecords", QStringLiteral("Result &records…")); m_records->hide();
  connect(m_records, &QPushButton::clicked, this, [this] { if (m_technical) m_technical(); });
  layout->addLayout(row); layout->addLayout(second);
  connect(m_open, &QPushButton::clicked, this, &CaptureHistoryWindow::openFile);
  connect(m_editButton, &QPushButton::clicked, this, &CaptureHistoryWindow::editFile);
  connect(m_copyImage, &QPushButton::clicked, this, &CaptureHistoryWindow::copyImage);
  connect(m_copyPath, &QPushButton::clicked, this, &CaptureHistoryWindow::copyPath);
  connect(m_reveal, &QPushButton::clicked, this, &CaptureHistoryWindow::showInFolder);
  connect(m_details, &QPushButton::clicked, this, &CaptureHistoryWindow::details);
  connect(m_trash, &QPushButton::clicked, this, &CaptureHistoryWindow::trashFile);
  connect(m_forget, &QPushButton::clicked, this, &CaptureHistoryWindow::forget);
  connect(m_refresh, &QPushButton::clicked, this, &CaptureHistoryWindow::refresh);
  connect(m_more, &QPushButton::clicked, this, [this] { ++m_pages; rebuild(); });
  connect(m_search, &QLineEdit::textChanged, this, [this] { m_pages = 1; rebuild(); });
  connect(m_list, &QListWidget::itemSelectionChanged, this, &CaptureHistoryWindow::updateAvailability);
  connect(m_list, &QListWidget::itemActivated, this, [this] { m_edit ? editFile() : openFile(); });
  m_timer = new QTimer(this); m_timer->setInterval(20); connect(m_timer, &QTimer::timeout, this, &CaptureHistoryWindow::poll);
  m_timer->start(); refresh(); m_search->setFocus();
}
CaptureHistoryWindow::~CaptureHistoryWindow() = default;
void CaptureHistoryWindow::setRecordsOpener(std::function<void()> open) { m_technical = std::move(open); m_records->setVisible(bool(m_technical)); }

const xerahs::history::CaptureHistoryItem *CaptureHistoryWindow::selectedItem() const {
  const auto *item = m_list->currentItem(); if (!item || !item->isSelected()) return nullptr;
  const auto id = item->data(Qt::UserRole).toString();
  for (const auto &entry : m_page) if (entry.taskId == id) return &entry;
  return nullptr;
}
QString CaptureHistoryWindow::selectedPath() const {
  const auto *item = selectedItem(); return item ? item->files.front().path : QString{};
}
void CaptureHistoryWindow::updateAvailability() {
  const auto path = selectedPath(); const bool idle = !m_job && !m_confirming;
  const bool busy = m_captureBusy && m_captureBusy();
  const bool present = !path.isEmpty() && !m_missing.contains(path);
  const bool image = present && !m_thumbnails.value(path).isNull();
  QString url;
  if (const auto *item = selectedItem()) {
    const auto entry = m_entries.value(item->taskId);
    if (entry.parsed.record) url = entry.parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject()
        .value("upload_result").toObject().value("url").toString();
  }
  findChild<QPushButton *>("copyHistoryUrl")->setEnabled(!url.isEmpty());
  m_open->setEnabled(present); m_editButton->setEnabled(image && bool(m_edit)); m_copyImage->setEnabled(image);
  m_copyPath->setEnabled(!path.isEmpty()); m_reveal->setEnabled(present);
  m_details->setEnabled(!path.isEmpty()); m_trash->setEnabled(present && idle);
  m_forget->setEnabled(!path.isEmpty() && idle && !busy); m_refresh->setEnabled(idle && !busy);
}

void CaptureHistoryWindow::refresh() {
  if (m_job || m_confirming || !m_storage) return;
  if (m_captureBusy && m_captureBusy()) {
    m_reloadWhenIdle = true; m_status->setText(QStringLiteral("A capture is running. History will refresh when it finishes.")); updateAvailability(); return;
  }
  m_reloadWhenIdle = false;
  m_job = std::make_shared<CaptureHistoryJob>(); const auto job = m_job; const auto storage = m_storage;
  m_status->setText(QStringLiteral("Reading capture history…")); updateAvailability();
  auto *worker = QThread::create([job, storage] {
    auto listing = storage->list(); std::lock_guard<std::mutex> lock(job->mutex);
    job->listing = std::move(listing); job->ready = true;
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
}

void CaptureHistoryWindow::poll() {
  if (!m_job) {
    if (m_reloadWhenIdle && !m_confirming && (!m_captureBusy || !m_captureBusy())) refresh();
    return;
  }
  const auto job = m_job;
  { std::lock_guard<std::mutex> lock(job->mutex); if (!job->ready) return; }
  m_job.reset();
  switch (job->kind) {
  case CaptureHistoryJob::Erase:
    if (!job->erased.stored) { m_status->setText(QStringLiteral("Could not forget the capture (%1).").arg(job->erased.diagnostic)); updateAvailability(); return; }
    refresh(); return;
  case CaptureHistoryJob::Thumbnails:
    for (auto it = job->thumbnails.cbegin(); it != job->thumbnails.cend(); ++it) m_thumbnails.insert(it.key(), it.value());
    for (const auto &path : job->paths) { if (job->missing.contains(path)) m_missing.insert(path); else m_missing.remove(path); }
    rebuild(); return;
  case CaptureHistoryJob::List: break;
  }
  // A capture may have started during the read. Its live write-ahead record is
  // not a finished capture; read again once it completes.
  if (m_captureBusy && m_captureBusy()) { m_reloadWhenIdle = true; updateAvailability(); return; }
  std::vector<xerahs::actions::RecoveredPipelineJournal> records; m_entries.clear();
  for (const auto &entry : job->listing.entries) {
    if (!entry.parsed.record) continue;
    records.push_back(*entry.parsed.record); m_entries.insert(entry.taskId, entry);
  }
  m_items = xerahs::history::captureHistoryItems(records);
  // Files may have been moved or restored since the last read.
  m_thumbnails.clear(); m_missing.clear();
  if (!job->listing.diagnostic.isEmpty()) m_status->setText(QStringLiteral("Some records could not be read (%1). Readable captures are shown.").arg(job->listing.diagnostic));
  rebuild();
}

void CaptureHistoryWindow::loadThumbnails() {
  if (m_job) return;
  QStringList wanted;
  for (const auto &item : m_page) if (!m_thumbnails.contains(item.files.front().path) && !m_missing.contains(item.files.front().path)) wanted << item.files.front().path;
  if (wanted.isEmpty()) return;
  m_job = std::make_shared<CaptureHistoryJob>(); m_job->kind = CaptureHistoryJob::Thumbnails; m_job->paths = wanted;
  const auto job = m_job;
  auto *worker = QThread::create([job] {
    QHash<QString, QImage> thumbnails; QSet<QString> missing;
    for (const auto &path : job->paths) {
      if (!QFileInfo::exists(path)) missing.insert(path);
      else thumbnails.insert(path, loadThumbnail(path));
    }
    std::lock_guard<std::mutex> lock(job->mutex);
    job->thumbnails = std::move(thumbnails); job->missing = std::move(missing); job->ready = true;
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
}

void CaptureHistoryWindow::rebuild() {
  const auto selected = selectedItem() ? selectedItem()->taskId : QString{};
  int matches = 0;
  m_page = xerahs::history::historyPage(m_items, m_search->text(), m_pages, &matches);
  const QSignalBlocker blocker(m_list); m_list->clear();
  const QIcon loading = placeholder(QStringLiteral("Loading…"), palette());
  const QIcon missing = placeholder(QStringLiteral("File missing"), palette());
  const QIcon unreadable = placeholder(QStringLiteral("No preview"), palette());
  for (const auto &entry : m_page) {
    const auto &file = entry.files.front();
    QStringList lines{QFileInfo(file.path).fileName(), entry.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))};
    QStringList detail;
    if (!entry.captureSource.isEmpty()) detail << sourceName(entry.captureSource);
    if (file.width > 0 && file.height > 0) detail << QStringLiteral("%1 × %2").arg(file.width).arg(file.height);
    if (!detail.isEmpty()) lines << detail.join(QStringLiteral(", "));
    auto *item = new QListWidgetItem(lines.join('\n'), m_list);
    item->setData(Qt::UserRole, entry.taskId);
    QString tip = file.path;
    const auto record = m_entries.value(entry.taskId);
    if (record.parsed.record) {
      const auto url = record.parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject()
          .value("upload_result").toObject().value("url").toString();
      if (!url.isEmpty()) { tip += "\nURL: " + url; item->setText(item->text() + "\n" + url); }
    }
    if (!entry.workflowName.isEmpty()) tip += QStringLiteral("\nWorkflow: ") + entry.workflowName;
    if (entry.state != "completed") tip += QStringLiteral("\nResult: ") + entry.state;
    if (m_missing.contains(file.path)) { item->setIcon(missing); tip += QStringLiteral("\nThe file is no longer at this path."); }
    else if (!m_thumbnails.contains(file.path)) item->setIcon(loading);
    else if (m_thumbnails.value(file.path).isNull()) item->setIcon(unreadable);
    else item->setIcon(QIcon(QPixmap::fromImage(m_thumbnails.value(file.path))));
    item->setToolTip(tip);
    if (entry.taskId == selected) { m_list->setCurrentItem(item); item->setSelected(true); }
  }
  m_more->setVisible(int(m_page.size()) < matches);
  if (!m_job || m_job->kind != CaptureHistoryJob::List) {
    if (m_items.empty()) m_status->setText(QStringLiteral("No saved captures yet. Captures saved to a file appear here."));
    else if (matches == 0) m_status->setText(QStringLiteral("No captures match the search."));
    else m_status->setText(matches == int(m_items.size()) ? QStringLiteral("%1 captures.").arg(matches)
        : QStringLiteral("%1 of %2 captures match.").arg(matches).arg(m_items.size()));
  }
  updateAvailability(); loadThumbnails();
}

void CaptureHistoryWindow::openFile() {
  const auto path = selectedPath(); if (path.isEmpty() || m_missing.contains(path)) return;
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) m_status->setText(QStringLiteral("No application could open this file."));
}
void CaptureHistoryWindow::editFile() {
  const auto path = selectedPath(); if (path.isEmpty() || m_missing.contains(path) || !m_edit) return;
  m_edit(path, this);
}
void CaptureHistoryWindow::copyImage() {
  const auto path = selectedPath(); if (path.isEmpty()) return;
  QImageReader reader(path); reader.setAutoTransform(true); const QImage image = reader.read();
  if (image.isNull()) { m_status->setText(QStringLiteral("The image could not be read, so nothing was copied.")); return; }
  QApplication::clipboard()->setImage(image); m_status->setText(QStringLiteral("Image copied."));
}
void CaptureHistoryWindow::copyPath() {
  const auto path = selectedPath(); if (path.isEmpty()) return;
  QApplication::clipboard()->setText(path); m_status->setText(QStringLiteral("Path copied."));
}
void CaptureHistoryWindow::showInFolder() {
  const auto path = selectedPath(); if (path.isEmpty()) return;
  auto message = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
      "org.freedesktop.FileManager1", "ShowItems");
  message << QStringList{QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)} << QString{};
  // Without a file manager service, open the folder instead.
  auto bus = QDBusConnection::sessionBus();
  const bool fileManager = bus.isConnected() && bus.interface() &&
      bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.FileManager1")).value();
  if ((!fileManager || !bus.send(message)) && !QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())))
    m_status->setText(QStringLiteral("No file manager could show this folder."));
}
void CaptureHistoryWindow::details() {
  const auto *item = selectedItem(); if (!item) return;
  const auto entry = m_entries.value(item->taskId); if (!entry.parsed.record) return;
  const auto &record = *entry.parsed.record;
  makePipelineResultDialog(record.report, this, record.selected, record.pendingAction.value_or(QString{}))->open();
}
void CaptureHistoryWindow::trashFile() {
  const auto path = selectedPath(); if (path.isEmpty() || m_confirming) return;
  m_confirming = true; updateAvailability();
  auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Move capture to trash?"),
      QStringLiteral("Move %1 to the trash? You can restore it from the trash.").arg(QFileInfo(path).fileName()),
      QMessageBox::Yes | QMessageBox::Cancel, this);
  box->setObjectName(QStringLiteral("trashHistoryCaptureDialog")); box->setDefaultButton(QMessageBox::Cancel);
  box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box, &QMessageBox::finished, this, [this, path](int choice) {
    m_confirming = false;
    if (choice == QMessageBox::Yes) {
      QFile file(path);
      if (file.moveToTrash()) { m_missing.insert(path); m_status->setText(QStringLiteral("Moved to the trash.")); rebuild(); return; }
      m_status->setText(QStringLiteral("The file could not be moved to the trash (%1).").arg(file.errorString()));
    }
    updateAvailability();
  });
  box->open();
}
void CaptureHistoryWindow::forget() {
  if (m_job || m_confirming || (m_captureBusy && m_captureBusy())) return;
  const auto *item = selectedItem(); if (!item || !m_entries.contains(item->taskId)) return;
  const auto entry = m_entries.value(item->taskId); m_confirming = true; updateAvailability();
  auto *box = new QMessageBox(QMessageBox::Question, QStringLiteral("Forget capture?"),
      QStringLiteral("Remove this capture from history? The saved file is kept."),
      QMessageBox::Discard | QMessageBox::Cancel, this);
  box->setObjectName(QStringLiteral("forgetHistoryCaptureDialog")); box->setDefaultButton(QMessageBox::Cancel);
  box->setEscapeButton(QMessageBox::Cancel); box->setAttribute(Qt::WA_DeleteOnClose);
  connect(box, &QMessageBox::finished, this, [this, entry](int choice) {
    m_confirming = false;
    if (choice != QMessageBox::Discard || m_job || (m_captureBusy && m_captureBusy())) { updateAvailability(); return; }
    m_job = std::make_shared<CaptureHistoryJob>(); m_job->kind = CaptureHistoryJob::Erase;
    const auto job = m_job; const auto storage = m_storage; updateAvailability();
    auto *worker = QThread::create([job, storage, entry] {
      const auto erased = storage->erase(entry.taskId, entry.bytes); std::lock_guard<std::mutex> lock(job->mutex);
      job->erased = erased; job->ready = true;
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start();
  });
  box->open();
}
} // namespace xerahs::app
