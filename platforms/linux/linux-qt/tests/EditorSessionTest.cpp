// Production-path checks that the conformance vectors cannot cover: the .xann
// gzip container and its decompression limit, PNG header checks, save
// outcomes, sidecar writing, colour case, and rectangle rendering.

#include "image-editor/AnnotationRenderer.h"
#include "image-editor/EditorSession.h"
#include "image-editor/SourceBinding.h"

#include <QJsonArray>
#include <QTest>

#include <map>
#include <limits>

using namespace xerahs::editor;

namespace {

const QByteArray kOnePixelPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mP4DwQACfsD/Wj6HMwAAAAASUVORK5CYII=");

AnnotationDocument document(qint64 width = 10, qint64 height = 10) {
  AnnotationDocument d;
  d.canvasWidth = width;
  d.canvasHeight = height;
  d.createdAt = d.modifiedAt = QStringLiteral("2026-10-08T00:00:00Z");
  d.sourceImagePng = kOnePixelPng;
  return d;
}

RectangleAnnotation rectangle(double l, double t, double r, double b) {
  RectangleAnnotation a;
  a.id = QUuid::createUuid();
  a.start = {l, t};
  a.end = {r, b};
  return a;
}

// A PNG header claiming the given size, with no pixel data.
QByteArray pngHeader(quint32 width, quint32 height) {
  QByteArray png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR", 16);
  for (quint32 v : {width, height}) {
    for (int shift = 24; shift >= 0; shift -= 8) png.append(static_cast<char>((v >> shift) & 0xFF));
  }
  return png + QByteArray(5, '\0');
}

class MapStore final : public SidecarStore {
public:
  std::map<QString, QByteArray> files;
  bool failWrites = false;
  bool exists(const QString &path) override { return files.count(path) > 0; }
  bool write(const QString &path, const QByteArray &bytes) override {
    if (failWrites) return false;
    files[path] = bytes;
    return true;
  }
  bool remove(const QString &path) override { return files.erase(path) > 0; }
};

}  // namespace

