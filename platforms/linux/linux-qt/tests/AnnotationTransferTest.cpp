#include "image-editor/EditorSession.h"
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QTest>
#include <limits>
using namespace xerahs::editor;
namespace {
RectangleAnnotation rectangle(int i) {
  RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {double(i * 20), 1}; r.end = {double(i * 20 + 15), 16};
  r.style = {0x80556677, 0x408899AA, 3, 27.5, .6}; r.extra = {{"vendor", QJsonObject{{"value", "preserve"}}}}; return r;
}
AnnotationDocument fixture() {
  AnnotationDocument d; d.canvasWidth = 200; d.canvasHeight = 100; d.imagePath = "/PRIVATE/source.png"; d.imageHash = "PRIVATE";
  d.sourceImagePng = "PRIVATE source pixels"; d.extra = {{"private", "EXCLUDE"}};
  d.annotations = {rectangle(0), rectangle(1), rectangle(2)}; return d;
}
QByteArray pngOf(QColor color, int size = 6) {
  QImage image(size, size, QImage::Format_ARGB32); image.fill(color);
  QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return png;
}
// A structurally sound PNG header and trailer claiming the size, padded with `padding` bytes, no real pixels.
QByteArray shellPng(quint32 width, quint32 height, qsizetype padding = 0) {
  QByteArray png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
  for (quint32 v : {width, height}) for (int shift = 24; shift >= 0; shift -= 8) png.append(char((v >> shift) & 0xFF));
  return png + QByteArray(9, '\0') + QByteArray(padding, '\0') + QByteArray("\0\0\0\0IEND\xAE\x42\x60\x82", 12);
}
RectangleAnnotation assetObject(const QString &type, const QString &key, int i = 0) {
  auto r = rectangle(i); r.content.type = type; r.content.imageKey = key; return r;
}
QByteArray assetPayload(const std::vector<Annotation> &objects, const QJsonObject &assets, int version = 2) {
  QJsonObject root{{"version", version}, {"annotations", serializeAnnotationList(objects)}, {"assets", assets}};
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
QByteArray payload(const QJsonArray &objects) { return QJsonDocument(QJsonObject{{"version", 1}, {"annotations", objects}}).toJson(QJsonDocument::Compact); }
}
class AnnotationTransferTest : public QObject {
  Q_OBJECT
private slots:
  void assetFreeSelectionsDoNotPublishUnselectedEmbeddedImages() {
    auto d = fixture(); d.embeddedImages = {{"private-unselected", "PRIVATE embedded image bytes"}};
    auto &unselected = std::get<RectangleAnnotation>(d.annotations[2]); unselected.content.type = shape::image; unselected.content.imageKey = "private-unselected";
    auto &text = std::get<RectangleAnnotation>(d.annotations[1]); text.content.type = shape::text; text.content.text = "Selected text";
    text.content.textFillColor = 0x80112233;
    const auto before = serializeDocument(d); const auto projected = selectedAnnotationTransfer(d, {annotationId(d.annotations[0]), text.id}, true);
    QVERIFY(projected.ok()); QCOMPARE(projected.annotations.size(), size_t(2));
    QVERIFY(!projected.bytes.contains("PRIVATE") && !projected.bytes.contains("private-unselected"));
    const auto parsed = parseAnnotationClipboard(projected.bytes); QVERIFY(parsed.ok()); QCOMPARE(serializeAnnotationList(parsed.annotations), serializeAnnotationList(projected.annotations));
    QCOMPARE(serializeDocument(d), before);
  }
  void assetBearingClipboardPayloadsCannotImportUnresolvedReferences_data() {
    QTest::addColumn<QString>("type"); for (const auto &type : {shape::image, shape::emoji, shape::cursor}) QTest::newRow(qPrintable(type)) << type;
  }
  void assetBearingClipboardPayloadsCannotImportUnresolvedReferences() {
    QFETCH(QString, type); auto d = fixture(); auto &asset = std::get<RectangleAnnotation>(d.annotations[0]); asset.content.type = type; asset.content.imageKey = "absent";
    const auto inbound = parseAnnotationClipboard(payload(serializeAnnotationList({asset})));
    QVERIFY(!inbound.ok()); QVERIFY(inbound.annotations.empty()); QCOMPARE(inbound.diagnostic, QString("annotation-clipboard-assets-unavailable"));
  }

