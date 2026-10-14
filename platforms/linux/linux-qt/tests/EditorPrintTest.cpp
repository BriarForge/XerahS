#include "EditorPrint.h"
#include "EditorSource.h"
#include "image-editor/PrintLayout.h"
#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QDir>
#include <QPointer>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QPrinterInfo>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <limits>
#include <thread>
#include <stdexcept>
using namespace xerahs::app;
using namespace xerahs::editor;
using namespace xerahs::actions;
namespace {
LoadedSource fixture() {
  QImage image(40, 20, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); image.fill(qRgba(255, 0, 0, 128));
  auto source = prepareCapturedSource(image); RectangleAnnotation rectangle;
  rectangle.id = QUuid::createUuid(); rectangle.start = {20, 0}; rectangle.end = {40, 20}; rectangle.style.fillColor = 0xFF0000FF; rectangle.style.strokeColor = 0xFF0000FF; rectangle.style.strokeWidth = 1;
  source.document->annotations = {rectangle}; return source;
}
EditorPrintChoice pdfChoice(const QString &path) {
  auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution); printer->setOutputFormat(QPrinter::PdfFormat);
  printer->setOutputFileName(path); printer->setResolution(72);
  printer->setPageLayout(QPageLayout(QPageSize(QSizeF(100, 80), QPageSize::Point, "fixture"), QPageLayout::Portrait, QMarginsF(10, 10, 10, 10), QPageLayout::Point));
  return {{}, printer, 50};
}
struct Gate { std::atomic<bool> entered{false}, released{false}, finished{false}; };
void waitGate(const std::shared_ptr<Gate> &gate) {
  gate->entered.store(true); while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); gate->finished.store(true);
}
}
class EditorPrintTest : public QObject {
  Q_OBJECT
  QTemporaryDir preferences;
private slots:
  void initTestCase() {
    QVERIFY(preferences.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, preferences.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    // Querying printer availability must not reach a user's live CUPS service.
    qputenv("CUPS_SERVER", preferences.filePath("unavailable-cups.sock").toLocal8Bit());
  }
  void init() { QApplication::clipboard()->setText("KEEP"); }
  void independentLayoutFitsCentresAndRejectsInvalidGeometry() {
    QCOMPARE(printImageRect(200, 100, {10, 20, 100, 80}, 100), std::optional<QRectF>{QRectF(10, 35, 100, 50)});
    QCOMPARE(printImageRect(200, 100, {10, 20, 100, 80}, 50), std::optional<QRectF>{QRectF(35, 47.5, 50, 25)});
    QCOMPARE(printImageRect(100, 200, {0, 0, 100, 80}, 100), std::optional<QRectF>{QRectF(30, 0, 40, 80)});
    QVERIFY(!printImageRect(0, 20, {0, 0, 10, 10}, 100)); QVERIFY(!printImageRect(1, 1, {0, 0, 0, 10}, 100));
    QCOMPARE(printPixelOnWhite(0x00ABCDEFu), 0xFFFFFFFFu); QCOMPARE(printPixelOnWhite(0xFFFF0000u), 0xFFFF0000u); QCOMPARE(printPixelOnWhite(0x80FF0000u), 0xFFFF7F7Fu);
    for (double value : {0.0, 101.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) QVERIFY(!printImageRect(1, 1, {0, 0, 10, 10}, value));
  }
  void frozenPreparationAndNativePdfOutputHaveIndependentPixelsAndGeometry_data() { QTest::addColumn<bool>("gray"); QTest::newRow("colour") << false; QTest::newRow("grayscale") << true; }
  void frozenPreparationAndNativePdfOutputHaveIndependentPixelsAndGeometry() {
    QFETCH(bool, gray); QTemporaryDir dir; auto source = fixture(); const auto original = source.image;
    const auto gate = std::make_shared<Gate>(); EditorPrintPorts ports; bool renderWorker = false, configureGui = false, submitWorker = false, changed = false;
    ports.beforeRender = [&] { renderWorker = QThread::currentThread() != QApplication::instance()->thread(); waitGate(gate); };
    const QString path = dir.filePath("output.pdf");
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { configureGui = QThread::currentThread() == QApplication::instance()->thread(); auto choice = pdfChoice(path); choice.printer->setColorMode(gray ? QPrinter::GrayScale : QPrinter::Color); return choice; };
    ports.submit = [&](QPrinter &printer, const EditorPrintSnapshot &snapshot, double scale, const std::atomic<bool> &cancel) { submitWorker = QThread::currentThread() != QApplication::instance()->thread(); return submitEditorPrint(printer, snapshot, scale, cancel); };
    QWidget owner; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (gate->entered.load() && !changed) { changed = true; source.image.fill(Qt::green); source.document->annotations.clear(); gate->released.store(true); } });
    timer.start(5); const auto result = runEditorPrint(source.image, *source.document, &owner, ports); timer.stop();
    QVERIFY(renderWorker && configureGui && submitWorker && changed); QCOMPARE(result.action.state, ActionState::Succeeded); QVERIFY(!result.action.outcomeUnknown && result.submissionAttempted && result.snapshot);
    QCOMPARE(result.snapshot->colour.pixel(0, 0), qRgb(255, 127, 127)); QCOMPARE(result.snapshot->colour.pixel(30, 10), qRgb(0, 0, 255));
    QCOMPARE(result.snapshot->grayscale.pixelColor(0, 0).red(), qGray(qRgb(255, 127, 127))); QCOMPARE(original.pixel(0, 0), qRgba(255, 0, 0, 128));
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(file.readAll().startsWith("%PDF-"));
    // Poppler observes the actual Qt PDF boundary. No printer is contacted.
    QProcess render; render.start(QStringLiteral(XERAHS_PDFTOPPM), {"-scale-to-x", "100", "-scale-to-y", "80", "-singlefile", "-png", path, dir.filePath("page")}); QVERIFY(render.waitForFinished(10000)); QCOMPARE(render.exitCode(), 0);
    const QImage page(dir.filePath("page.png")); QCOMPARE(page.size(), QSize(100, 80));
    // 40x20 source at 50% of 80x60 printable area -> 40x20 centred at (30,30).
    QCOMPARE(page.pixelColor(5, 5), QColor(Qt::white)); QCOMPARE(page.pixelColor(29, 25), QColor(Qt::white));
    const auto left = page.pixelColor(35, 40), right = page.pixelColor(65, 40);
    if (gray) { QVERIFY(std::abs(left.red() - qGray(qRgb(255, 127, 127))) <= 2); QVERIFY(std::abs(right.red() - qGray(qRgb(0, 0, 255))) <= 2); QCOMPARE(left.red(), left.green()); QCOMPARE(right.green(), right.blue()); }
    else { QVERIFY(left.red() >= 253 && std::abs(left.green() - 127) <= 2); QVERIFY(right.blue() >= 253 && right.red() <= 2); }
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
    const auto artifact = qEnvironmentVariable("XERAHS_PRINT_QA_DIRECTORY");
    if (!artifact.isEmpty()) { QVERIFY(QDir().mkpath(artifact)); QVERIFY(QFile::copy(path, artifact + (gray ? "/grayscale.pdf" : "/colour.pdf"))); }
  }
  void cancellationAndOwnerDestructionBeforeConfigurationCannotSubmit_data() { QTest::addColumn<bool>("destroy"); QTest::newRow("Cancel") << false; QTest::newRow("owner deletion") << true; }
  void cancellationAndOwnerDestructionBeforeConfigurationCannotSubmit() {
    QFETCH(bool, destroy); auto source = fixture(); const auto gate = std::make_shared<Gate>(); EditorPrintPorts ports; int configurations = 0;
    ports.beforeRender = [gate] { waitGate(gate); }; ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { ++configurations; return EditorPrintChoice{}; };
    auto *owner = new QWidget; bool acted = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (!gate->entered.load() || acted) return; acted = true;
      if (destroy) { delete owner; owner = nullptr; }
      else { auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); QVERIFY(progress); progress->findChild<QPushButton *>()->click(); gate->released.store(true); }
    });
    timer.start(5); const auto result = runEditorPrint(source.image, *source.document, owner, ports); timer.stop();
    QVERIFY(acted); QCOMPARE(result.action.state, ActionState::Cancelled); QCOMPARE(result.abandoned, destroy); QVERIFY(!result.submissionAttempted); QCOMPARE(configurations, 0);
    if (destroy) { QVERIFY(!gate->finished.load()); source = {}; gate->released.store(true); QTRY_VERIFY(gate->finished.load()); }
    delete owner; QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void renderFailuresAndMissingCallbacksCannotReachSubmission() {
    auto source = fixture(); QWidget owner; EditorPrintPorts ports; int configurations = 0;
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { ++configurations; return EditorPrintChoice{}; };
    ports.beforeRender = [] { throw std::runtime_error("PRIVATE"); };
    const auto exception = runEditorPrint(source.image, *source.document, &owner, ports); QCOMPARE(exception.action.diagnostic, std::optional<QString>{"print-render-failed"});
    ports.beforeRender = {}; const auto id = QUuid::createUuid(); source.document->annotations.emplace_back(UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future"}, {"visible", true}}});
    const auto unsupported = runEditorPrint(source.image, *source.document, &owner, ports); QCOMPARE(unsupported.action.diagnostic, std::optional<QString>{"print-annotation-unsupported"}); QCOMPARE(configurations, 0);
    ports.submit = {}; QCOMPARE(runEditorPrint(source.image, *source.document, &owner, ports).action.diagnostic, std::optional<QString>{"print-unavailable"});
  }
  void nativePreviewExposesScaleOrientationColourPageAndCancelWithoutFiles() {
    const auto source = fixture(); QWidget owner; EditorPrintPorts ports; bool inspected = false; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] {
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!modal || modal->objectName() != "editorPrintDialog" || inspected) return; inspected = true;
      auto *scale = modal->findChild<QDoubleSpinBox *>("printScale"); auto *preview = modal->findChild<QPrintPreviewWidget *>("printPreview"); QVERIFY(scale && preview);
      auto *page = modal->findChild<QComboBox *>("printPageSize"); auto *margins = modal->findChild<QDoubleSpinBox *>("printMargins"); QVERIFY(page && margins);
      QPageLayout observed; QPrinter::ColorMode mode = QPrinter::Color;
      const auto connection = connect(preview, &QPrintPreviewWidget::paintRequested, [&](QPrinter *printer) { observed = printer->pageLayout(); mode = printer->colorMode(); });
      QCOMPARE(scale->value(), 100.0); scale->setValue(50); modal->findChild<QComboBox *>("printOrientation")->setCurrentIndex(1); modal->findChild<QComboBox *>("printColour")->setCurrentIndex(1);
      page->setCurrentIndex(page->findData(int(QPageSize::Letter))); margins->setValue(15); preview->updatePreview(); disconnect(connection);
      QCOMPARE(observed.orientation(), QPageLayout::Landscape); QCOMPARE(observed.pageSize().id(), QPageSize::Letter); QCOMPARE(observed.margins(QPageLayout::Millimeter), QMarginsF(15, 15, 15, 15)); QCOMPARE(mode, QPrinter::GrayScale);
      QVERIFY(!scale->accessibleName().isEmpty()); QVERIFY(preview->pageCount() == 1); auto *print = modal->findChild<QPushButton *>("confirmPrint"); QVERIFY(print);
      if (QPrinterInfo::availablePrinters().isEmpty()) { QVERIFY(!print->isEnabled()); QVERIFY(!print->toolTip().isEmpty()); }
      auto *cancel = modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel); QVERIFY(cancel->isDefault());
      const auto artifact = qEnvironmentVariable("XERAHS_PRINT_QA_DIRECTORY"); if (!artifact.isEmpty()) { QVERIFY(QDir().mkpath(artifact)); QVERIFY(modal->grab().save(artifact + "/preview.png")); }
      cancel->click();
    });
    timer.start(5); const auto result = runEditorPrint(source.image, *source.document, &owner, ports); timer.stop();
    QVERIFY(inspected); QCOMPARE(result.action.state, ActionState::Cancelled); QVERIFY(!result.submissionAttempted); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void configurationCancellationExceptionAndOwnerDeletionCannotSubmit_data() { QTest::addColumn<int>("kind"); QTest::newRow("Cancel") << 0; QTest::newRow("exception") << 1; QTest::newRow("owner deletion") << 2; QTest::newRow("invalid scale") << 3; }
  void configurationCancellationExceptionAndOwnerDeletionCannotSubmit() {
    QFETCH(int, kind); QTemporaryDir dir; const auto source = fixture(); auto *owner = new QWidget; EditorPrintPorts ports; int submissions = 0;
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { if (kind == 1) throw std::runtime_error("PRIVATE"); if (kind == 2) { delete owner; owner = nullptr; } if (kind == 3) { auto choice = pdfChoice(dir.filePath("never.pdf")); choice.percentOfFit = 1000; return choice; } return EditorPrintChoice{{ActionState::Cancelled, std::nullopt, std::nullopt}, {}, 100}; };
    ports.submit = [&](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { ++submissions; return ActionOutcome{}; };
    const auto result = runEditorPrint(source.image, *source.document, owner, ports); QCOMPARE(submissions, 0); QVERIFY(!result.submissionAttempted); QVERIFY(!QFile::exists(dir.filePath("never.pdf"))); delete owner;
    if (kind == 1 || kind == 3) QCOMPARE(result.action.diagnostic, std::optional<QString>{"print-configuration-failed"}); else QCOMPARE(result.action.state, ActionState::Cancelled);
  }
  void cancelDuringSubmissionAndOwnerDeletionPreserveHonestDelivery_data() { QTest::addColumn<int>("kind"); QTest::newRow("acknowledged late cancel") << 0; QTest::newRow("no-effect cancel") << 1; QTest::newRow("exception after effect") << 2; QTest::newRow("owner destroyed during job") << 3; }
  void cancelDuringSubmissionAndOwnerDeletionPreserveHonestDelivery() {
    QFETCH(int, kind); QTemporaryDir dir; const auto source = fixture(); auto *owner = new QWidget; const auto gate = std::make_shared<Gate>(); EditorPrintPorts ports; bool acted = false;
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { return pdfChoice(dir.filePath("job.pdf")); };
    const auto marker = dir.filePath("host-effect");
    ports.submit = [gate, kind, marker](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { waitGate(gate); if (kind != 1) { QFile file(marker); if (file.open(QIODevice::WriteOnly)) file.write("SUBMITTED"); } if (kind == 2) throw std::runtime_error("PRIVATE"); return kind == 1 ? ActionOutcome{ActionState::Cancelled, std::nullopt, std::nullopt} : ActionOutcome{}; };
    QTimer timer; connect(&timer, &QTimer::timeout, [&] { if (!gate->entered.load() || acted) return; acted = true;
      if (kind == 3) { delete owner; owner = nullptr; }
      else { auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); QVERIFY(progress); QCOMPARE(progress->objectName(), QString("printJobProgress")); progress->findChild<QPushButton *>()->click(); gate->released.store(true); }
    });
    timer.start(5); const auto result = runEditorPrint(source.image, *source.document, owner, ports); timer.stop(); QVERIFY(acted && result.submissionAttempted && result.cancellationArrivedDuring);
    if (kind >= 2) { QVERIFY(result.action.outcomeUnknown); QCOMPARE(result.action.diagnostic, std::optional<QString>{"print-delivery-unknown"}); }
    else QCOMPARE(result.action.state, kind == 1 ? ActionState::Cancelled : ActionState::Succeeded);
    if (kind == 3) { QVERIFY(result.abandoned && !gate->finished.load()); gate->released.store(true); QTRY_VERIFY(gate->finished.load()); }
    if (kind == 2) QVERIFY(QFile::exists(marker));
    delete owner;
  }
  void ownerDestructionDuringProgressTeardownPreservesCompletedAcknowledgement() {
    const auto source = fixture(); auto *owner = new QWidget; QTemporaryDir dir; EditorPrintPorts ports; const auto gate = std::make_shared<Gate>(); bool connected = false;
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { return pdfChoice(dir.filePath("job.pdf")); };
    ports.submit = [gate](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { waitGate(gate); return ActionOutcome{}; };
    QTimer timer; connect(&timer, &QTimer::timeout, [&] { if (!gate->entered.load() || connected) return; connected = true;
      auto *progress = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); QVERIFY(progress); QCOMPARE(progress->objectName(), QString("printJobProgress"));
      connect(progress, &QObject::destroyed, this, [&] { delete owner; owner = nullptr; }); gate->released.store(true);
    });
    timer.start(5); const auto result = runEditorPrint(source.image, *source.document, owner, ports); timer.stop();
    QVERIFY(connected && !owner && result.abandoned); QCOMPARE(result.action.state, ActionState::Succeeded); QVERIFY(!result.action.outcomeUnknown);
  }
  void sourceColourConversionIsDisclosedAndOriginalAlphaProfileAndPixelsSurvive() {
    QImage image(4, 4, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgbLinear); image.fill(qRgba(128, 128, 128, 73)); const auto source = prepareCapturedSource(image);
    QWidget owner; EditorPrintPorts ports; ports.configure = [](const EditorPrintSnapshot &, QWidget *) { return EditorPrintChoice{{ActionState::Cancelled, std::nullopt, std::nullopt}, {}, 100}; };
    const auto result = runEditorPrint(source.image, *source.document, &owner, ports); QVERIFY(result.snapshot); QVERIFY(result.snapshot->disclosures.contains("print-colour-converted-to-srgb"));
    QCOMPARE(result.snapshot->colour.pixel(0, 0), qRgb(236, 236, 236)); QCOMPARE(source.image, image); QCOMPARE(source.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
  }
  void knownFailureReusesSnapshotWhileUnknownMalformedOrNativeFailureDoesNotClaimSuccess() {
    const auto source = fixture(); QTemporaryDir dir; QWidget owner; EditorPrintPorts ports; int renders = 0, configurations = 0;
    ports.beforeRender = [&] { ++renders; }; ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { ++configurations; return pdfChoice(dir.filePath("job.pdf")); };
    ports.submit = [](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { return ActionOutcome{ActionState::Failed, "PRIVATE", std::nullopt}; };
    const auto first = runEditorPrint(source.image, *source.document, &owner, ports); QVERIFY(first.snapshot && !first.action.outcomeUnknown); QCOMPARE(first.action.diagnostic, std::optional<QString>{"print-failed"});
    ports.submit = [](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { return ActionOutcome{}; };
    const auto retry = runEditorPrint(source.image, *source.document, &owner, ports, first.snapshot); QCOMPARE(retry.action.state, ActionState::Succeeded); QCOMPARE(retry.snapshot, first.snapshot); QCOMPARE(renders, 1); QCOMPARE(configurations, 2);
    ports.submit = [](QPrinter &, const EditorPrintSnapshot &, double, const std::atomic<bool> &) { return ActionOutcome{ActionState::Skipped, "PRIVATE", std::nullopt}; };
    QVERIFY(runEditorPrint(source.image, *source.document, &owner, ports).action.outcomeUnknown);
    ports.configure = [&](const EditorPrintSnapshot &, QWidget *) { return pdfChoice(dir.filePath("absent/job.pdf")); }; ports.submit = submitEditorPrint;
    const auto failed = runEditorPrint(source.image, *source.document, &owner, ports); QVERIFY(failed.action.outcomeUnknown); QVERIFY(!QFile::exists(dir.filePath("absent/job.pdf")));
  }
};
QTEST_MAIN(EditorPrintTest)
#include "EditorPrintTest.moc"
