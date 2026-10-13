#include "EditorCopy.h"
#include "ComparisonImages.h"
#include <QClipboard>
#include <QColorSpace>
#include <QDialog>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIODevice>
#include <QMimeData>
#include <QPointer>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>
#include <atomic>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, std::nullopt}; }
ActionOutcome cancelled() { return {ActionState::Cancelled, std::nullopt, std::nullopt}; }
ActionOutcome unknown() { auto result = failed("clipboard-delivery-unknown"); result.outcomeUnknown = true; return result; }
class PngBuffer final : public QIODevice {
public:
  PngBuffer() { open(QIODevice::WriteOnly); }
  QByteArray bytes;
protected:
  qint64 readData(char *, qint64) override { return -1; }
  qint64 writeData(const char *data, qint64 size) override {
    if (size < 0 || size > 1073741824 - bytes.size()) return -1;
    bytes.append(data, size); return size;
  }
};
struct CopyWork {
  std::atomic<bool> cancelled{false}, ready{false};
  std::atomic<int> percent{0};
  EditorCopyPayloadPtr payload;
  QString diagnostic;
};
ActionOutcome validatedPublication(ActionOutcome result) {
  if (result.outcomeUnknown || result.state == ActionState::Skipped || result.replacementSelection || !result.artifacts.empty()) return unknown();
  if (result.state == ActionState::Cancelled) return cancelled();
  if (result.state == ActionState::Succeeded) return {};
  if (result.state != ActionState::Failed) return unknown();
  // Host diagnostics are never a channel for clipboard contents/private values.
  return failed(result.diagnostic == QStringLiteral("clipboard-unavailable") ? "clipboard-unavailable" : "clipboard-failed");
}
}
ActionOutcome publishEditorClipboard(const EditorCopyPayload &payload) {
  const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || payload.image.isNull() || payload.png.isEmpty()) return failed("clipboard-unavailable");
  auto *clipboard = QGuiApplication::clipboard(); if (!clipboard) return failed("clipboard-unavailable");
  bool attempted = false;
  try {
    auto mime = std::make_unique<QMimeData>(); mime->setImageData(payload.image); mime->setData("image/png", payload.png);
    const QPointer<QMimeData> offered(mime.get()); attempted = true; clipboard->setMimeData(mime.release());
    // Ownership alone is insufficient if a synchronous consumer replaced our
    // offer. Never clear/restore someone else's clipboard to manufacture success.
    return offered && clipboard->mimeData() == offered.data() && clipboard->ownsClipboard() ? ActionOutcome{} : unknown();
  } catch (...) { return attempted ? unknown() : failed("clipboard-failed"); }
}
EditorCopyOutcome runEditorCopy(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                               QWidget *parent, const EditorCopyPorts &ports, const EditorCopyPayloadPtr &retry) {
  EditorCopyOutcome outcome;
  const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || !ports.publish) { outcome.action = failed("clipboard-unavailable"); return outcome; }
  const auto publish = ports.publish;
  auto state = std::make_shared<CopyWork>(); const QPointer<QWidget> owner(parent);
  QPointer<QProgressDialog> dialog(new QProgressDialog(retry ? "Preparing the retained clipboard snapshot…" : "Rendering visible annotations for the clipboard as sRGB with alpha…", "Cancel", 0, 100, parent));
  dialog->setObjectName("copyImageProgress"); dialog->setAccessibleName("Copy image progress"); dialog->setWindowTitle("Copy Image");
  dialog->setWindowModality(Qt::WindowModal); dialog->setAutoClose(false); dialog->setAutoReset(false);
  QEventLoop completion; bool abandoned = false;
  const auto cancel = [state] { state->cancelled.store(true); };
  QObject::connect(dialog, &QProgressDialog::canceled, &completion, cancel);
  QObject::connect(dialog, &QDialog::rejected, &completion, cancel);
  if (parent) QObject::connect(parent, &QObject::destroyed, &completion, [&] { cancel(); abandoned = true; completion.quit(); });
  QThread *worker = nullptr;
  try {
    // Copy only worker callbacks, never the GUI publication closure.
    worker = QThread::create([state, source, document, beforeRender = ports.beforeRender, encode = ports.encode, retry] {
      try {
        if (retry) state->payload = retry;
        else {
          if (beforeRender) beforeRender();
          if (!state->cancelled.load()) {
            const auto parsed = xerahs::editor::parseDocument(xerahs::editor::serializeDocument(document));
            if (!parsed.document || document.canvasWidth != source.width() || document.canvasHeight != source.height()) state->diagnostic = "clipboard-render-failed";
            else {
              const auto image = prepareComparisonImage(source, document.annotations,
                  {[state] { return state->cancelled.load(); }, [state](int p) { state->percent.store(p * 8 / 10); }});
              if (image.image.isNull()) state->diagnostic = image.diagnostic == "comparison-annotation-unsupported" ? "clipboard-annotation-unsupported" : "clipboard-render-failed";
              else if (!state->cancelled.load()) {
                state->percent.store(85); PngBuffer png;
                const bool encoded = encode ? encode(image.image, &png) : image.image.save(&png, "PNG");
                if (!encoded || png.bytes.isEmpty()) state->diagnostic = "clipboard-encode-failed";
                else if (!state->cancelled.load()) {
                  auto payload = std::make_shared<EditorCopyPayload>(); payload->image = image.image; payload->png = std::move(png.bytes);
                  if (!source.colorSpace().isValid()) payload->disclosures.append("clipboard-untagged-assumed-srgb");
                  else if (source.colorSpace() != QColorSpace(QColorSpace::SRgb)) payload->disclosures.append("clipboard-colour-converted-to-srgb");
                  state->payload = std::move(payload);
                }
              }
            }
          }
        }
      } catch (...) { state->diagnostic = "clipboard-render-failed"; }
      state->ready.store(true);
    });
  } catch (...) { delete dialog.data(); outcome.action = failed("clipboard-render-failed"); return outcome; }
  QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &completion, [&] {
    if (state->ready.load()) { completion.quit(); return; }
    if (dialog) {
      dialog->setValue(state->percent.load()); if (!dialog) return;
      dialog->setLabelText(state->cancelled.load() ? "Cancelling image copy…" : retry ? "Preparing the retained clipboard snapshot…" : state->percent.load() >= 85
          ? "Preparing an alpha-preserving PNG clipboard representation…" : "Rendering visible annotations for the clipboard as sRGB with alpha…");
    }
  });
  worker->start(); timer.start(15); if (dialog) dialog->open(); if (!abandoned) completion.exec(); timer.stop();
  if (abandoned || (parent && !owner) || state->cancelled.load()) {
    delete dialog.data(); outcome.action = cancelled(); outcome.abandoned = abandoned || (parent && !owner); return outcome;
  }
  outcome.payload = state->payload;
  if (!outcome.payload) { delete dialog.data(); outcome.action = failed(state->diagnostic.isEmpty() ? "clipboard-render-failed" : state->diagnostic); return outcome; }
  if (dialog) dialog->setLabelText("Publishing the rendered image to the clipboard…");
  // Label changes can dispatch owner destruction/cancellation before publication.
  if (abandoned || (parent && !owner) || state->cancelled.load()) {
    delete dialog.data(); outcome.action = cancelled(); outcome.abandoned = abandoned || (parent && !owner); return outcome;
  }
  outcome.publicationAttempted = true;
  try { outcome.action = validatedPublication(publish(*outcome.payload)); }
  catch (...) { outcome.action = unknown(); }
  outcome.cancellationArrivedDuring = state->cancelled.load(); outcome.abandoned = abandoned || (parent && !owner);
  delete dialog.data(); return outcome;
}
} // namespace xerahs::app
