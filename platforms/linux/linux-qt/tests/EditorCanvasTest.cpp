// Native input tests exercise the actual canvas and shared session model.
#include "EditorCanvas.h"
#include "TextContentDialog.h"
#include "image-editor/AnnotationRenderer.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QPainter>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include "NativeAnnotationTools.h"
#include "RectanglePropertiesDialog.h"
#include "SelectionRotationDialog.h"
#include <QPlainTextEdit>
#include <QInputDialog>

#include <QAction>
#include <QBuffer>
#include <QJsonArray>
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
#include <cmath>
#include <limits>
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
// The pointer reports the pixel that contains it, so one document pixel is aimed
// at with the first whole view point inside its own view rectangle; the pair also
// reports whether that point really lands inside it.
std::pair<QPoint, bool> viewPointInPixel(const EditorViewport &view, QPoint pixel) {
  const QPointF corner(pixel);
  const QRectF bounds(view.toView(corner), view.toView(corner + QPointF(1, 1)));
  const QPoint at(int(std::ceil(bounds.left())), int(std::ceil(bounds.top())));
  return {at, bounds.contains(at)};
}
// QTest delivers nothing while the global pointer keeps the same place, so a
// wanted view point is always approached from a second position.
void hoverViewPoint(QWidget *viewport, QPoint at) {
  QTest::mouseMove(viewport, at + QPoint(-3, 2));
  QTest::mouseMove(viewport, at);
}
}  // namespace

