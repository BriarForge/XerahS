#include "QrCodeDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

namespace xerahs::app {

using tools::QrEcc;

namespace {

constexpr int kPreviewBox = 320;

QString plainEnglish(const QString &code) {
  if (code == QLatin1String("qr-empty-content")) return QStringLiteral("Enter some text to encode.");
  if (code == QLatin1String("qr-content-too-long")) return QStringLiteral("That text is too long for a QR code at this error correction level. Shorten it or choose a lower level.");
  if (code == QLatin1String("qr-invalid-size")) return QStringLiteral("The image size is out of range. Choose a size the code fits in, up to %1 pixels.").arg(kQrMaxImagePixels);
  if (code == QLatin1String("qr-save-failed")) return QStringLiteral("The image could not be saved to that file.");
  if (code == QLatin1String("qr-clipboard-unavailable")) return QStringLiteral("The clipboard is not available.");
  if (code == QLatin1String("qr-upload-unavailable")) return QStringLiteral("Uploading is not available here.");
  return code;
}

}  // namespace

QrCodeDialog::QrCodeDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("qr-code-dialog"));
  setWindowTitle(tr("QR Code"));
  m_text = new QPlainTextEdit(this);
  m_text->setObjectName(QStringLiteral("qr-text"));
  m_text->setAccessibleName(tr("Text to encode"));
  m_text->setTabChangesFocus(true);
  m_text->setMinimumHeight(80);
  m_ecc = new QComboBox(this);
  m_ecc->setObjectName(QStringLiteral("qr-ecc"));
  m_ecc->setAccessibleName(tr("Error correction level"));
  m_ecc->addItem(tr("L (about 7%)"), static_cast<int>(QrEcc::L));
  m_ecc->addItem(tr("M (about 15%)"), static_cast<int>(QrEcc::M));
  m_ecc->addItem(tr("Q (about 25%)"), static_cast<int>(QrEcc::Q));
  m_ecc->addItem(tr("H (about 30%)"), static_cast<int>(QrEcc::H));
  m_ecc->setCurrentIndex(1);  // M by default
  m_size = new QSpinBox(this);
  m_size->setObjectName(QStringLiteral("qr-size"));
  m_size->setAccessibleName(tr("Image size in pixels"));
  m_size->setRange(1, 100000);  // out-of-range values are reported, not clamped
  m_size->setValue(kPreviewBox);
  m_size->setSuffix(tr(" px"));
  m_preview = new QLabel(this);
  m_preview->setObjectName(QStringLiteral("qr-preview"));
  m_preview->setAccessibleName(tr("QR code preview"));
  m_preview->setAlignment(Qt::AlignCenter);
  m_preview->setMinimumSize(kPreviewBox, kPreviewBox);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("qr-status"));
  m_status->setAccessibleName(tr("Status"));
  m_status->setWordWrap(true);
  m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_copy = new QPushButton(tr("&Copy image"), this);
  m_copy->setObjectName(QStringLiteral("qr-copy"));
  m_save = new QPushButton(tr("&Save PNG..."), this);
  m_save->setObjectName(QStringLiteral("qr-save"));
  m_upload = new QPushButton(tr("&Upload"), this);
  m_upload->setObjectName(QStringLiteral("qr-upload"));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

  auto *form = new QFormLayout;
  form->addRow(tr("&Text:"), m_text);
  form->addRow(tr("&Error correction:"), m_ecc);
  form->addRow(tr("Si&ze:"), m_size);
  auto *actions = new QHBoxLayout;
  actions->addWidget(m_copy); actions->addWidget(m_save); actions->addWidget(m_upload); actions->addStretch(1);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(m_preview, 1);
  layout->addWidget(m_status);
  layout->addLayout(actions);
  layout->addWidget(buttons);
  setTabOrder(m_text, m_ecc); setTabOrder(m_ecc, m_size);

  connect(m_text, &QPlainTextEdit::textChanged, this, &QrCodeDialog::refresh);
  connect(m_ecc, &QComboBox::currentIndexChanged, this, &QrCodeDialog::refresh);
  connect(m_size, &QSpinBox::valueChanged, this, &QrCodeDialog::refresh);
  connect(m_copy, &QPushButton::clicked, this, &QrCodeDialog::copyImage);
  connect(m_save, &QPushButton::clicked, this, &QrCodeDialog::chooseAndSave);
  connect(m_upload, &QPushButton::clicked, this, &QrCodeDialog::upload);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  refresh();
}

