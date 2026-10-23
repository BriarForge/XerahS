// EDITOR-SESSION-001: the re-editable annotation document, its `.xann` v1
// reader and writer, and sidecar placement. Normative behaviour lives in
// product-contract/capabilities/EDITOR-SESSION-001/SPEC.md and
// annotation-document.schema.json; requirement IDs refer to SPEC.md.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace xerahs::editor {

// ES-023: #AARRGGBB, non-premultiplied 8-bit sRGB.
using Argb = quint32;
std::optional<Argb> parseColor(const QString &text);
QString formatColor(Argb color);  // uppercase

struct PointF {
  double x = 0;
  double y = 0;
  bool operator==(const PointF &o) const { return x == o.x && y == o.y; }
};

// ES-003, ES-023, ES-024: qualification defaults.
struct RectangleStyle {
  Argb strokeColor = 0xFFFF0000;
  Argb fillColor = 0x00000000;
  double strokeWidth = 4;
  double rotationDegrees = 0;
  double opacity = 1;
};

// EA-001: the document types of the box-bounded annotation tools. Every
// type shares bounds, style, selection, move, resize, rotation and history.
namespace shape {
inline const QString rectangle = QStringLiteral("rectangle");
inline const QString ellipse = QStringLiteral("ellipse");
inline const QString line = QStringLiteral("line");
inline const QString arrow = QStringLiteral("arrow");
inline const QString freehand = QStringLiteral("freehand");
inline const QString text = QStringLiteral("text");
inline const QString step = QStringLiteral("step");
inline const QString highlight = QStringLiteral("highlight");
inline const QString blur = QStringLiteral("blur");
inline const QString pixelate = QStringLiteral("pixelate");
inline const QString speechBalloon = QStringLiteral("speech-balloon");
inline const QString magnify = QStringLiteral("magnify");
inline const QString spotlight = QStringLiteral("spotlight");
inline const QString smartEraser = QStringLiteral("smart-eraser");
inline const QString image = QStringLiteral("image");
inline const QString emoji = QStringLiteral("emoji");
inline const QString cursor = QStringLiteral("cursor");
}  // namespace shape
QStringList shapeTypes();
// Path types draw through `points`; region types sample source pixels.
bool isPathShape(const QString &type);
bool isTextShape(const QString &type);
// EA-010: Image, Emoji and Cursor draw an embedded PNG named by imageKey.
bool isAssetShape(const QString &type);
// Types whose `strength` is user editable (blur, pixelate, magnify, spotlight).
bool hasShapeStrength(const QString &type);

// Type-specific content. Path points are fractions of the bounds, so move,
// resize and rotation of the bounds carry the path with them (EA-006/007).
struct ShapeContent {
  QString type = shape::rectangle;
  std::vector<PointF> points;  // speech-balloon: points[0] is the tail tip
  QString text;           // text: content; step: unused
  double fontSize = 0;    // text and step; 0 uses the type default
  qint64 stepValue = 0;   // EA-009: the displayed step number
  double strength = 0;    // blur radius or pixelate cell; 0 uses the default
  QString imageKey;       // asset types: key into AnnotationDocument::embeddedImages
};

struct RectangleAnnotation {
  QUuid id;
  PointF start;  // normalized: start is the top-left corner
  PointF end;
  RectangleStyle style;
  bool visible = true;
  QJsonObject extra;  // ES-015: unknown properties kept for round-trip
  ShapeContent content;
  const QString &type() const { return content.type; }

  double left() const { return start.x; }
  double top() const { return start.y; }
  double right() const { return end.x; }
  double bottom() const { return end.y; }
};

// ES-015: an annotation type this build does not know, kept verbatim and shown
// as an unsupported placeholder.
struct UnsupportedAnnotation {
  QUuid id;
  QJsonObject raw;
};