class EditorSessionTest : public QObject {
  Q_OBJECT

private slots:
  void guardedCanvasCommitPreservesSelectionHistoryAndRefusesStaleAdmission_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"valid", "stale-state", "stale-selection", "invalid-source"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedCanvasCommitPreservesSelectionHistoryAndRefusesStaleAdmission() {
    QFETCH(QString, mode); auto d = document(1, 1); auto a = rectangle(0, 0, 1, 1), b = rectangle(.1, .1, .9, .9); d.annotations = {a, b}; d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "root"}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); const auto state = session.stateId(); const auto selection = session.selection(); const auto original = serializeDocument(session.document());
    auto next = session.document(); next.annotations = {b}; if (mode == "invalid-source") next.sourceImagePng = "invalid";
    const bool accepted = session.commitCanvas(next, mode == "stale-state" ? state + 1 : state, mode == "stale-selection" ? QSet<QUuid>{b.id} : selection);
    QCOMPARE(accepted, mode == "valid");
    if (!accepted) { QCOMPARE(serializeDocument(session.document()), original); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty()); QCOMPARE(session.editDiagnostic(), mode == "invalid-source" ? diagnostic::documentInvalid : QString("annotation-edit-stale")); }
    else { QCOMPARE(serializeDocument(session.document()), serializeDocument(next)); QVERIFY(session.selection().isEmpty()); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), original); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(next)); }
    QCOMPARE(session.document().embeddedImages, d.embeddedImages); QCOMPARE(session.document().extra, d.extra);
  }
  void guardedHistoryAdmitsOnlyTheFrozenStateSelectionAndTarget_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"undo", "redo", "stale-state", "stale-selection", "stale-target", "replaced-redo", "missing-target"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedHistoryAdmitsOnlyTheFrozenStateSelectionAndTarget() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 4, 4); a.extra = {{"vendor", "retained"}}; d.annotations = {a}; d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "root"}};
    EditorSession session(d); session.select({a.id}); QVERIFY(!session.historyTargetId(false)); QVERIFY(!session.historyTargetId(true)); QVERIFY(session.moveSelection(2, 3));
    const auto moved = serializeDocument(session.document()); const auto movedId = session.stateId();
    const bool redo = mode == "redo" || mode == "replaced-redo";
    if (redo || mode == "missing-target") QVERIFY(session.undo());
    const auto state = session.stateId(); const auto selection = session.selection();
    const auto target = session.historyTargetId(redo).value_or(0);
    if (mode == "replaced-redo") { QVERIFY(session.moveSelection(4, 0)); QVERIFY(session.undo()); QCOMPARE(session.stateId(), state); QCOMPARE(session.selection(), selection); QVERIFY(session.historyTargetId(true) != target); }
    const auto before = serializeDocument(session.document()); const auto undoCount = session.undoCount(), redoCount = session.redoCount(); const bool dirty = session.dirty();
    const bool accepted = session.restoreHistory(redo, mode == "stale-state" ? state + 100 : state, mode == "stale-selection" ? QSet<QUuid>{} : selection, mode == "stale-target" ? target + 100 : target);
    QCOMPARE(accepted, mode == "undo" || mode == "redo");
    if (accepted) {
      QCOMPARE(serializeDocument(session.document()), redo ? moved : serializeDocument(d)); QCOMPARE(session.selection(), selection); QCOMPARE(session.dirty(), redo); QCOMPARE(session.stateId(), redo ? movedId : quint64(0)); QVERIFY(session.editDiagnostic().isEmpty());
    } else {
      QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale")); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), undoCount); QCOMPARE(session.redoCount(), redoCount); QCOMPARE(session.dirty(), dirty);
    }
    QCOMPARE(session.document().embeddedImages, d.embeddedImages); QCOMPARE(session.document().extra, d.extra);
  }
  void emptySidecarSaveRefusesToExposeBaselineCompanion_data() {
    QTest::addColumn<bool>("defaultExists"); QTest::addColumn<bool>("intent");
    for (bool exists : {false, true}) for (bool intent : {false, true}) QTest::newRow(qPrintable(QString("%1-%2").arg(exists).arg(intent))) << exists << intent;
  }
  void emptySidecarSaveRefusesToExposeBaselineCompanion() {
    QFETCH(bool, defaultExists); QFETCH(bool, intent); MapStore store; store.files["dir.with.dot/shot.xann"] = "LEGACY"; if (defaultExists) store.files["dir.with.dot/shot.png.xann"] = "DEFAULT"; const auto before = store.files;
    const auto saved = saveSidecar(store, "dir.with.dot/shot.png", document(), intent); QCOMPARE(saved.ok, !intent); QVERIFY(!saved.sidecarPath); QCOMPARE(saved.diagnostic, intent ? std::optional<QString>(diagnostic::sidecarSaveFailed) : std::nullopt); QCOMPARE(store.files, before);
  }
  void guardedDeleteKeepsRemainingPayloadAssetsAndExactHistory_data() {
    QTest::addColumn<QString>("selected"); QTest::addColumn<QString>("remaining");
    QTest::newRow("single") << QString("1") << QString("0234");
    QTest::newRow("mixed") << QString("02") << QString("134");
    QTest::newRow("opaque") << QString("2") << QString("0134");
    QTest::newRow("all") << QString("01234") << QString("");
  }
  void guardedDeleteKeepsRemainingPayloadAssetsAndExactHistory() {
    QFETCH(QString, selected); QFETCH(QString, remaining); auto d = document();
    for (int i = 0; i < 5; ++i) { auto r = rectangle(i, 1, i + 2, 5); r.extra = {{"vendor", i}}; d.annotations.push_back(r); }
    for (size_t i : {size_t(2), size_t(4)}) { const auto id = annotationId(d.annotations[i]); d.annotations[i] = UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"zIndex", 99}, {"payload", QJsonArray{42, "keep"}}}}; }
    d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "root"}}; EditorSession session(d); auto defaults = session.toolStyle(); defaults.strokeWidth = 7.5; defaults.fillColor = 0x40112233; QVERIFY(session.setCreationStyle(defaults));
    session.select({annotationId(d.annotations[0])}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); QList<QUuid> ids; for (auto c : selected) ids.append(annotationId(d.annotations[c.digitValue()])); session.select(ids); const auto selection = session.selection();
    QVERIFY(session.canDeleteSelection()); QVERIFY(session.deleteSelection(session.stateId(), selection)); QVERIFY(session.editDiagnostic().isEmpty()); auto expected = d; expected.annotations.clear(); for (auto c : remaining) expected.annotations.push_back(d.annotations[c.digitValue()]);
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QVERIFY(session.selection().isEmpty()); QVERIFY(!session.canDeleteSelection()); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty());
    QCOMPARE(session.toolStyle().strokeWidth, defaults.strokeWidth); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QVERIFY(session.selection().isEmpty()); QCOMPARE(session.document().embeddedImages, d.embeddedImages);
  }
  void guardedDeleteRefusalPreservesCheckpointRedoAndSelection_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"empty", "hidden", "locked", "opaque-hidden", "opaque-lock", "stale-state", "stale-selection"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedDeleteRefusalPreservesCheckpointRedoAndSelection() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 3, 3), b = rectangle(4, 4, 8, 8); if (mode == "hidden") b.visible = false; if (mode == "locked") b.extra = {{"locked", true}}; d.annotations = {a, b};
    if (mode.startsWith("opaque-")) d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {mode == "opaque-hidden" ? "visible" : "locked", mode == "opaque-hidden" ? QJsonValue(false) : QJsonValue("future-lock")}}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select(mode == "empty" ? QList<QUuid>{} : QList<QUuid>{a.id, b.id});
    auto state = session.stateId(); const auto selected = session.selection(); if (mode == "stale-state") ++state; if (mode == "stale-selection") session.select({a.id}); const auto current = session.selection();
    QVERIFY(!session.deleteSelection(state, selected)); QCOMPARE(session.editDiagnostic(), mode.startsWith("stale-") ? QString("annotation-edit-stale") : mode == "empty" ? QString{} : QString("annotation-selection-protected"));
    QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), current); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
  }
  void guardedOrderingPreservesGroupPayloadAndHistory_data() {
    QTest::addColumn<int>("order"); QTest::addColumn<QString>("selected"); QTest::addColumn<QString>("indices");
    // Independently declared array orders, including an opaque-only selection.
    const QStringList group{"02143", "10324", "02413", "13024"};
    const QStringList single{"02134", "10234", "02341", "10234"};
    const QStringList opaque{"01324", "02134", "01342", "20134"};
    for (int order = 0; order < 4; ++order) for (int selection = 0; selection < 3; ++selection)
      QTest::newRow(qPrintable(QString("%1-%2").arg(order).arg(selection))) << order << QString(selection == 0 ? "13" : selection == 1 ? "1" : "2") << (selection == 0 ? group[order] : selection == 1 ? single[order] : opaque[order]);
  }
  void guardedOrderingPreservesGroupPayloadAndHistory() {
    QFETCH(int, order); QFETCH(QString, selected); QFETCH(QString, indices); auto d = document();
    for (int i = 0; i < 5; ++i) { auto r = rectangle(i, 1, i + 2, 4); r.style = {0x80112233, 0x40223344, 2.5, 37.25, .42}; r.extra = {{"vendor", i}}; d.annotations.push_back(r); }
    const auto opaqueId = annotationId(d.annotations[2]); d.annotations[2] = UnsupportedAnnotation{opaqueId, {{"id", opaqueId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"zIndex", 99}, {"payload", QJsonArray{42, "keep"}}}};
    d.extra = {{"vendor", "root"}}; d.embeddedImages = {{"asset", kOnePixelPng}}; EditorSession session(d);
    session.select({annotationId(d.annotations[0])}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); QList<QUuid> ids;
    for (auto c : selected) ids.append(annotationId(d.annotations[c.digitValue()])); session.select(ids); const auto selection = session.selection();
    const auto defaults = session.toolStyle(); QVERIFY(session.canOrderSelection());
    QVERIFY(session.reorderSelection(EditorSession::Order(order), session.stateId(), selection)); QVERIFY(session.editDiagnostic().isEmpty());
    auto expected = d; expected.annotations.clear(); for (auto c : indices) expected.annotations.push_back(d.annotations[c.digitValue()]);
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty());
    QCOMPARE(session.toolStyle().strokeWidth, defaults.strokeWidth); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection);
  }
  void orderingNoopAndRefusalKeepCheckpointAndRedo_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"empty", "all", "front", "back", "forward-edge", "backward-edge", "hidden", "locked", "opaque-lock", "stale-state", "stale-selection", "invalid"}) QTest::newRow(mode) << QString(mode);
  }
  void orderingNoopAndRefusalKeepCheckpointAndRedo() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 3, 3), b = rectangle(4, 4, 7, 7);
    if (mode == "hidden") b.visible = false; if (mode == "locked") b.extra = {{"locked", true}};
    d.annotations = {a, b}; if (mode == "opaque-lock") d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"locked", "opaque"}}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    const bool group = mode == "all" || mode == "hidden" || mode.contains("lock"); session.select(group ? QList<QUuid>{a.id, b.id} : QList<QUuid>{b.id});
    auto order = EditorSession::Order::Front; if (mode == "back" || mode == "backward-edge") { session.select({a.id}); order = mode == "back" ? EditorSession::Order::Back : EditorSession::Order::Backward; }
    if (mode == "forward-edge") order = EditorSession::Order::Forward; if (mode == "empty") session.select({});
    auto state = session.stateId(); const auto selection = session.selection(); if (mode == "stale-state") ++state; if (mode == "stale-selection") session.select({a.id});
    if (mode == "invalid") order = EditorSession::Order(99); const auto before = serializeDocument(session.document()); const auto actualSelection = session.selection();
    const bool protectedGroup = mode == "hidden" || mode.contains("lock"); QCOMPARE(session.canOrderSelection(), !protectedGroup && mode != "empty");
    QVERIFY(!session.reorderSelection(order, state, selection)); const QString diagnostic = mode.startsWith("stale-") ? "annotation-edit-stale" : mode == "invalid" ? "annotation-order-invalid" : protectedGroup ? "annotation-selection-protected" : "";
    QCOMPARE(session.editDiagnostic(), diagnostic); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), actualSelection); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
  }
  void guardedThicknessCommitsOneGroupAndSnapshotsDefaults_data() {
    QTest::addColumn<double>("width"); for (double width : {.25, 1.0, 7.5, 10000.0}) QTest::newRow(qPrintable(QString::number(width))) << width;
  }
  void guardedThicknessCommitsOneGroupAndSnapshotsDefaults() {
    QFETCH(double, width); auto d = document(); auto a = rectangle(1, 2, 4, 6), b = rectangle(5, 6, 9, 10); a.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; a.extra = {{"vendor", "keep"}};
    const auto id = QUuid::createUuid(); const UnsupportedAnnotation future{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}}; d.annotations = {a, future, b}; d.embeddedImages = {{"asset", kOnePixelPng}};
    EditorSession session(d); session.select({a.id, b.id, id}); const auto selection = session.selection(); const auto defaults = session.toolStyle();
    const auto edit = session.editStrokeWidth(width, session.stateId(), selection, defaults); QVERIFY(edit.accepted && edit.documentChanged && edit.defaultsChanged); QVERIFY(edit.diagnostic.isEmpty());
    auto expected = d; for (size_t i : {size_t(0), size_t(2)}) std::get<RectangleAnnotation>(expected.annotations[i]).style.strokeWidth = width;
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.selection(), selection); QCOMPARE(session.toolStyle().strokeWidth, width);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QCOMPARE(session.toolStyle().strokeWidth, width);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QVERIFY(session.createRectangle({-5, -4}, {-2, -1})); QCOMPARE(session.selectedRectangle()->style.strokeWidth, width);
  }
  void guardedThicknessRefusalPreservesDocumentDefaultsCheckpointAndRedo_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"hidden", "locked", "opaque-lock", "unknown-only", "geometry", "stale-document", "stale-selection", "stale-defaults", "zero", "negative", "too-large", "nan", "infinity"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedThicknessRefusalPreservesDocumentDefaultsCheckpointAndRedo() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 4, 4), b = rectangle(5, 5, 8, 8); if (mode == "hidden") b.visible = false;
    if (mode == "locked" || mode == "opaque-lock") b.extra = {{"locked", mode == "locked" ? QJsonValue(true) : QJsonValue("future")}};
    if (mode == "geometry") { b.start.x = 1.2e308; b.end.x = 1.3e308; b.style.rotationDegrees = 90; }
    d.annotations = {a, b}; if (mode == "unknown-only") d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select(mode == "unknown-only" ? QList<QUuid>{b.id} : QList<QUuid>{a.id, b.id});
    const auto state = session.stateId(); const auto selection = session.selection(); const auto defaults = session.toolStyle(); QString diagnostic = "annotation-style-invalid";
    double width = 7; if (mode == "zero") width = 0; if (mode == "negative") width = -1; if (mode == "too-large") width = 10000.5;
    if (mode == "nan") width = std::numeric_limits<double>::quiet_NaN(); if (mode == "infinity") width = std::numeric_limits<double>::infinity();
    if (mode == "stale-document") { session.select({a.id}); QVERIFY(session.moveSelection(2, 0)); }
    if (mode == "stale-selection") session.select({a.id});
    if (mode == "stale-defaults") { auto later = defaults; later.fillColor = 0x80112233; QVERIFY(session.setCreationStyle(later)); }
    if (mode.startsWith("stale-")) diagnostic = "annotation-edit-stale";
    else if (mode == "geometry") diagnostic = "annotation-geometry-invalid";
    else if (mode == "unknown-only") diagnostic = "annotation-type-unsupported";
    else if (mode == "hidden" || mode.contains("lock")) diagnostic = "annotation-selection-protected";
    const auto before = serializeDocument(session.document()); const auto actualSelection = session.selection(); const auto actualDefaults = session.toolStyle(); const int undo = session.undoCount(), redo = session.redoCount(); const bool dirty = session.dirty();
    const auto result = session.editStrokeWidth(width, state, selection, defaults); QVERIFY(!result.accepted && !result.documentChanged && !result.defaultsChanged); QCOMPARE(result.diagnostic, diagnostic);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), actualSelection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), dirty);
    QCOMPARE(session.toolStyle().strokeWidth, actualDefaults.strokeWidth); QCOMPARE(session.toolStyle().strokeColor, actualDefaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, actualDefaults.fillColor);
  }
  void thicknessNoopsAndDefaultsOnlyPreserveHistoryAndRedo() {
    EditorSession session(document()); QVERIFY(session.createRectangle({1, 1}, {4, 4})); session.markSaved(); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); const auto before = serializeDocument(session.document());
    auto edit = session.editStrokeWidth(4, session.stateId(), session.selection(), session.toolStyle()); QVERIFY(edit.accepted && !edit.documentChanged && !edit.defaultsChanged); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    session.select({}); edit = session.editStrokeWidth(7.5, session.stateId(), {}, session.toolStyle()); QVERIFY(edit.accepted && !edit.documentChanged && edit.defaultsChanged);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty()); QVERIFY(session.redo()); QCOMPARE(session.toolStyle().strokeWidth, 7.5);
  }
  void guardedColorEditsCommitOneGroupAndUpdateFutureDefaults_data() {
    QTest::addColumn<bool>("fill"); QTest::newRow("stroke") << false; QTest::newRow("fill") << true;
  }
  void guardedColorEditsCommitOneGroupAndUpdateFutureDefaults() {
    QFETCH(bool, fill); auto d = document(); auto a = rectangle(1, 2, 4, 6), b = rectangle(5, 6, 9, 10);
    a.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; a.extra = {{"vendor", "keep"}};
    const auto futureId = QUuid::createUuid(); const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    d.annotations = {a, future, b}; d.embeddedImages = {{"asset", kOnePixelPng}}; EditorSession session(d); session.select({a.id, b.id, futureId});
    const auto selection = session.selection(); const auto before = serializeDocument(d); const auto defaults = session.toolStyle();
    const auto target = fill ? EditorSession::ColorTarget::Fill : EditorSession::ColorTarget::Stroke;
    const auto edit = session.editColor(target, 0x40667788, 0, selection, defaults); QVERIFY(edit.accepted); QVERIFY(edit.documentChanged); QVERIFY(edit.defaultsChanged); QVERIFY(edit.diagnostic.isEmpty());
    auto expected = d; for (size_t i : {size_t(0), size_t(2)}) { auto &style = std::get<RectangleAnnotation>(expected.annotations[i]).style; (fill ? style.fillColor : style.strokeColor) = 0x40667788; }
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    QCOMPARE(fill ? session.toolStyle().fillColor : session.toolStyle().strokeColor, Argb(0x40667788));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QCOMPARE(fill ? session.toolStyle().fillColor : session.toolStyle().strokeColor, Argb(0x40667788)); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected));
    const auto fresh = session.createRectangle({-5, -4}, {-2, -1}); QVERIFY(fresh); QCOMPARE(fill ? session.selectedRectangle()->style.fillColor : session.selectedRectangle()->style.strokeColor, Argb(0x40667788));
  }
  void guardedColorFailuresPreserveDefaultsDocumentSelectionAndRedo() {
    auto d = document(); const auto a = rectangle(1, 1, 4, 4); auto invalid = rectangle(1.2e308, 1, 1.3e308, 4); invalid.style.rotationDegrees = 90; d.annotations = {a, invalid};
    EditorSession session(d); QVERIFY(session.createRectangle({5, 5}, {6, 6})); QVERIFY(session.undo()); session.select({a.id, invalid.id});
    const auto before = serializeDocument(session.document()); const auto defaults = session.toolStyle(); const auto selection = session.selection();
    for (auto target : {EditorSession::ColorTarget::Stroke, EditorSession::ColorTarget::Fill}) {
      const auto edit = session.editColor(target, 0x40667788, 0, selection, defaults); QVERIFY(!edit.accepted); QCOMPARE(edit.diagnostic, QString("annotation-geometry-invalid"));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
      QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor);
    }
    session.select({a.id}); auto result = session.editColor(EditorSession::ColorTarget::Stroke, 0x40667788, 0, selection, defaults);
    QVERIFY(!result.accepted); QCOMPARE(result.diagnostic, QString("annotation-edit-stale")); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor);
    auto changedDefaults = defaults; changedDefaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(changedDefaults));
    result = session.editColor(EditorSession::ColorTarget::Fill, 0x40667788, 0, session.selection(), defaults); QVERIFY(!result.accepted); QCOMPARE(result.diagnostic, QString("annotation-edit-stale")); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
    QVERIFY(session.redo()); const auto intervening = serializeDocument(session.document());
    result = session.editColor(EditorSession::ColorTarget::Fill, 0x40667788, 0, session.selection(), session.toolStyle()); QVERIFY(!result.accepted); QCOMPARE(result.diagnostic, QString("annotation-edit-stale")); QCOMPARE(serializeDocument(session.document()), intervening);
    result = session.editColor(EditorSession::ColorTarget(99), 0x40667788, session.stateId(), session.selection(), session.toolStyle()); QVERIFY(!result.accepted); QCOMPARE(result.diagnostic, QString("annotation-style-invalid"));
  }
  void colorNoopsAndDefaultsOnlyPreserveCheckpointAndRedo() {
    EditorSession session(document()); const auto a = *session.createRectangle({1, 1}, {3, 3}); session.markSaved(); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    auto defaults = session.toolStyle(); const auto before = serializeDocument(session.document());
    auto edit = session.editColor(EditorSession::ColorTarget::Stroke, defaults.strokeColor, session.stateId(), session.selection(), defaults);
    QVERIFY(edit.accepted); QVERIFY(!edit.documentChanged); QVERIFY(!edit.defaultsChanged); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    session.select({}); edit = session.editColor(EditorSession::ColorTarget::Fill, 0x00667788, session.stateId(), {}, defaults);
    QVERIFY(edit.accepted); QVERIFY(!edit.documentChanged); QVERIFY(edit.defaultsChanged); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    QCOMPARE(session.toolStyle().fillColor, Argb(0x00667788)); session.select({a}); QVERIFY(session.redo()); QCOMPARE(session.toolStyle().fillColor, Argb(0x00667788));
  }
  void guardedColorNeverUpdatesDefaultsForProtectedOrUnsupportedSelections_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"hidden", "locked", "opaque-lock", "unknown-only"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedColorNeverUpdatesDefaultsForProtectedOrUnsupportedSelections() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 3, 3); if (mode == "hidden") a.visible = false; else if (mode != "unknown-only") a.extra = {{"locked", mode == "opaque-lock" ? QJsonValue("future") : QJsonValue(true)}};
    d.annotations = {a}; if (mode == "unknown-only") d.annotations[0] = UnsupportedAnnotation{a.id, {{"id", a.id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}}};
    EditorSession session(d); session.select({a.id}); QVERIFY(!session.canEditColor()); const auto defaults = session.toolStyle(); const auto before = serializeDocument(d);
    for (auto target : {EditorSession::ColorTarget::Stroke, EditorSession::ColorTarget::Fill}) {
      const auto edit = session.editColor(target, 0x40667788, 0, session.selection(), defaults); QVERIFY(!edit.accepted);
      QCOMPARE(edit.diagnostic, mode == "unknown-only" ? QString("annotation-type-unsupported") : QString("annotation-selection-protected"));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    }
  }
  void guardedGroupResizePreservesFieldsAssetsDefaultsAndHistory() {
    auto d = document(); auto a = rectangle(1, 2, 4, 6), b = rectangle(5, 6, 9, 10), untouched = rectangle(-5, -4, -2, -1);
    a.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; a.extra = {{"vendor", "keep"}}; b.style.rotationDegrees = 90;
    const auto futureId = QUuid::createUuid(); const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"payload", "keep"}}};
    d.annotations = {a, future, b, untouched}; d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "root"}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select({a.id, b.id, futureId}); const auto selection = session.selection();
    RectangleStyle defaults; defaults.strokeWidth = 9; defaults.rotationDegrees = 17.25; QVERIFY(session.setCreationStyle(defaults));
    QVERIFY(session.resizeSelection(1.5, -2.25, session.stateId(), selection)); auto expected = d;
    std::get<RectangleAnnotation>(expected.annotations[0]).end = {5.5, 3.75}; std::get<RectangleAnnotation>(expected.annotations[2]).end = {10.5, 7.75};
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty());
    QCOMPARE(session.toolStyle().strokeWidth, 9.0); QCOMPARE(session.toolStyle().rotationDegrees, 17.25);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection);
  }
  void guardedResizeRefusalsKeepCompleteStateAndRedo_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"empty", "unknown-only", "hidden", "locked", "opaque-lock", "unknown-lock", "stale-state", "stale-selection", "collapse", "nan", "infinity", "overflow", "zero", "rounding"}) QTest::newRow(mode) << QString(mode);
  }
  void guardedResizeRefusalsKeepCompleteStateAndRedo() {
    QFETCH(QString, mode); auto d = document(); auto a = rectangle(1, 1, 4, 4), b = rectangle(5, 5, 6, 6);
    if (mode == "hidden") b.visible = false; if (mode == "locked") b.extra = {{"locked", true}}; if (mode == "opaque-lock") b.extra = {{"locked", "future-lock"}};
    if (mode == "overflow" || mode == "rounding") { b.start = {1.2e308, 1}; b.end = {1.3e308, 4}; }
    d.annotations = {a, b}; if (mode == "unknown-lock" || mode == "unknown-only") d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"locked", mode == "unknown-lock"}}};
    EditorSession session(d); session.select({a.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    session.select(mode == "empty" ? QList<QUuid>{} : mode == "rounding" || mode == "unknown-only" ? QList<QUuid>{b.id} : QList<QUuid>{a.id, b.id});
    const auto state = session.stateId(); const auto frozen = session.selection(); if (mode == "stale-selection") session.select({a.id});
    if (mode == "stale-state") QVERIFY(session.redo()); const auto before = serializeDocument(session.document()); const auto selection = session.selection(); const auto undo = session.undoCount(), redo = session.redoCount(); const auto dirty = session.dirty();
    const double dw = mode == "collapse" ? -1 : mode == "nan" ? std::numeric_limits<double>::quiet_NaN() : mode == "infinity" ? std::numeric_limits<double>::infinity() : mode == "overflow" ? 1e308 : mode == "zero" ? 0 : 1;
    QVERIFY(!session.resizeSelection(dw, 0, state, frozen));
    const auto diagnostic = mode.startsWith("stale-") ? "annotation-edit-stale" : mode == "hidden" || mode.contains("lock") ? "annotation-selection-protected" : mode == "collapse" || mode == "nan" || mode == "infinity" || mode == "overflow" ? "annotation-geometry-invalid" : "";
    QCOMPARE(session.editDiagnostic(), QString(diagnostic)); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), dirty);
  }
  void guardedGroupRotationPreservesFieldsDefaultsAndExactHistory() {
    auto d = document(); auto a = rectangle(1, 2, 4, 6), b = rectangle(5, 6, 9, 10), untouched = rectangle(-5, -4, -2, -1);
    a.style = {0x80112233, 0x40223344, 2.5, 350, .25}; a.extra = {{"vendor", "keep"}}; b.style.rotationDegrees = 37.25;
    const auto futureId = QUuid::createUuid(); const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    d.annotations = {a, future, b, untouched}; d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "keep"}};
    EditorSession session(d); session.select({a.id, b.id, futureId}); const auto selection = session.selection(); const auto before = serializeDocument(d);
    RectangleStyle defaults; defaults.rotationDegrees = 90; defaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(defaults)); QVERIFY(session.canRotateSelection());
    QVERIFY(session.rotateSelection(17.25, session.stateId(), selection)); auto expected = d;
    std::get<RectangleAnnotation>(expected.annotations[0]).style.rotationDegrees = 7.25; std::get<RectangleAnnotation>(expected.annotations[2]).style.rotationDegrees = 54.5;
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    QCOMPARE(session.toolStyle().rotationDegrees, 90.0); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection);
    session.select({futureId}); QVERIFY(!session.canRotateSelection());
  }
  void rotationRefusesStaleAndInvalidGroupsWithoutTruncatingRedo() {
    auto d = document(); const auto a = rectangle(1, 1, 4, 4), large = rectangle(1.2e308, 1, 1.3e308, 4); d.annotations = {a, large};
    EditorSession session(d); QVERIFY(session.createRectangle({5, 5}, {6, 6})); QVERIFY(session.undo()); session.select({a.id, large.id});
    const auto state = session.stateId(); const auto before = serializeDocument(session.document()); const auto selection = session.selection();
    for (double degrees : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 90.0}) {
      QVERIFY(!session.rotateSelection(degrees, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    }
    for (double degrees : {0.0, 360.0, -720.0}) { QVERIFY(!session.rotateSelection(degrees, state, selection)); QVERIFY(session.editDiagnostic().isEmpty()); QCOMPARE(session.redoCount(), 1); }
    session.select({a.id}); QVERIFY(!session.rotateSelection(17.25, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    QVERIFY(session.redo()); const auto intervening = serializeDocument(session.document()); QVERIFY(!session.rotateSelection(17.25, state, session.selection()));
    QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale")); QCOMPARE(serializeDocument(session.document()), intervening);
    const auto huge = rotateRectangleGeometry({a.start, a.end, a.style}, 1e308); QVERIFY(huge.properties); QVERIFY(std::isfinite(huge.properties->style.rotationDegrees));
    QVERIFY(huge.properties->style.rotationDegrees >= 0 && huge.properties->style.rotationDegrees < 360);
  }
  void rotationRefusesProtectedCompleteGroups_data() {
    QTest::addColumn<QString>("protection"); for (const auto *p : {"hidden", "locked", "opaque-lock", "unknown-lock"}) QTest::newRow(p) << QString(p);
  }
  void rotationRefusesProtectedCompleteGroups() {
    QFETCH(QString, protection); auto d = document(); const auto a = rectangle(1, 1, 3, 3); auto b = rectangle(4, 4, 6, 6);
    if (protection == "hidden") b.visible = false; else b.extra = {{"locked", protection == "opaque-lock" ? QJsonValue("future") : QJsonValue(true)}};
    d.annotations = {a, b}; if (protection == "unknown-lock") d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"locked", true}}};
    EditorSession session(d); session.select({a.id, b.id}); QVERIFY(!session.canRotateSelection()); const auto before = serializeDocument(d);
    QVERIFY(!session.rotateSelection(17.25, 0, session.selection())); QCOMPARE(session.editDiagnostic(), QString("annotation-selection-protected"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void guardedGroupMovePreservesCompleteFieldsAndExactHistory() {
    auto d = document(); auto a = rectangle(1, 2, 3, 4), b = rectangle(5, 6, 9, 10), untouched = rectangle(-5, -4, -2, -1);
    a.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; a.extra = {{"future", "preserved"}};
    b.style.rotationDegrees = 90; const auto futureId = QUuid::createUuid();
    const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    d.annotations = {a, future, b, untouched}; d.embeddedImages = {{"asset", kOnePixelPng}}; d.extra = {{"vendor", "keep"}};
    EditorSession session(d); session.select({a.id, b.id, futureId}); const auto selection = session.selection(); const auto before = serializeDocument(d);
    RectangleStyle defaults; defaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(defaults));
    // Unknown types stay inert as in the existing keyboard move path; only
    // represented rectangles translate, without interpreting raw payloads.
    QVERIFY(session.moveSelection(-1.5, 2.25, session.stateId(), selection));
    auto expected = d; auto &ea = std::get<RectangleAnnotation>(expected.annotations[0]); auto &eb = std::get<RectangleAnnotation>(expected.annotations[2]);
    ea.start = {-.5, 4.25}; ea.end = {1.5, 6.25}; eb.start = {3.5, 8.25}; eb.end = {7.5, 12.25};
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1);
    QCOMPARE(session.toolStyle().strokeWidth, 9.0); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection);
  }
  void guardedMoveFailuresAndNoopsPreserveRedoAndCheckpoint() {
    auto d = document(); const auto a = rectangle(1, 1, 4, 4), large = rectangle(1.2e308, 1, 1.3e308, 4); d.annotations = {a, large};
    EditorSession session(d); QVERIFY(session.createRectangle({5, 5}, {6, 6})); QVERIFY(session.undo()); session.select({a.id, large.id});
    const auto state = session.stateId(); const auto before = serializeDocument(session.document()); const auto selection = session.selection();
    for (const auto delta : {PointF{std::numeric_limits<double>::quiet_NaN(), 0}, PointF{0, std::numeric_limits<double>::infinity()}, PointF{1e308, 0}}) {
      QVERIFY(!session.moveSelection(delta.x, delta.y, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    }
    QVERIFY(!session.moveSelection(0, 0, state, selection)); QVERIFY(session.editDiagnostic().isEmpty()); QCOMPARE(session.redoCount(), 1);
    session.select({large.id}); QVERIFY(!session.moveSelection(1, 0, state, session.selection())); QVERIFY(session.editDiagnostic().isEmpty());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    session.select({a.id}); QVERIFY(!session.moveSelection(1, 0, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    QVERIFY(session.redo()); const auto intervening = serializeDocument(session.document());
    QVERIFY(!session.moveSelection(1, 0, state, session.selection())); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    QCOMPARE(serializeDocument(session.document()), intervening); QCOMPARE(session.undoCount(), 1);
    // Finite translated scalars can still overflow rotated export geometry.
    auto rotated = rectangle(5e307, 1, 6e307, 4); rotated.style.rotationDegrees = 90;
    const auto prepared = moveRectangleGeometry({rotated.start, rotated.end, rotated.style}, {5e307, 0});
    QVERIFY(!prepared.properties); QCOMPARE(prepared.diagnostic, QString("annotation-geometry-invalid"));
  }
  void guardedMoveNeverPartiallyMutatesProtectedGroups_data() {
    QTest::addColumn<QString>("protection");
    for (const auto *p : {"hidden", "locked", "opaque-lock", "unknown-lock"}) QTest::newRow(p) << QString(p);
  }
  void guardedMoveNeverPartiallyMutatesProtectedGroups() {
    QFETCH(QString, protection); auto d = document(); const auto a = rectangle(1, 1, 3, 3); auto b = rectangle(4, 4, 6, 6);
    if (protection == "hidden") b.visible = false; else b.extra = {{"locked", protection == "opaque-lock" ? QJsonValue("future") : QJsonValue(true)}};
    d.annotations = {a, b};
    if (protection == "unknown-lock") d.annotations[1] = UnsupportedAnnotation{b.id, {{"id", b.id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"locked", true}}};
    EditorSession session(d); session.select({a.id, b.id}); const auto before = serializeDocument(d);
    QVERIFY(!session.moveSelection(1, 2, session.stateId(), session.selection())); QCOMPARE(session.editDiagnostic(), QString("annotation-selection-protected"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void largeSelectionFiltersUnknownAndDuplicateIdsWithoutDocumentMutation() {
    auto d = document(); QList<QUuid> ids;
    for (int i = 0; i < 1000; ++i) { const auto r = rectangle(1, 1, 3, 3); d.annotations.push_back(r); ids << r.id << r.id; }
    ids << QUuid::createUuid(); EditorSession session(d); const auto before = serializeDocument(session.document());
    QVERIFY(session.select(ids, 0)); QCOMPARE(session.selection().size(), 1000); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    QCOMPARE(serializeDocument(session.document()), before);
  }
  void guardedSelectionDoesNotMutateHistoryOrReplaceInterveningState() {
    EditorSession session(document()); const auto id = *session.createRectangle({1, 1}, {3, 3}); session.markSaved();
    const auto state = session.stateId(); const auto before = serializeDocument(session.document());
    QVERIFY(session.select({}, state)); QVERIFY(session.select({id, QUuid::createUuid()}, state)); QCOMPARE(session.selection(), QSet<QUuid>{id});
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty());
    QVERIFY(session.moveSelection(1, 0)); const auto intervening = serializeDocument(session.document());
    QVERIFY(!session.select({}, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale")); QCOMPARE(session.selection(), QSet<QUuid>{id});
    QCOMPARE(serializeDocument(session.document()), intervening); QVERIFY(session.undo());
    QVERIFY(session.select({}, state)); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
  }
  void frozenCreationNormalizesBoundsAndPreservesDefaultsAndHistory() {
    auto d = document(); const auto original = rectangle(1, 2, 3, 4); d.annotations = {original}; d.embeddedImages = {{"kept", kOnePixelPng}};
    EditorSession session(d); session.select({original.id}); const auto before = serializeDocument(session.document());
    const RectangleStyle frozen{0x80112233, 0x40223344, 2.5, 37.25, .25};
    RectangleStyle later; later.strokeWidth = 9; QVERIFY(session.setCreationStyle(later));
    const auto id = session.createRectangle({{8.75, 9.125}, {-1.5, 2.25}, frozen}, session.stateId(), session.selection()); QVERIFY(id);
    QVERIFY(*id != original.id); const auto *r = session.selectedRectangle(); QVERIFY(r); QCOMPARE(r->start, (PointF{-1.5, 2.25})); QCOMPARE(r->end, (PointF{8.75, 9.125}));
    QCOMPARE(r->style.strokeColor, frozen.strokeColor); QCOMPARE(r->style.fillColor, frozen.fillColor); QCOMPARE(r->style.strokeWidth, frozen.strokeWidth);
    QCOMPARE(r->style.rotationDegrees, frozen.rotationDegrees); QCOMPARE(r->style.opacity, frozen.opacity); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
    QCOMPARE(session.document().embeddedImages, d.embeddedImages); QCOMPARE(session.document().sourceImagePng, d.sourceImagePng); QCOMPARE(session.undoCount(), 1);
    const auto created = serializeDocument(session.document()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    QCOMPARE(session.selection(), QSet<QUuid>{original.id}); QVERIFY(!session.dirty()); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), created); QCOMPARE(session.selection(), QSet<QUuid>{*id});
  }
  void failedFrozenCreationPreservesRedoCheckpointAndInterveningState() {
    EditorSession session(document()); const auto id = *session.createRectangle({1, 1}, {3, 3}); session.markSaved();
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); const auto state = session.stateId(); const auto selection = session.selection();
    const auto before = serializeDocument(session.document()); RectangleProperties draft{{2, 2}, {4, 4}, session.toolStyle()};
    draft.end.x = draft.start.x; QVERIFY(!session.createRectangle(draft, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    draft.end.x = std::numeric_limits<double>::quiet_NaN(); QVERIFY(!session.createRectangle(draft, state, selection));
    draft.end.x = 4; draft.style.opacity = 2; QVERIFY(!session.createRectangle(draft, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    draft.style.opacity = 1; session.select({}); QVERIFY(!session.createRectangle(draft, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    session.select({id}); QVERIFY(session.redo()); const auto intervening = serializeDocument(session.document());
    QVERIFY(!session.createRectangle(draft, state, selection)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale")); QCOMPARE(serializeDocument(session.document()), intervening);
  }
  void pointerResizeHasFixedOppositeBounds_data() {
    QTest::addColumn<int>("handle"); QTest::addColumn<double>("left"); QTest::addColumn<double>("top");
    QTest::addColumn<double>("right"); QTest::addColumn<double>("bottom");
    QTest::newRow("top-left") << int(RectangleHandle::TopLeft) << 14.0 << 26.0 << 30.0 << 40.0;
    QTest::newRow("top") << int(RectangleHandle::Top) << 10.0 << 26.0 << 30.0 << 40.0;
    QTest::newRow("top-right") << int(RectangleHandle::TopRight) << 10.0 << 26.0 << 34.0 << 40.0;
    QTest::newRow("right") << int(RectangleHandle::Right) << 10.0 << 20.0 << 34.0 << 40.0;
    QTest::newRow("bottom-right") << int(RectangleHandle::BottomRight) << 10.0 << 20.0 << 34.0 << 46.0;
    QTest::newRow("bottom") << int(RectangleHandle::Bottom) << 10.0 << 20.0 << 30.0 << 46.0;
    QTest::newRow("bottom-left") << int(RectangleHandle::BottomLeft) << 14.0 << 20.0 << 30.0 << 46.0;
    QTest::newRow("left") << int(RectangleHandle::Left) << 14.0 << 20.0 << 30.0 << 40.0;
  }
  void pointerResizeHasFixedOppositeBounds() {
    QFETCH(int, handle); QFETCH(double, left); QFETCH(double, top); QFETCH(double, right); QFETCH(double, bottom);
    auto d = document(); auto object = rectangle(10, 20, 30, 40); object.extra = {{"future", QJsonObject{{"value", "kept"}}}};
    d.annotations = {object}; d.embeddedImages = {{"asset", kOnePixelPng}}; EditorSession session(d); session.select({object.id});
    const auto original = serializeDocument(session.document()); const auto defaults = session.toolStyle();
    QVERIFY(session.resizeRectangle(object.id, RectangleHandle(handle), {4, 6}, session.stateId()));
    const auto *r = session.selectedRectangle(); QVERIFY(r); QCOMPARE(r->start, (PointF{left, top})); QCOMPARE(r->end, (PointF{right, bottom}));
    QCOMPARE(r->id, object.id); QCOMPARE(r->extra, object.extra); QCOMPARE(session.document().embeddedImages, d.embeddedImages);
    QCOMPARE(session.toolStyle().strokeWidth, defaults.strokeWidth); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), original); QVERIFY(!session.dirty()); QCOMPARE(session.selection(), QSet<QUuid>{object.id});
    QVERIFY(session.redo()); QCOMPARE(session.selectedRectangle()->end, (PointF{right, bottom})); QCOMPARE(session.selectedRectangle()->id, object.id);
  }
  void rotatedResizeAndAbsolutePropertiesPreserveFieldsAndCreationDefaults() {
    auto d = document(); auto object = rectangle(10, 20, 30, 40); object.style.rotationDegrees = 90; object.extra = {{"borderStyle", "future-compatible"}};
    d.annotations = {object}; EditorSession session(d); session.select({object.id}); const auto defaults = session.toolStyle();
    QVERIFY(session.resizeRectangle(object.id, RectangleHandle::BottomRight, {-6, 4}, session.stateId()));
    QCOMPARE(session.selectedRectangle()->start, (PointF{5, 19})); QCOMPARE(session.selectedRectangle()->end, (PointF{29, 45}));
    RectangleProperties properties{{-1.5, 2.25}, {8.75, 9.125}, {0x80112233, 0x40223344, 2.5, 37.25, .25}};
    QVERIFY(session.editRectangleProperties(object.id, properties, session.stateId())); QCOMPARE(session.undoCount(), 2);
    const auto *r = session.selectedRectangle(); QCOMPARE(r->start, properties.start); QCOMPARE(r->end, properties.end);
    QCOMPARE(r->style.strokeColor, properties.style.strokeColor); QCOMPARE(r->style.fillColor, properties.style.fillColor);
    QCOMPARE(r->style.opacity, .25); QCOMPARE(r->extra, object.extra); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QCOMPARE(session.toolStyle().strokeWidth, defaults.strokeWidth);
    QVERIFY(!session.editRectangleProperties(object.id, properties, session.stateId())); QVERIFY(session.editDiagnostic().isEmpty()); QCOMPARE(session.undoCount(), 2);
    const auto fresh = session.createRectangle({1, 1}, {3, 3}); QVERIFY(fresh); QCOMPARE(session.selectedRectangle()->style.strokeColor, defaults.strokeColor);
    QCOMPARE(session.selectedRectangle()->style.strokeWidth, defaults.strokeWidth); QCOMPARE(session.selectedRectangle()->style.opacity, defaults.opacity);
  }
  void invalidStaleProtectedAndNoopRectangleEditsPreserveRedo() {
    auto d = document(); const auto object = rectangle(10, 20, 30, 40); d.annotations = {object}; EditorSession session(d); session.select({object.id});
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select({object.id});
    const auto state = session.stateId(); const auto original = serializeDocument(session.document());
    for (const PointF delta : {PointF{-20, 0}, PointF{-21, 0}, PointF{std::numeric_limits<double>::infinity(), 0}}) {
      QVERIFY(!session.resizeRectangle(object.id, RectangleHandle::BottomRight, delta, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    }
    QVERIFY(!session.resizeRectangle(object.id, RectangleHandle(99), {1, 1}, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-handle-invalid"));
    QVERIFY(!session.resizeRectangle(object.id, RectangleHandle::BottomRight, {0, 0}, state)); QVERIFY(session.editDiagnostic().isEmpty());
    RectangleProperties invalid{object.start, object.end, object.style}; invalid.style.opacity = 2;
    QVERIFY(!session.editRectangleProperties(object.id, invalid, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
    invalid.style = object.style; invalid.end.x = std::numeric_limits<double>::quiet_NaN(); QVERIFY(!session.editRectangleProperties(object.id, invalid, state));
    QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    invalid = {{1.2e308, 1}, {1.3e308, 4}, object.style}; invalid.style.rotationDegrees = 90;
    QVERIFY(!session.editRectangleProperties(object.id, invalid, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), original); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    session.select({}); QVERIFY(!session.resizeRectangle(object.id, RectangleHandle::Right, {1, 0}, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    session.select({object.id}); QVERIFY(session.redo());
    QVERIFY(!session.editRectangleProperties(object.id, {object.start, object.end, object.style}, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-edit-stale"));
    for (bool visible : {true, false}) {
      auto protectedDoc = d; auto &r = std::get<RectangleAnnotation>(protectedDoc.annotations[0]); r.visible = visible; r.extra = {{"locked", true}};
      EditorSession protectedSession(protectedDoc); protectedSession.select({object.id});
      QVERIFY(!protectedSession.resizeRectangle(object.id, RectangleHandle::Right, {1, 0}, 0));
      QVERIFY(!protectedSession.editRectangleProperties(object.id, {object.start, object.end, object.style}, 0));
      QCOMPARE(protectedSession.editDiagnostic(), QString("annotation-selection-protected")); QCOMPARE(protectedSession.undoCount(), 0); QVERIFY(!protectedSession.dirty());
    }
  }
  void rejectedCreationPreservesRedoCheckpointAndSelection_data() {
    QTest::addColumn<double>("sx"); QTest::addColumn<double>("sy");
    QTest::addColumn<double>("ex"); QTest::addColumn<double>("ey");
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    QTest::newRow("start-x-nan") << nan << 1.0 << 5.0 << 6.0;
    QTest::newRow("start-y-nan") << 1.0 << nan << 5.0 << 6.0;
    QTest::newRow("end-x-nan") << 1.0 << 1.0 << nan << 6.0;
    QTest::newRow("end-y-nan") << 1.0 << 1.0 << 5.0 << nan;
    QTest::newRow("infinite") << 1.0 << 1.0 << inf << 6.0;
    QTest::newRow("span-overflow") << -1e308 << 1.0 << 1e308 << 6.0;
    QTest::newRow("zero-width") << 1.0 << 1.0 << 1.0 << 6.0;
    QTest::newRow("zero-height") << 1.0 << 1.0 << 5.0 << 1.0;
  }
  void rejectedCreationPreservesRedoCheckpointAndSelection() {
    QFETCH(double, sx); QFETCH(double, sy); QFETCH(double, ex); QFETCH(double, ey);
    EditorSession session(document()); const auto id = session.createRectangle({1, 1}, {4, 4}); QVERIFY(id);
    session.markSaved(); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select({*id});
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    QVERIFY(!session.createRectangle({sx, sy}, {ex, ey}));
    QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.stateId(), state);
    QCOMPARE(session.selection(), QSet<QUuid>{*id}); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), 2.0);
  }
  void batchGeometryFailureAndRoundedNoopsNeverTruncateRedo() {
    auto d = document(); const auto small = rectangle(1, 1, 4, 4), large = rectangle(1.2e308, 1, 1.3e308, 4);
    d.annotations = {small, large}; EditorSession session(d);
    QVERIFY(session.createRectangle({5, 5}, {6, 6})); QVERIFY(session.undo()); session.select({small.id, large.id});
    const auto before = serializeDocument(session.document());
    QVERIFY(!session.moveSelection(1e308, 0)); QVERIFY(!session.resizeSelection(1e308, 0));
    QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    QCOMPARE(session.selection(), (QSet<QUuid>{small.id, large.id}));
    session.select({large.id}); QVERIFY(!session.moveSelection(1, 0)); QVERIFY(!session.resizeSelection(1, 0));
    QVERIFY(session.editDiagnostic().isEmpty()); QCOMPARE(session.redoCount(), 1); QCOMPARE(serializeDocument(session.document()), before);
    // Finite angle additions must never overflow before normalization.
    session.select({small.id}); QVERIFY(session.rotateSelection(1e308)); QVERIFY(session.rotateSelection(1e308));
    const double angle = std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees;
    QVERIFY(std::isfinite(angle)); QVERIFY(angle >= 0 && angle < 360);
    QVERIFY(render(solidImage(10, 10, 0), session.document().annotations).image);
  }
  void everyEditRestoresTheActualPreCommandSelection() {
    EditorSession session(document()); const auto a = *session.createRectangle({1, 1}, {4, 4});
    const auto b = *session.createRectangle({5, 5}, {8, 8}); session.select({a}); session.markSaved();
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); QCOMPARE(session.selection(), QSet<QUuid>{a}); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); session.select({b});
    const auto c = *session.createRectangle({-5, -4}, {-2, -1});
    QVERIFY(session.undo()); QCOMPARE(session.selection(), QSet<QUuid>{b});
    QVERIFY(session.redo()); QCOMPARE(session.selection(), QSet<QUuid>{c});
    session.select({a, b}); QVERIFY(session.deleteSelection());
    QVERIFY(session.undo()); QCOMPARE(session.selection(), (QSet<QUuid>{a, b}));
    QVERIFY(session.redo()); QVERIFY(session.selection().isEmpty()); QCOMPARE(session.document().annotations.size(), size_t(1));
  }
  void snapshotSaveAcknowledgesItsHistoryStateAndSplitSaveInvalidatesCleanState() {
    EditorSession session(document()); QVERIFY(session.createRectangle({2, 2}, {10, 10}));
    const auto saved = session.stateId(); QVERIFY(session.moveSelection(2, 0));
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
    QCOMPARE(session.recordSnapshotSave(saved, true, false), QStringList{diagnostic::sidecarSaveFailed}); QVERIFY(session.dirty());
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(!session.dirty());
    QVERIFY(session.undo()); QVERIFY(session.createRectangle({12, 12}, {18, 18}));
    QVERIFY(session.recordSnapshotSave(saved, true, true).isEmpty()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(session.dirty()); // truncated saved snapshot is unreachable
  }
  void creationDefaultsLeaveSelectedObjectsCheckpointAndRedoIntact() {
    EditorSession session(document());
    const auto id = session.createRectangle({1, 1}, {4, 4}); QVERIFY(id);
    session.markSaved();
    const auto before = serializeDocument(session.document());
    const auto selection = session.selection(); const auto state = session.stateId();
    RectangleStyle next; next.strokeColor = 0x1234ABCD; next.fillColor = 0x80665544; next.strokeWidth = 10;
    QVERIFY(session.setCreationStyle(next));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection);
    QCOMPARE(session.stateId(), state); QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty());
    QVERIFY(session.undo()); QCOMPARE(session.redoCount(), 1);
    next.strokeWidth = 9; QVERIFY(session.setCreationStyle(next)); QCOMPARE(session.redoCount(), 1);
    QVERIFY(session.redo()); QVERIFY(!session.dirty());
    QVERIFY(session.createRectangle({5, 5}, {8, 8}));
    const auto &created = std::get<RectangleAnnotation>(session.document().annotations.back());
    QCOMPARE(created.style.strokeColor, Argb{0x1234ABCD}); QCOMPARE(created.style.fillColor, Argb{0x80665544});
    QCOMPARE(created.style.strokeWidth, 9.0);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations.front()).style.strokeWidth, 4.0);
    QVERIFY(session.undo()); QVERIFY(!session.dirty()); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
    // Explicit editing of the selected original object remains a history edit.
    QVERIFY(session.setStrokeWidth(7)); QVERIFY(session.dirty());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations.front()).style.strokeWidth, 7.0);
  }
  void invalidCreationDefaultsPreserveThePreviousStyle() {
    EditorSession session(document());
    RectangleStyle valid; valid.strokeWidth = 8; QVERIFY(session.setCreationStyle(valid));
    for (double width : {0.0, -1.0, 10000.1, std::numeric_limits<double>::infinity()}) {
      auto next = valid; next.strokeWidth = width; QVERIFY(!session.setCreationStyle(next));
      QCOMPARE(session.toolStyle().strokeWidth, 8.0);
    }
    for (double opacity : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
      auto next = valid; next.opacity = opacity; QVERIFY(!session.setCreationStyle(next));
    }
    auto next = valid; next.rotationDegrees = std::numeric_limits<double>::infinity();
    QVERIFY(!session.setCreationStyle(next)); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void unsavedInitialPixelsHaveNoCleanCheckpointUntilSuccessfulSave() {
    EditorSession session(document(), EditorSession::InitialPersistence::Unsaved);
    QVERIFY(session.dirty()); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.undo());
    QVERIFY(session.createRectangle({1, 1}, {4, 4}));
    QVERIFY(session.undo()); QVERIFY(session.dirty());
    QCOMPARE(session.recordSave(true, false), QStringList{diagnostic::sidecarSaveFailed});
    QVERIFY(session.dirty());
    QVERIFY(session.recordSave(true, true).isEmpty()); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
    EditorSession opened(document()); QVERIFY(!opened.dirty());
  }

  void mismatchedSourceRequiresAnExplicitBindingChoice() {
    auto d = document(10, 10); d.sourceImagePng = pngHeader(10, 10);
    d.imageHash = QStringLiteral("sha256:") + QString(64, QLatin1Char('0'));
    const QString hash = QStringLiteral("sha256:") + QString(64, QLatin1Char('1'));
    const QByteArray current = pngHeader(12, 8);
    const auto r = rectangle(1, 2, 5, 6); d.annotations = {r};
    d.extra.insert(QStringLiteral("vendor"), QStringLiteral("preserve"));
    auto result = bindSource(d, current, QStringLiteral("changed.png"), hash);
    QVERIFY(result.choiceRequired && !result.document && !result.error);
    result = bindSource(d, current, QStringLiteral("changed.png"), hash, SourceChoice::CurrentRaster);
    QVERIFY(result.document); QCOMPARE(result.document->sourceImagePng, current);
    QCOMPARE(result.document->canvasWidth, 12); QCOMPARE(result.document->canvasHeight, 8);
    QCOMPARE(result.document->imageHash, hash); QCOMPARE(result.document->extra, d.extra);
    QCOMPARE(serializeDocument(*result.document).value(u"annotations"), serializeDocument(d).value(u"annotations"));
    result = bindSource(d, current, QStringLiteral("changed.png"), hash, SourceChoice::EmbeddedSource);
    QVERIFY(result.document); QCOMPARE(result.document->sourceImagePng, d.sourceImagePng);
    QCOMPARE(result.document->canvasWidth, 10); QCOMPARE(result.document->canvasHeight, 10);
    QCOMPARE(d.imageHash, QStringLiteral("sha256:") + QString(64, QLatin1Char('0')));
    result = bindSource(d, current, QStringLiteral("unchanged.png"), d.imageHash);
    QVERIFY(result.document && !result.choiceRequired); QCOMPARE(result.document->sourceImagePng, d.sourceImagePng);
  }

  void sourceBindingRejectsVersionsCorruptHeadersAndInvalidChoices() {
    auto d = document(1, 1);
    d.version = 2;
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString()).error,
             std::optional<QString>(diagnostic::documentVersionUnsupported));
    d.version = 1;
    QCOMPARE(bindSource(d, QByteArray("bad"), QStringLiteral("image.png"), QString(), SourceChoice::CurrentRaster).error,
             std::optional<QString>(diagnostic::documentInvalid));
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString(), SourceChoice(-1)).error,
             std::optional<QString>(diagnostic::documentInvalid));
    d.canvasWidth = 2;
    QCOMPARE(bindSource(d, kOnePixelPng, QStringLiteral("image.png"), QString()).error,
             std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-013: gzip-wrapped JSON round-trips with uppercase colours.
  void xannRoundTrips() {
    AnnotationDocument d = document();
    RectangleAnnotation r = rectangle(1, 2, 5, 6);
    r.style.fillColor = 0x80ABCDEF;
    d.annotations.emplace_back(r);
    const auto bytes = writeXann(d);
    QVERIFY(bytes);
    QCOMPARE(bytes->left(2), QByteArray("\x1f\x8b", 2));  // gzip magic
    const ParseResult parsed = readXann(*bytes);
    QVERIFY2(parsed.document, qPrintable(parsed.error.value_or(QString())));
    const auto &back = std::get<RectangleAnnotation>(parsed.document->annotations.at(0));
    QCOMPARE(back.id, r.id);
    QCOMPARE(back.style.fillColor, Argb{0x80ABCDEF});
    QCOMPARE(serializeDocument(*parsed.document).value(u"annotations").toArray().at(0).toObject().value(u"fillColor").toString(),
             QStringLiteral("#80ABCDEF"));
  }

  // ES-028: inflation stops at the limit.
  void decompressionLimitIsEnforced() {
    const auto bytes = writeXann(document());
    QVERIFY(bytes);
    QCOMPARE(readXann(*bytes, 64).error, std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(readXann(QByteArray("not gzip")).error, std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-017, ES-028: PNG headers are checked without decoding pixels.
  void embeddedImagesAreCheckedBeforeDecode() {
    QJsonObject payload = serializeDocument(document());
    payload.insert(QStringLiteral("embeddedImages"),
                   QJsonObject{{QStringLiteral("big"), QString::fromLatin1(pngHeader(200000, 1).toBase64())}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentTooLarge));
    payload.insert(QStringLiteral("embeddedImages"), QJsonObject{{QStringLiteral("bad"), QStringLiteral("@@@")}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
    payload.insert(QStringLiteral("embeddedImages"),
                   QJsonObject{{QStringLiteral("gif"), QString::fromLatin1(QByteArray("GIF89a-not-a-png-file!!!").toBase64())}});
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentInvalid));
  }

  // ES-029 priority: too large outranks invalid.
  void tooLargeOutranksInvalid() {
    QJsonObject payload = serializeDocument(document(200000, 1));
    payload.insert(QStringLiteral("imageHash"), QStringLiteral("md5:nope"));
    QCOMPARE(parseDocument(payload).error, std::optional<QString>(diagnostic::documentTooLarge));
  }

  void lowercaseColoursAreAccepted() {
    QCOMPARE(parseColor(QStringLiteral("#ffff0000")), std::optional<Argb>(0xFFFF0000));
    QCOMPARE(formatColor(0xffab0000), QStringLiteral("#FFAB0000"));
  }

  // ES-011, ES-012: a sidecar failure keeps the session dirty.
  void sidecarFailureKeepsSessionDirty() {
    EditorSession session(document());
    session.createRectangle({1, 1}, {4, 4});
    QCOMPARE(session.recordSave(true, false), QStringList{diagnostic::sidecarSaveFailed});
    QVERIFY(session.dirty());
    QVERIFY(session.recordSave(true, true).isEmpty());
    QVERIFY(!session.dirty());
  }

  // ES-014: a document with annotations writes the default sidecar.
  void sidecarIsWrittenAndFailureReported() {
    AnnotationDocument d = document();
    d.annotations.emplace_back(rectangle(1, 1, 3, 3));
    MapStore store;
    const SidecarSaveResult saved = saveSidecar(store, QStringLiteral("a/shot.png"), d, false);
    QCOMPARE(saved.sidecarPath, std::optional<QString>(QStringLiteral("a/shot.png.xann")));
    QVERIFY(readXann(store.files.at(QStringLiteral("a/shot.png.xann"))).document);
    store.failWrites = true;
    QCOMPARE(saveSidecar(store, QStringLiteral("a/shot.png"), d, true).diagnostic,
             std::optional<QString>(diagnostic::sidecarSaveFailed));
  }

  // ES-024, ES-025: a quarter turn renders as the swapped rectangle.
  void quarterTurnSwapsExtents() {
    RectangleAnnotation turned = rectangle(2, 4, 8, 6);
    turned.style.rotationDegrees = -270;
    turned.style.fillColor = 0xFF00FF00;
    RectangleAnnotation upright = rectangle(4, 2, 6, 8);
    upright.style.fillColor = 0xFF00FF00;
    const ArgbImage source = solidImage(10, 10, 0xFFFFFFFF);
    const RenderResult a = render(source, {Annotation(turned)});
    const RenderResult b = render(source, {Annotation(upright)});
    QVERIFY(a.image && b.image);
    QVERIFY(a.image->pixels == b.image->pixels);
  }

  // ES-025: a rotated 2x2 square is a diamond of area 4. In each quadrant
  // the central pixel has area 2*sqrt(2)-2 and the two outside triangles
  // have area (sqrt(2)-1)^2/2. These expected bytes come from those areas,
  // not from another renderer or native preview.
  void diamondHasExactPolygonCoverage_data() {
    QTest::addColumn<double>("angle");
    for (double angle : {45.0, -45.0, 135.0, 405.0, -315.0}) {
      QTest::newRow(qPrintable(QString::number(angle))) << angle;
    }
  }
  void diamondHasExactPolygonCoverage() {
    QFETCH(double, angle);
    RectangleAnnotation r = rectangle(1, 1, 3, 3);
    r.style.rotationDegrees = angle;
    r.style.fillColor = 0xFFFFFFFF;
    r.style.strokeColor = 0;
    const RenderResult result = render(solidImage(4, 4, 0), {r});
    QVERIFY(result.image);
    const int alpha[4][4] = {{0, 22, 22, 0}, {22, 211, 211, 22}, {22, 211, 211, 22}, {0, 22, 22, 0}};
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const Argb expected = alpha[y][x] == 0 ? 0 : (Argb(alpha[y][x]) << 24) | 0xFFFFFF;
        QCOMPARE(result.image->at(x, y), expected);
      }
    }
  }

  // The 1x1 diamond has .25 fill coverage in each pixel. Its 1px stroke
  // grows to a 2x2 diamond with 2*sqrt(2)-2 coverage, and the inner box is
  // empty. Applying ES-025 with red fill, blue stroke and opacity .5 over
  // white gives (#FF9590FA) in each pixel.
  void rotatedFillAndStrokeApplyOpacityOnce() {
    RectangleAnnotation r = rectangle(.5, .5, 1.5, 1.5);
    r.style = {0xFF0000FF, 0xFFFF0000, 1, 45, .5};
    const RenderResult result = render(solidImage(2, 2, 0xFFFFFFFF), {r});
    QVERIFY(result.image);
    for (Argb pixel : result.image->pixels) QCOMPARE(pixel, Argb{0xFF9590FA});
  }

  void rotatedStrokeSubtractsInnerPolygon() {
    RectangleAnnotation r = rectangle(2, 2, 6, 6);
    r.style = {0xFFFF0000, 0, 1, 45, 1};
    const RenderResult result = render(solidImage(8, 8, 0), {r});
    QVERIFY(result.image);
    // This pixel is wholly inside the rotated inner square and has no fill.
    QCOMPARE(result.image->at(3, 3), Argb{0});
    QVERIFY((result.image->at(4, 1) >> 24) > 0);
    // Stroke area = 5^2 - 3^2 = 16; quantization of boundary pixels may
    // differ by at most half a byte per pixel.
    int alpha = 0;
    for (Argb pixel : result.image->pixels) alpha += pixel >> 24;
    QVERIFY(std::abs(alpha - 16 * 255) <= 32);
  }

  void exportRejectsInvalidGeometryAndClipsFarAwayObjects() {
    RectangleAnnotation r = rectangle(1e100, 1e100, 2e100, 2e100);
    const ArgbImage source = solidImage(2, 2, 0xFFFFFFFF);
    for (double angle : {0.0, 17.0}) {
      r.style.rotationDegrees = angle;
      const auto result = render(source, {r});
      QVERIFY(result.image);
      QVERIFY(result.image->pixels == source.pixels);
    }
    r.style.rotationDegrees = std::numeric_limits<double>::infinity();
    QCOMPARE(render(source, {r}).error, std::optional<RenderError>(RenderError::InvalidGeometry));
    r.style.rotationDegrees = 17;
    r.start.x = std::numeric_limits<double>::quiet_NaN();
    QCOMPARE(render(source, {r}).error, std::optional<RenderError>(RenderError::InvalidGeometry));
    QCOMPARE(render(ArgbImage{2, 2, {}}, {}).error, std::optional<RenderError>(RenderError::InvalidSource));
  }

  // ES-025: transparent destination channels become 0 once composited.
  void transparentDestinationIsNormalized() {
    const RenderResult result = render(solidImage(10, 1, 0x00123456), {Annotation(rectangle(0, 0, 1, 1))});
    QVERIFY(result.image);
    QCOMPARE(result.image->at(9, 0), Argb{0});  // outside the 4-pixel stroke
  }

  // ES-006: redo restores the selection the redone state had.
  void redoRestoresSelection() {
    EditorSession session(document());
    session.createRectangle({1, 1}, {4, 4});
    session.deleteSelection();
    session.undo();
    QCOMPARE(session.selection().size(), 1);
    session.redo();
    QCOMPARE(session.selection().size(), 0);
    QCOMPARE(session.document().annotations.size(), std::size_t{0});
  }

  void resizeRotateAndReorder() {
    EditorSession session(document());
    const QUuid a = *session.createRectangle({1, 1}, {4, 4});
    const QUuid b = *session.createRectangle({2, 2}, {5, 5});
    session.select({a});
    QVERIFY(!session.resizeSelection(-3, 0));  // would collapse to zero width
    QVERIFY(session.resizeSelection(2, 1));
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).end.x, 6.0);
    QVERIFY(session.rotateSelection(-90));
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).style.rotationDegrees, 270.0);
    QVERIFY(session.reorderSelection(EditorSession::Order::Front));
    QCOMPARE(annotationId(session.document().annotations[1]), a);
    QVERIFY(!session.reorderSelection(EditorSession::Order::Front));  // already on top
    QVERIFY(session.reorderSelection(EditorSession::Order::Backward));
    QCOMPARE(annotationId(session.document().annotations[0]), a);
    QCOMPARE(annotationId(session.document().annotations[1]), b);
    const int undos = session.undoCount();
    session.undo();
    QCOMPARE(session.undoCount(), undos - 1);  // each was one operation
  }
};

QTEST_GUILESS_MAIN(EditorSessionTest)
#include "EditorSessionTest.moc"
