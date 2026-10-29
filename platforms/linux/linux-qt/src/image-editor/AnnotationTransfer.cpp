#include "AnnotationTransfer.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>
namespace xerahs::editor {
namespace {
QString clipboardAssetDiagnostic(const std::vector<Annotation> &annotations) {
  for (const auto &annotation : annotations) {
    const auto *shape = std::get_if<RectangleAnnotation>(&annotation);
    if (shape && isAssetShape(shape->type())) return "annotation-clipboard-assets-unavailable";
  }
  return {};
}
bool boundedJson(const QJsonValue &value, qint64 &remaining, int depth, int &nodes) {
  if (depth > 32 || --nodes < 0 || (remaining -= 64) < 0) return false;
  if (value.isString()) { remaining -= qint64(value.toString().size()) * 6; return remaining >= 0; }
  if (value.isArray()) { for (const auto &item : value.toArray()) if (!boundedJson(item, remaining, depth + 1, nodes)) return false; }
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
      remaining -= qint64(it.key().size()) * 6; if (remaining < 0 || !boundedJson(it.value(), remaining, depth + 1, nodes)) return false;
    }
  }
  return true;
}
}
QString annotationTransferDiagnostic(const std::vector<Annotation> &annotations) {
  if (annotations.empty()) return "annotation-selection-empty";
  if (annotations.size() > kMaxAnnotationTransferObjects) return "annotation-transfer-too-large";
  for (const auto &a : annotations) {
    const auto *r = std::get_if<RectangleAnnotation>(&a); if (!r) return "annotation-type-unsupported";
    if (!annotationEditable(a)) return "annotation-selection-protected";
    if (r->id.isNull() || !std::isfinite(r->start.x) || !std::isfinite(r->start.y) || !std::isfinite(r->end.x) || !std::isfinite(r->end.y) ||
        r->end.x <= r->start.x || r->end.y <= r->start.y || std::abs(r->start.x) > kMaxDimension || std::abs(r->start.y) > kMaxDimension || std::abs(r->end.x) > kMaxDimension || std::abs(r->end.y) > kMaxDimension) return "annotation-transfer-invalid";
  }
  qint64 remaining = kMaxAnnotationClipboardBytes; int nodes = 100000;
  const auto json = serializeAnnotationList(annotations);
  if (!boundedJson(json, remaining, 0, nodes)) return "annotation-transfer-too-large";
  if (!parseAnnotationList(json).annotations) return "annotation-transfer-invalid";
  return {};
}
AnnotationTransferResult selectedAnnotationTransfer(const AnnotationDocument &document, const QSet<QUuid> &selection, bool clipboard) {
  AnnotationTransferResult result;
  try {
    for (const auto &a : document.annotations) if (selection.contains(annotationId(a))) {
      if (result.annotations.size() >= kMaxAnnotationTransferObjects) { result.annotations.clear(); result.diagnostic = "annotation-transfer-too-large"; return result; }
      result.annotations.push_back(a);
    }
    result.diagnostic = annotationTransferDiagnostic(result.annotations);
    if (!result.diagnostic.isEmpty()) { result.annotations.clear(); return result; }
    if (clipboard) {
      result.diagnostic = clipboardAssetDiagnostic(result.annotations);
      if (!result.diagnostic.isEmpty()) { result.annotations.clear(); return result; }
      const QJsonObject root{{"version", 1}, {"annotations", serializeAnnotationList(result.annotations)}};
      qint64 remaining = kMaxAnnotationClipboardBytes; int nodes = 100000;
      // Count the envelope's depth/keys/nodes as well as its annotation array,
      // so every admitted offer is also admitted by the receiving boundary.
      if (!boundedJson(root, remaining, 0, nodes)) { result = {}; result.diagnostic = "annotation-transfer-too-large"; return result; }
      result.bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
      if (result.bytes.size() > kMaxAnnotationClipboardBytes) { result = {}; result.diagnostic = "annotation-transfer-too-large"; }
    }
  } catch (...) { result = {}; result.diagnostic = "annotation-transfer-failed"; }
  return result;
}
AnnotationTransferResult parseAnnotationClipboard(const QByteArray &bytes) {
  AnnotationTransferResult result;
  try {
    if (bytes.size() > kMaxAnnotationClipboardBytes) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    QJsonParseError error; const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    const auto root = json.object(); qint64 remaining = kMaxAnnotationClipboardBytes; int nodes = 100000;
    if (!boundedJson(root, remaining, 0, nodes)) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    if (root.size() != 2 || !root.value("version").isDouble() || root.value("version").toDouble() != 1 || !root.value("annotations").isArray()) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    // This private transport requires compact canonical Qt JSON. Duplicate
    // object keys and alternate ambiguous encodings cannot survive this check.
    if (QJsonDocument(root).toJson(QJsonDocument::Compact) != bytes) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    const auto list = root.value("annotations").toArray(); if (list.size() > qint64(kMaxAnnotationTransferObjects)) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    auto parsed = parseAnnotationList(list); if (!parsed.annotations) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    result.annotations = std::move(*parsed.annotations); result.diagnostic = annotationTransferDiagnostic(result.annotations);
    if (result.diagnostic.isEmpty()) result.diagnostic = clipboardAssetDiagnostic(result.annotations);
    if (!result.diagnostic.isEmpty()) result.annotations.clear();
  } catch (...) { result = {}; result.diagnostic = "annotation-transfer-failed"; }
  return result;
}
}
