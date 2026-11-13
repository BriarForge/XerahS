#pragma once

#include "image-editor/EditorRecovery.h"
#include <QObject>
#include <QHash>
#include <deque>
#include <functional>
#include <memory>

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
  QString wait(quint64 job);
  void waitIdle();
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
  JobPtr m_active;
};

}
