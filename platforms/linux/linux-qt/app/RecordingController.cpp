#include "RecordingController.h"
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QUuid>
namespace xerahs::app {
using namespace recording;
namespace { const QString service = "org.freedesktop.portal.Desktop"; const QString desktop = "/org/freedesktop/portal/desktop"; const QString screenCast = "org.freedesktop.portal.ScreenCast"; }
RecordingController::RecordingController(QObject *parent) : QObject(parent) {
#ifdef XERAHS_GSTREAMER
  if (gst_init_check(nullptr, nullptr, nullptr)) {
    for (const auto &name : {"pipewiresrc", "videoconvert", "videorate", "videoscale", "capsfilter", "filesink", "x264enc", "openh264enc", "vah264enc", "vaapih264enc", "h264parse", "mp4mux", "vp9enc", "webmmux", "gifenc", "videocrop", "audiomixer", "audioconvert", "audioresample", "queue", "voaacenc", "avenc_aac", "opusenc"}) {
      auto *factory = gst_element_factory_find(name); if (factory) { m_elements.insert(name); gst_object_unref(factory); }
    }
  }
#endif
  m_poll.setInterval(100); connect(&m_poll, &QTimer::timeout, this, &RecordingController::poll);
  m_deadline.setSingleShot(true); connect(&m_deadline, &QTimer::timeout, this, [this] { fail(state() == State::Stopping ? "finalize-timeout" : "portal-timeout"); });
  auto *watcher = new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &) { if (busy()) fail("target-lost"); });
}
RecordingController::~RecordingController() { teardown(); }
State RecordingController::state() const { return m_session ? m_session->state() : State::Idle; }
bool RecordingController::busy() const { return state() != State::Idle && !terminal(state()); }
QString RecordingController::unavailable(Format format) const {
#ifndef XERAHS_GSTREAMER
  Q_UNUSED(format); return "Recording unavailable: built without GStreamer support.";
#else
  if (!m_bus.isConnected()) return "Recording unavailable: desktop session D-Bus is disconnected.";
  if (!m_elements.contains("pipewiresrc")) return "Recording unavailable: GStreamer pipewiresrc is missing.";
  const auto choice = selectEncoder(format, m_elements);
  if (!choice.diagnostic.isEmpty()) return "Recording unavailable: encoder or muxer elements for " + extension(format) + " are missing.";
  return {};
#endif
}
QString RecordingController::indicator() const {
  const qint64 seconds = (m_elapsed + (state() == State::Recording && m_clock.isValid() ? m_clock.elapsed() : 0)) / 1000;
  return QStringLiteral("XerahS · %1 · %2:%3 · %4 · %5").arg(stateName(state())).arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')).arg(m_encoder.element).arg(m_audioActive ? QStringLiteral("Audio recording") : QStringLiteral("No audio"));
}
void RecordingController::start(Request request) {
  if (busy()) return;
  teardown(); m_request = std::move(request); m_elapsed = 0; m_clock.invalidate();
  m_session = std::make_unique<Session>();
  connect(m_session.get(), &Session::changed, this, &RecordingController::changed);
  connect(m_session.get(), &Session::finished, this, [this](State s, const QString &code) {
    const auto path = m_output.destination(); teardown();
    if (s == State::Completed) emit notify("Screen recording completed", path, true);
    else emit notify("Screen recording " + stateName(s), code.isEmpty() ? stateName(s) : code + ". Retry recording after checking the source and output folder.", false);
  });
  m_session->send(Event::Request);
  if (!validate(m_request).isEmpty()) { fail("request-invalid"); return; }
  if (!unavailable(m_request.format).isEmpty()) { fail("encoder-unavailable"); return; }
  m_encoder = selectEncoder(m_request.format, m_elements);
  // Resolve a unique owner before subscribing; never accept another service's responses.
  auto *watch = new QDBusPendingCallWatcher(m_bus.interface()->asyncCall("StartServiceByName", service, uint(0)), this);
  const int generation = m_generation;
  connect(watch, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
    w->deleteLater(); if (generation != m_generation || !busy()) return;
    const QDBusReply<QString> owner = m_bus.interface()->serviceOwner(service);
    if (!owner.isValid()) { fail("portal-unavailable"); return; }
    m_owner = owner.value();
    const QString token = "xerahs_" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = m_bus.baseService().mid(1); sender.replace('.', '_');
    m_sessionPath = "/org/freedesktop/portal/desktop/session/" + sender + '/' + token;
    call("CreateSession", {}, {{"session_handle_token", token}});
  });
  m_deadline.start(30000);
}
void RecordingController::call(const QString &method, QVariantList arguments, QVariantMap options) {
  if (!busy()) return;
  if (!m_path.isEmpty()) m_bus.disconnect(m_owner, m_path, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
  m_method = method;
  const QString token = "xerahs_" + QUuid::createUuid().toString(QUuid::Id128);
  QString sender = m_bus.baseService().mid(1); sender.replace('.', '_');
  m_path = "/org/freedesktop/portal/desktop/request/" + sender + '/' + token;
  options.insert("handle_token", token); arguments.append(options);
  if (!m_bus.connect(m_owner, m_path, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)))) { fail("portal-unavailable"); return; }
  auto message = QDBusMessage::createMethodCall(m_owner, desktop, screenCast, method); message.setArguments(arguments);
  auto *watch = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 25000), this);
  const auto path = m_path; const auto owner = m_owner; const auto bus = m_bus; const auto sessionPath = m_sessionPath; const int generation = m_generation;
  connect(watch, &QDBusPendingCallWatcher::finished, this, [this, path, generation, owner, bus, sessionPath](QDBusPendingCallWatcher *w) {
    QDBusPendingReply<QDBusObjectPath> reply = *w; w->deleteLater();
    if (generation != m_generation || !busy()) {
      if (!reply.isError()) { auto close = QDBusMessage::createMethodCall(owner, reply.value().path(), "org.freedesktop.portal.Request", "Close"); bus.asyncCall(close); }
      if (!sessionPath.isEmpty()) bus.asyncCall(QDBusMessage::createMethodCall(owner, sessionPath, "org.freedesktop.portal.Session", "Close"));
      return;
    }
    if (m_path != path) return; // Response already advanced the request before the method reply.
    if (reply.isError()) { fail("portal-unavailable"); return; }
    if (reply.value().path() != path) { fail("portal-invalid-response"); return; }
    // The user's consent interaction is intentionally not timed out.
    if (m_method == "Start") m_deadline.stop();
  });
  m_deadline.start(30000);
}
void RecordingController::response(uint code, const QVariantMap &results, const QDBusMessage &message) {
  if (!busy() || message.path() != m_path || message.service() != m_owner) return;
  m_bus.disconnect(m_owner, m_path, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
  m_path.clear(); m_deadline.stop();
  if (code != 0) { m_session->send(code == 1 ? Event::Cancel : Event::Denied); return; }
  if (m_method == "CreateSession") {
    if (results.value("session_handle").toString() != m_sessionPath) { fail("portal-invalid-response"); return; }
    m_bus.connect(m_owner, m_sessionPath, "org.freedesktop.portal.Session", "Closed", this, SLOT(closed()));
    call("SelectSources", {QVariant::fromValue(QDBusObjectPath(m_sessionPath))}, {{"types", uint(m_request.target == Target::Monitor ? 1 : 2)}, {"multiple", false}, {"cursor_mode", uint(m_request.cursor ? 2 : 1)}});
  } else if (m_method == "SelectSources") {
    call("Start", {QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QString{}}, {});
  } else if (m_method == "Start") {
    const auto argument = qvariant_cast<QDBusArgument>(results.value("streams"));
    uint node = 0; QVariantMap properties; int count = 0;
    argument.beginArray(); while (!argument.atEnd()) { argument.beginStructure(); argument >> node >> properties; argument.endStructure(); ++count; } argument.endArray();
    if (count != 1 || !node) { fail("portal-invalid-response"); return; }
    if (properties.contains("source_type") && properties.value("source_type").toUInt() != uint(m_request.target == Target::Monitor ? 1 : 2)) { fail("portal-invalid-response"); return; }
    if (m_request.region) {
      const auto pair = [](const QVariant &value) -> std::optional<QPoint> {
        if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return {};
        const auto argument = qvariant_cast<QDBusArgument>(value);
        if (argument.currentSignature() != "(ii)") return {};
        int x = 0, y = 0; argument.beginStructure(); argument >> x >> y; argument.endStructure();
        return QPoint(x, y);
      };
      const auto position = pair(properties.value("position")), size = pair(properties.value("size"));
      if (!position || !size || size->x() <= 0 || size->y() <= 0) { fail("region-mapping-unavailable"); return; }
      m_streamDesktop = QRectF(*position, QSizeF(size->x(), size->y()));
      if (!m_streamDesktop.contains(*m_request.region)) { fail("region-outside-stream"); return; }
    }
    m_session->send(Event::Granted);
    auto message = QDBusMessage::createMethodCall(m_owner, desktop, screenCast, "OpenPipeWireRemote");
    message.setArguments({QVariant::fromValue(QDBusObjectPath(m_sessionPath)), QVariantMap{}});
    auto *watch = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 25000), this); const int generation = m_generation;
    connect(watch, &QDBusPendingCallWatcher::finished, this, [this, generation, node](QDBusPendingCallWatcher *w) {
      QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w; w->deleteLater();
      if (generation != m_generation || !busy()) return;
      if (reply.isError() || !reply.value().isValid()) { fail("portal-unavailable"); return; }
      m_fd = reply.value(); encode(node);
    }); m_deadline.start(30000);
  }
}
void RecordingController::closed() { if (busy()) fail("target-lost"); }
void RecordingController::encode(uint node) {
#ifdef XERAHS_GSTREAMER
  m_deadline.stop(); m_node = node;
  const auto error = m_output.reserve(m_request); if (!error.isEmpty()) { fail(error); return; }
  if (m_request.region && !m_elements.contains("videocrop")) { fail("encoder-unavailable"); return; }
  const auto audio = selectAudio(m_request, [this] { return m_devices.list(); });
  for (const auto &diagnostic : audio.diagnostics) emit notify("Recording without audio", diagnostic, false);
  // Live-source timestamps use pipeline running time, preserved across PAUSED/PLAYING.
  const QString source = QStringLiteral("pipewiresrc fd=%1 path=%2 do-timestamp=true").arg(m_fd.fileDescriptor()).arg(node);
  GError *errorInfo = nullptr;
  QVector<GstElement *> inputs;
  for (int i = 0; i < audio.devices.size(); ++i) {
    auto *input = m_devices.create(audio.devices[i].id, qPrintable(QString("audioinput%1").arg(i)));
    if (!input) {
      emit notify("Recording without audio", "audio-device-missing: " + audio.devices[i].label + " (" + audio.devices[i].id + ')', false);
      for (auto *created : inputs) gst_object_unref(created);
      inputs.clear(); break;
    }
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(input), "do-timestamp")) g_object_set(input, "do-timestamp", true, nullptr);
    inputs << input;
  }
  QString description = pipeline(m_request, m_encoder, source);
  if (!inputs.isEmpty()) {
    const QString encoder = m_request.format == Format::WebM ? QStringLiteral("opusenc") :
        m_elements.contains("voaacenc") ? QStringLiteral("voaacenc") : QStringLiteral("avenc_aac");
    if (!m_elements.contains(encoder) || !m_elements.contains("audiomixer") || !m_elements.contains("audioconvert") || !m_elements.contains("audioresample") || !m_elements.contains("queue")) {
      for (auto *input : inputs) gst_object_unref(input);
      fail("encoder-unavailable"); return;
    }
    description = pipeline(m_request, m_encoder, source, inputs.size(), encoder);
  }
  m_pipeline = gst_parse_launch(description.toUtf8().constData(), &errorInfo);
  if (errorInfo || !m_pipeline) {
    for (auto *input : inputs) gst_object_unref(input);
  }
  if (errorInfo) { g_error_free(errorInfo); fail("encoder-start-failed"); return; }
  if (!m_pipeline) { fail("encoder-start-failed"); return; }
  for (int i = 0; i < inputs.size(); ++i) {
    auto *input = inputs[i];
    auto *queue = gst_bin_get_by_name(GST_BIN(m_pipeline), qPrintable(QString("audioqueue%1").arg(i)));
    gst_bin_add(GST_BIN(m_pipeline), input);
    const bool linked = gst_element_link(input, queue); gst_object_unref(queue);
    if (!linked) { for (int j = i + 1; j < inputs.size(); ++j) gst_object_unref(inputs[j]); fail("encoder-start-failed"); return; }
  }
  m_audioActive = !inputs.isEmpty();
  if (m_request.region) {
    auto *crop = gst_bin_get_by_name(GST_BIN(m_pipeline), "regioncrop");
    auto *pad = gst_element_get_static_pad(crop, "sink");
    struct Mapping { RecordingController *owner; QRectF region, desktop; int generation; };
    auto *mapping = new Mapping{this, *m_request.region, m_streamDesktop, m_generation};
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, [](GstPad *pad, GstPadProbeInfo *info, gpointer data) -> GstPadProbeReturn {
      auto *mapping = static_cast<Mapping *>(data);
      auto *event = GST_PAD_PROBE_INFO_EVENT(info);
      if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
      GstCaps *caps = nullptr; gst_event_parse_caps(event, &caps);
      int width = 0, height = 0;
      const auto *structure = gst_caps_get_structure(caps, 0);
      gst_structure_get_int(structure, "width", &width); gst_structure_get_int(structure, "height", &height);
      const auto result = mapCrop(mapping->region, mapping->desktop, {width, height});
      if (!result.diagnostic.isEmpty()) {
        QMetaObject::invokeMethod(mapping->owner, [owner = mapping->owner, generation = mapping->generation, code = result.diagnostic] {
          if (owner->m_generation == generation) owner->fail(code);
        }, Qt::QueuedConnection);
        return GST_PAD_PROBE_DROP;
      }
      auto *element = gst_pad_get_parent_element(pad);
      const auto r = result.rectangle;
      g_object_set(element, "left", r.x(), "top", r.y(), "right", width - r.x() - r.width(), "bottom", height - r.y() - r.height(), nullptr);
      gst_object_unref(element);
      return GST_PAD_PROBE_OK;
    }, mapping, [](gpointer data) { delete static_cast<Mapping *>(data); });
    gst_object_unref(pad); gst_object_unref(crop);
  }
  auto *sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "output");
  g_object_set(sink, "location", m_output.temporary().toUtf8().constData(), nullptr); gst_object_unref(sink);
  if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) { if (m_audioActive) withoutAudio(); else fail("encoder-start-failed"); return; }
  m_poll.start(); m_deadline.start(30000);
