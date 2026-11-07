#include "RecordingErrors.h"
#include <QCoreApplication>
namespace xerahs::app {
QString recordingAudioErrorMessage(const QString &code) {
  if (code == "audio-consent-required") return QCoreApplication::translate("RecordingErrors", "Audio was not started. Explicitly allow each selected input for this recording, or leave audio unchecked.");
  if (code == "audio-device-missing") return QCoreApplication::translate("RecordingErrors", "A selected audio input is unavailable. Reconnect it or choose another input in Recording settings, then start a new recording.");
  if (code == "audio-input-unavailable") return QCoreApplication::translate("RecordingErrors", "Audio capture failed and recording stopped. Check input availability and platform permission before starting again. Any recoverable partial output is listed below; it may need repair.");
  return {};
}
}
#ifdef XERAHS_GSTREAMER
namespace xerahs::app {
QString recordingErrorCode(GstMessage *message) {
  auto *object = GST_MESSAGE_SRC(message);
  if (object) gst_object_ref(object);
  while (object) {
    gchar *name = gst_object_get_name(object);
    const bool audio = name && (qstrcmp(name, "audioinput0") == 0 || qstrcmp(name, "audioinput1") == 0);
    g_free(name);
    auto *parent = audio ? nullptr : gst_object_get_parent(object);
    gst_object_unref(object); object = parent;
    if (audio) return "audio-input-unavailable";
  }
  return "encoder-failed";
}
}
#endif
