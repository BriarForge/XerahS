#include "EditorWindow.h"

#include "EditorSource.h"

#include <QAction>
#include <QCloseEvent>
#include <QColorDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>

#include <cmath>

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

// Draws the source with the session's rectangles and turns pointer and keyboard
// input into session operations. Preview painting is not the export (ES-010).
class EditorCanvas final : public QWidget {
  Q_OBJECT
public:
  EditorCanvas(EditorSession &session, const QImage &image) : m_session(session), m_image(image) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setAccessibleName(QStringLiteral("Image canvas"));
    setAccessibleDescription(QStringLiteral("Drag to draw a rectangle. Arrow keys move the selection; Delete removes it."));
    resize(image.size());
    setMinimumSize(image.size());
  }

signals:
  void changed();

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.drawImage(0, 0, m_image);
    p.setRenderHint(QPainter::Antialiasing);
    for (const Annotation &annotation : m_session.document().annotations) {
      const auto *r = std::get_if<RectangleAnnotation>(&annotation);
      if (!r || !r->visible) continue;
      const QRectF box(QPointF(r->left(), r->top()), QPointF(r->right(), r->bottom()));
      p.save();
      if (m_moving && m_session.selection().contains(r->id)) p.translate(m_moveLast - m_moveStart);  // gesture preview
      p.translate(box.center());
      p.rotate(r->style.rotationDegrees);
      p.translate(-box.center());
      p.setOpacity(r->style.opacity);
      p.setBrush(toColor(r->style.fillColor));
      p.setPen(QPen(toColor(r->style.strokeColor), r->style.strokeWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
      p.drawRect(box);
      p.restore();
      if (m_session.selection().contains(r->id)) {
        // Non-colour-only selection (ES-019): dashed two-tone outline.
        const QRectF outline = box.adjusted(-r->style.strokeWidth, -r->style.strokeWidth, r->style.strokeWidth,
                                            r->style.strokeWidth);
        p.save();
        if (m_moving) p.translate(m_moveLast - m_moveStart);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 2));
        p.drawRect(outline);
        p.setPen(QPen(QColor(0, 90, 220), 2, Qt::DashLine));
        p.drawRect(outline);
        p.restore();
      }
    }
    if (m_drag) {
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(QColor(0, 90, 220), 1, Qt::DashLine));
      p.drawRect(QRectF(m_drag->first, m_drag->second).normalized());
    }
    if (hasFocus()) {
      p.setRenderHint(QPainter::Antialiasing, false);
      p.setPen(QPen(QColor(0, 90, 220), 2));
      p.setBrush(Qt::NoBrush);
      p.drawRect(rect().adjusted(1, 1, -1, -1));
    }
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton) return;
    setFocus();
    const QPointF at = event->position();
    if (const auto hit = topmostAt(at)) {
      // Clicking an annotation selects it and begins a move gesture (ES-004).
      const bool additive = event->modifiers() & Qt::ControlModifier;
      QList<QUuid> ids;
      if (additive) {
        for (const QUuid &id : m_session.selection()) ids << id;
        if (!ids.removeOne(*hit)) ids << *hit;
      } else if (!m_session.selection().contains(*hit)) {
        ids << *hit;
      } else {
        for (const QUuid &id : m_session.selection()) ids << id;
      }
      m_session.select(ids);
      m_moving = m_session.selection().contains(*hit);
      m_moveStart = m_moveLast = at;
    } else {
      if (!(event->modifiers() & Qt::ControlModifier)) m_session.select({});
      m_drag = std::make_pair(at, at);
    }
    update();
    emit changed();
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (m_drag) {
      m_drag->second = event->position();
      update();
    } else if (m_moving) {
      m_moveLast = event->position();
      update();
    }
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton) return;
    if (m_drag) {
      const auto [a, b] = *m_drag;
      m_drag.reset();
      m_session.createRectangle({a.x(), a.y()}, {event->position().x(), event->position().y()});
    } else if (m_moving) {
      m_moving = false;
      // ES-005: one history operation per completed gesture.
      const QPointF delta = event->position() - m_moveStart;
      if (delta.x() != 0 || delta.y() != 0) m_session.moveSelection(delta.x(), delta.y());
    }
    update();
    emit changed();
  }

  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Escape && (m_drag || m_moving)) {
      // ES-008: cancelling a gesture restores the last committed state.
      m_drag.reset();
      m_moving = false;
      update();
      return;
    }
    const double step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (event->key()) {
      case Qt::Key_Left: m_session.moveSelection(-step, 0); break;
      case Qt::Key_Right: m_session.moveSelection(step, 0); break;
      case Qt::Key_Up: m_session.moveSelection(0, -step); break;
      case Qt::Key_Down: m_session.moveSelection(0, step); break;
      case Qt::Key_Delete:
      case Qt::Key_Backspace: m_session.deleteSelection(); break;
      default: QWidget::keyPressEvent(event); return;
    }
    update();
    emit changed();
  }

  void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
  void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

private:
  // Hit targets include the stroke and a minimum slop so thin lines stay usable.
  std::optional<QUuid> topmostAt(const QPointF &at) const {
    const auto &list = m_session.document().annotations;
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
      const auto *r = std::get_if<RectangleAnnotation>(&*it);
      if (!r || !r->visible) continue;
      const double slop = std::max(6.0, r->style.strokeWidth / 2);
      if (QRectF(QPointF(r->left(), r->top()), QPointF(r->right(), r->bottom())).adjusted(-slop, -slop, slop, slop).contains(at))
        return r->id;
    }
    return std::nullopt;
  }

  EditorSession &m_session;
  const QImage &m_image;
  std::optional<std::pair<QPointF, QPointF>> m_drag;
  bool m_moving = false;
  QPointF m_moveStart, m_moveLast;
};

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
  auto *scroll = new QScrollArea;
  scroll->setWidget(m_canvas);
  scroll->setAlignment(Qt::AlignCenter);
  setCentralWidget(scroll);

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
  connect(m_canvas, &EditorCanvas::changed, this, &EditorWindow::refresh);

  resize(std::min(m_image.width() + 40, 1400), std::min(m_image.height() + 120, 900));
  refresh();
  m_canvas->setFocus();
}

void EditorWindow::refresh() {
  m_undo->setEnabled(m_session->undoCount() > 0);
  m_redo->setEnabled(m_session->redoCount() > 0);
  m_delete->setEnabled(!m_session->selection().isEmpty());
  setWindowTitle(QStringLiteral("%1%2 — XerahS").arg(QFileInfo(m_path).fileName(),
                                                    m_session->dirty() ? QStringLiteral(" •") : QString()));
  statusBar()->showMessage(QStringLiteral("%1 × %2 · %3 annotation(s) · %4 selected")
                               .arg(m_image.width()).arg(m_image.height())
                               .arg(m_session->document().annotations.size()).arg(m_session->selection().size()));
  m_canvas->update();
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

#include "EditorWindow.moc"
