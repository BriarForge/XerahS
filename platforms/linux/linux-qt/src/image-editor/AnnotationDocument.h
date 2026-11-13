// EDITOR-SESSION-001: the re-editable annotation document, its `.xann` v1
// reader and writer, and sidecar placement. Normative behaviour lives in
// product-contract/capabilities/EDITOR-SESSION-001/SPEC.md and
// annotation-document.schema.json; requirement IDs refer to SPEC.md.
#pragma once

#include "storage/Compatibility.h"

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

// EA-005: border style members follow the reference XANN format (camelCase
// on write: solid, dash, dot, dashDot, dashDotDot; read case-insensitively).
enum class BorderStyle { Solid, Dash, Dot, DashDot, DashDotDot };
QString borderStyleName(BorderStyle style);
std::optional<BorderStyle> parseBorderStyle(const QString &name);

// EA-006: arrow head style. Names and members follow the reference XANN
// format (`style`, camelCase: classic, modern, double, basic, line; read
// case-insensitively). Modern is a straight tapered arrow and cannot be curved.
enum class ArrowStyle { Classic, Modern, Double, Basic, Line };
QString arrowStyleName(ArrowStyle style);
std::optional<ArrowStyle> parseArrowStyle(const QString &name);

// EA-005/EA-006: drop shadow of a Rectangle, Ellipse, Line or Arrow. Field names and defaults
// follow the reference XANN format for interoperability with existing files
// (shadowEnabled, shadowColor, shadowBlurRadius, shadowOpacity, shadowOffsetX,
// shadowOffsetY). Ranges are this build's bounded validation (kMax* below).
struct ShapeShadow {
  bool enabled = false;
  double blurRadius = 4;
  double offsetX = 3;
  double offsetY = 3;
  double opacity = 1;  // 0..1, multiplies the colour alpha
  Argb color = 0x80000000;
  bool operator==(const ShapeShadow &o) const {
    return enabled == o.enabled && blurRadius == o.blurRadius && offsetX == o.offsetX && offsetY == o.offsetY &&
           opacity == o.opacity && color == o.color;
  }
  bool operator!=(const ShapeShadow &o) const { return !(*this == o); }
};
inline constexpr double kMaxCornerRadius = 10000;
inline constexpr double kMaxShadowBlur = 1000;
inline constexpr double kMaxShadowOffset = 10000;
// EA-006: the curve anchor is a fraction of the bounds; this bounds it.
inline constexpr double kMaxCurveFraction = 1000000;

// ES-003, ES-023, ES-024: qualification defaults.
struct RectangleStyle {
  Argb strokeColor = 0xFFFF0000;
  Argb fillColor = 0x00000000;
  double strokeWidth = 4;
  double rotationDegrees = 0;
  double opacity = 1;
  // EA-005. Absent in older documents: Solid, 0 and no shadow.
  BorderStyle borderStyle = BorderStyle::Solid;
  double cornerRadius = 0;  // whole pixels, Rectangle only; ignored for every other type
  ShapeShadow shadow;
  // EA-006. Absent in older documents: Classic. Arrow only; ignored elsewhere.
  ArrowStyle arrowStyle = ArrowStyle::Classic;
};
// True when `style` is not the legacy undecorated look for `type`.
bool hasDecoration(const QString &type, const RectangleStyle &style);
// Finite and within range (EA-020). Fields that do not apply to `type` are not checked.
bool validDecoration(const QString &type, const RectangleStyle &style);
bool sameDecoration(const RectangleStyle &a, const RectangleStyle &b);
// Types that carry the EA-005 border style and shadow: Rectangle, Ellipse
// and (EA-006) Line and Arrow. Corner radius is Rectangle only, arrow style Arrow only.
bool hasShapeDecoration(const QString &type);

