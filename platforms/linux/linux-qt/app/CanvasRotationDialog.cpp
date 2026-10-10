#include "CanvasRotationDialog.h"

#include <QCheckBox>
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

CanvasRotationDialog::CanvasRotationDialog(AnnotationDocument document, QImage source, QWidget *parent)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)) {
  setObjectName(QStringLiteral("customRotationDialog"));
  setWindowTitle(QStringLiteral("Rotate Image"));
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
    const QColor chosen = QColorDialog::getColor(QColor::fromRgba(m_fill), this, QStringLiteral("Uncovered pixels"), QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) { m_fill = chosen.rgba(); color->setText(chosen.name(QColor::HexArgb)); requestPreview(); }
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

CanvasRotationDialog::~CanvasRotationDialog() {
  if (m_control) m_control->cancelled.store(true);
  if (m_worker.valid()) m_worker.wait();
}

void CanvasRotationDialog::requestPreview() {
  if (m_closing) return;
  ++m_revision;
  if (m_control) m_control->cancelled.store(true);
  m_prepared.reset(); m_pending = true;
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
  m_status->setText(QStringLiteral("Preparing preview…")); m_progress->setValue(0);
  m_debounce.start(150);
}

void CanvasRotationDialog::startPreview() {
  if (m_closing || m_worker.valid() || !m_pending) return;
  m_pending = false; m_jobRevision = m_revision;
  CanvasOperation operation; operation.action = CanvasAction::RotateCustom;
  operation.rotationDegrees = m_angle->value(); operation.expandCanvas = m_expand->isChecked();
  operation.interpolation = Interpolation(m_interpolation->currentIndex()); operation.fill = m_fill;
  try {
    m_control = std::make_shared<WorkControl>();
    m_worker = std::async(std::launch::async, [this, operation, control = m_control] {
      return prepareCanvasPreview(m_document, m_source, operation,
          {[control] { return control->cancelled.load(); }, [control](int p) { control->progress.store(p); }});
    });
  } catch (const std::bad_alloc &) {
    m_status->setText(QStringLiteral("Not enough memory to prepare the preview."));
  } catch (const std::system_error &) {
    m_status->setText(QStringLiteral("A preview worker could not be started. Try again."));
  }
}

void CanvasRotationDialog::pollPreview() {
  if (!m_worker.valid()) return;
  if (!m_closing && m_jobRevision == m_revision) m_progress->setValue(m_control->progress.load());
  if (m_worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
  CanvasPreview preview = m_worker.get();
  if (m_closing) { QDialog::reject(); return; }
  if (m_jobRevision == m_revision && !m_control->cancelled.load()) {
    if (preview.edit.result.error) {
      QString message = *preview.edit.result.error;
      if (message == diagnostic::documentTooLarge) message = QStringLiteral("The expanded image exceeds the editor's dimensions or memory limits.");
      else if (message == u"canvas-annotation-transform-unsupported") message = QStringLiteral("This document contains annotation types this editor cannot rotate yet.");
      else if (message == diagnostic::documentInvalid) message = QStringLiteral("The document contains invalid image or annotation data.");
      else if (message == u"canvas-encoding-failed") message = QStringLiteral("The rotated image could not be encoded.");
      m_status->setText(message); m_preview->setImage({});
    } else {
      m_status->setText(QStringLiteral("Preview: %1 × %2 pixels. %3")
          .arg(preview.composite.width()).arg(preview.composite.height())
          .arg(preview.edit.result.changed ? QStringLiteral("OK applies one undoable edit.") : QStringLiteral("No change to the document.")));
      m_preview->setImage(std::move(preview.composite));
      m_prepared = std::move(preview.edit);
      m_progress->setValue(100); m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
  }
  if (m_pending && !m_debounce.isActive()) startPreview();
}

void CanvasRotationDialog::accept() {
  if (!m_closing && m_prepared && !m_pending && !m_worker.valid()) QDialog::accept();
}
void CanvasRotationDialog::reject() {
  m_closing = true; m_pending = false; m_debounce.stop(); m_prepared.reset();
  if (m_control) m_control->cancelled.store(true);
  if (!m_worker.valid()) { QDialog::reject(); return; }
  m_status->setText(QStringLiteral("Cancelling preview…")); m_buttons->setEnabled(false);
}
std::optional<PreparedCanvasEdit> CanvasRotationDialog::takeEdit() {
  if (result() != QDialog::Accepted) return std::nullopt;
  return std::exchange(m_prepared, std::nullopt);
}
}  // namespace xerahs::app
