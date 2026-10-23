#include "IconConverterDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QSaveFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace xerahs::app {
namespace {

// A single image larger than this is refused before it is decoded.
constexpr int kMaxSide = 16384;
// The preview shows the 32 pixel entry.
constexpr int kPreviewSize = 32;
constexpr int kPreviewPixels = 128;

QStringList localFilePaths(const QMimeData *data) {
  QStringList paths;
  if (!data) return paths;
  for (const auto &url : data->urls())
    if (url.isLocalFile()) paths.append(url.toLocalFile());
  return paths;
}

void acceptLocalFiles(QDropEvent *event) {
  if (localFilePaths(event->mimeData()).isEmpty()) event->ignore();
  else event->acceptProposedAction();
}

}  // namespace

IconConverterDialog::IconConverterDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("iconConverterDialog"));
  setWindowTitle(QStringLiteral("Icon converter"));
  setAccessibleName(QStringLiteral("Icon converter"));
  setAcceptDrops(true);

  m_path = new QLineEdit(this);
  m_path->setObjectName(QStringLiteral("iconImagePath"));
  m_path->setReadOnly(true);
  m_path->setAccessibleName(QStringLiteral("Image"));
  m_choose = new QPushButton(QStringLiteral("Choose &image…"), this);
  m_choose->setObjectName(QStringLiteral("chooseIconImage"));
  m_choose->setAccessibleName(QStringLiteral("Choose image"));

  auto *sizeGroup = new QGroupBox(QStringLiteral("Sizes"), this);
  sizeGroup->setObjectName(QStringLiteral("iconSizeGroup"));
  auto *sizeLayout = new QVBoxLayout(sizeGroup);
  // EU-007: the contract offers these six sizes.
  for (const int size : iconSizes()) {
    auto *box = new QCheckBox(QString::number(size), sizeGroup);
    box->setObjectName(QStringLiteral("iconSize%1").arg(size));
    box->setAccessibleName(QStringLiteral("%1 pixels").arg(size));
    box->setProperty("iconEntrySize", size);
    // EU-007: 16, 32, 48 and 256 are offered by default.
    box->setChecked(size == 16 || size == 32 || size == 48 || size == 256);
    sizeLayout->addWidget(box);
    m_sizes.append(box);
  }

  auto *modeGroup = new QGroupBox(QStringLiteral("Mode"), this);
  modeGroup->setObjectName(QStringLiteral("iconModeGroup"));
  auto *modeLayout = new QVBoxLayout(modeGroup);
  m_trueColor = new QRadioButton(QStringLiteral("True colour (32-bit)"), modeGroup);
  m_trueColor->setObjectName(QStringLiteral("iconModeTrueColor32"));
  m_trueColor->setAccessibleName(QStringLiteral("True colour 32-bit"));
  m_palette = new QRadioButton(QStringLiteral("Palette (8-bit)"), modeGroup);
  m_palette->setObjectName(QStringLiteral("iconModePalette8"));
  m_palette->setAccessibleName(QStringLiteral("Palette 8-bit"));
  m_trueColor->setChecked(true);
  modeLayout->addWidget(m_trueColor);
  modeLayout->addWidget(m_palette);

  m_preview = new QLabel(this);
  m_preview->setObjectName(QStringLiteral("iconPreview"));
  m_preview->setAccessibleName(QStringLiteral("32 pixel preview"));
  m_preview->setAlignment(Qt::AlignCenter);
  m_preview->setFrameShape(QFrame::Box);
  m_preview->setFixedSize(kPreviewPixels, kPreviewPixels);

  m_save = new QPushButton(QStringLiteral("&Save…"), this);
  m_save->setObjectName(QStringLiteral("saveIcon"));
  m_save->setAccessibleName(QStringLiteral("Save icon"));
  m_error = new QLabel(this);
  m_error->setObjectName(QStringLiteral("iconError"));
  m_error->setAccessibleName(QStringLiteral("Error"));
  m_error->setTextFormat(Qt::PlainText);
  m_error->setWordWrap(true);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("iconStatus"));
  m_status->setAccessibleName(QStringLiteral("Status"));
  m_status->setWordWrap(true);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setObjectName(QStringLiteral("iconButtons"));

  auto *topRow = new QHBoxLayout;
  topRow->addWidget(m_path, 1);
  topRow->addWidget(m_choose);
  auto *optionsRow = new QHBoxLayout;
  optionsRow->addWidget(sizeGroup);
  optionsRow->addWidget(modeGroup);
  optionsRow->addWidget(m_preview, 0, Qt::AlignTop);
  optionsRow->addStretch();
  auto *actionRow = new QHBoxLayout;
  actionRow->addWidget(m_save);
  actionRow->addWidget(buttons);
  actionRow->addStretch();
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(topRow);
  layout->addLayout(optionsRow);
  layout->addLayout(actionRow);
  layout->addWidget(m_status);
  layout->addWidget(m_error);
  resize(560, 320);

  m_status->setText(QStringLiteral("Choose or drop an image, then select the sizes to write."));
  m_error->clear();

  connect(m_choose, &QPushButton::clicked, this, &IconConverterDialog::chooseImage);
  connect(m_save, &QPushButton::clicked, this, &IconConverterDialog::save);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  for (auto *box : m_sizes) connect(box, &QCheckBox::toggled, this, [this] { updateSaveState(); });
  connect(m_trueColor, &QRadioButton::toggled, this, [this] { updatePreview(); });
  connect(m_palette, &QRadioButton::toggled, this, [this] { updatePreview(); });
  updateSaveState();
  updatePreview();
}

