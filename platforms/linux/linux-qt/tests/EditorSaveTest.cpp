#include "EditorSave.h"
#include "EditorSource.h"
#include <QApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QProgressDialog>
#include <QTimer>
#include <atomic>
#include <thread>
#include <stdexcept>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QImage pixels() { QImage image(40, 30, QImage::Format_ARGB32); image.fill(Qt::blue); return image; }
LoadedSource edited() { auto source = prepareCapturedSource(pixels()); EditorSession session(*source.document); session.createRectangle({5, 5}, {20, 20}); source.document = session.document(); return source; }
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
void write(const QString &path, const QByteArray &value) { QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size()) qFatal("fixture write failed"); }
}
class EditorSaveTest final : public QObject {
  Q_OBJECT
private slots:
  void cancellationAtEncodingBoundaryDoesNotStartAnEncoder() {
    QTemporaryDir dir; const auto path = dir.filePath("kept.jpg"); const auto source = edited(); QVERIFY(pixels().save(path, "JPEG")); const auto before = bytes(path);
    bool cancel = false; int encodes = 0; EditorSaveControl control{[&] { return cancel; }, [&](EditorSavePhase phase, int) { if (phase == EditorSavePhase::Encoding) cancel = true; }};
    EditorSaveHooks hooks; hooks.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "JPEG"); };
    const auto result = saveEdit(path, source.image, *source.document, control, hooks, {RasterFormat::Jpeg, 90, 0xFFFFFFFF});
    QVERIFY(result.cancelled && !result.rasterSaved); QCOMPARE(encodes, 0); QCOMPARE(bytes(path), before); QVERIFY(!QFile::exists(path + ".xann"));
  }
  void encodedFormatsKeepEditableSourceHashAndChosenAlphaPolicy_data() {
    QTest::addColumn<QString>("format"); QTest::newRow("jpeg") << QString("jpeg"); QTest::newRow("webp") << QString("webp");
  }
  void encodedFormatsKeepEditableSourceHashAndChosenAlphaPolicy() {
    QFETCH(QString, format); QTemporaryDir dir; QImage image(40, 30, QImage::Format_ARGB32); image.fill(qRgba(255, 0, 0, 128));
    auto source = prepareCapturedSource(image); EditorSession session(*source.document); session.createRectangle({8, 8}, {16, 16});
    const auto original = serializeDocument(session.document()); RasterSaveOptions options;
    options.format = format == "jpeg" ? RasterFormat::Jpeg : RasterFormat::WebP; options.quality = 95;
    if (format == "jpeg") options.jpegMatte = 0xFF0000FF;
    const auto path = dir.filePath(format == "jpeg" ? "image.jpg" : "image.webp"); bool inspected = false;
    EditorSaveHooks hooks; hooks.encode = [&](const QImage &pixels, QIODevice *device) {
      inspected = true;
      if (format == "jpeg") { if (pixels.hasAlphaChannel() || pixels.pixel(0, 0) != qRgb(128, 0, 127)) return false; }
      else if (pixels.pixel(0, 0) != qRgba(255, 0, 0, 128)) return false;
      QImageWriter writer(device, format.toLatin1()); writer.setQuality(95); return writer.write(pixels);
    };
    const auto result = saveEdit(path, source.image, session.document(), {}, hooks, options);
    QVERIFY(inspected && result.rasterSaved && result.sidecarSaved); QCOMPARE(QImageReader::imageFormat(path), format.toLatin1());
    if (format == "webp") QCOMPARE(qAlpha(QImage(path).pixel(0, 0)), 128); else QCOMPARE(qAlpha(QImage(path).pixel(0, 0)), 255);
    QCOMPARE(result.receipt->rasterHash, QString("sha256:") + QString::fromLatin1(QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex()));
    const auto reopened = loadSource(path); QVERIFY(reopened.document); QVERIFY(reopened.warnings.isEmpty()); QCOMPARE(reopened.image, source.image); QCOMPARE(reopened.document->annotations.size(), size_t(1));
    QCOMPARE(serializeDocument(session.document()), original); QCOMPARE(source.image, image);
    // A split persistence outcome repairs only the companion, for both formats.
    QVERIFY(QFile::remove(path + ".xann")); QVERIFY(QDir().mkdir(path + ".xann"));
    const auto partial = saveEdit(path, source.image, session.document(), {}, {}, options); QVERIFY(partial.rasterSaved && !partial.sidecarSaved); const auto saved = bytes(path);
    QVERIFY(QDir().rmdir(path + ".xann")); int encodes = 0; hooks.encode = [&](const QImage &, QIODevice *) { ++encodes; return false; };
    const auto repaired = retryEditorSidecar(partial.receipt, {}, hooks); QVERIFY(repaired.rasterSaved && repaired.sidecarSaved && repaired.rasterReused); QCOMPARE(encodes, 0); QCOMPARE(bytes(path), saved); QVERIFY(loadSource(path).document);
  }
  void qualityChangesActualEncodingAndFailuresKeepPreviousPairs_data() {
    QTest::addColumn<QString>("format"); QTest::newRow("jpeg") << QString("jpeg"); QTest::newRow("webp") << QString("webp");
  }
  void qualityChangesActualEncodingAndFailuresKeepPreviousPairs() {
    QFETCH(QString, format); QTemporaryDir dir; auto source = edited(); RasterSaveOptions options;
    options.format = format == "jpeg" ? RasterFormat::Jpeg : RasterFormat::WebP; if (format == "jpeg") options.jpegMatte = 0xFFFFFFFF;
    const auto path = dir.filePath(format == "jpeg" ? "image.jpg" : "image.webp"); options.quality = 10;
    QVERIFY(saveEdit(path, source.image, *source.document, {}, {}, options).sidecarSaved); const auto low = bytes(path); options.quality = 95;
    QVERIFY(saveEdit(path, source.image, *source.document, {}, {}, options).sidecarSaved); const auto high = bytes(path); QVERIFY(high != low); QCOMPARE(QImageReader::imageFormat(path), format.toLatin1()); const auto companion = bytes(path + ".xann");
    EditorSaveHooks fail; fail.encode = [](const QImage &, QIODevice *device) { device->write("incomplete"); return false; };
    const auto failure = saveEdit(path, source.image, *source.document, {}, fail, options); QVERIFY(!failure.rasterSaved); QCOMPARE(bytes(path), high); QCOMPARE(bytes(path + ".xann"), companion);
    options.quality = -1; int encoded = 0; fail.encode = [&](const QImage &, QIODevice *) { ++encoded; return true; };
    const auto invalid = saveEdit(path, source.image, *source.document, {}, fail, options); QVERIFY(!invalid.rasterSaved); QCOMPARE(encoded, 0); QCOMPARE(bytes(path), high);
    QCOMPARE(bytes(path + ".xann"), companion); QVERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
  }
  void workerSnapshotSurvivesConcurrentCallerChanges() {
    QTemporaryDir dir; auto source = edited(); const auto path = dir.filePath("image.png");
    std::atomic<bool> entered{false}, released{false}; EditorSaveHooks hooks;
    hooks.beforePublish = [&](EditorSaveArtifact kind, const QString &) {
      if (kind == EditorSaveArtifact::Raster) { entered.store(true); while (!released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    };
    QWidget owner; QTimer timer; bool changed = false;
    connect(&timer, &QTimer::timeout, [&] { if (entered.load() && !changed) { changed = true; source.image.fill(Qt::green); source.document->annotations.clear(); released.store(true); } });
    timer.start(5); const auto result = runEditorSave(path, source.image, *source.document, &owner, hooks); timer.stop();
    QVERIFY(changed && result.rasterSaved && result.sidecarSaved); const auto loaded = loadSource(path); QVERIFY(loaded.document);
    QCOMPARE(loaded.image, pixels()); QCOMPARE(loaded.document->annotations.size(), size_t(1)); QCOMPARE(QImage(path).pixel(5, 12), qRgb(255, 0, 0));
  }
  void realPairReopensWithoutCompoundingAndUsesTheEncodedRasterHash() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited();
    const auto result = saveEdit(path, source.image, *source.document); QVERIFY(result.rasterSaved && result.sidecarSaved && result.receipt);
    QCOMPARE(result.receipt->rasterHash, QString("sha256:") + QString::fromLatin1(QCryptographicHash::hash(bytes(path), QCryptographicHash::Sha256).toHex()));
    const auto loaded = loadSource(path); QVERIFY(loaded.document); QVERIFY(loaded.warnings.isEmpty()); QCOMPARE(loaded.image, pixels());
    QCOMPARE(loaded.document->annotations.size(), size_t(1)); QCOMPARE(QImage(path).pixel(5, 12), qRgb(255, 0, 0));
    QCOMPARE(result.sidecarPath, std::optional<QString>{path + ".xann"});
  }
  void encodeExceptionAndRacingReplacementPreserveThePreviousDestination() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited(); QVERIFY(pixels().save(path)); const auto original = bytes(path);
    EditorSaveHooks hooks; hooks.encode = [](const QImage &, QIODevice *device) -> bool { device->write("partial"); throw std::runtime_error("PRIVATE exception payload"); };
    const auto failed = saveEdit(path, source.image, *source.document, {}, hooks); QVERIFY(!failed.rasterSaved); QCOMPARE(failed.rasterError, QString("raster-save-failed")); QCOMPARE(bytes(path), original);
    hooks.encode = {}; hooks.beforePublish = [&](EditorSaveArtifact kind, const QString &) { if (kind == EditorSaveArtifact::Raster) write(path, "FOREIGN"); };
    const auto raced = saveEdit(path, source.image, *source.document, {}, hooks); QVERIFY(!raced.rasterSaved); QCOMPARE(bytes(path), QByteArray("FOREIGN"));
    QVERIFY(!QFile::exists(path + ".xann")); QVERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
  }
  void resultsReportDestinationFormatAndRecoverabilityAndNeverCarryPrivatePayloads() {
    QTemporaryDir dir; const auto path = dir.filePath("image.webp"); const auto source = edited(); QVERIFY(pixels().save(path, "WEBP")); const auto original = bytes(path);
    const RasterSaveOptions options{RasterFormat::WebP, 80, {}}; const QString secret = "PRIVATE exception payload";
    QStringList logged; static QStringList *sink = nullptr; sink = &logged; const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) { if (sink) sink->append(message); });
    EditorSaveHooks hooks; hooks.encode = [&](const QImage &, QIODevice *device) -> bool { device->write("partial"); throw std::runtime_error(secret.toStdString()); };
    const auto failed = saveEdit(path, source.image, *source.document, {}, hooks, options);
    // Not recoverable as a pair: nothing was published, no receipt, previous destination preserved, and only a stable code is reported.
    QVERIFY(!failed.rasterSaved && !failed.sidecarSaved && !failed.receipt && !failed.cancelled); QCOMPARE(failed.rasterError, QString("raster-save-failed")); QVERIFY(failed.sidecarError.isEmpty()); QCOMPARE(bytes(path), original);
    // Raster published, companion blocked: destination + format are reported through the receipt, and the code makes the split recoverable.
    hooks.encode = {}; QVERIFY(QDir().mkdir(path + ".xann"));
    const auto partial = saveEdit(path, source.image, *source.document, {}, hooks, options);
    QVERIFY(partial.rasterSaved && !partial.sidecarSaved && partial.receipt); QCOMPARE(partial.sidecarError, QString("sidecar-save-failed")); QVERIFY(partial.rasterError.isEmpty());
    QCOMPARE(partial.receipt->path, path); QCOMPARE(partial.receipt->encoding.format, RasterFormat::WebP); QCOMPARE(partial.receipt->encoding.quality, 80);
    QVERIFY(QDir().rmdir(path + ".xann")); const auto repaired = retryEditorSidecar(partial.receipt, {}, hooks); QVERIFY(repaired.rasterSaved && repaired.sidecarSaved && repaired.rasterReused); QCOMPARE(repaired.receipt->path, path); QCOMPARE(repaired.receipt->encoding.format, RasterFormat::WebP);
    qInstallMessageHandler(previous); sink = nullptr;
    for (const auto &text : {failed.rasterError, failed.sidecarError, partial.rasterError, partial.sidecarError, repaired.rasterError, repaired.sidecarError, logged.join('\n'), partial.receipt->path, partial.receipt->rasterHash}) {
      QVERIFY2(!text.contains(secret) && !text.contains("PRIVATE"), qPrintable(text));
    }
    QVERIFY(!failed.rasterError.contains(dir.path()) && !partial.sidecarError.contains(dir.path())); // codes are path-free, so telemetry never needs private path data
  }
  void oversizedEncodedTemporaryIsRejectedWithoutReplacingOrBufferingIt() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited(); QVERIFY(pixels().save(path)); const auto original = bytes(path);
    EditorSaveHooks hooks; hooks.encode = [](const QImage &, QIODevice *device) {
      auto *file = dynamic_cast<QFile *>(device); return file && file->resize(1073741825); // sparse, no large allocation
    };
    const auto result = saveEdit(path, source.image, *source.document, {}, hooks);
    QVERIFY(!result.rasterSaved && !result.sidecarSaved); QCOMPARE(result.rasterError, QString("raster-save-failed")); QCOMPARE(bytes(path), original);
    QVERIFY(!QFile::exists(path + ".xann")); QVERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
  }
  void completionObserverExceptionPreservesAcknowledgedArtifacts() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited();
    EditorSaveControl control{{}, [](EditorSavePhase phase, int) { if (phase == EditorSavePhase::Complete) throw std::runtime_error("PRIVATE observer payload"); }};
    const auto result = saveEdit(path, source.image, *source.document, control);
    QVERIFY(result.rasterSaved && result.sidecarSaved); QVERIFY(result.rasterError.isEmpty() && result.sidecarError.isEmpty()); QVERIFY(loadSource(path).document);
  }
  void incompletePairRepairsOnlyItsBoundCompanionAndRejectsChangedPng() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"), sidecar = path + ".xann"; const auto source = edited(); QVERIFY(QDir().mkdir(sidecar));
    int encodes = 0; EditorSaveHooks hooks; hooks.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); };
    const auto first = saveEdit(path, source.image, *source.document, {}, hooks); QVERIFY(first.rasterSaved && !first.sidecarSaved && first.receipt);
    const auto original = bytes(path); const auto binding = first.receipt->raster;
    const auto failedAgain = retryEditorSidecar(first.receipt, {}, hooks); QVERIFY(failedAgain.rasterSaved && !failedAgain.sidecarSaved); QCOMPARE(encodes, 1);
    QVERIFY(QDir().rmdir(sidecar)); const auto repaired = retryEditorSidecar(first.receipt, {}, hooks); QVERIFY(repaired.rasterSaved && repaired.sidecarSaved && repaired.rasterReused);
    QCOMPARE(encodes, 1); QCOMPARE(bytes(path), original); QVERIFY(repaired.receipt->raster == binding); QVERIFY(loadSource(path).document);
    QVERIFY(QFile::remove(sidecar)); write(path, original); // same bytes, changed publication identity
    const auto changed = retryEditorSidecar(first.receipt, {}, hooks); QVERIFY(changed.rasterSaved && !changed.sidecarSaved); QVERIFY(!QFile::exists(sidecar)); QCOMPARE(bytes(path), original);
  }
  void explicitEmptySaveRemovesOnlyTheKnownCompatibleDefaultSidecar() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited(); QVERIFY(saveEdit(path, source.image, *source.document).sidecarSaved);
    auto empty = *source.document; empty.annotations.clear(); const auto erased = saveEdit(path, source.image, empty);
    QVERIFY(erased.rasterSaved && erased.sidecarSaved); QVERIFY(!erased.sidecarPath); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(QImage(path), pixels());
    auto future = *source.document; future.version = 2; const auto encoded = writeXann(future); QVERIFY(encoded); write(path + ".xann", *encoded);
    const auto refused = saveEdit(path, source.image, empty); QVERIFY(refused.rasterSaved && !refused.sidecarSaved); QCOMPARE(bytes(path + ".xann"), *encoded); QCOMPARE(QImage(path), pixels());
  }
  void cancelledBeforePublicationPreservesFilesAndAfterRasterRetainsPartialFacts_data() {
    QTest::addColumn<bool>("afterRaster"); QTest::newRow("before PNG") << false; QTest::newRow("after PNG") << true;
  }
  void cancelledBeforePublicationPreservesFilesAndAfterRasterRetainsPartialFacts() {
    QFETCH(bool, afterRaster); QTemporaryDir dir; const auto path = dir.filePath("image.png"); const auto source = edited(); QVERIFY(pixels().save(path)); const auto original = bytes(path); bool cancel = false;
    EditorSaveControl control{[&] { return cancel; }, [&](EditorSavePhase phase, int) { if (afterRaster && phase == EditorSavePhase::SavingAnnotations) cancel = true; }};
    EditorSaveHooks hooks; hooks.beforePublish = [&](EditorSaveArtifact kind, const QString &) { if (!afterRaster && kind == EditorSaveArtifact::Raster) cancel = true; };
    const auto result = saveEdit(path, source.image, *source.document, control, hooks); QVERIFY(result.cancelled); QCOMPARE(result.rasterSaved, afterRaster); QVERIFY(!result.sidecarSaved);
    if (afterRaster) { QVERIFY(result.receipt); QCOMPARE(result.receipt->document.sourceImagePng, source.document->sourceImagePng); QVERIFY(bytes(path) != original); }
    else QCOMPARE(bytes(path), original);
    QVERIFY(!QFile::exists(path + ".xann")); QVERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty());
  }
  void symlinksAndChangedCompanionsArePreservedAndParentSegmentsKeepKernelMeaning() {
    QTemporaryDir dir; const auto source = edited(); const auto foreign = dir.filePath("foreign"); write(foreign, "KEEP"); const auto path = dir.filePath("image.png"); QVERIFY(QFile::link(foreign, path));
    QVERIFY(!saveEdit(path, source.image, *source.document).rasterSaved); QCOMPARE(bytes(foreign), QByteArray("KEEP")); QVERIFY(QFile::remove(path)); QVERIFY(QFile::link(foreign, path + ".xann"));
    const auto blocked = saveEdit(path, source.image, *source.document); QVERIFY(blocked.rasterSaved && !blocked.sidecarSaved); QCOMPARE(bytes(foreign), QByteArray("KEEP"));
    QVERIFY(QDir().mkpath(dir.filePath("outside/child"))); QVERIFY(QFile::link(dir.filePath("outside/child"), dir.filePath("linked")));
    const auto logical = dir.filePath("linked/../chosen.png"); QVERIFY(saveEdit(logical, source.image, *source.document).sidecarSaved);
    QVERIFY(QFile::exists(dir.filePath("outside/chosen.png"))); QVERIFY(!QFile::exists(dir.filePath("chosen.png"))); QVERIFY(loadSource(logical).document);
  }
  void modalWorkerOwnsItsSnapshotAndReleasesWithoutJoiningOnParentDestruction() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); auto source = edited(); std::atomic<bool> entered{false}, released{false}, finished{false};
    EditorSaveHooks hooks; hooks.beforePublish = [&](EditorSaveArtifact kind, const QString &) { if (kind == EditorSaveArtifact::Raster) { entered.store(true); while (!released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); finished.store(true); } };
    auto *parent = new QWidget; QTimer timer; bool destroyed = false;
    connect(&timer, &QTimer::timeout, [&] { if (entered.load() && !destroyed) { destroyed = true; delete parent; parent = nullptr; } });
    timer.start(5); const auto result = runEditorSave(path, source.image, *source.document, parent, hooks); timer.stop();
    QVERIFY(destroyed); QVERIFY(result.cancelled); QVERIFY(!finished.load()); source.image.fill(Qt::green); source.document.reset(); released.store(true);
    QTRY_VERIFY(finished.load()); QTRY_VERIFY(QDir(dir.path()).entryList({".xerahs-editor-*"}, QDir::Files | QDir::Hidden).isEmpty()); QVERIFY(!QFile::exists(path));
  }
};
QTEST_MAIN(EditorSaveTest)
#include "EditorSaveTest.moc"
