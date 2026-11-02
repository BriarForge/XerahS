#include "ImageRecognitionDialog.h"
#include "CanvasCropDialog.h"
#include "EditorPreparation.h"
#include "EditorSource.h"
#include "PortalScreenshot.h"
#include "RasterSource.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace xerahs::app {
namespace {
RecognitionImageChoice selectNativeRegion(const QImage &image, QWidget *parent) {
  const QPointer<QWidget> owner(parent);
  auto source = std::make_shared<LoadedSource>();
  const auto preparation = runEditorPreparation(parent,
      {"recognitionRegionProgress", "Select image region", "Preparing the captured image for region selection…", "Region selection preparation"},
      [source, image](const auto &control) { *source = prepareCapturedSource(image, control); });
  if (preparation == EditorPreparationResult::Cancelled || (parent && !owner)) return {{}, {}, true};
  if (!source->document) return {{}, source->diagnostic.isEmpty() ? QStringLiteral("recognition-input-invalid") : source->diagnostic};
  QPointer<CanvasCropDialog> dialog(new CanvasCropDialog(*source->document, source->image,
      {0, 0, image.width(), image.height()}, parent));
  dialog->setObjectName("recognitionRegionDialog"); dialog->setWindowTitle("Select region to recognize");
  for (auto *label : dialog->findChildren<QLabel *>())
    if (label->text().startsWith("Keep only the declared rectangle"))
      label->setText("Select the pixels to recognize. Pixels outside this region are excluded. Cancel keeps the previous recognition results.");
  if (auto *buttons = dialog->findChild<QDialogButtonBox *>()) buttons->button(QDialogButtonBox::Apply)->setText("&Use Region");
  const auto answer = dialog->exec();
  if (!dialog || (parent && !owner)) return {{}, {}, true};
  auto selected = answer == QDialog::Accepted ? dialog->takeEdit() : std::nullopt;
  delete dialog.data();
  if (!selected) return {{}, {}, true};
  return {selected->edit.source, {}, false};
}
QString unavailable(ImageRecognitionKind kind) {
  return kind == ImageRecognitionKind::QrCode
      ? QStringLiteral("QR scanning is unavailable. Install zxing-cpp 3.1.1 or later and rebuild XerahS.")
      : QStringLiteral("OCR is unavailable. Install Tesseract 5.5.3 or later with local English language data and rebuild XerahS.");
}
}
ImageRecognitionDialog::ImageRecognitionDialog(ImageRecognitionKind kind, QWidget *parent, ImageRecognitionPorts ports)
    : QDialog(parent), m_kind(kind), m_ports(std::move(ports)) {
  const bool suppliedRecognition = bool(m_ports.recognize);
  m_available = suppliedRecognition || (kind == ImageRecognitionKind::QrCode ? qrScanningAvailable() : textRecognitionAvailable());
  setObjectName(kind == ImageRecognitionKind::QrCode ? "qrScanDialog" : "ocrDialog");
  setWindowTitle(kind == ImageRecognitionKind::QrCode ? "Scan QR codes" : "Recognize text (OCR)");
  setModal(false); setAttribute(Qt::WA_DeleteOnClose);
  if (!m_ports.chooseFile) m_ports.chooseFile = [](QWidget *owner) {
    return QFileDialog::getOpenFileName(owner, "Choose image to recognize", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All files (*)");
  };
  if (!m_ports.selectRegion) m_ports.selectRegion = selectNativeRegion;
  if (!m_ports.recognize) m_ports.recognize = [](ImageRecognitionKind type, const QImage &image, const auto &control) {
    return type == ImageRecognitionKind::QrCode ? scanQrImage(image, control) : recognizeTextImage(image, control);
  };
  auto *layout = new QVBoxLayout(this);
  auto *privacy = new QLabel(kind == ImageRecognitionKind::Text
      ? "OCR reads English using the installed local model and a white background for transparent pixels. Images and recognized text stay in this window; Copy results is explicit."
      : "Images and recognized values stay in this window. Nothing is uploaded or copied automatically.", this);
  privacy->setWordWrap(true); privacy->setTextFormat(Qt::PlainText); layout->addWidget(privacy);
  auto *inputs = new QHBoxLayout;
  const auto button = [this, inputs](const QString &name, const QString &label) {
    auto *result = new QPushButton(label, this); result->setObjectName(name); result->setAccessibleName(label); inputs->addWidget(result); return result;
  };
  m_file = button("recognitionFile", "Image &File…");
  m_screen = button("recognitionScreen", "&Screen…");
  m_region = button("recognitionRegion", "&Region…");
  layout->addLayout(inputs);
  m_results = new QPlainTextEdit(this); m_results->setReadOnly(true); m_results->setObjectName("recognitionResults"); m_results->setAccessibleName("Recognized values"); layout->addWidget(m_results);
  m_status = new QLabel("Choose an image file, screen, or region. Capture modes ask your desktop for permission.", this);
  m_status->setObjectName("recognitionStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setObjectName("recognitionProgress"); m_progress->setAccessibleName("Recognition progress"); m_progress->setRange(0, 100); layout->addWidget(m_progress);
  auto *actions = new QHBoxLayout;
  m_copy = new QPushButton("&Copy results", this); m_copy->setObjectName("copyRecognitionResults"); m_copy->setEnabled(false); actions->addWidget(m_copy);
  m_cancel = new QPushButton("&Cancel operation", this); m_cancel->setObjectName("cancelRecognition"); m_cancel->setEnabled(false); actions->addWidget(m_cancel);
  auto *close = new QPushButton("Close", this); close->setObjectName("closeRecognition"); actions->addWidget(close); layout->addLayout(actions);
  connect(m_file, &QPushButton::clicked, this, &ImageRecognitionDialog::scanFile);
  connect(m_screen, &QPushButton::clicked, this, &ImageRecognitionDialog::scanScreen);
  connect(m_region, &QPushButton::clicked, this, &ImageRecognitionDialog::scanRegion);
  connect(m_cancel, &QPushButton::clicked, this, &ImageRecognitionDialog::cancel);
  connect(close, &QPushButton::clicked, this, &ImageRecognitionDialog::reject);
  connect(m_copy, &QPushButton::clicked, this, [this] { if (!m_busy && !m_values.isEmpty()) QApplication::clipboard()->setText(m_values.join('\n')); });
  if (!m_available) m_status->setText(unavailable(kind));
  setBusy(false);
  resize(600, 420);
}
ImageRecognitionDialog::~ImageRecognitionDialog() { cancelWork(false); }
void ImageRecognitionDialog::setBusy(bool busy) {
  m_busy = busy; m_file->setEnabled(!busy && m_available); m_screen->setEnabled(!busy && m_available); m_region->setEnabled(!busy && m_available);
  m_cancel->setEnabled(busy); m_copy->setEnabled(!busy && !m_values.isEmpty());
}
void ImageRecognitionDialog::showRecognizedValues(const QStringList &values) {
  if (!m_busy) complete(m_generation, {values, {}, false});
}
void ImageRecognitionDialog::scanFile() {
  if (m_busy || !m_available) return;
  const QPointer<ImageRecognitionDialog> owner(this); const auto generation = ++m_generation;
  setBusy(true);
  const auto file = m_ports.chooseFile(this);
  if (!owner || generation != m_generation) return;
  if (file.isEmpty()) { complete(generation, {{}, {}, true}); return; }
  m_status->setText("Loading and recognizing the selected image locally…"); m_progress->setValue(0);
  const auto recognize = m_ports.recognize; const auto kind = m_kind;
  m_work = OwnedWork::run<ImageRecognitionResult>(this,
      [file, recognize, kind](const OwnedWork::Context &work) {
        const xerahs::editor::RenderControl control{[work] { return work.cancelled(); }, [work](int p) { work.progress(p, 100); }};
        const auto source = loadRaster(file, control);
        if (work.cancelled()) return ImageRecognitionResult{{}, {}, true};
        if (!source.diagnostic.isEmpty()) return ImageRecognitionResult{{}, source.diagnostic};
        try { return recognize(kind, source.image, control); } catch (...) { return ImageRecognitionResult{{}, "recognition-failed"}; }
      }, [this, generation](ImageRecognitionResult result) { complete(generation, std::move(result)); },
      [this, generation](qint64 done, qint64) { if (generation == m_generation) m_progress->setValue(int(done)); });
}
void ImageRecognitionDialog::scanScreen() { capture(false); }
void ImageRecognitionDialog::scanRegion() { capture(true); }
void ImageRecognitionDialog::capture(bool region) {
  if (m_busy || !m_available) return;
  const auto generation = ++m_generation; setBusy(true); m_progress->setValue(0);
  m_status->setText("Waiting for desktop capture permission. Cancel operation stops this request.");
  if (!m_ports.capture) showMinimized();
  const QPointer<ImageRecognitionDialog> owner(this);
  QTimer::singleShot(0, this, [this, owner, generation, region] {
    if (!owner || generation != m_generation) return;
    if (m_ports.capture) {
      m_ports.capture([owner, generation, region](RecognitionImageChoice image) { if (owner) owner->acquired(generation, region, std::move(image)); });
      return;
    }
    auto *portal = new PortalScreenshot(this); m_portal = portal;
    connect(portal, &PortalScreenshot::finished, this, [this, portal, generation, region](PortalScreenshot::Outcome outcome, const QImage &image, const QString &diagnostic) {
      if (m_portal == portal) m_portal = nullptr;
      portal->retire();
      acquired(generation, region, {image, diagnostic, outcome == PortalScreenshot::Outcome::Cancelled});
    });
    portal->request();
  });
}
void ImageRecognitionDialog::acquired(quint64 generation, bool region, RecognitionImageChoice image) {
  if (generation != m_generation) return;
  showNormal(); raise(); activateWindow();
  if (image.cancelled || !image.diagnostic.isEmpty() || image.image.isNull()) {
    complete(generation, {{}, image.diagnostic.isEmpty() && !image.cancelled ? QStringLiteral("recognition-capture-failed") : image.diagnostic, image.cancelled}); return;
  }
  if (region) {
    const QPointer<ImageRecognitionDialog> owner(this);
    image = m_ports.selectRegion(image.image, this);
    if (!owner || generation != m_generation) return;
    if (image.cancelled || !image.diagnostic.isEmpty() || image.image.isNull()) { complete(generation, {{}, image.diagnostic.isEmpty() && !image.cancelled ? QStringLiteral("recognition-input-invalid") : image.diagnostic, image.cancelled}); return; }
  }
  recognize(std::move(image.image), generation);
}
void ImageRecognitionDialog::recognize(QImage image, quint64 generation) {
  m_status->setText("Recognizing the selected pixels locally…");
  const auto recognize = m_ports.recognize; const auto kind = m_kind;
  m_work = OwnedWork::run<ImageRecognitionResult>(this,
      [image = std::move(image), recognize, kind](const OwnedWork::Context &work) {
        try { return recognize(kind, image, {[work] { return work.cancelled(); }, [work](int p) { work.progress(p, 100); }}); }
        catch (...) { return ImageRecognitionResult{{}, "recognition-failed"}; }
      }, [this, generation](ImageRecognitionResult result) { complete(generation, std::move(result)); },
      [this, generation](qint64 done, qint64) { if (generation == m_generation) m_progress->setValue(int(done)); });
}
void ImageRecognitionDialog::complete(quint64 generation, ImageRecognitionResult result) {
  if (generation != m_generation) return;
  setBusy(false);
  if (result.cancelled) m_status->setText("Cancelled. No new pixels or results were retained.");
  else if (!result.diagnostic.isEmpty()) m_status->setText(result.diagnostic == "qr-scanner-unavailable" || result.diagnostic == "ocr-unavailable" ? unavailable(m_kind)
      : result.diagnostic == "ocr-language-unavailable" ? QStringLiteral("English OCR language data is unavailable. Install local Tesseract English (eng) data. Nothing is downloaded automatically.")
      : "Recognition failed: " + result.diagnostic);
  else {
    m_values = result.values;
    QStringList display;
    for (int i = 0; i < m_values.size(); ++i) display.append(m_values.size() > 1 ? QStringLiteral("[%1]\n%2").arg(i + 1).arg(m_values[i]) : m_values[i]);
    m_results->setPlainText(display.join("\n\n"));
    m_status->setText(m_kind == ImageRecognitionKind::QrCode
        ? m_values.isEmpty() ? QStringLiteral("No QR codes found.") : QStringLiteral("%1 QR code(s) decoded locally.").arg(m_values.size())
        : m_values.isEmpty() ? QStringLiteral("No text found.") : QStringLiteral("Text recognized locally."));
    m_progress->setValue(100); m_copy->setEnabled(!m_values.isEmpty());
  }
  emit recognitionFinished(result.values, result.diagnostic, result.cancelled);
}
void ImageRecognitionDialog::cancelWork(bool announce) {
  if (!m_busy) return;
  ++m_generation; m_work.cancel();
  if (m_portal) m_portal->cancel();
  if (m_ports.cancelCapture) m_ports.cancelCapture();
  if (announce) { setBusy(false); show(); m_status->setText("Cancelled. No new pixels or results were retained."); emit recognitionFinished({}, {}, true); }
}
void ImageRecognitionDialog::cancel() { cancelWork(true); }
void ImageRecognitionDialog::reject() { cancelWork(false); setBusy(false); QDialog::reject(); }
} // namespace xerahs::app
