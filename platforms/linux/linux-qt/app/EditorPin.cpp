#include "EditorPin.h"
#include "ComparisonImages.h"
#include "EditorPreparation.h"
#include "PinWindow.h"
#include <QApplication>
#include <QColorSpace>
#include <QPointer>
#include <QThread>
#include <memory>

namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &diagnostic) { return {ActionState::Failed, diagnostic, std::nullopt}; }
ActionOutcome unknown() { auto result = failed("pin-delivery-unknown"); result.outcomeUnknown = true; return result; }
}
ActionOutcome publishEditorPin(const EditorPinSnapshot &snapshot) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || snapshot.image.isNull()) return failed("pin-unavailable");
  auto pin = std::make_unique<PinWindow>(snapshot.image);
  pin->show();
  pin.release();
  return {};
}
EditorPinOutcome runEditorPin(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                             QWidget *parent, const EditorPinPorts &ports) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || !ports.publish) return {failed("pin-unavailable")};
  const QPointer<QWidget> owner(parent);
  const auto publish = ports.publish;
  struct Prepared { EditorPinSnapshot snapshot; QString diagnostic; };
  const auto prepared = std::make_shared<Prepared>();
  const auto result = runEditorPreparation(parent,
      {"pinImageProgress", "Pin to Screen", "Rendering source and visible annotations as sRGB with alpha…", "Pin to Screen progress"},
      [source, document, prepared, beforeRender = ports.beforeRender](const xerahs::editor::RenderControl &control) {
        if (beforeRender) beforeRender();
        if (control.cancelled && control.cancelled()) return;
        const auto parsed = xerahs::editor::parseDocument(xerahs::editor::serializeDocument(document));
        if (!parsed.document || source.width() != document.canvasWidth || source.height() != document.canvasHeight) {
          prepared->diagnostic = "pin-render-failed"; return;
        }
        const auto rendered = prepareComparisonImage(source, document.annotations, control);
        if (rendered.image.isNull()) {
          prepared->diagnostic = rendered.diagnostic == "comparison-annotation-unsupported" ? "pin-annotation-unsupported" : "pin-render-failed";
          return;
        }
        prepared->snapshot.image = rendered.image;
        if (!source.colorSpace().isValid()) prepared->snapshot.disclosures.append("pin-untagged-assumed-srgb");
        else if (source.colorSpace() != QColorSpace(QColorSpace::SRgb)) prepared->snapshot.disclosures.append("pin-colour-converted-to-srgb");
      });
  if (result == EditorPreparationResult::Cancelled || (parent && !owner)) return {{ActionState::Cancelled, {}, {}}};
  if (result != EditorPreparationResult::Ready || prepared->snapshot.image.isNull())
    return {failed(prepared->diagnostic.isEmpty() ? QStringLiteral("pin-render-failed") : prepared->diagnostic)};
  EditorPinOutcome outcome; outcome.publicationAttempted = true;
  try {
    const auto action = publish(prepared->snapshot);
    if (action.outcomeUnknown || action.state == ActionState::Skipped || action.replacementSelection || !action.artifacts.empty()) outcome.action = unknown();
    else if (action.state == ActionState::Succeeded || action.state == ActionState::Cancelled) outcome.action = {action.state, {}, {}};
    else outcome.action = failed(action.diagnostic == "pin-unavailable" ? "pin-unavailable" : "pin-failed");
  } catch (...) { outcome.action = unknown(); }
  return outcome;
}
} // namespace xerahs::app
