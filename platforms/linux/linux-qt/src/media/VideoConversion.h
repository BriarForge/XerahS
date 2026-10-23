// EDITOR-UTILITIES-001 / EU-012..EU-014: Video Converter decision model. Qt Core only; GStreamer is
// reached through ElementAvailability (a port) and the ConversionRunner interface, so every rule here
// is testable without any media framework. Paths never enter the pipeline description (EU-013, EU-015):
// the runner sets filesrc/filesink "location" as structured properties.
#pragma once

#include <QList>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QStringList>

namespace xerahs::media {

enum class Container { Mp4, Mkv, WebM, Avi, Gif, WebP, Apng };

// EU-012: the 15 codec choices of the utility catalog, in catalog order.
struct Codec {
  QString id, label;
  QList<Container> containers;     // containers this codec may be muxed into
  QStringList encoders;            // GStreamer encoder candidates, first usable wins (deterministic)
  bool hardware = false;
  QString bitrateProperty;         // empty when bitrate is not meaningful (animated image formats)
  int bitrateScale = 1;            // property units per kbit/s
  QString parser;                  // optional parser element between encoder and muxer
};

QList<Codec> codecs();
const Codec *findCodec(const QString &id);
QList<Container> containers();
QString containerId(Container c);       // "mp4"
QString containerLabel(Container c);    // "MP4"
QString extensionFor(Container c);
bool containerFromId(const QString &id, Container *out);
QString muxerFor(Container c);          // empty: the encoder writes the file itself (GIF)
bool containerAllowsAudio(Container c);
QString audioEncoderFor(Container c, const QSet<QString> &available, QString *label = nullptr);

// EU-013: reports which GStreamer elements exist. Real implementation lives in the runner library.
class ElementAvailability {
public:
  virtual ~ElementAvailability() = default;
  virtual bool hasElement(const QString &name) const = 0;
};
class NoElements final : public ElementAvailability { public: bool hasElement(const QString &) const override { return false; } };
class SetAvailability final : public ElementAvailability {
public:
  explicit SetAvailability(QSet<QString> names = {}) : m_names(std::move(names)) {}
  bool hasElement(const QString &name) const override { return m_names.contains(name); }
  QSet<QString> names() const { return m_names; }
private:
  QSet<QString> m_names;
};

struct CodecAvailability {
  QString codecId, encoder, reason;   // reason is set when !usable
  bool usable = false;
};
// Never substitutes another codec: unavailable codecs carry an actionable reason.
CodecAvailability codecAvailability(const Codec &codec, Container container, const ElementAvailability &port);

enum class ErrorCode {
  None, EmptyInput, EmptyOutput, UnknownCodec, UnknownContainer, IncompatibleContainer, InvalidBitrate,
  InputMissing, NoVideoStream, OutputIsInput, OutputExists, OutputFolderMissing, CodecUnavailable,
  MuxerUnavailable, ParseFailed, PipelineFailed, StartFailed, OutputWriteFailed
};
QString errorCodeId(ErrorCode code);    // stable, e.g. "VC_OUTPUT_EXISTS"

struct Request {
  QString inputPath, outputPath, codecId;
  Container container = Container::Mp4;
  int bitrateKbps = 0;                  // 0: encoder default
  bool includeAudio = true;
  bool overwrite = false;               // explicit opt-in to replace an existing output (never the input)
};
struct InputInfo {
  bool known = false, hasVideo = true, hasAudio = false;
  qint64 durationMs = 0;
};

struct Plan {
  ErrorCode error = ErrorCode::None;
  QString message;                      // actionable, no full paths
  QString encoder, muxer, audioEncoder, audioLabel;
  QString description;                  // gst_parse_launch text, path-free; names: src, dec, mux, sink
  QStringList diagnostics;              // e.g. audio dropped
  bool ok() const { return error == ErrorCode::None; }
};

// EU-014: validates container/codec/input compatibility and the output rules, then builds the plan.
Plan planConversion(const Request &request, const InputInfo &info, const ElementAvailability &port);
// Output rules alone (no codec): never the input, not an existing file unless overwrite.
ErrorCode checkOutputPath(const QString &input, const QString &output, bool overwrite);
// A non-conflicting suggestion next to the input: "<name>-converted.<ext>", then " (2)", ...
QString suggestOutputPath(const QString &input, Container container);
// Hidden sibling of the final output the runner writes to, renamed on success (EU-013 atomic output).
QString partialPathFor(const QString &output, const QString &token);

// Result of one run (the runner contract lives in app/ConversionRunner.h).
enum class Outcome { Success, Cancelled, Failed };
struct ConversionResult {
  Outcome outcome = Outcome::Failed;
  ErrorCode error = ErrorCode::None;
  QString message, outputPath;
  QStringList diagnostics;
};
}  // namespace xerahs::media
Q_DECLARE_METATYPE(xerahs::media::ConversionResult)
