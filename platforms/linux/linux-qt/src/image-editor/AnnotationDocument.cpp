#include "image-editor/AnnotationDocument.h"
#include "storage/JsonBoundary.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

#include <zlib.h>

#include <cmath>
#include <utility>

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

using compatibility::ImportDisposition;

bool traversal(const QString &text) {
  QString path = text; path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  return path.contains(QChar(0)) || path.split(QLatin1Char('/')).contains(QStringLiteral(".."));
}
bool unsafePathContent(const QJsonValue &value) { return compatibility::containsTraversal(value); }
// Preset operation receipts are an existing, inert native metadata schema.
// Their legacy type tags are identifiers, never deserialized runtime types.
// Only those declared tag positions are excluded from unknown-field checks.
bool unsafeDocumentExtra(QJsonObject extra) {
  const auto receipts = extra.take(QStringLiteral("effectPresetOperations"));
  if (!receipts.isUndefined()) {
    if (!receipts.isArray()) return true;
    for (const auto &receipt : receipts.toArray()) {
      if (!receipt.isObject()) return true;
      auto preset = receipt.toObject();
      if (preset.value("format") != "com.xerahs.app/effect-preset" || preset.value("version") != 1 ||
          !preset.value("legacy").isObject() || !preset.value("steps").isArray()) return true;
      auto stripTag = [](QJsonObject &legacy) {
        const auto tag = legacy.take(QStringLiteral("$type"));
        return tag.isUndefined() || (tag.isString() && tag.toString().size() <= 1024);
      };
      auto legacy = preset.value("legacy").toObject();
      if (legacy.contains("Effects") && !legacy.value("Effects").isArray()) return true;
      auto effects = legacy.value("Effects").toArray();
      for (int n = 0; n < effects.size(); ++n) {
        auto step = effects[n].toObject(); if (!effects[n].isObject() || !stripTag(step)) return true; effects[n] = step;
      }
      if (legacy.contains("Effects")) legacy.insert("Effects", effects);
      preset.insert("legacy", legacy);
      auto steps = preset.value("steps").toArray();
      for (int n = 0; n < steps.size(); ++n) {
        auto step = steps[n].toObject(); auto raw = step.value("legacy").toObject();
        if (!steps[n].isObject() || !step.value("legacy").isObject() || !stripTag(raw)) return true;
        step.insert("legacy", raw); steps[n] = step;
      }
      preset.insert("steps", steps);
      if (compatibility::containsUnsafeUnknown(preset)) return true;
    }
  }
  return compatibility::containsUnsafeUnknown(extra);
}
QString canonicalTime(const QString &text) {
  const auto time = QDateTime::fromString(text, Qt::ISODate);
  return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : text;
}

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


// EA-005: optional decoration fields of Rectangle and Ellipse. Absent fields
// keep the legacy defaults; present ones are validated and bounded.
const QStringList kDecorationKeys = {
    QStringLiteral("borderStyle"),    QStringLiteral("shadowEnabled"), QStringLiteral("shadowBlurRadius"),
    QStringLiteral("shadowColor"),    QStringLiteral("shadowOffsetX"), QStringLiteral("shadowOffsetY"),
    QStringLiteral("shadowOpacity")};
const QString kCornerRadiusKey = QStringLiteral("cornerRadius");

bool parseDecoration(const QJsonObject &o, const QString &type, RectangleStyle &style) {
  const auto number = [&](const char16_t *key, double low, double high, double &out) {
    if (!o.contains(key)) return true;
    const QJsonValue v = o.value(key);
    if (!isFiniteNumber(v) || v.toDouble() < low || v.toDouble() > high) return false;
    out = v.toDouble();
    return true;
  };
  if (o.contains(u"borderStyle")) {
    const QJsonValue v = o.value(u"borderStyle");
    const auto parsed = v.isString() ? parseBorderStyle(v.toString()) : std::nullopt;
    if (!parsed) return false;
    style.borderStyle = *parsed;
  }
  if (type == shape::arrow && o.contains(u"style")) {
    const QJsonValue v = o.value(u"style");
    const auto parsed = v.isString() ? parseArrowStyle(v.toString()) : std::nullopt;
    if (!parsed) return false;
    style.arrowStyle = *parsed;
  }
  if (type == shape::rectangle && (!number(u"cornerRadius", 0, kMaxCornerRadius, style.cornerRadius) ||
                                   (o.contains(u"cornerRadius") && !isInteger(o.value(u"cornerRadius")))))
    return false;
  if (o.contains(u"shadowEnabled")) {
    if (!o.value(u"shadowEnabled").isBool()) return false;
    style.shadow.enabled = o.value(u"shadowEnabled").toBool();
  }
  if (!number(u"shadowBlurRadius", 0, kMaxShadowBlur, style.shadow.blurRadius) ||
      !number(u"shadowOffsetX", -kMaxShadowOffset, kMaxShadowOffset, style.shadow.offsetX) ||
      !number(u"shadowOffsetY", -kMaxShadowOffset, kMaxShadowOffset, style.shadow.offsetY) ||
      !number(u"shadowOpacity", 0, 1, style.shadow.opacity))
    return false;
  if (o.contains(u"shadowColor")) {
    const QJsonValue v = o.value(u"shadowColor");
    const auto color = v.isString() ? parseColor(v.toString()) : std::nullopt;
    if (!color) return false;
    style.shadow.color = *color;
  }
  return true;
}

