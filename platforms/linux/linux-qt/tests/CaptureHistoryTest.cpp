#include "CapturePipeline.h"
#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "NativeEditorPreferences.h"
#include "CanvasCropDialog.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <stdexcept>
#include <thread>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage cropPixels() { QImage image(80, 60, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); for (int y = 0; y < 60; ++y) for (int x = 0; x < 80; ++x) image.setPixel(x, y, qRgba(x * 3, y * 4, 90, x < 20 ? 128 : 255)); return image; }
CaptureAnnotationDialog *dialog() { return qobject_cast<CaptureAnnotationDialog *>(QApplication::activeModalWidget()); }
void setCropBounds(CanvasCropDialog &crop, int left, int top, int width, int height) {
  crop.activateWindow(); QTRY_VERIFY(crop.isActiveWindow());
  const QStringList names{"cropLeft", "cropTop", "cropWidth", "cropHeight"}; const QList<int> values{left, top, width, height};
  for (int i = 0; i < 4; ++i) { auto *field = crop.findChild<QSpinBox *>(names[i]); QVERIFY(field); auto *accessible = QAccessible::queryAccessibleInterface(field); QVERIFY(accessible && accessible->role() == QAccessible::SpinBox); QVERIFY(!accessible->text(QAccessible::Name).contains('&')); field->setFocus(); QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(field, QString::number(values[i])); QTest::keyClick(field, Qt::Key_Tab); QCOMPARE(field->value(), values[i]); }
  QTRY_VERIFY(crop.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->isEnabled());
}
void applyCrop(CanvasCropDialog &crop) { auto *apply = crop.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply); QVERIFY(apply->isEnabled()); apply->setFocus(); QTest::keyClick(apply, Qt::Key_Space); }
void cropCapture(CaptureAnnotationDialog &capture, int left = 10, int top = 15, int width = 40, int height = 30) {
  capture.findChild<QAction *>("cropImage")->trigger(); QTRY_VERIFY(qobject_cast<CanvasCropDialog *>(QApplication::activeModalWidget()));
  auto *crop = qobject_cast<CanvasCropDialog *>(QApplication::activeModalWidget()); setCropBounds(*crop, left, top, width, height); applyCrop(*crop);
  auto *canvas = capture.findChild<EditorCanvas *>(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(canvas->annotationDocument().canvasWidth, width);
}
struct HistoryGate {
  std::atomic<bool> entered{false}, released{false}, left{false}, guiThread{false};
  void block(bool fail = false) {
    guiThread.store(QThread::currentThread() == qApp->thread()); entered.store(true);
    while (!released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    left.store(true); if (fail) throw std::runtime_error("private-history-fixture-path-and-secret");
  }
};
struct PaintCounter final : QObject {
  int paints = 0;
  bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::Paint) ++paints; return false; }
};
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
}
class CaptureHistoryTest final : public QObject {
  Q_OBJECT
  QTemporaryDir dialogPreferences;
private slots:
  void historyCancellationRetiresBeforeWorkerAndKeepsExactTarget_data() {
    QTest::addColumn<bool>("redo"); QTest::addColumn<QString>("mode");
    for (bool redo : {false, true}) for (const auto *mode : {"button", "escape", "reject", "close", "destroy"})
      QTest::newRow(qPrintable(QString("%1-%2").arg(redo ? "redo" : "undo", mode))) << redo << QString(mode);
  }
  void historyCancellationRetiresBeforeWorkerAndKeepsExactTarget() {
    QFETCH(bool, redo); QFETCH(QString, mode); using namespace xerahs::editor;
    const auto gate = std::make_shared<HistoryGate>(); auto loaded = prepareCapturedSource(cropPixels()); QVERIFY(loaded.document);
    loaded.document->extra = {{"vendor", QJsonObject{{"keep", true}}}};
    loaded.document->embeddedImages.push_back({"unused.png", loaded.document->sourceImagePng});
    QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(loaded, nullptr, {0xFF123456, 0, 3, 0, 1}));
    const auto cleanup = qScopeGuard([&] { gate->released.store(true); if (capture) delete capture.data(); });
    capture->show(); cropCapture(*capture); auto *canvas = capture->findChild<EditorCanvas *>();
    if (redo) { capture->findChild<QAction *>("undo")->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(canvas->annotationDocument().canvasWidth, 80); }
    const auto before = serializeDocument(canvas->annotationDocument()); const auto bytes = canvas->annotationDocument().sourceImagePng;
    const auto state = canvas->annotationState(), generation = canvas->documentGeneration(); const auto selection = canvas->annotationSelection(); const auto region = capture->sourceRegion();
    auto *action = capture->findChild<QAction *>(redo ? "redo" : "undo"); QVERIFY(action->isEnabled());
    auto *sidecar = capture->findChild<QCheckBox *>("captureSaveAnnotationSidecar"); sidecar->setChecked(true);
    capture->setHistoryHooks({[gate] { gate->block(); }}); action->trigger(); QTRY_VERIFY(gate->entered.load()); QVERIFY(!gate->guiThread.load());
    QPointer<QProgressDialog> progress(capture->findChild<QProgressDialog *>("captureHistoryProgress")); QVERIFY(progress && progress->isVisible());
    QCOMPARE(progress->accessibleName(), QString("Restore capture history progress"));
    auto *cancel = progress->findChild<QPushButton *>(); QVERIFY(cancel); auto *accessible = QAccessible::queryAccessibleInterface(cancel); QVERIFY(accessible && accessible->role() == QAccessible::Button);
    QVERIFY(!canvas->isEnabled()); QVERIFY(!action->isEnabled()); QVERIFY(!capture->findChild<QPushButton *>("applyCaptureAnnotation")->isEnabled());
    capture->done(QDialog::Accepted); QVERIFY(!capture->document()); QVERIFY(capture->isVisible());
    if (!redo && mode == "button" && qEnvironmentVariableIsSet("XERAHS_CAPTURE_HISTORY_QA")) QVERIFY(progress->grab().save(qEnvironmentVariable("XERAHS_CAPTURE_HISTORY_QA")));
    QElapsedTimer elapsed; elapsed.start();
    if (mode == "button") { progress->activateWindow(); cancel->setFocus(); QTest::keyClick(cancel, Qt::Key_Space); }
    else if (mode == "escape") QTest::keyClick(progress, Qt::Key_Escape);
    else if (mode == "reject") progress->reject(); else if (mode == "close") progress->close(); else delete progress.data();
    QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->released.load()); QVERIFY(!gate->left.load()); QVERIFY(canvas->isEnabled());
    QVERIFY(action->isEnabled()); QVERIFY(capture->findChild<QPushButton *>("applyCaptureAnnotation")->isEnabled());
    QVERIFY(!progress || (!progress->isVisible() && !progress->parent()));
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationDocument().sourceImagePng, bytes);
    QCOMPARE(canvas->annotationState(), state); QCOMPARE(canvas->documentGeneration(), generation); QCOMPARE(canvas->annotationSelection(), selection);
    QVERIFY(capture->sourceRegion() == region); QCOMPARE(canvas->creationStrokeWidth(), 3.0); QVERIFY(sidecar->isChecked());
    const auto status = capture->findChild<QLabel *>("captureAnnotationStatus")->text(); QVERIFY(status.contains("cancelled"));
    gate->released.store(true); QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationState(), state);
    QCOMPARE(capture->findChild<QLabel *>("captureAnnotationStatus")->text(), status); QVERIFY(action->isEnabled());
  }
  void retiredHistoryCannotClobberRetryOrNewBranch_data() {
    QTest::addColumn<bool>("fail"); QTest::addColumn<QString>("next");
    for (bool fail : {false, true}) for (const auto *next : {"retry", "crop", "annotation"})
      QTest::newRow(qPrintable(QString("%1-%2").arg(next, fail ? "late-exception" : "late-success"))) << fail << QString(next);
  }
  void retiredHistoryCannotClobberRetryOrNewBranch() {
    QFETCH(bool, fail); QFETCH(QString, next); const auto gate = std::make_shared<HistoryGate>(); auto loaded = prepareCapturedSource(cropPixels()); QVERIFY(loaded.document);
    xerahs::editor::RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {20, 20}; r.end = {30, 30}; loaded.document->annotations = {r};
    QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(loaded)); const auto cleanup = qScopeGuard([&] { gate->released.store(true); if (capture) delete capture.data(); });
    capture->show(); cropCapture(*capture); auto *canvas = capture->findChild<EditorCanvas *>();
    capture->setHistoryHooks({[gate, fail] { gate->block(fail); }}); capture->findChild<QAction *>("undo")->trigger(); QTRY_VERIFY(gate->entered.load());
    capture->findChild<QProgressDialog *>("captureHistoryProgress")->reject(); QVERIFY(canvas->isEnabled()); capture->setHistoryHooks({});
    if (next == "retry") { capture->findChild<QAction *>("undo")->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(canvas->annotationDocument().canvasWidth, 80); }
    else if (next == "crop") { cropCapture(*capture, 5, 5, 20, 10); QVERIFY(capture->sourceRegion() == xerahs::editor::CropRegion({15, 20, 20, 10})); }
    else { QVERIFY(canvas->selectAnnotations({r.id}, canvas->annotationState(), canvas->documentGeneration())); QVERIFY(canvas->changeStrokeWidth(true)); QCOMPARE(canvas->creationStrokeWidth(), 5.0); }
    const auto expected = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState(); const auto region = capture->sourceRegion();
    const auto status = capture->findChild<QLabel *>("captureAnnotationStatus")->text(); const bool redo = capture->findChild<QAction *>("redo")->isEnabled();
    gate->released.store(true); QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), expected); QCOMPARE(canvas->annotationState(), state); QVERIFY(capture->sourceRegion() == region);
    QCOMPARE(capture->findChild<QLabel *>("captureAnnotationStatus")->text(), status); QCOMPARE(capture->findChild<QAction *>("redo")->isEnabled(), redo);
    QVERIFY(canvas->isEnabled()); QVERIFY(capture->findChild<QAction *>("cropImage")->isEnabled());
  }
  void captureHistoryGuardsLifetimeAndStalePublication_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"start-owner", "cleanup-owner", "finish-owner", "reject-owner", "done-owner", "selection", "state", "generation", "cleanup-selection", "failure-retry"}) QTest::newRow(mode) << QString(mode);
  }
  void captureHistoryGuardsLifetimeAndStalePublication() {
    QFETCH(QString, mode); using namespace xerahs::editor; const auto gate = std::make_shared<HistoryGate>();
    auto loaded = prepareCapturedSource(cropPixels()); QVERIFY(loaded.document); RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {20, 20}; r.end = {30, 30}; loaded.document->annotations = {r};
    std::optional<EditorSession> replacement; QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(loaded));
    const auto cleanup = qScopeGuard([&] { gate->released.store(true); if (capture) delete capture.data(); }); capture->show(); cropCapture(*capture); auto *canvas = capture->findChild<EditorCanvas *>();
    QVERIFY(canvas->selectAnnotations({r.id}, canvas->annotationState(), canvas->documentGeneration()));
    const auto before = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState();
    capture->setHistoryHooks({[gate, mode] { gate->block(mode == "failure-retry"); }});
    if (mode == "start-owner") connect(canvas, &EditorCanvas::changed, capture, [capture, canvas] { if (capture && !canvas->isEnabled()) delete capture.data(); });
    capture->findChild<QAction *>("undo")->trigger(); if (mode == "start-owner") { QVERIFY(!capture); QVERIFY(!gate->entered.load()); return; }
    QTRY_VERIFY(gate->entered.load()); auto *progress = capture->findChild<QProgressDialog *>("captureHistoryProgress"); QVERIFY(progress);
    if (mode == "cleanup-owner") connect(progress, &QObject::destroyed, this, [capture] { if (capture) delete capture.data(); });
    if (mode == "finish-owner") connect(canvas, &EditorCanvas::changed, capture, [capture, canvas] { if (capture && canvas->isEnabled()) delete capture.data(); });
    if (mode == "reject-owner" || mode == "done-owner") {
      QElapsedTimer elapsed; elapsed.start(); if (mode == "reject-owner") capture->reject(); else capture->done(QDialog::Rejected);
      QVERIFY(elapsed.elapsed() < 500); QVERIFY(!gate->released.load()); QTRY_VERIFY(!capture);
    } else {
      const auto alterSelection = [canvas] { canvas->setEnabled(true); canvas->selectAnnotations({}, canvas->annotationState(), canvas->documentGeneration()); canvas->setEnabled(false); };
      if (mode == "selection") alterSelection();
      if (mode == "cleanup-selection") connect(progress, &QObject::destroyed, this, [capture, alterSelection] { if (capture) alterSelection(); });
      if (mode == "state") { canvas->setEnabled(true); QVERIFY(canvas->changeStrokeWidth(true)); canvas->setEnabled(false); }
      if (mode == "generation") { replacement.emplace(canvas->annotationDocument()); canvas->resetDocument(*replacement, QImage::fromData(replacement->document().sourceImagePng, "PNG")); }
      const auto expected = serializeDocument(canvas->annotationDocument()); const auto expectedState = canvas->annotationState();
      gate->released.store(true); QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
      if (mode == "cleanup-owner" || mode == "finish-owner") { QTRY_VERIFY(!capture); return; }
      QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), expected); QCOMPARE(canvas->annotationState(), expectedState);
      const auto status = capture->findChild<QLabel *>("captureAnnotationStatus")->text();
      if (mode == "failure-retry") {
        QVERIFY(status.contains("could not be decoded")); QVERIFY(!status.contains("private-history")); QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(canvas->annotationState(), state);
        capture->setHistoryHooks({}); capture->findChild<QAction *>("undo")->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(canvas->annotationDocument().canvasWidth, 80);
      } else { QVERIFY(status.contains("changed")); QCOMPARE(canvas->annotationDocument().canvasWidth, 40); if (mode.contains("selection")) QVERIFY(canvas->annotationSelection().isEmpty()); }
    }
    gate->released.store(true); QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0);
  }
  void exactHistoryRetainsSourceAssetsAndAnnotationOnlyAvoidsWorker() {
    using namespace xerahs::editor; auto input = cropPixels(); input.setColorSpace(QColorSpace::DisplayP3); auto loaded = prepareCapturedSource(input); QVERIFY(loaded.document);
    loaded.document->imagePath = "capture-original-private.png"; loaded.document->imageHash = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(loaded.document->sourceImagePng, QCryptographicHash::Sha256).toHex());
    loaded.document->extra = {{"vendor", QJsonObject{{"nested", "preserved"}}}}; loaded.document->embeddedImages.push_back({"keep.png", loaded.document->sourceImagePng});
    RectangleAnnotation r; r.id = QUuid::createUuid(); r.start = {20, 20}; r.end = {30, 30}; r.extra = {{"vendor", 3}}; loaded.document->annotations = {r};
    QPointer<CaptureAnnotationDialog> capture(new CaptureAnnotationDialog(loaded)); const auto cleanup = qScopeGuard([&] { if (capture) delete capture.data(); }); capture->show(); auto *canvas = capture->findChild<EditorCanvas *>();
    QVERIFY(canvas->selectAnnotations({r.id}, canvas->annotationState(), canvas->documentGeneration())); const auto original = serializeDocument(canvas->annotationDocument()); const auto originalState = canvas->annotationState(); const auto selection = canvas->annotationSelection();
    cropCapture(*capture); const auto first = serializeDocument(canvas->annotationDocument()); const auto firstState = canvas->annotationState(); cropCapture(*capture, 5, 5, 20, 10); const auto second = serializeDocument(canvas->annotationDocument());
    auto *undo = capture->findChild<QAction *>("undo"), *redo = capture->findChild<QAction *>("redo");
    undo->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), first); QCOMPARE(canvas->annotationState(), firstState); QVERIFY(capture->sourceRegion() == CropRegion({10, 15, 40, 30}));
    undo->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), original); QCOMPARE(canvas->annotationState(), originalState); QCOMPARE(canvas->annotationSelection(), selection);
    QCOMPARE(QImage::fromData(canvas->annotationDocument().sourceImagePng, "PNG"), input); QVERIFY(capture->sourceRegion() == CropRegion({0, 0, 80, 60}));
    redo->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), first); redo->trigger(); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), second); QVERIFY(capture->sourceRegion() == CropRegion({15, 20, 20, 10}));
    const auto workers = std::make_shared<std::atomic<int>>(0); capture->setHistoryHooks({[workers] { ++*workers; throw std::runtime_error("annotation history must not decode"); }});
    QVERIFY(canvas->changeStrokeWidth(true)); const auto styled = serializeDocument(canvas->annotationDocument()); QCoreApplication::processEvents();
    PaintCounter repaint; canvas->viewport()->installEventFilter(&repaint); undo->trigger(); QCOMPARE(serializeDocument(canvas->annotationDocument()), second); QTRY_VERIFY(repaint.paints > 0);
    redo->trigger(); QCOMPARE(serializeDocument(canvas->annotationDocument()), styled); QCOMPARE(workers->load(), 0);
    capture->accept(); QVERIFY(capture->document()); QCOMPARE(serializeDocument(*capture->document()), styled);
  }
  void historyPublicationAndCancelFlowThroughActualCaptureSaveRetry_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"undo", "cancel-history", "global-cancel", "destroy-pipeline"}) QTest::newRow(mode) << QString(mode);
  }
  void historyPublicationAndCancelFlowThroughActualCaptureSaveRetry() {
    QFETCH(QString, mode); using namespace xerahs::editor; QTemporaryDir dir; const auto gate = std::make_shared<HistoryGate>();
    const auto prefs = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty()); const auto revision = nativeEditorPreferenceRevision(); const auto reset = qScopeGuard([] { initializeNativeEditorPreferences(); });
    int encodes = 0; CaptureActionPorts ports; ports.save.encode = [&](const QImage &image, QIODevice *device) { return ++encodes == 1 ? false : image.save(device, "PNG"); };
    const auto input = cropPixels(); auto pipeline = std::make_unique<CapturePipeline>(input, dir.path(), "history", QStringList{"annotation", "save"}, nullptr, ports);
    QSignalSpy done(pipeline.get(), &CapturePipeline::finished); pipeline->start(); QTRY_VERIFY(dialog()); QPointer<CaptureAnnotationDialog> capture(dialog()); const auto cleanup = qScopeGuard([&] { gate->released.store(true); if (capture) delete capture.data(); }); cropCapture(*capture); auto *canvas = capture->findChild<EditorCanvas *>();
    capture->findChild<QCheckBox *>("captureSaveAnnotationSidecar")->setChecked(true); capture->setHistoryHooks({[gate] { gate->block(); }}); capture->findChild<QAction *>("undo")->trigger(); QTRY_VERIFY(gate->entered.load());
    if (mode == "global-cancel" || mode == "destroy-pipeline") {
      QElapsedTimer elapsed; elapsed.start(); if (mode == "global-cancel") pipeline->cancel(); else pipeline.reset(); QVERIFY(elapsed.elapsed() < 500); QTRY_VERIFY(!capture);
      if (mode == "global-cancel") { QTRY_COMPARE(done.count(), 1); QCOMPARE(receipt(done)->pipeline.state, PipelineState::Cancelled); QVERIFY(!receipt(done)->annotationDocument); }
      QVERIFY(!QFile::exists(dir.filePath("history.png"))); QCOMPARE(encodes, 0);
    } else {
      if (mode == "undo") { gate->released.store(true); QTRY_VERIFY(canvas->isEnabled()); QCOMPARE(canvas->annotationDocument().canvasWidth, 80); }
      else { capture->findChild<QProgressDialog *>("captureHistoryProgress")->reject(); QVERIFY(canvas->isEnabled()); QVERIFY(!gate->released.load()); }
      const auto expected = mode == "undo" ? input : input.copy(10, 15, 40, 30); capture->accept(); QTRY_COMPARE(done.count(), 1); const auto retained = receipt(done);
      QVERIFY(retained->annotationDocument); QVERIFY(retained->retryPlan().available()); QVERIFY(retained->files.empty()); QCOMPARE(retained->workingMedia, expected);
      CapturePipeline retry(retained, nullptr, ports); QSignalSpy retried(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(retried.count(), 1); const auto result = receipt(retried);
      QVERIFY(result->file); QCOMPARE(QImage(result->file->path), expected); QCOMPARE(encodes, 2); const auto reopened = loadSource(result->file->path); QVERIFY(reopened.document);
      QCOMPARE(reopened.document->sourceImagePng, retained->annotationDocument->sourceImagePng); QCOMPARE(QImage::fromData(reopened.document->sourceImagePng, "PNG"), expected);
      QCOMPARE(serializeDocument(*reopened.document).value("annotations"), serializeDocument(*retained->annotationDocument).value("annotations"));
    }
    gate->released.store(true); QTRY_VERIFY(QThreadPool::globalInstance()->activeThreadCount() == 0); QCOMPARE(nativeEditorPreferenceRevision(), revision); QCOMPARE(input, cropPixels());
  }
  void initTestCase() {
    QVERIFY(dialogPreferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dialogPreferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dialogPreferences.path());
  }
};
QTEST_MAIN(CaptureHistoryTest)
#include "CaptureHistoryTest.moc"
