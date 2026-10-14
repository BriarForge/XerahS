#include "CaptureAnnotationDialog.h"
#include "EditorCanvas.h"
#include "NativeAnnotationTools.h"
#include <QAction>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
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
  m_canvas = new EditorCanvas(m_session, m_image, this, source.displayImage); layout->addWidget(m_canvas);
  const auto tools = annotationToolActions(*m_canvas, this, false);
  bar->addAction(tools.value("select-tool")); bar->addAction(tools.value("rectangle-tool"));
  auto *create = bar->addAction("Create Rectangle…"); create->setObjectName("createRectangle"); create->setAutoRepeat(false);
  create->setToolTip("Enter numeric bounds and create one rectangle with captured defaults. The active tool stays unchanged.");
  bar->widgetForAction(create)->setFocusPolicy(Qt::StrongFocus);
  auto *properties = bar->addAction("Rectangle Properties…"); properties->setObjectName("rectangleProperties"); properties->setAutoRepeat(false);
  properties->setToolTip("Edit one selected, visible and unlocked rectangle. Creation defaults stay unchanged.");
  const auto refreshProperties = [this, properties, create] {
    properties->setEnabled(m_canvas->canEditRectangle() && !m_canvas->interactionActive()); create->setEnabled(!m_canvas->interactionActive());
  };
  connect(m_canvas, &EditorCanvas::changed, properties, refreshProperties);
  connect(m_canvas, &EditorCanvas::toolChanged, properties, refreshProperties);
  connect(m_canvas, &EditorCanvas::viewportChanged, properties, refreshProperties); refreshProperties();
  connect(properties, &QAction::triggered, m_canvas, [this] { m_canvas->editRectangleProperties(this); });
  connect(create, &QAction::triggered, m_canvas, [this] { m_canvas->createRectangle(this); });
  layout->insertWidget(2, annotationToolInstructions(*m_canvas, this));
  m_saveSidecar = new QCheckBox("Also save editable annotations beside each saved PNG (.xann)", this);
  m_saveSidecar->setObjectName("captureSaveAnnotationSidecar"); layout->addWidget(m_saveSidecar);
  auto *privacy = new QLabel("The companion contains the original capture and editable annotations. A companion failure keeps the PNG and the editable document for explicit retry. "
      "You can also use Edit retained capture to save an editable copy later.", this);
  privacy->setWordWrap(true); privacy->setTextFormat(Qt::PlainText); layout->addWidget(privacy);
  auto *undo = bar->addAction("Undo"); undo->setShortcut(QKeySequence::Undo); addAction(undo);
  auto *redo = bar->addAction("Redo"); redo->setShortcut(QKeySequence::Redo); addAction(redo);
  connect(undo, &QAction::triggered, this, [this, refreshProperties] { if (!m_canvas->interactionActive()) { m_session.undo(); m_canvas->viewport()->update(); refreshProperties(); } });
  connect(redo, &QAction::triggered, this, [this, refreshProperties] { if (!m_canvas->interactionActive()) { m_session.redo(); m_canvas->viewport()->update(); refreshProperties(); } });
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
