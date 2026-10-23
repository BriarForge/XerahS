#include "media/VideoConversion.h"

#include <QDir>
#include <QFileInfo>

namespace xerahs::media {
namespace {
using C = Container;
const QList<C> kMp4Mkv{C::Mp4, C::Mkv};
const QList<C> kWebm{C::WebM, C::Mkv};

QString canonical(const QString &path) {
  QFileInfo info(path);
  const auto c = info.canonicalFilePath();
  return c.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : c;
}
}  // namespace

QList<Codec> codecs() {
  // Order and ids are the catalog's (utility-catalog.json video-converter.codecs).
  return {
      {"X264", "H.264 (x264)", kMp4Mkv, {"x264enc", "openh264enc"}, false, "bitrate", 1, "h264parse"},
      {"X265", "H.265 / HEVC (x265)", kMp4Mkv, {"x265enc"}, false, "bitrate", 1, "h265parse"},
      {"H264Nvenc", "H.264 (NVIDIA NVENC)", kMp4Mkv, {"nvh264enc", "nvautogpuh264enc"}, true, "bitrate", 1, "h264parse"},
      {"HevcNvenc", "H.265 / HEVC (NVIDIA NVENC)", kMp4Mkv, {"nvh265enc", "nvautogpuh265enc"}, true, "bitrate", 1, "h265parse"},
      {"H264Amf", "H.264 (AMD AMF)", kMp4Mkv, {"amfh264enc"}, true, "bitrate", 1, "h264parse"},
      {"HevcAmf", "H.265 / HEVC (AMD AMF)", kMp4Mkv, {"amfh265enc"}, true, "bitrate", 1, "h265parse"},
      {"H264Qsv", "H.264 (Intel Quick Sync)", kMp4Mkv, {"qsvh264enc"}, true, "bitrate", 1, "h264parse"},
      {"HevcQsv", "H.265 / HEVC (Intel Quick Sync)", kMp4Mkv, {"qsvh265enc"}, true, "bitrate", 1, "h265parse"},
      {"Vp8", "VP8", kWebm, {"vp8enc"}, false, "target-bitrate", 1000, {}},
      {"Vp9", "VP9", kWebm, {"vp9enc"}, false, "target-bitrate", 1000, {}},
      {"Av1", "AV1", {C::Mp4, C::WebM, C::Mkv}, {"svtav1enc", "av1enc", "rav1enc"}, false, "target-bitrate", 1, "av1parse"},
      {"Xvid", "Xvid / MPEG-4 ASP", {C::Avi, C::Mkv}, {"avenc_mpeg4"}, false, "bitrate", 1000, {}},
      {"Gif", "Animated GIF", {C::Gif}, {"gifenc"}, false, {}, 1, {}},
      {"Webp", "Animated WebP", {C::WebP}, {"avenc_libwebp_anim"}, false, {}, 1, {}},
      {"Apng", "Animated PNG", {C::Apng}, {"avenc_apng"}, false, {}, 1, {}},
  };
}

const Codec *findCodec(const QString &id) {
  static const QList<Codec> all = codecs();
  for (const auto &c : all) if (c.id == id) return &c;
  return nullptr;
}

QList<Container> containers() { return {C::Mp4, C::Mkv, C::WebM, C::Avi, C::Gif, C::WebP, C::Apng}; }
QString containerId(Container c) {
  switch (c) { case C::Mp4: return "mp4"; case C::Mkv: return "mkv"; case C::WebM: return "webm"; case C::Avi: return "avi";
    case C::Gif: return "gif"; case C::WebP: return "webp"; case C::Apng: return "apng"; }
  return {};
}
QString containerLabel(Container c) {
  switch (c) { case C::Mp4: return "MP4"; case C::Mkv: return "Matroska (MKV)"; case C::WebM: return "WebM"; case C::Avi: return "AVI";
    case C::Gif: return "GIF"; case C::WebP: return "WebP"; case C::Apng: return "APNG"; }
  return {};
}
QString extensionFor(Container c) { return c == C::Apng ? QStringLiteral("png") : containerId(c); }
bool containerFromId(const QString &id, Container *out) {
  for (auto c : containers()) if (containerId(c) == id) { if (out) *out = c; return true; }
  return false;
}
QString muxerFor(Container c) {
  switch (c) { case C::Mp4: return "mp4mux"; case C::Mkv: return "matroskamux"; case C::WebM: return "webmmux"; case C::Avi: return "avimux";
    case C::WebP: return "avmux_webp"; case C::Apng: return "avmux_apng"; case C::Gif: return {}; }
  return {};
}
bool containerAllowsAudio(Container c) { return c == C::Mp4 || c == C::Mkv || c == C::WebM || c == C::Avi; }

QString audioEncoderFor(Container c, const QSet<QString> &available, QString *label) {
  QStringList candidates;
  if (c == C::Mp4) candidates = {"avenc_aac", "voaacenc", "fdkaacenc"};
  else if (c == C::Avi) candidates = {"lamemp3enc"};
  else if (c == C::Mkv || c == C::WebM) candidates = {"opusenc", "vorbisenc"};
  for (const auto &e : candidates) if (available.contains(e)) { if (label) *label = e; return e; }
  return {};
}

CodecAvailability codecAvailability(const Codec &codec, Container container, const ElementAvailability &port) {
  CodecAvailability a; a.codecId = codec.id;
  if (!codec.containers.contains(container)) { a.reason = QStringLiteral("%1 cannot be stored in %2.").arg(codec.label, containerLabel(container)); return a; }
  for (const auto &e : codec.encoders) if (port.hasElement(e)) { a.encoder = e; a.usable = true; return a; }
  a.reason = codec.hardware
      ? QStringLiteral("No usable %1 hardware encoder was found (needs GStreamer element %2 and a supported device).").arg(codec.label, codec.encoders.first())
      : QStringLiteral("Install the GStreamer plugin providing %1.").arg(codec.encoders.join(QStringLiteral(" or ")));
  return a;
}

QString errorCodeId(ErrorCode code) {
  switch (code) {
    case ErrorCode::None: return "VC_OK"; case ErrorCode::EmptyInput: return "VC_EMPTY_INPUT"; case ErrorCode::EmptyOutput: return "VC_EMPTY_OUTPUT";
    case ErrorCode::UnknownCodec: return "VC_UNKNOWN_CODEC"; case ErrorCode::UnknownContainer: return "VC_UNKNOWN_CONTAINER";
    case ErrorCode::IncompatibleContainer: return "VC_INCOMPATIBLE_CONTAINER"; case ErrorCode::InvalidBitrate: return "VC_INVALID_BITRATE";
    case ErrorCode::InputMissing: return "VC_INPUT_MISSING"; case ErrorCode::NoVideoStream: return "VC_NO_VIDEO_STREAM";
    case ErrorCode::OutputIsInput: return "VC_OUTPUT_IS_INPUT"; case ErrorCode::OutputExists: return "VC_OUTPUT_EXISTS";
    case ErrorCode::OutputFolderMissing: return "VC_OUTPUT_FOLDER_MISSING"; case ErrorCode::CodecUnavailable: return "VC_CODEC_UNAVAILABLE";
    case ErrorCode::MuxerUnavailable: return "VC_MUXER_UNAVAILABLE"; case ErrorCode::ParseFailed: return "VC_PARSE_FAILED";
    case ErrorCode::PipelineFailed: return "VC_PIPELINE_FAILED"; case ErrorCode::StartFailed: return "VC_START_FAILED";
    case ErrorCode::OutputWriteFailed: return "VC_OUTPUT_WRITE_FAILED";
  }
  return {};
}

ErrorCode checkOutputPath(const QString &input, const QString &output, bool overwrite) {
  if (output.trimmed().isEmpty()) return ErrorCode::EmptyOutput;
  if (QFileInfo(output).isDir()) return ErrorCode::OutputExists;
  if (canonical(input) == canonical(output)) return ErrorCode::OutputIsInput;   // EU-014: input is preserved
  if (!QFileInfo(QFileInfo(output).absolutePath()).isDir()) return ErrorCode::OutputFolderMissing;
  if (QFileInfo::exists(output) && !overwrite) return ErrorCode::OutputExists;
  return ErrorCode::None;
}

QString suggestOutputPath(const QString &input, Container container) {
  const QFileInfo in(input);
  const auto base = in.absolutePath() + '/' + in.completeBaseName() + QStringLiteral("-converted");
  const auto ext = '.' + extensionFor(container);
  auto candidate = base + ext;
  for (int n = 2; QFileInfo::exists(candidate) || canonical(candidate) == canonical(input); ++n) candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(ext);
  return candidate;
}

QString partialPathFor(const QString &output, const QString &token) {
  const QFileInfo out(output);
  return out.absolutePath() + QStringLiteral("/.%1.%2.partial").arg(out.fileName(), token);
}

namespace {
Plan fail(ErrorCode code, const QString &message) { Plan p; p.error = code; p.message = message; return p; }
}  // namespace

Plan planConversion(const Request &r, const InputInfo &info, const ElementAvailability &port) {
  if (r.inputPath.trimmed().isEmpty()) return fail(ErrorCode::EmptyInput, "Choose an input video.");
  const auto *codec = findCodec(r.codecId);
  if (!codec) return fail(ErrorCode::UnknownCodec, "That codec is not one of the converter's choices.");
  if (!containers().contains(r.container)) return fail(ErrorCode::UnknownContainer, "That container is not supported.");
  if (!codec->containers.contains(r.container))
    return fail(ErrorCode::IncompatibleContainer, QStringLiteral("%1 cannot be stored in %2.").arg(codec->label, containerLabel(r.container)));
  if (r.bitrateKbps < 0 || r.bitrateKbps > 200000) return fail(ErrorCode::InvalidBitrate, "Bitrate must be between 1 and 200000 kbit/s, or 0 for the default.");
  if (!QFileInfo(r.inputPath).isFile()) return fail(ErrorCode::InputMissing, "The input video could not be found.");
  if (info.known && !info.hasVideo) return fail(ErrorCode::NoVideoStream, "The input has no video stream to convert.");
  if (auto e = checkOutputPath(r.inputPath, r.outputPath, r.overwrite); e != ErrorCode::None) {
    switch (e) {
      case ErrorCode::EmptyOutput: return fail(e, "Choose an output file.");
      case ErrorCode::OutputIsInput: return fail(e, "The output must differ from the input; the input is never overwritten.");
      case ErrorCode::OutputFolderMissing: return fail(e, "The output folder does not exist.");
      default: return fail(e, "The output already exists. Choose another name or allow overwriting.");
    }
  }
  const auto availability = codecAvailability(*codec, r.container, port);
  if (!availability.usable) return fail(ErrorCode::CodecUnavailable, availability.reason);
  Plan plan; plan.encoder = availability.encoder; plan.muxer = muxerFor(r.container);
  if (!plan.muxer.isEmpty() && !port.hasElement(plan.muxer))
    return fail(ErrorCode::MuxerUnavailable, QStringLiteral("Install the GStreamer plugin providing %1 to write %2 files.").arg(plan.muxer, containerLabel(r.container)));

  // Audio: re-encoded only where the container allows it, else dropped with a diagnostic.
  bool audio = false;
  const bool inputMayHaveAudio = !info.known || info.hasAudio;
  if (r.includeAudio && inputMayHaveAudio) {
    if (!containerAllowsAudio(r.container)) plan.diagnostics << QStringLiteral("AUDIO_DROPPED_CONTAINER: %1 files cannot carry audio, so the audio track was dropped.").arg(containerLabel(r.container));
    else {
      QSet<QString> have;
      for (const auto &e : {"avenc_aac", "voaacenc", "fdkaacenc", "lamemp3enc", "opusenc", "vorbisenc"}) if (port.hasElement(e)) have.insert(e);
      plan.audioEncoder = audioEncoderFor(r.container, have);
      if (plan.audioEncoder.isEmpty()) plan.diagnostics << QStringLiteral("AUDIO_DROPPED_NO_ENCODER: no audio encoder for %1 is installed, so the audio track was dropped.").arg(containerLabel(r.container));
      else audio = true;
    }
  }
  const bool dropAudioBranch = !audio && info.known && info.hasAudio;   // keep decodebin's audio pad drained
  auto queue = QStringLiteral("queue");
  QString video = QStringLiteral("dec. ! video/x-raw ! %1 ! videoconvert").arg(queue);
  if (r.container == Container::Gif || r.container == Container::WebP || r.container == Container::Apng) video += " ! videorate ! video/x-raw,framerate=10/1";
  video += " ! " + plan.encoder;
  if (!codec->bitrateProperty.isEmpty() && r.bitrateKbps > 0) video += QStringLiteral(" %1=%2").arg(codec->bitrateProperty).arg(qint64(r.bitrateKbps) * codec->bitrateScale);
  if (!codec->parser.isEmpty() && !plan.muxer.isEmpty() && port.hasElement(codec->parser)) video += " ! " + codec->parser;
  video += plan.muxer.isEmpty() ? QStringLiteral(" ! sink.") : QStringLiteral(" ! %1 ! mux.").arg(queue);

  QStringList parts;
  parts << "filesrc name=src ! decodebin name=dec";
  parts << (plan.muxer.isEmpty() ? QStringLiteral("filesink name=sink") : QStringLiteral("%1 name=mux ! filesink name=sink").arg(plan.muxer));
  parts << video;
  if (audio) parts << QStringLiteral("dec. ! audio/x-raw ! %1 ! audioconvert ! audioresample ! %2 ! %1 ! mux.").arg(queue, plan.audioEncoder);
  else if (dropAudioBranch) parts << QStringLiteral("dec. ! audio/x-raw ! %1 ! fakesink sync=false async=false").arg(queue);
  plan.description = parts.join(' ');
  return plan;
}

}  // namespace xerahs::media