  void assetBearingOffersCannotOmitMissingPixels_data() { assetBearingClipboardPayloadsCannotImportUnresolvedReferences_data(); }
  void assetBearingOffersCannotOmitMissingPixels() {
    QFETCH(QString, type); auto d = fixture(); auto &asset = std::get<RectangleAnnotation>(d.annotations[0]); asset.content.type = type; asset.content.imageKey = "absent";
    const auto projected = selectedAnnotationTransfer(d, {asset.id}, true); QVERIFY(!projected.ok()); QVERIFY(projected.bytes.isEmpty());
    QCOMPARE(projected.diagnostic, QString("annotation-clipboard-assets-unavailable"));
  }

  void assetBearingCopyPasteCarriesPixelsToAnotherDocumentWithNewIdentitiesAndKeys_data() {
    QTest::addColumn<QString>("type"); for (const auto &type : {shape::image, shape::emoji, shape::cursor}) QTest::newRow(qPrintable(type)) << type;
  }
  void assetBearingCopyPasteCarriesPixelsToAnotherDocumentWithNewIdentitiesAndKeys() {
    QFETCH(QString, type);
    const auto mine = pngOf(Qt::green), other = pngOf(Qt::blue), privatePixels = pngOf(Qt::magenta, 9);
    auto source = fixture(); source.annotations = {assetObject(type, "k1"), rectangle(1)};
    source.embeddedImages = {{"k1", mine}, {"unselected", privatePixels}};
    const auto sourceId = annotationId(source.annotations[0]);
    const auto projected = selectedAnnotationTransfer(source, {sourceId}, true); QVERIFY2(projected.ok(), qPrintable(projected.diagnostic));
    QCOMPARE(projected.assets.size(), std::size_t(1)); QCOMPARE(projected.assets[0].first, QString("k1")); QCOMPARE(projected.assets[0].second, mine);
    QVERIFY(!projected.bytes.contains(privatePixels.toBase64()) && !projected.bytes.contains("unselected") && !projected.bytes.contains("PRIVATE"));
    QCOMPARE(QJsonDocument::fromJson(projected.bytes).object().value("version").toInt(), 2);
    const auto incoming = parseAnnotationClipboard(projected.bytes); QVERIFY2(incoming.ok(), qPrintable(incoming.diagnostic));
    QCOMPARE(incoming.assets, projected.assets); QCOMPARE(serializeAnnotationList(incoming.annotations), serializeAnnotationList(projected.annotations));
    // The target already uses the key k1 for different pixels: they must survive, and the paste must not alias them.
    auto target = fixture(); target.sourceImagePng = pngOf(Qt::white, 4); target.imageHash.clear(); target.createdAt = target.modifiedAt = "2026-11-03T00:00:00Z"; target.annotations = {rectangle(0)}; target.embeddedImages = {{"k1", other}}; EditorSession session(target); session.markSaved();
    const auto before = serializeDocument(session.document());
    const auto pasted = session.insertAnnotations(incoming.annotations, session.stateId(), 12, QUuid::createUuid, incoming.assets);
    QVERIFY2(pasted.changed, qPrintable(pasted.diagnostic)); QCOMPARE(session.undoCount(), 1);
    QCOMPARE(session.document().embeddedImages.size(), std::size_t(2)); QCOMPARE(session.document().embeddedImages[0], (std::pair<QString, QByteArray>{"k1", other}));
    const auto &copy = std::get<RectangleAnnotation>(session.document().annotations.back());
    QVERIFY(copy.id != sourceId); QCOMPARE(copy.type(), type); QVERIFY(copy.content.imageKey != "k1");
    QCOMPARE(copy.start.x, std::get<RectangleAnnotation>(source.annotations[0]).start.x + 12);
    const auto found = std::find_if(session.document().embeddedImages.begin(), session.document().embeddedImages.end(), [&](const auto &asset) { return asset.first == copy.content.imageKey; });
    QVERIFY(found != session.document().embeddedImages.end()); QCOMPARE(found->second, mine);
    // Durable: the saved document resolves every reference and reloads identically.
    const auto saved = serializeDocument(session.document()); const auto reloaded = parseDocument(saved); QVERIFY2(reloaded.document, qPrintable(reloaded.error.value_or(QString())));
    QCOMPARE(serializeDocument(*reloaded.document), saved); QCOMPARE(reloaded.document->embeddedImages.size(), session.document().embeddedImages.size());
    for (const auto &[key, bytes] : session.document().embeddedImages) {  // keyed members reload in key order
      const auto at = std::find_if(reloaded.document->embeddedImages.begin(), reloaded.document->embeddedImages.end(), [&](const auto &e) { return e.first == key; });
      QVERIFY(at != reloaded.document->embeddedImages.end()); QCOMPARE(at->second, bytes);
    }
    // Pasting the same payload again reuses the identical pixels instead of growing the document.
    const auto again = session.insertAnnotations(incoming.annotations, session.stateId(), 24, QUuid::createUuid, incoming.assets); QVERIFY(again.changed);
    QCOMPARE(session.document().embeddedImages.size(), std::size_t(2)); QVERIFY(again.insertedIds[0] != pasted.insertedIds[0]);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations.back()).content.imageKey, copy.content.imageKey);
    // One undo per paste removes the object and its newly added pixels together.
    QVERIFY(session.undo()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(session.document().embeddedImages.size(), std::size_t(2));
    // Same-document copy/paste (the contract scenario): both annotations reference durable data and their IDs differ.
    EditorSession same(source); same.select({sourceId});
    const auto samePaste = same.insertAnnotations(incoming.annotations, same.stateId(), 10, QUuid::createUuid, incoming.assets); QVERIFY(samePaste.changed);
    QCOMPARE(same.document().embeddedImages, source.embeddedImages);  // identical pixels share the existing key
    const auto &first = std::get<RectangleAnnotation>(same.document().annotations[0]); const auto &second = std::get<RectangleAnnotation>(same.document().annotations.back());
    QVERIFY(first.id != second.id); QCOMPARE(first.content.imageKey, second.content.imageKey);
  }
  void everyDrawableTypeSurvivesCopyAndPasteWithItsFieldsAndAssets() {
    // EA-016: every supported object type is copied and pasted whole; only identity, position and asset key may change.
    const auto png = pngOf(Qt::green); auto source = fixture(); source.annotations.clear(); source.embeddedImages = {{"a", png}};
    int i = 0;
    for (const auto &type : shapeTypes()) {
      auto r = rectangle(i++ % 8); r.content.type = type;
      if (isPathShape(type)) r.content.points = {{0, 0}, {0.5, 1}, {1, 0.25}};
      if (isTextShape(type) && type != shape::step) { r.content.text = "Text " + type; r.content.fontSize = 21.5; r.content.textAlignment = "right"; r.content.textColor = 0x80010203; r.content.textFillColor = 0x10203040; r.content.textBold = true; r.content.textItalic = false; }
      if (type == shape::speechBalloon) { r.content.points = {{0.3, 1.4}}; r.content.balloonBorderColor = 0x40A0B0C0; }
      if (type == shape::text) r.content.textBorderColor = 0x7F332211;
      if (type == shape::step) { r.content.stepValue = 27; r.content.stepStyle = StepStyle::UppercaseLetter; }
      if (hasShapeStrength(type)) r.content.strength = 9.5;
      if (hasShapeEllipse(type)) r.content.isEllipse = true;
      if (hasShapeBlurAmount(type)) r.content.blurAmount = 4.5;
      if (isAssetShape(type)) r.content.imageKey = "a";
      source.annotations.push_back(r);
    }
    QSet<QUuid> all; for (const auto &a : source.annotations) all.insert(annotationId(a));
    const auto offer = selectedAnnotationTransfer(source, all, true); QVERIFY2(offer.ok(), qPrintable(offer.diagnostic));
    QCOMPARE(offer.annotations.size(), std::size_t(17)); QCOMPARE(offer.assets.size(), std::size_t(1));  // the three asset objects share one PNG
    const auto incoming = parseAnnotationClipboard(offer.bytes); QVERIFY2(incoming.ok(), qPrintable(incoming.diagnostic));
    auto target = fixture(); target.annotations.clear(); target.sourceImagePng = pngOf(Qt::white, 4); target.imageHash.clear(); target.createdAt = target.modifiedAt = "2026-11-03T00:00:00Z";
    EditorSession session(target); const auto pasted = session.insertAnnotations(incoming.annotations, session.stateId(), 15, QUuid::createUuid, incoming.assets);
    QVERIFY2(pasted.changed, qPrintable(pasted.diagnostic)); QCOMPARE(session.undoCount(), 1); QCOMPARE(pasted.insertedIds.size(), 17);
    QCOMPARE(session.document().embeddedImages.size(), std::size_t(1));
    for (std::size_t n = 0; n < 17; ++n) {
      auto want = std::get<RectangleAnnotation>(source.annotations[n]); auto got = std::get<RectangleAnnotation>(session.document().annotations[n]);
      QVERIFY(got.id != want.id); QCOMPARE(got.start.x, want.start.x + 15); QCOMPARE(got.end.y, want.end.y + 15);
      if (isAssetShape(want.type())) QCOMPARE(got.content.imageKey, session.document().embeddedImages[0].first);
      got.id = want.id; got.start = want.start; got.end = want.end; got.content.imageKey = want.content.imageKey;
      QCOMPARE(serializeAnnotationList({got}), serializeAnnotationList({want}));  // every other field, extra data included
    }
  }
  void cutOfAssetObjectsIsOfferedAndRemovesOnlyTheObjects() {
    auto d = fixture(); d.annotations = {assetObject(shape::image, "k1"), rectangle(1)}; d.embeddedImages = {{"k1", pngOf(Qt::green)}};
    EditorSession session(d); session.select({annotationId(d.annotations[0])});
    const auto cut = session.cutAnnotations(session.stateId(), session.selection()); QVERIFY2(cut.changed, qPrintable(cut.diagnostic));
    QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(session.document().embeddedImages, d.embeddedImages);
  }
  void untrustedAssetPayloadsAreRejectedBeforeAnyInsertion_data() {
    QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("diagnostic");
    const auto good = pngOf(Qt::green); const auto object = assetObject(shape::image, "k"); const std::vector<Annotation> one{object};
    const auto asset = [](const QByteArray &png) { return QJsonObject{{"k", QString::fromLatin1(png.toBase64())}}; };
    QTest::newRow("valid-control") << assetPayload(one, asset(good)) << QString();
    QTest::newRow("not-base64") << assetPayload(one, QJsonObject{{"k", "@@@not base64@@@"}}) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("not-a-png") << assetPayload(one, asset("GIF89a plain text pretending to be an image....")) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("truncated-no-iend") << assetPayload(one, asset(good.chopped(12))) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("zero-width") << assetPayload(one, asset(shellPng(0, 4))) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("beyond-document-limit") << assetPayload(one, asset(shellPng(200000, 1))) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("beyond-decode-limit") << assetPayload(one, asset(shellPng(16000, 16000))) << QString("annotation-clipboard-asset-invalid");
    QTest::newRow("per-asset-size-cap") << assetPayload(one, asset(shellPng(4, 4, kMaxClipboardAssetPngBytes))) << QString("annotation-transfer-too-large");
    QTest::newRow("unreferenced-private-asset") << assetPayload(one, QJsonObject{{"k", QString::fromLatin1(good.toBase64())}, {"extra", QString::fromLatin1(good.toBase64())}}) << QString("annotation-transfer-invalid");
    QTest::newRow("reference-without-asset") << assetPayload(one, QJsonObject{{"other", QString::fromLatin1(good.toBase64())}}) << QString("annotation-clipboard-assets-unavailable");
    QTest::newRow("empty-assets") << assetPayload(one, {}) << QString("annotation-transfer-invalid");
    QTest::newRow("assets-not-an-object") << QJsonDocument(QJsonObject{{"version", 2}, {"annotations", serializeAnnotationList(one)}, {"assets", QJsonArray{"x"}}}).toJson(QJsonDocument::Compact) << QString("annotation-transfer-invalid");
    QTest::newRow("version-1-with-assets") << assetPayload(one, asset(good), 1) << QString("annotation-transfer-invalid");
    QTest::newRow("version-3") << assetPayload(one, asset(good), 3) << QString("annotation-transfer-invalid");
    QTest::newRow("asset-value-not-string") << assetPayload(one, QJsonObject{{"k", 5}}) << QString("annotation-transfer-invalid");
    QTest::newRow("non-canonical-json") << QJsonDocument(QJsonDocument::fromJson(assetPayload(one, asset(good)))).toJson(QJsonDocument::Indented) << QString("annotation-transfer-invalid");
    QTest::newRow("whole-payload-cap") << QByteArray(kMaxAnnotationAssetClipboardBytes + 1, ' ') << QString("annotation-transfer-too-large");
    QJsonObject many; std::vector<Annotation> objects;
    for (std::size_t i = 0; i <= kMaxClipboardAssets; ++i) { const auto key = QString("k%1").arg(i); many.insert(key, QString::fromLatin1(good.toBase64())); objects.push_back(assetObject(shape::image, key, int(i % 10))); }
    QTest::newRow("too-many-assets") << assetPayload(objects, many) << QString("annotation-transfer-too-large");
  }
  void untrustedAssetPayloadsAreRejectedBeforeAnyInsertion() {
    QFETCH(QByteArray, bytes); QFETCH(QString, diagnostic);
    const auto parsed = parseAnnotationClipboard(bytes);
    if (diagnostic.isEmpty()) { QVERIFY2(parsed.ok(), qPrintable(parsed.diagnostic)); QCOMPARE(parsed.assets.size(), std::size_t(1)); return; }
    QVERIFY(!parsed.ok()); QCOMPARE(parsed.diagnostic, diagnostic); QVERIFY(parsed.annotations.empty()); QVERIFY(parsed.assets.empty());
  }
  void assetsThatBypassThePasteParserAreStillRevalidatedByTheSession() {
    auto d = fixture(); d.annotations = {rectangle(0)}; EditorSession session(d); const auto before = serializeDocument(session.document());
    const std::vector<Annotation> one{assetObject(shape::emoji, "k")};
    for (const auto &bad : {QByteArray("junk"), shellPng(16000, 16000), pngOf(Qt::red).chopped(8)}) {
      const auto result = session.insertAnnotations(one, session.stateId(), 10, QUuid::createUuid, {{"k", bad}});
      QVERIFY(!result.changed); QCOMPARE(result.diagnostic, QString("annotation-clipboard-asset-invalid"));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
    }
  }
  void offersRefuseMissingInvalidAndOversizedSelectedAssets() {
    auto d = fixture(); d.annotations = {assetObject(shape::image, "k")};
    const auto id = annotationId(d.annotations[0]);
    d.embeddedImages = {{"k", "not a png"}}; QCOMPARE(selectedAnnotationTransfer(d, {id}, true).diagnostic, QString("annotation-clipboard-asset-invalid"));
    d.embeddedImages = {{"k", shellPng(4, 4, kMaxClipboardAssetPngBytes)}}; QCOMPARE(selectedAnnotationTransfer(d, {id}, true).diagnostic, QString("annotation-transfer-too-large"));
    d.embeddedImages = {}; QCOMPARE(selectedAnnotationTransfer(d, {id}, true).diagnostic, QString("annotation-clipboard-assets-unavailable"));
    // The whole payload is bounded too: four 6 MiB assets stay under the per-asset cap but over half the payload cap.
    d.annotations.clear(); d.embeddedImages.clear(); QSet<QUuid> all;
    for (int i = 0; i < 4; ++i) { auto r = assetObject(shape::image, QString("k%1").arg(i), i); d.annotations.push_back(r); d.embeddedImages.emplace_back(r.content.imageKey, shellPng(4, 4, 6 * 1024 * 1024)); all.insert(r.id); }
    const auto result = selectedAnnotationTransfer(d, all, true); QVERIFY(!result.ok()); QCOMPARE(result.diagnostic, QString("annotation-transfer-too-large")); QVERIFY(result.bytes.isEmpty() && result.assets.empty());
    // Duplication does not use the clipboard and still copes with anything already in the document.
    EditorSession session(d); session.select({all.values().first()}); QVERIFY(session.duplicateSelection().changed);
  }

