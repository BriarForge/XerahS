// CORE-020, CORE-023.
#include "FileUploadController.h"
#include "OwnedWork.h"

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
#include <QTimer>
#include <QUuid>

namespace xerahs::app {
namespace {
struct FilePreparation {
  uploads::UploadRequestResult request;
  QString destinationName;
  bool existingFile = false;
  bool missingDestination = false;
};
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
  enqueue(paths, QStringLiteral("file-upload"));
}

void FileUploadController::uploadFromWatchFolder(const QString &path) {
  enqueue({path}, QStringLiteral("watch-folder"));
}

void FileUploadController::enqueue(const QStringList &paths, const QString &origin) {
  for (const auto &path : paths)
    if (!path.isEmpty()) m_queue.append({QFileInfo(path).absoluteFilePath(), origin});
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
  const auto queued = m_queue.takeFirst();
  const auto path = queued.path;
  m_currentPath = path;
  m_currentName = fileNameOf(path);
  m_historyRecord = {};
  m_historyRecord.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_historyRecord.createdAt = QDateTime::currentDateTimeUtc();
  m_historyRecord.path = path; m_historyRecord.origin = queued.origin;
  m_existingFile = false; m_sensitiveValues.clear();
  showProgress(m_currentName);
  const auto credentials = m_ports.credentials;
  const auto catalogPath = m_ports.catalogPath;
  const auto cap = m_cap;
  m_preparing = true;
  OwnedWork::run<FilePreparation>(this,
      [path, credentials, catalogPath, cap](const OwnedWork::Context &) {
    FilePreparation result;
    const auto fail = [&](const QString &code, const QString &message) {
      result.request.code = code; result.request.errorMessage = message; return result;
    };
    const QFileInfo info(path);
    if (!info.exists()) return fail("input-missing", "The file does not exist.");
    if (!info.isFile()) return fail("input-not-file", "The path is not a regular file.");
    result.existingFile = true;
    const uploads::DestinationCatalog catalog(uploads::defaultCatalogPath(catalogPath));
    const auto destination = catalog.defaultDestination();
    if (destination) result.destinationName = destination->name;
    if (!info.isReadable()) return fail("input-unreadable", "The file could not be read.");
    if (info.size() > cap) return fail("input-too-large", "input-too-large");
    if (!catalog.loadCode().isEmpty()) return fail("destination-catalog-unreadable", "Could not read Upload destinations.");
    if (!destination) {
      result.missingDestination = true;
      return fail("destination-missing", "No upload destination. Add one in Upload destinations.");
    }
    const auto store = credentials ? credentials : std::shared_ptr<uploads::CredentialStore>(std::make_shared<SecretServiceCredentialStore>());
    uploads::UploadInput input;
    input.filePath = path; input.fileName = info.fileName();
    input.mimeType = QMimeDatabase().mimeTypeForFile(path).name();
    if (input.mimeType.isEmpty()) input.mimeType = QStringLiteral("application/octet-stream");
    uploads::UploadRequestOptions options; options.maxInputBytes = cap;
    result.request = uploads::buildUploadRequest(*destination, input, *store, options);
    return result;
  }, [this](FilePreparation result) {
    m_preparing = false;
    m_existingFile = result.existingFile;
    m_historyRecord.destinationName = result.destinationName;
    m_historyRecord.failureCode = result.request.code;
    if (m_stop) { m_historyRecord.failureCode = "cancelled"; showFailure(m_currentName, QStringLiteral("cancelled")); return; }
    if (!result.request.request) {
      showFailure(m_currentName, result.request.errorMessage.isEmpty() ? result.request.code : result.request.errorMessage,
                  result.missingDestination);
      return;
    }
    m_sensitiveValues = result.request.request->sensitiveValues;
    m_transferring = true;
    m_executor->start(*result.request.request);
  });
}

void FileUploadController::recordCompletion(const uploads::UploadOutcome &outcome, std::function<void(QString)> done) {
  if (!m_existingFile) { done({}); return; }
  auto record = m_historyRecord;
  record.result = {outcome.url, outcome.thumbnailUrl, outcome.deletionUrl, outcome.errorMessage};
  // Response templates may echo credentials even into public receipt fields.
  for (auto *field : {&record.result.url, &record.result.thumbnailUrl, &record.result.deletionUrl, &record.result.errorMessage})
    for (const auto &secret : m_sensitiveValues) {
      field->replace(secret, QStringLiteral("[redacted]"));
      field->replace(QString::fromLatin1(QUrl::toPercentEncoding(secret)), QStringLiteral("[redacted]"));
    }
  if (!record.failureCode.isEmpty()) {
    const auto message = record.result.errorMessage;
    record.result.errorMessage = record.failureCode + QStringLiteral(": ") +
        (message.isEmpty() || message == record.failureCode ? QStringLiteral("The file upload did not complete.") : message);
    record.result.url.clear(); record.result.thumbnailUrl.clear(); record.result.deletionUrl.clear();
  }
  m_sensitiveValues.clear();
  const auto storage = m_ports.history ? m_ports.history : std::make_shared<NativePipelineJournal>(NativePipelineJournal::defaultDirectory());
  m_preparing = true;
  OwnedWork::run<JournalWrite>(this, [storage, record](const OwnedWork::Context &) {
    return storage->recordFileUpload(record);
  }, [this, done = std::move(done)](JournalWrite written) {
    m_preparing = false;
    done(written.stored ? QString{} : QStringLiteral("upload-history-write-failed: Could not save the upload to history (%1).").arg(written.diagnostic));
  });
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
  uploads::UploadOutcome outcome; outcome.errorMessage = message;
  if (m_historyRecord.failureCode.isEmpty()) m_historyRecord.failureCode = "upload-failed";
  recordCompletion(outcome, [this, fileName, message, missingDestination](const QString &historyError) {
    emit fileCompleted(m_currentPath, false, message);
    closeProgress();
    auto *dialog = new FileUploadResultDialog(fileName, historyError.isEmpty() ? message : message + '\n' + historyError,
        missingDestination ? m_ports.openDestinations : std::function<void()>{}, m_dialogParent);
    dialog->show(); scheduleNext();
  });
}

void FileUploadController::showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome) {
  m_historyRecord.failureCode.clear();
  recordCompletion(outcome, [this, fileName, outcome](const QString &historyError) {
    emit fileCompleted(m_currentPath, true, {});
    closeProgress();
    auto *dialog = new FileUploadResultDialog(fileName, outcome, m_dialogParent);
    dialog->show();
    if (!historyError.isEmpty()) {
      auto *error = new FileUploadResultDialog(fileName, historyError, {}, m_dialogParent); error->show();
    }
    scheduleNext();
  });
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
  m_historyRecord.failureCode = outcome.code;
  if (m_stop || outcome.code == QLatin1String("cancelled")) {
    m_historyRecord.failureCode = "cancelled";
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
