// CORE-018: native Audio/Source enumeration and exact device creation.
#pragma once
#include "recording/Recording.h"
#ifdef XERAHS_GSTREAMER
#include <gst/gst.h>
#endif
namespace xerahs::app {
class RecordingDevices {
public:
  RecordingDevices();
  ~RecordingDevices();
  RecordingDevices(const RecordingDevices &) = delete;
  RecordingDevices &operator=(const RecordingDevices &) = delete;
  QVector<recording::AudioDevice> list() const;
#ifdef XERAHS_GSTREAMER
  GstElement *create(const QString &id, const char *name) const;
private:
  GstDeviceMonitor *m_monitor = nullptr;
#endif
};
}
