// Native input tests exercise the actual canvas and shared session model.
#include "EditorCanvas.h"
#include "NativeAnnotationTools.h"
#include "RectanglePropertiesDialog.h"

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