#else
  Q_UNUSED(node); fail("encoder-unavailable");
#endif
}
void RecordingController::withoutAudio() {
#ifdef XERAHS_GSTREAMER
  const QString inputs = "system audio (" + m_request.systemAudioDevice + "), microphone (" + m_request.microphoneDevice + ')';
  if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_NULL); gst_object_unref(m_pipeline); m_pipeline = nullptr; }
  m_audioActive = false; m_request.systemAudioDevice.clear(); m_request.microphoneDevice.clear();
  emit notify("Recording without audio", "audio-input-unavailable: " + inputs, false);
  encode(m_node);
#endif
}
void RecordingController::poll() {
#ifdef XERAHS_GSTREAMER
  if (!m_pipeline) return;
  auto *bus = gst_element_get_bus(m_pipeline);
  while (auto *message = gst_bus_pop(bus)) {
    const auto type = GST_MESSAGE_TYPE(message);
    if (type == GST_MESSAGE_ERROR) {
      gst_message_unref(message); gst_object_unref(bus);
      if (state() == State::Starting && m_audioActive) withoutAudio(); else fail("encoder-failed");
      return;
    }
    if (type == GST_MESSAGE_EOS) {
      gst_message_unref(message); gst_object_unref(bus);
      if (state() != State::Stopping) { fail("target-lost"); return; }
      gst_element_set_state(m_pipeline, GST_STATE_NULL); gst_object_unref(m_pipeline); m_pipeline = nullptr;
      const auto error = m_output.publish(); if (!error.isEmpty()) fail(error); else m_session->send(Event::Finalized);
      return;
    }
    if (type == GST_MESSAGE_STATE_CHANGED && GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
      GstState old, current, pending; gst_message_parse_state_changed(message, &old, &current, &pending);
      if (current == GST_STATE_PLAYING && state() == State::Starting) { m_deadline.stop(); m_clock.start(); m_session->send(Event::Started); }
    }
    gst_message_unref(message);
  }
  gst_object_unref(bus);
#endif
  emit changed();
}
void RecordingController::pauseResume() {
#ifdef XERAHS_GSTREAMER
  if (!m_pipeline || (state() != State::Recording && state() != State::Paused)) return;
  const bool pause = state() == State::Recording;
  if (gst_element_set_state(m_pipeline, pause ? GST_STATE_PAUSED : GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) { fail("encoder-failed"); return; }
  if (pause) { m_elapsed += m_clock.elapsed(); m_clock.invalidate(); } else m_clock.start();
  m_session->send(pause ? Event::Pause : Event::Resume);
#endif
}
void RecordingController::stop() {
#ifdef XERAHS_GSTREAMER
  if (!m_pipeline || (state() != State::Recording && state() != State::Paused)) return;
  if (state() == State::Recording) m_elapsed += m_clock.elapsed();
  m_clock.invalidate(); m_session->send(Event::Stop);
  // A paused pipeline must run to drain queued buffers and deliver EOS.
  gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
  if (!gst_element_send_event(m_pipeline, gst_event_new_eos())) { fail("encoder-failed"); return; }
  m_deadline.start(15000);
#endif
}
void RecordingController::abort() { if (busy()) m_session->send(Event::Abort); }
void RecordingController::fail(const QString &code) { if (busy()) m_session->send(Event::Error, code); }
void RecordingController::teardown() {
  ++m_generation; m_deadline.stop(); m_poll.stop();
#ifdef XERAHS_GSTREAMER
  if (m_pipeline) { gst_element_set_state(m_pipeline, GST_STATE_NULL); gst_object_unref(m_pipeline); m_pipeline = nullptr; }
#endif
  m_audioActive = false;
  m_fd = {};
  if (!m_path.isEmpty()) {
    auto message = QDBusMessage::createMethodCall(m_owner, m_path, "org.freedesktop.portal.Request", "Close"); m_bus.asyncCall(message);
    m_bus.disconnect(m_owner, m_path, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage))); m_path.clear();
  }
  if (!m_sessionPath.isEmpty()) {
    m_bus.disconnect(m_owner, m_sessionPath, "org.freedesktop.portal.Session", "Closed", this, SLOT(closed()));
    auto message = QDBusMessage::createMethodCall(m_owner, m_sessionPath, "org.freedesktop.portal.Session", "Close"); m_bus.asyncCall(message); m_sessionPath.clear();
  }
  m_output.abort();
}
}
