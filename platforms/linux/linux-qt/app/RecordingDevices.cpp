// CORE-018: retain monitor sources and never use a backend's default input.
#include "RecordingDevices.h"
#include <algorithm>
namespace xerahs::app {
#ifdef XERAHS_GSTREAMER
namespace {
recording::AudioDevice describe(GstDevice *device) {
  auto *properties = gst_device_get_properties(device);
  QVariantMap values;
  if (properties) {
    for (const auto *key : {"device.name", "node.name", "device.class"}) {
      const auto *value = gst_structure_get_string(properties, key);
      if (value) values.insert(key, QString::fromUtf8(value));
    }
    gst_structure_free(properties);
  }
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(device), "internal-name")) {
    gchar *internal = nullptr; g_object_get(device, "internal-name", &internal, nullptr);
    if (internal) values.insert("internal-name", QString::fromUtf8(internal));
    g_free(internal);
  }
  auto *name = gst_device_get_display_name(device);
  auto result = recording::audioDevice(values, QString::fromUtf8(name)); g_free(name);
  return result;
}
}
#endif
RecordingDevices::RecordingDevices() {
#ifdef XERAHS_GSTREAMER
  if (!gst_init_check(nullptr, nullptr, nullptr)) return;
  m_monitor = gst_device_monitor_new();
  auto *bus = gst_device_monitor_get_bus(m_monitor);
  gst_bus_set_flushing(bus, true); gst_object_unref(bus);
  gst_device_monitor_set_show_all_devices(m_monitor, true);
  gst_device_monitor_add_filter(m_monitor, "Audio/Source", nullptr);
  if (!gst_device_monitor_start(m_monitor)) { gst_object_unref(m_monitor); m_monitor = nullptr; }
#endif
}
RecordingDevices::~RecordingDevices() {
#ifdef XERAHS_GSTREAMER
  if (m_monitor) { gst_device_monitor_stop(m_monitor); gst_object_unref(m_monitor); }
#endif
}
QVector<recording::AudioDevice> RecordingDevices::list() const {
  QVector<recording::AudioDevice> result;
#ifdef XERAHS_GSTREAMER
  if (!m_monitor) return result;
  auto *devices = gst_device_monitor_get_devices(m_monitor);
  for (auto *entry = devices; entry; entry = entry->next) {
    const auto device = describe(GST_DEVICE(entry->data));
    if (!device.id.isEmpty() && std::none_of(result.cbegin(), result.cend(), [&](const auto &d) { return d.id == device.id; })) result << device;
  }
  g_list_free_full(devices, gst_object_unref);
#endif
  return result;
}
#ifdef XERAHS_GSTREAMER
GstElement *RecordingDevices::create(const QString &id, const char *name) const {
  if (!m_monitor || id.isEmpty()) return nullptr;
  GstElement *result = nullptr;
  auto *devices = gst_device_monitor_get_devices(m_monitor);
  for (auto *entry = devices; entry; entry = entry->next) {
    auto *device = GST_DEVICE(entry->data);
    if (describe(device).id == id) { result = gst_device_create_element(device, name); break; }
  }
  g_list_free_full(devices, gst_object_unref);
  return result;
}
#endif
}