void IconConverterDialog::setFileChooser(FileChooser chooser) { m_chooser = std::move(chooser); }

void IconConverterDialog::setImage(const QString &path) {
  m_path->setText(path);
  m_source = QImage();
  if (path.isEmpty()) {
    clearError();
    updatePreview();
    updateSaveState();
    return;
  }

  QImageReader reader(path);
  const QSize declared = reader.size();
  if (declared.isValid() && (declared.width() > kMaxSide || declared.height() > kMaxSide)) {
    showError(QStringLiteral("The image is %1 by %2 pixels, which is over the %3 pixel limit.")
                  .arg(declared.width())
                  .arg(declared.height())
                  .arg(kMaxSide));
    updatePreview();
    updateSaveState();
    return;
  }
  const QImage image = reader.read();
  if (image.isNull()) {
    showError(QStringLiteral("The image could not be read."));
    updatePreview();
    updateSaveState();
    return;
  }
  if (image.width() > kMaxSide || image.height() > kMaxSide) {
    showError(QStringLiteral("The image is %1 by %2 pixels, which is over the %3 pixel limit.")
                  .arg(image.width())
                  .arg(image.height())
                  .arg(kMaxSide));
    updatePreview();
    updateSaveState();
    return;
  }
  m_source = image;
  clearError();
  m_status->setText(QStringLiteral("%1 by %2 pixels.").arg(image.width()).arg(image.height()));
  updatePreview();
  updateSaveState();
}

QString IconConverterDialog::imagePath() const { return m_path->text(); }

QString IconConverterDialog::errorText() const { return m_error->text(); }

IconOptions IconConverterDialog::options() const {
  IconOptions options;
  options.sizes = selectedSizes();
  options.mode = mode();
  return options;
}

QList<QCheckBox *> IconConverterDialog::sizeCheckBoxes() const { return m_sizes; }

QList<int> IconConverterDialog::selectedSizes() const {
  QList<int> sizes;
  for (const auto *box : m_sizes)
    if (box->isChecked()) sizes.append(box->property("iconEntrySize").toInt());
  return sizes;
}

void IconConverterDialog::chooseImage() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Choose an image"), m_path->text(),
      QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)"));
  if (!path.isEmpty()) setImage(path);
}

void IconConverterDialog::save() {
  const QList<int> sizes = selectedSizes();
  if (sizes.isEmpty()) {
    showError(QStringLiteral("Choose at least one size before saving."));
    return;
  }
  if (m_source.isNull()) {
    showError(QStringLiteral("Choose an image before saving."));
    return;
  }
  const QString suggested = suggestedPath();
  const QString target = m_chooser ? m_chooser(suggested)
                                   : QFileDialog::getSaveFileName(
                                         this, QStringLiteral("Save icon"), suggested,
                                         QStringLiteral("Windows icons (*.ico)"));
  if (target.isEmpty()) return;  // A cancel writes nothing.

  IconOptions options;
  options.sizes = sizes;
  options.mode = mode();
  // EU-007: encode fully first, then publish atomically, so a failure never
  // leaves a truncated .ico or destroys the file being replaced.
  QString error;
  const QByteArray bytes = encodeIcon(m_source, options, &error);
  if (bytes.isEmpty()) {
    showError(QStringLiteral("The icon could not be written (%1).").arg(error.isEmpty() ? QStringLiteral("write-failed") : error));
    return;
  }
  QSaveFile file(target);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
    showError(QStringLiteral("The icon could not be written (write-failed)."));
    return;
  }
  clearError();
  m_status->setText(QStringLiteral("Icon saved to %1.").arg(QFileInfo(target).fileName()));
}

void IconConverterDialog::updatePreview() {
  if (m_source.isNull()) {
    m_preview->setPixmap(QPixmap());
    m_preview->setText(QStringLiteral("No image"));
    return;
  }
  const QImage entry = iconEntryImage(m_source, kPreviewSize, mode());
  if (entry.isNull()) {
    m_preview->setPixmap(QPixmap());
    m_preview->setText(QStringLiteral("No preview"));
    return;
  }
  m_preview->setText(QString());
  m_preview->setPixmap(
      QPixmap::fromImage(entry).scaled(kPreviewPixels, kPreviewPixels, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

void IconConverterDialog::updateSaveState() {
  // EU-007: a container is written only after at least one size is selected.
  m_save->setEnabled(!selectedSizes().isEmpty());
}

void IconConverterDialog::showError(const QString &message) { m_error->setText(message); }

void IconConverterDialog::clearError() { m_error->clear(); }

IconOptions::Mode IconConverterDialog::mode() const {
  return m_palette->isChecked() ? IconOptions::Mode::Palette8 : IconOptions::Mode::TrueColor32;
}

QString IconConverterDialog::suggestedPath() const {
  if (m_path->text().isEmpty()) return QStringLiteral("icon.ico");
  QFileInfo info(m_path->text());
  const QString directory = info.absolutePath();
  const QString base = info.completeBaseName().isEmpty() ? QStringLiteral("icon") : info.completeBaseName();
  return QDir(directory).filePath(base + QStringLiteral(".ico"));
}

void IconConverterDialog::dragEnterEvent(QDragEnterEvent *event) { acceptLocalFiles(event); }

void IconConverterDialog::dragMoveEvent(QDragMoveEvent *event) { acceptLocalFiles(event); }

void IconConverterDialog::dropEvent(QDropEvent *event) {
  const auto paths = localFilePaths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  setImage(paths.first());
}

}  // namespace xerahs::app