// EA-006: line/arrow curve anchor. Persisted as the optional third entry of
// `points` (a fraction of the bounds like the endpoints). The reference XANN
// format stores `curvePoint` {x, y} in document pixels plus
// `curvePointActivated`; those are accepted on read when `points` holds only
// the two endpoints, and converted to the fractional form.
bool parseCurve(const QJsonObject &o, RectangleAnnotation &r) {
  auto &points = r.content.points;
  if (points.size() == 3) {
    if (std::abs(points[2].x) > kMaxCurveFraction || std::abs(points[2].y) > kMaxCurveFraction) return false;
    return std::isfinite(r.left() + points[2].x * (r.right() - r.left())) && std::isfinite(r.top() + points[2].y * (r.bottom() - r.top()));
  }
  if (points.size() == 2 && o.contains(u"curvePoint")) {
    if (o.contains(u"curvePointActivated") && !o.value(u"curvePointActivated").isBool()) return false;
    const auto anchor = parsePoint(o.value(u"curvePoint"));
    if (!anchor) return false;
    const double width = r.right() - r.left(), height = r.bottom() - r.top();
    if (!o.value(u"curvePointActivated").toBool(false) || !(width > 0) || !(height > 0)) return true;
    const PointF fraction{(anchor->x - r.left()) / width, (anchor->y - r.top()) / height};
    if (!std::isfinite(fraction.x) || !std::isfinite(fraction.y) || std::abs(fraction.x) > kMaxCurveFraction || std::abs(fraction.y) > kMaxCurveFraction) return false;
    points.push_back(fraction);
  }
  return true;
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
  QString consumedAlias;  // EA-011 reference alias read in place of `strength`
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
    if (type == shape::text || type == shape::speechBalloon) {
      for (const auto &field : {std::pair{QStringLiteral("textBold"), &content.textBold}, std::pair{QStringLiteral("textItalic"), &content.textItalic}}) {
        if (!o.contains(field.first)) continue;
        if (!o.value(field.first).isBool()) { findings.invalid = true; return std::nullopt; }
        *field.second = o.value(field.first).toBool();
      }
    }
    if ((type == shape::text || type == shape::speechBalloon) && o.contains(u"textColor")) {
      const auto value = o.value(u"textColor");
      if (!value.isString() || !(content.textColor = parseColor(value.toString()))) { findings.invalid = true; return std::nullopt; }
    }
    if ((type == shape::text || type == shape::speechBalloon) && o.contains(u"textFillColor")) {
      const auto value = o.value(u"textFillColor");
      if (!value.isString() || !(content.textFillColor = parseColor(value.toString()))) { findings.invalid = true; return std::nullopt; }
    }
    if (type == shape::text && o.contains(u"textBorderColor")) {
      const auto value = o.value(u"textBorderColor");
      if (!value.isString() || !(content.textBorderColor = parseColor(value.toString()))) { findings.invalid = true; return std::nullopt; }
    }
    if (type == shape::speechBalloon && o.contains(u"balloonBorderColor")) {
      const auto value = o.value(u"balloonBorderColor");
      if (!value.isString() || !(content.balloonBorderColor = parseColor(value.toString()))) { findings.invalid = true; return std::nullopt; }
    }
    if ((type == shape::text || type == shape::speechBalloon) && o.contains(u"fontFamily")) {
      const auto family = o.value(u"fontFamily");
      if (!family.isString() || !validFontFamily(family.toString())) { findings.invalid = true; return std::nullopt; }
      content.fontFamily = family.toString();
    }
    if (hasShapeEllipse(type) && o.contains(u"isEllipse")) {
      if (!o.value(u"isEllipse").isBool()) { findings.invalid = true; return std::nullopt; }
      content.isEllipse = o.value(u"isEllipse").toBool();
    }
    double stepValue = 0;
    bool ok = (!hasShapeBlurAmount(type) || optionalNumber(u"blurAmount", 0, kMaxBlurAmount, content.blurAmount)) &&
              optionalNumber(u"fontSize", 0, 2000, content.fontSize) && optionalNumber(u"strength", 0, 1000, content.strength) &&
              optionalNumber(u"stepValue", 0, double(kMaxStepValue), stepValue) && (!o.contains(u"stepValue") || isInteger(o.value(u"stepValue")));
    content.stepValue = qint64(stepValue);
    if (type == shape::step && o.contains(u"stepType")) {
      const auto value = o.value(u"stepType");
      const auto style = value.isString() ? parseStepStyle(value.toString()) : std::nullopt;
      if (!style) { findings.invalid = true; return std::nullopt; }
      content.stepStyle = *style;
    }
    if (ok && hasShapeStrength(type) && !o.contains(u"strength")) {
      // EA-011 compatibility: the reference XerahS .xann spells effect strength `amount` and the Spotlight dim `darkenOpacity` (byte, 0..255).
      // They are read only when `strength` is absent, consumed so they are not preserved beside the `strength` this build writes.
      const bool spot = type == shape::spotlight; const char16_t *key = spot ? u"darkenOpacity" : u"amount";
      if (o.contains(key)) {
        const QJsonValue v = o.value(key);
        if (!isFiniteNumber(v) || v.toDouble() < 0 || v.toDouble() > (spot ? 255.0 : 1000.0)) ok = false;
        else {
          // Spotlight strength is the dim percentage (alpha = percent * 2.55), and 0 means "use the default"; a fully
          // transparent reference dim therefore maps to a sub-visible percentage that still paints alpha 0.
          content.strength = spot ? std::max(v.toDouble() / 2.55, 0.01) : v.toDouble();
          consumedAlias = QString::fromUtf16(key);
        }
      }
    }
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
    // EA-010: optional asset metadata, reference XANN names. A present but unusable value rejects the document.
    if (type == shape::emoji) {
      for (const auto &field : {std::pair{u"unicodeSequence", &content.unicodeSequence}, std::pair{u"displayName", &content.displayName}}) {
        if (!o.contains(field.first)) continue;
        const auto value = o.value(field.first);
        const bool good = value.isString() && (field.first[0] == u'u' ? validUnicodeSequence(value.toString()) : validEmojiDisplayName(value.toString()));
        if (!good) { ok = false; break; }
        *field.second = value.toString();
      }
    }
    if (type == shape::cursor && o.contains(u"cursorType")) {
      const auto name = o.value(u"cursorType").isString() ? parseCursorTypeName(o.value(u"cursorType").toString()) : std::nullopt;
      if (!name) ok = false; else content.cursorType = *name;
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
  // EA-009: a Step has at most one point, its tail tip, which must map to a finite position.
  if (type == shape::step && !rectangle.content.points.empty() &&
      (rectangle.content.points.size() > 1 || !validBalloonTail(rectangle, rectangle.content.points))) {
    findings.invalid = true;
    return std::nullopt;
  }
  if (hasShapeDecoration(type) && !parseDecoration(o, type, rectangle.style)) {
    findings.invalid = true;
    return std::nullopt;
  }
  if ((type == shape::line || type == shape::arrow) && !parseCurve(o, rectangle)) {
    findings.invalid = true;
    return std::nullopt;
  }
  for (auto it = o.begin(); it != o.end(); ++it) {
    if (!kKnownRectangleKeys.contains(it.key())) rectangle.extra.insert(it.key(), it.value());
  }
  if (type == shape::line || type == shape::arrow) {
    // Consumed reference curve fields are re-expressed as points[2]; an unusable one stays verbatim.
    if (rectangle.content.points.size() == 3 && o.value(u"points").toArray().size() == 2 && o.value(u"curvePointActivated").toBool()) {
      rectangle.extra.remove(QStringLiteral("curvePoint")); rectangle.extra.remove(QStringLiteral("curvePointActivated"));
    }
  }
  if (type == shape::arrow) rectangle.extra.remove(QStringLiteral("style"));
  if (!consumedAlias.isEmpty()) rectangle.extra.remove(consumedAlias);
  if (hasShapeDecoration(type)) {
    for (const auto &key : kDecorationKeys) rectangle.extra.remove(key);
    if (type == shape::rectangle) rectangle.extra.remove(kCornerRadiusKey);  // Ellipse keeps an unknown cornerRadius verbatim
  }
  if (type == shape::text || type == shape::speechBalloon) {
    for (const auto &key : {QStringLiteral("textAlignment"), QStringLiteral("textBold"), QStringLiteral("textItalic"), QStringLiteral("textColor"), QStringLiteral("fontFamily"), QStringLiteral("textFillColor")}) rectangle.extra.remove(key);
  }
  if (type == shape::speechBalloon) rectangle.extra.remove(QStringLiteral("balloonBorderColor"));
  if (type == shape::step) rectangle.extra.remove(QStringLiteral("stepType"));
  if (type == shape::emoji) { rectangle.extra.remove(QStringLiteral("unicodeSequence")); rectangle.extra.remove(QStringLiteral("displayName")); }
  if (type == shape::cursor) rectangle.extra.remove(QStringLiteral("cursorType"));
  if (type == shape::text) rectangle.extra.remove(QStringLiteral("textBorderColor"));
  if (hasShapeEllipse(type)) rectangle.extra.remove(QStringLiteral("isEllipse"));
  if (hasShapeBlurAmount(type)) rectangle.extra.remove(QStringLiteral("blurAmount"));
  return Annotation(rectangle);
}

QJsonObject pointJson(const PointF &p) { return QJsonObject{{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}}; }

}  // namespace

