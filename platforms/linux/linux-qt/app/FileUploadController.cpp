// CORE-020, CORE-023.
#include "FileUploadController.h"
#include "actions/PipelineResultJson.h"
#include <QDebug>
#include "OwnedWork.h"

#include "FileUploadResultDialog.h"
#include "NativeWorkflowExecutions.h"
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
  QString destinationName, destinationId;
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
  if (dialogParent) {
    connect(dialogParent, &QObject::destroyed, this, [this] {
      if (m_progress)
        QObject::disconnect(m_progress.data(), &QProgressDialog::canceled, this, &FileUploadController::cancel);
    });
  }
  connect(m_executor, &uploads::UploadExecutor::progress, this, &FileUploadController::onProgress);
  connect(m_executor, &uploads::UploadExecutor::finished, this, &FileUploadController::onFinished);
}

void FileUploadController::upload(const QStringList &paths) {
  enqueue(paths, QStringLiteral("file-upload"));
}

void FileUploadController::uploadFromWatchFolder(const QString &path, const QJsonObject &rule) {
  enqueue({path}, QStringLiteral("watch-folder"), rule);
}

void FileUploadController::enqueue(const QStringList &paths, const QString &origin, const QJsonObject &rule) {
  for (const auto &path : paths)
    if (!path.isEmpty()) m_queue.append({QFileInfo(path).absoluteFilePath(), origin, rule});
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
  const auto abandoned = std::move(m_queue);
  m_queue.clear();
  const QPointer<FileUploadController> owner(this);
  if (m_transferring) m_executor->cancel();
  if (!owner) return;
  // A queued watch item owns a durable intent but has not started a request.
  // Return it to the watch service so cancellation can release that intent.
  for (const auto &file : abandoned) {
    if (file.origin != QLatin1String("watch-folder")) continue;
    emit publicationResolved(file.path, UploadPublication::Unsent, 0, 0);
    if (!owner) return;
    emit fileCompleted(file.path, false, QStringLiteral("cancelled"));
    if (!owner) return;
  }
}

void FileUploadController::startNext() {
  if (m_preparing || m_transferring) return;
  // cancel() already dropped the queue it owned. A file that arrived after that
  // is a new explicit upload, not a retry of the cancelled transfer.
  if (m_queue.isEmpty()) {
    m_stop = false;
    m_busy = false;
    closeProgress();
    emit finished();
    return;
  }
  m_stop = false;
  const auto queued = m_queue.takeFirst();
  const auto path = queued.path;
  m_currentPath = path;
  m_currentName = fileNameOf(path);
  m_historyRecord = {};
  m_historyRecord.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_historyRecord.createdAt = QDateTime::currentDateTimeUtc();
  m_historyRecord.path = path; m_historyRecord.origin = queued.origin;
  m_existingFile = false; m_sensitiveValues.clear();
  m_rule = queued.rule; m_requestBuilt = false; m_httpStatus = 0; m_executionOutputs.clear(); m_executionStored = true; m_execution.reset();
  // CORE-025: the attempt facts reset with every queued file.
  m_uploadAttempts = 0; m_answeredAttempts = 0; m_requestStarted = false;
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
    if (destination) { result.destinationName = destination->name; result.destinationId = destination->id.toString(QUuid::WithoutBraces); }
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
    m_destinationId = result.destinationId;
    // WFCAT-002: every invocation owns one execution from admission, even a missing file.
    m_execution = std::make_unique<workflows::WorkflowExecution>(m_historyRecord.taskId, m_historyRecord.origin,
        workflows::fileOperationSnapshot(QStringLiteral("upload"), m_destinationId, result.destinationName,
            m_rule.isEmpty() ? std::nullopt : std::optional<QJsonObject>(m_rule)),
        m_ports.executions ? m_ports.executions : NativeWorkflowExecutions(NativeWorkflowExecutions::defaultDirectory()).writer());
    m_executionStored = m_execution->admit() && m_execution->start();
    if (!m_executionStored) {
      m_historyRecord.failureCode = QStringLiteral("workflow-storage-unavailable");
      showFailure(m_currentName, QStringLiteral("workflow-storage-unavailable: The upload was not sent because its execution record could not be saved."));
      return;
    }
    m_historyRecord.failureCode = result.request.code;
    if (m_stop) { m_historyRecord.failureCode = "cancelled"; showFailure(m_currentName, QStringLiteral("cancelled")); return; }
    if (!result.request.request) {
      showFailure(m_currentName, result.request.errorMessage.isEmpty() ? result.request.code : result.request.errorMessage,
                  result.missingDestination);
      return;
    }
    m_sensitiveValues = result.request.request->sensitiveValues;
    if (!m_execution->step(QStringLiteral("read-file"), QStringLiteral("succeeded"))) {
      m_executionStored = false;
      m_historyRecord.failureCode = QStringLiteral("workflow-storage-unavailable");
      showFailure(m_currentName, QStringLiteral("workflow-storage-unavailable: The upload was not sent because its execution record could not be saved."));
      return;
    }
    m_requestBuilt = true;
    m_transferring = true;
    // CORE-025 P1: the request is started here. A request that never reaches
    // this point is unsent, not an unknown publication.
    m_requestStarted = true;
    m_executor->start(*result.request.request);
  });
}

