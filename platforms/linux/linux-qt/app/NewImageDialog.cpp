#include "NewImageDialog.h"
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <utility>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
QString describe(const QString &error) {
  if (error == diagnostic::documentTooLarge || error == diagnostic::sourceTooLarge)
    return QStringLiteral("The image exceeds the dimensions or memory limits. Choose a smaller size.");
  if (error == diagnostic::sourceUnsupported) return QStringLiteral("The selected input is not a supported image.");
  if (error == diagnostic::sourceEmpty) return QStringLiteral("The selected file is empty.");
  if (error == diagnostic::sourceCorrupt) return QStringLiteral("The selected image is damaged and could not be decoded.");
  if (error == u"canvas-encoding-failed") return QStringLiteral("The new source image could not be encoded.");
  return QStringLiteral("The new image data is invalid. Check the size, colour and profile.");
}
}
NewImageDialog::NewImageDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("newImageDialog")); setWindowTitle(QStringLiteral("New Image"));
  auto *layout = new QVBoxLayout(this);
  m_controls = new QWidget(this); auto *form = new QFormLayout(m_controls);
  m_width = new QSpinBox(this); m_height = new QSpinBox(this);
  m_width->setRange(1, int(kMaxDimension)); m_height->setRange(1, int(kMaxDimension));
  m_width->setValue(800); m_height->setValue(600);
  m_width->setObjectName(QStringLiteral("newImageWidth")); m_height->setObjectName(QStringLiteral("newImageHeight"));
  m_width->setAccessibleName(QStringLiteral("New image width in pixels"));
  m_height->setAccessibleName(QStringLiteral("New image height in pixels"));
  form->addRow(QStringLiteral("&Width (pixels)"), m_width); form->addRow(QStringLiteral("&Height (pixels)"), m_height);
  m_background = new QComboBox(this); m_background->setObjectName(QStringLiteral("newImageBackground"));
  m_background->setAccessibleName(QStringLiteral("New image background"));
  m_background->addItems({QStringLiteral("Transparent"), QStringLiteral("Chosen colour and alpha")});
  form->addRow(QStringLiteral("&Background"), m_background);
  m_colour = new QLineEdit(QStringLiteral("#FFFFFFFF"), this); m_colour->setObjectName(QStringLiteral("newImageColour"));
  m_colour->setAccessibleName(QStringLiteral("Background colour, ARGB hexadecimal"));
  m_colour->setAccessibleDescription(QStringLiteral("Eight hexadecimal digits: alpha, red, green, blue, each from 00 to FF."));
  m_chooseColour = new QPushButton(QStringLiteral("Choose colour…"), this);
  m_chooseColour->setObjectName(QStringLiteral("chooseNewImageColour"));
  auto *colourRow = new QHBoxLayout; colourRow->addWidget(m_colour); colourRow->addWidget(m_chooseColour);
  form->addRow(QStringLiteral("&Colour (#AARRGGBB)"), colourRow);
  qobject_cast<QLabel *>(form->labelForField(colourRow))->setBuddy(m_colour);
  connect(m_chooseColour, &QPushButton::clicked, this, &NewImageDialog::chooseColour);
  m_profile = new QComboBox(this); m_profile->setObjectName(QStringLiteral("newImageProfile"));
  m_profile->setAccessibleName(QStringLiteral("New image colour profile"));
  m_profile->addItems({QStringLiteral("sRGB"), QStringLiteral("Linear sRGB"), QStringLiteral("Display P3"), QStringLiteral("Adobe RGB")});
  form->addRow(QStringLiteral("Colour &profile"), m_profile);
  m_initialLabel = new QLabel(QStringLiteral("No initial image"), this); m_initialLabel->setWordWrap(true);
  m_initialLabel->setObjectName(QStringLiteral("newImageInitial")); form->addRow(QStringLiteral("Initial pixels"), m_initialLabel);
  auto *file = new QPushButton(QStringLiteral("Choose initial image…"), this); file->setObjectName(QStringLiteral("chooseNewImageInitial"));
  auto *clipboard = new QPushButton(QStringLiteral("Use clipboard image"), this); clipboard->setObjectName(QStringLiteral("pasteNewImageInitial"));
  m_clearInitial = new QPushButton(QStringLiteral("Remove initial image"), this); m_clearInitial->setObjectName(QStringLiteral("clearNewImageInitial"));
  auto *initialRow = new QHBoxLayout; initialRow->addWidget(file); initialRow->addWidget(clipboard); initialRow->addWidget(m_clearInitial); form->addRow(initialRow);
  connect(file, &QPushButton::clicked, this, &NewImageDialog::chooseFile);
  connect(clipboard, &QPushButton::clicked, this, &NewImageDialog::useClipboard);
  connect(m_clearInitial, &QPushButton::clicked, this, [this] {
    m_initial = {}; m_initialLabel->setText(QStringLiteral("No initial image")); validateControls();
  });
  layout->addWidget(m_controls);
  auto *policy = new QLabel(QStringLiteral("Initial pixels keep their native size at the top left. The chosen dimensions "
      "crop excess pixels or pad with the background. Initial pixels and the sRGB background colour convert to the selected profile. "
      "Untagged initial images are treated as sRGB."), this); policy->setWordWrap(true); layout->addWidget(policy);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("newImageStatus")); m_status->setWordWrap(true); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setObjectName(QStringLiteral("newImageProgress")); m_progress->setRange(0, 100);
  m_progress->setAccessibleName(QStringLiteral("New image preparation progress"));
  m_cancelWork = new QPushButton(QStringLiteral("Cancel work"), this); m_cancelWork->setObjectName(QStringLiteral("cancelNewImageWork"));
  auto *workRow = new QHBoxLayout; workRow->addWidget(m_progress, 1); workRow->addWidget(m_cancelWork); layout->addLayout(workRow);
  connect(m_cancelWork, &QPushButton::clicked, this, [this] {
    if (m_control) { m_control->cancelled.store(true); m_cancelWork->setEnabled(false); m_status->setText(QStringLiteral("Cancelling…")); }
  });
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
  connect(m_buttons, &QDialogButtonBox::accepted, this, &NewImageDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &NewImageDialog::reject); layout->addWidget(m_buttons);
  connect(m_width, &QSpinBox::valueChanged, this, &NewImageDialog::validateControls);
  connect(m_height, &QSpinBox::valueChanged, this, &NewImageDialog::validateControls);
  connect(m_background, &QComboBox::currentIndexChanged, this, &NewImageDialog::validateControls);
  connect(m_profile, &QComboBox::currentIndexChanged, this, &NewImageDialog::validateControls);
  connect(m_colour, &QLineEdit::textChanged, this, &NewImageDialog::validateControls);
  m_poll.setInterval(20); connect(&m_poll, &QTimer::timeout, this, &NewImageDialog::pollWork);
  setBusy(false); validateControls(); resize(620, sizeHint().height());
}
NewImageDialog::~NewImageDialog() { if (m_control) m_control->cancelled.store(true); if (m_worker.valid()) m_worker.wait(); }
NewImageOptions NewImageDialog::options() const {
  return {m_width->value(), m_height->value(), m_background->currentIndex() == 0 ? Argb(0) : parseColor(m_colour->text()).value_or(0),
      NewImageProfile(m_profile->currentIndex())};
}
void NewImageDialog::validateControls() {
  const bool chosen = m_background->currentIndex() != 0;
  m_colour->setEnabled(chosen); m_chooseColour->setEnabled(chosen); m_clearInitial->setEnabled(!m_initial.isNull());
  if (m_worker.valid() || m_closing) return;
  const auto error = validateNewImage(options());
  const bool validColour = !chosen || parseColor(m_colour->text()).has_value();
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!error && validColour);
  m_status->setText(error ? describe(*error) : !validColour ? QStringLiteral("Enter a colour as #AARRGGBB, including alpha.") :
      QStringLiteral("Create prepares a new unsaved document. The current document stays unchanged until preparation succeeds."));
}
void NewImageDialog::chooseColour() {
  const QColor colour = QColorDialog::getColor(QColor::fromRgba(parseColor(m_colour->text()).value_or(0xFFFFFFFF)), this,
      QStringLiteral("New image background"), QColorDialog::ShowAlphaChannel);
  if (colour.isValid()) m_colour->setText(formatColor(colour.rgba()));
}
void NewImageDialog::chooseFile() {
  QStringList patterns; for (const auto &format : QImageReader::supportedImageFormats()) patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Initial image pixels"), {},
      QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' '))));
  if (!path.isEmpty()) loadInitialFile(path);
}
void NewImageDialog::useClipboard() {
  const QImage image = QApplication::clipboard()->image();
  if (image.isNull()) { m_status->setText(QStringLiteral("The clipboard does not contain a supported image. The initial image is unchanged.")); return; }
  setInitialImage(image, QStringLiteral("Clipboard image"));
}
bool NewImageDialog::setInitialImage(const QImage &image, const QString &label) {
  if (m_closing || m_worker.valid()) return false;
  if (auto error = validateNewImage({image.width(), image.height()})) { m_status->setText(describe(*error)); return false; }
  m_initial = image; m_width->setValue(image.width()); m_height->setValue(image.height());
  m_initialLabel->setText(QStringLiteral("%1 · %2 × %3 pixels").arg(label).arg(image.width()).arg(image.height()));
  validateControls(); return true;
}
bool NewImageDialog::loadInitialFile(const QString &path) { return !path.isEmpty() && startWork(path, true); }
bool NewImageDialog::startWork(const QString &path, bool load) {
  if (m_closing || m_worker.valid()) return false;
  const auto requested = options(); const QImage initial = m_initial;
  try {
    m_control = std::make_shared<WorkControl>();
    m_worker = std::async(std::launch::async, [requested, initial, path, load, control = m_control]() -> WorkResult {
      const RenderControl callbacks{[control] { return control->cancelled.load(); }, [control](int p) { control->progress.store(p); }};
      if (load) return loadRaster(path, callbacks);
      return prepareNewImage(requested, initial, callbacks);
    });
  } catch (const std::exception &) { m_status->setText(QStringLiteral("Image preparation could not start. The current document is unchanged.")); return false; }
  m_loading = load; m_initialName = QFileInfo(path).fileName(); m_progress->setValue(0);
  setBusy(true); m_status->setText(load ? QStringLiteral("Loading initial pixels…") : QStringLiteral("Preparing new image…")); m_poll.start(); return true;
}
void NewImageDialog::setBusy(bool busy) {
  m_controls->setEnabled(!busy && !m_closing); m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy && !m_closing);
  m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(!m_closing); m_progress->setVisible(busy); m_cancelWork->setVisible(busy);
  m_cancelWork->setEnabled(busy && !m_closing);
}
void NewImageDialog::pollWork() {
  if (!m_worker.valid()) return;
  m_progress->setValue(m_control->progress.load());
  if (m_worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
  std::optional<WorkResult> result;
  try { result = m_worker.get(); } catch (const std::exception &) { /* Report below after restoring controls. */ }
  m_poll.stop();
  if (m_closing) { QDialog::reject(); return; }
  setBusy(false); validateControls();
  if (m_control->cancelled.load()) { m_status->setText(QStringLiteral("Work cancelled. The current document and prior initial image are unchanged.")); return; }
  if (!result) { m_status->setText(QStringLiteral("Image preparation failed. The current document and prior initial image are unchanged.")); return; }
  if (m_loading) {
    auto &loaded = std::get<LoadedRaster>(*result);
    if (!loaded.diagnostic.isEmpty()) { m_status->setText(describe(loaded.diagnostic)); return; }
    setInitialImage(loaded.image, m_initialName); return;
  }
  auto prepared = std::get<PreparedNewImage>(std::move(*result));
  if (!prepared.document) { m_status->setText(describe(prepared.diagnostic)); return; }
  m_prepared = std::move(prepared); QDialog::accept();
}
void NewImageDialog::accept() {
  if (m_closing || m_worker.valid()) return;
  validateControls(); if (!m_buttons->button(QDialogButtonBox::Ok)->isEnabled()) return;
  startWork({}, false);
}
void NewImageDialog::reject() {
  m_closing = true; if (m_control) m_control->cancelled.store(true);
  if (!m_worker.valid()) { QDialog::reject(); return; }
  setBusy(true); m_status->setText(QStringLiteral("Cancelling image preparation…"));
}
std::optional<PreparedNewImage> NewImageDialog::takeImage() {
  if (result() != QDialog::Accepted || !m_prepared) return std::nullopt;
  return std::exchange(m_prepared, std::nullopt);
}
}  // namespace xerahs::app
