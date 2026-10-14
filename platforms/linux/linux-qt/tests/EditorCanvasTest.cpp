// Native input tests exercise the actual canvas and shared session model.
#include "EditorCanvas.h"
#include "NativeAnnotationTools.h"
#include "RectanglePropertiesDialog.h"
#include "SelectionRotationDialog.h"
#include <QPlainTextEdit>

#include <QAction>
#include <QAccessible>
#include <QLabel>
#include <QKeyEvent>
#include <QToolBar>
#include <QToolButton>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QWheelEvent>
#include <QTransform>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QScopeGuard>
#include <QPointer>
#include <QColorDialog>
#include <QSettings>
#include <QFocusEvent>
#include <QTemporaryDir>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
AnnotationDocument document() {
  AnnotationDocument d;
  d.canvasWidth = 800;
  d.canvasHeight = 600;
  return d;
}
QImage image() {
  QImage image(800, 600, QImage::Format_ARGB32);
  image.fill(Qt::white);
  return image;
}
void wheel(EditorCanvas &canvas, QPointF at, Qt::KeyboardModifiers modifiers, QPoint delta = {0, 120}) {
  QWheelEvent event(at, canvas.viewport()->mapToGlobal(at.toPoint()), {}, delta, Qt::NoButton, modifiers,
                    Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas.viewport(), &event);
}
}  // namespace