class EditorCanvasTest : public QObject {
  Q_OBJECT
  QTemporaryDir dialogPreferences;
private slots:
  void unchangedStrengthDialogPreservesFractionAndDefault_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("strength");
    for (const auto &type : {shape::blur, shape::pixelate, shape::magnify, shape::spotlight}) {
      QTest::newRow(qPrintable(type + "-fraction")) << type << 1.25; QTest::newRow(qPrintable(type + "-default")) << type << 0.0;
    }
  }
  void unchangedStrengthDialogPreservesFractionAndDefault() {
    QFETCH(QString, type); QFETCH(double, strength); EditorSession session(document()); ShapeContent content; content.type = type; content.strength = strength;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source); bool acted = false;
    QTimer timer; connect(&timer, &QTimer::timeout, &canvas, [&] {
      auto *input = canvas.findChild<QInputDialog *>(); if (!input || acted) return; acted = true; input->accept();
    });
    timer.start(5); canvas.editShapeContent(&canvas); QTRY_VERIFY(acted); timer.stop(); QCOMPARE(session.selectedRectangle()->content.strength, strength); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
  }
  void strengthDialogCommitsFractionalValuesOnce_data() {
    QTest::addColumn<QString>("type"); for (const auto &type : {shape::blur, shape::pixelate, shape::magnify, shape::spotlight}) QTest::newRow(qPrintable(type)) << type;
  }
  void strengthDialogCommitsFractionalValuesOnce() {
    QFETCH(QString, type); auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-28T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = type; content.strength = 0;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *input = canvas.findChild<QInputDialog *>(); QVERIFY(input); QCOMPARE(input->inputMode(), QInputDialog::DoubleInput); QVERIFY(canvas.interactionActive());
    QCOMPARE(input->doubleMinimum(), type == shape::spotlight ? 0.0 : 1.0); QCOMPARE(input->doubleMaximum(), type == shape::magnify ? 16.0 : type == shape::spotlight ? 100.0 : 1000.0);
    input->setDoubleValue(1.25); input->reject(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    const double value = 1.2345678901234567; input->setDoubleValue(value); input->accept(); input->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.selectedRectangle()->content.strength, value); QCOMPARE(session.undoCount(), 2); QCOMPARE(rejected.count(), 0);
    const auto completed = serializeDocument(session.document()); const auto loaded = parseDocument(completed); QVERIFY(loaded.document); QCOMPARE(serializeDocument(*loaded.document), completed);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), completed); QCOMPARE(source, image());
  }
  void unchangedStrengthPreservesCompatibleValuesOutsideNativeEditingRange_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("value"); QTest::newRow("blur") << shape::blur << 0.125; QTest::newRow("pixelate") << shape::pixelate << 0.125; QTest::newRow("magnify") << shape::magnify << 1000.0; QTest::newRow("spotlight") << shape::spotlight << 1000.0;
  }
  void unchangedStrengthPreservesCompatibleValuesOutsideNativeEditingRange() {
    QFETCH(QString, type); QFETCH(double, value); EditorSession session(document()); ShapeContent content; content.type = type; content.strength = value;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source); QVERIFY(canvas.editShapeContent(&canvas)); auto *input = canvas.findChild<QInputDialog *>(); QVERIFY(input);
    QVERIFY(input->doubleValue() != value); input->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
  }
  void strengthDialogRejectsStaleAndDeadOwners_data() { stepNumericDialogRejectsStaleAndDeadOwners_data(); }
  void strengthDialogRejectsStaleAndDeadOwners() {
    QFETCH(QString, mode); EditorSession session(document()); ShapeContent content; content.type = shape::magnify; content.strength = 1.25;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); std::unique_ptr<EditorSession> replacement; auto parent = std::make_unique<QWidget>(); auto canvas = std::make_unique<EditorCanvas>(session, source);
    QVERIFY(canvas->editShapeContent(parent.get())); QPointer<QInputDialog> input = parent->findChild<QInputDialog *>(); QVERIFY(input); input->setDoubleValue(2.5);
    if (mode == "state") QVERIFY(session.createRectangle({400, 400}, {450, 450})); if (mode == "selection") session.select({});
    if (mode == "document") { replacement = std::make_unique<EditorSession>(document()); QVERIFY(replacement->createShape({{100, 100}, {180, 180}, {}}, content, replacement->stateId(), replacement->selection())); canvas->resetDocument(*replacement, source); }
    if (mode == "disabled") canvas->setEnabled(false);
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto otherBefore = replacement ? serializeDocument(replacement->document()) : QJsonObject{};
    if (mode == "owner") { QSignalSpy changes(canvas.get(), &EditorCanvas::changed); const auto count = changes.count(); canvas.reset(); input->accept(); QCoreApplication::processEvents(); QCOMPARE(changes.count(), count); }
    else if (mode == "parent") { parent.reset(); QTRY_VERIFY(!canvas->interactionActive()); } else { input->accept(); QTRY_VERIFY(!canvas->interactionActive()); }
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo); if (replacement) QCOMPARE(serializeDocument(replacement->document()), otherBefore);
  }
  void oldStrengthDialogCannotCancelNewStepEditing() {
    EditorSession session(document()); ShapeContent content; content.type = shape::blur; content.strength = 1.25; QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); EditorCanvas canvas(session, source); QVERIFY(canvas.editShapeContent(&canvas)); auto *old = canvas.findChild<QInputDialog *>(); QVERIFY(old); old->reject();
    content.type = shape::step; content.stepValue = 1; content.strength = 0; QVERIFY(session.createShape({{300, 300}, {380, 380}, {}}, content, session.stateId(), session.selection()));
    QVERIFY(canvas.editShapeContent(&canvas)); auto *current = canvas.findChildren<QInputDialog *>().last(); QVERIFY(current != old); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); QVERIFY(canvas.interactionActive());
    current->setIntValue(5); current->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.selectedRectangle()->content.stepValue, qint64(5)); QCOMPARE(session.undoCount(), 3);
  }
  void stepEditorPreservesLargeStoredNumbersWithoutClamping() {
    EditorSession session(document()); ShapeContent content; content.type = shape::step; content.stepValue = 123456789;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source); int observed = -1; bool acted = false;
    QTimer timer; connect(&timer, &QTimer::timeout, &canvas, [&] {
      auto *input = canvas.findChild<QInputDialog *>(); if (!input || acted) return; acted = true; observed = input->intValue(); input->reject();
    });
    timer.start(5); canvas.editShapeContent(&canvas); QTRY_VERIFY(acted); timer.stop(); QCOMPARE(observed, 123456789); QCOMPARE(serializeDocument(session.document()), before);
  }
  void invalidStepNumbersCannotEnterCreation() {
    for (qint64 value : {qint64(-1), qint64(1000000001), std::numeric_limits<qint64>::max()}) {
      EditorSession session(document()); const auto before = serializeDocument(session.document()); ShapeContent content; content.type = shape::step; content.stepValue = value;
      QVERIFY(!session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection())); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
    }
  }
  void invalidStepNumbersCannotEnterEdits() {
    EditorSession session(document()); ShapeContent content; content.type = shape::step; content.stepValue = 1;
    const auto id = session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    for (qint64 value : {qint64(-1), qint64(1000000001), std::numeric_limits<qint64>::max()}) {
      content.stepValue = value; QVERIFY(!session.editShapeContent(*id, content, state)); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(session.stateId(), state); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    }
  }
  void stepNumericDialogCommitsOnceAndReloads_data() {
    QTest::addColumn<int>("value"); QTest::newRow("zero") << 0; QTest::newRow("one") << 1; QTest::newRow("past-old-ui-limit") << 1000001; QTest::newRow("version-one-maximum") << 1000000000;
  }
  void stepNumericDialogCommitsOnceAndReloads() {
    QFETCH(int, value); auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-27T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = shape::step; content.stepValue = 123456789; content.fontSize = 28;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *input = canvas.findChild<QInputDialog *>(); QVERIFY(input); QVERIFY(canvas.interactionActive()); QCOMPARE(input->intValue(), 123456789); QCOMPARE(input->intMinimum(), 0); QCOMPARE(input->intMaximum(), 1000000000);
    input->setIntValue(value); input->reject(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input); input->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); input = canvas.findChild<QInputDialog *>(); QVERIFY(input); input->setIntValue(value); input->accept(); input->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.undoCount(), 2); QCOMPARE(session.selectedRectangle()->content.stepValue, qint64(value)); QCOMPARE(session.selectedRectangle()->content.fontSize, 28.0); QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(rejected.count(), 0);
    const auto completed = serializeDocument(session.document()); const auto loaded = parseDocument(completed); QVERIFY(loaded.document); QCOMPARE(serializeDocument(*loaded.document), completed);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), completed); QCOMPARE(source, image());
  }
  void stepNumericDialogRejectsStaleAndDeadOwners_data() {
    QTest::addColumn<QString>("mode"); for (const auto &mode : {"state", "selection", "document", "disabled", "owner", "parent"}) QTest::newRow(mode) << QString(mode);
  }
  void stepNumericDialogRejectsStaleAndDeadOwners() {
    QFETCH(QString, mode); EditorSession session(document()); ShapeContent content; content.type = shape::step; content.stepValue = 123456789;
    QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); std::unique_ptr<EditorSession> replacement; auto parent = std::make_unique<QWidget>(); auto canvas = std::make_unique<EditorCanvas>(session, source);
    QVERIFY(canvas->editShapeContent(parent.get())); QPointer<QInputDialog> input = parent->findChild<QInputDialog *>(); QVERIFY(input); input->setIntValue(1000000000);
    if (mode == "state") QVERIFY(session.createRectangle({400, 400}, {450, 450}));
    if (mode == "selection") session.select({});
    if (mode == "document") { replacement = std::make_unique<EditorSession>(document()); QVERIFY(replacement->createShape({{100, 100}, {180, 180}, {}}, content, replacement->stateId(), replacement->selection())); canvas->resetDocument(*replacement, source); }
    if (mode == "disabled") canvas->setEnabled(false);
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto otherBefore = replacement ? serializeDocument(replacement->document()) : QJsonObject{};
    if (mode == "owner") { QSignalSpy changes(canvas.get(), &EditorCanvas::changed); const auto count = changes.count(); canvas.reset(); input->accept(); QCoreApplication::processEvents(); QCOMPARE(changes.count(), count); }
    else if (mode == "parent") { parent.reset(); QTRY_VERIFY(!canvas->interactionActive()); }
    else { input->accept(); QTRY_VERIFY(!canvas->interactionActive()); }
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo); if (replacement) QCOMPARE(serializeDocument(replacement->document()), otherBefore);
  }
  void oldStepDialogDestructionCannotCancelNewContentDialog() {
    EditorSession session(document()); ShapeContent content; content.type = shape::step; content.stepValue = 1; QVERIFY(session.createShape({{100, 100}, {180, 180}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); EditorCanvas canvas(session, source); QVERIFY(canvas.editShapeContent(&canvas)); auto *old = canvas.findChild<QInputDialog *>(); QVERIFY(old); old->reject();
    QVERIFY(canvas.editShapeContent(&canvas)); const auto inputs = canvas.findChildren<QInputDialog *>(); auto *current = inputs.last(); QVERIFY(current != old); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); QVERIFY(canvas.interactionActive());
    current->setIntValue(1000000000); current->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.selectedRectangle()->content.stepValue, qint64(1000000000)); QCOMPARE(session.undoCount(), 2);
  }
  void textCreationCommitsFormattedSnapshotOnce_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void textCreationCommitsFormattedSnapshotOnce() {
    QFETCH(QString, type); const auto tool = type == shape::text ? EditorCanvas::Tool::Text : EditorCanvas::Tool::SpeechBalloon; auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-27T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); RectangleStyle original; original.strokeColor = 0xFF123456; original.fillColor = 0xFFEEEEEE; original.strokeWidth = 5;
    QVERIFY(session.setCreationStyle(original)); const auto before = serializeDocument(session.document()); const auto source = image(); EditorCanvas canvas(session, source);
    canvas.resize(900, 700); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(tool);
    const auto start = canvas.viewState().toView({100, 100}).toPoint(), end = canvas.viewState().toView({350, 250}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, start);
    auto changedDefault = original; changedDefault.strokeColor = 0xFFABCDEF; changedDefault.strokeWidth = 9; QVERIFY(session.setCreationStyle(changedDefault));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, end);
    auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog); QVERIFY(canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
    dialog->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Discard"); dialog->reject(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(session.setCreationStyle(original));
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, start); QVERIFY(session.setCreationStyle(changedDefault)); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, end);
    dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog); dialog->accept(); QVERIFY(canvas.interactionActive()); QCOMPARE(session.undoCount(), 0);
    dialog->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Created\nmultiline"); dialog->findChild<QDoubleSpinBox *>("balloonFontSize")->setValue(36);
    const auto families = QFontDatabase::families(); QVERIFY(!families.isEmpty()); dialog->findChild<QComboBox *>("annotationFontFamily")->setEditText(families.last());
    dialog->findChild<QCheckBox *>("annotationTextBold")->setCheckState(Qt::Checked); dialog->findChild<QCheckBox *>("annotationTextItalic")->setCheckState(Qt::Checked);
    auto *alignment = dialog->findChild<QComboBox *>("annotationTextAlignment"); alignment->setCurrentIndex(alignment->findData("right")); dialog->findChild<QLineEdit *>("annotationTextColor")->setText("#80123456"); dialog->findChild<QLineEdit *>("annotationTextFillColor")->setText("#40112233");
    if (tool == EditorCanvas::Tool::SpeechBalloon) { dialog->findChild<QCheckBox *>("balloonTailEnabled")->setChecked(true); dialog->findChild<QLineEdit *>("balloonTailX")->setText("0.8"); dialog->findChild<QLineEdit *>("balloonTailY")->setText("1.3"); }
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, start); QCOMPARE(session.undoCount(), 0); QVERIFY(canvas.interactionActive());
    dialog->accept(); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), std::size_t(1));
    const auto *created = session.selectedRectangle(); QVERIFY(created); QCOMPARE(created->type(), type); QCOMPARE(created->left(), 100.0); QCOMPARE(created->top(), 100.0); QCOMPARE(created->right(), 350.0); QCOMPARE(created->bottom(), 250.0);
    QCOMPARE(created->style.strokeColor, original.strokeColor); QCOMPARE(created->style.fillColor, original.fillColor); QCOMPARE(created->style.strokeWidth, original.strokeWidth);
    QCOMPARE(created->content.text, QString("Created\nmultiline")); QCOMPARE(created->content.fontSize, 36.0); QCOMPARE(created->content.fontFamily, families.last());
    QCOMPARE(created->content.textBold, std::optional<bool>(true)); QCOMPARE(created->content.textItalic, std::optional<bool>(true)); QCOMPARE(created->content.textAlignment, QString("right")); QCOMPARE(created->content.textColor, std::optional<Argb>(0x80123456)); QCOMPARE(created->content.textFillColor, std::optional<Argb>(0x40112233));
    if (tool == EditorCanvas::Tool::SpeechBalloon) { QCOMPARE(created->content.points.size(), std::size_t(1)); QCOMPARE(created->content.points[0].x, 0.8); QCOMPARE(created->content.points[0].y, 1.3); }
    const auto completed = serializeDocument(session.document()); const auto loaded = parseDocument(completed); QVERIFY(loaded.document); QCOMPARE(serializeDocument(*loaded.document), completed);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), completed); QCOMPARE(source, image()); QCOMPARE(canvas.tool(), tool);
  }
  void textCreationRefusesStaleOrCancelledOwners_data() {
    QTest::addColumn<QString>("mode");
    for (const auto &mode : {"state", "selection", "document", "disabled", "tool", "source", "owner", "escape"}) QTest::newRow(mode) << QString(mode);
  }
  void textCreationRefusesStaleOrCancelledOwners() {
    QFETCH(QString, mode); EditorSession session(document()); const auto existing = session.createRectangle({{10, 10}, {40, 40}, {}}, session.stateId(), session.selection()); QVERIFY(existing);
    const auto source = image(); std::unique_ptr<EditorSession> replacement; auto canvas = std::make_unique<EditorCanvas>(session, source); canvas->resize(900, 700); canvas->show(); QCoreApplication::processEvents(); canvas->resetZoom(); canvas->setTool(EditorCanvas::Tool::Text);
    const auto start = canvas->viewState().toView({100, 100}).toPoint(), end = canvas->viewState().toView({350, 250}).toPoint();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, start); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, end); QPointer<TextContentDialog> dialog = canvas->findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Late creation");
    if (mode == "state") QVERIFY(session.createRectangle({{400, 400}, {450, 450}, {}}, session.stateId(), session.selection()));
    if (mode == "selection") session.select({*existing});
    if (mode == "document") { replacement = std::make_unique<EditorSession>(document()); QVERIFY(replacement->createRectangle({{10, 10}, {40, 40}, {}}, replacement->stateId(), replacement->selection())); canvas->resetDocument(*replacement, source); }
    if (mode == "disabled") canvas->setEnabled(false);
    if (mode == "tool") canvas->setTool(EditorCanvas::Tool::Ellipse);
    if (mode == "source") canvas->sourceChanged(source);
    if (mode == "escape") QTest::keyClick(dialog, Qt::Key_Escape);
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto otherBefore = replacement ? serializeDocument(replacement->document()) : QJsonObject{};
    if (mode == "owner") { QSignalSpy changes(canvas.get(), &EditorCanvas::changed); const auto count = changes.count(); canvas.reset(); QCoreApplication::processEvents(); QCOMPARE(changes.count(), count); QVERIFY(!dialog); }
    else { if (dialog) dialog->accept(); QTRY_VERIFY(!canvas->interactionActive()); }
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo); if (replacement) QCOMPARE(serializeDocument(replacement->document()), otherBefore);
  }
  void creationSurvivesFocusTransferToItsDialog() {
    EditorSession session(document()); const auto source = image(); EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Text);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({100, 100}).toPoint()); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({350, 250}).toPoint());
    QPointer<TextContentDialog> dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    QFocusEvent loss(QEvent::FocusOut, Qt::ActiveWindowFocusReason); QApplication::sendEvent(&canvas, &loss); QCoreApplication::processEvents();
    QVERIFY(canvas.interactionActive()); QVERIFY(dialog); dialog->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Focused dialog"); dialog->accept(); QCOMPARE(session.undoCount(), 1);
  }
  void completedTextDialogCannotClearLaterModalPendingState() {
    EditorSession session(document()); ShapeContent content; content.type = shape::text; content.text = "Existing";
    QVERIFY(session.createShape({{100, 100}, {350, 250}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); EditorCanvas canvas(session, source); QVERIFY(canvas.editShapeContent(&canvas)); auto *old = canvas.findChild<TextContentDialog *>(); QVERIFY(old); old->reject();
    QVERIFY(session.createRectangle({10, 10}, {40, 40})); bool checked = false; bool stillPending = false;
    QTimer timer; connect(&timer, &QTimer::timeout, &canvas, [&] {
      auto *rotation = canvas.findChild<SelectionRotationDialog *>(); if (!rotation) return; timer.stop();
      QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); stillPending = canvas.interactionActive(); checked = true; rotation->reject();
    });
    timer.start(5); QVERIFY(!canvas.rotateSelection(&canvas)); timer.stop(); QVERIFY(checked); QVERIFY(stillPending); QCOMPARE(session.undoCount(), 2);
  }
  void oldCreationDialogDeletionCannotCancelANewCreation() {
    EditorSession session(document()); const auto source = image(); EditorCanvas canvas(session, source); canvas.resize(900, 700); canvas.show(); QCoreApplication::processEvents(); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Text);
    const auto start = canvas.viewState().toView({100, 100}).toPoint(), end = canvas.viewState().toView({350, 250}).toPoint();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, start); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, end); auto *old = canvas.findChild<TextContentDialog *>(); QVERIFY(old); old->reject();
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, start); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, end); const auto dialogs = canvas.findChildren<TextContentDialog *>(); auto *current = dialogs.last(); QVERIFY(current != old);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); QVERIFY(canvas.interactionActive());
    current->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Second dialog"); current->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), std::size_t(1));
  }
  void fontFamiliesCommitAndPreserveMissingNames_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void fontFamiliesCommitAndPreserveMissingNames() {
    QFETCH(QString, type); const auto installed = QFontDatabase::families(); QVERIFY(!installed.isEmpty()); const QString available = installed.first();
    const QString missing = "XerahS Missing Test Font 8b2649"; QVERIFY(!QFontDatabase::hasFamily(missing));
    auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-27T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "Family"; content.fontFamily = missing;
    QVERIFY(session.createShape({{100, 100}, {400, 250}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto source = image(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog); auto *family = dialog->findChild<QComboBox *>("annotationFontFamily"); QVERIFY(family);
    QCOMPARE(family->currentText(), missing); QVERIFY(dialog->findChild<QLabel *>("annotationFontFamilyStatus")->text().startsWith("font-family-unavailable:"));
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    const auto loaded = parseDocument(before); QVERIFY(loaded.document); QCOMPARE(std::get<RectangleAnnotation>(loaded.document->annotations[0]).content.fontFamily, missing);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    family = dialog->findChild<QComboBox *>("annotationFontFamily"); family->setEditText(available); dialog->reject(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    family = dialog->findChild<QComboBox *>("annotationFontFamily"); family->setEditText(missing + " replacement"); dialog->accept(); QVERIFY(canvas.interactionActive());
    QVERIFY(dialog->findChild<QLabel *>("balloonContentError")->text().startsWith("font-family-unavailable:")); QCOMPARE(session.undoCount(), undo); QCOMPARE(serializeDocument(session.document()), before);
    family->setEditText(available); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), undo + 1); QCOMPARE(session.selectedRectangle()->content.fontFamily, available);
    const auto replacement = parseDocument(serializeDocument(session.document())); QVERIFY(replacement.document); QCOMPARE(std::get<RectangleAnnotation>(replacement.document->annotations[0]).content.fontFamily, available);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->findChild<QComboBox *>("annotationFontFamily")->setEditText(QString{}); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), undo + 2);
    QVERIFY(session.selectedRectangle()->content.fontFamily.isEmpty()); QVERIFY(!serializeDocument(session.document()).value("annotations").toArray()[0].toObject().contains("fontFamily"));
    for (const auto &bad : {QJsonValue(true), QJsonValue(1), QJsonValue(QJsonValue::Null), QJsonValue(QString("line\nbreak")), QJsonValue(QString(kMaxTextLength + 1, 'x'))}) {
      auto payload = before; auto annotations = payload.value("annotations").toArray(); auto object = annotations[0].toObject(); object.insert("fontFamily", bad); annotations[0] = object; payload.insert("annotations", annotations); QVERIFY(!parseDocument(payload).document);
      object.insert("type", "ellipse"); annotations[0] = object; payload.insert("annotations", annotations); const auto opaque = parseDocument(payload); QVERIFY(opaque.document);
      QCOMPARE(serializeDocument(*opaque.document).value("annotations").toArray()[0].toObject().value("fontFamily"), bad);
    }
    for (const auto &bad : {QString(QChar(0)), QString("bad\nfont"), QString(kMaxTextLength + 1, 'x')}) {
      content.fontFamily = bad; EditorSession invalid(initial); QVERIFY(!invalid.createShape({{100, 100}, {400, 250}, {}}, content, invalid.stateId(), invalid.selection())); QCOMPARE(invalid.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(invalid.undoCount(), 0);
      auto edit = session.selectedRectangle()->content; edit.fontFamily = bad; const auto state = session.stateId(); QVERIFY(!session.editShapeContent(session.selectedRectangle()->id, edit, state)); QCOMPARE(session.stateId(), state);
    }
  }
  void unchangedFontSizeKeepsCompatibleValue_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<double>("size");
    for (const auto &type : {shape::text, shape::speechBalloon})
      for (const auto size : {0.0, 0.125, 1.25, 2000.0})
        QTest::newRow(qPrintable(type + QString::number(size))) << type << size;
  }
  void unchangedFontSizeKeepsCompatibleValue() {
    QFETCH(QString, type); QFETCH(double, size);
    EditorSession session(document()); ShapeContent content; content.type = type; content.text = "Saved"; content.fontSize = size;
    QVERIFY(session.createShape({{100, 100}, {400, 250}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto source = image(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.selectedRectangle()->content.fontSize, size);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
  }

  void fractionalFontSizeCommitsOnceAndReloads_data() { fontFamiliesCommitAndPreserveMissingNames_data(); }
  void fractionalFontSizeCommitsOnceAndReloads() {
    QFETCH(QString, type);
    const auto source = image(); auto initial = document();
    initial.createdAt = initial.modifiedAt = "2026-10-28T00:00:00Z";
    QBuffer buffer(&initial.sourceImagePng); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(source.save(&buffer, "PNG"));
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "Precision"; content.fontSize = 12.5;
    content.textAlignment = "right"; content.textBold = true; content.textColor = 0x7F123456;
    QVERIFY(session.createShape({{100, 100}, {400, 250}, {}}, content, session.stateId(), session.selection()));
    EditorCanvas canvas(session, source); const auto before = serializeDocument(session.document()); const auto undo = session.undoCount();
    QVERIFY(canvas.editShapeContent(&canvas)); auto *cancelled = canvas.findChildren<TextContentDialog *>().last();
    auto *cancelSize = cancelled->findChild<QDoubleSpinBox *>("balloonFontSize"); QVERIFY(cancelSize);
    QCOMPARE(cancelSize->value(), 12.5); cancelSize->setValue(45.125); cancelled->reject();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    for (const double chosen : {1.25, 1.2345678901234567, 2000.0}) {
      const auto previous = serializeDocument(session.document()); const auto count = session.undoCount();
      QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChildren<TextContentDialog *>().last();
      auto *size = dialog->findChild<QDoubleSpinBox *>("balloonFontSize"); QVERIFY(size);
      QCOMPARE(size->minimum(), 1.0); QCOMPARE(size->maximum(), 2000.0);
      size->setValue(chosen); QCOMPARE(size->value(), chosen); dialog->accept(); dialog->accept();
      QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), count + 1);
      const auto &actual = session.selectedRectangle()->content;
      QCOMPARE(actual.fontSize, chosen); QCOMPARE(actual.text, content.text); QCOMPARE(actual.textColor, content.textColor);
      QCOMPARE(actual.textAlignment, content.textAlignment); QCOMPARE(actual.textBold, content.textBold);
      const auto saved = serializeDocument(session.document()); const auto parsed = parseDocument(saved); QVERIFY(parsed.document);
      QCOMPARE(serializeDocument(*parsed.document), saved); QCOMPARE(session.document().sourceImagePng, initial.sourceImagePng);
      QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), previous);
      QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), saved);
    }
  }

  void textFillColorControlsKeepAlphaAndHistory_data() { fontFamiliesCommitAndPreserveMissingNames_data(); }
  void textFillColorControlsKeepAlphaAndHistory() {
    QFETCH(QString, type); const auto source = image(); auto initial = document();
    initial.createdAt = initial.modifiedAt = "2026-10-29T00:00:00Z";
    QBuffer buffer(&initial.sourceImagePng); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(source.save(&buffer, "PNG"));
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "Fill"; content.fontSize = 12.5;
    content.textColor = 0x7F112233; content.textAlignment = "right";
    QVERIFY(session.createShape({{100, 100}, {400, 250}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto initialUndo = session.undoCount(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChildren<TextContentDialog *>().last();
    auto *fill = dialog->findChild<QLineEdit *>("annotationTextFillColor"); QVERIFY(fill); QVERIFY(fill->text().isEmpty());
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), initialUndo);
    QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChildren<TextContentDialog *>().last();
    fill = dialog->findChild<QLineEdit *>("annotationTextFillColor"); QVERIFY(fill); fill->setText("#80ABCDEF"); dialog->reject();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before);
    for (const auto &choice : {QString("#80aBcDeF"), QString("#00000000"), QString("#FF123456"), QString{}}) {
      const auto previous = serializeDocument(session.document()); const auto count = session.undoCount();
      QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChildren<TextContentDialog *>().last();
      fill = dialog->findChild<QLineEdit *>("annotationTextFillColor"); QVERIFY(fill); fill->setText("bad-color"); dialog->accept();
      QVERIFY(canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), previous);
      QVERIFY(dialog->findChild<QLabel *>("balloonContentError")->text().startsWith("annotation-text-fill-color-invalid:"));
      fill->setText(choice); dialog->accept(); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), count + 1);
      const auto saved = serializeDocument(session.document()); const auto annotation = saved.value("annotations").toArray()[0].toObject();
      if (choice.isEmpty()) QVERIFY(!annotation.contains("textFillColor")); else QCOMPARE(annotation.value("textFillColor"), QJsonValue(choice.toUpper()));
      QCOMPARE(session.selectedRectangle()->content.fontSize, content.fontSize); QCOMPARE(session.selectedRectangle()->content.textColor, content.textColor);
      QCOMPARE(session.selectedRectangle()->content.textAlignment, content.textAlignment);
      const auto parsed = parseDocument(saved); QVERIFY(parsed.document); QCOMPARE(serializeDocument(*parsed.document), saved);
      QCOMPARE(session.document().sourceImagePng, initial.sourceImagePng);
      QVERIFY(canvas.editShapeContent(&canvas)); auto *unchanged = canvas.findChildren<TextContentDialog *>().last();
      QCOMPARE(unchanged->findChild<QLineEdit *>("annotationTextFillColor")->text(), choice.toUpper());
      unchanged->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), count + 1); QCOMPARE(serializeDocument(session.document()), saved);
      QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), previous); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), saved);
    }
  }

  void textFillColorsRenderIndependentAlpha_data() {
    QTest::addColumn<QString>("type"); QTest::addColumn<QString>("fill"); QTest::addColumn<quint32>("expected");
    for (const auto &type : {shape::text, shape::speechBalloon}) {
      QTest::newRow(qPrintable(type + "-opaque")) << type << QString("#FF0000FF") << quint32(0xFF0000FF);
      // Half-blue source-over opaque #204060 rounds to #1020B0.
      QTest::newRow(qPrintable(type + "-half")) << type << QString("#800000FF") << quint32(0xFF1020B0);
      QTest::newRow(qPrintable(type + "-transparent")) << type << QString("#000000FF") << quint32(0xFF204060);
    }
  }
  void textFillColorsRenderIndependentAlpha() {
    QFETCH(QString, type); QFETCH(QString, fill); QFETCH(quint32, expected);
    auto doc = document(); doc.createdAt = doc.modifiedAt = "2026-10-29T00:00:00Z";
    auto sourceImage = image(); sourceImage.fill(0xFF204060); QBuffer buffer(&doc.sourceImagePng); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(sourceImage.save(&buffer, "PNG"));
    RectangleAnnotation annotation; annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {400, 250};
    annotation.style.strokeColor = 0; annotation.style.fillColor = 0xFF00FF00;
    annotation.content.type = type; annotation.content.text = "Hidden glyph"; annotation.content.textColor = 0;
    doc.annotations.push_back(annotation); const auto source = solidImage(800, 600, 0xFF204060); const auto originalPixels = source.pixels;
    const auto legacy = renderDocument(source, doc); QVERIFY(legacy.image); QCOMPARE(legacy.image->at(250, 175), Argb(0xFF00FF00));
    std::get<RectangleAnnotation>(doc.annotations[0]).style.fillColor = 0;
    const auto fallback = renderDocument(source, doc); QVERIFY(fallback.image);
    QCOMPARE(fallback.image->at(250, 175), type == shape::speechBalloon ? Argb(0xFFFFFFFF) : Argb(0xFF204060));
    std::get<RectangleAnnotation>(doc.annotations[0]).style.fillColor = 0xFF00FF00;
    auto payload = serializeDocument(doc); auto objects = payload.value("annotations").toArray(); auto object = objects[0].toObject();
    object.insert("textFillColor", fill); objects[0] = object; payload.insert("annotations", objects);
    const auto parsed = parseDocument(payload); QVERIFY(parsed.document); const auto beforeRender = serializeDocument(*parsed.document);
    const auto actual = renderDocument(source, *parsed.document); QVERIFY(actual.image); QCOMPARE(actual.image->at(250, 175), Argb(expected));
    QCOMPARE(actual.image->at(20, 20), Argb(0xFF204060)); QCOMPARE(source.pixels, originalPixels);
    QCOMPARE(serializeDocument(*parsed.document), beforeRender);
  }
  void textFillImportRejectsMalformedAndKeepsOtherFamiliesOpaque() {
    auto doc = document(); doc.createdAt = doc.modifiedAt = "2026-10-29T00:00:00Z";
    const auto source = image(); QBuffer buffer(&doc.sourceImagePng); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(source.save(&buffer, "PNG"));
    RectangleAnnotation annotation; annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {400, 250}; annotation.content.text = "Text";
    annotation.content.type = shape::text; doc.annotations.push_back(annotation);
    for (const auto &type : {shape::text, shape::speechBalloon}) for (const auto &bad : {QJsonValue("#GG000000"), QJsonValue("#ABCDEF"), QJsonValue(42), QJsonValue(true), QJsonValue(QJsonValue::Null)}) {
      auto payload = serializeDocument(doc); auto objects = payload.value("annotations").toArray(); auto object = objects[0].toObject();
      object.insert("type", type); object.insert("textFillColor", bad); objects[0] = object; payload.insert("annotations", objects); QVERIFY(!parseDocument(payload).document);
      for (const auto &otherType : {shape::ellipse, shape::step, shape::rectangle, QString("future-type")}) {
        object.insert("type", otherType); objects[0] = object; payload.insert("annotations", objects); const auto opaque = parseDocument(payload); QVERIFY(opaque.document);
        QCOMPARE(serializeDocument(*opaque.document).value("annotations").toArray()[0].toObject().value("textFillColor"), bad);
      }
    }
  }

  void longSavedFontFamilyIsNotTruncatedByNativeEntry() {
    EditorSession session(document()); ShapeContent content; content.type = shape::text; content.text = "Saved"; content.fontFamily = QString(kMaxTextLength, 'x');
    QVERIFY(session.createShape({{100, 100}, {400, 250}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto source = image(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    const auto displayed = dialog->findChild<QComboBox *>("annotationFontFamily")->currentText(); QCOMPARE(displayed.size(), content.fontFamily.size()); QCOMPARE(displayed, content.fontFamily);
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
  }
  void nativeFontRenderingAndMissingFallback_data() { fontFamiliesCommitAndPreserveMissingNames_data(); }
  void nativeFontRenderingAndMissingFallback() {
    QFETCH(QString, type); const auto installed = QFontDatabase::families(); QVERIFY(!installed.isEmpty()); auto doc = document(); RectangleAnnotation annotation;
    annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {500, 300}; annotation.content.type = type;
    annotation.content.text = "Family WiWi"; annotation.content.fontSize = 40; annotation.style.fillColor = 0xFFFFFFFF; annotation.style.strokeColor = type == shape::text ? 0xFF000000 : 0xFFFFFFFF;
    doc.annotations.push_back(annotation); const auto source = solidImage(800, 600, 0xFFFFFFFF); const auto legacy = renderDocument(source, doc); QVERIFY(legacy.image);
    auto &content = std::get<RectangleAnnotation>(doc.annotations[0]).content; content.fontFamily = "XerahS Missing Test Font 8b2649"; QVERIFY(!QFontDatabase::hasFamily(content.fontFamily));
    const auto saved = serializeDocument(doc); const auto fallback = renderDocument(source, doc); QVERIFY(fallback.image); QCOMPARE(fallback.image->pixels, legacy.image->pixels); QCOMPARE(serializeDocument(doc), saved);
    // Native Qt text drawing is an independent font oracle; no expected glyph pixels come from the production renderer.
    for (const auto &family : {installed.first(), installed.last()}) {
      content.fontFamily = family; const auto actual = renderDocument(source, doc); QVERIFY(actual.image);
      QImage expected(800, 600, QImage::Format_ARGB32_Premultiplied); expected.fill(Qt::white); QPainter painter(&expected); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
      QFont font = painter.font(); font.setFamily(family); font.setPixelSize(40); painter.setFont(font); painter.setPen(Qt::black);
      const bool balloon = type == shape::speechBalloon; const int pad = balloon ? 8 : 4; const int flags = balloon ? int(Qt::AlignCenter) : int(Qt::AlignLeft | Qt::AlignTop);
      painter.drawText(QRectF(100 + pad, 100 + pad, 400 - 2 * pad, 200 - 2 * pad), flags | Qt::TextWordWrap, "Family WiWi"); painter.end();
      for (int y = 0; y < 600; ++y) for (int x = 0; x < 800; ++x) QCOMPARE(actual.image->at(x, y), expected.pixel(x, y));
    }
  }
  void textColorsCommitCancelValidateAndReload_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void textColorsCommitCancelValidateAndReload() {
    QFETCH(QString, type); auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-26T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "Color";
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto style = session.selectedRectangle()->style;
    QVERIFY(!before.value("annotations").toArray()[0].toObject().contains("textColor")); const auto source = image(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    auto *color = dialog->findChild<QLineEdit *>("annotationTextColor"); QVERIFY(color); QVERIFY(color->text().isEmpty());
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    color = dialog->findChild<QLineEdit *>("annotationTextColor"); color->setText("#80ff1234"); dialog->reject(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    color = dialog->findChild<QLineEdit *>("annotationTextColor");
    for (const auto &bad : {QString("red"), QString("#FF1234"), QString("#XX123456"), QString("#FF1234567")}) {
      color->setText(bad); dialog->accept(); QVERIFY(canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
      QVERIFY(dialog->findChild<QLabel *>("balloonContentError")->text().startsWith("annotation-text-color-invalid:"));
    }
    color->setText("#80ff1234"); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.undoCount(), undo + 1); QCOMPARE(session.selectedRectangle()->content.textColor, std::optional<Argb>(0x80FF1234));
    QCOMPARE(session.selectedRectangle()->style.strokeColor, style.strokeColor); QCOMPARE(session.selectedRectangle()->style.fillColor, style.fillColor);
    const auto payload = serializeDocument(session.document()); QCOMPARE(payload.value("annotations").toArray()[0].toObject().value("textColor"), QJsonValue("#80FF1234"));
    const auto loaded = parseDocument(payload); QVERIFY(loaded.document); QCOMPARE(std::get<RectangleAnnotation>(loaded.document->annotations[0]).content.textColor, std::optional<Argb>(0x80FF1234));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    color = dialog->findChild<QLineEdit *>("annotationTextColor"); QCOMPARE(color->text(), QString("#80FF1234")); color->clear(); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo + 2);
    for (const auto &bad : {QJsonValue(true), QJsonValue(1), QJsonValue(QJsonValue::Null), QJsonValue("red"), QJsonValue("#FF1234")}) {
      auto invalid = payload; auto annotations = invalid.value("annotations").toArray(); auto object = annotations[0].toObject(); object.insert("textColor", bad); annotations[0] = object; invalid.insert("annotations", annotations);
      QVERIFY(!parseDocument(invalid).document); object.insert("type", "ellipse"); annotations[0] = object; invalid.insert("annotations", annotations);
      const auto opaque = parseDocument(invalid); QVERIFY(opaque.document); QCOMPARE(serializeDocument(*opaque.document).value("annotations").toArray()[0].toObject().value("textColor"), bad);
    }
    // Core creation accepts an explicit fully transparent color, without confusing it with absence.
    EditorSession created(initial); content.textColor = 0; QVERIFY(created.createShape({{100, 100}, {300, 200}, {}}, content, created.stateId(), created.selection()));
    QCOMPARE(serializeDocument(created.document()).value("annotations").toArray()[0].toObject().value("textColor"), QJsonValue("#00000000"));
  }
  void textColorsRenderAlphaIndependently_data() { textColorsCommitCancelValidateAndReload_data(); }
  void textColorsRenderAlphaIndependently() {
    QFETCH(QString, type); auto doc = document(); RectangleAnnotation annotation;
    annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {500, 300}; annotation.content.type = type;
    annotation.content.text = "Color"; annotation.content.fontSize = 40; annotation.style.fillColor = 0xFFFFFFFF;
    annotation.style.strokeColor = type == shape::text ? 0xFF000000 : 0xFFFFFFFF; doc.annotations.push_back(annotation);
    const auto source = solidImage(800, 600, 0xFFFFFFFF); const auto legacy = renderDocument(source, doc); QVERIFY(legacy.image); QVERIFY(legacy.image->pixels != source.pixels);
    auto &content = std::get<RectangleAnnotation>(doc.annotations[0]).content; content.textColor = 0xFF000000;
    const auto black = renderDocument(source, doc); QVERIFY(black.image); QCOMPARE(black.image->pixels, legacy.image->pixels);
    for (Argb value : {Argb(0xFFFF0000), Argb(0x80FF0000), Argb(0x00FF0000)}) {
      content.textColor = value; const auto rendered = renderDocument(source, doc); QVERIFY(rendered.image); bool colored = false; int minGreen = 255;
      for (Argb pixel : rendered.image->pixels) {
        QCOMPARE(qAlpha(pixel), 255); QCOMPARE(qRed(pixel), 255); QCOMPARE(qGreen(pixel), qBlue(pixel));
        if (qGreen(pixel) < 255) colored = true; minGreen = std::min(minGreen, qGreen(pixel));
      }
      if (value == 0x00FF0000) { QVERIFY(!colored); QCOMPARE(rendered.image->pixels, source.pixels); }
      else { QVERIFY(colored); if (value == 0x80FF0000) QVERIFY(minGreen >= 127 && minGreen <= 128); else QCOMPARE(minGreen, 0); }
    }
  }
  void textFontStylesCommitCancelAndReload_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void textFontStylesCommitCancelAndReload() {
    QFETCH(QString, type); auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-26T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "Style";
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount(); const auto source = image(); EditorCanvas canvas(session, source);
    const auto legacy = before.value("annotations").toArray()[0].toObject(); QVERIFY(!legacy.contains("textBold")); QVERIFY(!legacy.contains("textItalic"));
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    auto *bold = dialog->findChild<QCheckBox *>("annotationTextBold"); auto *italic = dialog->findChild<QCheckBox *>("annotationTextItalic"); QVERIFY(bold); QVERIFY(italic);
    QCOMPARE(bold->checkState(), Qt::PartiallyChecked); QCOMPARE(italic->checkState(), Qt::PartiallyChecked);
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    bold = dialog->findChild<QCheckBox *>("annotationTextBold"); italic = dialog->findChild<QCheckBox *>("annotationTextItalic");
    bold->setCheckState(Qt::Checked); italic->setCheckState(Qt::Checked); dialog->reject(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->findChild<QCheckBox *>("annotationTextBold")->setCheckState(Qt::Checked);
    dialog->findChild<QCheckBox *>("annotationTextItalic")->setCheckState(Qt::Unchecked); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(session.selectedRectangle()->content.textBold, std::optional<bool>(true)); QCOMPARE(session.selectedRectangle()->content.textItalic, std::optional<bool>(false)); QCOMPARE(session.undoCount(), undo + 1);
    const auto loaded = parseDocument(serializeDocument(session.document())); QVERIFY(loaded.document);
    const auto restored = std::get<RectangleAnnotation>(loaded.document->annotations[0]).content;
    QCOMPARE(restored.textBold, std::optional<bool>(true)); QCOMPARE(restored.textItalic, std::optional<bool>(false));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo());
    auto plain = session.selectedRectangle()->content; plain.textBold = false; plain.textItalic = true;
    QVERIFY(session.editShapeContent(session.selectedRectangle()->id, plain, session.stateId())); QCOMPARE(session.undoCount(), undo + 2);
    plain.textBold.reset(); plain.textItalic.reset(); QVERIFY(session.editShapeContent(session.selectedRectangle()->id, plain, session.stateId()));
    QCOMPARE(serializeDocument(session.document()), before);
    for (const auto &key : {QString("textBold"), QString("textItalic")}) {
      auto payload = before; auto annotations = payload.value("annotations").toArray(); auto object = annotations[0].toObject();
      for (const auto &bad : {QJsonValue("true"), QJsonValue(1), QJsonValue(QJsonValue::Null), QJsonValue(QJsonObject{})}) {
        object.insert(key, bad); annotations[0] = object; payload.insert("annotations", annotations); QVERIFY(!parseDocument(payload).document);
      }
      object.insert("type", "ellipse"); annotations[0] = object; payload.insert("annotations", annotations);
      const auto opaque = parseDocument(payload); QVERIFY(opaque.document);
      QCOMPARE(serializeDocument(*opaque.document).value("annotations").toArray()[0].toObject().value(key), QJsonValue(QJsonObject{}));
    }
  }
  void explicitFontStylesSurviveCreation() {
    for (const auto &type : {shape::text, shape::speechBalloon}) {
      for (bool bold : {false, true}) for (bool italic : {false, true}) {
        EditorSession session(document()); ShapeContent content; content.type = type; content.text = "Created"; content.textBold = bold; content.textItalic = italic;
        QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
        QCOMPARE(session.selectedRectangle()->content.textBold, std::optional<bool>(bold)); QCOMPARE(session.selectedRectangle()->content.textItalic, std::optional<bool>(italic));
        const auto object = serializeDocument(session.document()).value("annotations").toArray()[0].toObject();
        QCOMPARE(object.value("textBold"), QJsonValue(bold)); QCOMPARE(object.value("textItalic"), QJsonValue(italic));
      }
    }
  }
  void textFontStylesAffectRenderedGlyphs_data() { textFontStylesCommitCancelAndReload_data(); }
  void textFontStylesAffectRenderedGlyphs() {
    QFETCH(QString, type); auto doc = document(); RectangleAnnotation annotation;
    annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {500, 300}; annotation.content.type = type;
    annotation.content.text = "Hamburgefon"; annotation.content.fontSize = 40; annotation.style.fillColor = 0xFFFFFFFF;
    annotation.style.strokeColor = type == shape::text ? 0xFF000000 : 0xFFFFFFFF; doc.annotations.push_back(annotation);
    const auto source = solidImage(800, 600, 0xFFFFFFFF); const auto legacy = renderDocument(source, doc); QVERIFY(legacy.image);
    auto &content = std::get<RectangleAnnotation>(doc.annotations[0]).content;
    content.textBold = false; content.textItalic = false; const auto plain = renderDocument(source, doc); QVERIFY(plain.image); QCOMPARE(plain.image->pixels, legacy.image->pixels);
    content.textBold = true; const auto bold = renderDocument(source, doc); QVERIFY(bold.image); QVERIFY(bold.image->pixels != plain.image->pixels);
    const auto ink = [](const auto &pixels) { return std::count_if(pixels.begin(), pixels.end(), [](Argb p) { return p != 0xFFFFFFFF; }); };
    QVERIFY(ink(bold.image->pixels) > ink(plain.image->pixels));
    content.textBold = false; content.textItalic = true; const auto italic = renderDocument(source, doc); QVERIFY(italic.image); QVERIFY(italic.image->pixels != plain.image->pixels);
    content.textBold = true; const auto both = renderDocument(source, doc); QVERIFY(both.image); QVERIFY(both.image->pixels != bold.image->pixels); QVERIFY(both.image->pixels != italic.image->pixels);
  }
  void textAlignmentDialogCommitsOnceAndKeepsOldDocuments_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void textAlignmentDialogCommitsOnceAndKeepsOldDocuments() {
    QFETCH(QString, type); auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-25T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = type; content.text = "XY";
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto before = serializeDocument(session.document()); const auto undo = session.undoCount();
    QVERIFY(!before.value("annotations").toArray()[0].toObject().contains("textAlignment"));
    const auto source = image(); EditorCanvas canvas(session, source);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    auto *alignment = dialog->findChild<QComboBox *>("annotationTextAlignment"); QVERIFY(alignment); QCOMPARE(alignment->currentData().toString(), QString{});
    alignment->setCurrentIndex(alignment->findData("right")); dialog->reject(); QTRY_VERIFY(!canvas.interactionActive());
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    alignment = dialog->findChild<QComboBox *>("annotationTextAlignment"); alignment->setCurrentIndex(alignment->findData("center")); dialog->accept();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.selectedRectangle()->content.textAlignment, QString("center")); QCOMPARE(session.undoCount(), undo + 1);
    const auto loaded = parseDocument(serializeDocument(session.document())); QVERIFY(loaded.document);
    QCOMPARE(std::get<RectangleAnnotation>(loaded.document->annotations[0]).content.textAlignment, QString("center"));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(session.redo());
    QCOMPARE(session.selectedRectangle()->content.textAlignment, QString("center"));
    const auto state = session.stateId(); auto invalid = session.selectedRectangle()->content; invalid.textAlignment = "justify";
    QVERIFY(!session.editShapeContent(session.selectedRectangle()->id, invalid, state)); QCOMPARE(session.stateId(), state); QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid"));
    auto payload = serializeDocument(session.document()); auto annotations = payload.value("annotations").toArray(); auto object = annotations[0].toObject();
    object.insert("textAlignment", true); annotations[0] = object; payload.insert("annotations", annotations); QVERIFY(!parseDocument(payload).document);
    object.insert("textAlignment", "future-direction"); annotations[0] = object; payload.insert("annotations", annotations); QVERIFY(!parseDocument(payload).document);
    // Other shape families must continue to preserve this as an opaque field.
    object.insert("type", "ellipse"); annotations[0] = object; payload.insert("annotations", annotations); const auto other = parseDocument(payload); QVERIFY(other.document);
    QCOMPARE(serializeDocument(*other.document).value("annotations").toArray()[0].toObject().value("textAlignment"), QJsonValue("future-direction"));
  }
  void invalidAlignmentCannotEnterThroughCreation() {
    for (const auto &type : {shape::text, shape::speechBalloon}) {
      EditorSession session(document()); const auto before = serializeDocument(session.document());
      ShapeContent content; content.type = type; content.text = "Valid"; content.textAlignment = "future-direction";
      QVERIFY(!session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
      QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
    }
  }
  void renderedTextAlignmentMovesGlyphsWithoutChangingVerticalPlacement_data() {
    QTest::addColumn<QString>("type"); QTest::newRow("text") << shape::text; QTest::newRow("balloon") << shape::speechBalloon;
  }
  void renderedTextAlignmentMovesGlyphsWithoutChangingVerticalPlacement() {
    QFETCH(QString, type); AnnotationDocument doc = document(); RectangleAnnotation annotation;
    annotation.id = QUuid::createUuid(); annotation.start = {100, 100}; annotation.end = {300, 200}; annotation.content.type = type;
    annotation.content.text = "X"; annotation.content.fontSize = 24; annotation.style.fillColor = 0xFFFFFFFF;
    annotation.style.strokeColor = type == shape::text ? 0xFF000000 : 0xFFFFFFFF;
    doc.annotations.push_back(annotation); QList<QRect> ink; std::vector<Argb> legacy;
    for (const QString &alignment : {QString{}, QString("left"), QString("center"), QString("right")}) {
      std::get<RectangleAnnotation>(doc.annotations[0]).content.textAlignment = alignment;
      const auto rendered = renderDocument(solidImage(800, 600, 0xFFFFFFFF), doc); QVERIFY(rendered.image);
      if (alignment.isEmpty()) { legacy = rendered.image->pixels; continue; }
      QRect bounds;
      for (int y = 0; y < 600; ++y) for (int x = 0; x < 800; ++x)
        if (rendered.image->at(x, y) != 0xFFFFFFFF) bounds = bounds.united(QRect(x, y, 1, 1));
      QVERIFY(!bounds.isEmpty()); ink.append(bounds);
      if ((type == shape::text && alignment == "left") || (type == shape::speechBalloon && alignment == "center")) QCOMPARE(rendered.image->pixels, legacy);
    }
    QCOMPARE(ink.size(), 3); QVERIFY(ink[0].left() < ink[1].left()); QVERIFY(ink[1].left() < ink[2].left());
    QCOMPARE(ink[0].top(), ink[1].top()); QCOMPARE(ink[1].top(), ink[2].top()); QCOMPARE(ink[0].size(), ink[2].size());
    QVERIFY(ink[0].left() >= 100); QVERIFY(ink[2].right() < 300);
  }
  void balloonTailCommitCancelHistoryAndPersistence() {
    auto initial = document(); initial.createdAt = initial.modifiedAt = "2026-10-25T00:00:00Z";
    QBuffer encoded(&initial.sourceImagePng); QVERIFY(encoded.open(QIODevice::WriteOnly)); QVERIFY(image().save(&encoded, "PNG")); encoded.close();
    EditorSession session(initial); ShapeContent content; content.type = shape::speechBalloon; content.text = "Original\ntext"; content.points = {{0.2, 1.6}};
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto original = serializeDocument(session.document()); const auto state = session.stateId(); const auto undo = session.undoCount();
    const auto source = image(); EditorCanvas canvas(session, source); canvas.resize(640, 480); canvas.show(); QCoreApplication::processEvents();
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog); QVERIFY(canvas.interactionActive());
    auto *text = dialog->findChild<QPlainTextEdit *>("balloonText"); QVERIFY(text); text->setPlainText("Cancelled"); dialog->reject();
    QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.stateId(), state); QCOMPARE(serializeDocument(session.document()), original);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    text = dialog->findChild<QPlainTextEdit *>("balloonText"); text->setPlainText("Changed\nmultiline");
    dialog->findChild<QLineEdit *>("balloonTailX")->setText("0.75"); dialog->findChild<QLineEdit *>("balloonTailY")->setText("-0.5");
    dialog->accept(); QTRY_VERIFY(!canvas.interactionActive()); QCOMPARE(session.undoCount(), undo + 1);
    const auto edited = session.selectedRectangle()->content; QCOMPARE(edited.text, QString("Changed\nmultiline")); QCOMPARE(edited.fontSize, 0.0);
    QCOMPARE(edited.points.size(), size_t(1)); QCOMPARE(edited.points[0].x, 0.75); QCOMPARE(edited.points[0].y, -0.5);
    const auto parsed = parseDocument(serializeDocument(session.document())); QVERIFY(parsed.document);
    const auto &loaded = std::get<RectangleAnnotation>(parsed.document->annotations.front()); QCOMPARE(loaded.content.points[0].x, 0.75); QCOMPARE(loaded.content.points[0].y, -0.5);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), original); QVERIFY(session.redo());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->findChild<QCheckBox *>("balloonTailEnabled")->setChecked(false); dialog->accept(); QTRY_VERIFY(!canvas.interactionActive());
    QVERIFY(session.selectedRectangle()->content.points.empty());
    const auto withoutTail = parseDocument(serializeDocument(session.document())); QVERIFY(withoutTail.document); QVERIFY(std::get<RectangleAnnotation>(withoutTail.document->annotations.front()).content.points.empty());
    const auto noTail = renderDocument(solidImage(800, 600, 0xFFFFFFFF), session.document()); QVERIFY(noTail.image);
    QVERIFY(session.undo()); const auto withTail = renderDocument(solidImage(800, 600, 0xFFFFFFFF), session.document()); QVERIFY(withTail.image);
    QVERIFY(withTail.image->pixels != noTail.image->pixels);
  }
  void balloonDialogRejectsInvalidTailAndStaleDocuments() {
    EditorSession session(document()); ShapeContent content; content.type = shape::speechBalloon; content.text = "Text";
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); EditorCanvas canvas(session, source); const auto state = session.stateId(); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    QVERIFY(canvas.editShapeContent(&canvas)); auto *dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    dialog->findChild<QCheckBox *>("balloonTailEnabled")->setChecked(true);
    for (const auto &input : {QString(""), QString("nan"), QString("inf"), QString("1e309"), QString("1e308")}) {
      dialog->findChild<QLineEdit *>("balloonTailX")->setText(input); dialog->findChild<QLineEdit *>("balloonTailY")->setText("1.5"); dialog->accept();
      QVERIFY(canvas.interactionActive()); QCOMPARE(session.stateId(), state); QVERIFY(dialog->findChild<QLabel *>("balloonContentError")->text().startsWith("balloon-tail-invalid"));
    }
    dialog->findChild<QLineEdit *>("balloonTailX")->setText("0.4"); dialog->findChild<QLineEdit *>("balloonTailY")->setText("1.5");
    EditorSession replacement(document()); canvas.resetDocument(replacement, image()); dialog->accept();
    QVERIFY(!rejected.isEmpty()); QCOMPARE(rejected.last()[0].toString(), QString("annotation-edit-stale")); QVERIFY(replacement.document().annotations.empty());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    canvas.resetDocument(session, image()); QVERIFY(canvas.editShapeContent(&canvas)); dialog = canvas.findChild<TextContentDialog *>(); QVERIFY(dialog);
    session.select({}); dialog->accept(); QVERIFY(!rejected.isEmpty()); QCOMPARE(session.stateId(), state);
  }
  void balloonDialogOwnerAndDeferredDeletionKeepNewOperationSafe() {
    EditorSession session(document()); ShapeContent content; content.type = shape::speechBalloon; content.text = "Safe"; content.points = {{0.2, 1.6}};
    QVERIFY(session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()));
    const auto source = image(); QWidget parent;
    auto canvas = std::make_unique<EditorCanvas>(session, source);
    QVERIFY(canvas->editShapeContent(&parent)); auto *first = parent.findChild<TextContentDialog *>(); QVERIFY(first); first->reject();
    QVERIFY(canvas->editShapeContent(&parent)); auto dialogs = parent.findChildren<TextContentDialog *>(); auto *second = dialogs.last(); QVERIFY(second != first);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); QVERIFY(canvas->interactionActive());
    const auto before = serializeDocument(session.document()); canvas.reset();
    second->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Late result"); second->accept();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QCoreApplication::processEvents(); QCOMPARE(serializeDocument(session.document()), before);
    EditorCanvas live(session, source); auto external = std::make_unique<QWidget>();
    QVERIFY(live.editShapeContent(external.get())); external.reset(); QTRY_VERIFY(!live.interactionActive());
  }
  void balloonTailOnlyEditIsOneHistoryOperation() {
    EditorSession session(document()); ShapeContent content; content.type = shape::speechBalloon; content.text = "Unchanged"; content.points = {{0.2, 1.6}};
    const auto id = session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    const auto undo = session.undoCount(); content.points = {{0.75, -0.5}};
    QVERIFY(session.editShapeContent(*id, content, session.stateId())); QCOMPARE(session.undoCount(), undo + 1);
    QCOMPARE(session.selectedRectangle()->content.points[0].x, 0.75);
    QVERIFY(session.undo()); QCOMPARE(session.selectedRectangle()->content.points[0].x, 0.2);
    QVERIFY(session.redo()); QCOMPARE(session.selectedRectangle()->content.points[0].x, 0.75);
  }
  void invalidBalloonContentNeverChangesHistory() {
    EditorSession session(document()); ShapeContent content; content.type = shape::speechBalloon; content.text = "Valid"; content.points = {{0.2, 1.6}};
    const auto id = session.createShape({{100, 100}, {300, 200}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    const auto before = serializeDocument(session.document()); const auto state = session.stateId(); const auto undo = session.undoCount();
    for (const auto badPoint : {PointF{std::nan(""), 1}, PointF{0, std::numeric_limits<double>::infinity()}, PointF{1e308, 1}}) {
      auto invalid = content; invalid.points = {badPoint}; QVERIFY(!session.editShapeContent(*id, invalid, state));
      QCOMPARE(session.editDiagnostic(), QString("annotation-style-invalid")); QCOMPARE(session.stateId(), state);
    }
    auto invalid = content; invalid.points.resize(100001); QVERIFY(!session.editShapeContent(*id, invalid, state));
    invalid = content; invalid.text = QString(100001, 'x'); QVERIFY(!session.editShapeContent(*id, invalid, state));
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), undo);
  }
  void tinyBalloonTailsRenderInEveryDirection() {
    for (const auto tip : {PointF{0.5, -1}, PointF{2, 0.5}, PointF{0.5, 2}, PointF{-1, 0.5}}) {
      EditorSession session(document()); ShapeContent content; content.type = shape::speechBalloon; content.text = "x"; content.points = {tip};
      QVERIFY(session.createShape({{10, 10}, {18, 18}, {}}, content, session.stateId(), session.selection()));
      QVERIFY(renderDocument(solidImage(800, 600, 0xFFFFFFFF), session.document()).image);
    }
  }
  void initTestCase() {
    QVERIFY(dialogPreferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dialogPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dialogPreferences.path());
  }
  void keyboardTransformsUseImagePixelsAndKeepRepeatHistory_data() {
    QTest::addColumn<int>("key"); QTest::addColumn<int>("modifiers"); QTest::addColumn<double>("dx"); QTest::addColumn<double>("dy"); QTest::addColumn<double>("angle");
    for (bool resize : {false, true}) for (bool shift : {false, true}) for (int direction = 0; direction < 4; ++direction) {
      const int keys[] = {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down}; const double step = shift ? 10 : 1;
      QTest::newRow(qPrintable(QString("%1-%2-%3").arg(resize ? "resize" : "move").arg(shift ? 10 : 1).arg(direction)))
          << keys[direction] << int((resize ? Qt::AltModifier : Qt::NoModifier) | (shift ? Qt::ShiftModifier : Qt::NoModifier))
          << (direction == 0 ? -step : direction == 1 ? step : 0) << (direction == 2 ? -step : direction == 3 ? step : 0) << 0.0;
    }
    QTest::newRow("clockwise") << int(Qt::Key_R) << int(Qt::NoModifier) << 0.0 << 0.0 << 90.0;
    QTest::newRow("counterclockwise") << int(Qt::Key_R) << int(Qt::ShiftModifier) << 0.0 << 0.0 << -90.0;
  }
  void keyboardTransformsUseImagePixelsAndKeepRepeatHistory() {
    QFETCH(int, key); QFETCH(int, modifiers); QFETCH(double, dx); QFETCH(double, dy); QFETCH(double, angle);
    const auto source = image(); EditorSession prepared(document()); const auto a = *prepared.createRectangle({100, 100}, {160, 180}), b = *prepared.createRectangle({300, 200}, {380, 270}); QVERIFY(prepared.createRectangle({500, 300}, {560, 360}));
    auto d = prepared.document(); const auto id = QUuid::createUuid(); d.annotations.insert(d.annotations.begin() + 1, UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"payload", "keep"}}}); d.embeddedImages = {{"asset", "keep pixels"}}; d.extra = {{"vendor", "root"}};
    std::get<RectangleAnnotation>(d.annotations[0]).style.rotationDegrees = 37.25; std::get<RectangleAnnotation>(d.annotations[2]).style.rotationDegrees = 90;
    EditorSession session(d); session.select({a}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select({a, b, id}); const auto selection = session.selection(); const auto defaults = session.toolStyle();
    EditorCanvas canvas(session, source); canvas.resize(450, 350); canvas.show(); canvas.activateWindow(); canvas.setFocus(); QTRY_VERIFY(canvas.isActiveWindow()); canvas.zoomToFit(); const auto zoom = canvas.viewState().zoom(); const auto offset = canvas.viewState().offset();
    QVERIFY(canvas.accessibleDescription().contains("Shift+arrows")); QVERIFY(canvas.accessibleDescription().contains("Held transform keys repeat")); QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    auto expected = d; const auto advance = [&] {
      for (int index : {0, 2}) { auto &r = std::get<RectangleAnnotation>(expected.annotations[index]); if (angle) { r.style.rotationDegrees += angle; if (r.style.rotationDegrees >= 360) r.style.rotationDegrees -= 360; if (r.style.rotationDegrees < 0) r.style.rotationDegrees += 360; }
        else { if (!(modifiers & Qt::AltModifier)) { r.start.x += dx; r.start.y += dy; } r.end.x += dx; r.end.y += dy; } }
    };
    QTest::keyClick(&canvas, Qt::Key(key), Qt::KeyboardModifiers(modifiers)); advance(); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.redoCount(), 0);
    const auto first = serializeDocument(expected); QKeyEvent repeat(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers), {}, true); QApplication::sendEvent(&canvas, &repeat); advance();
    QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.undoCount(), 2); QCOMPARE(session.selection(), selection); QCOMPARE(rejected.count(), 0); QVERIFY(session.dirty());
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), first); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(d)); QCOMPARE(session.selection(), selection); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), serializeDocument(expected)); QCOMPARE(session.toolStyle().strokeWidth, defaults.strokeWidth); QCOMPARE(session.toolStyle().rotationDegrees, defaults.rotationDegrees); QCOMPARE(canvas.viewState().zoom(), zoom); QCOMPARE(canvas.viewState().offset(), offset); QCOMPARE(source, image());
  }
  void keyboardTransformsGuardPendingStateSelectionGenerationAndOwner_data() {
    QTest::addColumn<int>("key"); QTest::addColumn<int>("modifiers"); QTest::addColumn<QString>("mode");
    for (int transform = 0; transform < 3; ++transform) for (const auto *mode : {"apply", "defaults", "stale-state", "stale-selection", "replacement", "disabled", "late-disabled", "hidden", "locked", "opaque-lock", "unknown-only", "begin-destroy", "reject-destroy", "finish-destroy"})
      QTest::newRow(qPrintable(QString("%1-%2").arg(transform).arg(mode))) << int(transform == 2 ? Qt::Key_R : Qt::Key_Right) << int(transform == 1 ? Qt::AltModifier : Qt::NoModifier) << QString(mode);
  }
  void keyboardTransformsGuardPendingStateSelectionGenerationAndOwner() {
    QFETCH(int, key); QFETCH(int, modifiers); QFETCH(QString, mode); const auto source = image(); EditorSession prepared(document()); const auto a = *prepared.createRectangle({100, 100}, {160, 160}), b = *prepared.createRectangle({300, 100}, {360, 160}); auto d = prepared.document();
    if (mode == "hidden") std::get<RectangleAnnotation>(d.annotations[1]).visible = false; if (mode == "locked") std::get<RectangleAnnotation>(d.annotations[1]).extra = {{"locked", true}};
    if (mode == "opaque-lock" || mode == "unknown-only") d.annotations[1] = UnsupportedAnnotation{b, {{"id", b.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"locked", mode == "opaque-lock" ? QJsonValue("future-lock") : QJsonValue(false)}}};
    EditorSession session(d), replacement(d); session.select({a}); QVERIFY(session.moveSelection(1, 0)); QVERIFY(session.undo()); session.select(mode == "unknown-only" ? QList<QUuid>{b} : QList<QUuid>{a, b}); replacement.select({a, b});
    auto expected = serializeDocument(d); auto selection = session.selection(); int undo = 0, redo = 1; bool intervened = false; int notifications = 0;
    auto canvas = std::make_unique<EditorCanvas>(session, source); QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected); if (mode == "disabled") canvas->setEnabled(false); if (mode == "reject-destroy") connect(canvas.get(), &EditorCanvas::editRejected, [&] { canvas.reset(); });
    connect(canvas.get(), &EditorCanvas::changed, [&] {
      ++notifications; if (intervened) { if (canvas && !canvas->interactionActive() && mode == "finish-destroy") canvas.reset(); return; }
      if (!canvas->interactionActive()) return; intervened = true; QKeyEvent nested(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier); QApplication::sendEvent(canvas.get(), &nested); QCOMPARE(serializeDocument(session.document()), expected);
      if (mode == "begin-destroy") { canvas.reset(); return; } if (mode == "replacement") canvas->resetDocument(replacement, source); if (mode == "late-disabled") canvas->setEnabled(false);
      if (mode == "stale-selection" || mode == "reject-destroy") { session.select({b}); selection = session.selection(); }
      if (mode == "stale-state") { QVERIFY(session.moveSelection(2, 0)); expected = serializeDocument(session.document()); undo = 1; redo = 0; }
      if (mode == "defaults") { auto style = session.toolStyle(); style.strokeWidth = 9; QVERIFY(session.setCreationStyle(style)); }
    });
    QKeyEvent event(QEvent::KeyPress, key, Qt::KeyboardModifiers(modifiers)); QApplication::sendEvent(canvas.get(), &event); QCOMPARE(intervened, mode != "disabled");
    const bool applied = mode == "apply" || mode == "defaults" || mode == "finish-destroy";
    if (applied) { auto changed = d; for (auto &annotation : changed.annotations) { auto &r = std::get<RectangleAnnotation>(annotation); if (key == Qt::Key_R) r.style.rotationDegrees = 90; else { if (!(modifiers & Qt::AltModifier)) ++r.start.x; ++r.end.x; } }
      QCOMPARE(serializeDocument(session.document()), serializeDocument(changed)); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), 1); QVERIFY(session.undo()); }
    QCOMPARE(serializeDocument(session.document()), expected); QCOMPARE(session.selection(), selection); QCOMPARE(session.undoCount(), undo); QCOMPARE(session.redoCount(), redo); QCOMPARE(session.dirty(), mode == "stale-state"); QCOMPARE(session.toolStyle().strokeWidth, mode == "defaults" ? 9.0 : 4.0); QCOMPARE(replacement.undoCount(), 0); QCOMPARE(serializeDocument(replacement.document()), serializeDocument(d));
    const bool stale = mode.startsWith("stale-") || mode == "replacement" || mode == "late-disabled" || mode == "reject-destroy", protectedSelection = mode == "hidden" || mode.contains("lock");
    QCOMPARE(rejected.count(), stale || protectedSelection ? 1 : 0); if (stale || protectedSelection) QCOMPARE(rejected[0][0].toString(), QString(stale ? "annotation-edit-stale" : "annotation-selection-protected"));
    if (mode == "disabled") QCOMPARE(notifications, 0); if (mode == "begin-destroy" || mode == "reject-destroy") QCOMPARE(notifications, 1); if (canvas) QVERIFY(!canvas->interactionActive()); QCOMPARE(source, image());
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
    QVERIFY(!canvas); QCOMPARE(rejected, 1); QCOMPARE(changed, 1); QVERIFY(!session.dirty()); QCOMPARE(session.undoCount(), 1);
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

  void zoomAfterThePointerLeavesReportsNoPixel() {
    // A large, zoomed image puts document pixels under every view position,
    // including off-viewport ones, so a stale pointer would report a pixel.
    AnnotationDocument big; big.canvasWidth = 400; big.canvasHeight = 400;
    EditorSession session(big);
    QImage source(400, 400, QImage::Format_ARGB32); source.fill(Qt::white);
    EditorCanvas canvas(session, source);
    canvas.resize(120, 120); canvas.show(); QTest::qWait(10);
    for (int n = 0; n < 4; ++n) canvas.zoomIn();
    std::optional<QPoint> last = QPoint(-2, -2);
    connect(&canvas, &EditorCanvas::pointerPixelChanged, [&](std::optional<QPoint> pixel) { last = pixel; });
    hoverViewPoint(canvas.viewport(), QPoint(40, 40));
    QVERIFY(last && last->x() >= 0);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(canvas.viewport(), &leave);
    QCOMPARE(last, std::optional<QPoint>{});
    canvas.zoomIn(); canvas.zoomOut(); canvas.zoomIn();
    QCOMPARE(last, std::optional<QPoint>{});
  }

  void pointerPixelReportsDocumentPixelOnlyWhenItChanges() {
    AnnotationDocument small; small.canvasWidth = 4; small.canvasHeight = 4;
    EditorSession session(small);
    QImage source(4, 4, QImage::Format_ARGB32);
    source.fill(Qt::white); source.setPixel(2, 1, qRgb(255, 0, 0));
    EditorCanvas canvas(session, source);
    canvas.resize(320, 240); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    QCOMPARE(canvas.viewState().zoom(), 1.0);
    QList<QPoint> reported;
    connect(&canvas, &EditorCanvas::pointerPixelChanged, [&] (std::optional<QPoint> pixel) {
      reported.push_back(pixel ? *pixel : QPoint(-1, -1));
    });
    // The pointer reports the pixel that contains it, so the red pixel at (2,1)
    // is aimed at through the live viewport at both scales.
    const auto atZoom1 = viewPointInPixel(canvas.viewState(), QPoint(2, 1));
    QVERIFY(atZoom1.second);
    hoverViewPoint(canvas.viewport(), atZoom1.first);
    QCOMPARE(reported, QList<QPoint>{QPoint(2, 1)});
    QTest::mouseMove(canvas.viewport(), atZoom1.first);
    QCOMPARE(reported.size(), 1);
    // Zoom steps are 1.25, so three in reach the closest scale to 2x.
    for (int n = 0; n < 3; ++n) canvas.zoomIn();
    QVERIFY(canvas.viewState().zoom() > 1.9);
    const auto atZoom2 = viewPointInPixel(canvas.viewState(), QPoint(2, 1));
    QVERIFY(atZoom2.second);
    QVERIFY(atZoom2.first != atZoom1.first);
    QTest::mouseMove(canvas.viewport(), atZoom2.first);
    QCOMPARE(reported.last(), QPoint(2, 1));
    QTest::mouseMove(canvas.viewport(), QPoint(120, 100));  // inside the canvas, outside the image
    QCOMPARE(reported.last(), QPoint(-1, -1));
    QTest::mouseMove(canvas.viewport(), atZoom2.first);
    QCOMPARE(reported.last(), QPoint(2, 1));
    QEvent leave(QEvent::Leave); QApplication::sendEvent(canvas.viewport(), &leave);
    QCOMPARE(reported.last(), QPoint(-1, -1));
  }
};

QTEST_MAIN(EditorCanvasTest)
#include "EditorCanvasTest.moc"
