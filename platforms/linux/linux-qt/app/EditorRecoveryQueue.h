#pragma once

#include "image-editor/EditorRecovery.h"
#include <QObject>
#include <QHash>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace xerahs::app {

struct RecoveryJobResult {
  QString diagnostic;
  editor::RecoveryListing listing;
};

class EditorRecoveryQueue final : public QObject {
  Q_OBJECT
public:
  static EditorRecoveryQueue &instance();
  using Store = std::shared_ptr<editor::EditorRecoveryStore>;
  quint64 write(Store, QString id, QString source, editor::AnnotationDocument, qint64 pid,
                std::function<void(QString)> done);
  quint64 remove(Store, QString id, std::function<void(QString)> done);
  quint64 list(Store, std::function<void(editor::RecoveryListing)> done);
  // Wait immediately after submission; completed asynchronous tickets are retired.
  // wait()/waitIdle() spin a nested loop: they serve the post-main-loop drain and
  // test flushes only. GUI-session callers use the owner-fenced continuations
  // below so no loop runs inside an interactive window.
  QString wait(quint64 job);
  void waitIdle();
  // Runs done once on this (GUI) thread with the job's diagnostic when it
  // finishes: immediately for an unknown or retired ticket (matching wait()'s
  // "recovery-job-unavailable"), queued while the job runs. The queue is
  // application-parented and outlives every window, so done owns its lifetime.
  void whenFinished(quint64 job, std::function<void(QString)> done);
  // Runs done once when the queue has no pending or active job: immediately if
  // it is already idle. A done that submits new work keeps the queue busy again.
  void whenIdle(std::function<void()> done);
  static void setWorkHookForTest(std::function<void()>);

signals:
  void jobFinished(quint64 job);
  void idle();

private:
  explicit EditorRecoveryQueue(QObject *parent);
  enum class Kind { Write, Remove, List };
  struct Job {
    quint64 id = 0;
    Kind kind;
    QString recordId;
    std::function<RecoveryJobResult()> work;
    std::function<void(RecoveryJobResult)> done;
    // whenFinished continuations, run once from finish() with the diagnostic.
    std::vector<std::function<void(QString)>> notify;
    bool finished = false;
    QString diagnostic;
  };
  using JobPtr = std::shared_ptr<Job>;
  quint64 enqueue(Kind, QString id, std::function<RecoveryJobResult()>, std::function<void(RecoveryJobResult)>);
  void dropWrites(const QString &id);
  void startNext();
  void finish(const JobPtr &, RecoveryJobResult);
  quint64 m_next = 0;
  std::deque<JobPtr> m_pending;
  QHash<quint64, JobPtr> m_jobs;
  // whenIdle continuations, run once from finish() when the queue empties.
  std::vector<std::function<void()>> m_idleWaiters;
  JobPtr m_active;
};

}
