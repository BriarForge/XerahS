#include "EditorWindow.h"

#include "EditorCanvas.h"
#include "EditorSource.h"

#include <QAction>
#include <QCloseEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>

namespace xerahs::app {

using namespace xerahs::editor;

namespace {

QColor toColor(Argb argb) { return QColor::fromRgba(argb); }

QString describe(const QString &diagnostic) {
  if (diagnostic == diagnostic::sourceUnsupported) return QStringLiteral("This file is not a supported image.");
  if (diagnostic == diagnostic::sourceCorrupt) return QStringLiteral("This image is damaged and cannot be read.");
  if (diagnostic == diagnostic::sourceEmpty) return QStringLiteral("This image is empty.");
  if (diagnostic == diagnostic::sourceTooLarge) return QStringLiteral("This image is too large to edit.");
  return diagnostic;
}

}  // namespace

EditorWindow *EditorWindow::open(const QString &path, QWidget *parent) {
  LoadedSource source = loadSource(path);
  if (!source.document) {
    QMessageBox::warning(parent, QStringLiteral("Cannot open image"),
                         QStringLiteral("%1\n\n%2").arg(QFileInfo(path).fileName(), describe(source.diagnostic)));
    return nullptr;
  }
  auto *window = new EditorWindow(path, source.image, *source.document);
  window->setAttribute(Qt::WA_DeleteOnClose);
  if (!source.warnings.isEmpty()) {
    QMessageBox::information(window, QStringLiteral("Annotations"), source.warnings.join(QLatin1Char('\n')));
  }
  return window;
}

EditorWindow::EditorWindow(const QString &path, QImage image, AnnotationDocument document)
    : m_path(path), m_image(std::move(image)), m_session(std::make_unique<EditorSession>(std::move(document))) {
  m_canvas = new EditorCanvas(*m_session, m_image);
  setCentralWidget(m_canvas);

  auto *bar = addToolBar(QStringLiteral("Edit"));
  bar->setMovable(false);
  QAction *saveAction = bar->addAction(QStringLiteral("Save"));
  saveAction->setShortcut(QKeySequence::Save);
  QAction *saveAs = bar->addAction(QStringLiteral("Save As…"));
  saveAs->setShortcut(QKeySequence::SaveAs);
  bar->addSeparator();
  m_undo = bar->addAction(QStringLiteral("Undo"));
  m_undo->setShortcut(QKeySequence::Undo);
  m_redo = bar->addAction(QStringLiteral("Redo"));
  m_redo->setShortcut(QKeySequence::Redo);
  m_delete = bar->addAction(QStringLiteral("Delete"));
  m_rotate = bar->addAction(QStringLiteral("Rotate selection…"));
  bar->addSeparator();
  QAction *stroke = bar->addAction(QStringLiteral("Stroke colour…"));
  QAction *fill = bar->addAction(QStringLiteral("Fill colour…"));
  QAction *width = bar->addAction(QStringLiteral("Stroke +"));
  QAction *thin = bar->addAction(QStringLiteral("Stroke −"));

  connect(saveAction, &QAction::triggered, this, [this] { save(false); });
  connect(saveAs, &QAction::triggered, this, [this] { save(true); });
  connect(m_undo, &QAction::triggered, this, [this] { m_session->undo(); refresh(); });
  connect(m_redo, &QAction::triggered, this, [this] { m_session->redo(); refresh(); });
  connect(m_delete, &QAction::triggered, this, [this] { m_session->deleteSelection(); refresh(); });
  connect(m_rotate, &QAction::triggered, this, [this] {
    bool accepted = false;
    const double degrees = QInputDialog::getDouble(this, QStringLiteral("Rotate selection"),
        QStringLiteral("Clockwise angle in degrees (negative turns counter-clockwise):"),
        0, -360, 360, 2, &accepted);
    if (accepted) { m_session->rotateSelection(degrees); refresh(); }
  });
  connect(stroke, &QAction::triggered, this, [this] {
    const QColor c = QColorDialog::getColor(toColor(m_session->toolStyle().strokeColor), this,
                                            QStringLiteral("Stroke colour"), QColorDialog::ShowAlphaChannel);
    if (c.isValid()) { m_session->setStrokeColor(c.rgba()); refresh(); }
  });
  connect(fill, &QAction::triggered, this, [this] {
    const QColor c = QColorDialog::getColor(toColor(m_session->toolStyle().fillColor), this,
                                            QStringLiteral("Fill colour"), QColorDialog::ShowAlphaChannel);
    if (c.isValid()) { m_session->setFillColor(c.rgba()); refresh(); }
  });
  connect(width, &QAction::triggered, this, [this] {
    m_session->setStrokeWidth(m_session->toolStyle().strokeWidth + 1); refresh();
  });
  connect(thin, &QAction::triggered, this, [this] {
    m_session->setStrokeWidth(std::max(1.0, m_session->toolStyle().strokeWidth - 1)); refresh();
  });
  auto *view = addToolBar(QStringLiteral("View"));
  view->setMovable(false);
  QAction *zoomIn = view->addAction(QStringLiteral("Zoom In"));
  zoomIn->setShortcuts(QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  QAction *zoomOut = view->addAction(QStringLiteral("Zoom Out"));
  zoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  QAction *resetZoom = view->addAction(QStringLiteral("100%"));
  resetZoom->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  QAction *fit = view->addAction(QStringLiteral("Fit"));
  fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));
  connect(zoomIn, &QAction::triggered, m_canvas, &EditorCanvas::zoomIn);
  connect(zoomOut, &QAction::triggered, m_canvas, &EditorCanvas::zoomOut);
  connect(resetZoom, &QAction::triggered, m_canvas, &EditorCanvas::resetZoom);
  connect(fit, &QAction::triggered, m_canvas, &EditorCanvas::zoomToFit);
  connect(m_canvas, &EditorCanvas::viewportChanged, this, &EditorWindow::refresh);
  connect(m_canvas, &EditorCanvas::changed, this, &EditorWindow::refresh);

