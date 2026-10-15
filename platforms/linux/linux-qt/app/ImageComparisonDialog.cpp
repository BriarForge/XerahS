#include "ImageComparisonDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScopeGuard>
#include <QThreadPool>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
QString describe(const QString &error) {
  if (error == diagnostic::sourceUnsupported)
    return QStringLiteral("The selected file is not a supported image.");
  if (error == diagnostic::sourceCorrupt || error == diagnostic::documentInvalid)
    return QStringLiteral("The image or annotation data is damaged or invalid.");
  if (error == diagnostic::sourceEmpty)
    return QStringLiteral("The selected file is empty.");
  if (error == diagnostic::sourceTooLarge || error == diagnostic::documentTooLarge)
    return QStringLiteral("The image or combined comparison canvas exceeds the size or memory limits.");
  if (error == u"comparison-annotation-unsupported")
    return QStringLiteral(
        "The current image contains visible annotation types this editor cannot render. Choose a raster file instead.");
  return QStringLiteral("The image could not be prepared for comparison.");
}
} // namespace

ComparisonView::ComparisonView(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("comparisonView"));
  setAccessibleName(QStringLiteral("Image comparison"));
  setAccessibleDescription(QStringLiteral("First image left of the divider; second image right. Drag to move the "
                                          "divider, or use arrow keys and Home/End."));
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::SplitHCursor);
  setMinimumSize(320, 240);
}
void ComparisonView::setImages(QImage first, QImage second, ComparisonLayout layout) {
  m_images = {std::move(first), std::move(second)};
  m_layout = layout;
  update();
}
void ComparisonView::setReveal(int percent) {
  m_reveal = std::clamp(percent, 0, 100);
  update();
}
QRectF ComparisonView::imageRect() const {
  if (!m_layout)
    return {};
  const QRectF available = QRectF(contentsRect()).adjusted(8, 8, -8, -8);
  if (available.width() <= 0 || available.height() <= 0)
    return {};
  const double scale =
      std::min(available.width() / m_layout->canvas.width, available.height() / m_layout->canvas.height);
  const QSizeF size(m_layout->canvas.width * scale, m_layout->canvas.height * scale);
  return {available.center() - QPointF(size.width() / 2, size.height() / 2), size};
}
QPointF ComparisonView::toView(QPointF logicalPoint) const {
  const QRectF target = imageRect();
  if (!m_layout || target.isEmpty())
    return {};
  return target.topLeft() + logicalPoint * (target.width() / m_layout->canvas.width);
}
void ComparisonView::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().dark());
  const QRectF target = imageRect();
  if (!m_layout || target.isEmpty()) {
    painter.setPen(palette().text().color());
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Choose two images to compare."));
    return;
  }
  QPixmap tile(16, 16);
  tile.fill(Qt::white);
  {
    QPainter checker(&tile);
    checker.fillRect(0, 0, 8, 8, Qt::lightGray);
    checker.fillRect(8, 8, 8, 8, Qt::lightGray);
  }
  painter.fillRect(target, QBrush(tile));
  const qint64 split = *comparisonSplitColumn(*m_layout, m_reveal * 10);
  const double scale = target.width() / m_layout->canvas.width;
  painter.save();
  painter.translate(target.topLeft());
  painter.scale(scale, scale);
  for (int input = 0; input < 2; ++input) {
    painter.save();
    painter.setClipRect(input == 0 ? QRectF(0, 0, split, m_layout->canvas.height)
                                   : QRectF(split, 0, m_layout->canvas.width - split, m_layout->canvas.height));
    const PointF offset = input == 0 ? m_layout->firstOffset : m_layout->secondOffset;
    painter.drawImage(QPointF(offset.x, offset.y), m_images[input]);
    painter.restore();
  }
  painter.restore();
  const double x = target.left() + split * scale;
  painter.setPen(QPen(Qt::white, 3));
  painter.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
  painter.setPen(QPen(Qt::black, 1, Qt::DashLine));
  painter.drawLine(QPointF(x, target.top()), QPointF(x, target.bottom()));
  if (hasFocus()) {
    painter.setPen(QPen(palette().highlight().color(), 2, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
  }
}
void ComparisonView::revealAt(QPointF point) {
  const QRectF target = imageRect();
  if (target.isEmpty())
    return;
  emit revealRequested(int(std::round(std::clamp((point.x() - target.left()) / target.width(), 0.0, 1.0) * 100)));
}
void ComparisonView::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_layout) {
    const QPointer<ComparisonView> owner(this);
    setFocus();
    if (!owner) return;
    m_dragging = true;
    revealAt(event->position());
  } else
    QWidget::mousePressEvent(event);
}
void ComparisonView::mouseMoveEvent(QMouseEvent *event) {
  if (m_dragging)
    revealAt(event->position());
  else
    QWidget::mouseMoveEvent(event);
}
void ComparisonView::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && m_dragging) {
    m_dragging = false;
    revealAt(event->position());
  } else
    QWidget::mouseReleaseEvent(event);
}
void ComparisonView::keyPressEvent(QKeyEvent *event) {
  if (!m_layout) {
    QWidget::keyPressEvent(event);
    return;
  }
  if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    emit revealRequested(std::clamp(m_reveal + (event->key() == Qt::Key_Left ? -1 : 1), 0, 100));
  else if (event->key() == Qt::Key_Home)
    emit revealRequested(0);
  else if (event->key() == Qt::Key_End)
    emit revealRequested(100);
  else
    QWidget::keyPressEvent(event);
}
void ComparisonView::focusOutEvent(QFocusEvent *event) {
  m_dragging = false;
  QWidget::focusOutEvent(event);
}