// ESET-008: creation-time content defaults for new Text, SpeechBalloon and Step
// objects. They never edit existing objects. `requestedFamily` is the saved
// preference; `fontFamily` is the usable native family ("" = native default).
struct TextCreationDefaults {
  QString requestedFamily, fontFamily;
  bool familyUnavailable = false;
  double fontSize = 24;
  bool bold = true, italic = false;
  QString alignment = QStringLiteral("center");  // left, center, right
};
// EA-009: how a Step prints its number. Members, order and the persisted
// `stepType` spelling (camelCase, read case-insensitively) follow the reference
// StepType enum; the ESET-008 preference stores the PascalCase names.
enum class StepStyle { Numeric, UppercaseLetter, LowercaseLetter, UppercaseRoman, LowercaseRoman };
QString stepStyleName(StepStyle style);                    // numeric, uppercaseLetter, ...
std::optional<StepStyle> parseStepStyle(const QString &);  // either spelling, case-insensitive
// Values <= 0 print as plain decimal. Alphabetic is bijective base 26 (1 = A,
// 26 = Z, 27 = AA); Roman is standard subtractive with M repeated above 3999.
QString formatStepValue(qint64 value, StepStyle style);

// ESET-006: creation defaults of the emphasis and redaction tools. Field names and
// defaults follow the reference ImageEditorOptions (BlurStrength, PixelateStrength,
// MagnifierStrength/Ellipse, SpotlightStrength/Blur/Ellipse). A strength of 0 keeps the
// renderer's own fallback; the saved ranges live in EditorPreferences (preferenceNumericRange).
struct EmphasisCreationDefaults {
  double blurStrength = 30, pixelateStrength = 20, magnifyStrength = 2, spotlightStrength = 30, spotlightBlur = 0;
  bool magnifyEllipse = false, spotlightEllipse = false;
};
// EA-010: the reference CursorType members, in reference order and spelling (PascalCase). The
// persisted `cursorType` spelling is camelCase (first letter lowered, as the reference XANN policy
// does) and is read case-insensitively.
const QStringList &cursorTypeNames();
std::optional<QString> parseCursorTypeName(const QString &name);  // canonical PascalCase, or nullopt
QString cursorTypePersistedName(const QString &canonical);
// EA-010: Emoji metadata, reference `unicodeSequence` (space separated hex code points) and
// `displayName`. Both are optional; an empty value is "not recorded".
inline constexpr int kMaxEmojiCodePoints = 16;
inline constexpr int kMaxEmojiDisplayName = 256;
bool validUnicodeSequence(const QString &sequence);
bool validEmojiDisplayName(const QString &name);
QString unicodeSequenceOf(const QString &emoji);

