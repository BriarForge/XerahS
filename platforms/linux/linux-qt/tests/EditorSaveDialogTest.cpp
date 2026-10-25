#include "EditorSaveDialog.h"
#include "EditorSource.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::editor;
class EditorSaveDialogTest final : public QObject {
  Q_OBJECT
private slots:
  void jpegAlphaWarningExplainsThatEmptyDocumentsHaveNoCompanion() {
    QTemporaryDir dir; const auto path = dir.filePath("empty.jpg"); QImage image(4, 4, QImage::Format_ARGB32); image.fill(qRgba(255, 0, 0, 128));
    const auto source = prepareCapturedSource(image); QVERIFY(source.document); const auto bytes = source.document->sourceImagePng;
    const auto saved = saveEdit(path, source.image, *source.document, {}, {}, {RasterFormat::Jpeg, 90, 0xFF000000});
    QVERIFY(saved.rasterSaved && saved.sidecarSaved); QVERIFY(!saved.sidecarPath); QVERIFY(!QFile::exists(path + ".xann")); QCOMPARE(source.document->sourceImagePng, bytes);
    EditorSaveDialog dialog(path); const auto warning = dialog.findChild<QLabel *>("editorSaveAlphaPolicy")->text();
    QVERIFY2(warning.contains("Companions are saved only when annotations exist"), qPrintable(warning));
  }
  void formatsRequireExplicitQualityAndOpaqueMatteWithMatchingExtension() {
    QTemporaryDir dir; EditorSaveDialog dialog(dir.filePath("new.png")); dialog.show(); QCOMPARE(dialog.nameFilters().size(), 3);
    QVERIFY(!dialog.testOption(QFileDialog::DontConfirmOverwrite));
    auto *quality = dialog.findChild<QLineEdit *>("editorSaveQuality"), *matte = dialog.findChild<QLineEdit *>("editorSaveJpegMatte"); QVERIFY(quality && matte); QVERIFY(quality->text().isEmpty()); QVERIFY(matte->text().isEmpty());
    dialog.selectNameFilter("JPEG image (*.jpg *.jpeg)"); dialog.selectFile(dir.filePath("new.jpg")); QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.accept(); QCOMPARE(accepted.count(), 0); QVERIFY(dialog.findChild<QLabel *>("editorSaveOptionsError")->text().contains("export-options-invalid"));
    quality->setText("90"); matte->setText("#00112233"); dialog.accept(); QCOMPARE(accepted.count(), 0);
    matte->setText("#112233"); dialog.selectFile(dir.filePath("new.png")); dialog.accept(); QCOMPARE(accepted.count(), 0); QVERIFY(dialog.findChild<QLabel *>("editorSaveOptionsError")->text().contains("export-extension-mismatch"));
    dialog.selectFile(dir.filePath("new.JPEG")); dialog.accept(); QCOMPARE(accepted.count(), 1); const auto options = dialog.encodingOptions(); QCOMPARE(options.format, RasterFormat::Jpeg); QCOMPARE(options.quality, 90); QCOMPARE(options.jpegMatte, std::optional<Argb>(0xFF112233));
    QVERIFY(!QFile::exists(dir.filePath("new.JPEG"))); // Choosing a destination never performs I/O publication.
  }
  void validatesOptionsWithoutClampingOrGuessingDefaults() {
    QVERIFY(validRasterSaveOptions({})); QVERIFY(!validRasterSaveOptions({RasterFormat::WebP, -1, {}})); QVERIFY(validRasterSaveOptions({RasterFormat::WebP, 0, {}}));
    QVERIFY(validRasterSaveOptions({RasterFormat::WebP, 100, {}})); QVERIFY(!validRasterSaveOptions({RasterFormat::WebP, 101, {}}));
    QVERIFY(!validRasterSaveOptions({RasterFormat::Jpeg, 80, {}})); QVERIFY(!validRasterSaveOptions({RasterFormat::Jpeg, 80, 0xFEFFFFFF})); QVERIFY(validRasterSaveOptions({RasterFormat::Jpeg, 80, 0xFFFFFFFF}));
    QVERIFY(!validRasterSaveOptions({static_cast<RasterFormat>(99), 80, {}})); QVERIFY(!rasterFormatFromSuffix("avif")); QCOMPARE(rasterFormatFromSuffix("JpEg"), std::optional<RasterFormat>(RasterFormat::Jpeg));
  }
  void webpKeepsAlphaAndHasNoImplicitQualityChoice() {
    QTemporaryDir dir; EditorSaveDialog dialog(dir.filePath("new.webp")); dialog.show(); QSignalSpy accepted(&dialog, &QDialog::accepted); dialog.accept(); QCOMPARE(accepted.count(), 0);
    auto *quality = dialog.findChild<QLineEdit *>("editorSaveQuality"); quality->setText("100"); dialog.accept(); QCOMPARE(accepted.count(), 1); QCOMPARE(dialog.encodingOptions().format, RasterFormat::WebP);
    QVERIFY(!dialog.encodingOptions().jpegMatte); QVERIFY(dialog.findChild<QLabel *>("editorSaveAlphaPolicy")->text().contains("Alpha is preserved"));
  }
};
QTEST_MAIN(EditorSaveDialogTest)
#include "EditorSaveDialogTest.moc"
