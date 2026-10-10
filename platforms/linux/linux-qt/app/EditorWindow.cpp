#include "EditorWindow.h"

#include "EditorCanvas.h"
#include "EditorCanvasOperations.h"
#include "EditorSource.h"

#include <QAction>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QFormLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <array>
#include <cmath>
#include <future>

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
  m_sourceBytes = m_session->document().sourceImagePng;
  setCentralWidget(m_canvas);

  auto *imageMenu = menuBar()->addMenu(QStringLiteral("&Image"));
  QAction *crop = imageMenu->addAction(QStringLiteral("Crop Image…"));
  crop->setObjectName(QStringLiteral("cropImage"));
  QAction *cropTool = imageMenu->addAction(QStringLiteral("Crop tool"));
  cropTool->setCheckable(true);
  cropTool->setObjectName(QStringLiteral("cropTool"));
  connect(crop, &QAction::triggered, this, &EditorWindow::cropImage);
  connect(cropTool, &QAction::toggled, m_canvas, &EditorCanvas::setCropTool);
  connect(m_canvas, &EditorCanvas::cropRequested, this, [this](QPointF start, QPointF end) {
    CanvasOperation operation;
    operation.action = CanvasAction::Crop;
    operation.start = {start.x(), start.y()}; operation.end = {end.x(), end.y()};
    applyCanvas(operation);
  });
  QAction *resizePixels = imageMenu->addAction(QStringLiteral("Resize Image…"));
  resizePixels->setObjectName(QStringLiteral("resizeImage"));
  connect(resizePixels, &QAction::triggered, this, [this] { resizeImage(false); });
  QAction *resizeCanvas = imageMenu->addAction(QStringLiteral("Resize Canvas…"));
  resizeCanvas->setObjectName(QStringLiteral("resizeCanvas"));
  connect(resizeCanvas, &QAction::triggered, this, [this] { resizeImage(true); });
  imageMenu->addSeparator();
  for (const auto &entry : std::array<std::pair<QString, CanvasAction>, 5>{{
      {QStringLiteral("Rotate Image 90° Clockwise"), CanvasAction::RotateClockwise},
      {QStringLiteral("Rotate Image 90° Counter-clockwise"), CanvasAction::RotateCounterClockwise},
      {QStringLiteral("Rotate Image 180°"), CanvasAction::Rotate180},
      {QStringLiteral("Flip Image Horizontally"), CanvasAction::FlipHorizontal},
      {QStringLiteral("Flip Image Vertically"), CanvasAction::FlipVertical}}}) {
    QAction *action = imageMenu->addAction(entry.first);
    action->setObjectName(QStringLiteral("canvasAction%1").arg(int(entry.second)));
    connect(action, &QAction::triggered, this, [this, command = entry.second] {
      CanvasOperation operation; operation.action = command; applyCanvas(operation);
    });
  }

  auto *bar = addToolBar(QStringLiteral("Edit"));
  bar->setMovable(false);
  QAction *saveAction = bar->addAction(QStringLiteral("Save"));
  saveAction->setObjectName(QStringLiteral("saveImage"));
  saveAction->setShortcut(QKeySequence::Save);
  QAction *saveAs = bar->addAction(QStringLiteral("Save As…"));
  saveAs->setShortcut(QKeySequence::SaveAs);
  bar->addSeparator();
  m_undo = bar->addAction(QStringLiteral("Undo"));
  m_undo->setObjectName(QStringLiteral("undo"));
  m_undo->setShortcut(QKeySequence::Undo);
  m_redo = bar->addAction(QStringLiteral("Redo"));
  m_redo->setObjectName(QStringLiteral("redo"));
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
  if (m_sourceBytes != m_session->document().sourceImagePng) {
    const QImage next = QImage::fromData(m_session->document().sourceImagePng, "PNG").convertToFormat(QImage::Format_ARGB32);
    if (!next.isNull()) {
      m_sourceBytes = m_session->document().sourceImagePng;
      m_image = next;
      m_canvas->sourceChanged();
    }
  }
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

void EditorWindow::cropImage() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Crop Image"));
  auto *form = new QFormLayout(&dialog);
  const auto number = [&](const QString &label, int value, int maximum) {
    auto *spin = new QSpinBox(&dialog); spin->setRange(0, maximum); spin->setValue(value);
    spin->setAccessibleName(label); form->addRow(label, spin); return spin;
  };
  auto *left = number(QStringLiteral("Left (pixels)"), 0, m_image.width() - 1);
  auto *top = number(QStringLiteral("Top (pixels)"), 0, m_image.height() - 1);
  auto *width = number(QStringLiteral("Width (pixels)"), m_image.width(), m_image.width()); width->setMinimum(1);
  auto *height = number(QStringLiteral("Height (pixels)"), m_image.height(), m_image.height()); height->setMinimum(1);
  left->setObjectName(QStringLiteral("cropLeft")); top->setObjectName(QStringLiteral("cropTop"));
  width->setObjectName(QStringLiteral("canvasWidth")); height->setObjectName(QStringLiteral("canvasHeight"));
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  CanvasOperation operation;
  operation.action = CanvasAction::Crop;
  operation.start = {double(left->value()), double(top->value())};
  operation.end = {double(left->value() + width->value()), double(top->value() + height->value())};
  applyCanvas(operation);
}

