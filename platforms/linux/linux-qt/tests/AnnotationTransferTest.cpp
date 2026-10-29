#include "image-editor/EditorSession.h"
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
