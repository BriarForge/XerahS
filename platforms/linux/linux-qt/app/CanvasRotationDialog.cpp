#include "CanvasRotationDialog.h"

#include <QCheckBox>
#include <QColorSpace>
#include <QPointer>
#include <QThreadPool>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace xerahs::app {
using namespace xerahs::editor;

class CanvasRotationPreview final : public QWidget {
public:
  explicit CanvasRotationPreview(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("rotationPreview"));
    setAccessibleName(QStringLiteral("Rotation preview"));
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void setImage(QImage image) { m_image = std::move(image); update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().dark());
    if (m_image.isNull()) return;
    const QSize fitted = m_image.size().scaled(size() - QSize(16, 16), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    QPixmap tile(16, 16); tile.fill(Qt::white);
    { QPainter checker(&tile); checker.fillRect(0, 0, 8, 8, Qt::lightGray); checker.fillRect(8, 8, 8, 8, Qt::lightGray); }
    painter.fillRect(target, QBrush(tile));
    painter.drawImage(target, m_image);
  }
private:
  QImage m_image;
};

CanvasRotationDialog::CanvasRotationDialog(AnnotationDocument document, QImage source, QWidget *parent, EditorCanvasHooks hooks)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)), m_hooks(std::move(hooks)) {
  setObjectName(QStringLiteral("customRotationDialog"));
  setWindowTitle(QStringLiteral("Rotate Image")); setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this);
  auto *form = new QFormLayout;
  m_angle = new QDoubleSpinBox(this);
  m_angle->setObjectName(QStringLiteral("rotationAngle"));
  m_angle->setRange(-360, 360); m_angle->setDecimals(2); m_angle->setSuffix(QStringLiteral("°"));
  form->addRow(QStringLiteral("&Angle (clockwise)"), m_angle);
  m_interpolation = new QComboBox(this);
  m_interpolation->setObjectName(QStringLiteral("rotationInterpolation"));
  m_interpolation->addItems({QStringLiteral("Nearest neighbour"), QStringLiteral("Bilinear")});
  form->addRow(QStringLiteral("&Interpolation"), m_interpolation);
  m_expand = new QCheckBox(QStringLiteral("Expand canvas to contain the rotated image"), this);
  m_expand->setObjectName(QStringLiteral("rotationExpand")); m_expand->setChecked(true);
  form->addRow(m_expand);
  auto *color = new QPushButton(QStringLiteral("Transparent"), this);
  form->addRow(QStringLiteral("&Uncovered pixels"), color);
  connect(color, &QPushButton::clicked, this, [this, color] {
    const QPointer<CanvasRotationDialog> owner(this); QPointer<QColorDialog> picker(new QColorDialog(QColor::fromRgba(m_fill), this));
    picker->setWindowTitle("Uncovered pixels"); picker->setOption(QColorDialog::ShowAlphaChannel);
    const auto answer = picker->exec(); const auto chosen = picker && answer == QDialog::Accepted ? picker->selectedColor() : QColor{};
    if (picker) picker->hide();
    if (picker) picker->setParent(nullptr);
    delete picker.data();
    if (owner && chosen.isValid()) { m_fill = chosen.rgba(); color->setText(chosen.name(QColor::HexArgb)); if (owner) requestPreview(); }
  });
  layout->addLayout(form);
  auto *rules = new QLabel(QStringLiteral("Rotate about the image centre. Without expansion, keep the current dimensions "
      "and crop at the edges. Background colour fills samples outside the source; source transparency is preserved."), this);
  rules->setWordWrap(true); layout->addWidget(rules);
  m_preview = new CanvasRotationPreview(this); layout->addWidget(m_preview, 1);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("rotationStatus")); m_status->setWordWrap(true); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setRange(0, 100); m_progress->setAccessibleName(QStringLiteral("Rotation preview progress")); layout->addWidget(m_progress);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this); layout->addWidget(m_buttons);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &CanvasRotationDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &CanvasRotationDialog::reject);
  connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this] { requestPreview(); });
  connect(m_interpolation, &QComboBox::currentIndexChanged, this, [this] { requestPreview(); });
  connect(m_expand, &QCheckBox::toggled, this, [this] { requestPreview(); });
  m_debounce.setSingleShot(true);
  connect(&m_debounce, &QTimer::timeout, this, &CanvasRotationDialog::startPreview);
  connect(&m_poll, &QTimer::timeout, this, &CanvasRotationDialog::pollPreview);
  m_poll.start(20);
  requestPreview(); m_debounce.start(0);
  resize(560, 640);
}

