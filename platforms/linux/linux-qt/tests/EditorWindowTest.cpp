// Actual native actions, modal dialogs, worker preparation, undo and persisted
// PNG/sidecar reopening. All images and files are temporary fixtures.
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"
#include "CanvasRotationDialog.h"
#include "ImageComparisonDialog.h"
#include "NewImageDialog.h"
#include "RecentImageMenu.h"
#include "NativeEditorWindowState.h"
#include "NativeEditorAppearance.h"
#include "RectanglePropertiesDialog.h"
#include "AnnotationObjectNavigator.h"
#include "SelectionRotationDialog.h"
#include <QPlainTextEdit>
#include <QItemSelectionModel>

#include <QAction>
#include <QColorSpace>
#include <QClipboard>
#include <QColorDialog>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QJsonArray>
#include <QLineEdit>
#include <QLockFile>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPrinter>
#include <QScreen>
#include <QSettings>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QJsonDocument>
#include <QPushButton>
#include <QListWidget>
#include <QSpinBox>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTableWidget>
#include <QToolBar>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QThread>
#include <atomic>
#include <thread>
#include <stdexcept>
#include <cmath>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
QImage fixture() {
  QImage image(60, 40, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace::SRgb);
  for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
    image.setPixel(x, y, qRgba(x * 3, y * 5, 100, 255));
  return image;
}
void acceptDimensions(int width, int height, int left = 0, int top = 0) {
  QTimer::singleShot(0, [=] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    QVERIFY(dialog);
    auto *w = dialog->findChild<QSpinBox *>(QStringLiteral("canvasWidth"));
    auto *h = dialog->findChild<QSpinBox *>(QStringLiteral("canvasHeight"));
    QVERIFY(w && h); w->setValue(width); h->setValue(height);
    if (auto *l = dialog->findChild<QSpinBox *>(QStringLiteral("cropLeft"))) l->setValue(left);
    if (auto *t = dialog->findChild<QSpinBox *>(QStringLiteral("cropTop"))) t->setValue(top);
    dialog->accept();
  });
}
void trigger(EditorWindow &window, const QString &name) {
  auto *action = window.findChild<QAction *>(name);
  QVERIFY(action); action->trigger();
}
void drawRectangle(EditorCanvas &canvas) {
  QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.viewState().toView({8, 8}).toPoint());
  QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, canvas.viewState().toView({22, 16}).toPoint());
}
struct SaveGate {
  std::atomic<bool> entered{false}, released{false}, finished{false}, workerThread{false};
  EditorSaveArtifact stage = EditorSaveArtifact::Raster;
};
EditorSaveHooks gatedSave(const std::shared_ptr<SaveGate> &gate) {
  EditorSaveHooks hooks;
  hooks.beforePublish = [gate](EditorSaveArtifact stage, const QString &) {
    if (stage != gate->stage) return;
    gate->workerThread.store(QThread::currentThread() != QApplication::instance()->thread()); gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate->finished.store(true);
  };
  return hooks;
}
QByteArray storedBytes(const QString &path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
void changeDrawingDefaults(EditorWindow &window, const QString &stroke, const QString &fill, int thickness, bool apply = true) {
  QTimer timer; QElapsedTimer deadline; deadline.start(); bool configured = false, timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
    if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
    if (configured) return; configured = true;
    QCOMPARE(dialog->objectName(), QStringLiteral("drawingDefaultsDialog")); QVERIFY(!window.close());
    dialog->findChild<QLineEdit *>("defaultStrokeColour")->setText(stroke);
    dialog->findChild<QLineEdit *>("defaultFillColour")->setText(fill);
    dialog->findChild<QSpinBox *>("defaultStrokeThickness")->setValue(thickness);
    dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
  });
  timer.start(5); trigger(window, QStringLiteral("drawingDefaults")); timer.stop(); QVERIFY(configured && !timedOut);
}
void configureNewImage(NewImageDialog &dialog) {
  dialog.findChild<QSpinBox *>("newImageWidth")->setValue(64);
  dialog.findChild<QSpinBox *>("newImageHeight")->setValue(48);
  dialog.findChild<QComboBox *>("newImageBackground")->setCurrentIndex(1);
  dialog.findChild<QLineEdit *>("newImageColour")->setText("#FF808080");
  dialog.findChild<QComboBox *>("newImageProfile")->setCurrentIndex(1);
}
void replaceWithNewImage(EditorWindow &window, QMessageBox::StandardButton choice, bool cancelWizard = false,
                         bool expectFailedSave = false) {
  QTimer timer; QElapsedTimer deadline; deadline.start();
  bool configured = false, confirmed = false, saveFailed = false, timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!modal) return;
    if (deadline.elapsed() > 5000) { timedOut = true; modal->reject(); return; }
    if (auto *dialog = qobject_cast<NewImageDialog *>(modal); dialog && !configured) {
      configured = true; QVERIFY(!window.close());
      configureNewImage(*dialog);
      if (cancelWizard) {
        dialog->findChild<QSpinBox *>("newImageWidth")->setValue(100000);
        dialog->findChild<QSpinBox *>("newImageHeight")->setValue(100000);
        dialog->accept(); QVERIFY(dialog->isVisible()); dialog->reject();
      } else dialog->accept();
    } else if (auto *box = qobject_cast<QMessageBox *>(modal)) {
      if (box->windowTitle() == u"Replace current image" && !confirmed) {
        confirmed = true; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); box->button(choice)->click();
      } else if (box->windowTitle() == u"Save incomplete") { saveFailed = true; box->accept(); }
    }
  });
  timer.start(5); trigger(window, QStringLiteral("newImage")); timer.stop();
  QVERIFY(configured && !timedOut); QCOMPARE(confirmed, !cancelWizard); QCOMPARE(saveFailed, expectFailedSave);
}
void saveUntitled(EditorWindow &window, const QString &target = {}) {
  QTimer timer; QElapsedTimer deadline; deadline.start(); bool picked = false, timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
      if (deadline.elapsed() > 5000) { timedOut = true; picker->reject(); return; }
      picked = true;
      if (target.isEmpty()) picker->reject();
      else {
        if (auto *buttons = picker->findChild<QDialogButtonBox *>()) if (auto *save = buttons->button(QDialogButtonBox::Save)) save->setFocus();
        picker->selectFile(target); QCOMPARE(picker->selectedFiles().value(0), target); QMetaObject::invokeMethod(picker, "accept");
      }
    }
  });
  timer.start(5); trigger(window, QStringLiteral("saveImage")); timer.stop(); QVERIFY(picked && !timedOut);
}
void runRotationPreview(EditorWindow &window, bool accept) {
  QTimer timer; QElapsedTimer deadline; deadline.start();
  bool configured = false, intermediateReady = false, previewReady = false, timedOut = false;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *dialog = qobject_cast<CanvasRotationDialog *>(QApplication::activeModalWidget());
    if (!dialog) return;
    if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
    if (!configured) {
      auto *angle = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"));
      auto *interpolation = dialog->findChild<QComboBox *>(QStringLiteral("rotationInterpolation"));
      auto *expand = dialog->findChild<QCheckBox *>(QStringLiteral("rotationExpand"));
      QVERIFY(angle && interpolation && expand);
      angle->setValue(11); interpolation->setCurrentIndex(1); expand->setChecked(false);
      configured = true;
    }
    auto *status = dialog->findChild<QLabel *>(QStringLiteral("rotationStatus")); QVERIFY(status);
    if (!intermediateReady && status->text().startsWith(QStringLiteral("Preview: 60 × 40"))) {
      intermediateReady = true;
      dialog->findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"))->setValue(17);
      dialog->findChild<QComboBox *>(QStringLiteral("rotationInterpolation"))->setCurrentIndex(0);
      dialog->findChild<QCheckBox *>(QStringLiteral("rotationExpand"))->setChecked(true);
      dialog->accept();  // The prior prepared preview can no longer be accepted.
      QVERIFY(dialog->isVisible());
    }
    if (intermediateReady && status->text().startsWith(QStringLiteral("Preview: 70 × 56"))) {
      previewReady = true; accept ? dialog->accept() : dialog->reject();
    }
  });
  timer.start(10);
  trigger(window, QStringLiteral("rotateImage")); timer.stop();
  QVERIFY(configured && intermediateReady && previewReady && !timedOut);
}
}  // namespace

