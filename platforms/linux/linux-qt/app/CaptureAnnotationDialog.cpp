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
#include <QSplitter>
#include <QToolBar>
#include <QVBoxLayout>
namespace xerahs::app {
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
  const auto tools = annotationToolActions(*m_canvas, this, false);
  bar->addAction(tools.value("select-tool")); bar->addAction(tools.value("rectangle-tool"));
  auto *create = bar->addAction("Create Rectangle…"); create->setObjectName("createRectangle"); create->setAutoRepeat(false);
  create->setToolTip("Enter numeric bounds and create one rectangle with captured defaults. The active tool stays unchanged.");
  bar->widgetForAction(create)->setFocusPolicy(Qt::StrongFocus);
  auto *properties = bar->addAction("Rectangle Properties…"); properties->setObjectName("rectangleProperties"); properties->setAutoRepeat(false);
  auto *rotate = bar->addAction("Rotate Selection…"); rotate->setObjectName("rotateSelection"); rotate->setAutoRepeat(false);
  rotate->setToolTip("Add clockwise degrees to selected rectangles about their own bounds centres. Creation defaults stay unchanged.");
  bar->widgetForAction(rotate)->setFocusPolicy(Qt::StrongFocus);
  properties->setToolTip("Edit one selected, visible and unlocked rectangle. Creation defaults stay unchanged.");
  const auto refreshProperties = [this, properties, create, rotate] {
    properties->setEnabled(m_canvas->canEditRectangle() && !m_canvas->interactionActive()); create->setEnabled(!m_canvas->interactionActive());
    rotate->setEnabled(m_canvas->canRotateSelection() && !m_canvas->interactionActive());
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
    const bool ready = m_canvas->canEditStyle() && !m_canvas->interactionActive();
    for (auto *action : {stroke, fill, width, thin}) action->setEnabled(ready);
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
      action->setEnabled(m_canvas->isEnabled() && !m_canvas->interactionActive() && m_canvas->canOrderSelection());
      if (!owner) return;
    }
  };
  m_saveSidecar = new QCheckBox("Also save editable annotations beside each saved PNG (.xann)", this);
  m_saveSidecar->setObjectName("captureSaveAnnotationSidecar"); layout->addWidget(m_saveSidecar);
  auto *privacy = new QLabel("The companion contains the original capture and editable annotations. A companion failure keeps the PNG and the editable document for explicit retry. "
      "You can also use Edit retained capture to save an editable copy later.", this);
  privacy->setWordWrap(true); privacy->setTextFormat(Qt::PlainText); layout->addWidget(privacy);
  auto *undo = bar->addAction("Undo"); undo->setObjectName("undo"); undo->setShortcut(QKeySequence::Undo); addAction(undo);
  auto *redo = bar->addAction("Redo"); redo->setObjectName("redo");
  redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}); addAction(redo);
  const auto refreshHistory = [this, undo, redo] {
    undo->setEnabled(!m_canvas->interactionActive() && m_session.undoCount() > 0);
    redo->setEnabled(!m_canvas->interactionActive() && m_session.redoCount() > 0);
  };
  connect(m_canvas, &EditorCanvas::changed, undo, refreshHistory);
  connect(m_canvas, &EditorCanvas::toolChanged, undo, refreshHistory);
  connect(m_canvas, &EditorCanvas::viewportChanged, undo, refreshHistory); refreshHistory();
  connect(undo, &QAction::triggered, this, [this, refreshProperties, refreshHistory, refreshColors, refreshOrder] { if (!m_canvas->interactionActive()) { m_session.undo(); m_canvas->viewport()->update(); m_annotationObjects->refresh(); refreshProperties(); refreshHistory(); refreshColors(); refreshOrder(); } });
  connect(redo, &QAction::triggered, this, [this, refreshProperties, refreshHistory, refreshColors, refreshOrder] { if (!m_canvas->interactionActive()) { m_session.redo(); m_canvas->viewport()->update(); m_annotationObjects->refresh(); refreshProperties(); refreshHistory(); refreshColors(); refreshOrder(); } });
  connect(bar->addAction("Zoom in"), &QAction::triggered, m_canvas, &EditorCanvas::zoomIn);
  connect(bar->addAction("Zoom out"), &QAction::triggered, m_canvas, &EditorCanvas::zoomOut);
  connect(bar->addAction("100%"), &QAction::triggered, m_canvas, &EditorCanvas::resetZoom);
  connect(bar->addAction("Fit"), &QAction::triggered, m_canvas, &EditorCanvas::zoomToFit);
  m_status = new QLabel(this); m_status->setObjectName("captureAnnotationStatus"); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  connect(m_canvas, &EditorCanvas::editRejected, this, [this](const QString &diagnostic) { m_status->setText(annotationEditMessage(diagnostic)); });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto *apply = buttons->addButton("Apply to capture", QDialogButtonBox::AcceptRole); apply->setObjectName("applyCaptureAnnotation"); apply->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  connect(buttons, &QDialogButtonBox::accepted, this, &CaptureAnnotationDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(780, 600); m_canvas->setFocus();
}
CaptureAnnotationDialog::~CaptureAnnotationDialog() { delete m_canvas; m_canvas = nullptr; }
void CaptureAnnotationDialog::accept() {
  if (m_canvas->interactionActive()) { m_status->setText("Finish or cancel the current gesture before applying annotations."); return; }
  m_accepted = m_session.document(); m_sidecarIntent = m_saveSidecar->isChecked(); QDialog::accept();
}
} // namespace xerahs::app