QString borderStyleName(BorderStyle style) {
  switch (style) {
    case BorderStyle::Dash: return QStringLiteral("dash");
    case BorderStyle::Dot: return QStringLiteral("dot");
    case BorderStyle::DashDot: return QStringLiteral("dashDot");
    case BorderStyle::DashDotDot: return QStringLiteral("dashDotDot");
    case BorderStyle::Solid: break;
  }
  return QStringLiteral("solid");
}
std::optional<BorderStyle> parseBorderStyle(const QString &name) {
  for (const BorderStyle style : {BorderStyle::Solid, BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot})
    if (borderStyleName(style).compare(name, Qt::CaseInsensitive) == 0) return style;
  return std::nullopt;
}
QString arrowStyleName(ArrowStyle style) {
  switch (style) {
    case ArrowStyle::Modern: return QStringLiteral("modern");
    case ArrowStyle::Double: return QStringLiteral("double");
    case ArrowStyle::Basic: return QStringLiteral("basic");
    case ArrowStyle::Line: return QStringLiteral("line");
    case ArrowStyle::Classic: break;
  }
  return QStringLiteral("classic");
}
std::optional<ArrowStyle> parseArrowStyle(const QString &name) {
  for (const ArrowStyle style : {ArrowStyle::Classic, ArrowStyle::Modern, ArrowStyle::Double, ArrowStyle::Basic, ArrowStyle::Line})
    if (arrowStyleName(style).compare(name, Qt::CaseInsensitive) == 0) return style;
  return std::nullopt;
}
bool hasShapeDecoration(const QString &type) {
  return type == shape::rectangle || type == shape::ellipse || type == shape::line || type == shape::arrow;
}
bool hasDecoration(const QString &type, const RectangleStyle &style) {
  if (!hasShapeDecoration(type)) return false;
  return style.borderStyle != BorderStyle::Solid || (type == shape::rectangle && style.cornerRadius != 0) || style.shadow.enabled ||
         (type == shape::arrow && style.arrowStyle != ArrowStyle::Classic);
}
bool validDecoration(const QString &type, const RectangleStyle &style) {
  if (!hasShapeDecoration(type)) return true;
  const auto in = [](double v, double low, double high) { return std::isfinite(v) && v >= low && v <= high; };
  const ShapeShadow &sh = style.shadow;
  return (type != shape::rectangle || (in(style.cornerRadius, 0, kMaxCornerRadius) && std::floor(style.cornerRadius) == style.cornerRadius)) && in(sh.blurRadius, 0, kMaxShadowBlur) &&
         in(sh.offsetX, -kMaxShadowOffset, kMaxShadowOffset) && in(sh.offsetY, -kMaxShadowOffset, kMaxShadowOffset) &&
         in(sh.opacity, 0, 1);
}
bool sameDecoration(const RectangleStyle &a, const RectangleStyle &b) {
  return a.borderStyle == b.borderStyle && a.cornerRadius == b.cornerRadius && a.shadow == b.shadow && a.arrowStyle == b.arrowStyle;
}

