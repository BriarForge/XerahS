#include "CanvasCropDialog.h"
#include <QDialogButtonBox>
#include <QColorSpace>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThreadPool>
#include <QVBoxLayout>
#include <QPointer>
#include <utility>
namespace xerahs::app {
using namespace xerahs::editor;
class CropPreview final : public QWidget {
public:
  explicit CropPreview(QWidget *parent) : QWidget(parent) { setObjectName("cropPreview"); setAccessibleName("Retained crop preview"); setMinimumSize(320, 180); }
  void setImage(QImage image) { m_image = std::move(image); update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this); p.fillRect(rect(), palette().dark()); if (m_image.isNull()) return;
    const auto size = m_image.size().scaled(this->size() - QSize(16, 16), Qt::KeepAspectRatio); const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
    QPixmap tile(16, 16); tile.fill(Qt::white); { QPainter checker(&tile); checker.fillRect(0, 0, 8, 8, Qt::lightGray); checker.fillRect(8, 8, 8, 8, Qt::lightGray); }
    p.fillRect(target, QBrush(tile)); p.drawImage(target, m_image);
  }
private:
  QImage m_image;
};
CanvasCropDialog::CanvasCropDialog(AnnotationDocument document, QImage source, CropRegion region, QWidget *parent, CanvasCropHooks hooks)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)), m_hooks(std::move(hooks)) {
  setObjectName("canvasCropDialog"); setWindowTitle("Crop Image"); setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout; layout->addLayout(form);
  const auto number = [&](const char *name, const QString &label, int minimum, int maximum, int value) {
    auto *field = new QSpinBox(this); field->setObjectName(name); auto accessible = label; accessible.remove('&'); field->setAccessibleName(accessible); field->setRange(minimum, maximum); field->setValue(value); form->addRow(label, field); return field;
  };
  m_left = number("cropLeft", "&Left (pixels)", 0, m_source.width() - 1, int(region.left));
  m_top = number("cropTop", "&Top (pixels)", 0, m_source.height() - 1, int(region.top));
  m_width = number("cropWidth", "&Width (pixels)", 1, m_source.width() - m_left->value(), int(region.width));
  m_height = number("cropHeight", "&Height (pixels)", 1, m_source.height() - m_top->value(), int(region.height));
  auto *rules = new QLabel("Keep only the declared rectangle of source pixels. Known annotations are translated; objects wholly outside are removed, and intersecting objects retain their geometry and are clipped at export. "
      "Unsupported annotation geometry blocks cropping. Apply Crop adds one undo step; Cancel keeps the canvas unchanged. Undo history and embedded image assets can retain earlier pixels.", this); rules->setWordWrap(true); rules->setTextFormat(Qt::PlainText); layout->addWidget(rules);
  m_preview = new CropPreview(this); layout->addWidget(m_preview, 1);
  m_status = new QLabel(this); m_status->setObjectName("cropStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setRange(0, 100); m_progress->setAccessibleName("Crop preview progress"); layout->addWidget(m_progress);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this); m_buttons->button(QDialogButtonBox::Apply)->setText("&Apply Crop"); layout->addWidget(m_buttons);
  connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &CanvasCropDialog::accept); connect(m_buttons, &QDialogButtonBox::rejected, this, &CanvasCropDialog::reject);
  for (auto *field : {m_left, m_top, m_width, m_height}) connect(field, &QSpinBox::valueChanged, this, &CanvasCropDialog::request);
  m_debounce.setSingleShot(true); connect(&m_debounce, &QTimer::timeout, this, &CanvasCropDialog::start); connect(&m_poll, &QTimer::timeout, this, &CanvasCropDialog::poll); m_poll.start(20);
  request(); m_debounce.start(0); resize(540, 620);
}
CanvasCropDialog::~CanvasCropDialog() { if (m_work) m_work->cancelled.store(true); }
void CanvasCropDialog::request() {
  if (m_closing) return;
  const QPointer<CanvasCropDialog> owner(this); ++m_revision; m_pending = true; m_prepared.reset(); if (m_work) m_work->cancelled.store(true);
  m_width->setMaximum(m_source.width() - m_left->value()); if (!owner) return; m_height->setMaximum(m_source.height() - m_top->value()); if (!owner) return;
  m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false); if (!owner) return;
  m_status->setText("Preparing retained crop…"); m_progress->setValue(0); m_debounce.start(100);
}
void CanvasCropDialog::start() {
  if (m_closing || m_work || !m_pending) return;
  const CropRegion region{m_left->value(), m_top->value(), m_width->value(), m_height->value()};
  m_pending = false;
  try {
    auto work = std::make_shared<Work>(); work->region = region; work->revision = m_revision;
    const auto document = m_document; const auto source = m_source; const auto hooks = m_hooks;
    QThreadPool::globalInstance()->start([document, source, hooks, work] {
      try {
        if (hooks.beforePrepare) hooks.beforePrepare();
        CanvasOperation op; op.action = CanvasAction::Crop; const auto r = work->region; op.start = {double(r.left), double(r.top)}; op.end = {double(r.left + r.width), double(r.top + r.height)};
        work->preview = prepareCanvasPreview(document, source, op, {[work] { return work->cancelled.load(); }, [work](int value) { work->progress.store(value); }});
        if (!work->preview.edit.result.error) {
          const auto display = [](const QImage &image) { return !image.colorSpace().isValid() || image.colorSpace() == QColorSpace(QColorSpace::SRgb) ? image : image.convertedToColorSpace(QColorSpace::SRgb); };
          work->displaySource = display(work->preview.edit.source); work->preview.composite = display(work->preview.composite);
          if (work->displaySource.isNull() || work->preview.composite.isNull()) work->preview.edit.result.error = diagnostic::documentTooLarge;
        }
      } catch (...) { work->preview.edit.result.error = "canvas-preparation-failed"; }
      work->done.store(true, std::memory_order_release);
    });
    m_work = std::move(work);
  } catch (...) { m_status->setText("The crop preview could not be started. Change the bounds to retry. The canvas was kept."); }
}
void CanvasCropDialog::poll() {
  if (!m_work || m_closing) return;
  const QPointer<CanvasCropDialog> owner(this); const auto work = m_work;
  m_progress->setValue(work->progress.load()); if (!owner || !work->done.load(std::memory_order_acquire)) return;
  m_work.reset();
  if (work->revision == m_revision && !work->cancelled.load()) {
    if (work->preview.edit.result.error) {
      const auto error = *work->preview.edit.result.error;
      m_status->setText(error == "canvas-annotation-transform-unsupported" ? "Crop cannot transform unsupported annotation geometry. The canvas was kept." : "Crop preview failed (" + error + "). The canvas was kept."); if (!owner) return;
    } else {
      m_preview->setImage(std::move(work->preview.composite));
      m_prepared = PreparedCropEdit{std::move(work->preview.edit), work->region, std::move(work->displaySource)};
      m_status->setText(QString("Retain %1 × %2 pixels at %3, %4. Apply Crop to commit; Cancel keeps the canvas.").arg(work->region.width).arg(work->region.height).arg(work->region.left).arg(work->region.top)); if (!owner) return;
      m_buttons->button(QDialogButtonBox::Apply)->setEnabled(true); if (!owner) return;
    }
  }
  if (m_pending) start();
}
std::optional<PreparedCropEdit> CanvasCropDialog::takeEdit() { return std::exchange(m_prepared, {}); }
void CanvasCropDialog::accept() { if (!m_prepared || m_pending || m_work || m_closing) return; m_closing = true; QDialog::accept(); }
void CanvasCropDialog::reject() { m_closing = true; m_pending = false; m_prepared.reset(); if (m_work) m_work->cancelled.store(true); QDialog::reject(); }
} // namespace xerahs::app
