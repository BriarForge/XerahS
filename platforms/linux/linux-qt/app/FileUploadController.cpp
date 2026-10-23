// CORE-020, CORE-023.
#include "FileUploadController.h"

#include "FileUploadResultDialog.h"
#include "SecretServiceCredentialStore.h"
#include "uploads/CatalogPath.h"
#include "uploads/DestinationCatalog.h"
#include "uploads/UploadExecutor.h"
#include "uploads/UploadRequest.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QProgressDialog>
#include <QPushButton>
#include <QThread>
#include <QTimer>

namespace xerahs::app {
namespace {
QString fileNameOf(const QString &path) {
  const auto name = QFileInfo(path).fileName();
  return name.isEmpty() ? path : name;
}
}  // namespace

FileUploadController::FileUploadController(FileUploadPorts ports, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_ports(std::move(ports)), m_dialogParent(dialogParent),
      m_cap(m_ports.maxInputBytes.value_or(uploads::UploadRequestOptions{}.maxInputBytes)),
      m_network(new QNetworkAccessManager(this)), m_executor(new uploads::UploadExecutor(m_network, this)) {
  connect(m_executor, &uploads::UploadExecutor::progress, this, &FileUploadController::onProgress);
  connect(m_executor, &uploads::UploadExecutor::finished, this, &FileUploadController::onFinished);
}

void FileUploadController::upload(const QStringList &paths) {
  for (const auto &path : paths)
    if (!path.isEmpty()) m_queue.append(path);
  if (!m_busy && !m_queue.isEmpty()) {
    m_busy = true;
    startNext();
  }
}

void FileUploadController::chooseAndUpload() {
  upload(QFileDialog::getOpenFileNames(m_dialogParent, QStringLiteral("Upload file")));
}

void FileUploadController::cancel() {
  if (!m_busy) return;
  m_stop = true;
  m_queue.clear();
  if (m_transferring) m_executor->cancel();
}

void FileUploadController::startNext() {
  if (m_preparing || m_transferring) return;
  if (m_stop || m_queue.isEmpty()) {
    m_stop = false;
    m_busy = false;
    closeProgress();
    emit finished();
    return;
  }
  const auto path = m_queue.takeFirst();
  m_currentName = fileNameOf(path);
  const QFileInfo info(path);
  if (!info.exists()) { showFailure(m_currentName, QStringLiteral("The file does not exist.")); return; }
  if (!info.isFile()) { showFailure(m_currentName, QStringLiteral("The path is not a regular file.")); return; }
  if (!info.isReadable()) { showFailure(m_currentName, QStringLiteral("The file could not be read.")); return; }
  if (info.size() > m_cap) { showFailure(m_currentName, QStringLiteral("input-too-large")); return; }

  const uploads::DestinationCatalog catalog(uploads::defaultCatalogPath(m_ports.catalogPath));
  if (!catalog.loadCode().isEmpty()) {
    showFailure(m_currentName, QStringLiteral("Could not read Upload destinations."));
    return;
  }
  const auto destination = catalog.defaultDestination();
  if (!destination) {
    showFailure(m_currentName, QStringLiteral("No upload destination. Add one in Upload destinations."), true);
    return;
  }

  showProgress(m_currentName);
  auto result = std::make_shared<uploads::UploadRequestResult>();
  const auto credentials = m_ports.credentials;
  const auto cap = m_cap;
  auto *thread = QThread::create([result, destination = *destination, path, credentials, cap] {
    // Keyring lookups block. Request building, including that lookup, stays off the GUI thread.
    const auto store = credentials ? credentials : std::shared_ptr<uploads::CredentialStore>(std::make_shared<SecretServiceCredentialStore>());
    uploads::UploadInput input;
    input.filePath = path;
    input.fileName = QFileInfo(path).fileName();
    input.mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    if (input.mimeType.isEmpty()) input.mimeType = QStringLiteral("application/octet-stream");
    uploads::UploadRequestOptions options;
    options.maxInputBytes = cap;
    *result = uploads::buildUploadRequest(destination, input, *store, options);
  });
  m_preparing = true;
  connect(thread, &QThread::finished, thread, &QObject::deleteLater);
  connect(thread, &QThread::finished, this, [this, result] {
    m_preparing = false;
    if (m_stop) { showFailure(m_currentName, QStringLiteral("cancelled")); return; }
    if (!result->request) {
      closeProgress();
      showFailure(m_currentName, result->errorMessage.isEmpty() ? result->code : result->errorMessage);
      return;
    }
    m_transferring = true;
    m_executor->start(*result->request);
  });
  thread->start();
}

void FileUploadController::showProgress(const QString &fileName) {
  closeProgress();
  m_progress = new QProgressDialog(QStringLiteral("Uploading %1").arg(fileName), QStringLiteral("Cancel"), 0, 0, m_dialogParent);
  m_progress->setObjectName(QStringLiteral("fileUploadProgress"));
  m_progress->setWindowTitle(QStringLiteral("Uploading"));
  m_progress->setWindowModality(Qt::NonModal);
  m_progress->setMinimumDuration(0);
  m_progress->setAutoClose(false);
  m_progress->setAutoReset(false);
  m_progress->setAccessibleName(QStringLiteral("File upload progress"));
  if (auto *button = m_progress->findChild<QPushButton *>()) button->setObjectName(QStringLiteral("cancelFileUpload"));
  connect(m_progress, &QProgressDialog::canceled, this, &FileUploadController::cancel);
  m_progress->show();
}

void FileUploadController::closeProgress() {
  if (!m_progress) return;
  m_progress->hide();
  m_progress->deleteLater();
  m_progress = nullptr;
}

void FileUploadController::showFailure(const QString &fileName, const QString &message, bool missingDestination) {
  closeProgress();
  auto *dialog = new FileUploadResultDialog(fileName, message, missingDestination ? m_ports.openDestinations : std::function<void()>{},
                                            m_dialogParent);
  dialog->show();
  scheduleNext();
}

void FileUploadController::showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome) {
  closeProgress();
  auto *dialog = new FileUploadResultDialog(fileName, outcome, m_dialogParent);
  dialog->show();
  scheduleNext();
}

void FileUploadController::scheduleNext() {
  QTimer::singleShot(0, this, [this] {
    if (!m_busy) return;
    startNext();
  });
}

void FileUploadController::onProgress(qint64 sent, qint64 total) {
  emit progress(sent, total);
  if (!m_progress || total <= 0) return;
  m_progress->setRange(0, 100);
  m_progress->setValue(int(qBound(qint64(0), sent * 100 / total, qint64(100))));
}

void FileUploadController::onFinished(const uploads::UploadOutcome &outcome) {
  if (!m_transferring) return;
  m_transferring = false;
  if (m_stop || outcome.code == QLatin1String("cancelled")) {
    showFailure(m_currentName, QStringLiteral("cancelled"));
    return;
  }
  if (!outcome.code.isEmpty()) {
    showFailure(m_currentName, outcome.errorMessage.isEmpty() ? outcome.code : outcome.errorMessage);
    return;
  }
  showSuccess(m_currentName, outcome);
}
}  // namespace xerahs::app