void FileUploadController::recordCompletion(const uploads::UploadOutcome &outcome, std::function<void(QString)> done) {
  if (!m_existingFile) { done({}); return; }
  auto record = m_historyRecord;
  record.result = {outcome.url, outcome.thumbnailUrl, outcome.deletionUrl, outcome.errorMessage, outcome.attempts};
  // Response templates may echo credentials even into public receipt fields.
  for (auto *field : {&record.result.url, &record.result.thumbnailUrl, &record.result.deletionUrl, &record.result.errorMessage})
    for (const auto &secret : m_sensitiveValues) {
      field->replace(secret, QStringLiteral("[redacted]"));
      field->replace(QString::fromLatin1(QUrl::toPercentEncoding(secret)), QStringLiteral("[redacted]"));
    }
  // A sent request without an acknowledgement is not a failed publication.
  // Keep the stable code on the execution; the journal stays interrupted.
  const bool publicationUnknown = m_requestBuilt && workflows::uploadRemoteOutcomeUnknown(record.failureCode, record.result.attempts);
  if (!publicationUnknown && !record.failureCode.isEmpty() && record.failureCode != QLatin1String("cancelled"))
    record.failureClass = workflows::classifyFileOperationFailure(record.failureCode, m_httpStatus);
  if (!record.failureCode.isEmpty()) {
    const auto message = record.result.errorMessage;
    if (!publicationUnknown && !message.startsWith(record.failureCode + QStringLiteral(": ")))
      record.result.errorMessage = record.failureCode + QStringLiteral(": ") +
          (message.isEmpty() || message == record.failureCode ? QStringLiteral("The file upload did not complete.") : message);
    else if (message.isEmpty())
      record.result.errorMessage = record.failureCode + QStringLiteral(": remote publication is unconfirmed. Review the destination before retrying.");
    record.result.url.clear(); record.result.thumbnailUrl.clear(); record.result.deletionUrl.clear();
  }
  m_sensitiveValues.clear();
  const auto storage = m_ports.history ? m_ports.history : std::make_shared<NativePipelineJournal>(NativePipelineJournal::defaultDirectory());
  m_preparing = true;
  OwnedWork::run<JournalWrite>(this, [storage, record, publicationUnknown](const OwnedWork::Context &) {
    return storage->recordFileUpload(record, publicationUnknown ? FileUploadJournalState::Interrupted : FileUploadJournalState::Terminal);
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

// Commit the terminal state. An upload step reports success only after the
// destination answered with a usable result; the execution then settles once.
workflows::FileOperationNotice FileUploadController::settle(bool uploaded) {
  using namespace workflows;
  const auto code = fileOperationDiagnostic(m_historyRecord.failureCode);
  const QString action = m_requestBuilt ? QStringLiteral("upload-file") : QStringLiteral("read-file");
  std::optional<FileOperationTerminal> terminal;
  if (m_execution) {
    if (uploaded) m_executionStored = m_execution->step(QStringLiteral("upload-file"), QStringLiteral("succeeded")) && m_executionStored;
    else if (code == QLatin1String("cancelled")) m_executionStored = m_execution->step(action, QStringLiteral("cancelled")) && m_executionStored;
    else if (!(m_requestBuilt && workflows::uploadRemoteOutcomeUnknown(code, m_historyRecord.result.attempts)))
      m_executionStored = m_execution->step(action, QStringLiteral("failed"), code, classifyFileOperationFailure(code, m_httpStatus)) && m_executionStored;
    terminal = fileOperationTerminal(m_historyRecord.taskId, QStringLiteral("upload"), m_execution->record().steps, uploaded ? QString{} : code, m_requestBuilt ? m_historyRecord.result.attempts : 0);
    m_executionStored = m_execution->finish(terminal->state, terminal->diagnostic, terminal->outputs) && m_executionStored;
  }
  m_terminalObservation = m_execution ? m_execution->recordedObservation() : QJsonObject{{"task_id", m_historyRecord.taskId}, {"state", "outcome-unconfirmed"}, {"outputs", QJsonArray{}}};
  const auto state = m_terminalObservation.value("state").toString();
  m_executionOutputs.clear();
  for (const auto &output : m_terminalObservation.value("outputs").toArray()) m_executionOutputs.append(output.toString());
  const auto text = actions::pipelineObservationText(m_terminalObservation);
  const auto notice = workflows::fileOperationNotice(QStringLiteral("upload"), state, m_currentName);
  const auto path = m_currentPath, id = m_historyRecord.taskId; const auto outputs = m_executionOutputs;
  const auto observation = m_terminalObservation;
  m_execution.reset();
  qInfo().noquote() << "Workflow terminal" << text;
  const QPointer<FileUploadController> guard(this);
  emit activityChanged(text); if (!guard) return notice;
  emit terminalObserved(observation); if (!guard) return notice;
  emit notify(state == "succeeded" ? QStringLiteral("File upload completed") : QStringLiteral("File upload needs review"), text, state == "succeeded");
  if (guard) emit executionFinished(path, id, state, outputs);
  return notice;
}

void FileUploadController::showFailure(const QString &fileName, const QString &message, bool missingDestination, const QString &detail) {
  uploads::UploadOutcome outcome; outcome.errorMessage = detail.isEmpty() ? message : message + ": " + detail;
  outcome.attempts = m_historyRecord.result.attempts;
  outcome.answeredAttempts = m_answeredAttempts;
  if (m_historyRecord.failureCode.isEmpty()) m_historyRecord.failureCode = "upload-failed";
  const QPointer<FileUploadController> guard(this);
  const auto notice = settle(false);
  if (!guard) return;
  const auto storage = m_executionStored ? QString{} : QStringLiteral("workflow-storage-unavailable: Could not save the upload execution record.");
  recordCompletion(outcome, [this, fileName, message, missingDestination, detail, notice, storage](const QString &historyError) {
    // CORE-025: the publication facts are published before the completion, so
    // the watch service can apply the disposition to this very attempt.
    const QPointer<FileUploadController> owner(this);
    emit publicationResolved(m_currentPath, publication(), m_uploadAttempts, m_answeredAttempts);
    if (!owner) return;
    emit fileCompleted(m_currentPath, false, message);
    closeProgress();
    QStringList text{message}; if (!historyError.isEmpty()) text.append(historyError); if (!storage.isEmpty()) text.append(storage);
    auto *dialog = new FileUploadResultDialog(fileName, text.join('\n'),
        missingDestination ? m_ports.openDestinations : std::function<void()>{}, m_dialogParent, notice.headline + QLatin1Char('\n') + actions::pipelineObservationText(m_terminalObservation), detail);
    dialog->show(); scheduleNext();
  });
}

void FileUploadController::showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome) {
  m_historyRecord.failureCode.clear();
  const QPointer<FileUploadController> guard(this);
  const auto notice = settle(true);
  if (!guard) return;
  const auto storage = m_executionStored ? QString{} : QStringLiteral("workflow-storage-unavailable: Could not save the upload execution record.");
  recordCompletion(outcome, [this, fileName, outcome, notice, storage](const QString &historyError) {
    const QPointer<FileUploadController> owner(this);
    emit publicationResolved(m_currentPath, UploadPublication::Acknowledged, m_uploadAttempts, m_answeredAttempts);
    if (!owner) return;
    emit fileCompleted(m_currentPath, true, {});
    closeProgress();
    auto *dialog = new FileUploadResultDialog(fileName, outcome, m_dialogParent, notice.headline + QLatin1Char('\n') + actions::pipelineObservationText(m_terminalObservation));
    dialog->show();
    if (!historyError.isEmpty() || !storage.isEmpty()) {
      auto *error = new FileUploadResultDialog(fileName, QStringList{historyError, storage}.join('\n').trimmed(), {}, m_dialogParent); error->show();
    }
    scheduleNext();
  });
}

UploadPublication FileUploadController::publication() const {
  // CORE-025 P1. The acknowledgement test is proof over every attempt, not
  // the last status. A started request with any unanswered attempt is unknown.
  if (!m_requestStarted || m_uploadAttempts <= 0) return UploadPublication::Unsent;
  if (m_answeredAttempts == m_uploadAttempts) return UploadPublication::Acknowledged;
  return UploadPublication::Unknown;
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
  m_httpStatus = outcome.httpStatus;
  m_historyRecord.result.attempts = outcome.attempts;
  // CORE-025: every attempt must be accounted for before the publication may
  // be called acknowledged. The last status cannot answer that question: a
  // later answered attempt does not repair an earlier unanswered one.
  m_uploadAttempts = outcome.attempts;
  m_answeredAttempts = outcome.answeredAttempts;
  if (m_stop || outcome.code == QLatin1String("cancelled")) {
    m_historyRecord.failureCode = "cancelled";
    showFailure(m_currentName, QStringLiteral("cancelled"), false, outcome.attempts > 0
        ? tr("Remote publication is unconfirmed. Review the destination before retrying.") : QString{});
    return;
  }
  if (!outcome.code.isEmpty()) {
    const bool uncertain = outcome.attempts > 0 && (outcome.code == "timeout" || outcome.code == "network-error");
    showFailure(m_currentName, uncertain
        ? outcome.code + QStringLiteral(": remote publication is unconfirmed. Review the destination before retrying.")
        : outcome.errorMessage.isEmpty() ? outcome.code : outcome.errorMessage);
    return;
  }
  showSuccess(m_currentName, outcome);
}
}  // namespace xerahs::app
