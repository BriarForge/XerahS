#include "AnnotationTransfer.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>
namespace xerahs::editor {
namespace {
// Every asset the annotations reference must be supplied, and nothing else.
QString clipboardAssetDiagnostic(const std::vector<Annotation> &annotations, const EmbeddedImages &assets) {
  QSet<QString> referenced;
  for (const auto &annotation : annotations) {
    const auto *shape = std::get_if<RectangleAnnotation>(&annotation);
    if (!shape || !isAssetShape(shape->type())) continue;
    const auto &key = shape->content.imageKey;
    if (std::none_of(assets.begin(), assets.end(), [&](const auto &asset) { return asset.first == key; })) return "annotation-clipboard-assets-unavailable";
    referenced.insert(key);
  }
  if (assets.size() > kMaxClipboardAssets) return "annotation-transfer-too-large";
  qsizetype total = 0;
  for (const auto &[key, png] : assets) {
    if (!referenced.contains(key)) return "annotation-transfer-invalid";  // never carry unselected pixels
    if (png.size() > kMaxClipboardAssetPngBytes) return "annotation-transfer-too-large";
    if (!validEmbeddedPng(png)) return "annotation-clipboard-asset-invalid";
    total += png.size();
  }
  return total > kMaxAnnotationAssetClipboardBytes / 2 ? QString("annotation-transfer-too-large") : QString();
}
QJsonObject envelope(const std::vector<Annotation> &annotations, const EmbeddedImages &assets, bool withAssets) {
  QJsonObject root{{"version", assets.empty() ? 1 : 2}, {"annotations", serializeAnnotationList(annotations)}};
  if (withAssets && !assets.empty()) {
    QJsonObject encoded;
    for (const auto &[key, png] : assets) encoded.insert(key, QString::fromLatin1(png.toBase64()));
    root.insert("assets", encoded);
  }
  return root;
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
      for (const auto &a : result.annotations) {
        const auto &r = std::get<RectangleAnnotation>(a);
        if (!isAssetShape(r.type())) continue;
        const auto found = std::find_if(document.embeddedImages.begin(), document.embeddedImages.end(), [&](const auto &asset) { return asset.first == r.content.imageKey; });
        if (found == document.embeddedImages.end()) { result = {}; result.diagnostic = "annotation-clipboard-assets-unavailable"; return result; }
        if (std::none_of(result.assets.begin(), result.assets.end(), [&](const auto &asset) { return asset.first == found->first; })) result.assets.push_back(*found);
      }
      result.diagnostic = clipboardAssetDiagnostic(result.annotations, result.assets);
      if (!result.diagnostic.isEmpty()) { const auto diagnostic = result.diagnostic; result = {}; result.diagnostic = diagnostic; return result; }
      const auto root = envelope(result.annotations, result.assets, true);
      qint64 remaining = kMaxAnnotationClipboardBytes; int nodes = 100000;
      // Count the envelope's depth/keys/nodes as well as its annotation array,
      // so every admitted offer is also admitted by the receiving boundary.
      if (!boundedJson(envelope(result.annotations, result.assets, false), remaining, 0, nodes)) { result = {}; result.diagnostic = "annotation-transfer-too-large"; return result; }
      result.bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
      if (result.bytes.size() > (result.assets.empty() ? kMaxAnnotationClipboardBytes : kMaxAnnotationAssetClipboardBytes)) { result = {}; result.diagnostic = "annotation-transfer-too-large"; }
    }
  } catch (...) { result = {}; result.diagnostic = "annotation-transfer-failed"; }
  return result;
}
AnnotationTransferResult parseAnnotationClipboard(const QByteArray &bytes) {
  AnnotationTransferResult result;
  try {
    if (bytes.size() > kMaxAnnotationAssetClipboardBytes) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    QJsonParseError error; const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    auto root = json.object();
    // Version 1 carries annotations only; version 2 adds the PNG assets they reference.
    const bool assetPayload = root.value("version").isDouble() && root.value("version").toDouble() == 2;
    if (!assetPayload && bytes.size() > kMaxAnnotationClipboardBytes) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    if (root.size() != (assetPayload ? 3 : 2) || !root.value("annotations").isArray() ||
        (!assetPayload && (!root.value("version").isDouble() || root.value("version").toDouble() != 1)) ||
        (assetPayload && (!root.value("assets").isObject() || root.value("assets").toObject().isEmpty()))) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    // This private transport requires compact canonical Qt JSON. Duplicate
    // object keys and alternate ambiguous encodings cannot survive this check.
    if (QJsonDocument(root).toJson(QJsonDocument::Compact) != bytes) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    const auto encoded = root.value("assets").toObject(); root.remove("assets");
    qint64 remaining = kMaxAnnotationClipboardBytes; int nodes = 100000;
    if (!boundedJson(root, remaining, 0, nodes)) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    if (encoded.size() > qsizetype(kMaxClipboardAssets)) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    EmbeddedImages assets;
    for (auto it = encoded.begin(); it != encoded.end(); ++it) {
      // Untrusted: bounded encoded length first, then strict base64, then the PNG header checks. Nothing is decoded as an image here.
      if (it.key().isEmpty() || it.key().size() > 256 || !it.value().isString() || it.value().toString().size() > kMaxClipboardAssetPngBytes / 3 * 4 + 8) { result.diagnostic = it.value().isString() && it.key().size() <= 256 ? "annotation-transfer-too-large" : "annotation-transfer-invalid"; return result; }
      const auto decoded = QByteArray::fromBase64Encoding(it.value().toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
      if (!decoded) { result.diagnostic = "annotation-clipboard-asset-invalid"; return result; }
      assets.emplace_back(it.key(), decoded.decoded);
    }
    const auto list = root.value("annotations").toArray(); if (list.size() > qint64(kMaxAnnotationTransferObjects)) { result.diagnostic = "annotation-transfer-too-large"; return result; }
    auto parsed = parseAnnotationList(list); if (!parsed.annotations) { result.diagnostic = "annotation-transfer-invalid"; return result; }
    result.annotations = std::move(*parsed.annotations); result.diagnostic = annotationTransferDiagnostic(result.annotations);
    if (result.diagnostic.isEmpty()) result.diagnostic = clipboardAssetDiagnostic(result.annotations, assets);
    if (!result.diagnostic.isEmpty()) { const auto diagnostic = result.diagnostic; result = {}; result.diagnostic = diagnostic; return result; }
    result.assets = std::move(assets);
  } catch (...) { result = {}; result.diagnostic = "annotation-transfer-failed"; }
  return result;
}
}
