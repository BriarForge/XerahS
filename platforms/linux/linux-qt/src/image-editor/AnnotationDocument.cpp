#include "image-editor/AnnotationDocument.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <zlib.h>

#include <cmath>

namespace xerahs::editor {

namespace {
std::optional<QString> baselineSidecarPath(const QString &rasterPath) {
  const qsizetype slash = rasterPath.lastIndexOf(QLatin1Char('/'));
  const qsizetype dot = rasterPath.lastIndexOf(QLatin1Char('.'));
  return dot > slash + 1 ? std::optional<QString>(rasterPath.left(dot) + QStringLiteral(".xann")) : std::nullopt;
}

const QStringList kKnownDocumentKeys = {
    QStringLiteral("version"),      QStringLiteral("imagePath"),  QStringLiteral("imageHash"),
    QStringLiteral("canvasWidth"),  QStringLiteral("canvasHeight"), QStringLiteral("createdAt"),
    QStringLiteral("modifiedAt"),   QStringLiteral("sourceImagePngBase64"), QStringLiteral("embeddedImages"),
    QStringLiteral("annotations"),  QStringLiteral("extensions")};

const QStringList kKnownRectangleKeys = {
    QStringLiteral("id"),          QStringLiteral("type"),        QStringLiteral("start"),
    QStringLiteral("end"),         QStringLiteral("strokeColor"), QStringLiteral("fillColor"),
    QStringLiteral("strokeWidth"), QStringLiteral("rotationDegrees"), QStringLiteral("zIndex"),
    QStringLiteral("visible"),     QStringLiteral("opacity"),     QStringLiteral("points"),
    QStringLiteral("text"),        QStringLiteral("fontSize"),    QStringLiteral("stepValue"),
    QStringLiteral("strength"),    QStringLiteral("imageKey")};


bool isInteger(const QJsonValue &value) {
  if (!value.isDouble()) return false;
  const double d = value.toDouble();
  return std::isfinite(d) && std::floor(d) == d;
}

bool isFiniteNumber(const QJsonValue &value) { return value.isDouble() && std::isfinite(value.toDouble()); }

// Accumulates ES-029 failures so the highest-priority one is reported.
struct Findings {
  bool tooLarge = false;
  bool invalid = false;
};

void checkDimensions(qint64 width, qint64 height, Findings &findings) {
  if (width < 1 || height < 1) {
    findings.invalid = true;
    return;
  }
  if (width > kMaxDimension || height > kMaxDimension || width * height > kMaxPixels) findings.tooLarge = true;
}

// ES-017: strict base64, then the PNG signature and IHDR dimensions only.
std::optional<QByteArray> decodePng(const QJsonValue &value, Findings &findings) {
  if (!value.isString()) {
    findings.invalid = true;
    return std::nullopt;
  }
  const auto decoded = QByteArray::fromBase64Encoding(value.toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
  static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
  if (!decoded || decoded.decoded.size() < 24 || !decoded.decoded.startsWith(signature) ||
      decoded.decoded.mid(12, 4) != QByteArrayLiteral("IHDR")) {
    findings.invalid = true;
    return std::nullopt;
  }
  const auto be32 = [&](int offset) {
    const auto *p = reinterpret_cast<const unsigned char *>(decoded.decoded.constData() + offset);
    return (qint64(p[0]) << 24) | (qint64(p[1]) << 16) | (qint64(p[2]) << 8) | qint64(p[3]);
  };
  checkDimensions(be32(16), be32(20), findings);
  return decoded.decoded;
}

std::optional<PointF> parsePoint(const QJsonValue &value) {
  if (!value.isObject()) return std::nullopt;
  const QJsonObject o = value.toObject();
  // Schema: a point has exactly x and y.
  if (o.size() != 2 || !isFiniteNumber(o.value(u"x")) || !isFiniteNumber(o.value(u"y"))) return std::nullopt;
  return PointF{o.value(u"x").toDouble(), o.value(u"y").toDouble()};
}

bool isUuid(const QString &text) {
  static const QRegularExpression pattern(
      QStringLiteral("^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
  return pattern.match(text).hasMatch();
}

std::optional<Annotation> parseAnnotation(const QJsonValue &value, Findings &findings) {
  if (!value.isObject()) {
    findings.invalid = true;
    return std::nullopt;
  }
  const QJsonObject o = value.toObject();
  const QString id = o.value(u"id").toString();
  if (!o.value(u"id").isString() || !isUuid(id) || !o.value(u"type").isString()) {
    findings.invalid = true;
    return std::nullopt;
  }
  if ((o.contains(u"zIndex") && !isInteger(o.value(u"zIndex"))) ||
      (o.contains(u"visible") && !o.value(u"visible").isBool())) {
    findings.invalid = true;
    return std::nullopt;
  }
  if (o.contains(u"opacity")) {
    const QJsonValue opacity = o.value(u"opacity");
    if (!isFiniteNumber(opacity) || opacity.toDouble() < 0 || opacity.toDouble() > 1) {
      findings.invalid = true;
      return std::nullopt;
    }
  }
  const QString type = o.value(u"type").toString();
  if (!shapeTypes().contains(type)) return Annotation(UnsupportedAnnotation{QUuid(id), o});
  ShapeContent content;
  content.type = type;
  if (type != shape::rectangle) {
    const auto optionalNumber = [&](const char16_t *key, double low, double high, double &out) {
      if (!o.contains(key)) return true;
      const QJsonValue v = o.value(key);
      if (!isFiniteNumber(v) || v.toDouble() < low || v.toDouble() > high) return false;
      out = v.toDouble(); return true;
    };
    if ((type == shape::text || type == shape::speechBalloon) && o.contains(u"textAlignment")) {
      const auto alignment = o.value(u"textAlignment");
      if (!alignment.isString() || !validTextAlignment(alignment.toString())) { findings.invalid = true; return std::nullopt; }
      content.textAlignment = alignment.toString();
    }
    double stepValue = 0;
    bool ok = optionalNumber(u"fontSize", 0, 2000, content.fontSize) && optionalNumber(u"strength", 0, 1000, content.strength) &&
              optionalNumber(u"stepValue", 0, 1e9, stepValue) && (!o.contains(u"stepValue") || isInteger(o.value(u"stepValue")));
    content.stepValue = qint64(stepValue);
    if (o.contains(u"text")) { ok = ok && o.value(u"text").isString() && o.value(u"text").toString().size() <= kMaxTextLength; content.text = o.value(u"text").toString(); }
    if (o.contains(u"points")) {
      const QJsonArray points = o.value(u"points").toArray();
      ok = ok && o.value(u"points").isArray() && points.size() <= kMaxPathPoints;
      for (const auto &point : points) {
        const auto p = parsePoint(point);
        if (!p) { ok = false; break; }
        content.points.push_back(*p);
      }
    }
    if (isPathShape(type) && content.points.size() < 2) ok = false;
    if (isAssetShape(type)) {
      ok = ok && o.value(u"imageKey").isString() && !o.value(u"imageKey").toString().isEmpty() && o.value(u"imageKey").toString().size() <= 256;
      content.imageKey = o.value(u"imageKey").toString();
    }
    if (!ok) {
      findings.invalid = true;
      return std::nullopt;
    }
  }

  const auto start = parsePoint(o.value(u"start"));
  const auto end = parsePoint(o.value(u"end"));
  const auto stroke = parseColor(o.value(u"strokeColor").toString());
  const auto fill = parseColor(o.value(u"fillColor").toString());
  const QJsonValue width = o.value(u"strokeWidth");
  const QJsonValue rotation = o.value(u"rotationDegrees");
  if (!start || !end || !stroke || !fill || !isFiniteNumber(width) || width.toDouble() <= 0 ||
      width.toDouble() > 10000 || !isFiniteNumber(rotation)) {
    findings.invalid = true;
    return std::nullopt;
  }
  RectangleAnnotation rectangle;
  rectangle.id = QUuid(id);
  // ES-024: bounds are the normalized start and end points.
  rectangle.start = {std::min(start->x, end->x), std::min(start->y, end->y)};
  rectangle.end = {std::max(start->x, end->x), std::max(start->y, end->y)};
  rectangle.style = RectangleStyle{*stroke, *fill, width.toDouble(), rotation.toDouble(),
                                   o.value(u"opacity").toDouble(1.0)};
  rectangle.visible = o.value(u"visible").toBool(true);
  rectangle.content = std::move(content);
  for (auto it = o.begin(); it != o.end(); ++it) {
    if (!kKnownRectangleKeys.contains(it.key())) rectangle.extra.insert(it.key(), it.value());
  }
  if (type == shape::text || type == shape::speechBalloon) rectangle.extra.remove(QStringLiteral("textAlignment"));
  return Annotation(rectangle);
}

QJsonObject pointJson(const PointF &p) { return QJsonObject{{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}}; }

}  // namespace

QStringList shapeTypes() {
  return {shape::rectangle, shape::ellipse, shape::line, shape::arrow, shape::freehand, shape::text,
          shape::step, shape::highlight, shape::blur, shape::pixelate, shape::speechBalloon, shape::magnify,
          shape::spotlight, shape::smartEraser, shape::image, shape::emoji, shape::cursor};
}
bool isAssetShape(const QString &type) { return type == shape::image || type == shape::emoji || type == shape::cursor; }
bool hasShapeStrength(const QString &type) {
  return type == shape::blur || type == shape::pixelate || type == shape::magnify || type == shape::spotlight;
}
bool isPathShape(const QString &type) { return type == shape::line || type == shape::arrow || type == shape::freehand; }
bool isTextShape(const QString &type) { return type == shape::text || type == shape::step || type == shape::speechBalloon; }

std::optional<Argb> parseColor(const QString &text) {
  static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{8}$"));
  if (!pattern.match(text).hasMatch()) return std::nullopt;
  bool ok = false;
  const Argb value = text.mid(1).toUInt(&ok, 16);
  if (!ok) return std::nullopt;
  return value;
}

QString formatColor(Argb color) {
  return QStringLiteral("#%1").arg(color, 8, 16, QLatin1Char('0')).toUpper();
}

QUuid annotationId(const Annotation &annotation) {
  return std::visit([](const auto &a) { return a.id; }, annotation);
}
bool annotationEditable(const Annotation &annotation) {
  const auto *r = std::get_if<RectangleAnnotation>(&annotation);
  const auto &extra = r ? r->extra : std::get<UnsupportedAnnotation>(annotation).raw;
  const bool visible = r ? r->visible : !extra.contains("visible") || (extra.value("visible").isBool() && extra.value("visible").toBool());
  return visible && (!extra.contains("locked") || (extra.value("locked").isBool() && !extra.value("locked").toBool()));
}

ParseResult parseDocument(const QJsonObject &payload) {
  ParseResult result;
  // ES-029 priority 1: an unsupported major version fails before anything else
  // is examined, and nothing is rewritten.
  const QJsonValue version = payload.value(u"version");
  if (isInteger(version) && version.toDouble() > 1) {
    result.error = diagnostic::documentVersionUnsupported;
    return result;
  }

  Findings findings;
  if (!isInteger(version) || version.toDouble() != 1) findings.invalid = true;

  AnnotationDocument document;
  for (const QString &key : kKnownDocumentKeys) {
    if (key != QStringLiteral("extensions") && !payload.contains(key)) findings.invalid = true;
  }
  if (!payload.value(u"imagePath").isString() || !payload.value(u"createdAt").isString() ||
      !payload.value(u"modifiedAt").isString() || !payload.value(u"imageHash").isString()) {
    findings.invalid = true;
  }
  document.imagePath = payload.value(u"imagePath").toString();
  document.imageHash = payload.value(u"imageHash").toString();
  static const QRegularExpression hashPattern(QStringLiteral("^(|sha256:[0-9a-f]{64})$"));
  if (!hashPattern.match(document.imageHash).hasMatch()) findings.invalid = true;
  document.createdAt = payload.value(u"createdAt").toString();
  document.modifiedAt = payload.value(u"modifiedAt").toString();
  if (!QDateTime::fromString(document.createdAt, Qt::ISODate).isValid() ||
      !QDateTime::fromString(document.modifiedAt, Qt::ISODate).isValid()) {
    findings.invalid = true;
  }

  // ES-028: canvas limits before any pixel allocation.
  const QJsonValue width = payload.value(u"canvasWidth");
  const QJsonValue height = payload.value(u"canvasHeight");
  if (!isInteger(width) || !isInteger(height)) {
    findings.invalid = true;
  } else {
    document.canvasWidth = static_cast<qint64>(width.toDouble());
    document.canvasHeight = static_cast<qint64>(height.toDouble());
    checkDimensions(document.canvasWidth, document.canvasHeight, findings);
  }

  if (auto png = decodePng(payload.value(u"sourceImagePngBase64"), findings)) document.sourceImagePng = *png;

  const QJsonValue embedded = payload.value(u"embeddedImages");
  if (!embedded.isObject()) {
    findings.invalid = true;
  } else {
    const QJsonObject images = embedded.toObject();
    for (auto it = images.begin(); it != images.end(); ++it) {
      if (auto png = decodePng(it.value(), findings)) document.embeddedImages.emplace_back(it.key(), *png);
    }
  }

  if (payload.contains(u"extensions") && !payload.value(u"extensions").isObject()) findings.invalid = true;

  const QJsonValue annotations = payload.value(u"annotations");
  if (!annotations.isArray()) {
    findings.invalid = true;
  } else {
    auto parsed = parseAnnotationList(annotations.toArray());
    if (parsed.annotations) document.annotations = std::move(*parsed.annotations);
    else findings.invalid = true;
  }

  for (auto it = payload.begin(); it != payload.end(); ++it) {
    if (!kKnownDocumentKeys.contains(it.key()) || it.key() == QStringLiteral("extensions")) {
      document.extra.insert(it.key(), it.value());
    }
  }

  // ES-029 priority 2 and 3.
  if (findings.tooLarge) result.error = diagnostic::documentTooLarge;
  else if (findings.invalid) result.error = diagnostic::documentInvalid;
  else result.document = std::move(document);
  return result;
}

QJsonObject serializeDocument(const AnnotationDocument &document) {
  QJsonObject root = document.extra;
  root.insert(QStringLiteral("version"), document.version);
  root.insert(QStringLiteral("imagePath"), document.imagePath);
  root.insert(QStringLiteral("imageHash"), document.imageHash);
  root.insert(QStringLiteral("canvasWidth"), document.canvasWidth);
  root.insert(QStringLiteral("canvasHeight"), document.canvasHeight);
  root.insert(QStringLiteral("createdAt"), document.createdAt);
  root.insert(QStringLiteral("modifiedAt"), document.modifiedAt);
  root.insert(QStringLiteral("sourceImagePngBase64"), QString::fromLatin1(document.sourceImagePng.toBase64()));
  QJsonObject embedded;
  for (const auto &[key, png] : document.embeddedImages) embedded.insert(key, QString::fromLatin1(png.toBase64()));
  root.insert(QStringLiteral("embeddedImages"), embedded);

  root.insert(QStringLiteral("annotations"), serializeAnnotationList(document.annotations));
  return root;
}

AnnotationListResult parseAnnotationList(const QJsonArray &items) {
  Findings findings; std::vector<Annotation> result; QSet<QUuid> ids;
  for (const auto &item : items) {
    auto annotation = parseAnnotation(item, findings); if (!annotation) continue;
    const auto id = annotationId(*annotation); if (ids.contains(id)) findings.invalid = true;
    ids.insert(id); result.push_back(std::move(*annotation));
  }
  if (findings.invalid) return {std::nullopt, diagnostic::documentInvalid};
  return {std::move(result), {}};
}
QJsonArray serializeAnnotationList(const std::vector<Annotation> &items) {
  QJsonArray annotations;
  for (std::size_t index = 0; index < items.size(); ++index) {
    const Annotation &annotation = items[index];
    if (const auto *unsupported = std::get_if<UnsupportedAnnotation>(&annotation)) {
      annotations.append(unsupported->raw);  // ES-015: verbatim
      continue;
    }
    const auto &rectangle = std::get<RectangleAnnotation>(annotation);
    QJsonObject o = rectangle.extra;
    o.insert(QStringLiteral("id"), rectangle.id.toString(QUuid::WithoutBraces));
    o.insert(QStringLiteral("type"), rectangle.type());
    const ShapeContent &content = rectangle.content;
    if (!content.points.empty()) {
      QJsonArray points;
      for (const PointF &p : content.points) points.append(pointJson(p));
      o.insert(QStringLiteral("points"), points);
    }
    if (isTextShape(content.type)) o.insert(QStringLiteral("fontSize"), content.fontSize);
    if (content.type == shape::text || content.type == shape::speechBalloon) {
      o.insert(QStringLiteral("text"), content.text);
      if (!content.textAlignment.isEmpty()) o.insert(QStringLiteral("textAlignment"), content.textAlignment);
      else o.remove(QStringLiteral("textAlignment"));
    }
    if (content.type == shape::step) o.insert(QStringLiteral("stepValue"), double(content.stepValue));
    if (isAssetShape(content.type)) o.insert(QStringLiteral("imageKey"), content.imageKey);
    if (hasShapeStrength(content.type)) o.insert(QStringLiteral("strength"), content.strength);
    o.insert(QStringLiteral("start"), pointJson(rectangle.start));
    o.insert(QStringLiteral("end"), pointJson(rectangle.end));
    o.insert(QStringLiteral("strokeColor"), formatColor(rectangle.style.strokeColor));
    o.insert(QStringLiteral("fillColor"), formatColor(rectangle.style.fillColor));
    o.insert(QStringLiteral("strokeWidth"), rectangle.style.strokeWidth);
    o.insert(QStringLiteral("rotationDegrees"), rectangle.style.rotationDegrees);
    o.insert(QStringLiteral("opacity"), rectangle.style.opacity);
    o.insert(QStringLiteral("visible"), rectangle.visible);
    // Pending clarification (ROOT-ESCALATE-001): the schema has zIndex but
    // SPEC.md orders annotations by array position. The array is treated as
    // authoritative and zIndex is rewritten to match it.
    o.insert(QStringLiteral("zIndex"), static_cast<qint64>(index));
    annotations.append(o);
  }
  return annotations;
}

ParseResult readXann(const QByteArray &gzip, qint64 maxJsonBytes) {
  ParseResult result;
  z_stream stream{};
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
    result.error = diagnostic::documentInvalid;
    return result;
  }
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(gzip.constData()));
  stream.avail_in = static_cast<uInt>(gzip.size());
  QByteArray json;
  char buffer[65536];
  int status = Z_OK;
  while (status == Z_OK) {
    stream.next_out = reinterpret_cast<Bytef *>(buffer);
    stream.avail_out = sizeof buffer;
    status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) break;
    const qint64 produced = static_cast<qint64>(sizeof buffer - stream.avail_out);
    // ES-028: stop inflating as soon as the limit is crossed.
    if (json.size() + produced > maxJsonBytes) {
      inflateEnd(&stream);
      result.error = diagnostic::documentTooLarge;
      return result;
    }
    json.append(buffer, static_cast<qsizetype>(produced));
  }
  inflateEnd(&stream);
  if (status != Z_STREAM_END) {
    result.error = diagnostic::documentInvalid;
    return result;
  }
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = diagnostic::documentInvalid;
    return result;
  }
  return parseDocument(document.object());
}