class EditorCanvasTest : public QObject {
  Q_OBJECT
  QTemporaryDir dialogPreferences;
private slots:
  void initTestCase() {
    QVERIFY(dialogPreferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dialogPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dialogPreferences.path());
  }
  void nativeDeleteKeysRemoveOneGroupAndPreserveHistory_data() {
    QTest::addColumn<int>("key"); for (auto key : {Qt::Key_Delete, Qt::Key_Backspace}) QTest::newRow(key == Qt::Key_Delete ? "delete" : "backspace") << int(key);
  }
  void nativeDeleteKeysRemoveOneGroupAndPreserveHistory() {
    QFETCH(int, key); const auto source = image(); EditorSession prepared(document()); for (int i = 0; i < 3; ++i) QVERIFY(prepared.createRectangle({100.0 + i * 150, 100}, {160.0 + i * 150, 160}));
    auto d = prepared.document(); const auto id = annotationId(d.annotations[1]); d.annotations[1] = UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"payload", "keep"}}}; EditorSession session(d); session.select({annotationId(d.annotations[0]), id}); const auto selection = session.selection();
    EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); canvas.activateWindow(); canvas.setFocus(); QTRY_VERIFY(canvas.isActiveWindow()); QTest::keyClick(&canvas, Qt::Key(key));
    auto expected = d; expected.annotations = {d.annotations[2]}; QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QVERIFY(session.selection().isEmpty()); QCOMPARE(session.undoCount(), 1);
    QKeyEvent repeat(QEvent::KeyPress, key, Qt::NoModifier, {}, true); QApplication::sendEvent(&canvas, &repeat); QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(source, image());
  }
  void nativeDeleteGuardsStateSelectionGenerationAndOwner_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"apply", "defaults", "stale-state", "stale-selection", "replacement", "disabled", "begin-destroy", "reject-destroy", "finish-destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void nativeDeleteGuardsStateSelectionGenerationAndOwner() {
    QFETCH(QString, mode); const auto source = image(); EditorSession prepared(document()); const auto a = *prepared.createRectangle({100, 100}, {160, 160}), b = *prepared.createRectangle({300, 100}, {360, 160}); const auto d = prepared.document();
    EditorSession session(d), replacement(d); session.select({a}); replacement.select({a}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); auto expected = serializeDocument(d); auto selection = session.selection(); int undo = 0, redo = 1; bool intervened = false;
    auto canvas = std::make_unique<EditorCanvas>(session, source); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected); if (mode == "disabled") canvas->setEnabled(false); if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    connect(canvas.get(), &EditorCanvas::changed, [&] {
      if (intervened) { if (canvas && !canvas->interactionActive() && mode == "finish-destroy") canvas.reset(); return; }
      if (!canvas->interactionActive()) return; intervened = true; QVERIFY(!canvas->deleteSelection());
      if (mode == "begin-destroy") { canvas.reset(); return; } if (mode == "replacement") canvas->resetDocument(replacement, source);
      if (mode == "stale-selection" || mode == "reject-destroy") { session.select({b}); selection = session.selection(); }
      if (mode == "stale-state") { QVERIFY(session.moveSelection(2, 0)); expected = serializeDocument(session.document()); undo = 1; redo = 0; }
      if (mode == "defaults") { auto defaults = session.toolStyle(); defaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(defaults)); }
    });
    const bool changed = canvas->deleteSelection(); const bool applied = mode == "apply" || mode == "defaults" || mode == "finish-destroy"; QCOMPARE(changed, applied); QCOMPARE(intervened, mode != "disabled");
    if (applied) { auto deleted = d; deleted.annotations = {d.annotations[1]}; QCOMPARE(serializeDocument(session.document()), serializeDocument(deleted)); QVERIFY(session.selection().isEmpty()); QVERIFY(session.undo()); }
    QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), mode == "stale-state"); QCOMPARE(session.toolStyle().strokeWidth, mode == "defaults" ? 9.0 : 4.0); QCOMPARE(replacement.undoCount(), 0); QCOMPARE(serializeDocument(replacement.document()), serializeDocument(d));
    const bool stale = mode.startsWith("stale-") || mode == "replacement" || mode == "reject-destroy"; QCOMPARE(rejected.count(), stale ? 1 : 0); if (stale) QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); if (canvas) QVERIFY(!canvas->interactionActive());
  }
  void catalogueAndBracketOrderingKeysCommitOneGroup_data() {
    QTest::addColumn<int>("key"); QTest::addColumn<bool>("control"); QTest::addColumn<QString>("indices");
    QTest::newRow("home") << int(Qt::Key_Home) << false << QString("02413");
    QTest::newRow("end") << int(Qt::Key_End) << false << QString("13024");
    QTest::newRow("page-up") << int(Qt::Key_PageUp) << false << QString("02143");
    QTest::newRow("page-down") << int(Qt::Key_PageDown) << false << QString("10324");
    QTest::newRow("front-alias") << int(Qt::Key_BracketRight) << true << QString("02413");
    QTest::newRow("back-alias") << int(Qt::Key_BracketLeft) << true << QString("13024");
    QTest::newRow("forward-alias") << int(Qt::Key_BracketRight) << false << QString("02143");
    QTest::newRow("backward-alias") << int(Qt::Key_BracketLeft) << false << QString("10324");
  }
  void catalogueAndBracketOrderingKeysCommitOneGroup() {
    QFETCH(int, key); QFETCH(bool, control); QFETCH(QString, indices); const auto source = image(); EditorSession prepared(document());
    for (int i = 0; i < 5; ++i) QVERIFY(prepared.createRectangle({100.0 + i * 100, 100}, {160.0 + i * 100, 160}));
    const auto d = prepared.document(); EditorSession session(d); session.select({annotationId(d.annotations[1]), annotationId(d.annotations[3])}); const auto selection = session.selection();
    EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); canvas.activateWindow(); canvas.setFocus(); QTRY_VERIFY(canvas.isActiveWindow());
    QSignalSpy rejected(&canvas, &EditorCanvas::editRejected); QTest::keyClick(&canvas, Qt::Key(key), control ? Qt::ControlModifier : Qt::NoModifier);
    auto expected = d; expected.annotations.clear(); for (auto c : indices) expected.annotations.push_back(d.annotations[c.digitValue()]);
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QCOMPARE(rejected.count(), 0);
    QKeyEvent repeat(QEvent::KeyPress, key, control ? Qt::ControlModifier : Qt::NoModifier, {}, true); QApplication::sendEvent(&canvas, &repeat); QCOMPARE(session.undoCount(), 1);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(source, image());
  }
  void nativeOrderingGuardsPendingStateSelectionReplacementAndOwner_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"apply", "defaults", "stale-state", "stale-selection", "replacement", "disabled", "begin-destroy", "reject-destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void nativeOrderingGuardsPendingStateSelectionReplacementAndOwner() {
    QFETCH(QString, mode); const auto source = image(); EditorSession prepared(document());
    const auto a = *prepared.createRectangle({100, 100}, {160, 160}), b = *prepared.createRectangle({300, 100}, {360, 160}); const auto d = prepared.document();
    EditorSession session(d), replacement(d); session.select({a}); replacement.select({a}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    auto expected = serializeDocument(d); auto expectedSelection = session.selection(); int undo = 0, redo = 1; bool intervened = false;
    auto canvas = std::make_unique<EditorCanvas>(session, source); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected);
    if (mode == "disabled") canvas->setEnabled(false); if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    connect(canvas.get(), &EditorCanvas::changed, [&] {
      if (intervened || !canvas->interactionActive()) return; intervened = true; QVERIFY(!canvas->reorderSelection(EditorSession::Order::Back));
      if (mode == "begin-destroy") { canvas.reset(); return; }
      if (mode == "replacement") canvas->resetDocument(replacement, source);
      if (mode == "stale-selection" || mode == "reject-destroy") { session.select({b}); expectedSelection = session.selection(); }
      if (mode == "stale-state") { QVERIFY(session.moveSelection(2, 0)); expected = serializeDocument(session.document()); undo = 1; redo = 0; }
      if (mode == "defaults") { auto defaults = session.toolStyle(); defaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(defaults)); }
    });
    const bool changed = canvas->reorderSelection(EditorSession::Order::Front); QCOMPARE(changed, mode == "apply" || mode == "defaults"); QCOMPARE(intervened, mode != "disabled");
    if (changed) { auto ordered = d; ordered.annotations = {d.annotations[1], d.annotations[0]}; QCOMPARE(serializeDocument(session.document()), serializeDocument(ordered)); QVERIFY(session.undo()); }
    QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), expectedSelection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), mode == "stale-state");
    QCOMPARE(session.toolStyle().strokeWidth, mode == "defaults" ? 9.0 : 4.0); QCOMPARE(replacement.undoCount(), 0); QCOMPARE(serializeDocument(replacement.document()), serializeDocument(d));
    const bool stale = mode.startsWith("stale-") || mode == "replacement" || mode == "reject-destroy"; QCOMPARE(rejected.count(), stale ? 1 : 0); if (stale) QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale"));
    if (canvas) QVERIFY(!canvas->interactionActive());
  }
  void colorPickerGuardsDocumentSelectionDefaultsAndOwner_data() {
    QTest::addColumn<bool>("fill"); QTest::addColumn<QString>("mode");
    for (bool fill : {false, true}) for (const auto *mode : {"apply", "cancel", "no-op", "defaults-only", "stale-document", "stale-selection", "stale-defaults", "replacement", "destroy", "dialog-destroy", "reject-destroy"})
      QTest::newRow(qPrintable(QString("%1-%2").arg(fill ? "fill" : "stroke", mode))) << fill << QString(mode);
  }
  void thicknessCommandsFreezeStateDefaultsGenerationAndLifetime_data() {
    QTest::addColumn<bool>("increase"); QTest::addColumn<QString>("mode");
    for (bool increase : {false, true}) for (const auto *mode : {"apply", "defaults-only", "replacement", "stale-selection", "stale-defaults", "begin-destroy", "reject-destroy"})
      QTest::newRow(qPrintable(QString(increase ? "increase-" : "decrease-") + mode)) << increase << QString(mode);
  }
  void thicknessCommandsFreezeStateDefaultsGenerationAndLifetime() {
    QFETCH(bool, increase); QFETCH(QString, mode); const QImage source = image(); auto d = document(); RectangleAnnotation a;
    a.id = QUuid::createUuid(); a.start = {150, 180}; a.end = {230, 260}; a.extra = {{"vendor", "keep"}}; auto b = a; b.id = QUuid::createUuid(); b.start = {400, 240}; b.end = {480, 320}; d.annotations = {a, b};
    EditorSession session(d), replacement(d); session.select({a.id, b.id}); replacement.select({a.id, b.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); if (mode == "defaults-only") session.select({});
    const auto before = serializeDocument(session.document()); const auto defaults = session.toolStyle(); auto expectedSelection = session.selection(); auto expectedDefaults = defaults;
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->show(); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected); bool intervened = false;
    if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    connect(canvas.get(), &EditorCanvas::changed, [&] {
      if (intervened || !canvas->interactionActive()) return; intervened = true; QVERIFY(!canvas->changeStrokeWidth(!increase));
      if (mode == "begin-destroy") { canvas.reset(); return; }
      if (mode == "replacement") canvas->resetDocument(replacement, source);
      if (mode == "stale-selection" || mode == "reject-destroy") { session.select({b.id}); expectedSelection = session.selection(); }
      if (mode == "stale-defaults") { expectedDefaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(expectedDefaults)); }
    });
    const bool changed = canvas->changeStrokeWidth(increase); QCOMPARE(changed, mode == "apply"); QVERIFY(intervened); const double width = increase ? 5 : 3;
    if (changed) { auto expected = d; for (auto &r : expected.annotations) std::get<RectangleAnnotation>(r).style.strokeWidth = width; QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.undoCount(), 1); QVERIFY(session.undo()); }
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), expectedSelection); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
    if (mode == "apply" || mode == "defaults-only") expectedDefaults.strokeWidth = width;
    QCOMPARE(session.toolStyle().strokeWidth, expectedDefaults.strokeWidth); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor);
    const bool stale = mode == "replacement" || mode.startsWith("stale-") || mode == "reject-destroy"; QCOMPARE(rejected.count(), stale ? 1 : 0);
    if (stale) QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); QCOMPARE(replacement.undoCount(), 0); QCOMPARE(serializeDocument(replacement.document()), before);
    if (canvas) QVERIFY(!canvas->interactionActive()); QCOMPARE(source, image());
  }
  void colorPickerGuardsDocumentSelectionDefaultsAndOwner() {
    QFETCH(bool, fill); QFETCH(QString, mode); const QImage source = image(); EditorSession prepared(document());
    const auto a = *prepared.createRectangle({150, 180}, {230, 260}), b = *prepared.createRectangle({400, 240}, {480, 320});
    EditorSession session(prepared.document()), replacement(prepared.document()); session.select({a, b}); replacement.select({a, b});
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); if (mode == "defaults-only") session.select({});
    const auto before = serializeDocument(session.document()); const auto selection = session.selection(); const auto defaults = session.toolStyle();
    const auto target = fill ? EditorSession::ColorTarget::Fill : EditorSession::ColorTarget::Stroke;
    const Argb initial = fill ? defaults.fillColor : defaults.strokeColor, chosen = mode == "no-op" ? initial : 0x40667788;
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(900, 700); canvas->show(); QTest::qWait(10); canvas->resetZoom(); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected);
    if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    auto expected = before; auto expectedSelection = selection; auto expectedDefaults = defaults; int undo = 0, redo = 1; bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<QColorDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      dialog->activateWindow(); QTRY_VERIFY(dialog->isActiveWindow()); QCOMPARE(dialog->objectName(), QString("annotationColorDialog")); QCOMPARE(dialog->currentColor().rgba(), initial);
      QVERIFY(dialog->testOption(QColorDialog::ShowAlphaChannel)); QVERIFY(dialog->findChild<QLabel *>("annotationColorScope")->isVisible());
      QVERIFY(canvas->interactionActive()); QVERIFY(!canvas->editColor(target, canvas.get()));
      dialog->setCurrentColor(QColor::fromRgba(chosen)); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty());
      if (mode == "cancel") { QTest::keyClick(dialog, Qt::Key_Escape); return; }
      if (mode == "destroy") { canvas.reset(); return; }
      if (mode == "dialog-destroy") { delete dialog; return; }
      if (mode == "stale-document") { QVERIFY(session.moveSelection(2, 0)); undo = 1; redo = 0; }
      if (mode == "stale-selection" || mode == "reject-destroy") session.select({b});
      if (mode == "stale-defaults") { expectedDefaults.strokeWidth = 9; QVERIFY(session.setCreationStyle(expectedDefaults)); }
      if (mode == "replacement") canvas->resetDocument(replacement, source);
      expected = serializeDocument(session.document()); expectedSelection = session.selection();
      if (!qEnvironmentVariableIsEmpty("XERAHS_ANNOTATION_COLOR_QA") && fill && mode == "apply") QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_ANNOTATION_COLOR_QA")));
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons); auto *ok = buttons->button(QDialogButtonBox::Ok); QVERIFY(ok); ok->setFocus(); QTest::keyClick(ok, Qt::Key_Space); QTRY_VERIFY(!dialog->isVisible());
    });
    timer.start(5); const bool changed = canvas->editColor(target, canvas.get()); timer.stop(); QVERIFY(acted); QCOMPARE(changed, mode == "apply");
    if (mode == "apply") {
      auto expectedDoc = prepared.document(); for (auto &r : expectedDoc.annotations) { auto &style = std::get<RectangleAnnotation>(r).style; (fill ? style.fillColor : style.strokeColor) = chosen; }
      QCOMPARE(serializeDocument(session.document()), serializeDocument(expectedDoc)); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty());
      QCOMPARE(fill ? session.toolStyle().fillColor : session.toolStyle().strokeColor, chosen); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QVERIFY(session.redo());
    } else {
      QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), expectedSelection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo);
      const bool stale = mode.startsWith("stale-") || mode == "replacement" || mode == "reject-destroy";
      if (stale) { QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); } else QCOMPARE(rejected.count(), 0);
      if (mode == "defaults-only") (fill ? expectedDefaults.fillColor : expectedDefaults.strokeColor) = chosen;
      QCOMPARE(session.toolStyle().strokeColor, expectedDefaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, expectedDefaults.fillColor); QCOMPARE(session.toolStyle().strokeWidth, expectedDefaults.strokeWidth);
    }
    if (canvas) QVERIFY(!canvas->interactionActive()); QCOMPARE(replacement.undoCount(), 0); QCOMPARE(source, image());
  }
  void colorPendingObserverCanDestroyCanvasBeforeOpeningPicker() {
    const QImage source = image(); EditorSession session(document()); auto canvas = std::make_unique<EditorCanvas>(session, source); const auto defaults = session.toolStyle();
    connect(canvas.get(), &EditorCanvas::changed, [&] { canvas.reset(); }); QVERIFY(!canvas->editColor(EditorSession::ColorTarget::Stroke, canvas.get())); QVERIFY(!canvas);
    QCOMPARE(session.undoCount(), 0); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QVERIFY(!QApplication::activeModalWidget());
  }
  void nativeRotationDraftHasExplicitCommitAndStaleLifetimeGuards_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"apply", "cancel", "no-op", "invalid", "stale-document", "stale-selection", "replacement", "destroy", "dialog-destroy", "reject-destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void nativeRotationDraftHasExplicitCommitAndStaleLifetimeGuards() {
    QFETCH(QString, mode); const QImage source = image(); EditorSession prepared(document());
    const auto a = *prepared.createRectangle({150, 180}, {230, 260}), b = *prepared.createRectangle({400, 240}, {480, 320});
    EditorSession session(prepared.document()), replacement(prepared.document()); session.select({a, b}); replacement.select({a, b});
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); const auto before = serializeDocument(session.document()); const auto selection = session.selection();
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(900, 700); canvas->show(); QTest::qWait(10); canvas->resetZoom();
    QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected); const auto tool = canvas->tool(); const auto zoom = canvas->viewState().zoom();
    if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    auto expected = before; auto expectedSelection = selection; int expectedUndo = 0, expectedRedo = 1; bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<SelectionRotationDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<SelectionRotationDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      dialog->activateWindow(); QTRY_VERIFY(dialog->isActiveWindow());
      QVERIFY(canvas->interactionActive()); QVERIFY(!canvas->rotateSelection(canvas.get())); auto *preview = dialog->findChild<QPlainTextEdit *>("selectionRotationPreview"); QVERIFY(preview && preview->isReadOnly());
      QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty()); auto *angle = dialog->findChild<QLineEdit *>("selectionRotationDegrees"); QVERIFY(angle);
      QVERIFY(QAccessible::queryAccessibleInterface(angle)); QCOMPARE(QAccessible::queryAccessibleInterface(angle)->text(QAccessible::Name), QString("Clockwise degrees to add"));
      QVERIFY(QAccessible::queryAccessibleInterface(preview)->state().readOnly);
      canvas->setTool(EditorCanvas::Tool::Select); QCOMPARE(canvas->tool(), tool);
      QTest::keyClick(dialog, Qt::Key_A, Qt::AltModifier); QCOMPARE(QApplication::focusWidget(), angle); QTest::keyClick(angle, Qt::Key_A, Qt::ControlModifier);
      QTest::keyClicks(angle, mode == "no-op" ? "360" : "17.25"); QVERIFY(preview->toPlainText().contains("17.25") || mode == "no-op"); QVERIFY(!dialog->degrees());
      if (mode == "invalid") { angle->setText("nan"); dialog->accept(); QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QPushButton *>("applySelectionRotation")->isEnabled()); dialog->reject(); return; }
      if (mode == "cancel") { QTest::keyClick(dialog, Qt::Key_Escape); return; }
      if (mode == "destroy") { canvas.reset(); return; }
      if (mode == "dialog-destroy") { delete dialog; return; }
      if (mode == "stale-document") { QVERIFY(session.moveSelection(2, 0)); expectedUndo = 1; expectedRedo = 0; }
      if (mode == "stale-selection" || mode == "reject-destroy") session.select({b});
      if (mode == "replacement") canvas->resetDocument(replacement, source);
      expected = serializeDocument(session.document()); expectedSelection = session.selection();
      if (!qEnvironmentVariableIsEmpty("XERAHS_ANNOTATION_ROTATION_QA") && mode == "apply") QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_ANNOTATION_ROTATION_QA")));
      QTest::keyClick(dialog, Qt::Key_R, Qt::AltModifier); QTRY_VERIFY(!dialog->isVisible());
    });
    timer.start(5); const bool changed = canvas->rotateSelection(canvas.get()); timer.stop(); QVERIFY(acted); QCOMPARE(changed, mode == "apply");
    if (mode == "apply") {
      auto expectedDoc = prepared.document(); for (auto &r : expectedDoc.annotations) std::get<RectangleAnnotation>(r).style.rotationDegrees = 17.25;
      QCOMPARE(serializeDocument(session.document()), serializeDocument(expectedDoc)); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0); QVERIFY(session.dirty());
      QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty()); QVERIFY(session.redo());
    } else {
      QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), expectedSelection); QCOMPARE(session.undoCount(), expectedUndo); QCOMPARE(session.redoCount(), expectedRedo);
      const bool stale = mode.startsWith("stale-") || mode == "replacement" || mode == "reject-destroy";
      if (stale) { QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); }
      else QCOMPARE(rejected.count(), 0);
    }
    if (canvas) { QVERIFY(!canvas->interactionActive()); if (mode != "replacement") { QCOMPARE(canvas->tool(), tool); QCOMPARE(canvas->viewState().zoom(), zoom); } }
    QCOMPARE(replacement.undoCount(), 0); QCOMPARE(replacement.selection(), selection); QCOMPARE(source, image());
  }
  void rotationPendingObserverCanDestroyCanvasBeforeOpeningTheDialog() {
    const QImage source = image(); EditorSession session(document()); QVERIFY(session.createRectangle({10, 10}, {30, 30}));
    auto canvas = std::make_unique<EditorCanvas>(session, source); const auto before = serializeDocument(session.document());
    connect(canvas.get(), &EditorCanvas::changed, [&] { canvas.reset(); }); QVERIFY(!canvas->rotateSelection(canvas.get())); QVERIFY(!canvas);
    QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!QApplication::activeModalWidget());
  }
  void groupMovePreviewAndCommitUseFrozenSourcePixels_data() {
    QTest::addColumn<double>("angle"); QTest::addColumn<int>("zoomSteps");
    for (double angle : {0.0, 90.0, 37.0}) for (int steps : {-2, 0, 1})
      QTest::newRow(qPrintable(QString("rotation-%1-zoom-%2").arg(angle).arg(steps))) << angle << steps;
  }
  void groupMovePreviewAndCommitUseFrozenSourcePixels() {
    QFETCH(double, angle); QFETCH(int, zoomSteps); const QImage source = image(); EditorSession session(document());
    QVERIFY(session.setCreationStyle({0xFF112233, 0xFF00FF00, 2, angle, 1})); const auto a = *session.createRectangle({150, 180}, {230, 260});
    const auto b = *session.createRectangle({400, 240}, {480, 320}); session.select({a, b}); session.markSaved();
    const auto before = serializeDocument(session.document()); const auto original = session.document(); const auto selection = session.selection();
    EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    for (int i = 0; i < std::abs(zoomSteps); ++i) { if (zoomSteps > 0) canvas.zoomIn(); else canvas.zoomOut(); }
    const auto zoom = canvas.viewState().zoom(); const auto press = canvas.viewState().toView({190, 220}).toPoint(); const QPoint screenDelta(120, 50);
    const PointF delta{120 / zoom, 50 / zoom}; QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, press); QVERIFY(canvas.interactionActive());
    QTest::mouseMove(canvas.viewport(), press + screenDelta); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 2); QVERIFY(!session.dirty());
    const auto preview = canvas.viewport()->grab().toImage();
    QCOMPARE(preview.pixelColor(canvas.viewState().toView({190 + delta.x, 220 + delta.y}).toPoint()), QColor(Qt::green));
    QCOMPARE(preview.pixelColor(canvas.viewState().toView({440 + delta.x, 280 + delta.y}).toPoint()), QColor(Qt::green));
    QCOMPARE(preview.pixelColor(canvas.viewState().toView({190, 220}).toPoint()), QColor(Qt::white));
    if (angle == 37 && zoomSteps == 0 && !qEnvironmentVariableIsEmpty("XERAHS_ANNOTATION_MOVE_QA")) QVERIFY(canvas.grab().save(qEnvironmentVariable("XERAHS_ANNOTATION_MOVE_QA")));
    QTest::keyClick(&canvas, Qt::Key_Delete); QCOMPARE(serializeDocument(session.document()), before);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, press + screenDelta); QVERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 3); QVERIFY(session.dirty());
    auto expected = original;
    for (size_t i = 0; i < expected.annotations.size(); ++i) {
      auto &r = std::get<RectangleAnnotation>(expected.annotations[i]); const auto &actual = std::get<RectangleAnnotation>(session.document().annotations[i]);
      QVERIFY(std::abs(actual.start.x - (r.start.x + delta.x)) < 1e-10); QVERIFY(std::abs(actual.start.y - (r.start.y + delta.y)) < 1e-10);
      QVERIFY(std::abs(actual.end.x - (r.end.x + delta.x)) < 1e-10); QVERIFY(std::abs(actual.end.y - (r.end.y + delta.y)) < 1e-10);
      // The analytical displacement tolerates conversion rounding; all other
      // fields and the saved history state remain exact comparisons.
      r.start = actual.start; r.end = actual.end;
    }
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.selection(), selection); QCOMPARE(source, image());
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected));
  }
  void groupMoveCancellationAndStaleReleasePreserveCommittedState_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"escape", "focus", "tool", "zoom", "wheel", "source", "replace", "stale-selection", "stale-document", "no-motion", "return-start"}) QTest::newRow(mode) << QString(mode);
  }
  void groupMoveCancellationAndStaleReleasePreserveCommittedState() {
    QFETCH(QString, mode); const QImage source = image(); EditorSession session(document()), replacement(document());
    QVERIFY(session.setCreationStyle({0xFF112233, 0xFF00FF00, 2, 37, 1})); const auto a = *session.createRectangle({150, 180}, {230, 260});
    const auto b = *session.createRectangle({400, 240}, {480, 320}); session.select({a, b}); session.markSaved();
    QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); const auto selection = session.selection();
    EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QTest::qWait(10); canvas.resetZoom(); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    const auto press = canvas.viewState().toView({190, 220}).toPoint(); auto release = press + QPoint(120, 50);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, press); QVERIFY(canvas.interactionActive());
    if (mode != "no-motion") QTest::mouseMove(canvas.viewport(), release);
    if (mode == "escape") QTest::keyClick(&canvas, Qt::Key_Escape);
    else if (mode == "focus") { QFocusEvent event(QEvent::FocusOut); QApplication::sendEvent(&canvas, &event); }
    else if (mode == "tool") canvas.setTool(EditorCanvas::Tool::Select);
    else if (mode == "zoom") canvas.zoomIn();
    else if (mode == "wheel") wheel(canvas, release, Qt::NoModifier);
    else if (mode == "source") canvas.sourceChanged();
    else if (mode == "replace") canvas.resetDocument(replacement, source);
    else if (mode == "stale-selection") session.select({b});
    else if (mode == "stale-document") QVERIFY(session.moveSelection(2, 0));
    else { release = press; if (mode == "return-start") QTest::mouseMove(canvas.viewport(), press); }
    const auto expected = serializeDocument(session.document()); const auto expectedSelection = session.selection(); const auto history = session.undoCount(); const auto redo = session.redoCount(); const auto dirty = session.dirty();
    if (mode.startsWith("stale-")) {
      QTest::mouseMove(canvas.viewport(), press + QPoint(120, 50)); const auto preview = canvas.viewport()->grab().toImage();
      QCOMPARE(preview.pixelColor(canvas.viewState().toView({190, 220}).toPoint()), QColor(Qt::green));
      QCOMPARE(preview.pixelColor(canvas.viewState().toView({330, 270}).toPoint()), QColor(Qt::white));
    }
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, release); QVERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), expectedSelection); QCOMPARE(session.undoCount(), history); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), dirty);
    if (mode.startsWith("stale-")) { QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); }
    else { QCOMPARE(rejected.count(), 0); QCOMPARE(session.selection(), selection); QCOMPARE(session.redoCount(), 1); QVERIFY(!session.dirty()); }
    QCOMPARE(replacement.undoCount(), 0); QVERIFY(!replacement.dirty()); QCOMPARE(source, image());
  }
  void staleMoveRejectionCanDestroyCanvasWithoutAnotherNotification() {
    const QImage source = image(); EditorSession session(document()); const auto a = *session.createRectangle({150, 180}, {230, 260});
    const auto b = *session.createRectangle({400, 240}, {480, 320}); session.select({a, b});
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(900, 700); canvas->show(); QTest::qWait(10); canvas->resetZoom();
    const auto press = canvas->viewState().toView({190, 220}).toPoint(); QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, press); session.select({b});
    const auto expected = serializeDocument(session.document()); int changed = 0;
    connect(canvas.get(), &EditorCanvas::changed, [&] { ++changed; }); connect(canvas.get(), &EditorCanvas::editRejected, [&](const QString &diagnostic) { QCOMPARE(diagnostic, QString("annotation-edit-stale")); canvas.reset(); });
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, press + QPoint(120, 50)); QVERIFY(!canvas); QCOMPARE(changed, 0);
    QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), QSet<QUuid>{b}); QCOMPARE(session.undoCount(), 2);
  }
  void pointerCreationFreezesStyleAndRejectsInterveningDocumentChanges() {
    const QImage source = image(); EditorSession session(document()); RectangleStyle style; style.strokeColor = 0x80112233; style.strokeWidth = 7;
    QVERIFY(session.setCreationStyle(style)); EditorCanvas canvas(session, source); canvas.resize(520, 440); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto a = canvas.viewState().toView({150, 150}).toPoint(), b = canvas.viewState().toView({100, 100}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a); style.strokeWidth = 9; QVERIFY(session.setCreationStyle(style));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.selectedRectangle()->style.strokeWidth, 7.0);
    QCOMPARE(session.toolStyle().strokeWidth, 9.0); const auto id = session.selectedRectangle()->id;
    const auto c = canvas.viewState().toView({300, 300}).toPoint(); QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, c);
    QVERIFY(session.createRectangle({400, 400}, {450, 450})); const auto intervening = serializeDocument(session.document()); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, c + QPoint(20, 20)); QCOMPARE(serializeDocument(session.document()), intervening); QCOMPARE(session.undoCount(), 2);
    QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); QVERIFY(!canvas.interactionActive());
    QVERIFY(session.undo()); session.select({id}); QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, c); QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, c + QPoint(20, 20)); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 1);
  }
  void pendingCreationObserverCanDestroyTheCanvasBeforeTheDialog() {
    EditorSession session(document()); auto canvas = std::make_unique<EditorCanvas>(session, image());
    connect(canvas.get(), &EditorCanvas::changed, [&] { canvas.reset(); });
    QVERIFY(!canvas->createRectangle(canvas.get())); QVERIFY(!canvas); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    QVERIFY(!QApplication::activeModalWidget());
  }
  void numericRectangleDraftsRejectMatchingStateReplacement_data() {
    QTest::addColumn<bool>("creation"); QTest::addColumn<QString>("mode");
    for (bool creation : {false, true}) for (const auto *mode : {"replace", "early-replace", "cancel", "reject-destroy"})
      QTest::newRow(qPrintable(QString(creation ? "create-" : "properties-") + mode)) << creation << QString(mode);
  }
  void numericRectangleDraftsRejectMatchingStateReplacement() {
    QFETCH(bool, creation); QFETCH(QString, mode); const QImage source = image(); auto d = document(); RectangleAnnotation r;
    r.id = QUuid::createUuid(); r.start = {150, 180}; r.end = {230, 260}; r.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; r.extra = {{"vendor", "keep"}};
    d.annotations = {r}; d.embeddedImages = {{"asset", "retained"}}; EditorSession session(d); session.select({r.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    auto next = d; next.extra = {{"replacement", true}}; EditorSession replacement(next); replacement.select({r.id}); QVERIFY(replacement.moveSelection(1, 0)); QVERIFY(replacement.undo());
    QCOMPARE(session.stateId(), replacement.stateId()); QCOMPARE(session.selection(), replacement.selection()); const auto before = serializeDocument(session.document()), replaced = serializeDocument(replacement.document());
    const auto defaults = session.toolStyle(); auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->show(); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected);
    const auto generation = canvas->documentGeneration(); bool reset = false;
    const auto replace = [&] { reset = true; canvas->resetDocument(replacement, source); };
    if (mode == "early-replace") connect(canvas.get(), &EditorCanvas::changed, [&] { if (!reset && canvas->interactionActive()) replace(); });
    if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      auto *editor = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!editor || acted) return; acted = true;
      const QPointer<RectanglePropertiesDialog> owner(editor); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow()); QVERIFY(canvas->interactionActive());
      QCOMPARE(editor->objectName(), QString(creation ? "rectangleCreationDialog" : "rectanglePropertiesDialog"));
      editor->findChild<QLineEdit *>("rectangleWidth")->setText("45.5");
      if (!reset) replace(); QCOMPARE(canvas->annotationState(), session.stateId()); QCOMPARE(canvas->annotationSelection(), session.selection()); QVERIFY(canvas->documentGeneration() != generation);
      QCOMPARE(serializeDocument(canvas->annotationDocument()), replaced); QVERIFY(!canvas->createRectangle(canvas.get()));
      if (mode == "cancel") QTest::keyClick(editor, Qt::Key_Escape);
      else { auto *ok = editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); ok->setFocus(); QTest::keyClick(ok, Qt::Key_Space); }
      QTRY_VERIFY(!editor->isVisible());
    });
    timer.start(5); const bool changed = creation ? canvas->createRectangle(canvas.get()) : canvas->editRectangleProperties(canvas.get()); timer.stop(); QVERIFY(acted); QVERIFY(!changed);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(serializeDocument(replacement.document()), replaced);
    for (const auto *model : {&session, &replacement}) { QCOMPARE(model->undoCount(), 0); QCOMPARE(model->redoCount(), 1); QVERIFY(!model->dirty()); QCOMPARE(model->selection(), QSet<QUuid>{r.id}); QCOMPARE(model->toolStyle().strokeColor, defaults.strokeColor); }
    QCOMPARE(rejected.count(), mode == "cancel" ? 0 : 1); if (rejected.count()) QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale"));
    if (canvas) { QVERIFY(!canvas->interactionActive()); QCOMPARE(serializeDocument(canvas->annotationDocument()), replaced); }
    else QCOMPARE(mode, QString("reject-destroy"));
    QCOMPARE(source, image()); QVERIFY(replacement.redo()); QVERIFY(session.redo());
  }
  void nestedRectangleColorsOwnDraftAndLifetime_data() {
    QTest::addColumn<bool>("fill"); QTest::addColumn<QString>("mode");
    for (bool fill : {false, true}) for (const auto *mode : {"apply", "draft-cancel", "picker-cancel", "picker-destroy", "properties-destroy", "canvas-destroy"})
      QTest::newRow(qPrintable(QString(fill ? "fill-" : "stroke-") + mode)) << fill << QString(mode);
  }
  void nestedRectangleColorsOwnDraftAndLifetime() {
    QFETCH(bool, fill); QFETCH(QString, mode); const QImage source = image(); auto d = document(); RectangleAnnotation r;
    r.id = QUuid::createUuid(); r.start = {150, 180}; r.end = {230, 260}; r.style = {0x80112233, 0x40223344, 2.5, 37.25, .25}; r.extra = {{"vendor", "keep"}};
    d.annotations = {r}; d.embeddedImages = {{"asset", "retained"}}; EditorSession session(d); session.select({r.id}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo());
    const auto before = serializeDocument(session.document()); const auto defaults = session.toolStyle(); auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->show();
    constexpr Argb chosen = 0x80010203; bool acted = false, picked = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      const QPointer<RectanglePropertiesDialog> editor(qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget())); if (!editor || acted) return; acted = true;
      const auto close = qScopeGuard([editor] { if (editor && editor->isVisible()) editor->reject(); }); editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow());
      const auto fieldName = fill ? "rectangleFillColor" : "rectangleStrokeColor"; const QPointer<QLineEdit> field(editor->findChild<QLineEdit *>(fieldName));
      const auto initial = fill ? r.style.fillColor : r.style.strokeColor; QCOMPARE(field->text(), formatColor(initial)); QTimer pickerTimer;
      connect(&pickerTimer, &QTimer::timeout, [&] {
        const QPointer<QColorDialog> picker(qobject_cast<QColorDialog *>(QApplication::activeModalWidget())); if (!picker || picked) return; picked = true;
        const auto closePicker = qScopeGuard([picker] { if (picker && picker->isVisible()) picker->reject(); }); picker->activateWindow(); QTRY_VERIFY(picker->isActiveWindow());
        QCOMPARE(picker->objectName(), QString("annotationColorDialog")); QCOMPARE(picker->currentColor().rgba(), initial); QVERIFY(picker->testOption(QColorDialog::ShowAlphaChannel));
        QVERIFY(picker->findChild<QLabel *>("annotationColorScope")->text().contains("properties draft")); picker->setCurrentColor(QColor::fromRgba(chosen));
        if (fill && mode == "apply" && qEnvironmentVariableIsSet("XERAHS_RECTANGLE_NESTED_COLOR_QA")) QVERIFY(picker->grab().save(qEnvironmentVariable("XERAHS_RECTANGLE_NESTED_COLOR_QA")));
        QCOMPARE(field->text(), formatColor(initial)); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1);
        if (mode == "canvas-destroy") { canvas.reset(); return; } if (mode == "properties-destroy") { delete editor.data(); return; } if (mode == "picker-destroy") { delete picker.data(); return; }
        if (mode == "picker-cancel") QTest::keyClick(picker, Qt::Key_Escape);
        else { auto *ok = picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); ok->setFocus(); QTest::keyClick(ok, Qt::Key_Space); }
        QTRY_VERIFY(!picker->isVisible());
      });
      auto *button = editor->findChild<QPushButton *>(QString(fieldName) + "Picker"); QVERIFY(button); button->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), button);
      pickerTimer.start(5); QTest::keyClick(button, Qt::Key_Space); pickerTimer.stop(); QVERIFY(picked); if (!editor) return;
      const bool chosenDraft = mode == "apply" || mode == "draft-cancel"; QCOMPARE(field->text(), formatColor(chosenDraft ? chosen : initial));
      QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor);
      editor->activateWindow(); QTRY_VERIFY(editor->isActiveWindow());
      if (mode == "draft-cancel") QTest::keyClick(editor, Qt::Key_Escape);
      else { auto *ok = editor->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok); ok->setFocus(); QTest::keyClick(ok, Qt::Key_Space); }
      QTRY_VERIFY(!editor->isVisible());
    });
    timer.start(5); const bool changed = canvas->editRectangleProperties(canvas.get()); timer.stop(); QVERIFY(acted && picked); QCOMPARE(changed, mode == "apply");
    if (changed) { auto expected = d; auto &style = std::get<RectangleAnnotation>(expected.annotations[0]).style; (fill ? style.fillColor : style.strokeColor) = chosen; QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.undoCount(), 1); QVERIFY(session.undo()); }
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1); QCOMPARE(session.selection(), QSet<QUuid>{r.id}); QVERIFY(!session.dirty());
    QCOMPARE(session.toolStyle().strokeColor, defaults.strokeColor); QCOMPARE(session.toolStyle().fillColor, defaults.fillColor); QCOMPARE(source, image());
    if (canvas) QVERIFY(!canvas->interactionActive()); else QCOMPARE(mode, QString("canvas-destroy"));
  }
  void keyboardCreationHasOwnedDraftCancellationAndGuardedCommit_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"apply", "unchanged", "cancel", "invalid", "stale-document", "stale-selection", "destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void keyboardCreationHasOwnedDraftCancellationAndGuardedCommit() {
    QFETCH(QString, mode); EditorSession session(document()); const auto originalId = *session.createRectangle({300, 300}, {350, 350}); session.markSaved();
    RectangleStyle frozen{0x80112233, 0x40223344, 2.5, 37.25, .25}; QVERIFY(session.setCreationStyle(frozen));
    auto canvas = std::make_unique<EditorCanvas>(session, image()); canvas->show(); canvas->setTool(EditorCanvas::Tool::Select);
    const auto before = serializeDocument(session.document()); auto expected = before; bool acted = false; QTimer timer; QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected);
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<RectanglePropertiesDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      QCOMPARE(dialog->objectName(), QString("rectangleCreationDialog")); QVERIFY(canvas->interactionActive()); QVERIFY(!canvas->createRectangle(canvas.get()));
      QVERIFY(!canvas->editRectangleProperties(canvas.get())); auto *label = dialog->findChild<QLabel *>("rectangleCreationStyle"); QVERIFY(label); QVERIFY(label->text().contains("#80112233"));
      auto *accessible = QAccessible::queryAccessibleInterface(label); QVERIFY(accessible); QCOMPARE(accessible->text(QAccessible::Description), label->text());
      QVERIFY(!dialog->findChild<QLineEdit *>("rectangleStrokeWidth")); QCOMPARE(serializeDocument(session.document()), before);
      if (mode == "destroy") { canvas.reset(); return; }
      const QStringList fields{"rectangleLeft", "rectangleTop", "rectangleWidth", "rectangleHeight"}; const QStringList values{"-5.25", "6.5", "20.5", "30.25"};
      dialog->activateWindow(); dialog->findChild<QLineEdit *>(fields[0])->setFocus();
      for (int i = 0; i < fields.size(); ++i) {
        auto *field = dialog->findChild<QLineEdit *>(fields[i]); QTRY_COMPARE(QApplication::focusWidget(), field);
        if (mode != "unchanged") { QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(field, values[i]); }
        if (i != fields.size() - 1) QTest::keyClick(field, Qt::Key_Tab);
      }
      if (mode == "invalid") {
        dialog->findChild<QLineEdit *>("rectangleWidth")->setText("0"); QTest::keyClick(dialog, Qt::Key_R, Qt::AltModifier);
        QTRY_VERIFY(!dialog->findChild<QLabel *>("rectanglePropertiesStatus")->text().isEmpty());
        QVERIFY(dialog->isVisible()); QCOMPARE(serializeDocument(session.document()), before);
      }
      if (mode == "cancel" || mode == "invalid") { QTest::keyClick(dialog, Qt::Key_Escape); return; }
      if (mode == "stale-document") { QVERIFY(session.moveSelection(1, 0)); expected = serializeDocument(session.document()); }
      if (mode == "stale-selection") session.select({});
      RectangleStyle later = frozen; later.strokeWidth = 9; QVERIFY(session.setCreationStyle(later));
      if (qEnvironmentVariableIsSet("XERAHS_RECTANGLE_CREATION_QA") && mode == "apply") QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_RECTANGLE_CREATION_QA")));
      QTest::keyClick(dialog, Qt::Key_R, Qt::AltModifier);
      QTRY_VERIFY(!dialog->isVisible());
    });
    timer.start(5); const bool changed = canvas->createRectangle(canvas.get()); timer.stop(); QVERIFY(acted);
    const bool accepted = mode == "apply" || mode == "unchanged"; QCOMPARE(changed, accepted);
    if (accepted) {
      QCOMPARE(session.undoCount(), 2); const auto *r = session.selectedRectangle(); QVERIFY(r); QVERIFY(r->id != originalId);
      QCOMPARE(r->start, (mode == "apply" ? PointF{-5.25, 6.5} : PointF{0, 0})); QCOMPARE(r->end, (mode == "apply" ? PointF{15.25, 36.75} : PointF{100, 100}));
      QCOMPARE(r->style.strokeWidth, frozen.strokeWidth); QCOMPARE(r->style.strokeColor, frozen.strokeColor); QCOMPARE(r->style.fillColor, frozen.fillColor);
      QCOMPARE(r->style.rotationDegrees, frozen.rotationDegrees); QCOMPARE(r->style.opacity, frozen.opacity); QCOMPARE(session.toolStyle().strokeWidth, 9.0);
      QCOMPARE(canvas->tool(), EditorCanvas::Tool::Select); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), QSet<QUuid>{originalId}); QVERIFY(!session.dirty());
      QVERIFY(session.redo()); QCOMPARE(session.document().annotations.size(), size_t(2));
    } else {
      QCOMPARE(serializeDocument(session.document()), expected);
      if (mode.startsWith("stale-")) { QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale")); }
      else { QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty()); }
    }
    if (canvas) QVERIFY(!canvas->interactionActive());
  }
  void initialLayoutFitsTheActualViewportWithoutMutatingTheSession() {
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(520, 440); canvas.show(); QTest::qWait(10);
    const double fit = std::min(double(canvas.viewport()->width()) / 800, double(canvas.viewport()->height()) / 600);
    QCOMPARE(canvas.viewState().zoom(), fit); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    canvas.zoomIn(); const double chosen = canvas.viewState().zoom(); canvas.resize(620, 480); QCoreApplication::processEvents();
    QCOMPARE(canvas.viewState().zoom(), chosen); // later layout changes preserve the user's zoom
  }
  void rectangleDialogValidationPrecisionCancellationAndLifetime_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"cancel", "unchanged", "apply", "invalid", "picker", "destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void rectangleDialogValidationPrecisionCancellationAndLifetime() {
    QFETCH(QString, mode); const QImage source = image(); auto d = document(); RectangleAnnotation r;
    r.id = QUuid::createUuid(); r.start = {-10.25, 1.2345678901234567}; r.end = {37.987654321123456, 42.1111111111111};
    r.style = {0x80112233, 0x40223344, 4.123456789, 37.123456789, .4123456789}; r.extra = {{"future", "keep"}}; d.annotations = {r};
    EditorSession session(d); session.select({r.id}); auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->show();
    const auto before = serializeDocument(session.document()); bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<RectanglePropertiesDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      QVERIFY(canvas->interactionActive()); QVERIFY(!canvas->editRectangleProperties(canvas.get()));
      auto *left = dialog->findChild<QLineEdit *>("rectangleLeft"); QVERIFY(left);
      auto *accessible = QAccessible::queryAccessibleInterface(left); QVERIFY(accessible); QCOMPARE(accessible->text(QAccessible::Name), QString("Left"));
      QCOMPARE(serializeDocument(session.document()), before);
      if (mode == "destroy") { canvas.reset(); return; }
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      QCOMPARE(dialog->findChild<QLineEdit *>("rectangleRotation")->text().toDouble(), r.style.rotationDegrees);
      if (mode == "cancel") { dialog->findChild<QLineEdit *>("rectangleWidth")->setText("50"); buttons->button(QDialogButtonBox::Cancel)->click(); return; }
      if (mode == "invalid") {
        for (const auto &entry : {std::pair{"rectangleWidth", "0"}, std::pair{"rectangleOpacity", "1.1"},
            std::pair{"rectangleRotation", "NaN"}, std::pair{"rectangleStrokeColor", "bad"}}) {
          auto *field = dialog->findChild<QLineEdit *>(entry.first); const auto original = field->text(); field->setText(entry.second); dialog->accept();
          QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QLabel *>("rectanglePropertiesStatus")->text().isEmpty());
          QCOMPARE(serializeDocument(session.document()), before); field->setText(original);
        }
        dialog->reject(); return;
      }
      if (mode == "apply") {
        left->setText("-5.25"); dialog->findChild<QLineEdit *>("rectangleWidth")->setText("50");
        dialog->findChild<QLineEdit *>("rectangleStrokeColor")->setText("#80AABBCC");
        dialog->findChild<QLineEdit *>("rectangleFillColor")->setText("#40DDEEFF");
        dialog->findChild<QLineEdit *>("rectangleOpacity")->setText("0.25");
      }
      if (mode == "picker") {
        bool picked = false; QTimer colorTimer;
        connect(&colorTimer, &QTimer::timeout, [&] {
          auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); if (!picker || picked) return; picked = true;
          const auto closePicker = qScopeGuard([picker] { if (picker->isVisible()) picker->reject(); });
          QCOMPARE(picker->currentColor().rgba(), r.style.strokeColor); QVERIFY(picker->testOption(QColorDialog::ShowAlphaChannel));
          picker->setCurrentColor(QColor::fromRgba(0x80010203)); picker->accept();
        });
        colorTimer.start(5); dialog->findChild<QPushButton *>("rectangleStrokeColorPicker")->click(); colorTimer.stop(); QVERIFY(picked);
      }
      buttons->button(QDialogButtonBox::Ok)->click();
    });
    timer.start(5); const bool changed = canvas->editRectangleProperties(canvas.get()); timer.stop(); QVERIFY(acted);
    QCOMPARE(changed, mode == "apply" || mode == "picker");
    if (mode == "picker") { QCOMPARE(session.undoCount(), 1); QCOMPARE(session.selectedRectangle()->style.strokeColor, Argb(0x80010203)); QCOMPARE(session.selectedRectangle()->extra, r.extra); }
    else if (mode != "apply") { QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty()); }
    else {
      QCOMPARE(session.undoCount(), 1); const auto *edited = session.selectedRectangle(); QVERIFY(edited);
      QCOMPARE(edited->left(), -5.25); QCOMPARE(edited->right(), 44.75); QCOMPARE(edited->top(), r.top()); QCOMPARE(edited->bottom(), r.bottom());
      QCOMPARE(edited->style.strokeColor, Argb(0x80AABBCC)); QCOMPARE(edited->style.fillColor, Argb(0x40DDEEFF)); QCOMPARE(edited->style.opacity, .25);
      QCOMPARE(edited->extra, r.extra); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    }
    if (canvas) QVERIFY(!canvas->interactionActive()); else QCOMPARE(mode, QString("destroy"));
  }
  void resizeHandlesPreviewAndCommitOnceAtEveryRotationAndZoom_data() {
    QTest::addColumn<int>("handle"); QTest::addColumn<int>("horizontal"); QTest::addColumn<int>("vertical");
    QTest::addColumn<double>("angle"); QTest::addColumn<bool>("zoomed");
    const int directions[8][2] = {{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}};
    for (int h = 0; h < 8; ++h) for (double angle : {0.0, 90.0, 37.0}) for (bool zoom : {false, true})
      QTest::newRow(qPrintable(QString("%1-%2-%3").arg(h).arg(angle).arg(zoom))) << h << directions[h][0] << directions[h][1] << angle << zoom;
  }
  void resizeHandlesPreviewAndCommitOnceAtEveryRotationAndZoom() {
    QFETCH(int, handle); QFETCH(int, horizontal); QFETCH(int, vertical); QFETCH(double, angle); QFETCH(bool, zoomed);
    const QImage source = image(); auto d = document(); RectangleAnnotation r;
    r.id = QUuid::createUuid(); r.start = {350, 250}; r.end = {450, 350}; r.style.rotationDegrees = angle; r.extra = {{"future", "kept"}};
    d.annotations = {r}; EditorSession session(d); session.select({r.id}); EditorCanvas canvas(session, source);
    canvas.resize(520, 440); canvas.show(); QTest::qWait(10); canvas.zoomToFit(); if (zoomed) { canvas.resetZoom(); canvas.zoomIn(); }
    const double zoom = canvas.viewState().zoom(), gap = 2 + 3 / zoom;
    QTransform rotation; rotation.rotate(angle);
    const QPointF localHandle(horizontal * (50 + gap), vertical * (50 + gap));
    const QPoint press = canvas.viewState().toView(QPointF(400, 300) + rotation.map(localHandle)).toPoint();
    const QPoint delta = (rotation.map(QPointF(horizontal * 20, vertical * 10)) * zoom).toPoint();
    const QPointF effectiveLocal = rotation.inverted().map(QPointF(delta) / zoom);
    const QPointF opposite = QPointF(400, 300) + rotation.map(QPointF(-horizontal * 50, -vertical * 50));
    const auto before = serializeDocument(session.document()); const QImage previewBefore = canvas.viewport()->grab().toImage();
    QTest::mouseMove(canvas.viewport(), press); QVERIFY(canvas.viewport()->cursor().shape() != Qt::ArrowCursor);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, press); QVERIFY(canvas.interactionActive());
    QTest::mouseMove(canvas.viewport(), press + delta);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    QVERIFY(canvas.viewport()->grab().toImage() != previewBefore);
    if (qEnvironmentVariableIsSet("XERAHS_RECTANGLE_HANDLES_QA") && horizontal == 1 && vertical == 1 && angle == 37 && zoomed)
      QVERIFY(canvas.viewport()->grab().save(qEnvironmentVariable("XERAHS_RECTANGLE_HANDLES_QA")));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, press + delta); QVERIFY(!canvas.interactionActive());
    QCOMPARE(session.undoCount(), 1); const auto *resized = session.selectedRectangle(); QVERIFY(resized);
    const double width = resized->right() - resized->left(), height = resized->bottom() - resized->top();
    QVERIFY(std::abs(width - (100 + horizontal * effectiveLocal.x())) < 1e-8);
    QVERIFY(std::abs(height - (100 + vertical * effectiveLocal.y())) < 1e-8);
    const QPointF centre((resized->left() + resized->right()) / 2, (resized->top() + resized->bottom()) / 2);
    QVERIFY(QLineF(centre + rotation.map(QPointF(-horizontal * width / 2, -vertical * height / 2)), opposite).length() < 1e-8);
    QCOMPARE(resized->extra, r.extra); QCOMPARE(resized->id, r.id); QCOMPARE(resized->style.rotationDegrees, angle);
    QCOMPARE(source, image()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(session.selectedRectangle()->id, r.id); Q_UNUSED(handle);
  }
  void cancelledCollapsedStaleAndProtectedResizeKeepCommittedState() {
    const QImage source = image(); EditorSession session(document()); const auto id = *session.createRectangle({350, 250}, {450, 350}); session.markSaved();
    EditorCanvas canvas(session, source); canvas.resize(520, 440); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    const auto handle = [&] { const auto *r = session.selectedRectangle(); const double gap = 2 + 3 / canvas.viewState().zoom();
      return canvas.viewState().toView({r->right() + gap, r->bottom() + gap}).toPoint(); };
    const auto before = serializeDocument(session.document()); const QPoint at = handle();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, at); QTest::mouseMove(canvas.viewport(), at + QPoint(20, 10));
    QTest::keyClick(&canvas, Qt::Key_Delete); QTest::keyClick(&canvas, Qt::Key_Right); QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at + QPoint(20, 10)); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty());
    QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, at); QTest::mouseMove(canvas.viewport(), at - QPoint(120, 0));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at - QPoint(120, 0));
    QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-geometry-invalid")); QCOMPARE(serializeDocument(session.document()), before);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, at); QVERIFY(session.moveSelection(1, 0));
    const auto intervening = serializeDocument(session.document()); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at + QPoint(10, 0));
    QCOMPARE(rejected.count(), 2); QCOMPARE(rejected[1][0].toString(), QString("annotation-edit-stale")); QCOMPARE(serializeDocument(session.document()), intervening); QCOMPARE(session.undoCount(), 2);
    QVERIFY(session.undo()); session.select({id});
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, handle()); canvas.setTool(EditorCanvas::Tool::Select);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at + QPoint(20, 10)); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.redoCount(), 1);
    auto protectedDoc = session.document(); std::get<RectangleAnnotation>(protectedDoc.annotations[0]).extra = {{"locked", true}};
    EditorSession protectedSession(protectedDoc); protectedSession.select({id}); canvas.resetDocument(protectedSession, source); canvas.setTool(EditorCanvas::Tool::Select);
    const auto locked = serializeDocument(protectedSession.document());
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({457, 357}).toPoint());
    QVERIFY(!canvas.interactionActive()); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({480, 370}).toPoint());
    QCOMPARE(serializeDocument(protectedSession.document()), locked); QVERIFY(!canvas.canEditRectangle());
  }
  void explicitToolsAreCheckedAccessibleAndSelectNeverCreates() {
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto tools = annotationToolActions(canvas, &canvas); QToolBar bar;
    bar.addAction(tools["select-tool"]); bar.addAction(tools["rectangle-tool"]); bar.addAction(tools["crop-tool"]); bar.show();
    std::unique_ptr<QLabel> instructions(annotationToolInstructions(canvas, nullptr));
    QVERIFY(tools["rectangle-tool"]->isChecked()); QVERIFY(instructions->text().contains("stays active"));
    auto *button = qobject_cast<QToolButton *>(bar.widgetForAction(tools["select-tool"])); QVERIFY(button);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Select"));
    QTest::mouseClick(button, Qt::LeftButton); QVERIFY(accessible->state().checked);
    QCOMPARE(canvas.tool(), EditorCanvas::Tool::Select); QVERIFY(!tools["rectangle-tool"]->isChecked());
    QVERIFY(instructions->text().contains("Blank canvas clears selection")); QVERIFY(canvas.accessibleDescription().contains("Select:"));
    const QPoint a = canvas.viewState().toView({160, 140}).toPoint(), b = canvas.viewState().toView({240, 220}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a); QVERIFY(!canvas.interactionActive());
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b); QVERIFY(session.document().annotations.empty()); QVERIFY(!session.dirty());
    tools["rectangle-tool"]->trigger();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, b); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, a);
    QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), size_t(1)); QVERIFY(tools["rectangle-tool"]->isChecked());
    const auto id = annotationId(session.document().annotations[0]);
    tools["select-tool"]->trigger();
    const QPoint inside = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, inside); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, inside + QPoint(10, 0));
    QCOMPARE(session.undoCount(), 2); QCOMPARE(session.selection(), QSet<QUuid>{id});
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({400, 400}).toPoint());
    QVERIFY(session.selection().isEmpty()); QCOMPARE(session.document().annotations.size(), size_t(1));
    tools["crop-tool"]->setChecked(true); QVERIFY(tools["crop-tool"]->isChecked()); QVERIFY(!tools["select-tool"]->isChecked());
    tools["crop-tool"]->setChecked(false); QCOMPARE(canvas.tool(), EditorCanvas::Tool::Rectangle); QVERIFY(tools["rectangle-tool"]->isChecked());
    QCOMPARE(source, image());
  }
  void switchingToolsCancelsGesturesAndLiveKeyboardEditsAreBlocked() {
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    const auto id = *session.createRectangle({150, 130}, {250, 230}); session.markSaved();
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto tools = annotationToolActions(canvas, &canvas); const auto before = serializeDocument(session.document());
    const QPoint at = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, at); QVERIFY(canvas.interactionActive());
    QTest::keyClick(&canvas, Qt::Key_Delete); QTest::keyClick(&canvas, Qt::Key_Right); QTest::keyClick(&canvas, Qt::Key_R);
    tools["select-tool"]->trigger(); QVERIFY(!canvas.interactionActive());
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, at + QPoint(20, 0));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.selection(), QSet<QUuid>{id}); QVERIFY(!session.dirty());
    tools["rectangle-tool"]->trigger(); const QPoint blank = canvas.viewState().toView({400, 400}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, blank); QVERIFY(canvas.interactionActive());
    tools["select-tool"]->trigger(); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, blank + QPoint(20, 10));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1); QVERIFY(!session.dirty());
    canvas.setEnabled(false); tools["crop-tool"]->setChecked(true); QCOMPARE(canvas.tool(), EditorCanvas::Tool::Select);
    QVERIFY(tools["select-tool"]->isChecked()); QVERIFY(!tools["crop-tool"]->isChecked());
  }
  void failedGeometryEditReportsANativeFailureWithoutMutation() {
    const QImage source = image(); EditorSession session(document()); QVERIFY(session.createRectangle({150, 130}, {151, 230}));
    session.markSaved(); EditorCanvas canvas(session, source); canvas.resize(420, 340); canvas.show(); QTest::qWait(10);
    QSignalSpy rejected(&canvas, &EditorCanvas::editRejected); const auto before = serializeDocument(session.document());
    QTest::keyClick(&canvas, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(rejected.size(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-geometry-invalid"));
    QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty());
    QVERIFY(annotationEditMessage(rejected[0][0].toString()).contains("The document was kept"));
  }
  void failureObserverCanDestroyTheCanvasWithoutASecondNotification() {
    const QImage source = image(); EditorSession session(document()); QVERIFY(session.createRectangle({150, 130}, {151, 230})); session.markSaved();
    auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(420, 340); canvas->show();
    int rejected = 0, changed = 0;
    connect(canvas.get(), &EditorCanvas::changed, [&] { ++changed; });
    connect(canvas.get(), &EditorCanvas::editRejected, [&] { ++rejected; canvas.reset(); });
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Left, Qt::AltModifier); QApplication::sendEvent(canvas.get(), &event);
    QVERIFY(!canvas); QCOMPARE(rejected, 1); QCOMPARE(changed, 0); QVERIFY(!session.dirty()); QCOMPARE(session.undoCount(), 1);
  }
  void drawAndMoveUseImagePixelsAfterZoom() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    canvas.zoomIn();
    canvas.zoomIn();
    const QPoint a = canvas.viewState().toView({160, 140}).toPoint();
    const QPoint b = canvas.viewState().toView({240, 220}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(session.undoCount(), 1);
    const auto &r = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QVERIFY(std::abs(r.left() - 160) <= 1 / canvas.viewState().zoom());
    QVERIFY(std::abs(r.top() - 140) <= 1 / canvas.viewState().zoom());
    const double original = r.left();
    const QPoint middle = canvas.viewState().toView({200, 180}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, middle);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, middle + QPoint(25, 0));
    QCOMPARE(session.undoCount(), 2);
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), original + 25 / 1.5625);
    QVERIFY(session.undo());
    QCOMPARE(std::get<RectangleAnnotation>(session.document().annotations[0]).left(), original);
  }

  void zoomPanAndScrollDoNotDirtySavedDocument() {
    const QImage source = image();
    EditorSession session(document());
    QVERIFY(session.createRectangle({150, 130}, {250, 230}));
    session.markSaved();
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF anchor(190, 160);
    const QPointF logical = canvas.viewState().toImage(anchor);
    wheel(canvas, anchor, Qt::ControlModifier);
    QCOMPARE(canvas.viewState().toImage(anchor), logical);
    wheel(canvas, anchor, Qt::NoModifier);
    canvas.horizontalScrollBar()->setValue(canvas.horizontalScrollBar()->value() + 20);
    QTest::mousePress(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseRelease(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(130, 112));
    canvas.zoomToFit();
    canvas.resetZoom();
    QCOMPARE(session.undoCount(), 1);
    QCOMPARE(session.redoCount(), 0);
    QVERIFY(!session.dirty());
    QCOMPARE(source, image());
  }

  void nativePinchKeepsPointerAnchor() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF anchor(190, 160);
    const QPointF logical = canvas.viewState().toImage(anchor);
    const double previous = canvas.viewState().zoom();
    QNativeGestureEvent event(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, anchor, anchor,
                              canvas.viewport()->mapToGlobal(anchor.toPoint()), .2, {});
    QApplication::sendEvent(canvas.viewport(), &event);
    QVERIFY(canvas.viewState().zoom() > previous);
    QVERIFY(QLineF(canvas.viewState().toImage(anchor), logical).length() < 1e-10);
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(!session.dirty());
  }

  void cancelledPanAndDrawRestoreCommittedState() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    const QPointF offset = canvas.viewState().offset();
    QTest::keyPress(&canvas, Qt::Key_Space);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    QTest::mouseMove(canvas.viewport(), QPoint(190, 170));
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(190, 170));
    QTest::keyRelease(&canvas, Qt::Key_Space);
    QCOMPARE(canvas.viewState().offset(), offset);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150, 150));
    QTest::mouseMove(canvas.viewport(), QPoint(190, 170));
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(190, 170));
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(session.document().annotations.empty());
    QVERIFY(!session.dirty());
  }

  void rotatedHitTestingUsesDisplayedShape() {
    const QImage source = image();
    EditorSession session(document());
    const auto id = session.createRectangle({150, 180}, {350, 200});
    QVERIFY(id);
    QVERIFY(session.rotateSelection(90));
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(520, 440);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    // Inside the visible vertical rectangle, outside its unrotated bounds.
    const QPoint at = canvas.viewState().toView({250, 120}).toPoint();
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(session.selection(), QSet<QUuid>{*id});
    QCOMPARE(session.undoCount(), 2);
  }

  void hitTargetsStaySixScreenPixelsAtLowZoom() {
    const QImage source = image();
    EditorSession session(document());
    const auto id = session.createRectangle({200, 200}, {300, 300});
    QVERIFY(id);
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340);
    canvas.show();
    QTest::qWait(10);
    canvas.zoomToFit();
    for (int n = 0; n < 4; ++n) canvas.zoomOut();
    QVERIFY(canvas.viewState().zoom() < .25);
    // Five screen pixels from the rectangle remains a usable click target.
    const QPoint at = canvas.viewState().toView({200, 250}).toPoint() - QPoint(5, 0);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(session.selection(), QSet<QUuid>{*id});
    QCOMPARE(session.undoCount(), 1);
  }

  void rotatedPreviewUsesViewportTransform() {
    const QImage source = image();
    EditorSession session(document());
    QVERIFY(session.createRectangle({150, 180}, {350, 200}));
    session.setFillColor(0xFF00FF00);
    session.rotateSelection(90);
    session.select({});
    EditorCanvas canvas(session, source);
    canvas.resize(520, 440);
    canvas.show();
    QTest::qWait(10);
    canvas.resetZoom();
    canvas.zoomIn();
    const QPoint inside = canvas.viewState().toView({250, 150}).toPoint();
    const QPoint outside = canvas.viewState().toView({200, 190}).toPoint();
    const QImage preview = canvas.viewport()->grab().toImage();
    QCOMPARE(preview.pixelColor(inside), QColor(Qt::green));
    QCOMPARE(preview.pixelColor(outside), QColor(Qt::white));
  }

  void cropToolUsesImageCoordinatesAndCancellationIsLossless() {
    const QImage source = image();
    EditorSession session(document());
    EditorCanvas canvas(session, source);
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10);
    canvas.resetZoom(); canvas.zoomIn(); canvas.setCropTool(true);
    QSignalSpy requested(&canvas, &EditorCanvas::cropRequested);
    const QPoint a = canvas.viewState().toView({150, 130}).toPoint();
    const QPoint b = canvas.viewState().toView({250, 230}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::keyClick(&canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(requested.size(), 0);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, b);
    QCOMPARE(requested.size(), 1);
    const QPointF start = requested[0][0].toPointF(), end = requested[0][1].toPointF();
    QVERIFY(QLineF(start, QPointF(150, 130)).length() < 1);
    QVERIFY(QLineF(end, QPointF(250, 230)).length() < 1);
    QCOMPARE(session.undoCount(), 0);
    QVERIFY(session.document().annotations.empty());  // the window commits the crop, not a rectangle annotation
  }
};

QTEST_MAIN(EditorCanvasTest)
#include "EditorCanvasTest.moc"