  resize(std::min(m_image.width() + 40, 1400), std::min(m_image.height() + 120, 900));
  refresh();
  m_canvas->setFocus();
}

void EditorWindow::refresh() {
  m_undo->setEnabled(m_session->undoCount() > 0);
  m_redo->setEnabled(m_session->redoCount() > 0);
  m_delete->setEnabled(!m_session->selection().isEmpty());
  m_rotate->setEnabled(!m_session->selection().isEmpty());
  setWindowTitle(QStringLiteral("%1%2 — XerahS").arg(QFileInfo(m_path).fileName(),
                                                    m_session->dirty() ? QStringLiteral(" •") : QString()));
  statusBar()->showMessage(QStringLiteral("%1 × %2 · %3 annotation(s) · %4 selected · %5%")
                               .arg(m_image.width()).arg(m_image.height())
                               .arg(m_session->document().annotations.size()).arg(m_session->selection().size())
                               .arg(m_canvas->viewState().zoom() * 100, 0, 'f', 1));
  m_canvas->viewport()->update();
}

bool EditorWindow::save(bool chooseFile) {
  QString target = m_path;
  if (chooseFile) {
    target = QFileDialog::getSaveFileName(this, QStringLiteral("Save image"), m_path, QStringLiteral("PNG image (*.png)"));
    if (target.isEmpty()) return false;
    if (QFileInfo(target).suffix().isEmpty()) target += QStringLiteral(".png");
  }
  const SaveOutcome outcome = saveEdit(target, m_image, m_session->document());
  // ES-012: clean only when both artifacts persisted; say which one failed.
  const QStringList failed = m_session->recordSave(outcome.rasterSaved, outcome.sidecarSaved);
  if (!failed.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Save incomplete"),
                         QStringLiteral("Not saved: %1%2").arg(failed.join(QStringLiteral(", ")),
                                                              outcome.rasterError.isEmpty() ? QString() : QStringLiteral("\n") + outcome.rasterError));
    refresh();
    return false;
  }
  m_path = target;
  refresh();
  return true;
}

void EditorWindow::closeEvent(QCloseEvent *event) {
  if (!m_session->dirty()) { event->accept(); return; }
  // ES-008: Save, Discard, Cancel.
  QMessageBox box(QMessageBox::Question, QStringLiteral("Unsaved changes"),
                  QStringLiteral("Save changes to %1?").arg(QFileInfo(m_path).fileName()),
                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
  box.setDefaultButton(QMessageBox::Save);
  switch (box.exec()) {
    case QMessageBox::Save: save(false) ? event->accept() : event->ignore(); break;
    case QMessageBox::Discard: event->accept(); break;
    default: event->ignore();
  }
}

}  // namespace xerahs::app