CanvasRotationDialog::~CanvasRotationDialog() { if (m_work) m_work->cancelled.store(true); }

void CanvasRotationDialog::requestPreview() {
  if (m_closing) return;
  const QPointer<CanvasRotationDialog> owner(this); ++m_revision;
  if (m_work) m_work->cancelled.store(true);
  m_prepared.reset(); m_pending = true;
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false); if (!owner) return;
  m_status->setText("Preparing preview…"); if (!owner) return;
  m_progress->setValue(0); if (!owner) return; m_debounce.start(150);
}
void CanvasRotationDialog::startPreview() {
  if (m_closing || m_work || !m_pending) return;
  m_pending = false;
  CanvasOperation operation; operation.action = CanvasAction::RotateCustom;
  operation.rotationDegrees = m_angle->value(); operation.expandCanvas = m_expand->isChecked();
  operation.interpolation = Interpolation(m_interpolation->currentIndex()); operation.fill = m_fill;
  try {
    auto work = std::make_shared<Work>(); work->revision = m_revision;
    const auto document = m_document; const auto source = m_source; const auto hooks = m_hooks;
    QThreadPool::globalInstance()->start([work, document, source, hooks, operation] {
      try {
        if (hooks.beforePrepare) hooks.beforePrepare();
        const CanvasControl control{[work] { return work->cancelled.load(); }, [work](int p) { work->progress.store(p); }};
        work->preview = prepareCanvasPreview(document, source, operation, control); prepareCanvasDisplay(work->preview.edit, control);
        if (!work->preview.edit.result.error && !work->preview.composite.colorSpace().isValid()) work->preview.composite.setColorSpace(QColorSpace::SRgb);
        if (!work->preview.edit.result.error && work->preview.composite.colorSpace() != QColorSpace(QColorSpace::SRgb))
          work->preview.composite = work->preview.composite.convertedToColorSpace(QColorSpace::SRgb);
        if (!work->preview.edit.result.error && work->preview.composite.isNull()) work->preview.edit.result.error = diagnostic::documentTooLarge;
      } catch (...) { work->preview.edit.result.error = "canvas-preparation-failed"; }
      work->ready.store(true, std::memory_order_release);
    });
    m_work = std::move(work);
  } catch (...) { m_status->setText("A preview worker could not be started. Change the angle to retry. The document was kept."); }
}
void CanvasRotationDialog::pollPreview() {
  if (!m_work || m_closing) return;
  const QPointer<CanvasRotationDialog> owner(this); const auto work = m_work;
  if (work->revision == m_revision) m_progress->setValue(work->progress.load());
  if (!owner || !work->ready.load(std::memory_order_acquire)) return;
  m_work.reset();
  if (work->revision == m_revision && !work->cancelled.load()) {
    auto &preview = work->preview;
    if (preview.edit.result.error) {
      QString message = *preview.edit.result.error;
      if (message == diagnostic::documentTooLarge) message = "The expanded image exceeds the editor's dimensions or memory limits.";
      else if (message == "canvas-annotation-transform-unsupported") message = "This document contains annotation types this editor cannot rotate yet.";
      else if (message == diagnostic::documentInvalid) message = "The document contains invalid image or annotation data.";
      else if (message == "canvas-encoding-failed") message = "The rotated image could not be encoded.";
      else if (message == "canvas-preparation-failed") message = "The rotation preview could not be prepared. Change the angle to retry. The document was kept.";
      m_status->setText(message); if (!owner) return; m_preview->setImage({});
    } else {
      m_status->setText(QString("Preview: %1 × %2 pixels. %3").arg(preview.composite.width()).arg(preview.composite.height())
          .arg(preview.edit.result.changed ? "OK applies one undoable edit." : "No change to the document.")); if (!owner) return;
      m_preview->setImage(std::move(preview.composite)); m_prepared = std::move(preview.edit);
      m_progress->setValue(100); if (!owner) return; m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true); if (!owner) return;
    }
  }
  if (m_pending && !m_debounce.isActive()) startPreview();
}
void CanvasRotationDialog::accept() {
  if (!m_closing && m_prepared && !m_pending && !m_work) { m_closing = true; QDialog::accept(); }
}
void CanvasRotationDialog::reject() {
  m_closing = true; m_pending = false; m_debounce.stop(); m_prepared.reset();
  if (m_work) m_work->cancelled.store(true);
  QDialog::reject();
}
std::optional<PreparedCanvasEdit> CanvasRotationDialog::takeEdit() {
  return result() == QDialog::Accepted ? std::exchange(m_prepared, std::nullopt) : std::nullopt;
}
} // namespace xerahs::app