ImageComparisonDialog::ImageComparisonDialog(AnnotationDocument document, QImage source, QWidget *parent, ComparisonHooks hooks)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)), m_hooks(std::move(hooks)) {
  setObjectName(QStringLiteral("imageComparisonDialog"));
  setWindowTitle(QStringLiteral("Compare Images"));
  setAccessibleName(windowTitle());
  setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  for (int input = 0; input < 2; ++input) {
    auto *row = new QHBoxLayout;
    m_inputLabels[input] = new QLabel(QStringLiteral("No image selected"), this);
    m_inputLabels[input]->setObjectName(QStringLiteral("comparisonInput%1").arg(input));
    m_inputLabels[input]->setWordWrap(true);
    m_inputLabels[input]->setTextFormat(Qt::PlainText);
    row->addWidget(m_inputLabels[input], 1);
    m_inputButtons[input] = new QPushButton(QStringLiteral("Choose image…"), this);
    m_inputButtons[input]->setObjectName(QStringLiteral("chooseComparisonInput%1").arg(input));
    m_inputButtons[input]->setAccessibleName(input == 0 ? QStringLiteral("Choose first raster image")
                                                        : QStringLiteral("Choose second raster image"));
    connect(m_inputButtons[input], &QPushButton::clicked, this, [this, input] { chooseFile(input); });
    row->addWidget(m_inputButtons[input]);
    form->addRow(input == 0 ? QStringLiteral("First (left)") : QStringLiteral("Second (right)"), row);
  }
  m_inputButtons[2] = new QPushButton(QStringLiteral("Use Current Rendered Image"), this);
  m_inputButtons[2]->setObjectName(QStringLiteral("useCurrentComparisonImage"));
  connect(m_inputButtons[2], &QPushButton::clicked, this, [this] { useCurrentImage(); });
  form->addRow(m_inputButtons[2]);
  m_alignment = new QComboBox(this);
  m_alignment->setObjectName(QStringLiteral("comparisonAlignment"));
  m_alignment->addItems(
      {QStringLiteral("Top left, native pixel sizes"), QStringLiteral("Centered, native pixel sizes")});
  form->addRow(QStringLiteral("&Align images"), m_alignment);
  layout->addLayout(form);
  auto *rules = new QLabel(QStringLiteral("Compare raster files or a snapshot of the current rendered image. "
                                          "Pixel sizes stay unchanged; unmatched areas are transparent. Display copies "
                                          "use sRGB (untagged images are treated as sRGB)."),
                           this);
  rules->setWordWrap(true);
  layout->addWidget(rules);
  m_view = new ComparisonView(this);
  layout->addWidget(m_view, 1);
  m_reveal = new QSlider(Qt::Horizontal, this);
  m_reveal->setObjectName(QStringLiteral("comparisonDivider"));
  m_reveal->setRange(0, 100);
  m_reveal->setValue(50);
  m_reveal->setSingleStep(1);
  m_reveal->setPageStep(10);
  m_reveal->setAccessibleName(QStringLiteral("First image percentage"));
  m_reveal->setAccessibleDescription(QStringLiteral("First image left of the divider. Arrow keys change percentage; "
                                                    "Home shows the second image, End shows the first."));
  m_percent = new QLabel(this);
  m_percent->setBuddy(m_reveal);
  m_percent->setObjectName(QStringLiteral("comparisonPercent"));
  auto *revealRow = new QHBoxLayout;
  revealRow->addWidget(m_percent);
  revealRow->addWidget(m_reveal, 1);
  layout->addLayout(revealRow);
  m_geometry = new QLabel(this);
  m_geometry->setObjectName(QStringLiteral("comparisonGeometry"));
  m_geometry->setWordWrap(true);
  layout->addWidget(m_geometry);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("comparisonStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  layout->addWidget(m_status);
  m_progress = new QProgressBar(this);
  m_progress->setRange(0, 100);
  m_progress->setObjectName(QStringLiteral("comparisonProgress"));
  m_progress->setAccessibleName(QStringLiteral("Comparison image loading progress"));
  m_cancelLoad = new QPushButton(QStringLiteral("Cancel Loading"), this);
  m_cancelLoad->setObjectName(QStringLiteral("cancelComparisonLoad"));
  auto *progressRow = new QHBoxLayout;
  progressRow->addWidget(m_progress, 1);
  progressRow->addWidget(m_cancelLoad);
  layout->addLayout(progressRow);
  connect(m_cancelLoad, &QPushButton::clicked, this, &ImageComparisonDialog::cancelLoad);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &ImageComparisonDialog::reject);
  layout->addWidget(m_buttons);
  connect(m_alignment, &QComboBox::currentIndexChanged, this, [this] { updateView(); });
  connect(m_reveal, &QSlider::valueChanged, this, [this](int p) {
    const QPointer<ImageComparisonDialog> owner(this);
    m_view->setReveal(p);
    if (!owner) return;
    m_percent->setText(QStringLiteral("&First image: %1%").arg(p));
  });
  connect(m_view, &ComparisonView::revealRequested, m_reveal, &QSlider::setValue);
  m_view->setReveal(50);
  m_percent->setText(QStringLiteral("&First image: 50%"));
  connect(&m_poll, &QTimer::timeout, this, &ImageComparisonDialog::pollLoad);
  m_poll.setInterval(20);
  setBusy(false);
  updateView();
  resize(800, 650);
  QTimer::singleShot(0, this, [this] { useCurrentImage(); });
}
ImageComparisonDialog::~ImageComparisonDialog() {
  if (m_work) m_work->cancelled.store(true);
}
void ImageComparisonDialog::chooseFile(int input) {
  if (m_closing || m_work || m_transition || m_pickerOpen) return;
  const QPointer<ImageComparisonDialog> owner(this);
  QString path;
  {
    m_pickerOpen = true;
    const auto restore = qScopeGuard([owner] { if (owner) owner->m_pickerOpen = false; });
    QStringList patterns;
    for (const auto &format : QImageReader::supportedImageFormats())
      patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
    QPointer<QFileDialog> picker(new QFileDialog(this,
        input == 0 ? QStringLiteral("Compare first raster image") : QStringLiteral("Compare second raster image"),
        {}, QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')))));
    m_picker = picker;
    picker->setObjectName(QStringLiteral("comparisonFilePicker"));
    picker->setFileMode(QFileDialog::ExistingFile);
    picker->setWindowModality(Qt::WindowModal);
    const int answer = picker->exec();
    if (picker && answer == QDialog::Accepted && !picker->selectedFiles().isEmpty()) path = picker->selectedFiles().first();
    if (picker) picker->hide();
    if (picker) picker->setParent(nullptr);
    delete picker.data();
  }
  if (owner && !path.isEmpty()) loadFile(input, path);
}
bool ImageComparisonDialog::loadFile(int input, const QString &path) {
  return startLoad(input, path, false);
}
bool ImageComparisonDialog::useCurrentImage() {
  return startLoad(0, {}, true);
}
bool ImageComparisonDialog::startLoad(int input, const QString &path, bool current) {
  if (m_closing || m_work || m_transition || m_pickerOpen || input < 0 || input > 1 || (!current && path.isEmpty()))
    return false;
  const QPointer<ImageComparisonDialog> owner(this);
  m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  // Freeze owned inputs before any native notification can reenter or destroy us.
  const auto source = m_source;
  const auto annotations = m_document.annotations;
  const auto hooks = m_hooks;
  const auto work = std::make_shared<Work>();
  work->input = input;
  work->label = current ? QStringLiteral("Current rendered image") : QFileInfo(path).fileName();
  m_work = work;
  try {
    QThreadPool::globalInstance()->start([source, annotations, hooks, path, current, work] {
      const RenderControl callbacks{[work] { return work->cancelled.load(); },
                                    [work](int p) { work->progress.store(p); }};
      try {
        if (work->cancelled.load()) work->prepared = {{}, QStringLiteral("canvas-cancelled")};
        else {
          if (hooks.beforePrepare) hooks.beforePrepare();
          work->prepared = current ? prepareComparisonImage(source, annotations, callbacks)
                                   : loadComparisonImage(path, callbacks);
        }
      } catch (...) {
        work->prepared = {{}, QStringLiteral("comparison-preparation-failed")};
      }
      work->ready.store(true, std::memory_order_release);
    });
  } catch (...) {
    work->cancelled.store(true);
    m_work.reset();
    m_status->setText(QStringLiteral("Image loading could not be started. The previous comparison is kept."));
    return false;
  }
  m_status->setText(QStringLiteral("Preparing %1…").arg(work->label));
  if (!owner || m_closing || m_work != work) return false;
  m_progress->setValue(0);
  if (!owner || m_closing || m_work != work) return false;
  setBusy(true);
  if (!owner || m_closing || m_work != work) return false;
  m_poll.start();
  return true;
}
void ImageComparisonDialog::setBusy(bool busy) {
  const QPointer<ImageComparisonDialog> owner(this);
  const auto current = [owner, busy] { return owner && !owner->m_closing && bool(owner->m_work) == busy; };
  m_progress->setVisible(busy);
  if (!current()) return;
  m_cancelLoad->setVisible(busy);
  if (!current()) return;
  m_cancelLoad->setEnabled(busy && !m_closing);
  if (!current()) return;
  m_alignment->setEnabled(!busy && !m_closing);
  if (!current()) return;
  for (auto *button : m_inputButtons) {
    button->setEnabled(!busy && !m_closing);
    if (!current()) return;
  }
}
void ImageComparisonDialog::cancelLoad() {
  if (m_closing || !m_work) return;
  const QPointer<ImageComparisonDialog> owner(this);
  const bool transition = m_transition;
  m_transition = true;
  const auto restore = qScopeGuard([owner, transition] { if (owner) owner->m_transition = transition; });
  m_work->cancelled.store(true);
  m_work.reset();
  m_poll.stop();
  m_status->setText(QStringLiteral("Load cancelled. The previous comparison is kept."));
  if (owner && !m_closing) setBusy(false);
}
void ImageComparisonDialog::pollLoad() {
  if (m_closing || m_transition || !m_work) return;
  const auto work = m_work;
  const QPointer<ImageComparisonDialog> owner(this);
  m_transition = true;
  const auto restore = qScopeGuard([owner] { if (owner) owner->m_transition = false; });
  m_progress->setValue(work->progress.load());
  if (!owner || m_closing || m_work != work || !work->ready.load(std::memory_order_acquire)) return;
  if (work->prepared.diagnostic.isEmpty()) {
    m_progress->setValue(100);
    if (!owner || m_closing || m_work != work) return;
  }
  auto prepared = std::move(work->prepared);
  m_poll.stop();
  m_work.reset();
  // Retire this identity before native publication. Reentrant loads stay blocked
  // until the complete pair, geometry, status and enabled controls are published.
  QString status;
  if (work->cancelled.load() || prepared.diagnostic == u"canvas-cancelled") {
    status = QStringLiteral("Load cancelled. The previous comparison is kept.");
  } else if (!prepared.diagnostic.isEmpty()) {
    status = describe(prepared.diagnostic);
  } else {
    auto candidate = m_inputs;
    candidate[work->input] = {std::move(prepared.image), work->label};
    if (candidate[work->input].image.isNull()) {
      status = describe(diagnostic::documentInvalid);
    } else if (!candidate[0].image.isNull() && !candidate[1].image.isNull()) {
      const auto geometry = comparisonLayout({candidate[0].image.width(), candidate[0].image.height()},
                                             {candidate[1].image.width(), candidate[1].image.height()},
                                             ComparisonAlignment(m_alignment->currentIndex()));
      if (geometry.error) status = describe(*geometry.error);
    }
    if (status.isEmpty()) {
      m_inputs = std::move(candidate);
      status = m_inputs[0].image.isNull() || m_inputs[1].image.isNull()
                   ? QStringLiteral("Choose the other image to compare.")
                   : QStringLiteral("Move the divider or use arrow keys, Home and End.");
      updateView();
      if (!owner || m_closing) return;
    }
  }
  m_status->setText(status);
  if (owner && !m_closing) setBusy(false);
}
void ImageComparisonDialog::updateView() {
  if (m_closing) return;
  const QPointer<ImageComparisonDialog> owner(this);
  for (int input = 0; input < 2; ++input) {
    if (!m_inputs[input].image.isNull())
      m_inputLabels[input]->setText(QStringLiteral("%1 · %2 × %3")
                                        .arg(m_inputs[input].label)
                                        .arg(m_inputs[input].image.width())
                                        .arg(m_inputs[input].image.height()));
    if (!owner || m_closing) return;
  }
  const bool ready = !m_inputs[0].image.isNull() && !m_inputs[1].image.isNull();
  m_reveal->setEnabled(ready);
  if (!owner || m_closing) return;
  if (!ready) {
    m_geometry->setText(QStringLiteral("Both images must be validated before comparison."));
    return;
  }
  const auto geometry = comparisonLayout({m_inputs[0].image.width(), m_inputs[0].image.height()},
                                         {m_inputs[1].image.width(), m_inputs[1].image.height()},
                                         ComparisonAlignment(m_alignment->currentIndex()));
  if (geometry.error) {
    m_status->setText(describe(*geometry.error));
    return;
  }
  m_view->setImages(m_inputs[0].image, m_inputs[1].image, *geometry.layout);
  if (!owner || m_closing) return;
  m_geometry->setText(QStringLiteral("Comparison canvas: %1 × %2 pixels. No image resizing.")
                          .arg(geometry.layout->canvas.width)
                          .arg(geometry.layout->canvas.height));
}
void ImageComparisonDialog::reject() { done(QDialog::Rejected); }
void ImageComparisonDialog::done(int result) {
  if (m_closing) return;
  m_closing = true;
  if (m_work) m_work->cancelled.store(true);
  m_work.reset();
  m_poll.stop();
  const QPointer<ImageComparisonDialog> owner(this);
  if (m_picker) m_picker->reject();
  if (owner) QDialog::done(result);
}
} // namespace xerahs::app
