#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "NativeAnnotationTools.h"
#include "AnnotationObjectNavigator.h"
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QPointer>
#include <QProgressDialog>
#include <QScopeGuard>
#include <QSplitter>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
namespace xerahs::app {
struct CaptureAnnotationDialog::HistoryWork {
  std::atomic<bool> cancelled{false}, done{false};
  std::atomic<int> progress{0};
  LoadedSource source;
};
CaptureAnnotationDialog::CaptureAnnotationDialog(const LoadedSource &source, QWidget *parent, const xerahs::editor::RectangleStyle &style)
    : QDialog(parent), m_image(source.image), m_session(*source.document, xerahs::editor::EditorSession::InitialPersistence::Unsaved) {
  setObjectName("captureAnnotationDialog"); setWindowTitle("Annotate capture"); setAccessibleName(windowTitle());
  setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_DeleteOnClose);
  m_session.setCreationStyle(style);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel("Select a tool to draw rectangles or edit existing objects. Apply sends the edited image to the remaining capture actions. "
      "Cancel skips annotation and keeps the original capture. PNG capture saves contain rendered pixels.", this);
  intro->setTextFormat(Qt::PlainText); intro->setWordWrap(true); layout->addWidget(intro);
  auto *bar = new QToolBar("Annotation controls", this); layout->addWidget(bar);
  auto *splitter = new QSplitter(this); layout->addWidget(splitter, 1);
  m_canvas = new EditorCanvas(m_session, m_image, splitter, source.displayImage); splitter->addWidget(m_canvas);
  m_annotationObjects = new AnnotationObjectNavigator(*m_canvas, splitter); splitter->addWidget(m_annotationObjects);
  splitter->setStretchFactor(0, 3); splitter->setStretchFactor(1, 1); splitter->setSizes({560, 200});
  const auto tools = annotationToolActions(*m_canvas, this);
  bar->addAction(tools.value("select-tool")); bar->addAction(tools.value("rectangle-tool"));
  auto *remove = annotationDeleteAction(*m_canvas, this); bar->addAction(remove);
  bar->widgetForAction(remove)->setFocusPolicy(Qt::StrongFocus);
  const auto refreshDelete = [this, remove] { remove->setEnabled(editingReady() && m_canvas->canDeleteSelection()); };
  connect(m_canvas, &EditorCanvas::changed, remove, refreshDelete);
  auto *create = bar->addAction("Create Rectangle…"); create->setObjectName("createRectangle"); create->setAutoRepeat(false);
  create->setToolTip("Enter numeric bounds and create one rectangle with captured defaults. The active tool stays unchanged.");
  bar->widgetForAction(create)->setFocusPolicy(Qt::StrongFocus);
  auto *properties = bar->addAction("Rectangle Properties…"); properties->setObjectName("rectangleProperties"); properties->setAutoRepeat(false);
  auto *rotate = bar->addAction("Rotate Selection…"); rotate->setObjectName("rotateSelection"); rotate->setAutoRepeat(false);
  rotate->setToolTip("Add clockwise degrees to selected rectangles about their own bounds centres. Creation defaults stay unchanged.");
  bar->widgetForAction(rotate)->setFocusPolicy(Qt::StrongFocus);
  properties->setToolTip("Edit one selected, visible and unlocked rectangle. Creation defaults stay unchanged.");
  const auto refreshProperties = [this, properties, create, rotate] {
    const QPointer<CaptureAnnotationDialog> owner(this);
    properties->setEnabled(m_canvas->canEditRectangle() && editingReady()); if (!owner) return;
    create->setEnabled(editingReady()); if (!owner) return;
    rotate->setEnabled(m_canvas->canRotateSelection() && editingReady());
  };
  connect(m_canvas, &EditorCanvas::changed, properties, refreshProperties);
  connect(m_canvas, &EditorCanvas::toolChanged, properties, refreshProperties);
  connect(m_canvas, &EditorCanvas::viewportChanged, properties, refreshProperties); refreshProperties();
  connect(properties, &QAction::triggered, m_canvas, [this] { m_canvas->editRectangleProperties(this); });
  connect(create, &QAction::triggered, m_canvas, [this] { m_canvas->createRectangle(this); });
  connect(rotate, &QAction::triggered, m_canvas, [this] { m_canvas->rotateSelection(this); });
  layout->insertWidget(2, annotationToolInstructions(*m_canvas, this));
  auto *colors = new QToolBar("Capture style controls", this); colors->setObjectName("captureColorToolbar"); layout->insertWidget(3, colors);
  auto *stroke = colors->addAction("Stroke colour…"); stroke->setObjectName("strokeColour"); stroke->setAutoRepeat(false);
  auto *fill = colors->addAction("Fill colour…"); fill->setObjectName("fillColour"); fill->setAutoRepeat(false);
  auto *width = colors->addAction("Stroke +"); width->setObjectName("increaseStroke"); width->setAutoRepeat(false);
  auto *thin = colors->addAction("Stroke −"); thin->setObjectName("decreaseStroke"); thin->setAutoRepeat(false);
  for (auto *action : {stroke, fill, width, thin}) {
    colors->widgetForAction(action)->setFocusPolicy(Qt::StrongFocus);
    action->setToolTip("Change selected rectangles and the default for new rectangles in this capture. Editor preferences stay unchanged.");
  }
  auto *strokeWidth = new QLabel(colors); strokeWidth->setObjectName("captureStrokeWidth"); strokeWidth->setAccessibleName("New rectangle stroke width"); colors->addWidget(strokeWidth);
  const auto refreshColors = [this, stroke, fill, width, thin, strokeWidth] {
    const QPointer<CaptureAnnotationDialog> owner(this);
    const bool ready = m_canvas->canEditStyle() && editingReady();
    for (auto *action : {stroke, fill, width, thin}) { action->setEnabled(ready); if (!owner) return; }
    strokeWidth->setText(QString("New rectangles: %1 px").arg(QString::number(m_canvas->creationStrokeWidth(), 'g', 17)));
    strokeWidth->setAccessibleDescription(strokeWidth->text());
  };
  connect(m_canvas, &EditorCanvas::changed, stroke, refreshColors);
  connect(m_canvas, &EditorCanvas::toolChanged, stroke, refreshColors);
  connect(m_canvas, &EditorCanvas::viewportChanged, stroke, refreshColors); refreshColors();
  connect(stroke, &QAction::triggered, m_canvas, [this] { m_canvas->editColor(xerahs::editor::EditorSession::ColorTarget::Stroke, this); });
  connect(fill, &QAction::triggered, m_canvas, [this] { m_canvas->editColor(xerahs::editor::EditorSession::ColorTarget::Fill, this); });
  connect(width, &QAction::triggered, m_canvas, [this] { m_canvas->changeStrokeWidth(true); });
  connect(thin, &QAction::triggered, m_canvas, [this] { m_canvas->changeStrokeWidth(false); });
  auto *orderBar = new QToolBar("Annotation order", this); orderBar->setObjectName("captureOrderToolbar"); layout->insertWidget(4, orderBar);
  const auto orderActions = annotationOrderActions(*m_canvas, this);
  for (const auto *id : {"bring-forward", "send-backward", "bring-to-front", "send-to-back"}) {
    auto *action = orderActions.value(id); orderBar->addAction(action); orderBar->widgetForAction(action)->setFocusPolicy(Qt::StrongFocus);
  }
  const auto refreshOrder = [this, orderActions] {
    const QPointer<CaptureAnnotationDialog> owner(this);
    for (auto *action : orderActions) {
      action->setEnabled(editingReady() && m_canvas->canOrderSelection());
      if (!owner) return;
    }
  };
  connect(m_canvas, &EditorCanvas::changed, orderBar, refreshOrder);
  auto *canvasBar = new QToolBar("Capture canvas controls", this); canvasBar->setObjectName("captureCanvasToolbar"); layout->insertWidget(5, canvasBar);
  canvasBar->addAction(tools.value("crop-tool"));
  auto *crop = canvasBar->addAction("Crop Image…"); crop->setObjectName("cropImage"); crop->setAutoRepeat(false); crop->setToolTip("Choose numeric retained bounds, preview and Apply Crop in one undo step.");
  for (auto *action : canvasBar->actions()) canvasBar->widgetForAction(action)->setFocusPolicy(Qt::StrongFocus);
  connect(m_canvas, &EditorCanvas::changed, crop, [this, crop, tools] {
    const QPointer<CaptureAnnotationDialog> owner(this); crop->setEnabled(editingReady()); if (!owner) return;
    for (auto *action : tools) { action->setEnabled(!m_operationPending && m_canvas->isEnabled()); if (!owner) return; }
  });
  connect(crop, &QAction::triggered, this, [this] { cropImage({0, 0, m_image.width(), m_image.height()}); });
  connect(m_canvas, &EditorCanvas::cropRequested, this, [this](QPointF start, QPointF end) {
    const auto region = xerahs::editor::normalizedCropRegion(m_image.width(), m_image.height(), {start.x(), start.y()}, {end.x(), end.y()});
    if (region) cropImage(*region); else m_status->setText("Crop bounds are empty or invalid. The canvas was kept.");
  });
  m_saveSidecar = new QCheckBox("Also save editable annotations beside each saved PNG (.xann)", this);
  m_saveSidecar->setObjectName("captureSaveAnnotationSidecar"); layout->addWidget(m_saveSidecar);
  auto *privacy = new QLabel("The companion contains the current editable source, annotations and embedded assets. Undo retains earlier canvas states while this dialog is open. "
      "A companion failure keeps the PNG and editable document for explicit retry. You can also use Edit retained capture to save an editable copy later.", this);
  privacy->setWordWrap(true); privacy->setTextFormat(Qt::PlainText); layout->addWidget(privacy);
  auto *undo = bar->addAction("Undo"); undo->setObjectName("undo"); undo->setShortcut(QKeySequence::Undo); addAction(undo);
  auto *redo = bar->addAction("Redo"); redo->setObjectName("redo");
  redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}); addAction(redo);
  const auto refreshHistory = [this, undo, redo] {
    const QPointer<CaptureAnnotationDialog> owner(this);
    undo->setEnabled(editingReady() && m_session.undoCount() > 0); if (!owner) return;
    redo->setEnabled(editingReady() && m_session.redoCount() > 0);
  };
  connect(m_canvas, &EditorCanvas::changed, undo, refreshHistory);
  connect(m_canvas, &EditorCanvas::toolChanged, undo, refreshHistory);
  connect(m_canvas, &EditorCanvas::viewportChanged, undo, refreshHistory); refreshHistory();
  const auto history = [this](bool redo) { restoreHistory(redo); };
  connect(undo, &QAction::triggered, this, [history] { history(false); });
  connect(redo, &QAction::triggered, this, [history] { history(true); });
  connect(bar->addAction("Zoom in"), &QAction::triggered, m_canvas, &EditorCanvas::zoomIn);
  connect(bar->addAction("Zoom out"), &QAction::triggered, m_canvas, &EditorCanvas::zoomOut);
  connect(bar->addAction("100%"), &QAction::triggered, m_canvas, &EditorCanvas::resetZoom);
  connect(bar->addAction("Fit"), &QAction::triggered, m_canvas, &EditorCanvas::zoomToFit);
  m_status = new QLabel(this); m_status->setObjectName("captureAnnotationStatus"); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  connect(m_canvas, &EditorCanvas::editRejected, this, [this](const QString &diagnostic) { m_status->setText(annotationEditMessage(diagnostic)); });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto *apply = buttons->addButton("Apply to capture", QDialogButtonBox::AcceptRole); apply->setObjectName("applyCaptureAnnotation"); apply->setAutoDefault(false);
  connect(m_canvas, &EditorCanvas::changed, apply, [this, apply] { apply->setEnabled(editingReady()); });
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &CaptureAnnotationDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(780, 600); m_canvas->setFocus();
}
CaptureAnnotationDialog::~CaptureAnnotationDialog() {
  m_closing = true;
  if (m_historyWork) m_historyWork->cancelled.store(true);
  if (m_cropDialog) disconnect(m_cropDialog, nullptr, this, nullptr);
  delete m_canvas; m_canvas = nullptr;
}
bool CaptureAnnotationDialog::editingReady() const { return !m_closing && !m_operationPending && m_canvas->isEnabled() && !m_canvas->interactionActive(); }
void CaptureAnnotationDialog::finishOperation() {
  if (m_closing || !m_canvas) return;
  const QPointer<CaptureAnnotationDialog> owner(this); m_operationPending = false; m_canvas->setEnabled(true); if (!owner) return;
  emit m_canvas->changed();
}
void CaptureAnnotationDialog::cropImage(xerahs::editor::CropRegion region) {
  if (!editingReady()) return;
  xerahs::editor::AnnotationDocument document; QImage source;
  try { document = m_session.document(); source = m_image; }
  catch (...) { m_status->setText("The crop snapshot could not be prepared. The canvas was kept."); return; }
  const auto state = m_session.stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session.selection();
  const QPointer<CaptureAnnotationDialog> owner(this); m_operationPending = true; m_canvas->setEnabled(false); if (!owner) return;
  emit m_canvas->changed(); if (!owner || m_closing) return;
  try {
    auto *crop = new CanvasCropDialog(std::move(document), std::move(source), region, this, m_cropHooks); crop->setAttribute(Qt::WA_DeleteOnClose); m_cropDialog = crop;
    connect(crop, &QDialog::finished, this, [this, owner, crop, state, generation, selection](int outcome) {
      m_cropDialog.clear(); const auto restore = qScopeGuard([owner] { if (owner) owner->finishOperation(); });
      if (outcome != QDialog::Accepted) return;
      auto prepared = crop->takeEdit(); if (!prepared) return;
      if (generation != m_canvas->documentGeneration() || state != m_session.stateId() || selection != m_session.selection()) { m_status->setText("The document or selection changed. The crop was not applied."); return; }
      if (!prepared->edit.result.changed) { m_status->setText("The crop already covers this canvas. No history step was added."); return; }
      if (!prepared->edit.result.document || !m_session.commitCrop(*prepared->edit.result.document, prepared->region, state, selection)) { m_status->setText("Crop could not be admitted. The canvas was kept."); return; }
      m_image = std::move(prepared->edit.source); m_canvas->sourceChanged(prepared->displaySource); if (!owner) return;
      m_status->setText(QString("Cropped to %1 × %2 pixels. Undo restores the previous canvas.").arg(m_image.width()).arg(m_image.height()));
    });
    connect(crop, &QObject::destroyed, this, [this, crop] {
      if (m_cropDialog.data() == crop || (m_operationPending && !m_cropDialog && !m_historyWork)) { m_cropDialog.clear(); finishOperation(); }
    });
    crop->open();
  } catch (...) { if (owner) { m_status->setText("The crop dialog could not be opened. The canvas was kept."); if (owner) finishOperation(); } }
}
void CaptureAnnotationDialog::restoreHistory(bool redo) {
  if (!editingReady()) return;
  const auto *target = m_session.historyTarget(redo); if (!target) return;
  const QPointer<CaptureAnnotationDialog> owner(this); const auto state = m_session.stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session.selection();
  if (target->sourceImagePng == m_session.document().sourceImagePng) {
    m_operationPending = true; if (redo) m_session.redo(); else m_session.undo(); finishOperation(); return;
  }
  xerahs::editor::AnnotationDocument document;
  try { document = *target; } catch (...) { m_status->setText("Canvas history could not be prepared. History was kept."); return; }
  m_operationPending = true; m_canvas->setEnabled(false); if (!owner) return; emit m_canvas->changed(); if (!owner || m_closing) return;
  try {
    auto work = std::make_shared<HistoryWork>(); m_historyWork = work;
    auto *progress = new QProgressDialog("Restoring canvas history…", "Cancel", 0, 100, this); progress->setObjectName("captureHistoryProgress"); progress->setWindowModality(Qt::WindowModal); progress->setAutoClose(false); progress->setAutoReset(false); m_historyDialog = progress;
    connect(progress, &QProgressDialog::canceled, this, [work] { work->cancelled.store(true); });
    connect(progress, &QDialog::rejected, this, [work] { work->cancelled.store(true); });
    const auto hooks = m_cropHooks;
    QThreadPool::globalInstance()->start([work, document, hooks] {
      try { if (hooks.beforePrepare) hooks.beforePrepare(); work->source = prepareCapturedDocument(document, {[work] { return work->cancelled.load(); }, [work](int p) { work->progress.store(p); }}); }
      catch (...) { work->source.diagnostic = "canvas-preparation-failed"; }
      work->done.store(true, std::memory_order_release);
    });
    auto *timer = new QTimer(this); connect(timer, &QTimer::timeout, this, [this, owner, work, timer, state, generation, selection, redo] {
      if (m_historyDialog) m_historyDialog->setValue(work->progress.load());
      if (!owner || !work->done.load(std::memory_order_acquire)) return;
      timer->stop(); timer->deleteLater(); m_historyWork.reset();
      if (m_historyDialog) { const auto dialog = m_historyDialog; m_historyDialog.clear(); dialog->hide(); if (!owner) return; if (dialog) dialog->deleteLater(); }
      const auto restore = qScopeGuard([owner] { if (owner) owner->finishOperation(); });
      if (work->cancelled.load()) return;
      if (state != m_session.stateId() || generation != m_canvas->documentGeneration() || selection != m_session.selection()) { m_status->setText("The document or selection changed. History was kept."); return; }
      if (!work->source.document) { m_status->setText("Canvas history could not be decoded. History was kept."); return; }
      if (redo) m_session.redo(); else m_session.undo(); m_image = std::move(work->source.image); m_canvas->sourceChanged(work->source.displayImage);
    }); timer->start(20); progress->show();
  } catch (...) { if (m_historyWork) m_historyWork->cancelled.store(true); m_historyWork.reset(); if (m_historyDialog) { m_historyDialog->deleteLater(); m_historyDialog.clear(); } if (owner) { m_status->setText("Canvas history could not be started. History was kept."); if (owner) finishOperation(); } }
}
void CaptureAnnotationDialog::accept() {
  if (!editingReady()) { m_status->setText("Finish or cancel the current gesture before applying annotations."); return; }
  m_accepted = m_session.document(); m_sidecarIntent = m_saveSidecar->isChecked(); m_closing = true; QDialog::accept();
}
void CaptureAnnotationDialog::reject() {
  m_closing = true;
  const QPointer<CaptureAnnotationDialog> owner(this); if (m_historyWork) m_historyWork->cancelled.store(true);
  if (m_cropDialog) m_cropDialog->reject();
  if (!owner) return;
  QDialog::reject();
}
} // namespace xerahs::app
