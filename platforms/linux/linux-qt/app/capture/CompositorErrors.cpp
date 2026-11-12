#include "capture/CompositorErrors.h"

#include "capture/CompositorFrame.h"

#include <QCoreApplication>

namespace xerahs::app {
namespace {
// The capture layer names its diagnostics; the interface names its own sentences.
QString text(const char *source) { return QCoreApplication::translate("CompositorCapture", source); }
}  // namespace

QString compositorCaptureFailureMessage(const QString &diagnostic) {
  if (diagnostic == kCompositorCaptureUnsupported)
    return text("This desktop does not support screenshots with the pointer. Turn off Include the pointer in screenshots to use the Screenshot portal.");
  if (diagnostic == kCompositorLayoutUnproven)
    return text("Screenshots with the pointer currently require one named display with integer scaling and no rotation. Turn off Include the pointer in screenshots to use the Screenshot portal.");
  if (diagnostic == kCompositorSourceMismatch)
    return text("The compositor frame does not match the display. Capture was stopped. Try again after checking display settings.");
  if (diagnostic == kCompositorCaptureTimedOut)
    return text("The desktop did not return a screenshot in time. Try again or turn off Include the pointer in screenshots.");
  // capture-source-failed and every unknown diagnostic share this sentence.
  return text("The desktop could not provide a screenshot with the pointer. Try again or turn off Include the pointer in screenshots.");
}
}  // namespace xerahs::app
