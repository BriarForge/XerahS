// CORE-020, CORE-023: upload an existing local file through the default destination.
// Capture history is a pipeline journal of files a capture run created. This
// upload does not write one.
#pragma once

#include "uploads/CredentialStore.h"

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
};

class FileUploadController final : public QObject {
  Q_OBJECT
public:
  // dialogParent owns the progress and result windows. Leave parent null when
  // the controller is not heap-allocated with the window.
  explicit FileUploadController(FileUploadPorts ports, QWidget *dialogParent = nullptr, QObject *parent = nullptr);

  // Paths upload one after another. Further calls wait behind the current file.
  void upload(const QStringList &paths);
  void chooseAndUpload();
  void cancel();

signals:
  void progress(qint64 sent, qint64 total);
  // Emitted when the queue has drained or been cancelled.
  void finished();
  void fileCompleted(const QString &path, bool success, const QString &error);

private:
  void startNext();
  void showProgress(const QString &fileName);
  void closeProgress();
  void showFailure(const QString &fileName, const QString &message, bool missingDestination = false);
  void showSuccess(const QString &fileName, const uploads::UploadOutcome &outcome);
  void scheduleNext();
  void onProgress(qint64 sent, qint64 total);
  void onFinished(const uploads::UploadOutcome &outcome);

  FileUploadPorts m_ports;
  QWidget *m_dialogParent = nullptr;
  qint64 m_cap = 0;
  QStringList m_queue;
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
