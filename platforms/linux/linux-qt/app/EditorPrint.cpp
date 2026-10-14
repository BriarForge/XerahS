#include "EditorPrint.h"
#include "ComparisonImages.h"
#include "image-editor/PrintLayout.h"
#include <QColorSpace>
#include <QDialog>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QPrinter>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, std::nullopt}; }
ActionOutcome cancelled() { return {ActionState::Cancelled, std::nullopt, std::nullopt}; }
ActionOutcome unknown() { auto r = failed("print-delivery-unknown"); r.outcomeUnknown = true; return r; }
ActionOutcome validated(ActionOutcome result) {
  if (result.outcomeUnknown || result.state == ActionState::Skipped || result.replacementSelection || !result.artifacts.empty()) return unknown();
  if (result.state == ActionState::Succeeded) return {};
  if (result.state == ActionState::Cancelled) return cancelled();
  if (result.state != ActionState::Failed) return unknown();
  return failed(result.diagnostic == QStringLiteral("print-unavailable") ? "print-unavailable" : "print-failed");
}
struct Work {
  std::atomic<bool> cancel{false}, ready{false};
  std::atomic<int> percent{0};
  EditorPrintSnapshotPtr snapshot;
  QString diagnostic;
  ActionOutcome action;
};
void destroyProgress(QPointer<QProgressDialog> dialog) {
  // A destruction observer may delete the editor. Remove the live progress
  // widget from its parent's child/focus chains before entering its destructor,
  // so parent teardown cannot destroy the same partially destroyed child again.
  if (dialog) dialog->hide();
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
}
// Returns before joining when the owner dies. Only value-owned work crosses
// threads; the timer and modal objects never cross the GUI boundary.
bool awaitWorker(QWidget *parent, const std::shared_ptr<Work> &work, bool submitting, std::function<void()> operation) {
  QPointer<QProgressDialog> dialog(new QProgressDialog(submitting ? "Submitting the image to the printer…" : "Preparing a print snapshot…", "Cancel", 0, submitting ? 0 : 100, parent));
  dialog->setObjectName(submitting ? "printJobProgress" : "printImageProgress");
  dialog->setAccessibleName(submitting ? "Print submission progress" : "Print image preparation progress");
  dialog->setWindowTitle("Print Image"); dialog->setWindowModality(Qt::WindowModal); dialog->setAutoClose(false); dialog->setAutoReset(false);
  QEventLoop completion; bool abandoned = false;
  const auto cancel = [work] { work->cancel.store(true); };
  QObject::connect(dialog, &QProgressDialog::canceled, &completion, cancel);
  QObject::connect(dialog, &QDialog::rejected, &completion, cancel);
  if (parent) QObject::connect(parent, &QObject::destroyed, &completion, [&] { cancel(); abandoned = true; completion.quit(); });
  QThread *worker = nullptr;
  try { worker = QThread::create([work, operation = std::move(operation)] { operation(); work->ready.store(true); }); }
  catch (...) { destroyProgress(dialog); throw; }
  QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &completion, [&] {
    if (work->ready.load()) { completion.quit(); return; }
    if (dialog) {
      if (!submitting) dialog->setValue(work->percent.load());
      if (!dialog) return; // modal progress updates may dispatch owner deletion
      if (work->cancel.load()) dialog->setLabelText(submitting
          ? "Cancellation requested. The printer may already have received the job…" : "Cancelling print preparation…");
    }
  });
  worker->start(); timer.start(15); if (dialog) dialog->open(); if (!abandoned) completion.exec(); timer.stop();
  destroyProgress(dialog); return abandoned;
}
}
bool paintEditorPrintPage(QPainter &painter, QPrinter &printer, const EditorPrintSnapshot &snapshot, double percent) {
  const auto &image = printer.colorMode() == QPrinter::GrayScale ? snapshot.grayscale : snapshot.colour;
  const QRectF page(0, 0, printer.pageLayout().paintRectPixels(printer.resolution()).width(), printer.pageLayout().paintRectPixels(printer.resolution()).height());
  const auto rect = xerahs::editor::printImageRect(image.width(), image.height(), page, percent);
  if (!painter.isActive() || !rect) return false;
  painter.save(); painter.setClipRect(page); painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.fillRect(page, Qt::white); painter.drawImage(*rect, image); painter.restore(); return true;
}
ActionOutcome submitEditorPrint(QPrinter &printer, const EditorPrintSnapshot &snapshot, double percent, const std::atomic<bool> &cancel) {
  if (cancel.load()) return cancelled();
  if (!printer.isValid() || snapshot.colour.isNull() || snapshot.grayscale.isNull() ||
      !xerahs::editor::printImageRect(snapshot.colour.width(), snapshot.colour.height(), QRectF(0, 0, 1, 1), percent)) return failed("print-failed");
  // No file or document title is put in the spooler's metadata.
  printer.setDocName("XerahS image"); printer.setCreator("XerahS"); printer.setFullPage(false);
  if (cancel.load()) return cancelled();
  QPainter painter;
  // Beginning/ending may have native effects even when they fail. Do not
  // manufacture a safe retry or promise cancellation withdrew the job.
  if (!painter.begin(&printer)) return unknown();
  if (cancel.load()) { printer.abort(); painter.end(); return unknown(); }
  const bool painted = paintEditorPrintPage(painter, printer, snapshot, percent);
  if (!painted || cancel.load()) { printer.abort(); painter.end(); return unknown(); }
  const bool ended = painter.end();
  if (!ended || printer.printerState() == QPrinter::Error || printer.printerState() == QPrinter::Aborted) return unknown();
  return {}; // engine acknowledgement, never proof of physical paper delivery
}
EditorPrintOutcome runEditorPrint(const QImage &source, const xerahs::editor::AnnotationDocument &document, QWidget *parent,
                                 const EditorPrintPorts &ports, const EditorPrintSnapshotPtr &retry) {
  EditorPrintOutcome outcome; const auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!app || QThread::currentThread() != app->thread() || !ports.configure || !ports.submit) { outcome.action = failed("print-unavailable"); return outcome; }
  const auto configure = ports.configure; const auto submit = ports.submit; const QPointer<QWidget> owner(parent);
  auto work = std::make_shared<Work>();
  try {
    outcome.abandoned = awaitWorker(parent, work, false, [work, source, document, retry, before = ports.beforeRender] {
      try {
        if (retry) work->snapshot = retry;
        else {
          if (before) before();
          if (!work->cancel.load()) {
            const auto parsed = xerahs::editor::parseDocument(xerahs::editor::serializeDocument(document));
            if (!parsed.document || document.canvasWidth != source.width() || document.canvasHeight != source.height()) work->diagnostic = "print-render-failed";
            else {
              const auto rendered = prepareComparisonImage(source, document.annotations, {[work] { return work->cancel.load(); }, [work](int p) { work->percent.store(p * 8 / 10); }});
              if (rendered.image.isNull()) work->diagnostic = rendered.diagnostic == "comparison-annotation-unsupported" ? "print-annotation-unsupported" : "print-render-failed";
              else {
                auto snapshot = std::make_shared<EditorPrintSnapshot>();
                snapshot->colour = QImage(rendered.image.size(), QImage::Format_RGB32);
                snapshot->grayscale = QImage(rendered.image.size(), QImage::Format_Grayscale8);
                if (snapshot->colour.isNull() || snapshot->grayscale.isNull()) throw std::bad_alloc();
                snapshot->colour.setColorSpace(QColorSpace::SRgb); snapshot->grayscale.setColorSpace(QColorSpace::SRgb);
                for (int y = 0; y < rendered.image.height() && !work->cancel.load(); ++y) {
                  const auto *row = reinterpret_cast<const QRgb *>(rendered.image.constScanLine(y));
                  auto *colour = reinterpret_cast<QRgb *>(snapshot->colour.scanLine(y)); auto *gray = snapshot->grayscale.scanLine(y);
                  for (int x = 0; x < rendered.image.width(); ++x) {
                    colour[x] = xerahs::editor::printPixelOnWhite(row[x]); gray[x] = qGray(colour[x]);
                  }
                  work->percent.store(80 + 20 * (y + 1) / rendered.image.height());
                }
                if (!source.colorSpace().isValid()) snapshot->disclosures.append("print-untagged-assumed-srgb");
                else if (source.colorSpace() != QColorSpace(QColorSpace::SRgb)) snapshot->disclosures.append("print-colour-converted-to-srgb");
                if (!work->cancel.load()) work->snapshot = std::move(snapshot);
              }
            }
          }
        }
      } catch (...) { work->diagnostic = "print-render-failed"; }
    });
  } catch (...) { outcome.action = failed("print-render-failed"); return outcome; }
  if (outcome.abandoned || (parent && !owner) || work->cancel.load()) { outcome.action = cancelled(); return outcome; }
  outcome.snapshot = work->snapshot;
  if (!outcome.snapshot) { outcome.action = failed(work->diagnostic.isEmpty() ? "print-render-failed" : work->diagnostic); return outcome; }
  EditorPrintChoice choice;
  try { choice = configure(*outcome.snapshot, parent); }
  catch (...) { outcome.action = failed("print-configuration-failed"); return outcome; }
  if (parent && !owner) { outcome.abandoned = true; outcome.action = cancelled(); return outcome; }
  outcome.action = validated(choice.action);
  if (outcome.action.state != ActionState::Succeeded || outcome.action.outcomeUnknown) return outcome;
  if (!choice.printer || !choice.printer->isValid() || !xerahs::editor::printImageRect(outcome.snapshot->colour.width(), outcome.snapshot->colour.height(), QRectF(0, 0, 1, 1), choice.percentOfFit)) { outcome.action = failed("print-configuration-failed"); return outcome; }
  work = std::make_shared<Work>(); const auto snapshot = outcome.snapshot;
  outcome.submissionAttempted = true;
  try {
    outcome.abandoned = awaitWorker(parent, work, true, [work, snapshot, printer = std::move(choice.printer), percent = choice.percentOfFit, submit] {
      if (work->cancel.load()) { work->action = cancelled(); return; }
      try { work->action = validated(submit(*printer, *snapshot, percent, work->cancel)); }
      catch (...) { work->action = unknown(); }
    });
  } catch (...) { outcome.action = failed("print-failed"); outcome.submissionAttempted = false; return outcome; }
  outcome.cancellationArrivedDuring = work->cancel.load();
  // Destruction during modal teardown can follow a completed acknowledgement.
  // The ready release/acquire makes that actual result safe to retain.
  outcome.action = outcome.abandoned && !work->ready.load() ? unknown() : work->action;
  return outcome;
}
}
