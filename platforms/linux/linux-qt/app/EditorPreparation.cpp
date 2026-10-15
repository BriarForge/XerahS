#include "EditorPreparation.h"
#include <QEventLoop>
#include <QPointer>
#include <QProgressDialog>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>
namespace xerahs::app {
namespace {
struct Work {
  std::atomic<bool> cancelled{false}, ready{false}, failed{false};
  std::atomic<int> percent{0};
};
void destroyProgress(QPointer<QProgressDialog> dialog, QObject *context) {
  if (dialog) QObject::disconnect(dialog, nullptr, context, nullptr);
  if (dialog) dialog->hide();
  // A child-destruction observer may delete the parent. Detach first.
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
}
}
EditorPreparationResult runEditorPreparation(QWidget *parent, const EditorPreparationUi &ui,
    std::function<void(const xerahs::editor::RenderControl &)> operation) {
  std::shared_ptr<Work> work; QPointer<QProgressDialog> dialog; const QPointer<QWidget> owner(parent); QEventLoop completion;
  try {
    work = std::make_shared<Work>();
    dialog = new QProgressDialog(ui.label, "Cancel", 0, 100, parent);
    dialog->setObjectName(ui.name); dialog->setAccessibleName(ui.accessibleName); dialog->setWindowTitle(ui.title);
    dialog->setWindowModality(Qt::WindowModal); dialog->setAutoClose(false); dialog->setAutoReset(false); dialog->setMinimumDuration(0);
    const auto cancel = [&completion, work] { work->cancelled.store(true); completion.quit(); };
    QObject::connect(dialog, &QProgressDialog::canceled, &completion, cancel);
    QObject::connect(dialog, &QDialog::rejected, &completion, cancel);
    QObject::connect(dialog, &QObject::destroyed, &completion, cancel);
    if (parent) QObject::connect(parent, &QObject::destroyed, &completion, cancel);
    QThreadPool::globalInstance()->start([work, operation = std::move(operation)] {
      try { operation({[work] { return work->cancelled.load(); }, [work](int p) { work->percent.store(p); }}); }
      catch (...) { work->failed.store(true); }
      work->ready.store(true, std::memory_order_release);
    });
    QTimer timer; QObject::connect(&timer, &QTimer::timeout, &completion, [&] {
      if (work->ready.load(std::memory_order_acquire)) { completion.quit(); return; }
      if (dialog) dialog->setValue(work->percent.load()); // modal update may delete the owner
    });
    timer.start(15); if (dialog) dialog->open();
    if (!work->cancelled.load() && !work->ready.load(std::memory_order_acquire)) completion.exec();
    timer.stop();
    if (!work->ready.load(std::memory_order_acquire)) work->cancelled.store(true);
    destroyProgress(dialog, &completion);
    if (work->cancelled.load() || (parent && !owner)) return EditorPreparationResult::Cancelled;
    return work->failed.load() ? EditorPreparationResult::Failed : EditorPreparationResult::Ready;
  } catch (...) {
    if (work) work->cancelled.store(true);
    destroyProgress(dialog, &completion); return EditorPreparationResult::Failed;
  }
}
} // namespace xerahs::app
