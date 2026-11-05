#pragma once
#include <QString>
namespace xerahs::app { QString recordingAudioErrorMessage(const QString &code); }
#ifdef XERAHS_GSTREAMER
#include <gst/gst.h>
namespace xerahs::app {
// Inspect only the trusted source identity, never backend error/debug payloads.
QString recordingErrorCode(GstMessage *message);
}
#endif
