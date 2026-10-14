#include "AnnotationObjectNavigator.h"
#include "EditorCanvas.h"
#include <QAccessible>
#include <QApplication>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QHBoxLayout>
#include <QTest>
#include <memory>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
AnnotationDocument document() {
  AnnotationDocument d; d.canvasWidth = 800; d.canvasHeight = 600;
  for (int i = 0; i < 4; ++i) {
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {double(i * 100), 100}; r.end = {double(i * 100 + 50), 150};
    if (i == 2) r.visible = false; if (i == 3) r.extra = {{"locked", true}};
    d.annotations.push_back(r);
  }
  const auto id = QUuid::createUuid(); d.annotations.push_back(UnsupportedAnnotation{id,
      {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "<img src='file:///private'>\n\u202Efuture"}, {"vendor", "keep"}}});
  const auto opaque = QUuid::createUuid(); d.annotations.push_back(UnsupportedAnnotation{opaque,
      {{"id", opaque.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"visible", "opaque"}, {"locked", "opaque"}}});
  return d;
}
QImage image() { QImage result(800, 600, QImage::Format_ARGB32); result.fill(Qt::white); return result; }
void choose(AnnotationObjectNavigator &view, int row, QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::ClearAndSelect) {
  view.selectionModel()->select(view.model()->index(row, 0), flags);
}
}
class AnnotationObjectNavigatorTest final : public QObject {
  Q_OBJECT
private slots:
  void nativeKeyboardSelectionPreservesDocumentHistoryAndLabels() {
    const QImage source = image(); const auto d = document(); EditorSession session(d); QWidget host; auto *layout = new QHBoxLayout(&host);
    EditorCanvas canvas(session, source, &host); AnnotationObjectNavigator view(canvas, &host); layout->addWidget(&canvas); layout->addWidget(&view);
    layout->setStretch(0, 1); layout->setStretch(1, 1);
    host.resize(900, 400); host.show(); host.activateWindow(); view.setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), &view); QCOMPARE(view.model()->rowCount(), 6);
    const auto before = serializeDocument(session.document()); auto *accessible = QAccessible::queryAccessibleInterface(&view); QVERIFY(accessible);
    QCOMPARE(accessible->text(QAccessible::Name), QString("Annotation objects")); QVERIFY(view.accessibleDescription().contains("Control+Space"));
    QVERIFY(view.model()->index(2, 0).data().toString().contains("hidden")); QVERIFY(view.model()->index(3, 0).data().toString().contains("locked"));
    const auto unsafe = view.model()->index(4, 0).data().toString(); QVERIFY(!unsafe.contains('<')); QVERIFY(!unsafe.contains('\n')); QVERIFY(!unsafe.contains(QChar(0x202E)));
    const auto opaque = view.model()->index(5, 0).data().toString(); QVERIFY(opaque.contains("visibility unknown")); QVERIFY(opaque.contains("protection unknown")); QVERIFY(opaque.contains("protected"));
    QTest::keyClick(&view, Qt::Key_Down); QCOMPARE(session.selection(), QSet<QUuid>{annotationId(d.annotations[1])});
    QTest::keyClick(&view, Qt::Key_Down, Qt::ShiftModifier); QCOMPARE(session.selection().size(), 2); QVERIFY(!session.selectionEditable());
    QTest::keyClick(&view, Qt::Key_Space, Qt::ControlModifier); QCOMPARE(session.selection(), QSet<QUuid>{annotationId(d.annotations[1])});
    QCOMPARE(view.currentIndex().row(), 2);
    QVERIFY(view.model()->index(1, 0).data().toString().startsWith("[selected]"));
    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier); QCOMPARE(session.selection().size(), 6);
    QTest::keyClick(&view, Qt::Key_Escape); QVERIFY(session.selection().isEmpty()); QCOMPARE(serializeDocument(session.document()), before);
    QCOMPARE(session.undoCount(), 0); QCOMPARE(session.redoCount(), 0); QVERIFY(!session.dirty());
    choose(view, 0); QTest::keyClick(&view, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), &canvas);
    QCOMPARE(session.selection(), QSet<QUuid>{annotationId(d.annotations[0])});
    if (qEnvironmentVariableIsSet("XERAHS_ANNOTATION_OBJECTS_QA")) QVERIFY(view.grab().save(qEnvironmentVariable("XERAHS_ANNOTATION_OBJECTS_QA")));
  }
  void pointerHistoryOrderAndSourceReplacementFollowIdentities() {
    const QImage source = image(); auto d = document(); EditorSession session(d); EditorCanvas canvas(session, source); AnnotationObjectNavigator view(canvas);
    canvas.resize(520, 440); canvas.show(); view.show(); canvas.zoomToFit();
    const auto id = annotationId(d.annotations[1]);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView({125, 125}).toPoint());
    QCOMPARE(session.selection(), QSet<QUuid>{id}); QCOMPARE(view.selectionModel()->selectedRows().size(), 1);
    QCOMPARE(view.selectionModel()->selectedRows()[0].data(Qt::UserRole).toUuid(), id);
    QVERIFY(session.reorderSelection(EditorSession::Order::Front)); view.refresh(); QCOMPARE(view.selectionModel()->selectedRows()[0].row(), 5);
    QVERIFY(session.deleteSelection()); view.refresh(); QCOMPARE(view.model()->rowCount(), 5); QVERIFY(view.selectionModel()->selectedRows().isEmpty());
    QVERIFY(session.undo()); view.refresh(); QCOMPARE(view.model()->rowCount(), 6); QCOMPARE(view.selectionModel()->selectedRows()[0].data(Qt::UserRole).toUuid(), id);
    QVERIFY(session.undo()); view.refresh(); QCOMPARE(view.selectionModel()->selectedRows()[0].row(), 1); QVERIFY(session.redo()); view.refresh();
    EditorSession replacement(document()); canvas.resetDocument(replacement, source); QCOMPARE(view.model()->rowCount(), 6); QVERIFY(view.selectionModel()->selectedRows().isEmpty());
    choose(view, 0); QCOMPARE(replacement.selection(), QSet<QUuid>{annotationId(replacement.document().annotations[0])});
    QCOMPARE(session.selection(), QSet<QUuid>{id}); QCOMPARE(replacement.undoCount(), 0); QVERIFY(!replacement.dirty());
  }
  void busyStaleProtectedAndDestroyedOwnersRemainSafe() {
    const QImage source = image(); EditorSession session(document()); auto canvas = std::make_unique<EditorCanvas>(session, source);
    bool ready = true; auto view = std::make_unique<AnnotationObjectNavigator>(*canvas, nullptr, [&] { return ready; });
    choose(*view, 0); const auto selected = session.selection(); ready = false; view->refresh(); QVERIFY(!view->isEnabled()); choose(*view, 1); QCOMPARE(session.selection(), selected);
    ready = true; view->refresh(); canvas->setEnabled(false); view->refresh(); choose(*view, 1); QCOMPARE(session.selection(), selected); canvas->setEnabled(true); view->refresh();
    QSignalSpy rejected(canvas.get(), &EditorCanvas::editRejected); QVERIFY(session.moveSelection(1, 0));
    const auto intervening = serializeDocument(session.document()); choose(*view, 1); QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-edit-stale"));
    QCOMPARE(serializeDocument(session.document()), intervening); QCOMPARE(session.selection(), selected); QCOMPARE(session.undoCount(), 1);
    choose(*view, 2); QVERIFY(!session.deleteSelection()); QCOMPARE(session.editDiagnostic(), QString("annotation-selection-protected")); QCOMPARE(serializeDocument(session.document()), intervening);
    canvas.reset(); QVERIFY(!view->isEnabled()); choose(*view, 0); QCOMPARE(serializeDocument(session.document()), intervening);
  }
  void modelNotificationCanDestroyItsOwner_data() {
    QTest::addColumn<int>("mode"); QTest::newRow("refresh") << 0; QTest::newRow("focus") << 1; QTest::newRow("canvas") << 2; QTest::newRow("selection-canvas") << 3;
  }
  void modelNotificationCanDestroyItsOwner() {
    QFETCH(int, mode); const QImage source = image(); EditorSession session(document()); auto canvas = std::make_unique<EditorCanvas>(session, source);
    auto view = std::make_unique<AnnotationObjectNavigator>(*canvas);
    if (mode == 3) connect(view->model(), &QAbstractItemModel::dataChanged, [&] { canvas.reset(); });
    else connect(view->model(), &QAbstractItemModel::modelAboutToBeReset, [&] { if (mode == 2) canvas.reset(); else view.reset(); });
    QVERIFY(session.createRectangle({500, 200}, {550, 250})); if (mode == 1) view->focusObjects(); else view->refresh();
    if (mode >= 2) { QVERIFY(!canvas); QVERIFY(view && !view->isEnabled()); } else QVERIFY(!view);
    QCOMPARE(session.undoCount(), 1);
  }
};
QTEST_MAIN(AnnotationObjectNavigatorTest)
#include "AnnotationObjectNavigatorTest.moc"
