// CORE-020, CORE-021: one bounded logical upload per start.
#pragma once
#include "UploadRequest.h"
#include "UploadResponse.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
class QNetworkAccessManager;
class QNetworkReply;
namespace xerahs::uploads {
class UploadExecutor : public QObject {
  Q_OBJECT
public:
  explicit UploadExecutor(QNetworkAccessManager *, QObject *parent = nullptr, int timeoutMs = 60000);
  ~UploadExecutor() override;
  // A start during an active transfer is ignored. The object can be reused
  // after finished; manager and executor must live on the same thread.
  void start(const PreparedUploadRequest &);
  void cancel();
signals:
  void progress(qint64 sent, qint64 total);
  void finished(xerahs::uploads::UploadOutcome outcome);
  void retryScheduled(int nextAttempt, int delayMs);
private:
  void sendAttempt();
  void drain();
  void attemptFinished(UploadOutcome, bool transient);
  void releaseReply();
  bool retrySafe() const;
  void complete(UploadOutcome);
  QPointer<QNetworkAccessManager> m_manager;
  QPointer<QNetworkReply> m_reply;
  QTimer m_timer, m_retryTimer;
  int m_attempts = 0;
  int m_timeoutMs;
  bool m_active = false, m_finishing = false;
  quint64 m_generation = 0;
  QByteArray m_body;
  PreparedUploadRequest m_request;
};
}