class EditorWindowTest : public QObject {
  Q_OBJECT
  QTemporaryDir dialogPreferences;
private slots:
  void initTestCase() {
    QVERIFY(dialogPreferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dialogPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dialogPreferences.path());
    qputenv("CUPS_SERVER", dialogPreferences.filePath("unavailable-cups.sock").toLocal8Bit());
  }
  void init() { initializeNativeEditorPreferences(); }
  void cleanup() { initializeNativeEditorPreferences(); }
  void annotationObjectsSupportKeyboardSelectionAndActualPersistence() {
    QTemporaryDir dir; const auto path = dir.filePath("objects.png"), prefs = dir.filePath("prefs.json"); QVERIFY(fixture().save(path));
    QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{QJsonObject{{"Id", "focus-annotations"}, {"Hotkey", "F11"}, {"IsVisible", false}}}, nativeEditorPreferenceRevision()).saved);
    auto source = loadSource(path); QVERIFY(source.document); auto doc = *source.document;
    RectangleAnnotation first; first.id = QUuid::createUuid(); first.start = {1, 1}; first.end = {10, 10};
    auto second = first; second.id = QUuid::createUuid(); second.start = {20, 20}; second.end = {35, 30};
    auto hidden = first; hidden.id = QUuid::createUuid(); hidden.visible = false;
    const auto id = QUuid::createUuid(); UnsupportedAnnotation future{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    doc.annotations = {first, second, hidden, future}; QVERIFY(saveEdit(path, fixture(), doc).sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    auto *objects = window->findChild<AnnotationObjectNavigator *>(); QVERIFY(objects); QCOMPARE(objects->model()->rowCount(), 4);
    const auto png = storedBytes(path), companion = storedBytes(path + ".xann"), preferences = storedBytes(prefs);
    window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); QTest::keyClick(canvas, Qt::Key_F11); QTRY_COMPARE(QApplication::focusWidget(), objects);
    QTest::keyClick(objects, Qt::Key_Down); QVERIFY(window->findChild<QAction *>("rectangleProperties")->isEnabled());
    QTest::keyClick(objects, Qt::Key_Down, Qt::ShiftModifier); QVERIFY(!window->findChild<QAction *>("deleteSelection")->isEnabled());
    QTest::keyClick(objects, Qt::Key_Escape); QVERIFY(!window->findChild<QAction *>("deleteSelection")->isEnabled());
    QTest::keyClick(objects, Qt::Key_Home); QTest::keyClick(objects, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    QCOMPARE(storedBytes(path), png); QCOMPARE(storedBytes(path + ".xann"), companion); QCOMPARE(storedBytes(prefs), preferences); QVERIFY(!window->windowTitle().contains(" •"));
    QTest::keyClick(canvas, Qt::Key_Right); trigger(*window, "saveImage"); const auto saved = loadSource(path); QVERIFY(saved.document); QCOMPARE(saved.image, fixture());
    QCOMPARE(std::get<RectangleAnnotation>(saved.document->annotations[0]).start, (PointF{2, 1})); QCOMPARE(annotationId(saved.document->annotations[0]), first.id);
    QCOMPARE(std::get<UnsupportedAnnotation>(saved.document->annotations[3]).raw.value("vendor").toString(), QString("keep"));
    trigger(*window, "undo"); QCOMPARE(objects->selectionModel()->selectedRows()[0].data(Qt::UserRole).toUuid(), first.id);
    trigger(*window, "redo"); QVERIFY(!window->windowTitle().contains(" •")); QCOMPARE(storedBytes(prefs), preferences);
  }
  void nativeGroupMoveSavesBothObjectsAndPreservesSourceAndUnknownFields() {
    QTemporaryDir dir; const auto path = dir.filePath("group.png"), prefs = dir.filePath("prefs.json"); QVERIFY(fixture().save(path));
    QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty()); auto loaded = loadSource(path); QVERIFY(loaded.document); auto doc = *loaded.document;
    RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = {4, 5}; a.end = {16, 15}; a.style = {0x80112233, 0x4000FF00, 1.5, 37, .75}; a.extra = {{"vendor", "keep"}};
    auto b = a; b.id = QUuid::createUuid(); b.start = {30, 20}; b.end = {44, 32}; b.style.rotationDegrees = 90;
    const auto futureId = QUuid::createUuid(); const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    doc.annotations = {a, b, future}; doc.embeddedImages = {{"unused", doc.sourceImagePng}}; QVERIFY(saveEdit(path, fixture(), doc).sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); auto *objects = window->findChild<AnnotationObjectNavigator *>(); QVERIFY(objects);
    canvas->zoomToFit(); objects->focusObjects(); QTRY_COMPARE(QApplication::focusWidget(), objects);
    QTest::keyClick(objects, Qt::Key_Home); QTest::keyClick(objects, Qt::Key_Down, Qt::ShiftModifier); QCOMPARE(canvas->annotationSelection(), (QSet<QUuid>{a.id, b.id}));
    QTest::keyClick(objects, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    const auto original = canvas->annotationDocument();
    const auto before = serializeDocument(original); const auto png = storedBytes(path), sidecar = storedBytes(path + ".xann"), preferences = storedBytes(prefs);
    const auto press = canvas->viewState().toView({10, 10}).toPoint(); const QPoint pixels(32, 16); const auto zoom = canvas->viewState().zoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, press); QVERIFY(canvas->interactionActive()); QVERIFY(!objects->isEnabled());
    for (const auto *name : {"saveImage", "undo", "deleteSelection", "rotateSelection", "copyImage", "printImage"}) {
      auto *action = window->findChild<QAction *>(name); QVERIFY(action); QVERIFY(!action->isEnabled());
    }
    QTest::mouseMove(canvas->viewport(), press + pixels);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(storedBytes(path), png); QCOMPARE(storedBytes(path + ".xann"), sidecar); QVERIFY(!window->windowTitle().contains(" •"));
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, press + pixels); QVERIFY(objects->isEnabled()); QVERIFY(window->windowTitle().contains(" •"));
    auto expected = original;
    for (size_t i : {size_t(0), size_t(1)}) { auto &r = std::get<RectangleAnnotation>(expected.annotations[i]); const auto &actual = std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[i]);
      QVERIFY(std::abs(actual.start.x - (r.start.x + 32 / zoom)) < 1e-10); QVERIFY(std::abs(actual.end.x - (r.end.x + 32 / zoom)) < 1e-10);
      QVERIFY(std::abs(actual.start.y - (r.start.y + 16 / zoom)) < 1e-10); QVERIFY(std::abs(actual.end.y - (r.end.y + 16 / zoom)) < 1e-10); r.start = actual.start; r.end = actual.end;
    }
    QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected));
    trigger(*window, "undo"); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationSelection(), (QSet<QUuid>{a.id, b.id})); QVERIFY(!window->windowTitle().contains(" •"));
    trigger(*window, "redo"); QCOMPARE(serializeDocument(canvas->annotationDocument()), serializeDocument(expected)); trigger(*window, "saveImage");
    const auto reopened = loadSource(path); QVERIFY(reopened.document); QCOMPARE(reopened.image, fixture()); QCOMPARE(reopened.document->sourceImagePng, doc.sourceImagePng);
    QCOMPARE(reopened.document->embeddedImages, doc.embeddedImages); QCOMPARE(reopened.document->annotations.size(), size_t(3));
    for (size_t i : {size_t(0), size_t(1)}) { const auto &actual = std::get<RectangleAnnotation>(reopened.document->annotations[i]); const auto &wanted = std::get<RectangleAnnotation>(expected.annotations[i]);
      QCOMPARE(actual.id, wanted.id); QCOMPARE(actual.start, wanted.start); QCOMPARE(actual.end, wanted.end); QCOMPARE(actual.style.rotationDegrees, wanted.style.rotationDegrees);
      QCOMPARE(actual.style.strokeColor, wanted.style.strokeColor); QCOMPARE(actual.style.fillColor, wanted.style.fillColor); QCOMPARE(actual.style.strokeWidth, wanted.style.strokeWidth); QCOMPARE(actual.style.opacity, wanted.style.opacity); QCOMPARE(actual.extra, wanted.extra); }
    QCOMPARE(std::get<UnsupportedAnnotation>(reopened.document->annotations[2]).raw, future.raw); QCOMPARE(storedBytes(prefs), preferences); QVERIFY(!window->windowTitle().contains(" •"));
  }
  void nativeGroupRotationHasOwnedPreviewAndActualPersistence_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"apply", "cancel", "invalid", "destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void nativeGroupRotationHasOwnedPreviewAndActualPersistence() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("rotation.png"), prefs = dir.filePath("prefs.json"); QVERIFY(fixture().save(path));
    QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty()); QVERIFY(applyToolbarPreferences(QJsonArray{QJsonObject{{"Id", "rotate-selection"}, {"Hotkey", "F10"}, {"IsVisible", false}}}, nativeEditorPreferenceRevision()).saved);
    auto loaded = loadSource(path); QVERIFY(loaded.document); auto doc = *loaded.document;
    RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = {4, 5}; a.end = {16, 15}; a.style = {0x80112233, 0x4000FF00, 1.5, 350, .75}; a.extra = {{"vendor", "keep"}};
    auto b = a; b.id = QUuid::createUuid(); b.start = {30, 20}; b.end = {44, 32}; b.style.rotationDegrees = 37.25;
    const auto futureId = QUuid::createUuid(); const UnsupportedAnnotation future{futureId, {{"id", futureId.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "keep"}}};
    doc.annotations = {a, b, future}; doc.embeddedImages = {{"asset", doc.sourceImagePng}}; QVERIFY(saveEdit(path, fixture(), doc).sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); auto *objects = window->findChild<AnnotationObjectNavigator *>();
    objects->focusObjects(); QTest::keyClick(objects, Qt::Key_A, Qt::ControlModifier); QTest::keyClick(objects, Qt::Key_Return); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    const auto before = serializeDocument(canvas->annotationDocument()); const auto selection = canvas->annotationSelection(); const auto png = storedBytes(path), companion = storedBytes(path + ".xann"), preferences = storedBytes(prefs);
    bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<SelectionRotationDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<SelectionRotationDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      dialog->activateWindow(); QTRY_VERIFY(dialog->isActiveWindow());
      QVERIFY(!canvas->isEnabled()); QVERIFY(!objects->isEnabled()); QVERIFY(!window->close()); QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled());
      QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(storedBytes(path), png); QCOMPARE(storedBytes(path + ".xann"), companion); QCOMPARE(storedBytes(prefs), preferences);
      auto *angle = dialog->findChild<QLineEdit *>("selectionRotationDegrees"); QTest::keyClick(dialog, Qt::Key_A, Qt::AltModifier); QCOMPARE(QApplication::focusWidget(), angle);
      QTest::keyClick(angle, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(angle, "17.25"); const auto *preview = dialog->findChild<QPlainTextEdit *>("selectionRotationPreview");
      QVERIFY(preview->isReadOnly()); QVERIFY(preview->toPlainText().contains("350° → 7.25°")); QVERIFY(preview->toPlainText().contains("37.25° → 54.5°")); QVERIFY(preview->toPlainText().contains("Unsupported object"));
      if (mode == "destroy") { window.reset(); return; }
      if (mode == "invalid") { angle->setText("inf"); dialog->accept(); QVERIFY(dialog->isVisible()); dialog->reject(); return; }
      QTest::keyClick(dialog, mode == "apply" ? Qt::Key_R : Qt::Key_Escape, mode == "apply" ? Qt::AltModifier : Qt::NoModifier);
      QTRY_VERIFY(!dialog->isVisible());
    });
    window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); timer.start(5); QTest::keyClick(canvas, Qt::Key_F10); timer.stop(); QVERIFY(acted);
    QCOMPARE(storedBytes(path), png); QCOMPARE(storedBytes(path + ".xann"), companion); QCOMPARE(storedBytes(prefs), preferences); if (!window) return;
    QVERIFY(canvas->isEnabled()); QVERIFY(objects->isEnabled()); QCOMPARE(canvas->annotationSelection(), selection);
    if (mode != "apply") { QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QVERIFY(!window->windowTitle().contains(" •")); return; }
    auto expected = canvas->annotationDocument(); std::get<RectangleAnnotation>(expected.annotations[0]).style.rotationDegrees = 350; std::get<RectangleAnnotation>(expected.annotations[1]).style.rotationDegrees = 37.25;
    QCOMPARE(serializeDocument(expected), before); const auto rotated = serializeDocument(canvas->annotationDocument());
    QCOMPARE(std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[0]).style.rotationDegrees, 7.25); QCOMPARE(std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[1]).style.rotationDegrees, 54.5);
    trigger(*window, "undo"); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationSelection(), selection); QVERIFY(!window->windowTitle().contains(" •"));
    trigger(*window, "redo"); QCOMPARE(serializeDocument(canvas->annotationDocument()), rotated); trigger(*window, "saveImage"); const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image, fixture()); QCOMPARE(reopened.document->sourceImagePng, doc.sourceImagePng); QCOMPARE(reopened.document->embeddedImages, doc.embeddedImages);
    const auto &first = std::get<RectangleAnnotation>(reopened.document->annotations[0]), &second = std::get<RectangleAnnotation>(reopened.document->annotations[1]);
    QCOMPARE(first.id, a.id); QCOMPARE(first.start, a.start); QCOMPARE(first.end, a.end); QCOMPARE(first.style.rotationDegrees, 7.25); QCOMPARE(first.style.strokeColor, a.style.strokeColor); QCOMPARE(first.style.fillColor, a.style.fillColor); QCOMPARE(first.style.opacity, a.style.opacity); QCOMPARE(first.style.strokeWidth, a.style.strokeWidth); QCOMPARE(first.extra, a.extra);
    QCOMPARE(second.id, b.id); QCOMPARE(second.start, b.start); QCOMPARE(second.end, b.end); QCOMPARE(second.style.rotationDegrees, 54.5); QCOMPARE(std::get<UnsupportedAnnotation>(reopened.document->annotations[2]).raw, future.raw);
    QVERIFY(!window->windowTitle().contains(" •")); QCOMPARE(storedBytes(prefs), preferences);
  }
  void keyboardRectangleCreationSavesFrozenStyleAndPreservesSource_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"apply", "cancel", "destroy"}) QTest::newRow(mode) << QString(mode);
  }
  void keyboardRectangleCreationSavesFrozenStyleAndPreservesSource() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("source.png"), settings = dir.filePath("prefs.json");
    QVERIFY(fixture().save(path)); const auto original = storedBytes(path); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyDrawingDefaults({{"Thickness", 3}, {"BorderColorHex", "#80112233"}, {"FillColorHex", "#40223344"}}, nativeEditorPreferenceRevision()).saved);
    QVERIFY(applyToolbarPreferences(QJsonArray{QJsonObject{{"Id", "create-rectangle"}, {"Hotkey", "F9"}, {"IsVisible", false}}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    trigger(*window, "selectTool"); auto *create = window->findChild<QAction *>("createRectangle"); QVERIFY(create && create->isEnabled());
    const auto prefs = storedBytes(settings); const auto offset = canvas->viewState().offset(); bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const QPointer<RectanglePropertiesDialog> owner(dialog); const auto close = qScopeGuard([owner] { if (owner && owner->isVisible()) owner->reject(); });
      QCOMPARE(dialog->objectName(), QString("rectangleCreationDialog")); QVERIFY(!canvas->isEnabled()); QVERIFY(!create->isEnabled()); QVERIFY(!window->close());
      QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled()); QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
      if (mode == "destroy") { window.reset(); return; }
      dialog->findChild<QLineEdit *>("rectangleLeft")->setText("2.5"); dialog->findChild<QLineEdit *>("rectangleTop")->setText("3.75");
      dialog->findChild<QLineEdit *>("rectangleWidth")->setText("24"); dialog->findChild<QLineEdit *>("rectangleHeight")->setText("12");
      QTest::keyClick(dialog, mode == "apply" ? Qt::Key_R : Qt::Key_Escape, mode == "apply" ? Qt::AltModifier : Qt::NoModifier);
      QTRY_VERIFY(!dialog->isVisible());
    });
    window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); timer.start(5); QTest::keyClick(canvas, Qt::Key_F9); timer.stop(); QVERIFY(acted);
    QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(storedBytes(settings), prefs);
    if (!window) { QCOMPARE(mode, QString("destroy")); return; }
    QVERIFY(canvas->isEnabled()); QVERIFY(create->isEnabled()); QCOMPARE(canvas->tool(), EditorCanvas::Tool::Select); QCOMPARE(canvas->viewState().offset(), offset);
    QCOMPARE(window->windowTitle().contains(" •"), mode == "apply"); if (mode != "apply") return;
    trigger(*window, "undo"); QVERIFY(!window->windowTitle().contains(" •")); trigger(*window, "redo"); QVERIFY(window->windowTitle().contains(" •"));
    trigger(*window, "saveImage"); const auto saved = loadSource(path); QVERIFY(saved.document); QCOMPARE(saved.image, fixture()); QCOMPARE(saved.document->annotations.size(), size_t(1));
    const auto &r = std::get<RectangleAnnotation>(saved.document->annotations[0]); QCOMPARE(r.start, (PointF{2.5, 3.75})); QCOMPARE(r.end, (PointF{26.5, 15.75}));
    QCOMPARE(r.style.strokeWidth, 3.0); QCOMPARE(r.style.strokeColor, Argb(0x80112233)); QCOMPARE(r.style.fillColor, Argb(0x40223344));
    const auto id = r.id; QVERIFY(!id.isNull()); trigger(*window, "undo"); trigger(*window, "redo"); trigger(*window, "saveImage");
    QCOMPARE(std::get<RectangleAnnotation>(loadSource(path).document->annotations[0]).id, id); QCOMPARE(storedBytes(settings), prefs);
  }
  void rectanglePropertiesHaveAtomicHistoryDefaultsAndActualSaveRecovery_data() {
    QTest::addColumn<bool>("apply"); QTest::newRow("apply") << true; QTest::newRow("cancel") << false;
  }
  void rectanglePropertiesHaveAtomicHistoryDefaultsAndActualSaveRecovery() {
    QFETCH(bool, apply); QTemporaryDir dir; const auto preferencePath = dir.filePath("prefs.json"), path = dir.filePath("edited.png");
    QVERIFY(initializeNativeEditorPreferences(preferencePath).isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{QJsonObject{{"Id", "rectangle-properties"}, {"Hotkey", "F8"}, {"IsVisible", false}}}, nativeEditorPreferenceRevision()).saved);
    auto prepared = prepareCapturedSource(fixture()); QVERIFY(prepared.document); auto document = *prepared.document;
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {4, 4}; r.end = {20, 16}; r.style = {0x80112233, 0x40223344, 3, 37.25, .42}; r.extra = {{"future", "kept"}};
    document.annotations = {r}; document.embeddedImages = {{"kept", document.sourceImagePng}};
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(document)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    auto *action = window->findChild<QAction *>("rectangleProperties"); QVERIFY(action); QVERIFY(!action->isEnabled());
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({12, 10}).toPoint()); QVERIFY(action->isEnabled());
    saveUntitled(*window, path); const auto original = storedBytes(path), companion = storedBytes(path + ".xann"), preferences = storedBytes(preferencePath);
    const auto view = canvas->viewState().offset(); bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()); if (!dialog || acted) return; acted = true;
      const auto close = qScopeGuard([dialog] { if (dialog->isVisible()) dialog->reject(); });
      QVERIFY(!canvas->isEnabled()); QVERIFY(!action->isEnabled()); QVERIFY(!window->close());
      QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled()); QVERIFY(!window->findChild<QAction *>("selectTool")->isEnabled());
      QCOMPARE(storedBytes(path), original); QCOMPARE(storedBytes(path + ".xann"), companion);
      dialog->findChild<QLineEdit *>("rectangleLeft")->setText("2.5"); dialog->findChild<QLineEdit *>("rectangleTop")->setText("3.75");
      dialog->findChild<QLineEdit *>("rectangleWidth")->setText("24"); dialog->findChild<QLineEdit *>("rectangleHeight")->setText("12");
      dialog->findChild<QLineEdit *>("rectangleStrokeWidth")->setText("1.5"); dialog->findChild<QLineEdit *>("rectangleRotation")->setText("90");
      dialog->findChild<QLineEdit *>("rectangleOpacity")->setText("0.5"); dialog->findChild<QLineEdit *>("rectangleStrokeColor")->setText("#80334455");
      dialog->findChild<QLineEdit *>("rectangleFillColor")->setText("#40445566");
      if (apply && qEnvironmentVariableIsSet("XERAHS_RECTANGLE_PROPERTIES_QA"))
        QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_RECTANGLE_PROPERTIES_QA")));
      dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); timer.start(5); QTest::keyClick(canvas, Qt::Key_F8); timer.stop(); QVERIFY(acted);
    QVERIFY(canvas->isEnabled()); QVERIFY(action->isEnabled()); QCOMPARE(canvas->viewState().offset(), view); QCOMPARE(storedBytes(preferencePath), preferences);
    QCOMPARE(storedBytes(path), original); QCOMPARE(storedBytes(path + ".xann"), companion); QCOMPARE(window->windowTitle().contains(" •"), apply);
    if (!apply) return;
    trigger(*window, "undo"); QVERIFY(!window->windowTitle().contains(" •")); trigger(*window, "redo"); QVERIFY(window->windowTitle().contains(" •"));
    trigger(*window, "saveImage"); const auto saved = loadSource(path); QVERIFY(saved.document); QCOMPARE(saved.image, fixture());
    QCOMPARE(saved.document->embeddedImages, document.embeddedImages); const auto &edited = std::get<RectangleAnnotation>(saved.document->annotations[0]);
    QCOMPARE(edited.id, r.id); QCOMPARE(edited.extra, r.extra); QCOMPARE(edited.start, (PointF{2.5, 3.75})); QCOMPARE(edited.end, (PointF{26.5, 15.75}));
    QCOMPARE(edited.style.strokeWidth, 1.5); QCOMPARE(edited.style.rotationDegrees, 90.0); QCOMPARE(edited.style.opacity, .5); QCOMPARE(edited.style.strokeColor, Argb(0x80334455)); QCOMPARE(edited.style.fillColor, Argb(0x40445566));
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({38, 28}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({52, 37}).toPoint()); trigger(*window, "saveImage");
    const auto final = loadSource(path); QVERIFY(final.document); QCOMPARE(final.document->annotations.size(), size_t(2));
    const auto &created = std::get<RectangleAnnotation>(final.document->annotations[1]); QCOMPARE(created.style.strokeWidth, 4.0); QCOMPARE(created.style.strokeColor, Argb(0xFFFF0000)); QCOMPARE(created.style.opacity, 1.0);
    QCOMPARE(storedBytes(preferencePath), preferences);
  }
  void destroyingTheEditorDuringPropertiesDoesNotWriteFiles() {
    QTemporaryDir dir; const auto path = dir.filePath("source.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    bool acted = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] {
      if (!qobject_cast<RectanglePropertiesDialog *>(QApplication::activeModalWidget()) || acted) return; acted = true; window.reset();
    });
    timer.start(5); trigger(*window, "rectangleProperties"); timer.stop(); QVERIFY(acted); QVERIFY(!window);
    QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
  }
  void explicitToolsShareMenusHotkeysBusyGuardAndEditableSave() {
    QTemporaryDir dir; const auto preferences = dir.filePath("preferences.json"), path = dir.filePath("tools.png");
    QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{
        QJsonObject{{"Id", "select-tool"}, {"Hotkey", "F6"}, {"IsVisible", true}},
        QJsonObject{{"Id", "rectangle-tool"}, {"Hotkey", "F7"}, {"IsVisible", true}}}, nativeEditorPreferenceRevision()).saved);
    QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); window->activateWindow();
    const auto settings = storedBytes(preferences);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow());
    auto *select = window->findChild<QAction *>("selectTool"), *rectangle = window->findChild<QAction *>("rectangleTool"), *crop = window->findChild<QAction *>("cropTool");
    QVERIFY(select && rectangle && crop); auto *instructions = window->findChild<QLabel *>("annotationToolInstructions"); QVERIFY(instructions && instructions->isVisible());
    QVERIFY(instructions->width() > 0 && instructions->height() > 0);
    QTest::keyClick(canvas, Qt::Key_F6); QVERIFY(select->isChecked()); QVERIFY(!rectangle->isChecked());
    drawRectangle(*canvas); QVERIFY(!window->windowTitle().contains(" •")); QCOMPARE(storedBytes(path), original);
    QTest::keyClick(canvas, Qt::Key_F7); drawRectangle(*canvas); QVERIFY(rectangle->isChecked()); QVERIFY(window->windowTitle().contains(" •"));
    QTest::keyClick(canvas, Qt::Key_F6); QVERIFY(select->isChecked()); trigger(*window, "saveImage");
    QVERIFY(!window->windowTitle().contains(" •")); QVERIFY(storedBytes(path) != original);
    const auto persisted = loadSource(path); QVERIFY(persisted.document); QCOMPARE(persisted.document->annotations.size(), size_t(1));
    QCOMPARE(persisted.image, fixture()); const auto saved = storedBytes(path), sidecar = storedBytes(path + ".xann");
    bool protectedTools = false, rejectedProgrammaticTool = false;
    EditorCopyPorts ports; ports.publish = [&](const EditorCopyPayload &) {
      protectedTools = !select->isEnabled() && !rectangle->isEnabled() && !crop->isEnabled();
      rectangle->setChecked(true); rejectedProgrammaticTool = canvas->tool() == EditorCanvas::Tool::Select && select->isChecked();
      return xerahs::actions::ActionOutcome{};
    };
    window->setCopyPorts(ports); trigger(*window, "copyImage"); QVERIFY(protectedTools && rejectedProgrammaticTool);
    QVERIFY(select->isEnabled() && rectangle->isEnabled() && crop->isEnabled()); QVERIFY(select->isChecked());
    QCOMPARE(storedBytes(path), saved); QCOMPARE(storedBytes(path + ".xann"), sidecar); QCOMPARE(storedBytes(preferences), settings);
    auto *line = new QLineEdit(window.get()); line->show(); window->activateWindow(); line->setFocus();
    QTRY_VERIFY(window->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), line); QTest::keyClick(line, Qt::Key_F7);
    QVERIFY(select->isChecked()); delete line; canvas->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    QTest::keyClick(canvas, Qt::Key_F7); QVERIFY(rectangle->isChecked());
    if (qEnvironmentVariableIsSet("XERAHS_TOOL_QA_IMAGE")) {
      window->resize(1000, 700); QCoreApplication::processEvents(); QVERIFY(window->grab().save(qEnvironmentVariable("XERAHS_TOOL_QA_IMAGE")));
    }
  }
  void annotationCopyPasteCutAndDuplicateUseNativeMenusShortcutsAndOneHistoryPerMutation() {
    QTemporaryDir dir; const auto path = dir.filePath("source.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); const auto view = canvas->viewState(); const auto prefs = nativeEditorPreferences().values;
    AnnotationClipboardPorts ports; QByteArray offer; int publications = 0;
    ports.publish = [&](const QByteArray &bytes) { ++publications; offer = bytes; auto *mime = new QMimeData; mime->setData(annotationClipboardMime, bytes); QApplication::clipboard()->setMimeData(mime); return xerahs::actions::ActionOutcome{}; };
    window->setAnnotationClipboardPorts(ports); QSignalSpy finished(window.get(), &EditorWindow::annotationTransferFinished), saved(window.get(), &EditorWindow::saveFinished);
    trigger(*window, "copyAnnotations"); QCOMPARE(publications, 1); QVERIFY(!offer.contains(path.toUtf8()) && !offer.contains("sourceImagePng")); QVERIFY(parseAnnotationClipboard(offer).ok());
    const auto copied = offer; const auto originalId = annotationId(parseAnnotationClipboard(copied).annotations[0]); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    window->activateWindow(); canvas->setFocus(); QTest::qWait(10); QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(publications, 2);
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier); QVERIFY(window->statusBar()->currentMessage().contains("2 annotation(s)"));
    QTest::keyClick(canvas, Qt::Key_V, Qt::ControlModifier); QVERIFY(window->statusBar()->currentMessage().contains("3 annotation(s)"));
    trigger(*window, "copyAnnotations"); const auto pasted = parseAnnotationClipboard(offer); QVERIFY(pasted.ok()); QVERIFY(annotationId(pasted.annotations[0]) != originalId);
    QCOMPARE(std::get<RectangleAnnotation>(pasted.annotations[0]).start.x, std::get<RectangleAnnotation>(parseAnnotationClipboard(copied).annotations[0]).start.x + 20);
    QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier); QVERIFY(window->statusBar()->currentMessage().contains("4 annotation(s)"));
    trigger(*window, "undo"); QVERIFY(window->statusBar()->currentMessage().contains("3 annotation(s)")); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    QTest::keyClick(canvas, Qt::Key_X, Qt::ControlModifier); QVERIFY(window->statusBar()->currentMessage().contains("2 annotation(s)")); QVERIFY(!window->findChild<QAction *>("redo")->isEnabled());
    trigger(*window, "undo"); QVERIFY(window->statusBar()->currentMessage().contains("3 annotation(s)"));
    QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(saved.count(), 0); QCOMPARE(nativeEditorPreferences().values, prefs);
    QCOMPARE(canvas->viewState().zoom(), view.zoom()); QCOMPARE(canvas->viewState().offset(), view.offset());
    trigger(*window, "saveImage"); const auto reopened = loadSource(path); QVERIFY(reopened.document); QCOMPARE(reopened.document->annotations.size(), size_t(3));
    QSet<QUuid> ids; for (const auto &a : reopened.document->annotations) ids.insert(annotationId(a)); QCOMPARE(ids.size(), 3);
    QCOMPARE(finished.count(), 7); QVERIFY(window->findChild<QAction *>("copyImage")->shortcut() == QKeySequence::Copy);
  }
  void pastedObjectsHaveVisibleViewportOffsetEvenWhenZoomedOut() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    AnnotationClipboardPorts ports; QByteArray bytes;
    ports.publish = [&](const QByteArray &data) { bytes = data; auto *mime = new QMimeData; mime->setData(annotationClipboardMime, data); QApplication::clipboard()->setMimeData(mime); return xerahs::actions::ActionOutcome{}; }; window->setAnnotationClipboardPorts(ports);
    trigger(*window, "copyAnnotations"); const auto original = std::get<RectangleAnnotation>(parseAnnotationClipboard(bytes).annotations[0]);
    for (int i = 0; i < 8; ++i) canvas->zoomOut(); QVERIFY(canvas->viewState().zoom() < 1);
    trigger(*window, "pasteAnnotations"); trigger(*window, "copyAnnotations"); const auto pasted = std::get<RectangleAnnotation>(parseAnnotationClipboard(bytes).annotations[0]);
    const auto difference = canvas->viewState().toView({pasted.start.x, pasted.start.y}) - canvas->viewState().toView({original.start.x, original.start.y});
    QVERIFY(std::abs(difference.x() - 10) < 1e-8); QVERIFY(std::abs(difference.y() - 10) < 1e-8);
  }
  void nativeDuplicationPreservesEmbeddedAssetsAndAssetClipboardRefusalKeepsEverything() {
    QTemporaryDir dir; const auto prepared = prepareCapturedSource(fixture()); QVERIFY(prepared.document); auto document = *prepared.document;
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {4, 4}; r.end = {20, 15}; r.extra = {{"assetReference", "retained"}}; document.annotations = {r}; document.embeddedImages = {{"retained", document.sourceImagePng}};
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(document)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({7, 7}).toPoint()); trigger(*window, "duplicateAnnotations");
    QVERIFY(window->statusBar()->currentMessage().contains("2 annotation(s)")); AnnotationClipboardPorts ports; int publications = 0;
    ports.publish = [&](const QByteArray &) { ++publications; return xerahs::actions::ActionOutcome{}; }; window->setAnnotationClipboardPorts(ports); QApplication::clipboard()->setText("KEEP");
    bool warned = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { warned = true; QVERIFY(box->text().contains("assets")); box->accept(); } });
    timer.start(5); trigger(*window, "cutAnnotations"); timer.stop(); QVERIFY(warned); QCOMPARE(publications, 0); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP")); QVERIFY(window->statusBar()->currentMessage().contains("2 annotation(s)"));
    const auto path = dir.filePath("duplicated.png"); saveUntitled(*window, path); const auto loaded = loadSource(path); QVERIFY(loaded.document); QCOMPARE(loaded.document->embeddedImages, document.embeddedImages); QCOMPARE(loaded.document->annotations.size(), size_t(2));
    QCOMPARE(std::get<RectangleAnnotation>(loaded.document->annotations[1]).extra, r.extra); QVERIFY(annotationId(loaded.document->annotations[1]) != r.id);
  }
  void lockedNativeSelectionsKeepGeometryAndDisableDirectDeleteAndRotation() {
    const auto prepared = prepareCapturedSource(fixture()); auto document = *prepared.document; RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {4, 4}; r.end = {20, 15}; r.extra = {{"locked", true}}; document.annotations = {r};
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(document)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({7, 7}).toPoint());
    QVERIFY(!window->findChild<QAction *>("deleteSelection")->isEnabled()); QVERIFY(!window->findChild<QAction *>("rotateSelection")->isEnabled()); QVERIFY(window->findChild<QAction *>("deleteSelection")->toolTip().contains("protected"));
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({7, 7}).toPoint()); QVERIFY(!canvas->interactionActive());
    QTest::mouseMove(canvas->viewport(), canvas->viewState().toView({17, 17}).toPoint()); QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({17, 17}).toPoint());
    QTest::keyClick(canvas, Qt::Key_Right); QTest::keyClick(canvas, Qt::Key_Delete); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
    QTemporaryDir dir; const auto path = dir.filePath("locked.png"); saveUntitled(*window, path); const auto loaded = loadSource(path); QVERIFY(loaded.document); QCOMPARE(serializeAnnotationList(loaded.document->annotations), serializeAnnotationList(document.annotations));
  }
  void annotationCompletionObserverExceptionKeepsOneCommittedMutation() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    QSignalSpy finished(window.get(), &EditorWindow::annotationTransferFinished); connect(window.get(), &EditorWindow::annotationTransferFinished, this, [] { throw std::runtime_error("PRIVATE"); });
    trigger(*window, "duplicateAnnotations"); QCOMPARE(finished.count(), 1); QVERIFY(finished.at(0).at(3).toBool()); QVERIFY(window->statusBar()->currentMessage().contains("2 annotation(s)")); QVERIFY(window->statusBar()->currentMessage().contains("completion observer failed"));
    trigger(*window, "undo"); QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
  }
  void annotationPublicationFailuresNeverCutAndCallbackDiagnosticsAreRedacted_data() {
    QTest::addColumn<int>("kind"); for (int i = 0; i < 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
  }
  void annotationPublicationFailuresNeverCutAndCallbackDiagnosticsAreRedacted() {
    QFETCH(int, kind); std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    const auto title = window->windowTitle(); AnnotationClipboardPorts ports; int publications = 0; bool busy = false, warned = false;
    QApplication::clipboard()->setText("KEEP");
    ports.publish = [&](const QByteArray &bytes) -> xerahs::actions::ActionOutcome {
      ++publications; busy = !canvas->isEnabled() && !window->close(); trigger(*window, "duplicateAnnotations"); trigger(*window, "pasteAnnotations"); trigger(*window, "saveImage");
      if (kind) { auto *mime = new QMimeData; mime->setData(annotationClipboardMime, bytes); QApplication::clipboard()->setMimeData(mime); }
      if (kind == 1) throw std::runtime_error("PRIVATE callback");
      auto result = xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "PRIVATE diagnostic", std::nullopt};
      if (kind == 2) result.state = xerahs::actions::ActionState::Skipped;
      if (kind == 3) result.outcomeUnknown = true;
      return result;
    };
    window->setAnnotationClipboardPorts(ports); QSignalSpy finished(window.get(), &EditorWindow::annotationTransferFinished); QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { warned = true; QVERIFY(!box->informativeText().contains("PRIVATE")); box->accept(); } });
    timer.start(5); trigger(*window, "cutAnnotations"); timer.stop(); QVERIFY(warned && busy); QCOMPARE(publications, 1); QCOMPARE(window->windowTitle(), title); QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
    QCOMPARE(finished.at(0).at(1).toString(), kind ? QString("unknown") : QString("failed")); QVERIFY(!finished.at(0).at(3).toBool()); QVERIFY(canvas->isEnabled());
    if (!kind) QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
    trigger(*window, "undo"); QVERIFY(window->statusBar()->currentMessage().contains("0 annotation(s)"));
  }
  void malformedAndForeignClipboardReadsCannotMutateSessionAndTextEntryKeepsLocalEditing() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    const auto title = window->windowTitle(); bool warned = false; QTimer timer; connect(&timer, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { warned = true; box->accept(); } });
    auto *mime = new QMimeData; mime->setData(annotationClipboardMime, "malformed"); QApplication::clipboard()->setMimeData(mime);
    timer.start(5); trigger(*window, "pasteAnnotations"); timer.stop(); QVERIFY(warned); QCOMPARE(window->windowTitle(), title); QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
    auto *field = new QLineEdit("LOCAL", window.get()); field->show(); field->setFocus(); field->selectAll(); QTest::qWait(10); QTest::keyClick(field, Qt::Key_X, Qt::ControlModifier); QCOMPARE(field->text(), QString()); QCOMPARE(QApplication::clipboard()->text(), QString("LOCAL"));
    QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier); QCOMPARE(field->text(), QString("LOCAL")); QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
    AnnotationClipboardPorts ports; ports.publish = {}; ports.read = {}; window->setAnnotationClipboardPorts(ports);
    QVERIFY(!window->findChild<QAction *>("copyAnnotations")->isEnabled()); QVERIFY(!window->findChild<QAction *>("cutAnnotations")->isEnabled()); QVERIFY(!window->findChild<QAction *>("pasteAnnotations")->isEnabled());
    QVERIFY(window->findChild<QAction *>("copyAnnotations")->toolTip().contains("unavailable")); QVERIFY(window->findChild<QAction *>("duplicateAnnotations")->isEnabled());
  }
  void nativeAnnotationOfferAndReplacementReportActualOwnershipWithoutDeletingObjects() {
    auto prepared = prepareCapturedSource(fixture()); RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {1, 1}; r.end = {15, 10}; prepared.document->annotations = {r};
    const auto transfer = selectedAnnotationTransfer(*prepared.document, {r.id}, true); QVERIFY(transfer.ok()); QApplication::clipboard()->setText("KEEP");
    const auto invalid = publishAnnotationClipboard("invalid"); QCOMPARE(invalid.state, xerahs::actions::ActionState::Failed); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
    const auto offered = publishAnnotationClipboard(transfer.bytes); QVERIFY(QApplication::clipboard()->mimeData()->hasFormat(annotationClipboardMime)); QCOMPARE(readAnnotationClipboard().bytes, transfer.bytes);
    QCOMPARE(offered.outcomeUnknown, !QApplication::clipboard()->ownsClipboard());
    bool replaced = false; const auto connection = connect(QApplication::clipboard(), &QClipboard::dataChanged, [&] { if (!replaced) { replaced = true; QApplication::clipboard()->setText("FOREIGN"); } });
    const auto unknown = publishAnnotationClipboard(transfer.bytes); disconnect(connection); QVERIFY(replaced && unknown.outcomeUnknown); QCOMPARE(QApplication::clipboard()->text(), QString("FOREIGN"));
  }
  void annotationPublicationReadAndCompletionCanDestroyTheirOwner_data() { QTest::addColumn<int>("kind"); QTest::newRow("publication") << 0; QTest::newRow("read") << 1; QTest::newRow("completion") << 2; }
  void annotationPublicationReadAndCompletionCanDestroyTheirOwner() {
    QFETCH(int, kind); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas); AnnotationClipboardPorts ports;
    ports.publish = [&](const QByteArray &) { delete window.data(); return xerahs::actions::ActionOutcome{}; };
    ports.read = [&] { delete window.data(); return AnnotationClipboardRead{}; }; window->setAnnotationClipboardPorts(ports); bool changed = false;
    connect(window, &EditorWindow::annotationTransferFinished, this, [&](const QString &, const QString &, const QString &, bool edit) { changed = edit; delete window.data(); });
    window->findChild<QAction *>(kind == 0 ? "cutAnnotations" : kind == 1 ? "pasteAnnotations" : "duplicateAnnotations")->trigger(); QVERIFY(!window); QCOMPARE(changed, kind == 2);
  }
  void printMenuAndShortcutSubmitWithoutSavingOrChangingHistoryFilesPreferencesOrClipboard() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas); trigger(*window, "deleteSelection"); trigger(*window, "undo"); canvas->zoomIn();
    const auto title = window->windowTitle(); const auto preferences = nativeEditorPreferences().values; const auto view = canvas->viewState();
    QApplication::clipboard()->setText("KEEP"); EditorPrintPorts ports; int configurations = 0, submissions = 0; bool gui = false, worker = false, busy = false; EditorPrintSnapshotPtr snapshot;
    ports.configure = [&](const EditorPrintSnapshot &value, QWidget *) {
      ++configurations; gui = QThread::currentThread() == QApplication::instance()->thread(); snapshot = std::make_shared<EditorPrintSnapshot>(value);
      busy = !window->findChild<QAction *>("saveImage")->isEnabled() && !window->findChild<QAction *>("copyImage")->isEnabled() && !window->findChild<QAction *>("printImage")->isEnabled() && !canvas->isEnabled() && !window->close();
      trigger(*window, "printImage"); trigger(*window, "saveImage");
      auto printer = std::make_shared<QPrinter>(); printer->setOutputFormat(QPrinter::PdfFormat); printer->setOutputFileName(dir.filePath("printed.pdf")); return EditorPrintChoice{{}, printer, 100};
    };
    ports.submit = [&](QPrinter &printer, const EditorPrintSnapshot &value, double scale, const std::atomic<bool> &cancel) { ++submissions; worker = QThread::currentThread() != QApplication::instance()->thread(); return submitEditorPrint(printer, value, scale, cancel); };
    window->setPrintPorts(ports); QSignalSpy printed(window.get(), &EditorWindow::printFinished), saved(window.get(), &EditorWindow::saveFinished);
    auto *action = window->findChild<QAction *>("printImage"); QVERIFY(action && action->isEnabled()); QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)); QVERIFY(!action->autoRepeat());
    trigger(*window, "printImage"); QCOMPARE(configurations, 1); QCOMPARE(submissions, 1); QVERIFY(gui && worker && busy && snapshot); QCOMPARE(printed.at(0).at(0).toString(), QString("succeeded"));
    window->activateWindow(); canvas->setFocus(); QTest::qWait(10); QTest::keyClick(canvas, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QCOMPARE(submissions, 2);
    QCOMPARE(window->windowTitle(), title); QVERIFY(window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled()); QCOMPARE(canvas->viewState().zoom(), view.zoom()); QCOMPARE(canvas->viewState().offset(), view.offset());
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP")); QCOMPARE(saved.count(), 0);
    QVERIFY(window->statusBar()->currentMessage().contains("physical delivery is not confirmed")); QVERIFY(QFile::exists(dir.filePath("printed.pdf")));
  }
  void knownPrintFailureRetryUsesFrozenSnapshotAndFreshConfirmationWithDefaultCancel() {
    QTemporaryDir dir; std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>();
    drawRectangle(*canvas); trigger(*window, "deleteSelection"); trigger(*window, "undo"); const auto title = window->windowTitle();
    EditorPrintPorts ports; int renders = 0, configurations = 0, submissions = 0; bool fail = true, same = false; QImage first;
    ports.beforeRender = [&] { ++renders; }; ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { ++configurations; auto printer = std::make_shared<QPrinter>(); printer->setOutputFormat(QPrinter::PdfFormat); return EditorPrintChoice{{}, printer, 100}; };
    ports.submit = [&](QPrinter &, const EditorPrintSnapshot &value, double, const std::atomic<bool> &) { ++submissions; if (first.isNull()) first = value.colour; else same = first == value.colour; return fail ? xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "PRIVATE", std::nullopt} : xerahs::actions::ActionOutcome{}; };
    window->setPrintPorts(ports); bool warned = false, confirmed = false, approve = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (box->objectName() == "retryImagePrintDialog") { confirmed = true; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); box->button(approve ? QMessageBox::Retry : QMessageBox::Cancel)->click(); }
      else { warned = true; QVERIFY(!box->informativeText().contains("PRIVATE")); box->accept(); }
    });
    timer.start(5); trigger(*window, "printImage"); QVERIFY(warned); auto *retry = window->findChild<QAction *>("retryImagePrint"); QVERIFY(retry->isEnabled());
    trigger(*window, "retryImagePrint"); QVERIFY(confirmed); QCOMPARE(submissions, 1); approve = true; fail = false; trigger(*window, "retryImagePrint"); timer.stop();
    QCOMPARE(renders, 1); QCOMPARE(configurations, 2); QCOMPARE(submissions, 2); QVERIFY(same && !retry->isEnabled()); QCOMPARE(window->windowTitle(), title); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    fail = true; timer.start(5); trigger(*window, "printImage"); timer.stop(); QVERIFY(retry->isEnabled()); trigger(*window, "redo"); QVERIFY(!retry->isEnabled()); trigger(*window, "undo"); QVERIFY(retry->isEnabled());
  }
  void unknownPrintDeliveryCannotAuthorizeRetryAndMissingServiceIsVisible() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); EditorPrintPorts ports;
    ports.configure = [](const EditorPrintSnapshot &, QWidget *) { auto printer = std::make_shared<QPrinter>(); printer->setOutputFormat(QPrinter::PdfFormat); return EditorPrintChoice{{}, printer, 100}; };
    ports.submit = [](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) -> xerahs::actions::ActionOutcome { throw std::runtime_error("PRIVATE callback"); };
    window->setPrintPorts(ports); bool warned = false; QSignalSpy printed(window.get(), &EditorWindow::printFinished); QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) { warned = true; QVERIFY(box->text().contains("queue")); QVERIFY(!box->informativeText().contains("PRIVATE")); box->accept(); } });
    timer.start(5); trigger(*window, "printImage"); timer.stop(); QVERIFY(warned); QCOMPARE(printed.at(0).at(0).toString(), QString("unknown")); QVERIFY(!window->findChild<QAction *>("retryImagePrint")->isEnabled());
    ports.configure = {}; window->setPrintPorts(ports); auto *action = window->findChild<QAction *>("printImage"); QVERIFY(!action->isEnabled()); QVERIFY(action->toolTip().contains("no print service"));
  }
  void printCancellationKeepsTheEditorResponsiveAndRefusesReentrantActions() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); const auto title = window->windowTitle(); auto *canvas = window->findChild<EditorCanvas *>();
    const auto gate = std::make_shared<SaveGate>(); EditorPrintPorts ports; int configurations = 0;
    ports.beforeRender = [gate] { gate->entered.store(true); while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); gate->finished.store(true); };
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { ++configurations; return EditorPrintChoice{}; }; window->setPrintPorts(ports); bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (!gate->entered.load() || acted) return; acted = true; QVERIFY(!window->close()); QVERIFY(!window->openImage()); QVERIFY(!canvas->isEnabled());
      trigger(*window, "copyImage"); trigger(*window, "printImage"); trigger(*window, "saveImage"); auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); QVERIFY(progress); progress->findChild<QPushButton *>()->click(); gate->released.store(true); });
    timer.start(5); trigger(*window, "printImage"); timer.stop(); QVERIFY(acted && gate->finished.load()); QCOMPARE(configurations, 0); QCOMPARE(window->windowTitle(), title); QVERIFY(canvas->isEnabled()); QVERIFY(window->findChild<QAction *>("printImage")->isEnabled());
  }
  void printCompletionObserverExceptionKeepsAcknowledgementAndNeverRepeatsTheJob() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); EditorPrintPorts ports; int submissions = 0;
    ports.configure = [](const EditorPrintSnapshot &, QWidget *) { auto printer = std::make_shared<QPrinter>(); printer->setOutputFormat(QPrinter::PdfFormat); return EditorPrintChoice{{}, printer, 100}; };
    ports.submit = [&](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { ++submissions; return xerahs::actions::ActionOutcome{}; }; window->setPrintPorts(ports);
    QSignalSpy finished(window.get(), &EditorWindow::printFinished);
    connect(window.get(), &EditorWindow::printFinished, this, [] { throw std::runtime_error("PRIVATE observer"); });
    trigger(*window, "printImage"); QCOMPARE(submissions, 1); QCOMPARE(finished.count(), 1); QCOMPARE(finished.at(0).at(0).toString(), QString("succeeded"));
    QVERIFY(window->statusBar()->currentMessage().contains("completion observer failed")); QVERIFY(window->findChild<QAction *>("printImage")->isEnabled()); QVERIFY(!window->findChild<QAction *>("retryImagePrint")->isEnabled());
  }
  void printConfigurationAndCompletionCanDestroyTheirOwner_data() { QTest::addColumn<bool>("duringConfiguration"); QTest::newRow("configuration") << true; QTest::newRow("completion") << false; }
  void printConfigurationAndCompletionCanDestroyTheirOwner() {
    QFETCH(bool, duringConfiguration); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture())); QVERIFY(window); window->show(); EditorPrintPorts ports; bool acknowledged = false;
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { if (duringConfiguration) { delete window.data(); return EditorPrintChoice{}; } auto printer = std::make_shared<QPrinter>(); printer->setOutputFormat(QPrinter::PdfFormat); return EditorPrintChoice{{}, printer, 100}; };
    ports.submit = [](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { return xerahs::actions::ActionOutcome{}; }; window->setPrintPorts(ports);
    connect(window, &EditorWindow::printFinished, this, [&](const QString &state, const QString &, bool) { acknowledged = state == "succeeded"; trigger(*window, "printImage"); delete window.data(); });
    auto *action = window->findChild<QAction *>("printImage"); action->trigger(); QVERIFY(!window); QCOMPARE(acknowledged, !duringConfiguration);
  }
  void copyMenuAndShortcutPublishOneSnapshotWithoutSavingOrClearingHistory() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas); trigger(*window, "deleteSelection"); trigger(*window, "undo");
    const auto title = window->windowTitle(); const auto status = window->statusBar()->currentMessage(); const auto preferences = nativeEditorPreferences().values; const auto view = canvas->viewState();
    EditorCopyPorts ports; int publications = 0; EditorCopyPayloadPtr payload;
    ports.publish = [&](const EditorCopyPayload &value) { ++publications; payload = std::make_shared<EditorCopyPayload>(value); QApplication::clipboard()->setImage(value.image); return xerahs::actions::ActionOutcome{}; };
    window->setCopyPorts(ports); QSignalSpy copied(window.get(), &EditorWindow::copyFinished), saved(window.get(), &EditorWindow::saveFinished);
    auto *action = window->findChild<QAction *>("copyImage"); QVERIFY(action && action->isEnabled()); QCOMPARE(action->shortcut(), QKeySequence::Copy); QVERIFY(!action->autoRepeat());
    action->trigger(); QCOMPARE(publications, 1); QCOMPARE(copied.first()[0].toString(), QString("succeeded")); QVERIFY(payload);
    QCOMPARE(payload->image.pixel(8, 12), qRgb(255, 0, 0)); QCOMPARE(payload->image.pixel(0, 0), fixture().pixel(0, 0)); QCOMPARE(QApplication::clipboard()->image(), payload->image);
    QCOMPARE(window->windowTitle(), title); QVERIFY(window->statusBar()->currentMessage().startsWith(status)); QVERIFY(window->findChild<QAction *>("redo")->isEnabled()); QCOMPARE(saved.count(), 0); QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
    QCOMPARE(nativeEditorPreferences().values, preferences); QCOMPARE(canvas->viewState().zoom(), view.zoom()); QCOMPARE(canvas->viewState().offset(), view.offset());
    window->activateWindow(); QTRY_VERIFY(window->isActiveWindow());
    auto *line = new QLineEdit("Local text", window.get()); line->show(); line->setFocus(); line->selectAll(); QTRY_COMPARE(QApplication::focusWidget(), line); QTest::keyClick(line, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(publications, 1); QCOMPARE(QApplication::clipboard()->text(), QString("Local text")); delete line;
    window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier); QCOMPARE(publications, 2); QCOMPARE(window->windowTitle(), title); QCOMPARE(saved.count(), 0);
  }
  void copyCancellationIsResponsiveAndBlocksOtherOutputAndDocumentActions() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>()); const auto title = window->windowTitle();
    const auto gate = std::make_shared<SaveGate>(); EditorCopyPorts ports; int publications = 0;
    ports.beforeRender = [gate] { gate->workerThread.store(QThread::currentThread() != qApp->thread()); gate->entered.store(true); while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
    ports.publish = [&](const EditorCopyPayload &) { ++publications; return xerahs::actions::ActionOutcome{}; }; window->setCopyPorts(ports);
    QApplication::clipboard()->setText("KEEP"); QSignalSpy copied(window.get(), &EditorWindow::copyFinished); QTimer timer; bool observed = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); if (!dialog || !gate->entered.load() || observed) return;
      observed = true; QCOMPARE(dialog->objectName(), QString("copyImageProgress")); QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled()); QVERIFY(!window->findChild<QAction *>("copyImage")->isEnabled());
      QVERIFY(!window->close()); QVERIFY(!window->openImage(path)); trigger(*window, "saveImage"); trigger(*window, "copyImage");
      dialog->findChild<QPushButton *>()->click(); gate->released.store(true);
    });
    timer.start(5); trigger(*window, "copyImage"); timer.stop(); QVERIFY(observed && gate->workerThread.load()); QCOMPARE(publications, 0); QCOMPARE(copied.count(), 1); QCOMPARE(copied.first()[0].toString(), QString("cancelled"));
    QCOMPARE(window->windowTitle(), title); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP")); QVERIFY(window->findChild<QAction *>("saveImage")->isEnabled()); QVERIFY(window->findChild<QAction *>("copyImage")->isEnabled());
  }
  void knownCopyFailureOffersDefaultCancelRetryWithoutRenderingOrChangingRedo() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>());
    EditorCopyPorts ports; int encodes = 0, publications = 0; bool acceptPublication = false; QByteArray first;
    ports.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); };
    ports.publish = [&](const EditorCopyPayload &payload) { ++publications; if (first.isEmpty()) first = payload.png;
      if (!acceptPublication) return xerahs::actions::ActionOutcome{xerahs::actions::ActionState::Failed, "clipboard-unavailable", std::nullopt};
      QApplication::clipboard()->setImage(payload.image); return xerahs::actions::ActionOutcome{}; };
    window->setCopyPorts(ports); QSignalSpy copied(window.get(), &EditorWindow::copyFinished); QTimer failure; bool reported = false;
    connect(&failure, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box && box->windowTitle() == "Image copy incomplete") { reported = true; QVERIFY(box->informativeText().contains("clipboard-unavailable")); box->accept(); } });
    failure.start(5); trigger(*window, "copyImage"); failure.stop(); QVERIFY(reported); auto *retry = window->findChild<QAction *>("retryImageCopy"); QVERIFY(retry->isEnabled());
    trigger(*window, "deleteSelection"); QVERIFY(!retry->isEnabled()); trigger(*window, "undo"); QVERIFY(retry->isEnabled()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    bool cancelled = false, confirmed = false; QTimer confirmation;
    connect(&confirmation, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || box->objectName() != "retryImageCopyDialog") return;
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); if (!acceptPublication) { cancelled = true; box->button(QMessageBox::Cancel)->click(); } else { confirmed = true; box->button(QMessageBox::Retry)->click(); } });
    confirmation.start(5); retry->trigger(); confirmation.stop(); QVERIFY(cancelled); QCOMPARE(encodes, 1); QCOMPARE(publications, 1); QCOMPARE(copied.count(), 1);
    acceptPublication = true; confirmation.start(5); retry->trigger(); confirmation.stop(); QVERIFY(confirmed); QCOMPARE(encodes, 1); QCOMPARE(publications, 2); QCOMPARE(copied.last()[0].toString(), QString("succeeded"));
    QVERIFY(!retry->isEnabled()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled()); QVERIFY(window->windowTitle().contains(QChar(0x2022))); QCOMPARE(QApplication::clipboard()->image().pixel(8, 12), qRgb(255, 0, 0));
  }
  void unknownCopyDeliveryCannotAuthorizeRetryAndMissingServiceIsVisible() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>());
    EditorCopyPorts ports; ports.publish = [](const EditorCopyPayload &payload) -> xerahs::actions::ActionOutcome { QApplication::clipboard()->setImage(payload.image); throw std::runtime_error("PRIVATE callback payload"); };
    window->setCopyPorts(ports); QSignalSpy copied(window.get(), &EditorWindow::copyFinished); QTimer timer; bool reported = false;
    connect(&timer, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box && box->windowTitle() == "Image copy incomplete") { reported = true; QVERIFY(box->text().contains("may have changed")); QVERIFY(!box->informativeText().contains("PRIVATE")); box->accept(); } });
    timer.start(5); trigger(*window, "copyImage"); timer.stop(); QVERIFY(reported); QCOMPARE(copied.first()[0].toString(), QString("unknown")); QVERIFY(!window->findChild<QAction *>("retryImageCopy")->isEnabled());
    QVERIFY(window->windowTitle().contains(QChar(0x2022))); ports.publish = {}; window->setCopyPorts(ports); auto *copy = window->findChild<QAction *>("copyImage"); QVERIFY(!copy->isEnabled()); QVERIFY(copy->toolTip().contains("no clipboard service"));
    trigger(*window, "copyImage"); QCOMPARE(copied.count(), 1);
  }
  void copyPublicationAndCompletionCanDestroyTheirOwner_data() { QTest::addColumn<bool>("duringPublication"); QTest::newRow("publication") << true; QTest::newRow("completion") << false; }
  void copyPublicationAndCompletionCanDestroyTheirOwner() {
    QFETCH(bool, duringPublication); QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>()); bool published = false;
    EditorCopyPorts ports; ports.publish = [&](const EditorCopyPayload &payload) { QApplication::clipboard()->setImage(payload.image); published = true; if (duringPublication) window.reset(); return xerahs::actions::ActionOutcome{}; };
    window->setCopyPorts(ports); connect(window.get(), &EditorWindow::copyFinished, this, [&](const QString &, const QString &, bool) { if (!duringPublication) window.reset(); });
    trigger(*window, "copyImage"); QVERIFY(published && !window); QCOMPARE(QApplication::clipboard()->image().pixel(8, 12), qRgb(255, 0, 0));
  }
  void saveRunsOnWorkerWithResponsiveCancellationAndNoReentrantActions_data() {
    QTest::addColumn<bool>("afterRaster"); QTest::newRow("before PNG") << false; QTest::newRow("after PNG") << true;
  }
  void saveRunsOnWorkerWithResponsiveCancellationAndNoReentrantActions() {
    QFETCH(bool, afterRaster); QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    const auto gate = std::make_shared<SaveGate>(); if (afterRaster) gate->stage = EditorSaveArtifact::Sidecar; window->setSaveHooks(gatedSave(gate));
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished); QTimer timer; bool observed = false, failure = false;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); progress && gate->entered.load() && !observed) {
        observed = true; QCOMPARE(progress->objectName(), QString("saveImageProgress")); QVERIFY(!canvas->isEnabled()); QVERIFY(!window->close());
        QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled()); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
        QVERIFY(!window->openImage(path)); trigger(*window, "saveImage");
        auto *cancel = progress->findChild<QPushButton *>(); QVERIFY(cancel); cancel->click(); gate->released.store(true);
      } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); box && box->windowTitle() == "Save incomplete") { failure = true; box->accept(); }
    });
    timer.start(5); trigger(*window, "saveImage"); timer.stop(); QVERIFY(observed && gate->workerThread.load()); QCOMPARE(failure, afterRaster);
    QCOMPARE(saved.count(), 1); QCOMPARE(saved.first()[0].toBool(), afterRaster); QVERIFY(!saved.first()[1].toBool()); QVERIFY(saved.first()[2].toBool());
    QVERIFY(window->windowTitle().contains(QChar(0x2022))); QVERIFY(canvas->isEnabled()); QVERIFY(window->findChild<QAction *>("saveImage")->isEnabled());
    QCOMPARE(window->findChild<QAction *>("retryEditorSidecar")->isEnabled(), afterRaster); QVERIFY(!QFile::exists(path + ".xann"));
    if (!afterRaster) QCOMPARE(storedBytes(path), original); else QVERIFY(storedBytes(path) != original);
  }
  void incompleteSaveOffersDefaultCancelCompanionOnlyRepairAndKeepsRedo() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); auto *canvas = window->findChild<EditorCanvas *>(); drawRectangle(*canvas);
    QVERIFY(QDir().mkdir(path + ".xann")); int encodes = 0; EditorSaveHooks hooks;
    hooks.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); }; window->setSaveHooks(hooks);
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished); QTimer timer; bool reported = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || box->windowTitle() != "Save incomplete") return;
      reported = true; QVERIFY(box->text().contains("PNG saved")); QVERIFY(box->informativeText().contains("sidecar-save-failed")); box->accept();
    });
    timer.start(5); trigger(*window, "saveImage"); timer.stop(); QVERIFY(reported); const auto original = storedBytes(path);
    auto *retry = window->findChild<QAction *>("retryEditorSidecar"); QVERIFY(retry && retry->isEnabled()); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, "deleteSelection"); QVERIFY(!retry->isEnabled()); trigger(*window, "undo"); QVERIFY(retry->isEnabled()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    QTimer confirm; bool cancelled = false;
    connect(&confirm, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || box->objectName() != "retryEditorSidecarDialog") return;
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); cancelled = true; box->button(QMessageBox::Cancel)->click();
    });
    confirm.start(5); retry->trigger(); confirm.stop(); QVERIFY(cancelled); QCOMPARE(saved.count(), 1); QCOMPARE(encodes, 1); QCOMPARE(storedBytes(path), original);
    QVERIFY(QDir().rmdir(path + ".xann")); QTimer accept; bool confirmed = false;
    connect(&accept, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box && box->objectName() == "retryEditorSidecarDialog") { confirmed = true; box->button(QMessageBox::Retry)->click(); } });
    accept.start(5); retry->trigger(); accept.stop(); QVERIFY(confirmed); QCOMPARE(encodes, 1); QCOMPARE(storedBytes(path), original);
    QCOMPARE(saved.count(), 2); QVERIFY(saved.last()[0].toBool() && saved.last()[1].toBool()); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!retry->isEnabled()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled()); QCOMPARE(loadSource(path).document->annotations.size(), size_t(1));
    trigger(*window, "redo"); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
  }
  void partialSaveAsKeepsTheCurrentPathUntilItsCompanionRepairCompletes() {
    QTemporaryDir dir; const auto path = dir.filePath("current.png"), target = dir.filePath("chosen.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>());
    EditorSaveHooks hooks; bool collision = false; int encodes = 0;
    hooks.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); };
    hooks.beforePublish = [&](EditorSaveArtifact kind, const QString &chosen) { if (kind == EditorSaveArtifact::Raster) collision = QDir().mkdir(chosen + ".xann"); };
    window->setSaveHooks(hooks); QTimer timer; bool picked = false, reported = false;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); picker && !picked) {
        picked = true; auto *buttons = picker->findChild<QDialogButtonBox *>(); QVERIFY(buttons); buttons->button(QDialogButtonBox::Save)->setFocus();
        picker->selectFile(target); QCOMPARE(picker->selectedFiles().value(0), target); QMetaObject::invokeMethod(picker, "accept");
      }
      else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); box && box->windowTitle() == "Save incomplete") {
        reported = true; QVERIFY(box->informativeText().contains(target)); QVERIFY(box->informativeText().contains("PNG")); QCOMPARE(box->textFormat(), Qt::PlainText); box->accept();
      }
    });
    timer.start(5); trigger(*window, "saveAsImage"); timer.stop(); QVERIFY(picked && reported && collision); QVERIFY(window->windowTitle().startsWith("current.png"));
    QCOMPARE(storedBytes(path), original); QVERIFY(QFile::exists(target)); const auto rendered = storedBytes(target); QVERIFY(QDir().rmdir(target + ".xann"));
    bool confirmed = false; QTimer confirm;
    connect(&confirm, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box && box->objectName() == "retryEditorSidecarDialog") { confirmed = true; box->button(QMessageBox::Retry)->click(); } });
    confirm.start(5); trigger(*window, "retryEditorSidecar"); confirm.stop(); QVERIFY(confirmed); QCOMPARE(encodes, 1); QCOMPARE(storedBytes(target), rendered); QCOMPARE(storedBytes(path), original);
    QVERIFY(window->windowTitle().startsWith("chosen.png")); QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QVERIFY(loadSource(target).document);
  }
  void destructionDuringSaveCancelsTheOwnedWorkerWithoutWaitingOrPublishing() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path)); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); drawRectangle(*window->findChild<EditorCanvas *>());
    const auto gate = std::make_shared<SaveGate>(); window->setSaveHooks(gatedSave(gate)); QTimer timer; bool destroyed = false;
    connect(&timer, &QTimer::timeout, [&] { if (gate->entered.load() && !destroyed) { destroyed = true; window.reset(); } });
    timer.start(5); trigger(*window, "saveImage"); timer.stop(); QVERIFY(destroyed); QVERIFY(!gate->finished.load()); gate->released.store(true);
    QTRY_VERIFY(gate->finished.load()); QTRY_VERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
    QCOMPARE(storedBytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
  }
  void completionCanDestroyItsOwnerWithoutInvalidatingWorkerResults_data() {
    QTest::addColumn<bool>("onRestore"); QTest::newRow("acknowledgement") << false; QTest::newRow("busy restoration") << true;
  }
  void completionCanDestroyItsOwnerWithoutInvalidatingWorkerResults() {
    QFETCH(bool, onRestore); QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(fixture().save(path));
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); drawRectangle(*window->findChild<EditorCanvas *>()); bool acknowledged = false;
    connect(window.get(), &EditorWindow::saveFinished, this, [&](bool raster, bool sidecar, bool cancel) {
      QVERIFY(raster && sidecar && !cancel); acknowledged = true; if (!onRestore) window.reset();
    });
    auto *undo = window->findChild<QAction *>("undo");
    connect(undo, &QAction::changed, this, [&] { if (onRestore && acknowledged && undo->isEnabled()) window.reset(); });
    trigger(*window, "saveImage"); QVERIFY(acknowledged); QVERIFY(!window); const auto loaded = loadSource(path); QVERIFY(loaded.document); QCOMPARE(loaded.document->annotations.size(), size_t(1));
  }
  void saveRefusesAnUnavailableFormatWithoutEncodingOrChangingFiles() {
    QTemporaryDir dir; const auto path = dir.filePath("image.jpg"); QVERIFY(fixture().save(path, "JPEG")); const auto original = storedBytes(path);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("prefs/editor.json")).isEmpty()); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); drawRectangle(*window->findChild<EditorCanvas *>()); int encodes = 0; EditorSaveHooks hooks;
    hooks.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); }; window->setSaveHooks(hooks);
    QTimer timer; bool reported = false;
    connect(&timer, &QTimer::timeout, [&] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (box && box->windowTitle() == "Format unavailable") { reported = true; box->accept(); } });
    timer.start(5); trigger(*window, "saveImage"); timer.stop(); QVERIFY(reported); QCOMPARE(encodes, 0); QCOMPARE(storedBytes(path), original);
    QVERIFY(!QFile::exists(path + ".xann")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
  }
  void retainedAnnotatedDocumentOpensUnsavedAndPersistsAnEditablePair() {
    QTemporaryDir dir; QVERIFY(initializeNativeEditorPreferences(dir.filePath("settings/editor.json")).isEmpty());
    auto source = prepareCapturedSource(fixture()); QVERIFY(source.document); EditorSession session(*source.document); session.createRectangle({8, 8}, {22, 16});
    const auto document = session.document(); const auto original = serializeDocument(document);
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(document)); QVERIFY(window); window->show();
    QVERIFY(window->windowTitle().contains(QChar(0x2022))); QVERIFY(window->findChild<EditorCanvas *>());
    const auto target = dir.filePath("editable.png"); saveUntitled(*window, target);
    QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QVERIFY(QFile::exists(target + ".xann"));
    const auto loaded = loadSource(target); QVERIFY(loaded.document); QCOMPARE(loaded.document->annotations.size(), size_t(1));
    QCOMPARE(QImage::fromData(loaded.document->sourceImagePng, "PNG"), fixture());
    QCOMPARE(QImage(target), renderEditorImage(fixture(), document).image); QCOMPARE(serializeDocument(document), original);
  }
  void toolbarApplyKeepsImageCheckpointAndRedoWhileHiddenCommandsRemainReachable() {
    QTemporaryDir temporary; const auto image = temporary.filePath("input.png"), settings = temporary.filePath("native/settings.json");
    QVERIFY(fixture().save(image)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    std::unique_ptr<EditorWindow> one(EditorWindow::open(image)), two(EditorWindow::open(image)); QVERIFY(one && two);
    one->show(); two->show(); auto *canvas = one->findChild<EditorCanvas *>(); QVERIFY(canvas); drawRectangle(*canvas);
    trigger(*one, "saveImage"); trigger(*one, QString("canvasAction%1").arg(int(CanvasAction::RotateClockwise))); trigger(*one, "undo");
    const auto raster = storedBytes(image), sidecar = storedBytes(image + ".xann");
    QVERIFY(!raster.isEmpty() && !sidecar.isEmpty());
    const auto title = one->windowTitle(); const auto palette = one->palette();
    QVERIFY(one->findChild<QAction *>("undo")->isEnabled() && one->findChild<QAction *>("redo")->isEnabled());
    bool edited = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      const auto close = qScopeGuard([&] { if (dialog->isVisible()) dialog->reject(); });
      QCOMPARE(dialog->objectName(), QString("toolbarDialog")); QVERIFY(!one->close());
      auto *table = dialog->findChild<QTableWidget *>("toolbarItems"); QVERIFY(table); int undo = -1;
      for (int row = 0; row < table->rowCount(); ++row) {
        table->item(row, 1)->setCheckState(Qt::Unchecked);
        if (table->item(row, 0)->data(Qt::UserRole).value<QJsonObject>().value("Id") == QJsonValue("undo")) undo = row;
      }
      QVERIFY(undo >= 0); table->item(undo, 3)->setText("F6"); table->setCurrentCell(undo, 0);
      while (table->currentRow() > 0) dialog->findChild<QPushButton *>("toolbarUp")->click();
      dialog->findChild<QPushButton *>("applyToolbar")->click(); QVERIFY(!dialog->isVisible()); edited = true;
    });
    trigger(*one, "customizeToolbar"); QVERIFY(edited);
    QVERIFY(one->findChild<QToolBar *>("editorToolbar")->actions().isEmpty());
    QVERIFY(two->findChild<QToolBar *>("editorToolbar")->actions().isEmpty());
    QCOMPARE(storedBytes(image), raster); QCOMPARE(storedBytes(image + ".xann"), sidecar);
    QCOMPARE(one->windowTitle(), title); QCOMPARE(one->palette(), palette);
    QVERIFY(one->findChild<QAction *>("undo")->isEnabled() && one->findChild<QAction *>("redo")->isEnabled());
    const auto saved = nativeEditorPreferences().values.value("ToolbarItems").toArray(); QCOMPARE(saved.at(0).toObject().value("Id"), QJsonValue("undo"));
    for (const auto *name : {"saveImage", "undo", "redo", "closeEditor", "customizeToolbar", "toggleToolbars"}) {
      auto *action = one->findChild<QAction *>(name); QVERIFY(action && action->isVisible());
      bool menu = false; for (auto *object : action->associatedObjects()) menu = menu || qobject_cast<QMenu *>(object);
      QVERIFY(menu);
    }
    one->activateWindow(); canvas->setFocus(); QTRY_VERIFY(one->isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
    const auto shown = one->findChild<QAction *>("toggleToolbars")->isChecked();
    QTest::keyClick(canvas, Qt::Key_H, Qt::ControlModifier); QCOMPARE(one->findChild<QAction *>("toggleToolbars")->isChecked(), !shown);
    QTest::keyClick(canvas, Qt::Key_H, Qt::ControlModifier); QCOMPARE(one->findChild<QAction *>("toggleToolbars")->isChecked(), shown);
    QTest::keyClick(canvas, Qt::Key_F6); QVERIFY(!one->findChild<QAction *>("undo")->isEnabled()); QVERIFY(one->windowTitle().contains(QChar(0x2022)));
    QTest::keyClick(canvas, Qt::Key_Y, Qt::ControlModifier); QCOMPARE(one->windowTitle(), title); QVERIFY(one->findChild<QAction *>("redo")->isEnabled());
    QTest::keyClick(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(one->findChild<QStatusBar *>()->currentMessage().contains("40 × 60"));
    QCOMPARE(storedBytes(image), raster); QCOMPARE(storedBytes(image + ".xann"), sidecar);
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty()); std::unique_ptr<EditorWindow> restarted(EditorWindow::open(image)); QVERIFY(restarted);
    QVERIFY(restarted->findChild<QToolBar *>("editorToolbar")->actions().isEmpty());
    QCOMPARE(nativeEditorPreferences().values.value("ToolbarItems"), QJsonValue(saved));
  }
  void cancelledOrFailedToolbarDraftPreservesActualEditorHistory_data() {
    QTest::addColumn<bool>("fail"); QTest::newRow("cancel") << false; QTest::newRow("locked") << true;
  }
  void cancelledOrFailedToolbarDraftPreservesActualEditorHistory() {
    QFETCH(bool, fail); QTemporaryDir temporary; const auto image = temporary.filePath("input.png"), settings = temporary.filePath("native/settings.json");
    QVERIFY(fixture().save(image)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyToolbarPreferences(nativeEditorPreferences().values.value("ToolbarItems").toArray(), nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(image)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); drawRectangle(*canvas); trigger(*window, "saveImage");
    trigger(*window, QString("canvasAction%1").arg(int(CanvasAction::RotateClockwise))); trigger(*window, "undo");
    const auto before = storedBytes(settings), raster = storedBytes(image), sidecar = storedBytes(image + ".xann");
    QVERIFY(!before.isEmpty() && !raster.isEmpty() && !sidecar.isEmpty());
    const auto title = window->windowTitle();
    const auto layout = window->findChild<QToolBar *>("editorToolbar")->actions(); const auto revision = nativeEditorPreferenceRevision();
    QLockFile lock(settings + ".lock"); if (fail) QVERIFY(lock.tryLock(0)); bool edited = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      const auto close = qScopeGuard([&] { if (dialog->isVisible()) dialog->reject(); });
      auto *table = dialog->findChild<QTableWidget *>("toolbarItems"); QVERIFY(table);
      table->item(0, 1)->setCheckState(Qt::Unchecked); table->item(0, 3)->setText("F6");
      if (fail) { dialog->findChild<QPushButton *>("applyToolbar")->click(); QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QLabel *>("toolbarError")->text().isEmpty()); }
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click(); edited = true;
    });
    trigger(*window, "customizeToolbar"); QVERIFY(edited);
    QCOMPARE(window->findChild<QToolBar *>("editorToolbar")->actions(), layout); QCOMPARE(window->windowTitle(), title);
    QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("redo")->isEnabled());
    QCOMPARE(storedBytes(settings), before); QCOMPARE(storedBytes(image), raster); QCOMPARE(storedBytes(image + ".xann"), sidecar);
    QCOMPARE(nativeEditorPreferenceRevision(), revision); trigger(*window, "redo");
    QVERIFY(window->findChild<QStatusBar *>()->currentMessage().contains("40 × 60"));
  }
  void appearanceApplyUpdatesActualEditorsWithoutEditingPixelsCheckpointOrRedo() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), first = dir.filePath("first.png"), second = dir.filePath("second.png");
    QVERIFY(fixture().save(first)); QVERIFY(fixture().save(second)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyAppearancePreferences({{"Theme", "Dark"}, {"UseSystemTheme", false}, {"UseSystemAccentColor", false}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> one(EditorWindow::open(first)), two(EditorWindow::open(second)); QVERIFY(one && two); one->show(); two->show();
    QApplication::processEvents(); auto *canvas = one->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); trigger(*one, "saveImage");
    trigger(*one, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise))); trigger(*one, "undo");
    const auto raster = storedBytes(first), sidecar = storedBytes(first + ".xann"), preferences = storedBytes(settings);
    const auto title = one->windowTitle(); const auto palette = one->palette(); const auto defaults = drawingStyleFromPreferences(nativeEditorPreferences());
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool applied = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      QCOMPARE(dialog->objectName(), QStringLiteral("appearanceDialog")); QVERIFY(!one->close());
      dialog->findChild<QCheckBox *>("useSystemTheme")->setChecked(false);
      dialog->findChild<QComboBox *>("manualTheme")->setCurrentIndex(1);
      dialog->findChild<QCheckBox *>("useSystemAccent")->setChecked(false);
      dialog->findChild<QLineEdit *>("manualAccent")->setText("#80006000");
      QCOMPARE(one->palette(), palette); QCOMPARE(storedBytes(settings), preferences);
      applied = true; dialog->findChild<QPushButton *>("applyAppearance")->click();
    });
    timer.start(5); trigger(*one, "manageAppearance"); timer.stop(); QVERIFY(applied && !timedOut);
    QCOMPARE(one->palette().color(QPalette::Window), QColor(240, 240, 240)); QCOMPARE(one->palette(), two->palette());
    QCOMPARE(nativeEditorPreferences().values.value("AccentColorHex"), QJsonValue("#80006000"));
    QCOMPARE(one->windowTitle(), title); QVERIFY(one->findChild<QAction *>("undo")->isEnabled() && one->findChild<QAction *>("redo")->isEnabled());
    QCOMPARE(storedBytes(first), raster); QCOMPARE(storedBytes(first + ".xann"), sidecar); QCOMPARE(QImage(second), fixture()); QVERIFY(!QFile::exists(second + ".xann"));
    QCOMPARE(drawingStyleFromPreferences(nativeEditorPreferences()).strokeColor, defaults.strokeColor);
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    std::unique_ptr<EditorWindow> restarted(EditorWindow::open(second)); QVERIFY(restarted); QCOMPARE(restarted->palette(), two->palette());
    trigger(*one, "redo"); QVERIFY(one->statusBar()->currentMessage().startsWith("40 × 60"));
  }
  void cancelledAppearanceAndFailedApplyKeepActualEditorPaletteAndHistory() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyAppearancePreferences({{"Theme", "Dark"}, {"UseSystemTheme", false}, {"UseSystemAccentColor", false}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); QApplication::processEvents();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    const auto title = window->windowTitle(); const auto palette = window->palette(); const auto before = storedBytes(settings); const auto revision = nativeEditorPreferenceRevision();
    for (bool locked : {false, true}) {
      QLockFile lock(settings + ".lock"); if (locked) QVERIFY(lock.tryLock(0));
      QTimer timer; QElapsedTimer deadline; deadline.start(); bool configured = false, timedOut = false;
      connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
        if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
        dialog->findChild<QCheckBox *>("useSystemTheme")->setChecked(false); dialog->findChild<QComboBox *>("manualTheme")->setCurrentIndex(1);
        if (locked) {
          dialog->findChild<QPushButton *>("applyAppearance")->click();
          QVERIFY(dialog->isVisible()); QVERIFY(dialog->findChild<QLabel *>("appearanceError")->text().contains("Another process"));
        }
        configured = true; dialog->reject();
      });
      timer.start(5); trigger(*window, "manageAppearance"); timer.stop(); QVERIFY(configured && !timedOut);
      QCOMPARE(window->palette(), palette); QCOMPARE(window->windowTitle(), title); QCOMPARE(storedBytes(settings), before);
      QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(window->findChild<QAction *>("undo")->isEnabled());
    }
    QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(QImage(path), fixture());
    trigger(*window, "undo"); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }
  void recoveringTheActualEditorFramePreservesPixelsCheckpointAndRedo() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show(); QApplication::processEvents();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); trigger(*window, QStringLiteral("saveImage"));
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise))); trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("redo")->isEnabled());
    const auto raster = storedBytes(path), sidecar = storedBytes(path + ".xann"), preferences = storedBytes(settings);
    const auto revision = nativeEditorPreferenceRevision(); window->move(-3000, -2000);
    window->findChild<NativeEditorWindowState *>()->recoverPlacement();
    QVERIFY(window->screen()->availableGeometry().contains(window->frameGeometry()));
    // Exercise the complete menu/toolbar/canvas layout through the same native
    // display port, including a work area smaller than the preferred minimum.
    NativeEditorWindowState constrained(*window, {300, 200}, [] { return NativeWindowDisplays{{QRectF(0, 0, 320, 210)}, 0}; });
    constrained.recoverPlacement();
    QVERIFY(QRectF(0, 0, 320, 210).contains(QRectF(window->frameGeometry())));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("redo")->isEnabled());
    QCOMPARE(storedBytes(path), raster); QCOMPARE(storedBytes(path + ".xann"), sidecar); QCOMPARE(storedBytes(settings), preferences);
    QCOMPARE(nativeEditorPreferenceRevision(), revision);
    trigger(*window, QStringLiteral("redo")); QVERIFY(window->statusBar()->currentMessage().startsWith("40 × 60"));
  }
  void cancelledCloseDoesNotSaveWindowStateAndAcceptedCloseRestoresIt() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    const auto original = storedBytes(settings);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->show(); QApplication::processEvents();
    QCOMPARE(storedBytes(settings), original); // Showing/recovery never writes.
    window->resize(700, 500); auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelled = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (deadline.elapsed() > 5000) { timedOut = true; box->reject(); return; }
      QCOMPARE(box->windowTitle(), QStringLiteral("Unsaved changes"));
      QCloseEvent nested; QApplication::sendEvent(window.get(), &nested); QVERIFY(!nested.isAccepted()); QVERIFY(window->isVisible());
      cancelled = true; box->button(QMessageBox::Cancel)->click();
    });
    timer.start(5); QVERIFY(!window->close()); timer.stop(); QVERIFY(cancelled && !timedOut);
    QCOMPARE(storedBytes(settings), original); QVERIFY(window->isVisible());
    QVERIFY(window->windowTitle().contains(QChar(0x2022))); QVERIFY(window->findChild<QAction *>("undo")->isEnabled());
    const auto actual = window->findChild<NativeEditorWindowState *>()->snapshot();
    QTimer::singleShot(0, [] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
      box->button(QMessageBox::Discard)->click();
    });
    QVERIFY(window->close()); QVERIFY(!window->isVisible());
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QCOMPARE(nativeEditorPreferences().values.value("WindowWidth"), actual.value("WindowWidth"));
    QCOMPARE(nativeEditorPreferences().values.value("WindowHeight"), actual.value("WindowHeight"));
    QVERIFY(!windowPreferencesFromPreferences(nativeEditorPreferences()).maximized);
    QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(QImage(path), fixture()); window.reset();
    std::unique_ptr<EditorWindow> restored(EditorWindow::open(path)); QVERIFY(restored); restored->show(); QApplication::processEvents();
    QCOMPARE(restored->size(), QSize(actual.value("WindowWidth").toInt(), actual.value("WindowHeight").toInt()));
  }
  void windowMemoryMenuRollsBackFailedWritesAndUpdatesAllEditorActions() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), first = dir.filePath("first.png"), second = dir.filePath("second.png");
    QVERIFY(fixture().save(first)); QVERIFY(fixture().save(second)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> one(EditorWindow::open(first)), two(EditorWindow::open(second)); QVERIFY(one && two);
    for (auto *window : {one.get(), two.get()}) { window->setAttribute(Qt::WA_DeleteOnClose, false); window->show(); }
    QApplication::processEvents();
    auto *action = one->findChild<QAction *>("rememberWindowState"), *other = two->findChild<QAction *>("rememberWindowState");
    QVERIFY(action && other && action->isChecked() && other->isChecked()); const auto before = storedBytes(settings);
    const auto geometry = one->geometry(); QLockFile lock(settings + ".lock"); QVERIFY(lock.tryLock(0));
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool warned = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (deadline.elapsed() > 5000) { timedOut = true; box->reject(); return; }
      QCOMPARE(box->windowTitle(), QStringLiteral("Preferences not saved")); QVERIFY(!one->close());
      QVERIFY(box->text().contains("Another process")); warned = true; box->accept();
    });
    timer.start(5); action->trigger(); timer.stop(); QVERIFY(warned && !timedOut);
    QVERIFY(action->isChecked() && other->isChecked()); QCOMPARE(storedBytes(settings), before); lock.unlock();
    action->trigger(); QVERIFY(!action->isChecked() && !other->isChecked()); QCOMPARE(one->geometry(), geometry);
    QVERIFY(!one->windowTitle().contains(QChar(0x2022))); QVERIFY(!one->findChild<QAction *>("undo")->isEnabled());
    const auto disabled = storedBytes(settings); const auto revision = nativeEditorPreferenceRevision();
    one->resize(700, 500); two->resize(650, 450); QVERIFY(one->close()); QVERIFY(two->close());
    QCOMPARE(storedBytes(settings), disabled); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    action->trigger(); QVERIFY(action->isChecked() && other->isChecked());
    const auto snapshot = one->findChild<NativeEditorWindowState *>()->snapshot();
    QCOMPARE(nativeEditorPreferences().values.value("WindowWidth"), snapshot.value("WindowWidth"));
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty()); QVERIFY(windowPreferencesFromPreferences(nativeEditorPreferences()).remember);
  }
  void failedWindowPersistenceIsVisibleAndAllowsClosingWithoutChangingTheFile() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->show(); QApplication::processEvents(); window->resize(700, 500);
    const auto before = storedBytes(settings); const auto revision = nativeEditorPreferenceRevision();
    QLockFile lock(settings + ".lock"); QVERIFY(lock.tryLock(0));
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool warned = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (deadline.elapsed() > 5000) { timedOut = true; box->reject(); return; }
      QCOMPARE(box->windowTitle(), QStringLiteral("Window preferences not saved"));
      QVERIFY(box->text().contains("Another process"));
      QCloseEvent nested; QApplication::sendEvent(window.get(), &nested); QVERIFY(!nested.isAccepted()); QVERIFY(window->isVisible());
      warned = true; box->accept();
    });
    timer.start(5); QVERIFY(window->close()); timer.stop(); QVERIFY(warned && !timedOut);
    QCOMPARE(storedBytes(settings), before); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(!QFile::exists(path + ".xann"));
    lock.unlock(); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QCOMPARE(windowPreferencesFromPreferences(nativeEditorPreferences()).normalSize, QSizeF(640, 480));
  }
  void savedDefaultsAffectOnlyNewAnnotationsAcrossOpenEditorsAndRestart() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json");
    const QString first = dir.filePath("first.png"), second = dir.filePath("second.png"), replacement = dir.filePath("replacement.png");
    QVERIFY(fixture().save(first)); QVERIFY(fixture().save(second)); QVERIFY(fixture().save(replacement));
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    std::unique_ptr<EditorWindow> one(EditorWindow::open(first)), two(EditorWindow::open(second)); QVERIFY(one && two);
    for (auto *window : {one.get(), two.get()}) {
      window->resize(900, 600); window->show();
      auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
      trigger(*window, QStringLiteral("saveImage")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    }
    auto *other = two->findChild<EditorCanvas *>();
    QTest::mousePress(other->viewport(), Qt::LeftButton, Qt::NoModifier, other->viewState().toView({38, 28}).toPoint());
    QTest::mouseRelease(other->viewport(), Qt::LeftButton, Qt::NoModifier, other->viewState().toView({52, 37}).toPoint());
    trigger(*two, QStringLiteral("undo")); QVERIFY(!two->windowTitle().contains(QChar(0x2022)));
    QVERIFY(two->findChild<QAction *>("redo")->isEnabled());
    const auto firstBytes = storedBytes(first + ".xann"), secondBytes = storedBytes(second + ".xann");
    changeDrawingDefaults(*one, "#40123456", "#008899aa", 9, false);
    QVERIFY(!QFile::exists(settings)); QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(4));
    changeDrawingDefaults(*one, "#40123456", "#008899aa", 9);
    QCOMPARE(storedBytes(first + ".xann"), firstBytes); QCOMPARE(storedBytes(second + ".xann"), secondBytes);
    QVERIFY(!one->windowTitle().contains(QChar(0x2022))); QVERIFY(!two->windowTitle().contains(QChar(0x2022)));
    QVERIFY(two->findChild<QAction *>("redo")->isEnabled()); QVERIFY(one->findChild<QAction *>("deleteSelection")->isEnabled());
    trigger(*two, QStringLiteral("redo")); trigger(*two, QStringLiteral("saveImage"));
    const auto unchanged = loadSource(second); QVERIFY(unchanged.document); QCOMPARE(unchanged.document->annotations.size(), std::size_t{2});
    for (const auto &annotation : unchanged.document->annotations)
      QCOMPARE(std::get<RectangleAnnotation>(annotation).style.strokeWidth, 4.0);
    QTest::mousePress(other->viewport(), Qt::LeftButton, Qt::NoModifier, other->viewState().toView({35, 2}).toPoint());
    QTest::mouseRelease(other->viewport(), Qt::LeftButton, Qt::NoModifier, other->viewState().toView({50, 10}).toPoint());
    trigger(*two, QStringLiteral("saveImage"));
    const auto extended = loadSource(second); QVERIFY(extended.document); QCOMPARE(extended.document->annotations.size(), std::size_t{3});
    const auto &created = std::get<RectangleAnnotation>(extended.document->annotations.back());
    QCOMPARE(created.style.strokeWidth, 9.0); QCOMPARE(created.style.strokeColor, Argb{0x40123456});
    QCOMPARE(created.style.fillColor, Argb{0x008899AA});
    QVERIFY(one->openImage(replacement)); drawRectangle(*one->findChild<EditorCanvas *>());
    trigger(*one, QStringLiteral("saveImage"));
    auto replaced = loadSource(replacement); QVERIFY(replaced.document);
    QCOMPARE(std::get<RectangleAnnotation>(replaced.document->annotations.front()).style.strokeWidth, 9.0);
    one.reset(); two.reset();
    QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    const QString restarted = dir.filePath("restart.png"); QVERIFY(fixture().save(restarted));
    std::unique_ptr<EditorWindow> restart(EditorWindow::open(restarted)); QVERIFY(restart); restart->resize(900, 600); restart->show();
    auto *canvas = restart->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); trigger(*restart, QStringLiteral("saveImage"));
    const auto persisted = loadSource(restarted); QVERIFY(persisted.document);
    const auto &style = std::get<RectangleAnnotation>(persisted.document->annotations.front()).style;
    QCOMPARE(style.strokeWidth, 9.0); QCOMPARE(style.strokeColor, Argb{0x40123456}); QCOMPARE(style.fillColor, Argb{0x008899AA});
  }
  void toolbarPreferenceFailurePreservesExplicitSelectionEditAndHistory() {
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); trigger(*window, QStringLiteral("saveImage"));
    const auto originalBytes = storedBytes(settings); QLockFile lock(settings + ".lock"); QVERIFY(lock.tryLock(0));
    trigger(*window, QStringLiteral("increaseStroke"));
    QVERIFY(window->windowTitle().contains(QChar(0x2022))); QCOMPARE(storedBytes(settings), originalBytes);
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(7));
    trigger(*window, QStringLiteral("saveImage"));
    const auto edited = loadSource(path); QVERIFY(edited.document);
    QCOMPARE(std::get<RectangleAnnotation>(edited.document->annotations.front()).style.strokeWidth, 8.0);
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    canvas->zoomIn(); canvas->setFocus(); QApplication::processEvents();
    QVERIFY(window->statusBar()->currentMessage().contains("Drawing defaults could not be saved"));
    trigger(*window, QStringLiteral("undo")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("redo")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    lock.unlock(); changeDrawingDefaults(*window, "#FFFF0000", "#00000000", 9);
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!window->statusBar()->currentMessage().contains("Drawing defaults could not be saved"));
    const auto stillEdited = loadSource(path); QVERIFY(stillEdited.document);
    QCOMPARE(std::get<RectangleAnnotation>(stillEdited.document->annotations.front()).style.strokeWidth, 8.0);
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(9));
  }
  void toolbarColorChoicePersistsAlphaWhileCancelLeavesEverythingUnchanged_data() {
    QTest::addColumn<QString>("action"); QTest::addColumn<bool>("apply");
    for (const auto *action : {"strokeColour", "fillColour"}) for (bool apply : {false, true})
      QTest::newRow(qPrintable(QString("%1-%2").arg(action, apply ? "apply" : "cancel"))) << QString(action) << apply;
  }
  void toolbarColorChoicePersistsAlphaWhileCancelLeavesEverythingUnchanged() {
    QFETCH(QString, action); QFETCH(bool, apply);
    QTemporaryDir dir; const QString settings = dir.filePath("native/settings.json"), path = dir.filePath("image.png");
    QVERIFY(fixture().save(path)); QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); trigger(*window, QStringLiteral("saveImage"));
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool chosen = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      if (chosen) return; chosen = true; QVERIFY(dialog->testOption(QColorDialog::ShowAlphaChannel));
      QVERIFY(!window->close());
      dialog->setCurrentColor(QColor::fromRgba(0x40667788)); apply ? dialog->accept() : dialog->reject();
    });
    timer.start(5); trigger(*window, action); timer.stop(); QVERIFY(chosen && !timedOut);
    QCOMPARE(window->windowTitle().contains(QChar(0x2022)), apply); QCOMPARE(QFile::exists(settings), apply);
    if (apply) {
      const QString field = action == u"strokeColour" ? QStringLiteral("BorderColorHex") : QStringLiteral("FillColorHex");
      QCOMPARE(nativeEditorPreferences().values.value(field), QJsonValue("#40667788"));
      trigger(*window, QStringLiteral("saveImage")); const auto saved = loadSource(path); QVERIFY(saved.document);
      const auto &style = std::get<RectangleAnnotation>(saved.document->annotations.front()).style;
      QCOMPARE(action == u"strokeColour" ? style.strokeColor : style.fillColor, Argb{0x40667788});
      QVERIFY(initializeNativeEditorPreferences(settings).isEmpty());
      QCOMPARE(nativeEditorPreferences().values.value(field), QJsonValue("#40667788"));
    }
  }
  void actualOpenAndCompleteSavePersistRecentImagesAcrossRestart() {
    QTemporaryDir dir; const QString preferences = dir.filePath("native/settings.json");
    const QString first = dir.filePath("first.png"), second = dir.filePath("second.png");
    QVERIFY(fixture().save(first)); QVERIFY(fixture().save(second));
    QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty());
    { std::unique_ptr<EditorWindow> prior(EditorWindow::open(first)); QVERIFY(prior); }
    QVERIFY(!QFile::exists(preferences)); // Opening before opt-in leaves no path data.
    auto draft = recentImageHistory(); const auto revision = draft.revision();
    QVERIFY(draft.setPreferences(true, 2)); QVERIFY(applyRecentImagePreferences(draft, revision).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(second)); QVERIFY(window); window->resize(900, 600); window->show();
    QCOMPARE(recentImageHistory().paths(), QStringList{second});
    draft = recentImageHistory(); const auto beforeClear = draft.revision(); draft.clear();
    QVERIFY(applyRecentImagePreferences(draft, beforeClear).saved);
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    trigger(*window, QStringLiteral("saveImage")); QVERIFY(QFile::exists(second + ".xann"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QCOMPARE(recentImageHistory().paths(), QStringList{second});
    QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty()); QCOMPARE(recentImageHistory().paths(), QStringList{second});
    QVERIFY(recentImageHistory().enabled()); QCOMPARE(recentImageHistory().maximum(), 2);
  }
  void recentPreferenceWriteFailureDoesNotUndoSuccessfulImageOpenOrSave() {
    QTemporaryDir dir; const QString preferences = dir.filePath("native/settings.json");
    const QString first = dir.filePath("first.png"), second = dir.filePath("second.png");
    QVERIFY(fixture().save(first)); QVERIFY(fixture().save(second));
    QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty());
    auto draft = recentImageHistory(); QVERIFY(draft.setPreferences(true, 10));
    QVERIFY(applyRecentImagePreferences(draft, recentImageHistory().revision()).saved);
    { std::unique_ptr<EditorWindow> prior(EditorWindow::open(first)); QVERIFY(prior); }
    QFile preferenceFile(preferences); QVERIFY(preferenceFile.open(QIODevice::ReadOnly));
    const QByteArray before = preferenceFile.readAll(); preferenceFile.close();
    QLockFile lock(preferences + ".lock"); QVERIFY(lock.tryLock(0));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(second)); QVERIFY(window); window->resize(900, 600); window->show();
    QVERIFY(window->statusBar()->currentMessage().contains("Recent images could not be saved:"));
    QCOMPARE(recentImageHistory().paths(), QStringList{first});
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    QVERIFY(window->statusBar()->currentMessage().contains("Recent images could not be saved:"));
    trigger(*window, QStringLiteral("saveImage"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QVERIFY(QFile::exists(second + ".xann"));
    QVERIFY(window->statusBar()->currentMessage().contains("Recent images could not be saved:"));
    QCOMPARE(recentImageHistory().paths(), QStringList{first});
    QVERIFY(preferenceFile.open(QIODevice::ReadOnly)); QCOMPARE(preferenceFile.readAll(), before); preferenceFile.close();
    lock.unlock(); QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty()); QCOMPARE(recentImageHistory().paths(), QStringList{first});
  }
  void incompleteImageSaveDoesNotRecordAPersistedRecentPath() {
    QTemporaryDir dir; const QString preferences = dir.filePath("native/settings.json"), image = dir.filePath("image.png");
    QVERIFY(fixture().save(image)); QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty());
    auto draft = recentImageHistory(); QVERIFY(draft.setPreferences(true, 10));
    QVERIFY(applyRecentImagePreferences(draft, recentImageHistory().revision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(image)); QVERIFY(window); window->resize(900, 600); window->show();
    draft = recentImageHistory(); const auto revision = draft.revision(); draft.clear();
    QVERIFY(applyRecentImagePreferences(draft, revision).saved);
    QFile file(preferences); QVERIFY(file.open(QIODevice::ReadOnly)); const QByteArray before = file.readAll(); file.close();
    QVERIFY(QDir().mkdir(image + ".xann"));
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    bool reported = false;
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      reported = box->windowTitle() == u"Save incomplete"; box->accept();
    });
    timer.start(5); trigger(*window, QStringLiteral("saveImage")); timer.stop(); QVERIFY(reported); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(recentImageHistory().paths().isEmpty()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
    QVERIFY(initializeNativeEditorPreferences(preferences).isEmpty()); QVERIFY(recentImageHistory().paths().isEmpty());
  }
  void nativeRecentPreferencesAreOptInAtomicAndLeaveTheDocumentAlone_data() {
    QTest::addColumn<bool>("apply"); QTest::newRow("cancel") << false; QTest::newRow("apply") << true;
  }
  void nativeRecentPreferencesAreOptInAtomicAndLeaveTheDocumentAlone() {
    QFETCH(bool, apply); QTemporaryDir dir; const QString path = dir.filePath("current.png"); QVERIFY(fixture().save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); canvas->zoomIn();
    const QString status = window->statusBar()->currentMessage(), title = window->windowTitle();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    QVERIFY(!recentImageHistory().enabled()); QVERIFY(recentImageHistory().paths().isEmpty());
    auto *menu = window->findChild<QMenu *>(QStringLiteral("recentImagesMenu")); QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
    QTimer::singleShot(0, [&, apply] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); QCOMPARE(dialog->objectName(), QStringLiteral("recentImagesDialog"));
      QVERIFY(!window->close()); auto *enabled = dialog->findChild<QCheckBox *>(QStringLiteral("rememberRecentImages")); QVERIFY(enabled && !enabled->isChecked());
      enabled->setChecked(true); dialog->findChild<QSpinBox *>(QStringLiteral("maximumRecentImages"))->setValue(2);
      QVERIFY(!recentImageHistory().enabled());
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      buttons->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    trigger(*window, QStringLiteral("manageRecentImages"));
    QCOMPARE(recentImageHistory().enabled(), apply); QCOMPARE(recentImageHistory().maximum(), apply ? 2 : 10);
    QVERIFY(recentImageHistory().paths().isEmpty()); // Explicit opt-in does not backfill prior openings.
    QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    QVERIFY(window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
  }
  void recentMenuUsesDirtyConfirmationAndKeepsMissingEntriesRemovable() {
    QTemporaryDir dir; const QString current = dir.filePath("current.png"), target = dir.filePath("one&two.png");
    const QImage original = fixture(); QVERIFY(original.save(current)); QImage other(20, 15, QImage::Format_ARGB32); other.fill(Qt::green); QVERIFY(other.save(target));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(current)); QVERIFY(window); window->resize(900, 600); window->show();
    QVERIFY(recentImageHistory().setPreferences(true, 2)); QVERIFY(recentImageHistory().paths().isEmpty());
    { std::unique_ptr<EditorWindow> second(EditorWindow::open(target)); QVERIFY(second); }
    { std::unique_ptr<EditorWindow> second(EditorWindow::open(target)); QVERIFY(second); }
    QCOMPARE(recentImageHistory().paths(), QStringList{target});
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); canvas->zoomIn();
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    auto *menu = window->findChild<QMenu *>(QStringLiteral("recentImagesMenu")); QVERIFY(menu); QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
    auto *action = menu->findChild<QAction *>(QStringLiteral("recentImage0")); QVERIFY(action);
    QCOMPARE(action->data().toString(), target); QVERIFY(action->text().contains(QStringLiteral("one&&two.png")));
    QTimer timer; int confirmation = int(QMessageBox::Cancel); bool confirmed = false, reported = false;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        if (box->windowTitle() == u"Replace current image") { confirmed = true; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); box->button(QMessageBox::StandardButton(confirmation))->click(); }
        else { reported = box->windowTitle() == u"Cannot open image"; box->accept(); }
      }
    });
    timer.start(5); action->trigger(); timer.stop(); QVERIFY(confirmed && !reported);
    QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset); QCOMPARE(recentImageHistory().paths(), QStringList{target});
    confirmation = int(QMessageBox::Discard); confirmed = false; timer.start(5); action->trigger(); timer.stop(); QVERIFY(confirmed);
    QVERIFY(window->statusBar()->currentMessage().startsWith("20 × 15")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled()); QCOMPARE(QImage(current), original); QVERIFY(!QFile::exists(current + ".xann"));
    QVERIFY(QFile::remove(target)); const QString adoptedTitle = window->windowTitle(), adoptedStatus = window->statusBar()->currentMessage();
    QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow")); action = menu->findChild<QAction *>(QStringLiteral("recentImage0")); QVERIFY(action);
    confirmed = false; timer.start(5); action->trigger(); timer.stop(); QVERIFY(reported && !confirmed);
    QCOMPARE(window->windowTitle(), adoptedTitle); QCOMPARE(window->statusBar()->currentMessage(), adoptedStatus); QCOMPARE(recentImageHistory().paths(), QStringList{target});
    QVERIFY(recentImageHistory().remove(target)); QVERIFY(recentImageHistory().paths().isEmpty());
  }
  void recentEntryPreservesTheOpenedSymlinkParentSource() {
    QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("outside/child")));
    QVERIFY(fixture().save(dir.filePath("one.png")));
    QImage outside(20, 15, QImage::Format_ARGB32); outside.fill(Qt::green); QVERIFY(outside.save(dir.filePath("outside/one.png")));
    QVERIFY(QFile::link(dir.filePath("outside/child"), dir.filePath("linked")));
    const QString chosen = dir.filePath("linked/../one.png"); QVERIFY(recentImageHistory().setPreferences(true, 10));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(chosen)); QVERIFY(window); window->show();
    QVERIFY(window->statusBar()->currentMessage().startsWith("20 × 15")); QCOMPARE(recentImageHistory().paths(), QStringList{chosen});
    auto *menu = window->findChild<QMenu *>(QStringLiteral("recentImagesMenu")); QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
    auto *action = menu->findChild<QAction *>(QStringLiteral("recentImage0")); QVERIFY(action); QCOMPARE(action->data().toString(), chosen);
    action->trigger(); QVERIFY(window->statusBar()->currentMessage().startsWith("20 × 15")); QCOMPARE(recentImageHistory().paths(), QStringList{chosen});
    QCOMPARE(QImage(chosen), outside);
  }
  void nativeRecentRemovalAndLimitChangesApplyOrCancel_data() {
    QTest::addColumn<QString>("edit"); QTest::addColumn<bool>("apply");
    for (const QString &edit : {QStringLiteral("remove"), QStringLiteral("clear"), QStringLiteral("disable"), QStringLiteral("trim")})
      for (bool apply : {false, true}) QTest::newRow(qPrintable(edit + (apply ? "-apply" : "-cancel"))) << edit << apply;
  }
  void incomingOpenPreventsStaleRecentPreferencesFromOverwritingTheList() {
    QTemporaryDir dir; const QString current = dir.filePath("current.png"), incoming = dir.filePath("incoming.png");
    QVERIFY(fixture().save(current)); QVERIFY(fixture().save(incoming));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(current)); QVERIFY(window); window->show();
    QVERIFY(recentImageHistory().setPreferences(true, 10));
    auto *menu = window->findChild<QMenu *>(QStringLiteral("recentImagesMenu")); QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
    bool reported = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      dialog->findChild<QCheckBox *>(QStringLiteral("rememberRecentImages"))->setChecked(false);
      // Instance requests can open another editor inside the dialog's event loop.
      std::unique_ptr<EditorWindow> added(EditorWindow::open(incoming)); QVERIFY(added);
      QTimer::singleShot(0, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        reported = box->windowTitle() == u"Recent images changed"; box->accept();
      });
      dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
    });
    trigger(*window, QStringLiteral("manageRecentImages")); QVERIFY(reported);
    QVERIFY(recentImageHistory().enabled()); QCOMPARE(recentImageHistory().paths(), QStringList{incoming});
    QVERIFY(window->windowTitle().startsWith("current.png")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }
  void nativeRecentRemovalAndLimitChangesApplyOrCancel() {
    QFETCH(QString, edit); QFETCH(bool, apply); QTemporaryDir dir; const QString current = dir.filePath("current.png"); QVERIFY(fixture().save(current));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(current)); QVERIFY(window); window->show();
    auto &history = recentImageHistory(); QVERIFY(history.setPreferences(true, 3));
    for (const auto &path : QStringList{"/images/one.png", "/images/two.png", "/images/three.png"}) QVERIFY(history.remember(path));
    const QStringList before = history.paths(); const auto revision = history.revision();
    auto *menu = window->findChild<QMenu *>(QStringLiteral("recentImagesMenu")); QVERIFY(QMetaObject::invokeMethod(menu, "aboutToShow"));
    QTimer::singleShot(0, [&, apply, edit] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      auto *paths = dialog->findChild<QListWidget *>(QStringLiteral("recentImagePaths")); QVERIFY(paths); QCOMPARE(paths->count(), 3);
      if (edit == u"remove") { paths->setCurrentRow(1); dialog->findChild<QPushButton *>(QStringLiteral("removeRecentImage"))->click(); }
      else if (edit == u"clear") dialog->findChild<QPushButton *>(QStringLiteral("clearRecentImages"))->click();
      else if (edit == u"disable") dialog->findChild<QCheckBox *>(QStringLiteral("rememberRecentImages"))->setChecked(false);
      else dialog->findChild<QSpinBox *>(QStringLiteral("maximumRecentImages"))->setValue(1);
      QCOMPARE(history.paths(), before); QCOMPARE(history.revision(), revision);
      dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    trigger(*window, QStringLiteral("manageRecentImages"));
    if (!apply) { QCOMPARE(history.paths(), before); QCOMPARE(history.revision(), revision); QCOMPARE(history.maximum(), 3); QVERIFY(history.enabled()); }
    else if (edit == u"remove") QCOMPARE(history.paths(), QStringList({"/images/three.png", "/images/one.png"}));
    else if (edit == u"trim") { QCOMPARE(history.paths(), QStringList({"/images/three.png"})); QCOMPARE(history.maximum(), 1); }
    else { QVERIFY(history.paths().isEmpty()); QCOMPARE(history.enabled(), edit != u"disable"); }
    QVERIFY(window->statusBar()->currentMessage().startsWith("60 × 40 · 0 annotation(s)"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QCOMPARE(QImage(current), fixture());
  }
  void nativeOpenPickerConfirmsAndPreservesOrReplacesTheDirtySession_data() {
    QTest::addColumn<int>("choice");
    QTest::newRow("cancel") << int(QMessageBox::Cancel);
    QTest::newRow("save") << int(QMessageBox::Save);
    QTest::newRow("discard") << int(QMessageBox::Discard);
  }
  void nativeOpenPickerConfirmsAndPreservesOrReplacesTheDirtySession() {
    QFETCH(int, choice);
    QTemporaryDir dir; const QString path = dir.filePath("current.png"), target = dir.filePath("linear.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    QImage other(24, 18, QImage::Format_ARGB32); other.fill(qRgba(128, 0, 0, 80)); other.setColorSpace(QColorSpace::SRgbLinear); QVERIFY(other.save(target));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise))); trigger(*window, QStringLiteral("undo")); canvas->zoomIn();
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    bool picked = false, confirmed = false, timedOut = false; QTimer timer; QElapsedTimer deadline; deadline.start();
    connect(&timer, &QTimer::timeout, [&] {
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!modal) return;
      if (deadline.elapsed() > 5000) { timedOut = true; modal->reject(); return; }
      if (auto *picker = qobject_cast<QFileDialog *>(modal)) { picked = true; picker->selectFile(target); QMetaObject::invokeMethod(picker, "accept"); }
      else if (auto *box = qobject_cast<QMessageBox *>(modal)) { confirmed = true; QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel)); box->button(QMessageBox::StandardButton(choice))->click(); }
    });
    timer.start(5); trigger(*window, QStringLiteral("openImage")); timer.stop(); QVERIFY(picked && confirmed && !timedOut);
    QCOMPARE(window->findChild<EditorCanvas *>(), canvas); QCOMPARE(QImage(target), other); QCOMPARE(QImage(target).colorSpace(), other.colorSpace());
    if (choice == int(QMessageBox::Cancel)) {
      QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
      QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
      QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("redo")->isEnabled());
      QVERIFY(window->findChild<QAction *>("deleteSelection")->isEnabled());
      trigger(*window, QStringLiteral("redo")); QVERIFY(window->statusBar()->currentMessage().startsWith("40 × 60"));
      QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann"));
    } else {
      QVERIFY(window->windowTitle().startsWith("linear.png")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
      QVERIFY(window->statusBar()->currentMessage().startsWith("24 × 18 · 0 annotation(s) · 0 selected"));
      QVERIFY(!window->findChild<QAction *>("undo")->isEnabled() && !window->findChild<QAction *>("redo")->isEnabled());
      if (choice == int(QMessageBox::Save)) { const auto old = loadSource(path); QVERIFY(old.document); QCOMPARE(old.image, original); QCOMPARE(old.document->annotations.size(), std::size_t(1)); }
      else { QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann")); }
      // Saving the adopted source preserves encoded pixels, alpha and metadata.
      trigger(*window, QStringLiteral("saveImage")); const QImage saved(target); QCOMPARE(saved, other); QCOMPARE(saved.colorSpace(), other.colorSpace());
      canvas->resetZoom(); drawRectangle(*canvas); QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
      trigger(*window, QStringLiteral("undo")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    }
  }

  void cancelledPickerAndCorruptOpenPreserveTheCurrentSession_data() {
    QTest::addColumn<int>("failure");
    QTest::newRow("cancel-picker") << 0; QTest::newRow("unsupported-image") << 1; QTest::newRow("corrupt-sidecar") << 2;
  }
  void cancelledPickerAndCorruptOpenPreserveTheCurrentSession() {
    QFETCH(int, failure);
    QTemporaryDir dir; const QString path = dir.filePath("current.png"), target = dir.filePath("target.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    if (failure == 1) { QFile bad(target); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("bad"); }
    if (failure == 2) { QVERIFY(original.save(target)); QFile bad(target + ".xann"); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("corrupt"); }
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas); canvas->zoomIn();
    const QString status = window->statusBar()->currentMessage(), title = window->windowTitle();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    bool picked = false, reported = false, confirmed = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
        picked = true; if (!failure) picker->reject(); else { picker->selectFile(target); QMetaObject::invokeMethod(picker, "accept"); }
      } else if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        reported = box->windowTitle() == u"Cannot open image"; confirmed = box->windowTitle() == u"Replace current image"; box->reject();
      }
    });
    timer.start(5); trigger(*window, QStringLiteral("openImage")); timer.stop();
    QVERIFY(picked && !confirmed); QCOMPARE(reported, failure != 0);
    QCOMPARE(window->statusBar()->currentMessage(), status); QCOMPARE(window->windowTitle(), title);
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("deleteSelection")->isEnabled());
    QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann"));
    trigger(*window, QStringLiteral("undo")); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
  }

  void saveBeforeOpeningTheSameFileAdoptsTheJustSavedSourceAndSidecar() {
    QTemporaryDir dir; const QString path = dir.filePath("current.png"); const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    acceptDimensions(40, 20, 5, 6); trigger(*window, QStringLiteral("cropImage"));
    bool savedBeforeReplace = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        QCOMPARE(box->windowTitle(), QStringLiteral("Replace current image")); savedBeforeReplace = true; box->button(QMessageBox::Save)->click();
      }
    });
    timer.start(5); QVERIFY(window->openImage(path)); timer.stop(); QVERIFY(savedBeforeReplace);
    QVERIFY(window->statusBar()->currentMessage().startsWith("40 × 20 · 1 annotation(s) · 0 selected"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022))); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
    const auto persisted = loadSource(path); QVERIFY(persisted.document); QCOMPARE(persisted.image.size(), QSize(40, 20));
    QCOMPARE(persisted.image.pixel(0, 0), original.pixel(5, 6)); QCOMPARE(persisted.document->annotations.size(), std::size_t(1));
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    QVERIFY(window->statusBar()->currentMessage().startsWith("20 × 40")); trigger(*window, QStringLiteral("undo"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void cancellingAndClosingOpenWaitForTheSameLiveWorker() {
    QTemporaryDir dir; const QString path = dir.filePath("current.png"), target = dir.filePath("large.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    QImage large(4000, 4000, QImage::Format_ARGB32); large.fill(Qt::blue); QVERIFY(large.save(target));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    for (bool closeProgress : {false, true}) {
      QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelledLive = false;
      connect(&timer, &QTimer::timeout, [&] {
        auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); if (!progress) return;
        if ((progress->value() >= 9 && progress->value() < 100) || deadline.elapsed() > 5000) {
          cancelledLive = progress->value() >= 9 && progress->value() < 100;
          QVERIFY(!window->close()); timer.stop();
          if (closeProgress) progress->reject(); else progress->findChild<QPushButton *>()->click();
        }
      });
      timer.start(5); QVERIFY(!window->openImage(target)); timer.stop(); QVERIFY(cancelledLive);
      QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
      QVERIFY(window->findChild<QAction *>("undo")->isEnabled() && window->findChild<QAction *>("deleteSelection")->isEnabled());
    }
    QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(QImage(target), large);
  }

  void savingAnUntitledImageOverTheOpenCandidateReloadsTheSavedImage() {
    QTemporaryDir dir; const QString target = dir.filePath("candidate.png"); QVERIFY(fixture().save(target));
    QTimer::singleShot(0, [] {
      auto *dialog = qobject_cast<NewImageDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      configureNewImage(*dialog); dialog->accept();
    });
    std::unique_ptr<EditorWindow> window(EditorWindow::createNew()); QVERIFY(window); window->show();
    bool confirmed = false, picked = false; QTimer timer; QElapsedTimer deadline; deadline.start();
    connect(&timer, &QTimer::timeout, [&] {
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!modal) return;
      if (deadline.elapsed() > 5000) { modal->reject(); return; }
      if (auto *box = qobject_cast<QMessageBox *>(modal)) {
        if (box->windowTitle() == u"Replace current image") { confirmed = true; box->button(QMessageBox::Save)->click(); }
        else if (box->standardButtons() & QMessageBox::Yes) box->button(QMessageBox::Yes)->click();
      } else if (auto *picker = qobject_cast<QFileDialog *>(modal); picker && !picked) {
        picker->selectFile(target);
        // The file-system model loads the newly chosen directory asynchronously.
        if (picker->selectedFiles() != QStringList{target}) return;
        picked = true;
        // Let the recurring timer handle the native overwrite confirmation in
        // its nested loop, after this timeout's callback has returned.
        QTimer::singleShot(0, picker, [picker] { QMetaObject::invokeMethod(picker, "accept"); });
      }
    });
    timer.start(5); QVERIFY(window->openImage(target)); timer.stop(); QVERIFY(confirmed && picked);
    QVERIFY(window->statusBar()->currentMessage().startsWith("64 × 48")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
    const auto persisted = loadSource(target); QVERIFY(persisted.document); QCOMPARE(persisted.image.size(), QSize(64, 48));
    QCOMPARE(persisted.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
    QVERIFY(std::abs(qRed(persisted.image.pixel(0, 0)) - 55) <= 1);
  }

  void retainedCaptureOpensAnOwnedUntitledDocumentAndSavesOnlyToExplicitDestination() {
    QTemporaryDir dir; const auto original = fixture(); auto retained = original;
    const QString savedCapture = dir.filePath("capture.png"), destination = dir.filePath("edited.png"); QVERIFY(original.save(savedCapture));
    const auto captureBytes = storedBytes(savedCapture);
    QVERIFY(initializeNativeEditorPreferences(dir.filePath("preferences.json")).isEmpty());
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(retained)); QVERIFY(window); window->show();
    QVERIFY(window->windowTitle().startsWith("Untitled image")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(recentImageHistory().paths().isEmpty());
    retained = {}; saveUntitled(*window); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QCOMPARE(storedBytes(savedCapture), captureBytes); QVERIFY(!QFile::exists(savedCapture + ".xann"));
    saveUntitled(*window, destination); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    QCOMPARE(loadSource(destination).image, original); QCOMPARE(storedBytes(savedCapture), captureBytes);
    QVERIFY(!QFile::exists(destination + ".xann")); window->setAttribute(Qt::WA_DeleteOnClose, false); QVERIFY(window->close());
  }

  void cancelledOrFailedNewImagePreservesTheDirtySession_data() {
    QTest::addColumn<bool>("cancelWizard"); QTest::addColumn<int>("failure");
    QTest::newRow("invalid-and-cancelled-controls") << true << 0;
    QTest::newRow("cancel-replacement") << false << 0;
    QTest::newRow("failed-raster-save-before-replacement") << false << 1;
    QTest::newRow("failed-sidecar-save-before-replacement") << false << 2;
  }
  void cancelledOrFailedNewImagePreservesTheDirtySession() {
    QFETCH(bool, cancelWizard); QFETCH(int, failure); const bool failSave = failure != 0;
    QTemporaryDir dir; const QString path = dir.filePath("current.png"), backup = dir.filePath("backup.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom(); drawRectangle(*canvas);
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    trigger(*window, QStringLiteral("undo")); canvas->zoomIn();
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    const QPointF offset = canvas->viewState().offset(); const double zoom = canvas->viewState().zoom();
    auto *undo = window->findChild<QAction *>("undo"), *redo = window->findChild<QAction *>("redo");
    auto *remove = window->findChild<QAction *>("deleteSelection");
    QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    if (failure == 1) { QVERIFY(QFile::rename(path, backup)); QVERIFY(QDir().mkdir(path)); }
    if (failure == 2) QVERIFY(QDir().mkdir(path + ".xann"));
    replaceWithNewImage(*window, failSave ? QMessageBox::Save : QMessageBox::Cancel, cancelWizard, failSave);
    QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QCOMPARE(canvas->viewState().offset(), offset); QCOMPARE(canvas->viewState().zoom(), zoom);
    QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    if (failure == 2) {
      // The explicit Save persisted the raster, but no full checkpoint exists.
      QCOMPARE(QImage(path).size(), original.size()); QVERIFY(QImage(path) != original);
      QVERIFY(QDir(path + ".xann").exists()); QVERIFY(QDir().rmdir(path + ".xann"));
    } else { QCOMPARE(QImage(failure == 1 ? backup : path), original); QVERIFY(!QFile::exists(path + ".xann")); }
    if (failure == 1) { QVERIFY(QDir().rmdir(path)); QVERIFY(QFile::rename(backup, path)); }
    trigger(*window, QStringLiteral("redo")); QVERIFY(window->statusBar()->currentMessage().startsWith("40 × 60"));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("saveImage"));
    const auto saved = loadSource(path); QVERIFY(saved.document); QCOMPARE(saved.image, original);
    QCOMPARE(saved.document->annotations.size(), std::size_t(1));
  }

  void newImageReplacementResetsSessionBindingsAndSavesChosenProfile_data() {
    QTest::addColumn<int>("choice");
    QTest::newRow("save-current") << int(QMessageBox::Save);
    QTest::newRow("discard-current") << int(QMessageBox::Discard);
  }
  void newImageReplacementResetsSessionBindingsAndSavesChosenProfile() {
    QFETCH(int, choice);
    QTemporaryDir dir; const QString path = dir.filePath("current.png"), newPath = dir.filePath("new.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom(); drawRectangle(*canvas);
    auto *crop = window->findChild<QAction *>("cropTool"); crop->setChecked(true);
    replaceWithNewImage(*window, QMessageBox::StandardButton(choice));
    QCOMPARE(window->findChild<EditorCanvas *>(), canvas); QVERIFY(!crop->isChecked());
    QVERIFY(window->windowTitle().startsWith("Untitled image")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(window->statusBar()->currentMessage().startsWith("64 × 48 · 0 annotation(s) · 0 selected"));
    QVERIFY(!window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(!window->findChild<QAction *>("redo")->isEnabled());
    QVERIFY(!window->findChild<QAction *>("deleteSelection")->isEnabled());
    if (choice == int(QMessageBox::Save)) {
      const auto old = loadSource(path); QVERIFY(old.document); QCOMPARE(old.image, original);
      QCOMPARE(old.document->annotations.size(), std::size_t(1));
    } else { QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann")); }
    // The display shows interpreted sRGB gray while retaining linear source bytes.
    const QColor displayed = canvas->viewport()->grab().toImage().pixelColor(canvas->viewState().toView({32, 24}).toPoint());
    QVERIFY(std::abs(displayed.red() - 128) <= 1); QCOMPARE(displayed.alpha(), 255);
    canvas->resetZoom(); drawRectangle(*canvas);
    QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("redo"));
    QVERIFY(window->statusBar()->currentMessage().contains("1 annotation(s)"));
    trigger(*window, QStringLiteral("undo")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    QVERIFY(window->statusBar()->currentMessage().startsWith("48 × 64")); trigger(*window, QStringLiteral("undo"));
    saveUntitled(*window); QVERIFY(window->windowTitle().contains(QChar(0x2022))); QVERIFY(!QFile::exists(newPath));
    // Cancelling close, or its Save destination, keeps an untitled source dirty.
    for (const auto response : {QMessageBox::Cancel, QMessageBox::Save}) {
      bool confirmed = false, cancelledPicker = false; QTimer timer;
      connect(&timer, &QTimer::timeout, [&] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
          confirmed = true; QVERIFY(box->text().contains("Untitled image")); box->button(response)->click();
        } else if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) { cancelledPicker = true; picker->reject(); }
      });
      timer.start(5); QVERIFY(!window->close()); timer.stop(); QVERIFY(confirmed);
      QCOMPARE(cancelledPicker, response == QMessageBox::Save); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    }
    saveUntitled(*window, newPath); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    const auto fresh = loadSource(newPath); QVERIFY(fresh.document); QCOMPARE(fresh.image.size(), QSize(64, 48));
    QCOMPARE(fresh.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
    QVERIFY(std::abs(qRed(fresh.image.pixel(0, 0)) - 55) <= 1); QCOMPARE(qAlpha(fresh.image.pixel(0, 0)), 255);
    QVERIFY(fresh.document->annotations.empty()); QVERIFY(!QFile::exists(newPath + ".xann"));
    drawRectangle(*canvas); trigger(*window, QStringLiteral("saveImage"));
    const auto annotated = loadSource(newPath); QVERIFY(annotated.document);
    QCOMPARE(annotated.document->annotations.size(), std::size_t(1)); QCOMPARE(annotated.image, fresh.image);
    QCOMPARE(annotated.image.colorSpace(), fresh.image.colorSpace());
    window->setAttribute(Qt::WA_DeleteOnClose, false); QVERIFY(window->close());
  }

  void standaloneNewImageCanBeCancelledOrCreatedWithoutAnExistingFile() {
    bool configured = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<NewImageDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      configured = true; dialog->reject();
    });
    QVERIFY(!EditorWindow::createNew()); QVERIFY(configured); configured = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<NewImageDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      configured = true; configureNewImage(*dialog); dialog->accept();
    });
    std::unique_ptr<EditorWindow> window(EditorWindow::createNew()); QVERIFY(configured && window);
    QVERIFY(window->windowTitle().startsWith("Untitled image")); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
    QVERIFY(window->statusBar()->currentMessage().startsWith("64 × 48"));
  }

  void cancelledSaveBeforeReplacingAnUntitledImageKeepsItsSession() {
    QTimer::singleShot(0, [] {
      auto *dialog = qobject_cast<NewImageDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      configureNewImage(*dialog); dialog->accept();
    });
    std::unique_ptr<EditorWindow> window(EditorWindow::createNew()); QVERIFY(window); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); canvas->resetZoom(); drawRectangle(*canvas);
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    bool configured = false, confirmed = false, cancelledPicker = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *modal = QApplication::activeModalWidget();
      if (auto *dialog = qobject_cast<NewImageDialog *>(modal); dialog && !configured) {
        configured = true; configureNewImage(*dialog); dialog->findChild<QSpinBox *>("newImageWidth")->setValue(8); dialog->accept();
      } else if (auto *box = qobject_cast<QMessageBox *>(modal); box && !confirmed) {
        confirmed = true; QVERIFY(box->text().contains("Untitled image")); box->button(QMessageBox::Save)->click();
      } else if (auto *picker = qobject_cast<QFileDialog *>(modal)) { cancelledPicker = true; picker->reject(); }
    });
    timer.start(5); trigger(*window, QStringLiteral("newImage")); timer.stop();
    QVERIFY(configured && confirmed && cancelledPicker); QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QVERIFY(window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(window->findChild<QAction *>("deleteSelection")->isEnabled());
    trigger(*window, QStringLiteral("undo")); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled());
    QVERIFY(window->windowTitle().contains(QChar(0x2022)));
  }

  void comparisonPreservesDirtyHistorySelectionViewportAndFiles() {
    QTemporaryDir dir;
    const QString path = dir.filePath("current.png"), otherPath = dir.filePath("other.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    QImage other(40, 60, QImage::Format_ARGB32); other.fill(Qt::blue); QVERIFY(other.save(otherPath));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    trigger(*window, QStringLiteral("undo"));  // both undo and redo branches must survive comparison
    canvas->zoomIn();
    const QString title = window->windowTitle(), status = window->statusBar()->currentMessage();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    auto *undo = window->findChild<QAction *>("undo"), *redo = window->findChild<QAction *>("redo");
    auto *remove = window->findChild<QAction *>("deleteSelection");
    QVERIFY(undo && redo && remove); QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    bool loaded = false, exercised = false, timedOut = false;
    QTimer timer; QElapsedTimer deadline; deadline.start();
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<ImageComparisonDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      auto *first = dialog->findChild<QLabel *>("comparisonInput0"); QVERIFY(first);
      if (!loaded && first->text().startsWith("Current rendered image")) { loaded = dialog->loadFile(1, otherPath); }
      auto *slider = dialog->findChild<QSlider *>("comparisonDivider"); QVERIFY(slider);
      if (!slider->isEnabled()) return;
      dialog->findChild<QComboBox *>("comparisonAlignment")->setCurrentIndex(1);
      slider->setValue(75); QTest::keyClick(slider, Qt::Key_Left); QCOMPARE(slider->value(), 74);
      QVERIFY(!window->close());  // snapshots stay alive until the comparison closes
      exercised = true; dialog->reject();
    });
    timer.start(10); trigger(*window, QStringLiteral("compareImages")); timer.stop();
    QVERIFY(loaded && exercised && !timedOut);
    QCOMPARE(window->windowTitle(), title); QCOMPARE(window->statusBar()->currentMessage(), status);
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    QVERIFY(undo->isEnabled() && redo->isEnabled() && remove->isEnabled());
    QCOMPARE(QImage(path), original); QCOMPARE(QImage(otherPath), other);
    QVERIFY(!QFile::exists(path + ".xann") && !QFile::exists(otherPath + ".xann"));
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 60")));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("saveImage"));
    const auto saved = loadSource(path); QVERIFY(saved.document);
    QCOMPARE(saved.document->annotations.size(), std::size_t(1)); QCOMPARE(saved.image, original);
    const auto &rectangle = std::get<RectangleAnnotation>(saved.document->annotations[0]);
    QCOMPARE(rectangle.style.rotationDegrees, 0);
    QVERIFY(std::abs(rectangle.start.x - 8) <= .51 && std::abs(rectangle.end.x - 22) <= .51);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("0 annotation(s)")));
    QVERIFY(!undo->isEnabled());
  }

  void nativeSourceMismatchChoice_data() {
    QTest::addColumn<int>("choice");
    QTest::newRow("cancel") << int(SourceChoice::Unspecified);
    QTest::newRow("current") << int(SourceChoice::CurrentRaster);
    QTest::newRow("embedded") << int(SourceChoice::EmbeddedSource);
  }
  void nativeSourceMismatchChoice() {
    QFETCH(int, choice);
    QTemporaryDir dir; const QString path = dir.filePath("mismatch.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    const auto first = loadSource(path); QVERIFY(first.document);
    EditorSession session(*first.document); QVERIFY(session.createRectangle({8, 8}, {22, 16}));
    const auto saved = saveEdit(path, original, session.document()); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    QImage current(24, 20, QImage::Format_ARGB32); current.fill(Qt::black); QVERIFY(current.save(path));
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::ReadOnly)); const QByteArray sidecarBefore = sidecar.readAll(); sidecar.close();
    QTimer timer;
    connect(&timer, &QTimer::timeout, [choice, &timer] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      timer.stop();
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
      if (choice == int(SourceChoice::Unspecified)) { box->button(QMessageBox::Cancel)->click(); return; }
      const QString prefix = choice == int(SourceChoice::CurrentRaster) ? QStringLiteral("Current raster") : QStringLiteral("Embedded source");
      for (auto *button : box->buttons()) if (button->text().startsWith(prefix)) { button->click(); return; }
      QFAIL("The source-choice dialog did not expose both validated images.");
    });
    timer.start(5); std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); timer.stop();
    if (choice == int(SourceChoice::Unspecified)) QVERIFY(!window);
    else {
      QVERIFY(window);
      const QString dimensions = choice == int(SourceChoice::CurrentRaster) ? QStringLiteral("24 × 20") : QStringLiteral("60 × 40");
      QVERIFY(window->statusBar()->currentMessage().startsWith(dimensions));
      QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("1 annotation(s)")));
      QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    }
    QCOMPARE(QImage(path), current);
    QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), sidecarBefore);
  }

  void cropIsUndoableAndReopensTheCroppedSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("crop.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    canvas->resetZoom();
    const QPoint a = canvas->viewState().toView({8, 9}).toPoint();
    const QPoint b = canvas->viewState().toView({24, 25}).toPoint();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, b);
    acceptDimensions(40, 20, 5, 6);
    trigger(*window, QStringLiteral("cropImage"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 20")));
    QCOMPARE(QImage(path).size(), original.size());  // editing never overwrites source
    trigger(*window, QStringLiteral("saveImage"));
    QCOMPARE(QImage(path).size(), QSize(40, 20));
    QCOMPARE(QImage(path).pixel(39, 0), original.pixel(44, 6));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QVERIFY(reopened.warnings.isEmpty());
    QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &r = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QVERIFY(std::abs(r.start.x - 3) <= .51 && std::abs(r.start.y - 3) <= .51);
    QVERIFY(std::abs(r.end.x - 19) <= .51 && std::abs(r.end.y - 19) <= .51);
    QCOMPARE(reopened.image.size(), QSize(40, 20));
    QCOMPARE(reopened.image.colorSpace(), original.colorSpace());
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
    QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("40 × 20")));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void canvasResizeDoesNotResampleAndUndoRestoresTheSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("canvas.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    acceptDimensions(64, 44);
    trigger(*window, QStringLiteral("resizeCanvas"));
    trigger(*window, QStringLiteral("saveImage"));
    const QImage saved(path); QCOMPARE(saved.size(), QSize(64, 44));
    QCOMPARE(qAlpha(saved.pixel(0, 0)), 0);
    for (int y = 0; y < original.height(); ++y) for (int x = 0; x < original.width(); ++x)
      QCOMPARE(saved.pixel(x + 2, y + 2), original.pixel(x, y));
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
  }

  void imageRotationPersistsPixelsAndUndoSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("rotation.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    trigger(*window, QStringLiteral("canvasAction%1").arg(int(CanvasAction::RotateClockwise)));
    trigger(*window, QStringLiteral("saveImage"));
    const QImage saved(path); QCOMPARE(saved.size(), QSize(40, 60));
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 40; ++x) QCOMPARE(saved.pixel(x, y), original.pixel(y, 39 - x));
    QCOMPARE(saved.colorSpace(), original.colorSpace());
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
  }

  void cancelledDialogPreservesCleanHistoryAndSource() {
    QTemporaryDir dir;
    const QString path = dir.filePath("cancel.png");
    QVERIFY(fixture().save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show(); QTest::qWait(10);
    const QString status = window->statusBar()->currentMessage();
    QTimer::singleShot(0, [] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject();
    });
    trigger(*window, QStringLiteral("resizeImage"));
    QCOMPARE(window->statusBar()->currentMessage(), status);
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void nativeAutoCropPersistsAlphaAndCreatesExactlyOneUndoStep() {
    QTemporaryDir dir; const QString path = dir.filePath("auto.png");
    QImage original(8, 6, QImage::Format_ARGB32); original.fill(Qt::transparent);
    original.setColorSpace(QColorSpace::SRgb);
    for (int y = 1; y < 5; ++y) for (int x = 2; x < 6; ++x) original.setPixel(x, y, qRgba(90, 20, 40, 128));
    QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->show();
    const auto runCrop = [&] {
      QTimer::singleShot(0, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
        QVERIFY(dialog->findChild<QSpinBox *>(QStringLiteral("cropAlpha"))); dialog->accept();
      });
      trigger(*window, QStringLiteral("autoCrop"));
    };
    runCrop();
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("4 × 4")));
    runCrop();  // No border: no extra history entry.
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image.size(), QSize(4, 4)); QCOMPARE(reopened.image.colorSpace(), original.colorSpace());
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) QCOMPARE(reopened.image.pixel(x, y), original.pixel(x + 2, y + 1));
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("8 × 6")));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    const QString status = window->statusBar()->currentMessage();
    QTimer::singleShot(0, [] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->reject(); });
    trigger(*window, QStringLiteral("autoCrop"));
    QCOMPARE(window->statusBar()->currentMessage(), status);
    QVERIFY(window->findChild<QAction *>(QStringLiteral("redo"))->isEnabled());
    trigger(*window, QStringLiteral("redo")); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void cancelledSeventeenDegreePreviewPreservesDirtyAnnotationsAndHistory() {
    QTemporaryDir dir; const QString path = dir.filePath("cancel-rotation.png");
    const QImage source = fixture(); QVERIFY(source.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {8, 8}; r.end = {22, 16}; r.style.rotationDegrees = 17;
    loaded.document->annotations = {r};
    const auto saved = saveEdit(path, source, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({35, 25}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({45, 30}).toPoint());
    const QString before = window->statusBar()->currentMessage(); const QString title = window->windowTitle();
    QVERIFY(title.contains(QChar(0x2022)));
    runRotationPreview(*window, false);
    QCOMPARE(window->statusBar()->currentMessage(), before); QCOMPARE(window->windowTitle(), title);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image, source); QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &retained = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QCOMPARE(retained.id, r.id); QCOMPARE(retained.start, r.start); QCOMPARE(retained.end, r.end); QCOMPARE(retained.style.rotationDegrees, 17);
  }

  void acceptedPreviewPersistsTransformedSourceAndEditableAnnotationsOnce() {
    QTemporaryDir dir; const QString path = dir.filePath("custom-rotation.png");
    const QImage source = fixture(); QVERIFY(source.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {8, 8}; r.end = {22, 16}; r.style.rotationDegrees = 17;
    loaded.document->annotations = {r};
    const auto saved = saveEdit(path, source, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    runRotationPreview(*window, true);
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("70 × 56")));
    trigger(*window, QStringLiteral("saveImage"));
    const auto reopened = loadSource(path); QVERIFY(reopened.document);
    QCOMPARE(reopened.image.size(), QSize(70, 56)); QCOMPARE(reopened.image.colorSpace(), source.colorSpace());
    QCOMPARE(reopened.image.pixel(35, 28), source.pixel(30, 20)); QCOMPARE(qAlpha(reopened.image.pixel(0, 0)), 0);
    QCOMPARE(reopened.document->annotations.size(), std::size_t(1));
    const auto &moved = std::get<RectangleAnnotation>(reopened.document->annotations[0]);
    QCOMPARE(moved.id, r.id); QCOMPARE(moved.style.rotationDegrees, 34);
    QCOMPARE(moved.right() - moved.left(), 14.0); QCOMPARE(moved.bottom() - moved.top(), 8.0);
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(window->statusBar()->currentMessage().startsWith(QStringLiteral("60 × 40")));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    trigger(*window, QStringLiteral("redo"));
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
  }

  void nativePreviewCancellationWaitsForTheLiveWorkerWithoutCommitting() {
    QTemporaryDir dir; const QString path = dir.filePath("large-preview.png");
    QImage source(1500, 1000, QImage::Format_ARGB32); source.fill(Qt::red); QVERIFY(source.save(path));
    const auto loaded = loadSource(path); QVERIFY(loaded.document);
    CanvasRotationDialog dialog(*loaded.document, source);
    dialog.findChild<QDoubleSpinBox *>(QStringLiteral("rotationAngle"))->setValue(17);
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelledLiveWorker = false;
    connect(&timer, &QTimer::timeout, &dialog, [&] {
      const int progress = dialog.findChild<QProgressBar *>()->value();
      if (progress > 0 && progress < 100) { cancelledLiveWorker = true; dialog.reject(); timer.stop(); }
      else if (deadline.elapsed() > 5000) { dialog.reject(); timer.stop(); }
    });
    timer.start(5);
    QCOMPARE(dialog.exec(), int(QDialog::Rejected)); QVERIFY(cancelledLiveWorker);
    QVERIFY(!dialog.takeEdit());
    QCOMPARE(QImage(path), source);
  }

  void flattenPersistsOnlyRenderedObjectsAndShowsUnsupportedPlaceholders() {
    QTemporaryDir dir; const QString path = dir.filePath("flatten.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation shown; shown.id = QUuid::createUuid(); shown.start = {1, 1}; shown.end = {4, 4};
    shown.style.strokeColor = 0; shown.style.fillColor = 0xFF00FF00;
    RectangleAnnotation hidden = shown; hidden.id = QUuid::createUuid(); hidden.visible = false;
    const QUuid id = QUuid::createUuid();
    UnsupportedAnnotation future{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"visible", false}, {"vendor", "preserve"}}};
    loaded.document->annotations = {shown, hidden, future};
    const auto saved = saveEdit(path, original, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *placeholders = window->findChild<AnnotationObjectNavigator *>(); QVERIFY(placeholders);
    QCOMPARE(placeholders->model()->rowCount(), 3); QVERIFY(placeholders->isVisible());
    QVERIFY(placeholders->model()->index(2, 0).data().toString().contains(QStringLiteral("future-art")));
    trigger(*window, QStringLiteral("flattenImage"));
    QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("2 annotation(s)")));
    trigger(*window, QStringLiteral("saveImage"));
    const auto flattened = loadSource(path); QVERIFY(flattened.document);
    QCOMPARE(flattened.image.pixel(2, 2), QRgb(0xFF00FF00)); QCOMPARE(flattened.image.colorSpace(), original.colorSpace());
    QCOMPARE(annotationId(flattened.document->annotations[0]), hidden.id);
    QCOMPARE(std::get<UnsupportedAnnotation>(flattened.document->annotations[1]).raw.value(u"vendor").toString(), QStringLiteral("preserve"));
    trigger(*window, QStringLiteral("flattenImage"));  // no represented annotations: no second history entry
    trigger(*window, QStringLiteral("undo"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    trigger(*window, QStringLiteral("saveImage"));
    const auto restored = loadSource(path); QVERIFY(restored.document); QCOMPARE(restored.image, original);
    QCOMPARE(restored.document->annotations.size(), std::size_t(3));
  }

  void clearAnnotationsKeepsSourcePixelsAndRestoresUnsupportedObjectsWithUndo() {
    QTemporaryDir dir; const QString path = dir.filePath("clear-objects.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    const QUuid id = QUuid::createUuid();
    loaded.document->annotations = {UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-art"}, {"vendor", "preserve"}}}};
    const auto saved = saveEdit(path, original, *loaded.document); QVERIFY(saved.rasterSaved && saved.sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    trigger(*window, QStringLiteral("clearAnnotations"));
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("clearAnnotations"))->isEnabled());
    QCOMPARE(window->findChild<AnnotationObjectNavigator *>()->model()->rowCount(), 0);
    trigger(*window, QStringLiteral("saveImage")); QCOMPARE(QImage(path), original); QVERIFY(!QFile::exists(path + ".xann"));
    trigger(*window, QStringLiteral("undo"));
    QCOMPARE(window->findChild<AnnotationObjectNavigator *>()->model()->rowCount(), 1);
    trigger(*window, QStringLiteral("saveImage"));
    const auto restored = loadSource(path); QVERIFY(restored.document);
    QCOMPARE(std::get<UnsupportedAnnotation>(restored.document->annotations[0]).raw.value(u"vendor").toString(), QStringLiteral("preserve"));
  }

  void clearImageConfirmationAndRecovery_data() {
    QTest::addColumn<bool>("dirty"); QTest::addColumn<int>("answer");
    QTest::newRow("clean-cancel") << false << int(QMessageBox::Cancel);
    QTest::newRow("clean-clear") << false << int(QMessageBox::Ok);
    QTest::newRow("dirty-cancel") << true << int(QMessageBox::Cancel);
    QTest::newRow("dirty-save-first") << true << int(QMessageBox::Save);
    QTest::newRow("dirty-clear-without-saving") << true << int(QMessageBox::Discard);
  }
  void clearImageConfirmationAndRecovery() {
    QFETCH(bool, dirty); QFETCH(int, answer);
    QTemporaryDir dir; const QString path = dir.filePath("clear.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    if (dirty) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    }
    const QString status = window->statusBar()->currentMessage(); const QString title = window->windowTitle();
    const double zoom = canvas->viewState().zoom(); const QPointF offset = canvas->viewState().offset();
    QTimer::singleShot(0, [answer] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::Cancel));
      auto *button = box->button(QMessageBox::StandardButton(answer)); QVERIFY(button); button->click();
    });
    trigger(*window, QStringLiteral("clearImage"));
    QCOMPARE(canvas->viewState().zoom(), zoom); QCOMPARE(canvas->viewState().offset(), offset);
    if (answer == QMessageBox::Cancel) { QCOMPARE(window->statusBar()->currentMessage(), status); QCOMPARE(window->windowTitle(), title); }
    else {
      QVERIFY(window->statusBar()->currentMessage().contains(QStringLiteral("0 annotation(s)")));
      trigger(*window, QStringLiteral("undo")); QCOMPARE(window->statusBar()->currentMessage(), status);
      QCOMPARE(window->windowTitle().contains(QChar(0x2022)), dirty && answer != QMessageBox::Save);
      if (dirty) trigger(*window, QStringLiteral("undo"));
      QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    }
    if (answer != QMessageBox::Save) QCOMPARE(QImage(path), original);
    else { QVERIFY(QFile::exists(path + ".xann")); QVERIFY(QImage(path) != original); }
  }

  void clearedImageSavesTransparentPixelsAndUndoRestoresTheImmutableSource() {
    QTemporaryDir dir; const QString path = dir.filePath("transparent.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box); box->button(QMessageBox::Ok)->click(); });
    trigger(*window, QStringLiteral("clearImage")); trigger(*window, QStringLiteral("saveImage"));
    const QImage cleared(path); QCOMPARE(cleared.size(), original.size()); QCOMPARE(cleared.colorSpace(), original.colorSpace());
    for (int y = 0; y < cleared.height(); ++y) for (int x = 0; x < cleared.width(); ++x) QCOMPARE(cleared.pixel(x, y), QRgb(0));
    trigger(*window, QStringLiteral("undo")); trigger(*window, QStringLiteral("saveImage")); QCOMPARE(QImage(path), original);
  }

  void cancellingNativeFlattenLeavesFilesDocumentAndHistoryUntouched() {
    QTemporaryDir dir; const QString path = dir.filePath("cancel-flatten.png");
    QImage original(600, 400, QImage::Format_ARGB32); original.fill(Qt::blue); QVERIFY(original.save(path));
    auto loaded = loadSource(path); QVERIFY(loaded.document);
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {50, 50}; r.end = {550, 350};
    r.style.rotationDegrees = 17; r.style.fillColor = 0x80FF0000; loaded.document->annotations = {r};
    const auto bytes = writeXann(*loaded.document); QVERIFY(bytes);
    QFile sidecar(path + ".xann"); QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write(*bytes); sidecar.close();
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    const QString status = window->statusBar()->currentMessage(), title = window->windowTitle();
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool cancelledLive = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget());
      if (!progress) return;
      if ((progress->value() > 5 && progress->value() < 90) || deadline.elapsed() > 5000) {
        cancelledLive = progress->value() > 5 && progress->value() < 90;
        auto *button = progress->findChild<QPushButton *>(); QVERIFY(button); button->click();
        QVERIFY(!window->close());  // the editor stays alive until its worker has stopped
        timer.stop();
      }
    });
    timer.start(5); trigger(*window, QStringLiteral("flattenImage")); timer.stop();
    QVERIFY(cancelledLive); QCOMPARE(window->statusBar()->currentMessage(), status); QCOMPARE(window->windowTitle(), title);
    QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
    QCOMPARE(QImage(path), original); QVERIFY(sidecar.open(QIODevice::ReadOnly)); QCOMPARE(sidecar.readAll(), *bytes);
  }

  void failedSaveBeforeClearKeepsTheDirtySessionAndHistory() {
    QTemporaryDir dir; const QString path = dir.filePath("failed-save.png"), backup = dir.filePath("original.png");
    const QImage original = fixture(); QVERIFY(original.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({22, 16}).toPoint());
    const QString before = window->statusBar()->currentMessage(), title = window->windowTitle(); QVERIFY(title.contains(QChar(0x2022)));
    QVERIFY(QFile::rename(path, backup)); QVERIFY(QDir().mkdir(path));  // destination is now a directory
    QTimer timer; bool saveRequested = false, failureShown = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (box->windowTitle() == u"Clear Image and Annotations" && !saveRequested) { saveRequested = true; box->button(QMessageBox::Save)->click(); }
      else if (box->windowTitle() == u"Save incomplete") { failureShown = true; box->accept(); }
    });
    timer.start(5); trigger(*window, QStringLiteral("clearImage")); timer.stop();
    QVERIFY(saveRequested && failureShown); QCOMPARE(window->statusBar()->currentMessage(), before); QCOMPARE(window->windowTitle(), title);
    QVERIFY(QDir(path).exists()); QCOMPARE(QImage(backup), original);
    trigger(*window, QStringLiteral("undo")); QVERIFY(!window->findChild<QAction *>(QStringLiteral("undo"))->isEnabled());
  }
};

QTEST_MAIN(EditorWindowTest)
#include "EditorWindowTest.moc"
