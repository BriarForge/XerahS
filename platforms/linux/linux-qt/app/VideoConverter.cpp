#include "VideoConverter.h"

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <cstdio>

namespace xerahs::app {
using namespace media;

namespace {
bool initGst() {
  static const bool ok = gst_init_check(nullptr, nullptr, nullptr);
  return ok;
}
// EU-015: GStreamer messages may quote paths; show file names only.
QString scrub(QString text, const Request &r) {
  for (const auto &p : {r.inputPath, r.outputPath}) {
    if (p.isEmpty()) continue;
    text.replace(QFileInfo(p).absolutePath() + '/', QString{});
  }
  return text;
}
}  // namespace

bool GstElementAvailability::hasElement(const QString &name) const {
  if (!initGst()) return false;
  auto *factory = gst_element_factory_find(name.toUtf8().constData());
  if (!factory) return false;
  gst_object_unref(factory);
  return true;
}

GstConversionRunner::GstConversionRunner(QObject *parent) : ConversionRunner(parent) {
  qRegisterMetaType<ConversionResult>();
  m_timer.setInterval(100);
  connect(&m_timer, &QTimer::timeout, this, &GstConversionRunner::poll);
}

GstConversionRunner::~GstConversionRunner() {
  // EU-017: abandon any probe and remove partial output, but never emit from a
  // destructor: the owning dialog is already partly destroyed when its children go.
  blockSignals(true);
  cancel();
}

void GstConversionRunner::start(const Request &request) {
  if (busy()) return;
  const auto generation = ++m_generation;
  m_request = request; m_diagnostics.clear(); m_lastPercent = -1;
  if (!initGst()) { ConversionResult r; r.error = ErrorCode::StartFailed; r.message = "GStreamer could not be initialised."; emit finished(r); return; }
  m_state = State::Probing;
  const auto uri = QUrl::fromLocalFile(QFileInfo(request.inputPath).absoluteFilePath()).toEncoded();
  struct Probe { InputInfo info; bool failed = true; };
  // Blocking discovery runs through OwnedWork (CORE-004): a late result after cancel or destroy is dropped.
  m_probeWork = OwnedWork::run<Probe>(this, [uri](const OwnedWork::Context &) {
    Probe probe; GError *error = nullptr;
    if (auto *discoverer = gst_discoverer_new(5 * GST_SECOND, &error)) {
      if (auto *result = gst_discoverer_discover_uri(discoverer, uri.constData(), &error)) {
        const auto status = gst_discoverer_info_get_result(result);
        if (status == GST_DISCOVERER_OK || status == GST_DISCOVERER_MISSING_PLUGINS) {
          auto *video = gst_discoverer_info_get_video_streams(result); auto *audio = gst_discoverer_info_get_audio_streams(result);
          probe.info.known = true; probe.info.hasVideo = video != nullptr; probe.info.hasAudio = audio != nullptr;
          probe.info.durationMs = qint64(gst_discoverer_info_get_duration(result) / GST_MSECOND);
          gst_discoverer_stream_info_list_free(video); gst_discoverer_stream_info_list_free(audio);
          probe.failed = false;
        }
        g_object_unref(result);
      }
      g_object_unref(discoverer);
    }
    if (error) g_error_free(error);
    return probe;
  }, [this, generation](Probe probe) { probed(generation, probe.info, probe.failed); });
}

void GstConversionRunner::probed(quint64 generation, InputInfo info, bool failed) {
  if (generation != m_generation || m_state != State::Probing) return;   // cancelled meanwhile
  if (failed) { finish(Outcome::Failed, ErrorCode::NoVideoStream, "The input could not be read as a video."); return; }
  launch(m_request, info);
}

void GstConversionRunner::launch(const Request &request, const InputInfo &info) {
  static const GstElementAvailability port;
  const auto plan = planConversion(request, info, port);
  if (!plan.ok()) { finish(Outcome::Failed, plan.error, plan.message); return; }
  m_diagnostics = plan.diagnostics;
  GError *error = nullptr;
  m_pipeline = gst_parse_launch(plan.description.toUtf8().constData(), &error);
  if (!m_pipeline || error) {
    const auto text = error ? QString::fromUtf8(error->message) : QString{};
    if (error) g_error_free(error);
    if (m_pipeline) { gst_object_unref(m_pipeline); m_pipeline = nullptr; }
    finish(Outcome::Failed, ErrorCode::ParseFailed, "The conversion pipeline could not be built: " + scrub(text, request));
    return;
  }
  m_partial = partialPathFor(request.outputPath, QString::number(QRandomGenerator::global()->generate(), 16));
  // EU-013: paths are structured properties, never part of the parsed text.
  auto *src = gst_bin_get_by_name(GST_BIN(m_pipeline), "src"); auto *sink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
  g_object_set(src, "location", QFileInfo(request.inputPath).absoluteFilePath().toUtf8().constData(), nullptr);
  g_object_set(sink, "location", m_partial.toUtf8().constData(), nullptr);
  gst_object_unref(src); gst_object_unref(sink);
  m_state = State::Running;
  if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    finish(Outcome::Failed, ErrorCode::StartFailed, "The conversion could not be started."); return;
  }
  m_timer.start();
  emit progress(0);
}

