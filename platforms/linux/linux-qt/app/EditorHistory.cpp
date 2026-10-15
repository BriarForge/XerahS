#include "EditorHistory.h"
#include <QEventLoop>
#include <QPointer>
#include <QProgressDialog>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>
namespace xerahs::app {
namespace {
struct HistoryWork {
  std::atomic<bool> cancelled{false}, ready{false};
  std::atomic<int> percent{0};
  LoadedSource source;
};
void destroyProgress(QPointer<QProgressDialog> dialog, QObject *context) {
  // Remove the live child from parent focus/child chains before destruction
  // observers can delete the owner. Normal cleanup is not user cancellation.
  if (dialog) QObject::disconnect(dialog, nullptr, context, nullptr);
  if (dialog) dialog->hide();
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
}
LoadedSource failed(const QString &diagnostic) { LoadedSource result; result.diagnostic = diagnostic; return result; }
}
LoadedSource runEditorHistory(const xerahs::editor::AnnotationDocument &document, QWidget *parent, const EditorHistoryHooks &hooks) {
  const auto work = std::make_shared<HistoryWork>(); const QPointer<QWidget> owner(parent);
  QPointer<QProgressDialog> dialog(new QProgressDialog("Restoring canvas history…", "Cancel", 0, 100, parent));
  dialog->setObjectName("editorHistoryProgress"); dialog->setAccessibleName("Restore editor history progress"); dialog->setWindowTitle("Restore editor history");
  dialog->setWindowModality(Qt::WindowModal); dialog->setAutoClose(false); dialog->setAutoReset(false); dialog->setMinimumDuration(0);
  QEventLoop completion;
  const auto cancel = [&completion, work] { work->cancelled.store(true); completion.quit(); };
  QObject::connect(dialog, &QProgressDialog::canceled, &completion, cancel);
  QObject::connect(dialog, &QDialog::rejected, &completion, cancel);
  QObject::connect(dialog, &QObject::destroyed, &completion, cancel);
  if (parent) QObject::connect(parent, &QObject::destroyed, &completion, cancel);
  try {
    QThreadPool::globalInstance()->start([work, document, hooks] {
      try {
        if (hooks.beforePrepare) hooks.beforePrepare();
        work->source = prepareCapturedDocument(document, {[work] { return work->cancelled.load(); }, [work](int p) { work->percent.store(p); }});
      } catch (...) { work->source.diagnostic = "canvas-history-failed"; }
      work->ready.store(true, std::memory_order_release);
    });
  } catch (...) { destroyProgress(dialog, &completion); return failed("canvas-history-failed"); }
  QTimer timer; QObject::connect(&timer, &QTimer::timeout, &completion, [&] {
    if (work->ready.load(std::memory_order_acquire)) { completion.quit(); return; }
    if (dialog) dialog->setValue(work->percent.load()); // modal update may delete the owner/child
  });
  timer.start(15); if (dialog) dialog->open();
  if (!work->cancelled.load() && !work->ready.load(std::memory_order_acquire)) completion.exec();
  timer.stop();
  if (!work->ready.load(std::memory_order_acquire)) work->cancelled.store(true);
  destroyProgress(dialog, &completion);
  if (work->cancelled.load() || (parent && !owner)) return failed("canvas-cancelled");
  return std::move(work->source);
}
} // namespace xerahs::app
