#include "Recording.h"
#include "naming/FilenameGenerator.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUuid>
#include <cmath>
#include <algorithm>
namespace xerahs::recording {
bool terminal(State s) { return s == State::Completed || s == State::Cancelled || s == State::Failed || s == State::PermissionDenied; }
QString stateName(State s) {
  return QStringList{"idle", "requesting-permission", "starting", "recording", "paused", "stopping", "completed", "cancelled", "failed", "permission-denied"}.at(int(s));
}
bool Session::observe(const Request &r, const QString &origin, workflows::WorkflowExecution::Writer writer) {
  if (m_state != State::Idle || m_execution) return false;
  const auto rectangle = [](const auto &value) -> QJsonValue {
    return value ? QJsonValue(QJsonArray{value->x(), value->y(), value->width(), value->height()}) : QJsonValue(QJsonValue::Null);
  };
  QJsonObject snapshot{{"definitionVersion", 2}, {"id", m_id}, {"capture", "recording"}, {"format", extension(r.format)},
      {"directory", r.folder}, {"filename", r.filename}, {"target", r.target == Target::Monitor ? "monitor" : "window"},
      {"frameRate", r.frameRate}, {"quality", r.quality}, {"cursor", r.cursor}, {"systemAudioDevice", r.systemAudioDevice},
      {"microphoneDevice", r.microphoneDevice}, {"region", rectangle(r.region)}, {"crop", rectangle(r.crop)},
      {"streamSize", QJsonArray{r.streamSize.width(), r.streamSize.height()}}};
  m_execution = std::make_unique<workflows::WorkflowExecution>(m_id, origin, snapshot, std::move(writer));
  m_executionConfirmed = m_execution->admit() && m_execution->start();
  return m_executionConfirmed;
}
QString Session::makeId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
ControlResult Session::result() const {
  return {false, false, m_id, m_state, m_output, m_recoverable, m_diagnostic, !m_recoverable.isEmpty()};
}
void Session::outputs(const QString &published, const QString &recoverable) {
  m_output = published; m_recoverable = recoverable;
}
bool Session::finalize(Output &output) {
  if (m_state != State::Stopping) return false;
  const auto error = output.publish();
  if (!error.isEmpty()) return fail(output, error);
  outputs(output.destination());
  return send(Event::Finalized);
}
bool Session::fail(Output &output, const QString &diagnostic) {
  if (m_state == State::Idle || terminal(m_state)) return false;
  outputs({}, output.retainFailure());
  return send(Event::Error, diagnostic);
}
ControlResult Session::control(const QString &sessionId, Event event) {
  auto r = result();
  if (sessionId != m_id) { r.diagnostic = "stale-recording-session"; return r; }
  if (event != Event::Pause && event != Event::Resume && event != Event::Stop && event != Event::Abort) {
    r.diagnostic = "invalid-recording-control"; return r;
  }
  // Repeated controls do not repeat native side effects or emit another terminal.
  if ((event == Event::Stop && (m_state == State::Stopping || m_state == State::Completed)) ||
      (event == Event::Pause && m_state == State::Paused) ||
      (event == Event::Resume && m_state == State::Recording) ||
      (event == Event::Abort && m_state == State::Cancelled)) { r.accepted = true; return r; }
  const bool changed = send(event);
  r = result(); r.accepted = changed; r.changed = changed;
  if (!changed) r.diagnostic = "invalid-recording-transition";
  return r;
}
bool Session::send(Event e, const QString &diagnostic) {
  if (terminal(m_state)) return false;
  State next = m_state;
  if (e == Event::Abort || e == Event::Cancel) next = State::Cancelled;
  else if (e == Event::Error && m_state != State::Idle) next = State::Failed;
  else if (e == Event::Request && m_state == State::Idle) next = State::RequestingPermission;
  else if (e == Event::Denied && m_state == State::RequestingPermission) next = State::PermissionDenied;
  else if (e == Event::Granted && m_state == State::RequestingPermission) next = State::Starting;
  else if (e == Event::Started && m_state == State::Starting) next = State::Recording;
  else if (e == Event::Pause && m_state == State::Recording) next = State::Paused;
  else if (e == Event::Resume && m_state == State::Paused) next = State::Recording;
  else if (e == Event::Stop && (m_state == State::Recording || m_state == State::Paused)) next = State::Stopping;
  else if (e == Event::Finalized && m_state == State::Stopping) next = State::Completed;
  if (next == m_state) return false;
  const auto previous = m_state;
  m_state = next;
  if (m_execution && m_executionConfirmed) {
    if (next == State::Recording) m_executionConfirmed = m_execution->step(previous == State::Paused ? "resume-recording" : "start-recording", "succeeded");
    else if (next == State::Paused) m_executionConfirmed = m_execution->step("pause-recording", "succeeded");
    else if (terminal(next)) {
      const auto action = next == State::Completed || previous == State::Stopping ? QString("stop-recording") :
          e == Event::Abort ? QString("abort-recording") : QString("capture-recording");
      const auto result = next == State::Completed ? QString("succeeded") : next == State::Cancelled ? QString("cancelled") :
          next == State::PermissionDenied ? QString("permission-denied") : QString("failed");
      const auto state = next == State::Failed ? xerahs::actions::failureClassName(xerahs::actions::classifyFailure(diagnostic)) : result;
      m_executionConfirmed = m_execution->step(action, result, diagnostic) && m_execution->finish(state, diagnostic);
    }
  }
  emit changed();
  if (terminal(next)) {
    static const QSet<QString> codes{"portal-unavailable", "portal-timeout", "portal-invalid-response", "target-lost", "encoder-unavailable", "encoder-start-failed", "encoder-failed", "finalize-timeout", "output-create-failed", "output-publish-failed", "request-invalid", "region-outside-stream", "region-mapping-unavailable"};
    m_diagnostic = next == State::Failed ? (codes.contains(diagnostic) ? diagnostic : QStringLiteral("encoder-failed")) : QString{};
    emit finished(next, m_diagnostic);
  }
  return true;
}
CropMapping mapCrop(const QRectF &region, const QRectF &desktop, QSize size) {
  const auto finite = [](const QRectF &r) { return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height()); };
  if (!finite(region) || !finite(desktop) || region.isEmpty() || desktop.isEmpty() || size.isEmpty()) return {{}, "region-mapping-unavailable"};
  if (!desktop.contains(region)) return {{}, "region-outside-stream"};
  const int left = int(std::floor((region.x() - desktop.x()) * size.width() / desktop.width()));
  const int top = int(std::floor((region.y() - desktop.y()) * size.height() / desktop.height()));
  const int right = qMin(size.width(), int(std::ceil((region.x() + region.width() - desktop.x()) * size.width() / desktop.width())));
  const int bottom = qMin(size.height(), int(std::ceil((region.y() + region.height() - desktop.y()) * size.height() / desktop.height())));
  const QRect result(left, top, (right - left) & ~1, (bottom - top) & ~1);
  return result.isEmpty() ? CropMapping{{}, "region-outside-stream"} : CropMapping{result, {}};
}
AudioDevice audioDevice(const QVariantMap &properties, const QString &label) {
  QString backend, id;
  for (const auto &key : {"internal-name", "node.name", "device.name"}) {
    id = properties.value(key).toString();
    if (!id.isEmpty()) { backend = key; break; }
  }
  if (id.isEmpty()) return {};
  return {backend + ':' + id, label, id.endsWith(".monitor") || properties.value("device.class").toString() == "monitor"};
}
AudioSelection selectAudio(const Request &r, const DeviceMonitor &monitor) {
  AudioSelection result;
  if (r.format == Format::Gif || (r.systemAudioDevice.isEmpty() && r.microphoneDevice.isEmpty())) return result;
  const auto devices = monitor ? monitor() : QVector<AudioDevice>{};
  for (const auto &input : {qMakePair(QString("system audio"), r.systemAudioDevice), qMakePair(QString("microphone"), r.microphoneDevice)}) {
    if (input.second.isEmpty()) continue;
    auto found = std::find_if(devices.cbegin(), devices.cend(), [&](const AudioDevice &d) { return d.id == input.second; });
    if (found == devices.cend()) result.diagnostics << "audio-device-missing: " + input.first + " (" + input.second + ')';
    else if (std::none_of(result.devices.cbegin(), result.devices.cend(), [&](const AudioDevice &d) { return d.id == found->id; })) result.devices << *found;
  }
  // An unavailable selected input must not produce a partial, unexpected mix.
  if (!result.diagnostics.isEmpty()) result.devices.clear();
  return result;
}
QString extension(Format f) { return f == Format::Mp4 ? "mp4" : f == Format::WebM ? "webm" : "gif"; }
unsigned portalSourceType(Target target) { return target == Target::Monitor ? 1u : 2u; }
unsigned portalCursorMode(bool cursor) { return cursor ? 2u : 1u; }
QString indicatorText(State state, qint64 elapsedMs, const QString &encoder, bool audioActive) {
  const qint64 seconds = elapsedMs / 1000;
  return QStringLiteral("XerahS · %1 · %2:%3 · %4 · %5").arg(stateName(state)).arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')).arg(encoder).arg(audioActive ? QStringLiteral("Audio recording") : QStringLiteral("No audio"));
}
QString validate(const Request &r) {
  if (r.frameRate < 1 || r.frameRate > 120 || r.quality < 0 || r.quality > 100) return "request-invalid";
  if (r.target != Target::Monitor && r.target != Target::Window) return "request-invalid";
  if (r.format != Format::Mp4 && r.format != Format::WebM && r.format != Format::Gif) return "request-invalid";
  if (r.region && (r.target != Target::Monitor || r.region->isEmpty() || !std::isfinite(r.region->x()) || !std::isfinite(r.region->y()) || !std::isfinite(r.region->width()) || !std::isfinite(r.region->height()))) return "request-invalid";
  if (r.crop && (r.target != Target::Monitor || r.crop->x() < 0 || r.crop->y() < 0 || r.crop->isEmpty())) return "request-invalid";
  if (!QDir::isAbsolutePath(r.folder) || r.folder.contains(QChar(0)) || r.filename.contains(QChar(0)) || r.filename.contains('/') || r.filename.contains('\\') || r.filename == "." || r.filename == ".." || r.filename.isEmpty() || !r.filename.endsWith('.' + extension(r.format))) return "request-invalid";
  return {};
}
QString generatedFilename(Format f) {
  const auto now = QDateTime::currentDateTime();
  naming::ExpansionContext c; c.date = now.date(); c.time = now.time(); c.unixTime = now.toSecsSinceEpoch();
  const auto result = naming::expand({"Recording_%y-%mo-%d_%h-%mi-%s", naming::ParseMode::Filename, extension(f), {}}, c);
  return result.ok() ? result.value() : QStringLiteral("Recording.") + extension(f);
}
Encoder selectEncoder(Format f, const QSet<QString> &available) {
  QStringList candidates = f == Format::Mp4 ? QStringList{"x264enc", "openh264enc", "vah264enc", "vaapih264enc"} : f == Format::WebM ? QStringList{"vp9enc"} : QStringList{"gifenc"};
  QStringList common{"videoconvert", "videorate", "videoscale", "capsfilter", "filesink"};
  if (f == Format::Mp4) common << "h264parse" << "mp4mux";
  if (f == Format::WebM) common << "webmmux";
  for (const auto &candidate : candidates) {
    auto required = common; required << candidate;
    bool ok = true; for (const auto &name : required) ok &= available.contains(name);
    if (ok) return {candidate, required, {}};
  }
  return {{}, {}, "encoder-unavailable"};
}
QString pipeline(const Request &r, const Encoder &e, const QString &source, int audioInputs, const QString &audioEncoder) {
  if (!validate(r).isEmpty() || e.element.isEmpty() || (r.crop && (r.streamSize.isEmpty() || !QRect(QPoint{}, r.streamSize).contains(*r.crop)))) return {};
  QString description = source + QStringLiteral(" ! videoconvert ! videorate ! video/x-raw,framerate=%1/1 ! videoscale").arg(r.frameRate);
  if (r.crop) description += QStringLiteral(" ! videocrop name=regioncrop left=%1 top=%2 right=%3 bottom=%4")
      .arg(r.crop->x()).arg(r.crop->y()).arg(r.streamSize.width() - r.crop->x() - r.crop->width()).arg(r.streamSize.height() - r.crop->y() - r.crop->height());
  else if (r.region) description += " ! videocrop name=regioncrop";
  // Prefer a broadly supported H.264 raw format; negotiation failures are explicit.
  if (r.format == Format::Mp4) description += " ! video/x-raw,format=I420";
  description += " ! " + e.element;
  if (e.element == "x264enc") description += QStringLiteral(" quantizer=%1 pass=qual speed-preset=veryfast").arg(50 - r.quality * 50 / 100);
  else if (e.element == "openh264enc") description += QStringLiteral(" bitrate=%1").arg(500000 + r.quality * 95000);
  else if (e.element == "vah264enc" || e.element == "vaapih264enc") description += QStringLiteral(" bitrate=%1").arg(500 + r.quality * 95);
  else if (e.element == "vp9enc") description += QStringLiteral(" deadline=1 end-usage=cq cq-level=%1").arg(63 - r.quality * 63 / 100);
  if (r.format == Format::Mp4) description += " ! h264parse ! mp4mux";
  else if (r.format == Format::WebM) description += " ! webmmux";
  description += " ! filesink name=output";
  if (audioInputs > 0 && r.format != Format::Gif) {
    if ((r.format == Format::Mp4 && audioEncoder != "voaacenc" && audioEncoder != "avenc_aac") ||
        (r.format == Format::WebM && audioEncoder != "opusenc") || audioInputs > 2) return {};
    description.replace(r.format == Format::Mp4 ? "mp4mux" : "webmmux", r.format == Format::Mp4 ? "mp4mux name=mux" : "webmmux name=mux");
    description += " audiomixer name=audiomix ! audioconvert ! audioresample ! " + audioEncoder + " ! queue ! mux.";
    for (int i = 0; i < audioInputs; ++i) description += QStringLiteral(" queue name=audioqueue%1 ! audioconvert ! audioresample ! audiomix.").arg(i);
  }
  return description;
}
Output::~Output() { abort(); }
QString Output::reserve(const Request &r) {
  abort(); if (!validate(r).isEmpty() || !QDir().mkpath(r.folder)) return "output-create-failed";
  m_destination = QDir(r.folder).filePath(r.filename);
  if (QFileInfo::exists(m_destination)) { m_destination.clear(); return "output-create-failed"; }
  QTemporaryFile file(QDir(r.folder).filePath(".xerahs-recording-XXXXXX.part"));
  if (!file.open()) return "output-create-failed";
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner); file.setAutoRemove(false); m_temporary = file.fileName(); return {};
}
QString Output::publish() {
  if (m_temporary.isEmpty() || QFileInfo(m_temporary).size() == 0 || QFileInfo::exists(m_destination) || !QFile::rename(m_temporary, m_destination)) return "output-publish-failed";
  m_temporary.clear(); return {};
}
QString Output::retainFailure() {
  if (m_temporary.isEmpty() || QFileInfo(m_temporary).size() <= 0) { abort(); return {}; }
  const QString retained = m_temporary;
  m_temporary.clear();
  return retained;
}
void Output::abort() { if (!m_temporary.isEmpty()) QFile::remove(m_temporary); m_temporary.clear(); }
}