void QrCodeDialog::setUploadPort(std::function<void(const QString &)> port) {
  m_uploadPort = std::move(port);
  setActionsEnabled(!m_image.isNull());
}

void QrCodeDialog::setText(const QString &text) { m_text->setPlainText(text); }
void QrCodeDialog::setEcc(QrEcc ecc) { m_ecc->setCurrentIndex(static_cast<int>(ecc)); }
void QrCodeDialog::setImageSize(int pixels) { m_size->setValue(pixels); }
QString QrCodeDialog::text() const { return m_text->toPlainText(); }
QString QrCodeDialog::statusText() const { return m_status->text(); }

void QrCodeDialog::setActionsEnabled(bool enabled) {
  m_copy->setEnabled(enabled);
  m_save->setEnabled(enabled);
  m_upload->setEnabled(enabled && static_cast<bool>(m_uploadPort));
}

void QrCodeDialog::fail(const QString &code, const QString &message) {
  m_error = code;
  m_status->setText(QStringLiteral("%1 (%2)").arg(message, code));
}

void QrCodeDialog::refresh() {
  m_image = QImage();
  m_matrix = {};
  m_error.clear();
  m_preview->setPixmap(QPixmap());
  const auto result = tools::encodeQr(m_text->toPlainText(), static_cast<QrEcc>(m_ecc->currentData().toInt()));
  if (!result.ok) {
    fail(result.error, plainEnglish(result.error));
  } else {
    const auto rendered = renderQr(result.matrix, m_size->value());
    if (!rendered.ok) {
      m_matrix = result.matrix;
      fail(rendered.error, tr("The image size is out of range. Choose between %1 and %2 pixels.").arg(qrMinimumPixels(result.matrix)).arg(kQrMaxImagePixels));
    } else {
      m_matrix = result.matrix;
      m_image = rendered.image;
      auto pixmap = QPixmap::fromImage(m_image);
      if (pixmap.width() > kPreviewBox) pixmap = pixmap.scaled(kPreviewBox, kPreviewBox, Qt::KeepAspectRatio, Qt::FastTransformation);
      m_preview->setPixmap(pixmap);
      m_status->setText(tr("Version %1, %2 modules, mask %3, image %4 x %4 px.")
                            .arg(m_matrix.version).arg(m_matrix.size).arg(m_matrix.mask).arg(m_image.width()));
    }
  }
  setActionsEnabled(!m_image.isNull());
}

void QrCodeDialog::copyImage() {
  if (m_image.isNull()) return;
  auto *clipboard = QApplication::clipboard();
  if (!clipboard) return fail(QStringLiteral("qr-clipboard-unavailable"), plainEnglish(QStringLiteral("qr-clipboard-unavailable")));
  clipboard->setImage(m_image);
  m_error.clear();
  m_status->setText(tr("Copied the QR code image to the clipboard."));
}

bool QrCodeDialog::saveTo(const QString &path) {
  if (m_image.isNull()) return false;
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !m_image.save(&file, "PNG") || !file.commit()) {
    fail(QStringLiteral("qr-save-failed"), plainEnglish(QStringLiteral("qr-save-failed")));
    return false;
  }
  m_error.clear();
  m_status->setText(tr("Saved %1.").arg(path));
  return true;
}

void QrCodeDialog::chooseAndSave() {
  if (m_image.isNull()) return;
  const auto path = QFileDialog::getSaveFileName(this, tr("Save QR code"), QStringLiteral("qr-code.png"), tr("PNG image (*.png)"));
  if (!path.isEmpty()) saveTo(path);
}

void QrCodeDialog::upload() {
  if (m_image.isNull()) return;
  if (!m_uploadPort) return fail(QStringLiteral("qr-upload-unavailable"), plainEnglish(QStringLiteral("qr-upload-unavailable")));
  if (!m_temp) m_temp = std::make_unique<QTemporaryDir>();
  if (!m_temp->isValid()) return fail(QStringLiteral("qr-save-failed"), plainEnglish(QStringLiteral("qr-save-failed")));
  const auto path = m_temp->filePath(QStringLiteral("qr-code-%1.png").arg(++m_uploadCount));
  if (!saveTo(path)) return;
  m_status->setText(tr("Sent the QR code image to the uploader."));
  m_uploadPort(path);
}

}  // namespace xerahs::app