std::optional<QByteArray> writeXann(const AnnotationDocument &document) {
  const QByteArray json = QJsonDocument(serializeDocument(document)).toJson(QJsonDocument::Compact);
  z_stream stream{};
  if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
    return std::nullopt;
  }
  QByteArray out(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(json.size()))), '\0');
  stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(json.constData()));
  stream.avail_in = static_cast<uInt>(json.size());
  stream.next_out = reinterpret_cast<Bytef *>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());
  const int status = deflate(&stream, Z_FINISH);
  out.resize(static_cast<qsizetype>(stream.total_out));
  deflateEnd(&stream);
  if (status != Z_STREAM_END) return std::nullopt;
  return out;
}

SidecarPaths sidecarPaths(const QString &rasterPath, const std::function<bool(const QString &)> &exists) {
  SidecarPaths paths;
  paths.writePath = rasterPath + QStringLiteral(".xann");
  if (exists(paths.writePath)) {
    paths.readPath = paths.writePath;
    return paths;
  }
  // ES-030 baseline fallback: last extension replaced by .xann.
  const auto baseline = baselineSidecarPath(rasterPath);
  if (baseline && exists(*baseline)) paths.readPath = *baseline;
  return paths;
}

SidecarSaveResult saveSidecar(SidecarStore &store, const QString &rasterPath, const AnnotationDocument &document,
                              bool sidecarIntent) {
  SidecarSaveResult result;
  const QString path = sidecarPaths(rasterPath, [](const QString &) { return false; }).writePath;
  if (document.annotations.empty()) {
    // ES-014 removes the default, while ES-030 then exposes a baseline alias.
    // Until its archive/reconciliation policy is approved, never report an
    // editable save complete while that older document would become visible.
    const auto baseline = baselineSidecarPath(rasterPath);
    if (sidecarIntent && baseline && store.exists(*baseline)) {
      result.ok = false; result.diagnostic = diagnostic::sidecarSaveFailed; return result;
    }
    // ES-014: an empty document removes the default sidecar only on explicit
    // intent, and never returns a sidecar path.
    if (sidecarIntent && store.exists(path) && !store.remove(path)) {
      result.ok = false;
      result.diagnostic = diagnostic::sidecarSaveFailed;
    }
    return result;
  }
  // Pending clarification (ROOT-ESCALATE-001): ES-014 ties sidecar intent only
  // to removal. A document with annotations always writes its sidecar, since
  // that is what keeps the edit re-editable (ES-011).
  const std::optional<QByteArray> bytes = writeXann(document);
  if (!bytes || !store.write(path, *bytes)) {
    result.ok = false;
    result.diagnostic = diagnostic::sidecarSaveFailed;
    return result;
  }
  result.sidecarPath = path;
  return result;
}

bool validTextAlignment(const QString &alignment) {
  return alignment.isEmpty() || alignment == u"left" || alignment == u"center" || alignment == u"right";
}
bool validBalloonTail(const RectangleAnnotation &shape, const std::vector<PointF> &points) {
  if (points.size() > kMaxPathPoints) return false;
  for (const auto &point : points) if (!std::isfinite(point.x) || !std::isfinite(point.y)) return false;
  if (points.empty()) return true;
  return std::isfinite(shape.start.x + (shape.end.x - shape.start.x) * points[0].x) &&
      std::isfinite(shape.start.y + (shape.end.y - shape.start.y) * points[0].y);
}

}  // namespace xerahs::editor