QStringList shapeTypes() {
  return {shape::rectangle, shape::ellipse, shape::line, shape::arrow, shape::freehand, shape::text,
          shape::step, shape::highlight, shape::blur, shape::pixelate, shape::speechBalloon, shape::magnify,
          shape::spotlight, shape::smartEraser, shape::image, shape::emoji, shape::cursor};
}
bool isAssetShape(const QString &type) { return type == shape::image || type == shape::emoji || type == shape::cursor; }
bool hasShapeStrength(const QString &type) {
  return type == shape::blur || type == shape::pixelate || type == shape::magnify || type == shape::spotlight;
}
bool hasShapeEllipse(const QString &type) {
  return type == shape::highlight || type == shape::blur || type == shape::pixelate || type == shape::magnify ||
         type == shape::spotlight;
}
bool hasShapeBlurAmount(const QString &type) { return type == shape::spotlight; }
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
    result.report.add(ImportDisposition::Rejected);
    return result;
  }

  Findings findings;
  // Unsafe-content / traversal rejections are remembered (first encountered wins) and reported
  // after the ES-029 priority 2 size failure, so a too-large document is never masked by them.
  QString security;
  const auto flagSecurity = [&security](const QString &code) { if (security.isEmpty()) security = code; };
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
  if (traversal(document.imagePath)) flagSecurity(QStringLiteral("document-path-traversal"));
  document.imageHash = payload.value(u"imageHash").toString();
  static const QRegularExpression hashPattern(QStringLiteral("^(|sha256:[0-9a-f]{64})$"));
  if (!hashPattern.match(document.imageHash).hasMatch()) findings.invalid = true;
  document.createdAt = canonicalTime(payload.value(u"createdAt").toString());
  document.modifiedAt = canonicalTime(payload.value(u"modifiedAt").toString());
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
      if (traversal(it.key())) flagSecurity(QStringLiteral("document-path-traversal"));
      if (auto png = decodePng(it.value(), findings)) document.embeddedImages.emplace_back(it.key(), *png);
    }
  }

  if (payload.contains(u"extensions") && !payload.value(u"extensions").isObject()) findings.invalid = true;

  const QJsonValue annotations = payload.value(u"annotations");
  if (!annotations.isArray()) {
    findings.invalid = true;
  } else {
    auto parsed = parseAnnotationList(annotations.toArray());
    result.report = parsed.report;
    if (parsed.annotations) document.annotations = std::move(*parsed.annotations);
    else if (parsed.diagnostic != diagnostic::documentInvalid) flagSecurity(parsed.diagnostic);
    else findings.invalid = true;
  }

  for (auto it = payload.begin(); it != payload.end(); ++it) {
    if (!kKnownDocumentKeys.contains(it.key()) || it.key() == QStringLiteral("extensions")) {
      if (unsafePathContent(QJsonObject{{it.key(), it.value()}})) { flagSecurity(QStringLiteral("document-path-traversal")); continue; }
      if (compatibility::unsafeUnknown(it.key()) || unsafeDocumentExtra(QJsonObject{{it.key(), it.value()}})) {
        flagSecurity(QStringLiteral("document-unsafe-content")); continue;
      }
      document.extra.insert(it.key(), it.value());
    }
  }

  // ES-029 priority 2 and 3.
  if (findings.tooLarge) result.error = diagnostic::documentTooLarge;
  else if (!security.isEmpty()) result.error = security;
  else if (findings.invalid) result.error = diagnostic::documentInvalid;
  else result.document = std::move(document);
  if (result.error) result.report.add(ImportDisposition::Rejected);
  else {
    for (auto it = payload.begin(); it != payload.end(); ++it) {
      if (it.key() == u"annotations") continue; // records counted by the shared annotation parser
      const bool transformed = (it.key() == u"createdAt" && it.value().toString() != result.document->createdAt) ||
          (it.key() == u"modifiedAt" && it.value().toString() != result.document->modifiedAt);
      result.report.add(transformed ? ImportDisposition::Transformed : ImportDisposition::Retained);
    }
  }
  return result;
}