  void inDocumentAssetDuplicationRetainsPixelsAndHistory_data() { assetBearingClipboardPayloadsCannotImportUnresolvedReferences_data(); }
  void inDocumentAssetDuplicationRetainsPixelsAndHistory() {
    QFETCH(QString, type); auto d = fixture(); auto &asset = std::get<RectangleAnnotation>(d.annotations[0]); asset.content.type = type; asset.content.imageKey = "retained";
    d.embeddedImages = {{"retained", "EXACT retained pixels"}, {"other", "UNRELATED pixels"}};
    EditorSession session(d); session.select({asset.id}); const auto before = serializeDocument(session.document());
    const auto duplicated = session.duplicateSelection(); QVERIFY(duplicated.changed); QCOMPARE(duplicated.insertedIds.size(), 1);
    QCOMPARE(session.document().embeddedImages, d.embeddedImages); QCOMPARE(session.undoCount(), 1);
    const auto &copy = std::get<RectangleAnnotation>(session.document().annotations.back()); QCOMPARE(copy.type(), type); QCOMPARE(copy.content.imageKey, asset.content.imageKey); QVERIFY(copy.id != asset.id);
    const auto after = serializeDocument(session.document()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), after);
  }

  void projectionPreservesSelectedOrderFieldsAndExcludesDocumentAndSourceData() {
    const auto d = fixture(); const auto first = annotationId(d.annotations[0]), last = annotationId(d.annotations[2]);
    const auto projected = selectedAnnotationTransfer(d, {last, first}, true); QVERIFY(projected.ok()); QVERIFY(!projected.bytes.contains("PRIVATE") && !projected.bytes.contains("EXCLUDE"));
    QCOMPARE(annotationId(projected.annotations[0]), first); QCOMPARE(annotationId(projected.annotations[1]), last);
    const auto parsed = parseAnnotationClipboard(projected.bytes); QVERIFY(parsed.ok()); QCOMPARE(serializeAnnotationList(parsed.annotations), serializeAnnotationList(projected.annotations));
    QCOMPARE(projected.bytes, payload(serializeAnnotationList(projected.annotations)));
  }
  void duplicateRetainsAssetsAndCommitsOneHistoryOperationWithExactUndoRedoAndSelection() {
    auto d = fixture(); d.embeddedImages = {{"preserved", "private embedded bytes"}}; EditorSession session(d); session.select({annotationId(d.annotations[2]), annotationId(d.annotations[0])});
    const auto selected = session.selection(); session.markSaved(); const auto before = serializeDocument(session.document());
    const auto result = session.duplicateSelection(); QVERIFY(result.changed); QCOMPARE(result.insertedIds.size(), 2); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    const auto after = serializeDocument(session.document()); QCOMPARE(session.document().annotations.size(), size_t(5)); QCOMPARE(session.document().embeddedImages, d.embeddedImages);
    QCOMPARE(session.selection(), QSet<QUuid>(result.insertedIds.begin(), result.insertedIds.end()));
    for (int n = 0; n < 2; ++n) {
      const auto &copy = std::get<RectangleAnnotation>(session.document().annotations[3 + n]); const auto &original = std::get<RectangleAnnotation>(d.annotations[n ? 2 : 0]);
      QVERIFY(copy.id != original.id); QCOMPARE(copy.start.x, original.start.x + 10); QCOMPARE(copy.end.y, original.end.y + 10); QCOMPARE(copy.style.opacity, original.style.opacity); QCOMPARE(copy.extra, original.extra);
    }
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selected); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), after); QVERIFY(session.dirty()); QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.undo()); QVERIFY(session.duplicateSelection().changed); QVERIFY(!session.redo());
  }
  void pasteAcrossDocumentsPreservesTargetSourceDefaultsAndHasNewStableIdentities() {
    const auto source = fixture(); const auto projected = selectedAnnotationTransfer(source, {annotationId(source.annotations[1])}, true); QVERIFY(projected.ok());
    auto target = fixture(); target.annotations.clear(); target.embeddedImages = {{"target-only", "KEEP"}}; EditorSession session(target); const auto style = session.toolStyle();
    const auto parsed = parseAnnotationClipboard(projected.bytes); QVERIFY(parsed.ok()); const auto first = session.insertAnnotations(parsed.annotations, session.stateId()); QVERIFY(first.changed);
    const auto second = session.insertAnnotations(parsed.annotations, session.stateId(), 20); QVERIFY(second.changed); QVERIFY(first.insertedIds[0] != second.insertedIds[0]);
    QVERIFY(first.insertedIds[0] != annotationId(source.annotations[1])); QCOMPARE(session.document().sourceImagePng, target.sourceImagePng); QCOMPARE(session.document().imagePath, target.imagePath); QCOMPARE(session.document().embeddedImages, target.embeddedImages);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[1]).start.x, 40.0); QCOMPARE(session.toolStyle().strokeWidth, style.strokeWidth); QCOMPARE(session.undoCount(), 2);
    const auto id = second.insertedIds[0]; QVERIFY(session.undo()); QVERIFY(session.redo()); QCOMPARE(annotationId(session.document().annotations[1]), id);
  }
  void protectedUnknownAssetAndEmptySelectionsNeverProducePartialProjection_data() { QTest::addColumn<int>("kind"); for (int i = 0; i < 6; ++i) QTest::newRow(qPrintable(QString::number(i))) << i; }
  void protectedUnknownAssetAndEmptySelectionsNeverProducePartialProjection() {
    QFETCH(int, kind); auto d = fixture(); auto &r = std::get<RectangleAnnotation>(d.annotations[1]);
    if (kind == 0) r.visible = false; if (kind == 1) r.extra.insert("locked", true); if (kind == 2) d.annotations[1] = UnsupportedAnnotation{r.id, {{"type", "future"}}};
    if (kind == 3) { r.content.type = shape::image; r.content.imageKey = "selected-asset"; d.embeddedImages = {{"selected-asset", "KEEP"}}; } if (kind == 4) r.start.x = std::numeric_limits<double>::infinity();
    const auto before = serializeDocument(d); const auto selection = kind == 5 ? QSet<QUuid>{} : QSet<QUuid>{annotationId(d.annotations[0]), annotationId(d.annotations[1])};
    const auto result = selectedAnnotationTransfer(d, selection, true); QVERIFY(!result.ok()); QVERIFY(result.bytes.isEmpty() && result.annotations.empty()); QCOMPARE(serializeDocument(d), before);
    EditorSession session(d); session.select(selection.values());
    if (kind != 3) { QVERIFY(!session.duplicateSelection().changed); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.selection(), selection); }
  }
  void directSelectionCommandsCannotMutateHiddenLockedOrOpaqueProtectedObjects_data() { QTest::addColumn<int>("kind"); for (int i = 0; i < 3; ++i) QTest::newRow(qPrintable(QString::number(i))) << i; }
  void directSelectionCommandsCannotMutateHiddenLockedOrOpaqueProtectedObjects() {
    QFETCH(int, kind); auto d = fixture(); auto &r = std::get<RectangleAnnotation>(d.annotations[1]);
    if (!kind) r.visible = false; else r.extra.insert("locked", kind == 1 ? QJsonValue(true) : QJsonValue("opaque"));
    EditorSession session(d); session.select({annotationId(d.annotations[0]), r.id}); session.markSaved(); const auto before = serializeDocument(d); const auto selected = session.selection();
    QVERIFY(!session.selectionEditable()); QVERIFY(!session.moveSelection(2, 3)); QVERIFY(!session.resizeSelection(3, 4)); QVERIFY(!session.rotateSelection(10));
    QVERIFY(!session.setOpacity(.1)); QVERIFY(!session.setStrokeWidth(8)); QVERIFY(!session.setStrokeColor(0xFF001122)); QVERIFY(!session.setFillColor(0xFF334455));
    QVERIFY(!session.reorderSelection(EditorSession::Order::Front)); QVERIFY(!session.deleteSelection()); QVERIFY(!session.duplicateSelection().changed);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selected); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void largeMetadataAndOversizedBatchesCannotBeEncodedOrPartiallyCopied() {
    auto d = fixture(); auto &r = std::get<RectangleAnnotation>(d.annotations[0]); r.extra.insert("metadata", QString(kMaxAnnotationClipboardBytes, QLatin1Char('x')));
    const auto large = selectedAnnotationTransfer(d, {r.id}, true); QVERIFY(!large.ok() && large.bytes.isEmpty()); QCOMPARE(large.diagnostic, QString("annotation-transfer-too-large"));
    std::vector<Annotation> objects; for (size_t i = 0; i <= kMaxAnnotationTransferObjects; ++i) objects.push_back(rectangle(0));
    const auto bytes = payload(serializeAnnotationList(objects)); QVERIFY(!parseAnnotationClipboard(bytes).ok());
  }
  void invalidClipboardPayloadsAndLimitsFailBeforeAnyInsertion_data() {
    QTest::addColumn<int>("kind"); for (int i = 0; i < 10; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void invalidClipboardPayloadsAndLimitsFailBeforeAnyInsertion() {
    QFETCH(int, kind); auto objects = serializeAnnotationList(fixture().annotations); QByteArray bytes;
    if (kind == 0) bytes = "garbage";
    if (kind == 1) bytes = "[]";
    if (kind == 2) { objects.append(objects.first()); bytes = payload(objects); }
    if (kind == 3) { auto o = objects[0].toObject(); o.insert("type", "future"); objects[0] = o; bytes = payload(objects); }
    if (kind == 4) { auto o = objects[0].toObject(); o.insert("strokeWidth", 0); objects[0] = o; bytes = payload(objects); }
    if (kind == 5) { auto o = objects[0].toObject(); o.insert("id", QUuid{}.toString(QUuid::WithoutBraces)); objects[0] = o; bytes = payload(objects); }
    if (kind == 6) bytes = QByteArray(kMaxAnnotationClipboardBytes + 1, ' ');
    if (kind == 7) { auto root = QJsonObject{{"version", 1}, {"annotations", objects}, {"embeddedImages", QJsonObject{{"secret", "PRIVATE"}}}}; bytes = QJsonDocument(root).toJson(QJsonDocument::Compact); }
    if (kind == 8) { bytes = payload(objects); bytes.chop(1); bytes.append(",\"version\":1}"); }
    if (kind == 9) { QJsonValue nested("private"); for (int i = 0; i < 35; ++i) nested = QJsonArray{nested}; auto o = objects[0].toObject(); o.insert("extra", nested); objects[0] = o; bytes = payload(objects); }
    const auto result = parseAnnotationClipboard(bytes); QVERIFY(!result.ok()); QVERIFY(result.annotations.empty());
  }
  void everyAdmittedEnvelopeAtTheEncodingBudgetBoundaryCanBePasted() {
    auto d = fixture(); d.annotations.resize(1); const auto id = annotationId(d.annotations[0]);
    int low = 0, high = kMaxAnnotationClipboardBytes / 6;
    while (low < high) {
      const int size = low + (high - low + 1) / 2; std::get<RectangleAnnotation>(d.annotations[0]).extra.insert("metadata", QString(size, QLatin1Char('x')));
      if (selectedAnnotationTransfer(d, {id}, true).ok()) low = size; else high = size - 1;
    }
    QVERIFY(low > 0); std::get<RectangleAnnotation>(d.annotations[0]).extra.insert("metadata", QString(low, QLatin1Char('x')));
    const auto offer = selectedAnnotationTransfer(d, {id}, true); QVERIFY(offer.ok()); const auto decoded = parseAnnotationClipboard(offer.bytes); QVERIFY(decoded.ok());
    QCOMPARE(serializeAnnotationList(decoded.annotations), serializeAnnotationList(offer.annotations));
    std::get<RectangleAnnotation>(d.annotations[0]).extra.insert("metadata", QString(low + 1, QLatin1Char('x'))); QVERIFY(!selectedAnnotationTransfer(d, {id}, true).ok());
  }
  void failedInsertionIdentityOverflowAndStaleCutPreserveSelectionCheckpointAndRedo() {
    const auto d = fixture(); EditorSession session(d); session.select({annotationId(d.annotations[0])}); QVERIFY(session.duplicateSelection().changed); QVERIFY(session.undo()); session.markSaved();
    const auto before = serializeDocument(session.document()); const auto selection = session.selection(); const auto state = session.stateId(); const auto list = selectedAnnotationTransfer(d, {annotationId(d.annotations[0])}, false).annotations;
    QVERIFY(!session.insertAnnotations(list, state + 100).changed); QVERIFY(!session.insertAnnotations(list, state, 10, [] { return QUuid{}; }).changed);
    QVERIFY(!session.insertAnnotations(list, state, 10, [&] { return annotationId(d.annotations[1]); }).changed); QVERIFY(!session.insertAnnotations(list, state, std::numeric_limits<double>::infinity()).changed);
    auto tooFar = list; auto &r = std::get<RectangleAnnotation>(tooFar[0]); r.start.x = 99990; r.end.x = 100000; QVERIFY(!session.insertAnnotations(tooFar, state).changed);
    QVERIFY(!session.cutAnnotations(state + 1, selection).changed); QVERIFY(!session.cutAnnotations(state, {}).changed);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QCOMPARE(session.stateId(), state); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    const auto cut = session.cutAnnotations(state, selection); QVERIFY(cut.changed); QCOMPARE(session.document().annotations.size(), size_t(2)); QVERIFY(session.selection().isEmpty());
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QVERIFY(session.redo()); QCOMPARE(session.document().annotations.size(), size_t(2));
  }
};
QTEST_APPLESS_MAIN(AnnotationTransferTest)
#include "AnnotationTransferTest.moc"