// Every known shape type uses the same box-bounded record.
using ShapeAnnotation = RectangleAnnotation;
using Annotation = std::variant<RectangleAnnotation, UnsupportedAnnotation>;
using EmbeddedImages = std::vector<std::pair<QString, QByteArray>>;
QUuid annotationId(const Annotation &annotation);
// Hidden or explicitly locked compatible objects cannot be directly edited.
// An opaque/non-boolean lock field is protected rather than interpreted.
bool annotationEditable(const Annotation &annotation);

struct AnnotationDocument {
  int version = 1;
  QString imagePath;
  QString imageHash;  // "" or "sha256:<hex>"
  qint64 canvasWidth = 0;
  qint64 canvasHeight = 0;
  QString createdAt;
  QString modifiedAt;
  QByteArray sourceImagePng;               // decoded from base64
  EmbeddedImages embeddedImages;  // key -> PNG bytes
  std::vector<Annotation> annotations;     // document (z) order
  QJsonObject extra;                       // ES-015: unknown top-level properties
};

// ES-029 diagnostics.
namespace diagnostic {
inline const QString sourceUnsupported = QStringLiteral("source-unsupported");
inline const QString sourceCorrupt = QStringLiteral("source-corrupt");
inline const QString sourceEmpty = QStringLiteral("source-empty");
inline const QString sourceTooLarge = QStringLiteral("source-too-large");
inline const QString documentVersionUnsupported = QStringLiteral("document-version-unsupported");
inline const QString documentInvalid = QStringLiteral("document-invalid");
inline const QString documentTooLarge = QStringLiteral("document-too-large");
inline const QString rasterSaveFailed = QStringLiteral("raster-save-failed");
inline const QString sidecarSaveFailed = QStringLiteral("sidecar-save-failed");
}  // namespace diagnostic

// ES-028 limits.
constexpr qint64 kMaxDimension = 100000;
constexpr qint64 kMaxPixels = 268435456;
constexpr qint64 kMaxDecompressedJsonBytes = 1073741824;

struct ParseResult {
  std::optional<AnnotationDocument> document;
  std::optional<QString> error;
};

// ES-015, ES-017, ES-028, ES-029: validates a decoded payload. Image
// dimensions are read from PNG headers; no pixels are allocated.
ParseResult parseDocument(const QJsonObject &payload);
QJsonObject serializeDocument(const AnnotationDocument &document);
struct AnnotationListResult {
  std::optional<std::vector<Annotation>> annotations;
  QString diagnostic;
};
// The document and object-transfer boundaries use the same field validation
// and canonical serialization; unknown types remain inert document data.
AnnotationListResult parseAnnotationList(const QJsonArray &);
QJsonArray serializeAnnotationList(const std::vector<Annotation> &);

// ES-013: gzip-wrapped UTF-8 JSON. readXann enforces the decompressed limit
// while inflating.
ParseResult readXann(const QByteArray &gzip, qint64 maxJsonBytes = kMaxDecompressedJsonBytes);
std::optional<QByteArray> writeXann(const AnnotationDocument &document);

// ES-030.
struct SidecarPaths {
  QString writePath;
  std::optional<QString> readPath;
};
SidecarPaths sidecarPaths(const QString &rasterPath, const std::function<bool(const QString &)> &exists);

// ES-014: the sidecar side of a save. The raster is never touched here.
class SidecarStore {
public:
  virtual ~SidecarStore() = default;
  virtual bool exists(const QString &path) = 0;
  virtual bool write(const QString &path, const QByteArray &bytes) = 0;  // atomic replace
  virtual bool remove(const QString &path) = 0;
};

struct SidecarSaveResult {
  bool ok = true;
  std::optional<QString> sidecarPath;  // written sidecar, or nullopt
  std::optional<QString> diagnostic;   // sidecar-save-failed
};

SidecarSaveResult saveSidecar(SidecarStore &store, const QString &rasterPath, const AnnotationDocument &document,
                              bool sidecarIntent);

}  // namespace xerahs::editor