QJsonObject serializeDocument(const AnnotationDocument &document) {
  QJsonObject root = document.extra;
  root.insert(QStringLiteral("version"), document.version);
  root.insert(QStringLiteral("imagePath"), document.imagePath);
  root.insert(QStringLiteral("imageHash"), document.imageHash);
  root.insert(QStringLiteral("canvasWidth"), document.canvasWidth);
  root.insert(QStringLiteral("canvasHeight"), document.canvasHeight);
  root.insert(QStringLiteral("createdAt"), canonicalTime(document.createdAt));
  root.insert(QStringLiteral("modifiedAt"), canonicalTime(document.modifiedAt));
  root.insert(QStringLiteral("sourceImagePngBase64"), QString::fromLatin1(document.sourceImagePng.toBase64()));
  QJsonObject embedded;
  for (const auto &[key, png] : document.embeddedImages) embedded.insert(key, QString::fromLatin1(png.toBase64()));
  root.insert(QStringLiteral("embeddedImages"), embedded);

  root.insert(QStringLiteral("annotations"), serializeAnnotationList(document.annotations));
  return root;
}

AnnotationListResult parseAnnotationList(const QJsonArray &items) {
  Findings findings; std::vector<Annotation> result; QSet<QUuid> ids;
  compatibility::ImportReport report;
  qsizetype index = -1;
  const auto locate = [&](const QJsonValue &item, ImportDisposition d, const char *reason) {
    // Only a syntactically valid UUID is echoed; never any other payload text.
    const QString raw = item.isObject() ? item.toObject().value(u"id").toString() : QString();
    report.addRecord(index, QStringLiteral("annotation"), isUuid(raw) ? raw : QString(), d, QLatin1String(reason));
  };
  for (const auto &item : items) {
    ++index;
    auto annotation = parseAnnotation(item, findings);
    if (!annotation) { report.add(ImportDisposition::Rejected); locate(item, ImportDisposition::Rejected, "invalid-record"); continue; }
    const QUuid id = annotationId(*annotation);
    if (ids.contains(id)) { findings.invalid = true; report.add(ImportDisposition::Rejected); locate(item, ImportDisposition::Rejected, "duplicate-id"); continue; }
    ids.insert(id);
    const auto raw = item.toObject();
    const auto *known = std::get_if<RectangleAnnotation>(&*annotation);
    const QJsonObject extra = known ? known->extra : raw;
    if (unsafePathContent(extra) || (known && traversal(known->content.imageKey))) {
      report.add(ImportDisposition::Rejected);
      locate(item, ImportDisposition::Rejected, "path-traversal");
      return {std::nullopt, QStringLiteral("document-path-traversal"), report};
    }
    if (compatibility::containsUnsafeUnknown(extra)) {
      report.add(ImportDisposition::Rejected);
      locate(item, ImportDisposition::Rejected, "unsafe-content");
      return {std::nullopt, QStringLiteral("document-unsafe-content"), report};
    }
    if (!known) { locate(item, ImportDisposition::Quarantined, "unsupported-type"); report.add(ImportDisposition::Quarantined); } // inert UnsupportedAnnotation, never rendered/executed
    else {
      const auto canonical = serializeAnnotationList({*annotation}).at(0).toObject();
      for (auto it = raw.begin(); it != raw.end(); ++it) {
        // Array order is authoritative; the saved zIndex carries no ordering authority.
        if (it.key() == u"zIndex") report.add(ImportDisposition::Ignored);
        else report.add(canonical.value(it.key()) == it.value() ? ImportDisposition::Retained : ImportDisposition::Transformed);
      }
      for (auto it = canonical.begin(); it != canonical.end(); ++it) if (!raw.contains(it.key())) report.add(ImportDisposition::Defaulted);
    }
    result.push_back(std::move(*annotation));
  }
  if (findings.invalid) return {std::nullopt, diagnostic::documentInvalid, report};
  return {std::move(result), {}, report};
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
    if (content.type == shape::speechBalloon) {
      if (content.balloonBorderColor.has_value()) o.insert(QStringLiteral("balloonBorderColor"), formatColor(*content.balloonBorderColor));
      else o.remove(QStringLiteral("balloonBorderColor"));
    }
    if (isTextShape(content.type)) o.insert(QStringLiteral("fontSize"), content.fontSize);
    if (content.type == shape::text || content.type == shape::speechBalloon) {
      o.insert(QStringLiteral("text"), content.text);
      if (!content.fontFamily.isEmpty()) o.insert(QStringLiteral("fontFamily"), content.fontFamily); else o.remove(QStringLiteral("fontFamily"));
      if (content.textColor.has_value()) o.insert(QStringLiteral("textColor"), formatColor(*content.textColor)); else o.remove(QStringLiteral("textColor"));
      if (content.textFillColor.has_value()) o.insert(QStringLiteral("textFillColor"), formatColor(*content.textFillColor)); else o.remove(QStringLiteral("textFillColor"));
      if (content.type == shape::text) {
        if (content.textBorderColor.has_value()) o.insert(QStringLiteral("textBorderColor"), formatColor(*content.textBorderColor)); else o.remove(QStringLiteral("textBorderColor"));
      }
      for (const auto &field : {std::pair{QStringLiteral("textBold"), content.textBold}, std::pair{QStringLiteral("textItalic"), content.textItalic}}) {
        if (field.second.has_value()) o.insert(field.first, *field.second); else o.remove(field.first);
      }
      if (!content.textAlignment.isEmpty()) o.insert(QStringLiteral("textAlignment"), content.textAlignment);
      else o.remove(QStringLiteral("textAlignment"));
    }
    if (content.type == shape::step) {
      o.insert(QStringLiteral("stepValue"), double(content.stepValue));
      if (content.stepStyle != StepStyle::Numeric) o.insert(QStringLiteral("stepType"), stepStyleName(content.stepStyle));
      else o.remove(QStringLiteral("stepType"));
    }
    if (isAssetShape(content.type)) o.insert(QStringLiteral("imageKey"), content.imageKey);
    if (content.type == shape::emoji) {
      if (!content.unicodeSequence.isEmpty()) o.insert(QStringLiteral("unicodeSequence"), content.unicodeSequence); else o.remove(QStringLiteral("unicodeSequence"));
      if (!content.displayName.isEmpty()) o.insert(QStringLiteral("displayName"), content.displayName); else o.remove(QStringLiteral("displayName"));
    }
    if (content.type == shape::cursor) {
      if (!content.cursorType.isEmpty()) o.insert(QStringLiteral("cursorType"), cursorTypePersistedName(content.cursorType)); else o.remove(QStringLiteral("cursorType"));
    }
    if (hasShapeStrength(content.type)) o.insert(QStringLiteral("strength"), content.strength);
    // EA-011: defaults are omitted on write; a stale value from the source object is cleared.
    if (hasShapeEllipse(content.type)) {
      if (content.isEllipse) o.insert(QStringLiteral("isEllipse"), true); else o.remove(QStringLiteral("isEllipse"));
    }
    if (hasShapeBlurAmount(content.type)) {
      if (content.blurAmount != 0) o.insert(QStringLiteral("blurAmount"), content.blurAmount); else o.remove(QStringLiteral("blurAmount"));
    }
    o.insert(QStringLiteral("start"), pointJson(rectangle.start));
    o.insert(QStringLiteral("end"), pointJson(rectangle.end));
    o.insert(QStringLiteral("strokeColor"), formatColor(rectangle.style.strokeColor));
    o.insert(QStringLiteral("fillColor"), formatColor(rectangle.style.fillColor));
    o.insert(QStringLiteral("strokeWidth"), rectangle.style.strokeWidth);
    o.insert(QStringLiteral("rotationDegrees"), rectangle.style.rotationDegrees);
    o.insert(QStringLiteral("opacity"), rectangle.style.opacity);
    o.insert(QStringLiteral("visible"), rectangle.visible);
    if (hasShapeDecoration(content.type)) {
      const RectangleStyle &style = rectangle.style;
      if (style.borderStyle != BorderStyle::Solid) o.insert(QStringLiteral("borderStyle"), borderStyleName(style.borderStyle));
      else o.remove(QStringLiteral("borderStyle"));
      if (content.type == shape::arrow) {
        if (style.arrowStyle != ArrowStyle::Classic) o.insert(QStringLiteral("style"), arrowStyleName(style.arrowStyle));
        else o.remove(QStringLiteral("style"));
      }
      if (content.type == shape::rectangle) {
        if (style.cornerRadius != 0) o.insert(kCornerRadiusKey, style.cornerRadius); else o.remove(kCornerRadiusKey);
      }
      if (style.shadow != ShapeShadow{}) {
        o.insert(QStringLiteral("shadowEnabled"), style.shadow.enabled);
        o.insert(QStringLiteral("shadowBlurRadius"), style.shadow.blurRadius);
        o.insert(QStringLiteral("shadowColor"), formatColor(style.shadow.color));
        o.insert(QStringLiteral("shadowOffsetX"), style.shadow.offsetX);
        o.insert(QStringLiteral("shadowOffsetY"), style.shadow.offsetY);
        o.insert(QStringLiteral("shadowOpacity"), style.shadow.opacity);
      } else {
        for (const auto &key : {QStringLiteral("shadowEnabled"), QStringLiteral("shadowBlurRadius"), QStringLiteral("shadowColor"),
                                QStringLiteral("shadowOffsetX"), QStringLiteral("shadowOffsetY"), QStringLiteral("shadowOpacity")})
          o.remove(key);
      }
    }
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
  const auto reject = [&](const QString &code) { result.error = code; result.report.add(ImportDisposition::Rejected); return result; };
  if (gzip.size() > kMaxCompressedXannBytes) return reject(diagnostic::documentTooLarge);
  if (gzip.isEmpty()) return reject(diagnostic::documentInvalid);
  maxJsonBytes = std::min(maxJsonBytes, kMaxDecompressedJsonBytes);
  const qint64 expansionLimit = std::max(qint64(1048576), qint64(gzip.size()) * kMaxXannExpansionRatio);
  z_stream stream{};
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
    return reject(diagnostic::documentInvalid);
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
    if (json.size() + produced > std::min(maxJsonBytes, expansionLimit)) {
      inflateEnd(&stream);
      return reject(diagnostic::documentTooLarge);
    }
    json.append(buffer, static_cast<qsizetype>(produced));
  }
  const auto remaining = stream.avail_in;
  inflateEnd(&stream);
  if (status != Z_STREAM_END || remaining != 0) return reject(diagnostic::documentInvalid);
  const auto decoded = compatibility::readJson(json, {maxJsonBytes, 64, 1000000});
  if (!decoded.ok()) {
    if (decoded.code == u"invalid-json") return reject(diagnostic::documentInvalid);
    if (decoded.code == u"too-large") return reject(diagnostic::documentTooLarge);
    return reject(QStringLiteral("document-") + decoded.code);
  }
  if (!decoded.document.isObject()) return reject(diagnostic::documentInvalid);
  return parseDocument(decoded.document.object());
}