void EditorWindow::resizeImage(bool canvasOnly) {
  QDialog dialog(this);
  dialog.setWindowTitle(canvasOnly ? QStringLiteral("Resize Canvas") : QStringLiteral("Resize Image"));
  auto *form = new QFormLayout(&dialog);
  auto *width = new QSpinBox(&dialog), *height = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("canvasWidth")); height->setObjectName(QStringLiteral("canvasHeight"));
  width->setRange(1, int(kMaxDimension)); height->setRange(1, int(kMaxDimension));
  width->setValue(m_image.width()); height->setValue(m_image.height());
  width->setAccessibleName(QStringLiteral("Width in pixels")); height->setAccessibleName(QStringLiteral("Height in pixels"));
  form->addRow(QStringLiteral("Width (pixels)"), width); form->addRow(QStringLiteral("Height (pixels)"), height);
  auto *choice = new QComboBox(&dialog);
  auto *lock = new QCheckBox(QStringLiteral("Lock aspect ratio"), &dialog); lock->setChecked(true);
  auto *color = new QPushButton(QStringLiteral("Transparent"), &dialog);
  Argb fill = 0;
  if (canvasOnly) {
    choice->addItems({QStringLiteral("Top left"), QStringLiteral("Top"), QStringLiteral("Top right"),
                     QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right"),
                     QStringLiteral("Bottom left"), QStringLiteral("Bottom"), QStringLiteral("Bottom right")});
    choice->setCurrentIndex(4);
    form->addRow(QStringLiteral("Anchor"), choice); form->addRow(QStringLiteral("Added area"), color);
    connect(color, &QPushButton::clicked, &dialog, [&] {
      const QColor selected = QColorDialog::getColor(QColor::fromRgba(fill), &dialog, QStringLiteral("Added area colour"),
                                                     QColorDialog::ShowAlphaChannel);
      if (selected.isValid()) { fill = selected.rgba(); color->setText(selected.name(QColor::HexArgb)); }
    });
  } else {
    choice->addItems({QStringLiteral("Nearest neighbour"), QStringLiteral("Bilinear")});
    form->addRow(lock); form->addRow(QStringLiteral("Interpolation"), choice);
    connect(width, &QSpinBox::valueChanged, &dialog, [&](int value) {
      if (lock->isChecked()) { const QSignalBlocker block(height); height->setValue(int(std::floor(double(value) * m_image.height() / m_image.width() + .5))); }
    });
    connect(height, &QSpinBox::valueChanged, &dialog, [&](int value) {
      if (lock->isChecked()) { const QSignalBlocker block(width); width->setValue(int(std::floor(double(value) * m_image.width() / m_image.height() + .5))); }
    });
    connect(lock, &QCheckBox::toggled, &dialog, [&](bool active) {
      if (active) height->setValue(int(std::floor(double(width->value()) * m_image.height() / m_image.width() + .5)));
    });
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  CanvasOperation operation;
  operation.action = canvasOnly ? CanvasAction::ResizeCanvas : CanvasAction::ResizeImage;
  operation.width = width->value(); operation.height = height->value();
  operation.anchor = CanvasAnchor(choice->currentIndex()); operation.fill = fill;
  operation.interpolation = Interpolation(choice->currentIndex()); operation.lockAspect = lock->isChecked();
  applyCanvas(operation);
}

void EditorWindow::applyCanvas(CanvasOperation operation) {
  const quint64 state = m_session->stateId();
  const AnnotationDocument document = m_session->document();
  const QImage source = m_image;
  std::atomic<bool> cancelled{false};
  std::atomic<int> progress{0};
  QProgressDialog dialog(QStringLiteral("Editing image…"), QStringLiteral("Cancel"), 0, 100, this);
  dialog.setWindowTitle(QStringLiteral("Image operation"));
  dialog.setWindowModality(Qt::WindowModal);
  dialog.setAutoClose(false); dialog.setAutoReset(false);
  auto worker = std::async(std::launch::async, [&] {
    return prepareCanvasEdit(document, source, operation, CanvasControl{
        [&] { return cancelled.load(); }, [&](int value) { progress.store(value); }});
  });
  connect(&dialog, &QProgressDialog::canceled, this, [&] { cancelled.store(true); });
  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, [&] {
    dialog.setValue(progress.load());
    if (worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) dialog.accept();
  });
  timer.start(20);
  dialog.exec();
  // Cancellation may close the dialog before the worker exits. Keep the
  // modal window open until all captured state is safe to release.
  if (worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    cancelled.store(true); dialog.setLabelText(QStringLiteral("Cancelling…")); dialog.exec();
  }
  const auto prepared = worker.get();
  if (cancelled.load() || prepared.result.error == QStringLiteral("canvas-cancelled")) return;
  if (prepared.result.error) {
    QString message = *prepared.result.error;
    if (message == u"canvas-annotation-shear-unsupported") message = QStringLiteral("Keep the aspect ratio locked to preserve rotated or stroked annotations.");
    else if (message == u"canvas-annotation-transform-unsupported") message = QStringLiteral("This document contains annotation types this editor cannot transform yet.");
    else if (message == diagnostic::documentTooLarge) message = QStringLiteral("The requested image exceeds the editor's dimensions or memory limits.");
    QMessageBox::warning(this, QStringLiteral("Cannot edit image"), message);
    return;
  }
  if (!prepared.result.changed) return;
  if (!m_session->commitCanvas(*prepared.result.document, state)) {
    QMessageBox::warning(this, QStringLiteral("Cannot edit image"),
                         m_session->stateId() != state ? QStringLiteral("The document changed while this operation was being prepared.") :
                         QStringLiteral("The edited image could not be validated or stored in undo history."));
    return;
  }
  refresh();
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
