// User-facing text for a failed compositor capture. The capture layer reports a
// stable diagnostic code; this turns one code into one translated sentence.
// The code, the output name and any path never reach the user.
#pragma once

#include <QString>

namespace xerahs::app {

// Unknown diagnostics use generic recovery guidance without input values.
QString compositorCaptureFailureMessage(const QString &diagnostic);

}  // namespace xerahs::app