struct ContentCreationDefaults {
  EmphasisCreationDefaults emphasis;
  QString requestedCursorType = QStringLiteral("Default");  // saved CursorType (reference name)
  bool cursorTypeInert = false;                             // saved type is kept but this build cannot draw it
  QString cursorType = QStringLiteral("Default");           // the usable type new Cursor objects record
  TextCreationDefaults text, balloon;
  double stepFontSize = 0;  // 0 sizes the number from the step circle
  QString requestedStepStyle = QStringLiteral("Numeric");
  bool stepStyleInert = false;  // saved style is preserved but unknown to this build
  StepStyle stepStyle = StepStyle::Numeric;  // the usable style new Steps are created with
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
// EA-010/EA-016: header-only check of untrusted PNG bytes, before any decode: the
// signature, a leading IHDR with non-zero dimensions within ES-028 and a decoded
// size the renderer accepts (at most 64M pixels), and the closing IEND chunk.
inline constexpr qint64 kMaxAssetPixels = 64LL * 1024 * 1024;
bool validEmbeddedPng(const QByteArray &png);
// Types whose `strength` is user editable (blur, pixelate, magnify, spotlight).
bool hasShapeStrength(const QString &type);
// EA-011: Highlight, Blur, Pixelate, Magnify and Spotlight may declare an
// elliptical region (persisted `isEllipse`, default false = rectangle, as in
// the reference XANN format). Spotlight alone has `blurAmount`, the blur radius
// applied to the dimmed area outside its region (reference `blurAmount`, default 0).
bool hasShapeEllipse(const QString &type);
bool hasShapeBlurAmount(const QString &type);
inline constexpr double kMaxBlurAmount = 1000;

// Type-specific content. Path points are fractions of the bounds, so move,
// resize and rotation of the bounds carry the path with them (EA-006/007).
inline constexpr int kMaxPathPoints = 100000;
inline constexpr int kMaxTextLength = 100000;
inline constexpr qint64 kMaxStepValue = 1000000000;
struct ShapeContent {
  QString type = shape::rectangle;
  // speech-balloon and (EA-009) step: points[0] is the tail tip, none = no tail. line and arrow: points[0] is
  // the start and points[1] the end (direction); an optional points[2] is the
  // EA-006 curve anchor, the point the quadratic curve passes through at its
  // middle. All are fractions of the bounds, so move, resize, flip and any
  // shared transform carry the curve with the endpoints.
  std::vector<PointF> points;
  QString text;           // text: content; step: unused
  double fontSize = 0;    // text and step; 0 uses the type default
  qint64 stepValue = 0;   // EA-009: the displayed step number
  StepStyle stepStyle = StepStyle::Numeric;  // EA-009: persisted as `stepType`, omitted when numeric
  double strength = 0;    // blur radius or pixelate cell; 0 uses the default
  bool isEllipse = false;   // EA-011: region effects only; false = rectangular region
  double blurAmount = 0;    // EA-011: Spotlight only; 0 = no blur of the dimmed area
  QString imageKey;       // asset types: key into AnnotationDocument::embeddedImages
  // EA-010 asset metadata, optional and camelCase in the document as in the reference XANN format:
  // Emoji `unicodeSequence` and `displayName`, Cursor `cursorType` (canonical PascalCase here).
  QString unicodeSequence, displayName, cursorType;
  QString textAlignment;  // empty preserves legacy rendering; left, center, right
  std::optional<bool> textBold, textItalic; // absent retains the native font style of legacy files
  std::optional<Argb> textColor; // absent retains legacy stroke/automatic contrast
  QString fontFamily; // empty uses the native font; missing names remain preserved
  std::optional<Argb> textFillColor; // absent retains legacy fill; explicit zero remains transparent
  std::optional<Argb> balloonBorderColor; // speech balloon only; absent retains legacy stroke color
  // EA-008, Text only: colour of the outline drawn around the glyphs (reference OutlinedTextControl:
  // stroke colour and thickness, painted behind the text colour). Absent = no outline (legacy);
  // zero alpha or a zero stroke width also draws none.
  std::optional<Argb> textBorderColor;
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

// EA-008: shared alignment and finite mapped-tail validation.
bool validTextAlignment(const QString &alignment);
bool validFontFamily(const QString &family);
bool validStepValue(qint64 value);
bool validBalloonTail(const RectangleAnnotation &shape, const std::vector<PointF> &points);

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

// ES-029 public diagnostics form a closed set; specific rejection reasons are separate.
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
constexpr qint64 kMaxCompressedXannBytes = 268435456;
constexpr qint64 kMaxXannExpansionRatio = 1024;

struct ParseResult {
  std::optional<AnnotationDocument> document;
  std::optional<QString> error;
  compatibility::ImportReport report;
  std::optional<QString> reason = std::nullopt;  // value-free reader code, never document content
};

// ES-015, ES-017, ES-028, ES-029: validates a decoded payload. Image
// dimensions are read from PNG headers; no pixels are allocated.
ParseResult parseDocument(const QJsonObject &payload);
QJsonObject serializeDocument(const AnnotationDocument &document);
struct AnnotationListResult {
  std::optional<std::vector<Annotation>> annotations;
  QString diagnostic;
  compatibility::ImportReport report;
  std::optional<QString> reason = std::nullopt;
};
// The document and object-transfer boundaries use the same field validation
// and canonical serialization; unknown types remain inert document data.
AnnotationListResult parseAnnotationList(const QJsonArray &);
QJsonArray serializeAnnotationList(const std::vector<Annotation> &);

// ES-013: gzip-wrapped UTF-8 JSON. readXann enforces the decompressed limit
// while inflating.
// Map a trusted JSON-boundary failure code to ES-029, retaining its value-free reason.
ParseResult documentJsonFailure(const QString &code);
ParseResult readXann(const QByteArray &gzip, qint64 maxJsonBytes = kMaxDecompressedJsonBytes);
std::optional<QByteArray> writeXann(const AnnotationDocument &document, compatibility::ExportReport *report = nullptr);

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
  compatibility::ExportReport report;
};

SidecarSaveResult saveSidecar(SidecarStore &store, const QString &rasterPath, const AnnotationDocument &document,
                              bool sidecarIntent);

}  // namespace xerahs::editor
