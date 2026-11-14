// CORE-020, CORE-023: upload an existing local file through the default destination.
// Existing source files are retained as upload-only pipeline journal records.
#pragma once

#include "uploads/CredentialStore.h"
#include "NativePipelineJournal.h"
#include "workflows/FileOperationExecution.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QProgressDialog;
class QWidget;

namespace xerahs::uploads {
class UploadExecutor;
struct UploadDestination;
struct UploadOutcome;
}  // namespace xerahs::uploads
class QNetworkAccessManager;

namespace xerahs::app {

// CORE-025 P1: the typed outcome of one upload publication.
//   Unsent      - no request was started (validation, missing destination,
//                 cancelled before transfer, or a send intent that was not saved);
//   Acknowledged - the destination answered with an HTTP status of any kind,
//                 and every preceding request was answered;
//   Unknown     - a request was started and at least one attempt ended without
//                 an HTTP response, so the remote state cannot be known;
// The type is derived from the attempt facts, never from the last status alone.
enum class UploadPublication { Unsent, Acknowledged, Unknown };
struct FileUploadPorts {
  QString catalogPath;
  std::shared_ptr<uploads::CredentialStore> credentials;
  // Invoked by the result dialog when no default destination is saved.
  std::function<void()> openDestinations;
  // Empty uses the upload engine's maxInputBytes.
  std::optional<qint64> maxInputBytes;
  PipelineJournalStorage history = {};  // empty uses the shared default journal directory
  // WFCAT-002: execution records; empty uses the shared workflow-execution directory.
  workflows::WorkflowExecution::Writer executions = {};
};

class FileUploadController final : public QObject {
  Q_OBJECT
public:
  // dialogParent owns the progress and result windows while it lives. Destroying
  // it hides those windows and is not a cancellation and not a new result.
  // Leave parent null when the controller is not heap-allocated with the window.
  explicit FileUploadController(FileUploadPorts ports, QWidget *dialogParent = nullptr, QObject *parent = nullptr);

  // Paths upload one after another. Further calls wait behind the current file.
  void upload(const QStringList &paths);
  // rule is the redacted watch-rule snapshot (action, recursive, filters), if any.
  void uploadFromWatchFolder(const QString &path, const QJsonObject &rule = {});
  void chooseAndUpload();
  void cancel();

signals:
  void progress(qint64 sent, qint64 total);
  // Emitted when the queue has drained or been cancelled.
  void finished();
  void fileCompleted(const QString &path, bool success, const QString &error);
  // CORE-025: the typed publication outcome of the finished upload, with the
  // attempt facts that produced it. `attempts` is the number of request
  // attempts and `answeredAttempts` the number that ended with an HTTP status.
  void publicationResolved(const QString &path, xerahs::app::UploadPublication publication, int attempts, int answeredAttempts);
  // WFCAT-017: the terminal projection shared by dialog, notification and history.
  void activityChanged(const QString &text);
  void notify(const QString &title, const QString &body, bool success);
  void terminalObserved(const QJsonObject &observation);
  void executionFinished(const QString &path, const QString &executionId, const QString &state, const QStringList &outputs);

private:
  void enqueue(const QStringList &paths, const QString &origin, const QJsonObject &rule = {});
  void startNext();
  void recordCompletion(const uploads::UploadOutcome &outcome, std::function<void(QString)> done);
  void showProgress(const QString &fileName);
  void closeProgress();
  workflows::FileOperationNotice settle(bool uploaded);
  void showFailure(const QString &fileName, const QString &message, bool missingDestination = false, const QString &detail = {});
  void showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome);
  void scheduleNext();
  void onProgress(qint64 sent, qint64 total);
  void onFinished(const uploads::UploadOutcome &outcome);
  // CORE-025: the typed publication outcome of the current upload.
  UploadPublication publication() const;

  FileUploadPorts m_ports;
  QPointer<QWidget> m_dialogParent;
  qint64 m_cap = 0;
  struct QueuedFile { QString path, origin; QJsonObject rule; };
  QList<QueuedFile> m_queue;
  FileUploadHistoryRecord m_historyRecord;
  bool m_existingFile = false;
  QStringList m_sensitiveValues;
  std::unique_ptr<workflows::WorkflowExecution> m_execution;
  QJsonObject m_rule;
  QString m_destinationId;
  bool m_requestBuilt = false, m_executionStored = true;
  int m_httpStatus = 0;
  // CORE-025: the attempt facts of the finished upload. Every attempt must be
  // accounted for before the controller may claim the publication was
  // acknowledged.
  int m_uploadAttempts = 0;
  int m_answeredAttempts = 0;
  bool m_requestStarted = false;
  QStringList m_executionOutputs;
  QJsonObject m_terminalObservation;
  QString m_currentName;
  QString m_currentPath;
  bool m_busy = false;
  bool m_preparing = false;
  bool m_transferring = false;
  bool m_stop = false;
  QNetworkAccessManager *m_network = nullptr;
  uploads::UploadExecutor *m_executor = nullptr;
  QPointer<QProgressDialog> m_progress;
};

}  // namespace xerahs::app
