#include "EditorSave.h"
#include "OwnedWork.h"
#include <QDialog>
#include <QEventLoop>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>
#include <QWidget>
#include <atomic>
namespace xerahs::app {
namespace {
struct SaveWork {
  std::atomic<bool> cancelled{false}, ready{false};
  std::atomic<int> percent{0};
  std::atomic<EditorSavePhase> phase{EditorSavePhase::Rendering};
  SaveOutcome outcome;
};
QString phaseText(EditorSavePhase phase, const QString &format) {
  switch (phase) {
    case EditorSavePhase::Rendering: return "Rendering the saved document snapshot…";
    case EditorSavePhase::Encoding: return "Encoding " + format + "…";
    case EditorSavePhase::PublishingRaster: return "Saving " + format + "…";
    case EditorSavePhase::SavingAnnotations: return "Saving editable annotations…";
    case EditorSavePhase::Complete: return "Save finished.";
  }
  return {};
}
}
SaveOutcome runEditorSave(const QString &path, const QImage &source, const xerahs::editor::AnnotationDocument &document,
                         QWidget *parent, const EditorSaveHooks &hooks, const EditorSaveReceiptPtr &repair, const xerahs::editor::RasterSaveOptions &encoding) {
  auto state = std::make_shared<SaveWork>();
  QPointer<QWidget> owner(parent);
  QPointer<QProgressDialog> dialog(new QProgressDialog("Preparing save…", "Cancel", 0, 100, parent));
  dialog->setObjectName("saveImageProgress"); dialog->setAccessibleName("Save image progress"); dialog->setWindowTitle("Save image");
  dialog->setWindowModality(Qt::WindowModal); dialog->setAutoClose(false); dialog->setAutoReset(false);
  QEventLoop completion; bool abandoned = false;
  const auto cancel = [state] { state->cancelled.store(true); };
  QObject::connect(dialog, &QProgressDialog::canceled, &completion, cancel);
  QObject::connect(dialog, &QDialog::rejected, &completion, cancel);
  if (parent) QObject::connect(parent, &QObject::destroyed, &completion, [&] { cancel(); abandoned = true; completion.quit(); });
  OwnedWork worker;
  const auto format = xerahs::editor::rasterFormatName(repair ? repair->encoding.format : encoding.format);
  try {
    worker = OwnedWork::run<bool>(dialog.data(), [state, path, source, document, hooks, repair, encoding](const OwnedWork::Context &context) {
      const EditorSaveControl control{[state, &context] { return state->cancelled.load() || context.cancelled(); }, [state](EditorSavePhase phase, int percent) { state->phase.store(phase); state->percent.store(percent); }};
      state->outcome = repair ? retryEditorSidecar(repair, control, hooks) : saveEdit(path, source, document, control, hooks, encoding);
      state->ready.store(true); return true;
    }, [](bool) {});
  } catch (...) { delete dialog.data(); SaveOutcome result; result.rasterError = xerahs::editor::diagnostic::rasterSaveFailed; return result; }
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &completion, [&] {
    if (state->ready.load()) { completion.quit(); return; }
    if (dialog) {
      dialog->setValue(state->percent.load());
      // Modal setValue can process events that destroy the owner/dialog.
      if (!dialog) return;
      dialog->setLabelText(state->cancelled.load() ? "Cancelling save at the next safe boundary…" : phaseText(state->phase.load(), format));
    }
  });
  timer.start(15); if (dialog) dialog->open();
  if (!abandoned) completion.exec();
  timer.stop();
  delete dialog.data();
  if (abandoned || (parent && !owner)) { SaveOutcome result; result.cancelled = true; return result; }
  auto result = state->outcome; result.cancelled |= state->cancelled.load();
  return result;
}
} // namespace xerahs::app