std::optional<QByteArray> writeXann(const AnnotationDocument &document, compatibility::ExportReport *report) {
  compatibility::ExportReport outcome{QStringLiteral("xann"), document.version, {}};
  auto payload = serializeDocument(document);
  QString path = document.imagePath; path.replace(QLatin1Char('\\'), QLatin1Char('/'));
  const QString portable = path.mid(path.lastIndexOf(QLatin1Char('/')) + 1).normalized(QString::NormalizationForm_C);
  if (portable != document.imagePath) outcome.omissions.append(QStringLiteral("nonportable-image-path"));
  payload.insert(QStringLiteral("imagePath"), portable);
  if (report) *report = outcome;
  if (unsafeDocumentExtra(document.extra) || unsafePathContent(document.extra)) return std::nullopt;
  for (const auto &annotation : document.annotations) {
    const auto *known = std::get_if<RectangleAnnotation>(&annotation);
    const auto extra = known ? known->extra : std::get<UnsupportedAnnotation>(annotation).raw;
    if (compatibility::containsUnsafeUnknown(extra) || unsafePathContent(extra) || (known && traversal(known->content.imageKey))) return std::nullopt;
  }
  const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
  if (!compatibility::readJson(json, {kMaxDecompressedJsonBytes, 64, 1000000}).ok()) return std::nullopt;
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
  if (status != Z_STREAM_END || out.size() > kMaxCompressedXannBytes ||
      json.size() > std::max(qint64(1048576), qint64(out.size()) * kMaxXannExpansionRatio)) return std::nullopt;
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
  result.report = {QStringLiteral("xann"), document.version, {}};
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
  const std::optional<QByteArray> bytes = writeXann(document, &result.report);
  if (!bytes || !store.write(path, *bytes)) {
    result.ok = false;
    result.diagnostic = diagnostic::sidecarSaveFailed;
    return result;
  }
  result.sidecarPath = path;
  return result;
}

