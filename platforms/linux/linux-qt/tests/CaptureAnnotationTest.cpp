#include "CapturePipeline.h"
#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "RectanglePropertiesDialog.h"
#include "AnnotationObjectNavigator.h"
#include "SelectionRotationDialog.h"
#include "NativeEditorPreferences.h"
#include <QApplication>
#include <QAction>
#include <QLabel>
#include <QCheckBox>
#include <QColorDialog>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QLineEdit>
#include <QKeyEvent>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QToolBar>
#include <QToolButton>
#include <QAccessible>
#include <QPointer>
#include <QScopeGuard>
#include <cmath>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(80, 60, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
CaptureAnnotationDialog *dialog() { return qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget()); }
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
CaptureAnnotationChoice rectangle(const LoadedSource &source) {
  xerahs::editor::EditorSession session(*source.document); session.createRectangle({20, 20}, {40, 40}); return {session.document(), {}};
}
}
class CaptureAnnotationTest final : public QObject {
  Q_OBJECT
  QTemporaryDir dialogPreferences;
private slots:
  void initTestCase() {
    QVERIFY(dialogPreferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dialogPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dialogPreferences.path());
  }
  void captureKeyboardTransformsPublishAcceptedStateAndPreserveSource_data() {
    QTest::addColumn<int>("transform"); QTest::addColumn<QString>("mode");
    for (int transform = 0; transform < 3; ++transform) for (const auto *mode : {"apply", "protected", "cancel", "begin-destroy", "finish-destroy"}) QTest::newRow(qPrintable(QString("%1-%2").arg(transform).arg(mode))) << transform << QString(mode);
  }
  void captureKeyboardTransformsPublishAcceptedStateAndPreserveSource() {
    QFETCH(int, transform); QFETCH(QString, mode); using namespace xerahs::editor; QTemporaryDir dir; const auto prefs = dir.filePath("preferences.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty()); const auto resetPreferences = qScopeGuard([] { initializeNativeEditorPreferences(); });
    QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved); const auto revision = nativeEditorPreferenceRevision(); QFile pref(prefs); QVERIFY(pref.open(QIODevice::ReadOnly)); const auto preferenceBytes = pref.readAll(); pref.close();
    AnnotationDocument original, expected; bool acted = false; CaptureActionPorts ports; ports.annotationStyle.strokeWidth = 3;
    ports.annotation = [&](const LoadedSource &source, const CancellationToken &) {
      auto prepared = source; auto &d = *prepared.document; RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = {10, 10}; a.end = {30, 20}; a.style = {0xFFFF0000, 0xFFFF0000, 1, 0, 1}; a.extra = {{"vendor", "keep"}}; auto b = a; b.id = QUuid::createUuid(); b.start = {45, 30}; b.end = {65, 40}; b.style.fillColor = b.style.strokeColor = 0xFF00FF00; if (mode == "protected") b.extra = {{"locked", true}};
      const auto id = QUuid::createUuid(); d.annotations = {a, UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"payload", "keep"}}}, b}; d.embeddedImages = {{"asset", d.sourceImagePng}}; d.extra = {{"vendor", "root"}}; original = d;
      QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(prepared, nullptr, ports.annotationStyle)); CaptureAnnotationChoice choice;
      connect(capture, &QDialog::finished, capture, [&choice, capture](int outcome) { if (capture && outcome == QDialog::Accepted) choice = {capture->document(), {}, capture->sidecarIntent()}; });
      capture->exec(); if (capture) delete capture.data(); return choice;
    };
    QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *capture = dialog(); if (!capture || acted) return; acted = true; const QPointer<CaptureAnnotationDialog> owner(capture); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); }); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(canvas && objects); capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow());
      QVERIFY(canvas->selectAnnotations({annotationId(original.annotations[0]), annotationId(original.annotations[1]), annotationId(original.annotations[2])}, canvas->annotationState(), canvas->documentGeneration())); const auto selection = canvas->annotationSelection(); QCOMPARE(canvas->creationStrokeWidth(), 3.0); QVERIFY(canvas->accessibleDescription().contains("Shift+arrows"));
      const int key = transform == 2 ? Qt::Key_R : transform == 1 ? Qt::Key_Down : Qt::Key_Right; const auto modifiers = transform == 2 ? Qt::NoModifier : transform == 1 ? Qt::AltModifier | Qt::ShiftModifier : Qt::ShiftModifier;
      if (mode.contains("destroy")) connect(canvas, &EditorCanvas::changed, capture, [owner, canvas, mode] { if (owner && canvas->interactionActive() == (mode == "begin-destroy")) delete owner.data(); });
      QSignalSpy rejected(canvas, &EditorCanvas::editRejected); if (mode.contains("destroy")) { QKeyEvent event(QEvent::KeyPress, key, modifiers); QApplication::sendEvent(canvas, &event); QVERIFY(!owner); return; }
      QTest::keyClick(canvas, Qt::Key(key), modifiers); expected = original;
      if (mode != "protected") for (int index : {0, 2}) { auto &r = std::get<RectangleAnnotation>(expected.annotations[index]); if (transform == 2) r.style.rotationDegrees = 90; else if (transform == 1) r.end.y += 10; else { r.start.x += 10; r.end.x += 10; } }
      QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection); QCOMPARE(rejected.count(), mode == "protected" ? 1 : 0); QCOMPARE(objects->model()->rowCount(), 3); QCOMPARE(canvas->creationStrokeWidth(), 3.0);
      if (mode == "protected") { QCOMPARE(rejected[0][0].toString(), QString("annotation-selection-protected")); QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); }
      else { QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(original)); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled());
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!capture->findChild<QAction *>("redo")->isEnabled()); }
      if (mode == "cancel") { capture->reject(); return; } capture->findChild<QCheckBox *>("captureSaveAnnotationSidecar")->setChecked(true);
      if (transform == 1 && mode == "apply" && qEnvironmentVariableIsSet("XERAHS_KEYBOARD_QA")) QVERIFY(capture->grab().save(qEnvironmentVariable("XERAHS_KEYBOARD_QA"))); capture->accept();
    });
    timer.start(5); CapturePipeline pipeline(pixels(), dir.path(), "keyboard", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); timer.stop(); QVERIFY(acted); const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(QImage(result->file->path), result->workingMedia);
    if (mode.contains("destroy") || mode == "cancel") { QVERIFY(!result->annotationDocument); QCOMPARE(result->workingMedia, pixels()); QVERIFY(!QFile::exists(result->file->path + ".xann")); }
    else { QVERIFY(result->annotationDocument && result->sidecarIntent); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected)); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels()); const auto reopened = loadSource(result->file->path); QVERIFY(reopened.document); QCOMPARE(serializeDocument(*reopened.document).value("annotations"), serializeDocument(expected).value("annotations")); QCOMPARE(reopened.document->embeddedImages, original.embeddedImages); QCOMPARE(reopened.document->sourceImagePng, original.sourceImagePng); QCOMPARE(reopened.document->extra, original.extra);
      if (mode == "protected") QCOMPARE(result->workingMedia.pixelColor(15, 15), QColor(Qt::red)); else if (transform == 0) { QCOMPARE(result->workingMedia.pixelColor(15, 15), QColor(Qt::blue)); QCOMPARE(result->workingMedia.pixelColor(35, 15), QColor(Qt::red)); }
      else if (transform == 1) { QCOMPARE(result->workingMedia.pixelColor(15, 25), QColor(Qt::red)); QCOMPARE(result->workingMedia.pixelColor(15, 35), QColor(Qt::blue)); }
      else { QCOMPARE(result->workingMedia.pixelColor(12, 15), QColor(Qt::blue)); QCOMPARE(result->workingMedia.pixelColor(20, 7), QColor(Qt::red)); }
    }
    QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(pref.open(QIODevice::ReadOnly)); QCOMPARE(pref.readAll(), preferenceBytes);
  }
  void captureDeleteIsKeyboardAccessibleAndPublishesOnlyAcceptedState_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"button", "delete", "backspace", "all", "legacy-alias", "empty", "hidden", "locked", "opaque-lock", "start-destroy", "finish-destroy", "undo-destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void captureDeleteIsKeyboardAccessibleAndPublishesOnlyAcceptedState() {
    using namespace xerahs::editor; QFETCH(QString, mode); QTemporaryDir dir; const auto prefs = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty()); const auto resetPreferences = qScopeGuard([] { initializeNativeEditorPreferences(); }); QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved); const auto values = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); QFile pref(prefs); QVERIFY(pref.open(QIODevice::ReadOnly)); const auto preferenceBytes = pref.readAll(); pref.close();
    AnnotationDocument original, expected; bool acted = false; CaptureActionPorts ports; ports.annotationStyle.strokeWidth = 3;
    ports.annotation = [&](const LoadedSource &source, const CancellationToken &) {
      auto prepared = source; auto &d = *prepared.document; RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = {10, 10}; a.end = {30, 30}; a.style = {0xFFFF0000, 0xFFFF0000, 1, 0, 1}; auto b = a; b.id = QUuid::createUuid(); b.start = {45, 30}; b.end = {65, 50}; b.style.fillColor = b.style.strokeColor = 0xFF00FF00;
      if (mode == "hidden") b.visible = false; if (mode == "locked") b.extra = {{"locked", true}}; const auto id = QUuid::createUuid(); QJsonObject raw{{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"payload", "keep"}}; if (mode == "opaque-lock") raw.insert("locked", "future-lock"); d.annotations = {a, UnsupportedAnnotation{id, raw}, b}; d.embeddedImages = {{"asset", d.sourceImagePng}}; original = d;
      QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(prepared, nullptr, ports.annotationStyle)); CaptureAnnotationChoice choice; connect(capture, &QDialog::finished, capture, [&choice, capture] { if (capture) choice = {capture->document(), {}, capture->sidecarIntent()}; }); capture->exec(); if (capture) delete capture.data(); return choice;
    };
    QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *capture = dialog(); if (!capture || acted) return; acted = true; const QPointer<CaptureAnnotationDialog> owner(capture); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); }); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(canvas && objects); capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow());
      const bool protectedSelection = mode == "hidden" || mode == "locked" || mode == "opaque-lock"; const bool all = mode == "all" || mode == "legacy-alias" || mode == "hidden" || mode == "locked";
      const QList<QUuid> selected = mode == "empty" ? QList<QUuid>{} : all ? QList<QUuid>{annotationId(original.annotations[0]), annotationId(original.annotations[1]), annotationId(original.annotations[2])} : QList<QUuid>{annotationId(original.annotations[0]), annotationId(original.annotations[1])}; canvas->selectAnnotations(selected, canvas->annotationState(), canvas->documentGeneration()); const auto selection = canvas->annotationSelection(); auto *action = capture->findChild<QAction *>("deleteSelection"); QVERIFY(action); QCOMPARE(action->isEnabled(), !protectedSelection && mode != "empty");
      QToolButton *button = nullptr; for (auto *bar : capture->findChildren<QToolBar *>()) if (bar->actions().contains(action)) button = qobject_cast<QToolButton *>(bar->widgetForAction(action)); QVERIFY(button && button->isVisible()); QCOMPARE(button->focusPolicy(), Qt::StrongFocus); auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible); QCOMPARE(accessible->role(), QAccessible::Button); QCOMPARE(accessible->text(QAccessible::Name), QString("Delete"));
      if (mode == "start-destroy" || mode == "finish-destroy") connect(canvas, &EditorCanvas::changed, capture, [owner, canvas, mode] { if (owner && canvas->interactionActive() == (mode == "start-destroy")) delete owner.data(); });
      if (mode == "delete" || mode == "backspace") QTest::keyClick(canvas, mode == "delete" ? Qt::Key_Delete : Qt::Key_Backspace); else if (action->isEnabled()) { button->setFocus(); QTest::keyClick(button, Qt::Key_Space); }
      if (mode == "start-destroy" || mode == "finish-destroy") { QVERIFY(!owner); return; }
      expected = original; if (!protectedSelection && mode != "empty") expected.annotations = mode == "all" || mode == "legacy-alias" ? std::vector<Annotation>{} : std::vector<Annotation>{original.annotations[2]}; QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(objects->model()->rowCount(), int(expected.annotations.size())); QCOMPARE(canvas->creationStrokeWidth(), 3.0);
      if (!protectedSelection && mode != "empty") {
        QVERIFY(canvas->annotationSelection().isEmpty()); QVERIFY(!action->isEnabled());
        if (mode == "undo-destroy") connect(action, &QAction::changed, capture, [owner, action] { if (owner && action->isEnabled()) delete owner.data(); });
        canvas->setFocus(); if (mode == "undo-destroy") capture->findChild<QAction *>("undo")->trigger(); else QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); if (mode == "undo-destroy") { QVERIFY(!owner); return; }
        QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(original)); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(action->isEnabled()); QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled());
        QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QVERIFY(canvas->annotationSelection().isEmpty()); QVERIFY(!capture->findChild<QAction *>("redo")->isEnabled());
      } else { QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); }
      if (mode == "legacy-alias") { QFile alias(dir.filePath("delete.xann")); QVERIFY(alias.open(QIODevice::WriteOnly)); QCOMPARE(alias.write("KEEP LEGACY"), qint64(11)); }
      capture->findChild<QCheckBox *>("captureSaveAnnotationSidecar")->setChecked(true); if (mode == "button" && qEnvironmentVariableIsSet("XERAHS_DELETE_QA")) QVERIFY(capture->grab().save(qEnvironmentVariable("XERAHS_DELETE_QA"))); capture->accept();
    });
    timer.start(5); CapturePipeline pipeline(pixels(), dir.path(), "delete", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); timer.stop(); QVERIFY(acted); const auto result = receipt(done); QVERIFY(result->file); QCOMPARE(QImage(result->file->path), result->workingMedia);
    if (mode == "legacy-alias") { QVERIFY(result->annotationDocument && result->annotationDocument->annotations.empty()); QCOMPARE(result->workingMedia, pixels()); QCOMPARE(result->pipeline.orderedResults.back().state, ActionState::Failed); QCOMPARE(result->pipeline.orderedResults.back().diagnostic, std::optional<QString>("sidecar-save-failed")); QVERIFY(result->retryPlan().available()); QVERIFY(!QFile::exists(result->file->path + ".xann")); QFile alias(dir.filePath("delete.xann")); QVERIFY(alias.open(QIODevice::ReadOnly)); QCOMPARE(alias.readAll(), QByteArray("KEEP LEGACY")); }
    else if (mode.contains("destroy")) { QVERIFY(!result->annotationDocument); QCOMPARE(result->workingMedia, pixels()); QVERIFY(!QFile::exists(result->file->path + ".xann")); }
    else { QVERIFY(result->annotationDocument && result->sidecarIntent); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected)); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels()); QCOMPARE(result->annotationDocument->embeddedImages, original.embeddedImages);
      const bool protectedSelection = mode == "hidden" || mode == "locked" || mode == "opaque-lock"; QCOMPARE(result->workingMedia.pixelColor(15, 15), QColor(protectedSelection || mode == "empty" ? Qt::red : Qt::blue)); QCOMPARE(result->workingMedia.pixelColor(50, 35), QColor(mode == "hidden" || mode == "all" ? Qt::blue : Qt::green)); const auto reopened = loadSource(result->file->path); QVERIFY(reopened.document);
      if (mode == "all") { QVERIFY(!QFile::exists(result->file->path + ".xann")); QVERIFY(reopened.document->annotations.empty()); } else { QCOMPARE(serializeDocument(*reopened.document).value("annotations"), serializeDocument(expected).value("annotations")); QCOMPARE(reopened.document->embeddedImages, expected.embeddedImages); }
    }
    QCOMPARE(nativeEditorPreferences().values, values); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(pref.open(QIODevice::ReadOnly)); QCOMPARE(pref.readAll(), preferenceBytes);
  }
  void captureOrderingButtonsKeepGroupOrderThroughHistoryAndPublication_data() {
    QTest::addColumn<QString>("name"); QTest::addColumn<QString>("indices"); QTest::addColumn<bool>("blue");
    QTest::newRow("forward") << QString("bringForward") << QString("02143") << true;
    QTest::newRow("backward") << QString("sendBackward") << QString("10324") << false;
    QTest::newRow("front") << QString("bringToFront") << QString("02413") << true;
    QTest::newRow("back") << QString("sendToBack") << QString("13024") << false;
  }
  void captureOrderingButtonsKeepGroupOrderThroughHistoryAndPublication() {
    using namespace xerahs::editor; QFETCH(QString, name); QFETCH(QString, indices); QFETCH(bool, blue); QTemporaryDir dir; const auto prefs = dir.filePath("preferences.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty());
    const auto resetPreferences = qScopeGuard([] { initializeNativeEditorPreferences(); }); QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved); const auto values = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); QFile file(prefs); QVERIFY(file.open(QIODevice::ReadOnly)); const auto preferenceBytes = file.readAll(); file.close();
    AnnotationDocument original, expected; bool acted = false; CaptureActionPorts ports; ports.annotationStyle.strokeWidth = 3;
    // Use the production annotation port with a fixture-populated native
    // dialog. The normal worker validates and publishes its accepted document.
    ports.annotation = [&](const LoadedSource &source, const CancellationToken &) {
      auto prepared = source; auto &doc = *prepared.document;
      for (Argb color : {Argb(0xFFFF0000), Argb(0xFF00FF00), Argb(0), Argb(0xFF0000FF), Argb(0xFFFFFF00)}) {
        RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {10, 10}; r.end = {60, 45}; r.style = {color, color, 1, 0, 1}; r.extra = {{"vendor", "keep"}}; doc.annotations.push_back(r);
      }
      const auto id = annotationId(doc.annotations[2]); doc.annotations[2] = UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"zIndex", 99}, {"payload", "keep"}}}; doc.embeddedImages = {{"asset", doc.sourceImagePng}}; original = doc;
      QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(prepared, nullptr, ports.annotationStyle)); CaptureAnnotationChoice choice;
      connect(capture, &QDialog::finished, capture, [&choice, capture] { if (capture) choice = {capture->document(), {}, capture->sidecarIntent()}; });
      capture->exec(); if (capture) delete capture.data(); return choice;
    };
    QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *capture = dialog(); if (!capture || acted) return; acted = true; const QPointer<CaptureAnnotationDialog> owner(capture); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(canvas && objects); capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow());
      QVERIFY(canvas->selectAnnotations({annotationId(original.annotations[1]), annotationId(original.annotations[3])}, canvas->annotationState(), canvas->documentGeneration())); const auto selection = canvas->annotationSelection(); auto *bar = capture->findChild<QToolBar *>("captureOrderToolbar"); auto *action = capture->findChild<QAction *>(name); QVERIFY(bar && action && action->isEnabled()); QCOMPARE(bar->actions().size(), 4);
      auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action)); QVERIFY(button && button->isVisible()); QCOMPARE(button->focusPolicy(), Qt::StrongFocus); auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible); QCOMPARE(accessible->role(), QAccessible::Button); QCOMPARE(accessible->text(QAccessible::Name), action->text());
      button->setFocus(); QTest::keyClick(button, Qt::Key_Space); expected = original; expected.annotations.clear(); for (auto c : indices) expected.annotations.push_back(original.annotations[c.digitValue()]); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection); QCOMPARE(canvas->creationStrokeWidth(), 3.0);
      for (int row = 0; row < 5; ++row) QCOMPARE(objects->model()->index(row, 0).data(Qt::UserRole).toUuid(), annotationId(expected.annotations[size_t(row)]));
      canvas->setFocus(); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(original)); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); QVERIFY(action->isEnabled());
      QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!capture->findChild<QAction *>("redo")->isEnabled());
      QVERIFY(canvas->selectAnnotations({}, canvas->annotationState(), canvas->documentGeneration())); for (auto *orderAction : bar->actions()) QVERIFY(!orderAction->isEnabled());
      QVERIFY(canvas->selectAnnotations(selection.values(), canvas->annotationState(), canvas->documentGeneration())); capture->findChild<QCheckBox *>("captureSaveAnnotationSidecar")->setChecked(true);
      if (name == "bringToFront" && qEnvironmentVariableIsSet("XERAHS_ORDER_QA")) QVERIFY(capture->grab().save(qEnvironmentVariable("XERAHS_ORDER_QA")));
      capture->accept();
    });
    timer.start(5); CapturePipeline pipeline(pixels(), dir.path(), "order", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1); timer.stop(); QVERIFY(acted); const auto result = receipt(done); QVERIFY(result->annotationDocument && result->file && result->sidecarIntent); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected));
    QCOMPARE(result->workingMedia.pixelColor(30, 30), QColor(blue ? Qt::blue : Qt::yellow)); QCOMPARE(result->workingMedia.pixelColor(70, 50), QColor(Qt::blue)); QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    const auto reopened = loadSource(result->file->path); QVERIFY(reopened.document); QCOMPARE(serializeDocument(*reopened.document).value("annotations"), serializeDocument(expected).value("annotations")); QCOMPARE(reopened.document->embeddedImages, expected.embeddedImages);
    QCOMPARE(nativeEditorPreferences().values, values); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), preferenceBytes);
  }
  void captureThicknessControlsStayLocalThroughHistoryAndPublication_data() {
    QTest::addColumn<bool>("increase"); QTest::addColumn<bool>("defaultsOnly");
    for (bool increase : {false, true}) for (bool defaultsOnly : {false, true}) QTest::newRow(qPrintable(QString("%1-%2").arg(increase ? "increase" : "decrease", defaultsOnly ? "defaults" : "selected"))) << increase << defaultsOnly;
  }
  void captureThicknessControlsStayLocalThroughHistoryAndPublication() {
    using namespace xerahs::editor; QFETCH(bool, increase); QFETCH(bool, defaultsOnly); QTemporaryDir dir; const auto prefs = dir.filePath("preferences.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty());
    const auto resetPreferences = qScopeGuard([] { initializeNativeEditorPreferences(); }); QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved);
    const auto preferences = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); QFile preferenceFile(prefs); QVERIFY(preferenceFile.open(QIODevice::ReadOnly)); const auto preferenceBytes = preferenceFile.readAll(); preferenceFile.close();
    CaptureActionPorts ports; ports.annotationStyle = {0xFFFF0000, 0xFF00FF00, 4, 0, 1}; CapturePipeline pipeline(pixels(), dir.path(), "thickness", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); const QPointer<CaptureAnnotationDialog> capture(dialog()); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); canvas->zoomToFit();
    const auto draw = [&](QPointF start) { QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(start).toPoint()); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(start + QPointF(15, 15)).toPoint()); };
    if (!defaultsOnly) { draw({20, 20}); draw({55, 35}); objects->setFocus(); QTest::keyClick(objects, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(objects, Qt::Key_Return); }
    const auto original = canvas->annotationDocument(); const auto before = serializeDocument(original); const auto selection = canvas->annotationSelection(); auto *bar = capture->findChild<QToolBar *>("captureColorToolbar");
    auto *action = capture->findChild<QAction *>(increase ? "increaseStroke" : "decreaseStroke"); QVERIFY(action && action->isEnabled()); auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action)); QVERIFY(button && button->focusPolicy() & Qt::TabFocus);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).contains("Stroke"));
    capture->activateWindow(); button->setFocus(); QTRY_VERIFY(capture->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), button); QTest::keyClick(button, Qt::Key_Space);
    const double width = increase ? 5 : 3; auto expected = original;
    for (auto &r : expected.annotations) std::get<RectangleAnnotation>(r).style.strokeWidth = width;
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection); QCOMPARE(canvas->creationStrokeWidth(), width);
    auto *label = capture->findChild<QLabel *>("captureStrokeWidth"); QVERIFY(label && label->isVisible()); QVERIFY(label->text().contains(QString::number(width)));
    auto *labelAccess = QAccessible::queryAccessibleInterface(label); QVERIFY(labelAccess); QCOMPARE(labelAccess->text(QAccessible::Description), label->text());
    capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    if (!defaultsOnly) { QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationSelection(), selection); QCOMPARE(canvas->creationStrokeWidth(), width); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); }
    else { QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); draw({20, 20}); expected = canvas->annotationDocument(); QCOMPARE(std::get<RectangleAnnotation>(expected.annotations[0]).style.strokeWidth, width); }
    QVERIFY(!canvas->interactionActive()); QVERIFY(capture->findChild<QAction *>("undo")->isEnabled()); QVERIFY(capture->findChild<QAction *>("rotateSelection")->isEnabled());
    if (increase && !defaultsOnly && qEnvironmentVariableIsSet("XERAHS_THICKNESS_QA")) QVERIFY(capture->grab().save(qEnvironmentVariable("XERAHS_THICKNESS_QA")));
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(preferenceFile.open(QIODevice::ReadOnly)); QCOMPARE(preferenceFile.readAll(), preferenceBytes); preferenceFile.close();
    capture->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->annotationDocument && result->file); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected));
    QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(result->workingMedia.pixelColor(27, 27), QColor(Qt::green)); QCOMPARE(result->workingMedia.pixelColor(45, 20), QColor(Qt::blue)); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(nativeEditorPreferenceRevision(), revision);
  }
  void captureColorChoicesPreserveAlphaAndStayLocalBeforePublication_data() {
    QTest::addColumn<bool>("fill"); QTest::addColumn<QString>("mode");
    for (bool fill : {false, true}) for (const auto *mode : {"apply", "cancel", "defaults-only", "destroy"})
      QTest::newRow(qPrintable(QString(fill ? "fill-" : "stroke-") + mode)) << fill << QString(mode);
  }
  void captureColorChoicesPreserveAlphaAndStayLocalBeforePublication() {
    using namespace xerahs::editor; QFETCH(bool, fill); QFETCH(QString, mode); QTemporaryDir dir;
    const QString prefs = dir.filePath("editor-preferences.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty());
    const auto resetPreferences = qScopeGuard([] { initializeNativeEditorPreferences(); });
    QVERIFY(applyDrawingDefaults({{"BorderColorHex", "#FF112233"}, {"FillColorHex", "#40223344"}}, nativeEditorPreferenceRevision()).saved);
    const auto preferences = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision();
    QFile preferenceFile(prefs); QVERIFY(preferenceFile.open(QIODevice::ReadOnly)); const auto preferenceBytes = preferenceFile.readAll(); preferenceFile.close();
    CapturePipeline pipeline(pixels(), dir.path(), "colour", {"annotation", "save"}); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); const QPointer<CaptureAnnotationDialog> capture(dialog());
    auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(canvas && objects); canvas->zoomToFit();
    const auto draw = [&](QPointF origin) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin + QPointF(20, 20)).toPoint());
    };
    if (mode != "defaults-only") {
      draw({10, 10}); draw({45, 30}); QCOMPARE(objects->model()->rowCount(), 2);
      objects->setFocus(); QTest::keyClick(objects, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(objects, Qt::Key_Return);
    }
    const auto original = canvas->annotationDocument(); const auto before = serializeDocument(original); const auto selection = canvas->annotationSelection();
    auto *action = capture->findChild<QAction *>(fill ? "fillColour" : "strokeColour"); QVERIFY(action && action->isEnabled());
    auto *bar = capture->findChild<QToolBar *>("captureColorToolbar"); QVERIFY(bar);
    auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action)); QVERIFY(button && button->focusPolicy() & Qt::TabFocus);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).contains(fill ? "Fill" : "Stroke"));
    constexpr Argb chosen = 0x8000FF00; bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); if (!picker || acted) return; acted = true;
      const QPointer<QColorDialog> owner(picker); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      picker->activateWindow(); QTRY_VERIFY(picker->isActiveWindow()); QCOMPARE(picker->objectName(), QString("annotationColorDialog"));
      QVERIFY(picker->testOption(QColorDialog::ShowAlphaChannel)); QCOMPARE(picker->currentColor().rgba(), fill ? quint32(0) : quint32(0xFFFF0000));
      QVERIFY(picker->findChild<QLabel *>("annotationColorScope")->text().contains("this capture"));
      QVERIFY(canvas->interactionActive()); QVERIFY(!action->isEnabled()); QVERIFY(!objects->isEnabled());
      capture->accept(); QVERIFY(capture->isVisible()); QCOMPARE(done.count(), 0); picker->setCurrentColor(QColor::fromRgba(chosen));
      QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(nativeEditorPreferences().values, preferences);
      if (mode == "destroy") { delete capture.data(); return; }
      if (mode == "cancel") QTest::keyClick(picker, Qt::Key_Escape);
      else { auto *ok = picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); ok->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), ok); QTest::keyClick(ok, Qt::Key_Space); }
      QTRY_VERIFY(!picker->isVisible());
    });
    capture->activateWindow(); button->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), button);
    timer.start(5); QTest::keyClick(button, Qt::Key_Space); timer.stop(); QVERIFY(acted);
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    QVERIFY(preferenceFile.open(QIODevice::ReadOnly)); QCOMPARE(preferenceFile.readAll(), preferenceBytes); preferenceFile.close();
    if (!capture) { QTRY_COMPARE(done.count(), 1); QCOMPARE(receipt(done)->workingMedia, pixels()); return; }
    QVERIFY(!canvas->interactionActive()); QVERIFY(action->isEnabled()); QCOMPARE(canvas->annotationSelection(), selection);
    auto expected = original;
    if (mode == "apply") for (auto &annotation : expected.annotations) {
      auto &style = std::get<RectangleAnnotation>(annotation).style; (fill ? style.fillColor : style.strokeColor) = chosen;
    }
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected));
    capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    if (mode == "apply") {
      QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationSelection(), selection);
      QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected));
    } else if (mode == "defaults-only") {
      QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); draw({10, 10}); expected = canvas->annotationDocument();
      QCOMPARE(expected.annotations.size(), size_t(1)); const auto &style = std::get<RectangleAnnotation>(expected.annotations[0]).style;
      QCOMPARE(fill ? style.fillColor : style.strokeColor, chosen); QCOMPARE(fill ? style.strokeColor : style.fillColor, fill ? quint32(0xFFFF0000) : quint32(0));
    }
    capture->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->annotationDocument && result->file);
    QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected)); QCOMPARE(QImage(result->file->path), result->workingMedia);
    QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    const QColor composed(0, 128, 127); QCOMPARE(result->workingMedia.pixelColor(fill ? QPoint(20, 20) : QPoint(10, 20)),
      mode == "cancel" ? QColor(fill ? Qt::blue : Qt::red) : composed);
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(nativeEditorPreferenceRevision(), revision);
  }
  void nativeGroupRotationInCaptureHasExplicitApplyCancelAndPublication_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"apply", "cancel", "destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void nativeGroupRotationInCaptureHasExplicitApplyCancelAndPublication() {
    using namespace xerahs::editor; QFETCH(QString, mode); QTemporaryDir dir; CaptureActionPorts ports; ports.annotationStyle = {0xFF112233, 0xFF00FF00, 1, 0, 1};
    CapturePipeline pipeline(pixels(), dir.path(), "rotation", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); const QPointer<CaptureAnnotationDialog> capture(dialog()); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); canvas->zoomToFit();
    for (const QPointF origin : {QPointF(10, 10), QPointF(45, 30)}) { QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin).toPoint()); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin + QPointF(20, 10)).toPoint()); }
    objects->setFocus(); QTest::keyClick(objects, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(objects, Qt::Key_Return); const auto original = canvas->annotationDocument(); const auto selection = canvas->annotationSelection(); const auto before = serializeDocument(original);
    auto *action = capture->findChild<QAction *>("rotateSelection"); QVERIFY(action && action->isEnabled()); auto *button = qobject_cast<QToolButton *>(capture->findChild<QToolBar *>()->widgetForAction(action)); QVERIFY(button && button->focusPolicy() & Qt::TabFocus);
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *editor = qobject_cast<SelectionRotationDialog *>(QApplication::activeModalWidget()); if (!editor || acted) return; acted = true;
      const QPointer<SelectionRotationDialog> owner(editor); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow());
      QVERIFY(canvas->interactionActive()); QVERIFY(!action->isEnabled()); QVERIFY(!objects->isEnabled()); capture->accept(); QVERIFY(capture->isVisible()); QCOMPARE(done.count(), 0);
      QCOMPARE(serializeDocument(canvas->annotationDocument()), before); editor->findChild<QLineEdit *>("selectionRotationDegrees")->setText("90");
      if (mode == "destroy") { delete capture.data(); return; }
      QTest::keyClick(editor, mode == "apply" ? Qt::Key_R : Qt::Key_Escape, mode == "apply" ? Qt::AltModifier : Qt::NoModifier);
      QTRY_VERIFY(!editor->isVisible());
    });
    button->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), button); timer.start(5); QTest::keyClick(button, Qt::Key_Space); timer.stop(); QVERIFY(acted);
    if (!capture) { QTRY_COMPARE(done.count(), 1); QCOMPARE(receipt(done)->workingMedia, pixels()); return; }
    QVERIFY(!canvas->interactionActive()); QVERIFY(action->isEnabled()); auto expected = original;
    if (mode == "apply") for (auto &a : expected.annotations) std::get<RectangleAnnotation>(a).style.rotationDegrees = 90;
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QCOMPARE(canvas->annotationSelection(), selection);
    capture->activateWindow(); canvas->setFocus(); QTRY_VERIFY(capture->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    if (mode == "apply") { QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); }
    capture->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected));
    QVERIFY(result->file); QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    QCOMPARE(result->workingMedia.pixelColor(20, 7), mode == "apply" ? QColor(Qt::green) : QColor(Qt::blue)); QCOMPARE(result->workingMedia.pixelColor(12, 15), mode == "apply" ? QColor(Qt::blue) : QColor(Qt::green));
  }
  void nativeGroupMoveCommitsOnceBeforeActualCapturePublication() {
    using namespace xerahs::editor; QTemporaryDir dir; CaptureActionPorts ports; ports.annotationStyle = {0xFF112233, 0xFF00FF00, 1, 0, 1};
    CapturePipeline pipeline(pixels(), dir.path(), "group-move", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); auto *capture = dialog(); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(objects);
    canvas->zoomToFit();
    for (const QPointF origin : {QPointF(10, 10), QPointF(45, 30)}) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin + QPointF(15, 15)).toPoint());
    }
    QCOMPARE(objects->model()->rowCount(), 2); objects->setFocus(); QTest::keyClick(objects, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(objects, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    const auto original = canvas->annotationDocument(); const auto before = serializeDocument(original); const auto selection = canvas->annotationSelection(); QCOMPARE(selection.size(), 2);
    const auto zoom = canvas->viewState().zoom(); const auto press = canvas->viewState().toView({17.5, 17.5}).toPoint(); const QPoint screenDelta(24, 16);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, press); QVERIFY(canvas->interactionActive()); QVERIFY(!objects->isEnabled());
    QVERIFY(!capture->findChild<QAction *>("undo")->isEnabled()); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    QTest::mouseMove(canvas->viewport(), press + screenDelta); capture->accept(); QVERIFY(capture->isVisible()); QCOMPARE(done.count(), 0); QCOMPARE(serializeDocument(canvas->annotationDocument()), before);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, press + screenDelta); QVERIFY(objects->isEnabled()); QVERIFY(capture->findChild<QAction *>("undo")->isEnabled());
    auto expected = original;
    for (size_t i = 0; i < expected.annotations.size(); ++i) { auto &r = std::get<RectangleAnnotation>(expected.annotations[i]); const auto &actual = std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[i]);
      QVERIFY(std::abs(actual.start.x - (r.start.x + 24 / zoom)) < 1e-10); QVERIFY(std::abs(actual.end.x - (r.end.x + 24 / zoom)) < 1e-10);
      QVERIFY(std::abs(actual.start.y - (r.start.y + 16 / zoom)) < 1e-10); QVERIFY(std::abs(actual.end.y - (r.end.y + 16 / zoom)) < 1e-10); r.start = actual.start; r.end = actual.end;
    }
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationSelection(), selection); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); capture->accept(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(serializeDocument(*result->annotationDocument), serializeDocument(expected));
    QVERIFY(result->file); QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    const auto &moved = std::get<RectangleAnnotation>(expected.annotations[0]); QCOMPARE(result->workingMedia.pixelColor(QPoint(qRound(moved.start.x + 7), qRound(moved.start.y + 7))), QColor(Qt::green));
  }
  void keyboardObjectSelectionEditsBeforeCapturePublication() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "selected", {"annotation", "save"}); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); auto *capture = dialog(); auto *canvas = capture->findChild<EditorCanvas *>(); auto *objects = capture->findChild<AnnotationObjectNavigator *>(); QVERIFY(objects);
    canvas->zoomToFit(); QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({20, 20}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({40, 40}).toPoint()); QCOMPARE(objects->model()->rowCount(), 1);
    objects->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), objects); QTest::keyClick(objects, Qt::Key_Escape);
    auto *properties = capture->findChild<QAction *>("rectangleProperties"); QVERIFY(!properties->isEnabled()); QTest::keyClick(objects, Qt::Key_Space, Qt::ControlModifier); QVERIFY(properties->isEnabled());
    QTest::keyClick(objects, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), canvas); QTest::keyClick(canvas, Qt::Key_Right);
    capture->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->annotationDocument);
    QCOMPARE(result->annotationDocument->annotations.size(), size_t(1)); const auto &r = std::get<xerahs::editor::RectangleAnnotation>(result->annotationDocument->annotations[0]);
    QVERIFY(std::abs(r.start.x - 21) < .2); QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
  }
  void keyboardCreationInCaptureUsesSnapshotBeforeActualPublication_data() {
    QTest::addColumn<bool>("apply"); QTest::newRow("apply") << true; QTest::newRow("cancel") << false;
  }
  void numericRectangleReplacementRefusalPreservesActualCapturePublication_data() {
    QTest::addColumn<bool>("creation"); QTest::addColumn<bool>("cancel");
    for (bool creation : {false, true}) for (bool cancel : {false, true})
      QTest::newRow(qPrintable(QString("%1-%2").arg(creation ? "create" : "properties", cancel ? "cancel" : "accept"))) << creation << cancel;
  }
  void numericRectangleReplacementRefusalPreservesActualCapturePublication() {
    using namespace xerahs::editor; QFETCH(bool, creation); QFETCH(bool, cancel); QTemporaryDir dir; std::unique_ptr<EditorSession> replacement;
    CaptureActionPorts ports; ports.annotationStyle = {0xFF112233, 0xFF00FF00, 1, 0, 1};
    CapturePipeline pipeline(pixels(), dir.path(), "replacement", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); const QPointer<CaptureAnnotationDialog> capture(dialog()); auto *canvas = capture->findChild<EditorCanvas *>(); canvas->zoomToFit();
    for (const QPointF origin : {QPointF(10, 10), QPointF(45, 30)}) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(origin + QPointF(20, 20)).toPoint());
    }
    QTest::keyClick(canvas, Qt::Key_Right); QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier); const auto original = canvas->annotationDocument(); const auto before = serializeDocument(original);
    auto next = original; next.extra = {{"replacement", true}}; replacement = std::make_unique<EditorSession>(next); replacement->select(canvas->annotationSelection().values());
    QVERIFY(replacement->moveSelection(1, 0)); QVERIFY(replacement->moveSelection(-1, 0)); replacement->markSaved(); QVERIFY(replacement->moveSelection(1, 0)); QVERIFY(replacement->undo());
    QCOMPARE(canvas->annotationState(), replacement->stateId()); QCOMPARE(canvas->annotationSelection(), replacement->selection()); const auto replaced = serializeDocument(replacement->document());
    const auto generation = canvas->documentGeneration(); auto *action = capture->findChild<QAction *>(creation ? "createRectangle" : "rectangleProperties"); QVERIFY(action && action->isEnabled());
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      const QPointer<RectanglePropertiesDialog> editor(qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget())); if (!editor || acted) return; acted = true;
      const auto close = qScopeGuard([editor] { if (editor && editor->isVisible()) editor->reject(); }); editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow());
      editor->findChild<QLineEdit *>("rectangleWidth")->setText("12"); QVERIFY(canvas->interactionActive()); QVERIFY(!action->isEnabled()); capture->accept(); QCOMPARE(done.count(), 0);
      canvas->resetDocument(*replacement, pixels()); QCOMPARE(canvas->annotationState(), replacement->stateId()); QVERIFY(canvas->documentGeneration() != generation);
      if (cancel) QTest::keyClick(editor, Qt::Key_Escape);
      else { auto *ok = editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); ok->setFocus(); QTest::keyClick(ok, Qt::Key_Space); }
      QTRY_VERIFY(!editor->isVisible());
    });
    timer.start(5); action->trigger(); timer.stop(); QVERIFY(acted); QVERIFY(!canvas->interactionActive()); QCOMPARE(serializeDocument(replacement->document()), replaced);
    QCOMPARE(replacement->undoCount(), 2); QCOMPARE(replacement->redoCount(), 1); QVERIFY(!replacement->dirty());
    if (!cancel) QVERIFY(capture->findChild<QLabel *>("captureAnnotationStatus")->text().contains("not applied"));
    capture->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QVERIFY(result->annotationDocument && result->file);
    QCOMPARE(serializeDocument(*result->annotationDocument), before); QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(result->workingMedia.pixelColor(20, 20), QColor(Qt::green));
    QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
  }
  void keyboardCreationInCaptureUsesSnapshotBeforeActualPublication() {
    QFETCH(bool, apply); QTemporaryDir dir; CaptureActionPorts ports; ports.annotationStyle = {0xFF112233, 0xFF00FF00, 1, 0, 1};
    CapturePipeline pipeline(pixels(), dir.path(), "keyboard", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); auto *capture = dialog(); auto *canvas = capture->findChild<EditorCanvas *>(); QVERIFY(canvas);
    capture->findChild<QAction *>("selectTool")->trigger(); auto *create = capture->findChild<QAction *>("createRectangle"); QVERIFY(create && create->isEnabled());
    auto *button = qobject_cast<QToolButton *>(capture->findChild<QToolBar *>()->widgetForAction(create)); QVERIFY(button); QVERIFY(button->focusPolicy() & Qt::TabFocus);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible); QVERIFY(accessible->text(QAccessible::Name).contains("Create Rectangle"));
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *editor = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!editor || acted) return; acted = true;
      const QPointer<RectanglePropertiesDialog> owner(editor); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      QCOMPARE(editor->objectName(), QString("rectangleCreationDialog")); QVERIFY(!create->isEnabled()); capture->accept(); QVERIFY(capture->isVisible()); QCOMPARE(done.count(), 0);
      editor->activateWindow();
      for (const auto &entry : {std::pair{"rectangleLeft", "20"}, std::pair{"rectangleTop", "20"}, std::pair{"rectangleWidth", "20"}, std::pair{"rectangleHeight", "20"}}) {
        auto *field = editor->findChild<QLineEdit *>(entry.first); field->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), field);
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(field, entry.second);
      }
      QTest::keyClick(editor, apply ? Qt::Key_R : Qt::Key_Escape, apply ? Qt::AltModifier : Qt::NoModifier);
      QTRY_VERIFY(!editor->isVisible());
    });
    capture->activateWindow(); canvas->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    for (int i = 0; i < 30 && QApplication::focusWidget() != button; ++i) QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
    QCOMPARE(QApplication::focusWidget(), button); timer.start(5); QTest::keyClick(button, Qt::Key_Space); timer.stop(); QVERIFY(acted); QVERIFY(create->isEnabled());
    QCOMPARE(canvas->tool(), EditorCanvas::Tool::Select); capture->accept(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->annotations.size(), apply ? size_t(1) : size_t(0));
    QCOMPARE(result->workingMedia.pixel(30, 30), apply ? qRgb(0, 255, 0) : qRgb(0, 0, 255)); QCOMPARE(QImage(result->file->path), result->workingMedia);
    QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    if (apply) { const auto &r = std::get<xerahs::editor::RectangleAnnotation>(result->annotationDocument->annotations[0]); QCOMPARE(r.style.strokeWidth, 1.0); QCOMPARE(r.style.fillColor, quint32(0xFF00FF00)); QVERIFY(!r.id.isNull()); }
  }
  void selectedPropertiesApplyBeforeCaptureOutputAndKeepDefaults_data() {
    QTest::addColumn<bool>("apply"); QTest::newRow("apply") << true; QTest::newRow("cancel") << false;
  }
  void selectedPropertiesApplyBeforeCaptureOutputAndKeepDefaults() {
    QFETCH(bool, apply);
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "properties", {"annotation", "save"}); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); auto *capture = dialog(); auto *canvas = capture->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->zoomToFit();
    auto *properties = capture->findChild<QAction *>("rectangleProperties"); QVERIFY(properties); QVERIFY(!properties->isEnabled());
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({20, 20}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({40, 40}).toPoint()); QVERIFY(properties->isEnabled());
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *editor = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!editor || acted) return; acted = true;
      capture->accept(); QVERIFY(capture->isVisible()); QCOMPARE(done.count(), 0);
      editor->findChild<QLineEdit *>("rectangleFillColor")->setText("#FF00FF00"); editor->findChild<QLineEdit *>("rectangleStrokeWidth")->setText("1");
      editor->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    timer.start(5); properties->trigger(); timer.stop(); QVERIFY(acted); QVERIFY(properties->isEnabled());
    capture->activateWindow(); canvas->setFocus(); QTRY_COMPARE(QApplication::activeModalWidget(), capture); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    QCOMPARE(canvas->tool(), EditorCanvas::Tool::Rectangle);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({55, 20}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({70, 40}).toPoint()); capture->accept(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->annotations.size(), size_t(2));
    QCOMPARE(result->workingMedia.pixel(30, 30), apply ? qRgb(0, 255, 0) : qRgb(0, 0, 255)); QCOMPARE(result->workingMedia.pixel(62, 30), qRgb(0, 0, 255));
    QCOMPARE(QImage(result->file->path), result->workingMedia); QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    QCOMPARE(std::get<xerahs::editor::RectangleAnnotation>(result->annotationDocument->annotations[1]).style.strokeWidth, 4.0);
  }
  void captureToolModesDoNotCreateInSelectAndCancelLiveCreation() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "tools", {"annotation", "save"});
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(dialog());
    auto *canvas = dialog()->findChild<EditorCanvas *>(); QVERIFY(canvas);
    auto *select = dialog()->findChild<QAction *>("selectTool"), *rectangle = dialog()->findChild<QAction *>("rectangleTool");
    QVERIFY(select && rectangle); auto *instructions = dialog()->findChild<QLabel *>("annotationToolInstructions"); QVERIFY(instructions && instructions->isVisible());
    const auto a = canvas->viewState().toView({20, 20}).toPoint(), b = canvas->viewState().toView({40, 40}).toPoint();
    select->trigger(); QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, a); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, b);
    rectangle->trigger(); QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, a); select->trigger();
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, b);
    rectangle->trigger(); QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, b); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, a);
    QVERIFY(rectangle->isChecked()); dialog()->accept(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->annotations.size(), size_t(1));
    QCOMPARE(result->workingMedia.pixel(20, 20), qRgb(255, 0, 0)); QCOMPARE(QImage(result->file->path), result->workingMedia);
    QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
  }
  void actualCanvasApplyFeedsSaveAndClipboardAndOffersEditablePersistence() {
    QTemporaryDir dir; QImage input = pixels(), copied; CaptureActionPorts ports;
    ports.clipboard = [&](const QImage &image, const CancellationToken &) { copied = image; return ActionOutcome{}; };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline pipeline(input, dir.path(), "capture", {"image-clipboard", "save", "annotation"}, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(dialog());
    QVERIFY(!QFile::exists(dir.filePath("capture.png"))); QVERIFY(!QFile::exists(dir.filePath("state")));
    auto *canvas = dialog()->findChild<EditorCanvas *>(); QVERIFY(canvas);
    const auto start = canvas->viewState().toView({20, 20}).toPoint(); const auto end = canvas->viewState().toView({40, 40}).toPoint();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, start); dialog()->accept(); QVERIFY(dialog()->isVisible()); QCOMPARE(done.count(), 0);
    QTest::mouseMove(canvas->viewport(), end); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, end);
    input.fill(Qt::green); dialog()->accept(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QCOMPARE(result->pipeline.finalWorkingMedia, QString("annotation")); QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->annotations.size(), size_t(1));
    QCOMPARE(result->workingMedia.pixel(20, 20), qRgb(255, 0, 0)); QCOMPARE(result->workingMedia.pixel(0, 0), qRgb(0, 0, 255));
    QCOMPARE(copied, result->workingMedia); QCOMPARE(QImage(result->file->path), result->workingMedia);
    QVERIFY(!result->sidecarIntent); QVERIFY(!QFile::exists(result->file->path + ".xann"));
    QCOMPARE(QImage::fromData(result->annotationDocument->sourceImagePng, "PNG"), pixels());
    const auto editable = prepareCapturedDocument(*result->annotationDocument); QVERIFY(editable.document); QCOMPARE(editable.image, pixels());
    const auto persisted = saveEdit(dir.filePath("editable.png"), editable.image, *editable.document); QVERIFY(persisted.rasterSaved && persisted.sidecarSaved);
    QFile sidecar(dir.filePath("editable.png.xann")); QVERIFY(sidecar.open(QIODevice::ReadOnly)); const auto parsed = xerahs::editor::readXann(sidecar.readAll());
    QVERIFY(parsed.document); QCOMPARE(parsed.document->annotations.size(), size_t(1)); QCOMPARE(QImage::fromData(parsed.document->sourceImagePng, "PNG"), pixels());
    QCOMPARE(QImage(dir.filePath("editable.png")), result->workingMedia);
    QVERIFY(journal->list().entries[0].parsed.record); QCOMPARE(journal->list().entries[0].parsed.record->report.value("final_working_media"), QJsonValue("annotation"));
    QVERIFY(!result->canonicalResult().value("extensions").toObject().value("com.xerahs.app").toObject().contains("sourceImagePng"));
  }
  void escapeSkipsAnnotationAndIndependentOutputsUseOriginalPixels() {
    QTemporaryDir dir; CapturePipeline pipeline(pixels(), dir.path(), "original", {"annotation", "save"}); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_VERIFY(dialog()); dialog()->findChild<QCheckBox *>("captureSaveAnnotationSidecar")->setChecked(true);
    QTest::keyClick(dialog(), Qt::Key_Escape); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done);
    QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Cancelled); QCOMPARE(result->pipeline.orderedResults[1].state, ActionState::Succeeded);
    QCOMPARE(result->pipeline.finalWorkingMedia, QString("capture")); QVERIFY(!result->annotationDocument); QCOMPARE(QImage(result->file->path), pixels());
    QVERIFY(!result->sidecarIntent); QVERIFY(!QFile::exists(result->file->path + ".xann"));
  }
  void rectangleCreationUsesTheFrozenPreferenceSnapshot() {
    QTemporaryDir dir; CaptureActionPorts ports; ports.annotationStyle.fillColor = 0xff00ff00; ports.annotationStyle.strokeWidth = 1;
    CapturePipeline pipeline(pixels(), dir.path(), "style", {"annotation"}, nullptr, ports);
    ports.annotationStyle.fillColor = 0xffff0000; QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_VERIFY(dialog());
    auto *canvas = dialog()->findChild<EditorCanvas *>(); QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({20, 20}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({40, 40}).toPoint()); dialog()->accept(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QVERIFY(result->annotationDocument); QCOMPARE(result->workingMedia.pixel(30, 30), qRgb(0, 255, 0));
    QCOMPARE(std::get<xerahs::editor::RectangleAnnotation>(result->annotationDocument->annotations[0]).style.strokeWidth, 1.0);
  }
  void globalCancellationAndOwnerDestructionReleaseThePickerWithoutOutput() {
    QTemporaryDir dir; auto pipeline = std::make_unique<CapturePipeline>(pixels(), dir.path(), "never", QStringList{"annotation", "save"});
    QSignalSpy done(pipeline.get(), &CapturePipeline::finished); pipeline->start(); QTRY_VERIFY(dialog()); QPointer<CaptureAnnotationDialog> owned(dialog()); pipeline->cancel();
    QTRY_COMPARE(done.count(), 1); QTRY_VERIFY(!owned); QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled); QVERIFY(receipt(done)->files.empty());
    pipeline = std::make_unique<CapturePipeline>(pixels(), dir.path(), "never", QStringList{"annotation", "save"}); pipeline->start(); QTRY_VERIFY(dialog()); owned = dialog(); pipeline.reset(); QVERIFY(!owned);
    QTest::qWait(20); QVERIFY(!QFile::exists(dir.filePath("never.png")));
  }
  void invalidAnnotationCannotReplaceMediaAndDiagnosticsStayValueFree() {
    QTemporaryDir dir; CaptureActionPorts ports;
    ports.annotation = [](const LoadedSource &source, const CancellationToken &) { auto chosen = rectangle(source); chosen.document->sourceImagePng = "private invalid pixels"; return chosen; };
    CapturePipeline pipeline(pixels(), dir.path(), "fallback", {"annotation", "save"}, nullptr, ports); QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto result = receipt(done); QCOMPARE(result->pipeline.orderedResults[0].state, ActionState::Failed); QCOMPARE(result->pipeline.orderedResults[0].diagnostic, std::optional<QString>{"document-invalid"});
    QCOMPARE(QImage(result->file->path), pixels()); QCOMPARE(result->workingMedia, pixels()); QVERIFY(!result->annotationDocument);
  }
  void failedSaveRetryKeepsAnnotatedPixelsAndDocumentWithoutRepeatingAnnotation() {
    QTemporaryDir dir; int annotations = 0, encodes = 0; CaptureActionPorts ports;
    ports.annotation = [&](const LoadedSource &source, const CancellationToken &) { ++annotations; return rectangle(source); };
    ports.save.encode = [&](const QImage &image, QIODevice *device) { return ++encodes > 1 && image.save(device, "PNG"); };
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("state"));
    CapturePipeline first(pixels(), dir.path(), "retry", {"annotation", "save"}, nullptr, ports, journal); QSignalSpy done(&first, &CapturePipeline::finished); first.start(); QTRY_COMPARE(done.count(), 1);
    const auto source = receipt(done); QVERIFY(source->annotationDocument); QVERIFY(source->files.empty()); QCOMPARE(source->workingMedia.pixel(20, 20), qRgb(255, 0, 0)); QVERIFY(source->retryPlan().available());
    CapturePipeline retry(source, nullptr, ports, journal); QSignalSpy next(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(next.count(), 1); const auto result = receipt(next);
    QCOMPARE(annotations, 1); QCOMPARE(result->pipeline.finalWorkingMedia, QString("annotation")); QCOMPARE(result->pipeline.orderedResults.size(), size_t(1));
    QVERIFY(result->annotationDocument); QCOMPARE(result->annotationDocument->sourceImagePng, source->annotationDocument->sourceImagePng); QCOMPARE(result->workingMedia, source->workingMedia); QCOMPARE(QImage(result->file->path), source->workingMedia);
    QVERIFY(result->pipeline.retry->preservedSuccessfulActions.contains("annotation"));
    for (const auto &entry : journal->list().entries) { QVERIFY(entry.parsed.record); QVERIFY(entry.parsed.record->terminal); QCOMPARE(entry.parsed.record->report.value("final_working_media"), QJsonValue("annotation")); }
  }
};
QTEST_MAIN(CaptureAnnotationTest)
#include "CaptureAnnotationTest.moc"
