// The one way native windows and controllers run blocking work (keyring,
// file hashing, uploads, store I/O) off the GUI thread. CORE-004, EU-017.
//
// Contract of OwnedWork::run(owner, work, done, progress):
//  - owner must be non-null and live on the calling (GUI) thread.
//  - work runs once on a new worker thread; it must not touch widgets or owner.
//  - done runs once on the owner's thread after work returns, only if the owner
//    still exists. A destroyed owner never sees a late result. If work throws,
//    done does not run.
//  - The returned handle can request cancellation, which work polls through
//    Context::cancelled(). Cancellation is cooperative: done still receives
//    whatever work returned.
//  - The worker thread object deletes itself. Nothing blocks the GUI thread;
//    destroying the owner only abandons the result.
//  - Context::progress() may be called often from work; delivery to the owner's
//    progress callback is coalesced to at most one queued call at a time.
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace xerahs::app {

class OwnedWork {
  struct State {
    std::atomic_bool cancelled{false}, running{true}, progressQueued{false};
    std::atomic<qint64> done{0}, total{-1};
    QPointer<QObject> owner;
    std::function<void(qint64, qint64)> progress;
  };

public:
  class Context {
  public:
    bool cancelled() const { return m_state->cancelled.load(); }
    void progress(qint64 done, qint64 total) const {
      m_state->done.store(done); m_state->total.store(total);
      if (!m_state->progress || m_state->progressQueued.exchange(true)) return;
      const auto state = m_state;
      QMetaObject::invokeMethod(state->owner.data(), [state] {
        state->progressQueued.store(false);
        if (state->owner) state->progress(state->done.load(), state->total.load());
      }, Qt::QueuedConnection);
    }

  private:
    friend class OwnedWork;
    explicit Context(std::shared_ptr<State> state) : m_state(std::move(state)) {}
    std::shared_ptr<State> m_state;
  };

  OwnedWork() = default;
  void cancel() const { if (m_state) m_state->cancelled.store(true); }
  bool cancelled() const { return m_state && m_state->cancelled.load(); }
  bool running() const { return m_state && m_state->running.load(); }

  template <typename Result>
  static OwnedWork run(QObject *owner, std::function<Result(const Context &)> work, std::function<void(Result)> done,
                       std::function<void(qint64, qint64)> progress = {}) {
    auto state = std::make_shared<State>();
    state->owner = owner; state->progress = std::move(progress);
    auto result = std::make_shared<std::optional<Result>>();
    auto *thread = QThread::create([state, result, work = std::move(work)] {
      try { result->emplace(work(Context(state))); } catch (...) {}
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    // The owner is the receiver context, so Qt drops this call if it is destroyed first.
    QObject::connect(thread, &QThread::finished, owner, [state, result, done = std::move(done)] {
      state->running.store(false);
      if (state->owner && result->has_value()) done(std::move(**result));
    });
    thread->start();
    return OwnedWork(state);
  }

private:
  explicit OwnedWork(std::shared_ptr<State> state) : m_state(std::move(state)) {}
  std::shared_ptr<State> m_state;
};

}  // namespace xerahs::app