QString stepStyleName(StepStyle style) {
  switch (style) {
    case StepStyle::UppercaseLetter: return QStringLiteral("uppercaseLetter");
    case StepStyle::LowercaseLetter: return QStringLiteral("lowercaseLetter");
    case StepStyle::UppercaseRoman: return QStringLiteral("uppercaseRoman");
    case StepStyle::LowercaseRoman: return QStringLiteral("lowercaseRoman");
    case StepStyle::Numeric: break;
  }
  return QStringLiteral("numeric");
}
std::optional<StepStyle> parseStepStyle(const QString &name) {
  for (const auto style : {StepStyle::Numeric, StepStyle::UppercaseLetter, StepStyle::LowercaseLetter, StepStyle::UppercaseRoman, StepStyle::LowercaseRoman})
    if (name.compare(stepStyleName(style), Qt::CaseInsensitive) == 0) return style;
  return std::nullopt;
}
QString formatStepValue(qint64 value, StepStyle style) {
  if (value <= 0 || style == StepStyle::Numeric) return QString::number(value);
  const bool upper = style == StepStyle::UppercaseLetter || style == StepStyle::UppercaseRoman;
  QString out;
  if (style == StepStyle::UppercaseLetter || style == StepStyle::LowercaseLetter) {
    for (qint64 current = value; current > 0; current /= 26) { --current; out.prepend(QChar(u'A' + int(current % 26))); }
  } else {
    static const std::pair<int, const char *> symbols[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
                                                           {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    for (const auto &symbol : symbols)
      while (value >= symbol.first) { out += QLatin1String(symbol.second); value -= symbol.first; }
  }
  return upper ? out : out.toLower();
}
bool validEmbeddedPng(const QByteArray &png) {
  static const QByteArray signature("\x89PNG\r\n\x1a\n", 8);
  static const QByteArray trailer("\x00\x00\x00\x00IEND\xAE\x42\x60\x82", 12);
  if (png.size() < 33 + 12 || !png.startsWith(signature) || png.mid(12, 4) != QByteArrayLiteral("IHDR") || !png.endsWith(trailer)) return false;
  const auto be32 = [&](int offset) {
    const auto *p = reinterpret_cast<const unsigned char *>(png.constData() + offset);
    return (qint64(p[0]) << 24) | (qint64(p[1]) << 16) | (qint64(p[2]) << 8) | qint64(p[3]);
  };
  const qint64 width = be32(16), height = be32(20);
  return width >= 1 && height >= 1 && width <= kMaxDimension && height <= kMaxDimension && width * height <= kMaxAssetPixels;
}
const QStringList &cursorTypeNames() {
  static const QStringList names{"AppStarting", "Arrow", "Cross", "Default", "Hand", "Help", "HSplit", "IBeam", "No", "NoMove2D",
      "NoMoveHoriz", "NoMoveVert", "PanEast", "PanNE", "PanNorth", "PanNW", "PanSE", "PanSouth", "PanSW", "PanWest", "SizeAll",
      "SizeNESW", "SizeNS", "SizeNWSE", "SizeWE", "UpArrow", "VSplit", "WaitCursor"};
  return names;
}
std::optional<QString> parseCursorTypeName(const QString &name) {
  for (const auto &candidate : cursorTypeNames()) if (candidate.compare(name, Qt::CaseInsensitive) == 0) return candidate;
  return std::nullopt;
}
QString cursorTypePersistedName(const QString &canonical) {
  QString name = canonical;
  if (!name.isEmpty()) name[0] = name[0].toLower();
  return name;
}
bool validUnicodeSequence(const QString &sequence) {
  // Hex code points separated by single spaces or hyphens, as the reference catalog writes them.
  const auto parts = sequence.split(QRegularExpression(QStringLiteral("[ -]")), Qt::KeepEmptyParts);
  if (sequence.isEmpty() || parts.size() > kMaxEmojiCodePoints) return false;
  for (const auto &part : parts) {
    if (part.isEmpty() || part.size() > 6) return false;
    bool ok = false; const uint value = part.toUInt(&ok, 16);
    if (!ok || value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) return false;
    for (const QChar c : part) if (!c.isLetterOrNumber() || c.unicode() > 0x7F) return false;
  }
  return true;
}
bool validEmojiDisplayName(const QString &name) {
  if (name.isEmpty() || name.size() > kMaxEmojiDisplayName) return false;
  for (const QChar c : name) if (c.unicode() < 0x20 || c.unicode() == 0x7F) return false;
  return true;
}
QString unicodeSequenceOf(const QString &emoji) {
  QStringList parts;
  for (const uint cp : emoji.toUcs4()) parts << QString::number(cp, 16);
  return parts.join(QLatin1Char(' '));
}
bool validStepValue(qint64 value) { return value >= 0 && value <= kMaxStepValue; }

bool validFontFamily(const QString &family) {
  if (family.size() > kMaxTextLength) return false;
  for (const QChar c : family) if (c.unicode() < 0x20 || c.unicode() == 0x7F) return false;
  return true;
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
