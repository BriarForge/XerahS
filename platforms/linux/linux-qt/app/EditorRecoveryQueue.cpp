#include "EditorRecoveryQueue.h"
#include "OwnedWork.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>

namespace xerahs::app {
namespace {
std::function<void()> workHook;
}
EditorRecoveryQueue::EditorRecoveryQueue(QObject *parent) : QObject(parent) {}
EditorRecoveryQueue &EditorRecoveryQueue::instance() {
  static QPointer<EditorRecoveryQueue> queue;
  if (!queue) queue = new EditorRecoveryQueue(QCoreApplication::instance());
  return *queue;
}
void EditorRecoveryQueue::setWorkHookForTest(std::function<void()> hook) { workHook = std::move(hook); }

quint64 EditorRecoveryQueue::enqueue(Kind kind, QString id, std::function<RecoveryJobResult()> work,
                                    std::function<void(RecoveryJobResult)> done) {
  auto job = std::make_shared<Job>();
  job->id = ++m_next; job->kind = kind; job->recordId = std::move(id);
  job->work = std::move(work); job->done = std::move(done);
  m_jobs.insert(job->id, job); m_pending.push_back(job);
  QTimer::singleShot(0, this, &EditorRecoveryQueue::startNext);
  return job->id;
}
void EditorRecoveryQueue::dropWrites(const QString &id) {
  for (auto it = m_pending.begin(); it != m_pending.end();) {
    const auto job = *it;
    if (job->kind != Kind::Write || job->recordId != id) { ++it; continue; }
    it = m_pending.erase(it);
    QTimer::singleShot(0, this, [this, job] { finish(job, {QStringLiteral("recovery-write-superseded"), {}}); });
  }
}
quint64 EditorRecoveryQueue::write(Store store, QString id, QString source, editor::AnnotationDocument doc,
                                  qint64 pid, std::function<void(QString)> done) {
  dropWrites(id);
  return enqueue(Kind::Write, id, [store, id, source = std::move(source), doc = std::move(doc), pid] {
    return RecoveryJobResult{store->write(id, source, doc, pid), {}};
  }, [done = std::move(done)](RecoveryJobResult result) { if (done) done(result.diagnostic); });
}
quint64 EditorRecoveryQueue::remove(Store store, QString id, std::function<void(QString)> done) {
  dropWrites(id);
  return enqueue(Kind::Remove, id, [store, id] { return RecoveryJobResult{store->remove(id), {}}; },
      [done = std::move(done)](RecoveryJobResult result) { if (done) done(result.diagnostic); });
}
quint64 EditorRecoveryQueue::list(Store store, std::function<void(editor::RecoveryListing)> done) {
  return enqueue(Kind::List, {}, [store] { return RecoveryJobResult{{}, store->list()}; },
      [done = std::move(done)](RecoveryJobResult result) {
        if (!result.diagnostic.isEmpty()) result.listing.diagnostics.append(result.diagnostic);
        if (done) done(std::move(result.listing));
      });
}
void EditorRecoveryQueue::startNext() {
  if (m_active || m_pending.empty()) return;
  const auto job = m_pending.front(); m_pending.pop_front(); m_active = job;
  const auto hook = workHook;
  OwnedWork::run<RecoveryJobResult>(this, [job, hook](const OwnedWork::Context &) {
    try { if (hook) hook(); return job->work(); }
    catch (...) { return RecoveryJobResult{QStringLiteral("recovery-work-failed"), {}}; }
  }, [this, job](RecoveryJobResult result) { finish(job, std::move(result)); });
}
void EditorRecoveryQueue::finish(const JobPtr &job, RecoveryJobResult result) {
  if (m_active == job) m_active.reset();
  QTimer::singleShot(0, this, &EditorRecoveryQueue::startNext);
  job->diagnostic = result.diagnostic;
  job->finished = true;
  m_jobs.remove(job->id);
  if (job->done) job->done(std::move(result));
  emit jobFinished(job->id);
  if (m_jobs.isEmpty()) emit idle();
}
QString EditorRecoveryQueue::wait(quint64 id) {
  const auto job = m_jobs.value(id);
  if (!job) return QStringLiteral("recovery-job-unavailable");
  QEventLoop loop;
  connect(this, &EditorRecoveryQueue::jobFinished, &loop, [&loop, id](quint64 completed) { if (id == completed) loop.quit(); });
  if (!job->finished) loop.exec(QEventLoop::ExcludeUserInputEvents);
  return job->finished ? job->diagnostic : QStringLiteral("recovery-work-pending");
}
void EditorRecoveryQueue::waitIdle() {
  if (m_jobs.isEmpty()) return;
  QEventLoop loop;
  connect(this, &EditorRecoveryQueue::idle, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
}
}
