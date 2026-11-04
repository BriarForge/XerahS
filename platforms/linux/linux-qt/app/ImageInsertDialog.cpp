#include "ImageInsertDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {
ImageInsertDialog::ImageInsertDialog(QWidget *parent, ImageInsertHooks hooks)
    : QDialog(parent), m_hooks(std::move(hooks)) {
  setObjectName("imageInsertDialog"); setWindowTitle(tr("Insert Image")); setAccessibleName(windowTitle());
  setWindowModality(Qt::WindowModal); setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(tr("Choose an image file or explicitly read the current clipboard image. The image is embedded as one editable object; alpha and aspect ratio are kept. Nothing is uploaded or copied."), this);
  intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
  auto *sources = new QHBoxLayout;
  m_file = new QPushButton(tr("Image &File…"), this); m_file->setObjectName("insertImageFile"); sources->addWidget(m_file);
  m_clipboard = new QPushButton(tr("Use &Clipboard Image"), this); m_clipboard->setObjectName("insertImageClipboard"); sources->addWidget(m_clipboard);
  layout->addLayout(sources);
  m_status = new QLabel(tr("Choose a source. Cancel keeps the document and clipboard unchanged."), this);
  m_status->setObjectName("insertImageStatus"); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setObjectName("insertImageProgress"); m_progress->setAccessibleName(tr("Image insertion preparation progress"));
  m_progress->setRange(0, 100); m_progress->setValue(0); layout->addWidget(m_progress);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Cancel)->setFocus();
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &ImageInsertDialog::reject);
  connect(m_file, &QPushButton::clicked, this, &ImageInsertDialog::chooseFile);
  connect(m_clipboard, &QPushButton::clicked, this, &ImageInsertDialog::useClipboard);
  resize(530, 220);
}
ImageInsertDialog::~ImageInsertDialog() { cancelWork(); }
void ImageInsertDialog::setBusy(bool busy) { m_busy = busy; m_file->setEnabled(!busy); m_clipboard->setEnabled(!busy); }
void ImageInsertDialog::cancelWork() { ++m_generation; m_work.cancel(); }
void ImageInsertDialog::reject() { done(QDialog::Rejected); }
void ImageInsertDialog::done(int result) {
  if (result != QDialog::Accepted || !m_prepared.valid()) {
    result = QDialog::Rejected; m_prepared = {}; cancelWork();
  }
  if (m_picker) { m_picker->reject(); m_picker.clear(); }
  QDialog::done(result);
}
void ImageInsertDialog::chooseFile() {
  if (m_busy || m_picker) return;
  auto *picker = new QFileDialog(this, tr("Choose image to insert"));
  m_picker = picker; picker->setObjectName("insertImageFilePicker"); picker->setFileMode(QFileDialog::ExistingFile);
  picker->setAcceptMode(QFileDialog::AcceptOpen); picker->setAttribute(Qt::WA_DeleteOnClose);
  QStringList patterns;
  for (const auto &format : QImageReader::supportedImageFormats()) patterns.append("*." + QString::fromLatin1(format));
  picker->setNameFilter(QString("Images (%1);;All files (*)").arg(patterns.join(' ')));
  connect(picker, &QFileDialog::fileSelected, this, [this](const QString &path) { loadFile(path); });
  picker->open();
}
bool ImageInsertDialog::loadFile(const QString &path) {
  if (m_busy || path.isEmpty()) return false;
  prepare(path, {}); return true;
}
void ImageInsertDialog::useClipboard() {
  if (m_busy || m_picker) return;
  const QPointer<ImageInsertDialog> owner(this); const auto generation = ++m_generation; setBusy(true);
  auto *clipboard = QApplication::clipboard();
  const QImage image = clipboard ? clipboard->image() : QImage{};
  if (!owner || generation != m_generation) return;
  if (image.isNull()) { setBusy(false); m_status->setText(tr("The clipboard has no image. Choose an image file or copy an image and try again.")); return; }
  if (!insertImageSizeAllowed(image.size())) { setBusy(false); m_status->setText(tr("The clipboard image exceeds the 16384-pixel or 64-megapixel insertion limit.")); return; }
  // Qt images can wrap foreign storage. Own pixels before starting the worker.
  const QImage owned = image.copy();
  if (owned.isNull()) { setBusy(false); m_status->setText(tr("There is not enough memory to insert the clipboard image.")); return; }
  prepare({}, owned);
}
void ImageInsertDialog::prepare(QString path, QImage owned) {
  const auto generation = ++m_generation; setBusy(true); m_progress->setValue(0);
  m_status->setText(tr("Validating and embedding the selected image locally…"));
  const auto hooks = m_hooks;
  m_work = OwnedWork::run<PreparedImageAsset>(this,
      [path = std::move(path), owned = std::move(owned), hooks](const OwnedWork::Context &work) {
        try {
          if (hooks.beforePrepare) hooks.beforePrepare();
          const xerahs::editor::RenderControl control{[work] { return work.cancelled(); }, [work](int p) { work.progress(p, 100); }};
          return path.isEmpty() ? prepareImageAsset(owned, control) : loadImageAsset(path, control);
        } catch (...) { return PreparedImageAsset{{}, {}, "image-insert-failed"}; }
      }, [this, generation](PreparedImageAsset result) { complete(generation, std::move(result)); },
      [this, generation](qint64 value, qint64) { if (generation == m_generation) m_progress->setValue(int(value)); });
}
void ImageInsertDialog::complete(quint64 generation, PreparedImageAsset result) {
  if (generation != m_generation) return;
  setBusy(false);
  if (!result.valid()) {
    m_status->setText(tr("The image could not be inserted (%1). Choose a valid image file or clipboard image, or Cancel to keep the document unchanged.").arg(result.diagnostic));
    return;
  }
  m_prepared = std::move(result);
  m_status->setText(tr("Image inserted."));
  accept();
}
} // namespace xerahs::app