void GstConversionRunner::poll() {
  if (!m_pipeline) return;
  auto *bus = gst_element_get_bus(m_pipeline);
  while (auto *message = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
      gst_message_unref(message); gst_object_unref(bus);
      finish(Outcome::Success, ErrorCode::None, {});
      return;
    }
    GError *error = nullptr; gst_message_parse_error(message, &error, nullptr);
    const auto text = error ? QString::fromUtf8(error->message) : QStringLiteral("unknown error");
    if (error) g_error_free(error);
    gst_message_unref(message); gst_object_unref(bus);
    finish(Outcome::Failed, ErrorCode::PipelineFailed, scrub(text, m_request));
    return;
  }
  gst_object_unref(bus);
  gint64 position = 0, duration = 0;
  if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &position) && gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &duration) && duration > 0) {
    const int percent = int(std::clamp<gint64>(position * 100 / duration, 0, 99));
    if (percent != m_lastPercent) { m_lastPercent = percent; emit progress(percent); }
  }
}

void GstConversionRunner::teardown() {
  m_timer.stop();
  if (m_pipeline) {
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline); m_pipeline = nullptr;
  }
}

void GstConversionRunner::cancel() {
  if (m_state == State::Idle) return;
  m_probeWork.cancel();
  finish(Outcome::Cancelled, ErrorCode::None, "Conversion cancelled; no output was kept.");
}

void GstConversionRunner::finish(Outcome outcome, ErrorCode code, const QString &message) {
  teardown();
  ConversionResult result; result.outcome = outcome; result.error = code; result.message = message; result.diagnostics = m_diagnostics;
  if (outcome == Outcome::Success) {
    // Atomic publish: rename(2) replaces only when the user opted in; otherwise refuse a late-appearing file.
    const bool exists = QFileInfo::exists(m_request.outputPath);
    bool ok = false;
    if (!exists) ok = QFile::rename(m_partial, m_request.outputPath);
    else if (m_request.overwrite) ok = std::rename(m_partial.toUtf8().constData(), m_request.outputPath.toUtf8().constData()) == 0;
    if (ok) result.outputPath = m_request.outputPath;
    else { result.outcome = Outcome::Failed; result.error = exists && !m_request.overwrite ? ErrorCode::OutputExists : ErrorCode::OutputWriteFailed;
           result.message = "The converted video could not be saved to the output name."; }
  }
  if (!m_partial.isEmpty()) QFile::remove(m_partial);   // no-op after a successful rename
  m_partial.clear();
  ++m_generation;
  m_state = State::Idle;
  emit finished(result);
}

}  // namespace xerahs::app
