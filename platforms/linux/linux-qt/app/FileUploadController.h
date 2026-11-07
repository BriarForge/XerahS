// CORE-020, CORE-023: upload an existing local file through the default destination.
// Existing source files are retained as upload-only pipeline journal records.
#pragma once

#include "uploads/CredentialStore.h"
#include "NativePipelineJournal.h"
#include "workflows/FileOperationExecution.h"

#include <QJsonObject>
#include <QObject>
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
  // dialogParent owns the progress and result windows. Leave parent null when
  // the controller is not heap-allocated with the window.
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
  // WFCAT-017: the terminal projection shared by dialog, notification and history.
  void executionFinished(const QString &path, const QString &executionId, const QString &state, const QStringList &outputs);

private:
  void enqueue(const QStringList &paths, const QString &origin, const QJsonObject &rule = {});
  void startNext();
  void recordCompletion(const uploads::UploadOutcome &outcome, std::function<void(QString)> done);
  void showProgress(const QString &fileName);
  void closeProgress();
  workflows::FileOperationNotice settle(bool uploaded);
  void showFailure(const QString &fileName, const QString &message, bool missingDestination = false);
  void showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome);
  void scheduleNext();
  void onProgress(qint64 sent, qint64 total);
  void onFinished(const uploads::UploadOutcome &outcome);

  FileUploadPorts m_ports;
  QWidget *m_dialogParent = nullptr;
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
  QStringList m_executionOutputs;
  QString m_currentName;
  QString m_currentPath;
  bool m_busy = false;
  bool m_preparing = false;
  bool m_transferring = false;
  bool m_stop = false;
  QNetworkAccessManager *m_network = nullptr;
  uploads::UploadExecutor *m_executor = nullptr;
  QProgressDialog *m_progress = nullptr;
};

}  // namespace xerahs::app
