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
#include <QScopeGuard>
#include <QThreadPool>
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
NewImageDialog::NewImageDialog(QWidget *parent, NewImageHooks hooks) : QDialog(parent), m_hooks(std::move(hooks)) {
  setObjectName(QStringLiteral("newImageDialog")); setWindowTitle(QStringLiteral("New Image"));
  setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
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
  m_initialLabel = new QLabel(QStringLiteral("No initial image"), this); m_initialLabel->setWordWrap(true); m_initialLabel->setTextFormat(Qt::PlainText);
  m_initialLabel->setObjectName(QStringLiteral("newImageInitial")); form->addRow(QStringLiteral("Initial pixels"), m_initialLabel);
  auto *file = new QPushButton(QStringLiteral("Choose initial image…"), this); file->setObjectName(QStringLiteral("chooseNewImageInitial"));
  auto *clipboard = new QPushButton(QStringLiteral("Use clipboard image"), this); clipboard->setObjectName(QStringLiteral("pasteNewImageInitial"));
  m_clearInitial = new QPushButton(QStringLiteral("Remove initial image"), this); m_clearInitial->setObjectName(QStringLiteral("clearNewImageInitial"));
  auto *initialRow = new QHBoxLayout; initialRow->addWidget(file); initialRow->addWidget(clipboard); initialRow->addWidget(m_clearInitial); form->addRow(initialRow);
  connect(file, &QPushButton::clicked, this, &NewImageDialog::chooseFile);
  connect(clipboard, &QPushButton::clicked, this, &NewImageDialog::useClipboard);
  connect(m_clearInitial, &QPushButton::clicked, this, &NewImageDialog::clearInitial);
  layout->addWidget(m_controls);
  auto *policy = new QLabel(QStringLiteral("Initial pixels keep their native size at the top left. The chosen dimensions "
      "crop excess pixels or pad with the background. Initial pixels and the sRGB background colour convert to the selected profile. "
      "Untagged initial images are treated as sRGB."), this); policy->setWordWrap(true); layout->addWidget(policy);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("newImageStatus")); m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setObjectName(QStringLiteral("newImageProgress")); m_progress->setRange(0, 100);
  m_progress->setAccessibleName(QStringLiteral("New image preparation progress"));
  m_cancelWork = new QPushButton(QStringLiteral("Cancel work"), this); m_cancelWork->setObjectName(QStringLiteral("cancelNewImageWork"));
  auto *workRow = new QHBoxLayout; workRow->addWidget(m_progress, 1); workRow->addWidget(m_cancelWork); layout->addLayout(workRow);
  connect(m_cancelWork, &QPushButton::clicked, this, &NewImageDialog::cancelWork);
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
NewImageDialog::~NewImageDialog() { if (m_work) m_work->cancelled.store(true); }
NewImageOptions NewImageDialog::options() const {
  return {m_width->value(), m_height->value(), m_background->currentIndex() == 0 ? Argb(0) : parseColor(m_colour->text()).value_or(0),
      NewImageProfile(m_profile->currentIndex())};
}
QString NewImageDialog::controlStatus() const {
  const auto error = validateNewImage(options());
  const bool validColour = m_background->currentIndex() == 0 || parseColor(m_colour->text()).has_value();
  return error ? describe(*error) : !validColour ? QStringLiteral("Enter a colour as #AARRGGBB, including alpha.") :
      QStringLiteral("Create prepares a new unsaved document. The current document stays unchanged until preparation succeeds.");
}
void NewImageDialog::validateControls() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<NewImageDialog> owner(this); m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  m_status->setText(controlStatus()); if (owner) setBusy(false);
}
void NewImageDialog::clearInitial() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<NewImageDialog> owner(this); m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  m_initial = {}; m_initialLabel->setText(QStringLiteral("No initial image"));
  if (!owner) return;
  m_status->setText(controlStatus()); if (owner) setBusy(false);
}
void NewImageDialog::chooseColour() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<NewImageDialog> owner(this); QColor chosen;
  {
    m_pickerOpen = true; const auto restore = qScopeGuard([owner] { if (owner) owner->m_pickerOpen = false; });
    QPointer<QColorDialog> picker(new QColorDialog(QColor::fromRgba(parseColor(m_colour->text()).value_or(0xFFFFFFFF)), this));
    m_picker = picker; picker->setObjectName("newImageColourPicker"); picker->setWindowTitle("New image background"); picker->setOption(QColorDialog::ShowAlphaChannel);
    const auto answer = picker->exec(); if (picker && answer == QDialog::Accepted) chosen = picker->selectedColor();
    if (picker) picker->hide();
    if (picker) picker->setParent(nullptr);
    delete picker.data();
  }
  if (owner && !m_closing && chosen.isValid()) m_colour->setText(formatColor(chosen.rgba()));
}
void NewImageDialog::chooseFile() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<NewImageDialog> owner(this); QString path;
  {
    m_pickerOpen = true; const auto restore = qScopeGuard([owner] { if (owner) owner->m_pickerOpen = false; });
    QStringList patterns; for (const auto &format : QImageReader::supportedImageFormats()) patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    QPointer<QFileDialog> picker(new QFileDialog(this, QStringLiteral("Initial image pixels"), {}, QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')))));
    m_picker = picker; picker->setObjectName("newImageFilePicker"); picker->setFileMode(QFileDialog::ExistingFile);
    const auto answer = picker->exec(); if (picker && answer == QDialog::Accepted && !picker->selectedFiles().isEmpty()) path = picker->selectedFiles().first();
    if (picker) picker->hide();
    if (picker) picker->setParent(nullptr);
    delete picker.data();
  }
  if (owner && !path.isEmpty()) loadInitialFile(path);
}
void NewImageDialog::useClipboard() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<NewImageDialog> owner(this); QImage image;
  { m_transition = true; const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; }); image = QApplication::clipboard()->image(); }
  if (!owner || m_closing) return;
  if (image.isNull()) { m_status->setText(QStringLiteral("The clipboard does not contain a supported image. The initial image is unchanged.")); return; }
  setInitialImage(image, QStringLiteral("Clipboard image"));
}
void NewImageDialog::publishInitial(QImage image, const QString &label) {
  const QPointer<NewImageDialog> owner(this);
  const int width = image.width(), height = image.height(); m_initial = std::move(image);
  // Block intermediate dimension notifications; observers see one complete input.
  const QPointer<QSpinBox> w(m_width), h(m_height); const bool blockedW = w->blockSignals(true), blockedH = h->blockSignals(true);
  const auto restore = qScopeGuard([w, h, blockedW, blockedH] { if (w) w->blockSignals(blockedW); if (h) h->blockSignals(blockedH); });
  w->setValue(width); if (!owner) return;
  h->setValue(height); if (!owner) return;
  m_initialLabel->setText(QStringLiteral("%1 · %2 × %3 pixels").arg(label).arg(width).arg(height));
  if (!owner) return;
}
bool NewImageDialog::setInitialImage(const QImage &image, const QString &label) {
  if (m_closing || m_work || m_transition || m_pickerOpen) return false;
  const QPointer<NewImageDialog> owner(this); m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  if (auto error = validateNewImage({image.width(), image.height()})) { m_status->setText(describe(*error)); return false; }
  auto owned = image.copy(); if (owned.isNull()) { m_status->setText(describe(diagnostic::documentTooLarge)); return false; }
  publishInitial(std::move(owned), label); if (!owner || m_closing) return false;
  m_status->setText(controlStatus()); if (!owner || m_closing) return false;
  setBusy(false); return owner && !m_closing;
}
bool NewImageDialog::loadInitialFile(const QString &path) { return !path.isEmpty() && startWork(path, true); }
bool NewImageDialog::startWork(const QString &path, bool load) {
  if (m_closing || m_work || m_transition || m_pickerOpen) return false;
  const QPointer<NewImageDialog> owner(this); m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  const auto requested = options(); const QImage initial = m_initial; const auto hooks = m_hooks;
  const auto work = std::make_shared<Work>(); work->load = load; work->label = QFileInfo(path).fileName(); m_work = work;
  try {
    QThreadPool::globalInstance()->start([requested, initial, path, hooks, work] {
      const RenderControl callbacks{[work] { return work->cancelled.load(); }, [work](int p) { work->progress.store(p); }};
      try {
        if (work->cancelled.load()) { LoadedRaster failed; failed.diagnostic = "canvas-cancelled"; work->result = failed; }
        else { if (hooks.beforePrepare) hooks.beforePrepare(); if (work->load) work->result = loadRaster(path, callbacks); else work->result = prepareNewImage(requested, initial, callbacks); }
      } catch (...) { LoadedRaster failed; failed.diagnostic = "new-image-preparation-failed"; work->result = failed; }
      work->ready.store(true, std::memory_order_release);
    });
  } catch (...) { work->cancelled.store(true); m_work.reset(); m_status->setText(QStringLiteral("Image preparation could not start. The current document is unchanged.")); return false; }
  m_status->setText(load ? QStringLiteral("Loading initial pixels…") : QStringLiteral("Preparing new image…")); if (!owner || m_closing || m_work != work) return false;
  m_progress->setValue(0); if (!owner || m_closing || m_work != work) return false;
  setBusy(true); if (!owner || m_closing || m_work != work) return false;
  m_poll.start(); return true;
}
void NewImageDialog::setBusy(bool busy) {
  const QPointer<NewImageDialog> owner(this);
  const auto current = [owner, busy] { return owner && !owner->m_closing && bool(owner->m_work) == busy; };
  m_progress->setVisible(busy); if (!current()) return;
  m_cancelWork->setVisible(busy); if (!current()) return;
  m_cancelWork->setEnabled(busy); if (!current()) return;
  const bool chosen = m_background->currentIndex() != 0;
  m_colour->setEnabled(chosen); if (!current()) return;
  m_chooseColour->setEnabled(chosen); if (!current()) return;
  m_clearInitial->setEnabled(!m_initial.isNull()); if (!current()) return;
  m_controls->setEnabled(!busy); if (!current()) return;
  m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(true); if (!current()) return;
  const bool valid = !validateNewImage(options()) && (!chosen || parseColor(m_colour->text()).has_value());
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!busy && valid);
}
void NewImageDialog::cancelWork() {
  if (m_closing || !m_work) return;
  const QPointer<NewImageDialog> owner(this); const bool transition = m_transition; m_transition = true;
  const auto restore = qScopeGuard([owner, transition] { if (owner) owner->m_transition = transition; });
  m_work->cancelled.store(true); m_work.reset(); m_poll.stop();
  m_status->setText(QStringLiteral("Work cancelled. The current document and prior initial image are unchanged."));
  if (owner && !m_closing) setBusy(false);
}
void NewImageDialog::pollWork() {
  if (m_closing || m_transition || !m_work) return;
  const QPointer<NewImageDialog> owner(this); const auto work = m_work; m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  m_progress->setValue(work->progress.load()); if (!owner || m_closing || m_work != work || !work->ready.load(std::memory_order_acquire)) return;
  m_progress->setValue(100); if (!owner || m_closing || m_work != work) return;
  auto result = std::move(work->result); m_work.reset(); m_poll.stop();
  if (work->cancelled.load()) m_status->setText(QStringLiteral("Work cancelled. The current document and prior initial image are unchanged."));
  else if (auto *loaded = std::get_if<LoadedRaster>(&result)) {
    if (loaded->diagnostic == u"new-image-preparation-failed") m_status->setText(QStringLiteral("Image preparation failed. The current document and prior initial image are unchanged."));
    else if (!loaded->diagnostic.isEmpty()) m_status->setText(describe(loaded->diagnostic));
    else if (work->load) { publishInitial(std::move(loaded->image), work->label); if (!owner || m_closing) return; m_status->setText(controlStatus()); }
  } else {
    auto prepared = std::get<PreparedNewImage>(std::move(result));
    if (!prepared.document) m_status->setText(describe(prepared.diagnostic));
    else { m_prepared = std::move(prepared); done(QDialog::Accepted); return; }
  }
  if (owner && !m_closing) setBusy(false);
}
void NewImageDialog::accept() {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  if (validateNewImage(options()) || (m_background->currentIndex() != 0 && !parseColor(m_colour->text()))) { validateControls(); return; }
  startWork({}, false);
}
void NewImageDialog::reject() { done(QDialog::Rejected); }
void NewImageDialog::done(int result) {
  if (m_closing || (result == QDialog::Accepted && !m_prepared)) return;
  m_closing = true; if (m_work) m_work->cancelled.store(true); m_work.reset(); m_poll.stop();
  if (result != QDialog::Accepted) m_prepared.reset();
  const QPointer<NewImageDialog> owner(this); if (m_picker) m_picker->reject(); if (owner) QDialog::done(result);
}
std::optional<PreparedNewImage> NewImageDialog::takeImage() {
  if (result() != QDialog::Accepted || !m_prepared) return std::nullopt;
  return std::exchange(m_prepared, std::nullopt);
}
} // namespace xerahs::app
