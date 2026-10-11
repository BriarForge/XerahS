#include "EditorWindow.h"

#include "EditorCanvas.h"
#include "EditorCanvasOperations.h"
#include "CanvasRotationDialog.h"
#include "ImageComparisonDialog.h"
#include "EditorSource.h"

#include <QAction>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QFormLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScopedValueRollback>
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
  if (diagnostic == diagnostic::documentVersionUnsupported) return QStringLiteral("The annotation file was created by a newer editor version and cannot be opened safely.");
  if (diagnostic == diagnostic::documentInvalid) return QStringLiteral("The annotation file or its embedded source is damaged and cannot be opened safely.");
  if (diagnostic == diagnostic::documentTooLarge) return QStringLiteral("The annotation document exceeds the editor's resource limits.");
  return diagnostic;
}

}  // namespace

EditorWindow *EditorWindow::open(const QString &path, QWidget *parent) {
  LoadedSource source = loadSource(path);
  if (source.pendingChoice) {
    const auto &sources = *source.pendingChoice;
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Image changed since annotations were saved"),
        QStringLiteral("Choose the image to use with the saved annotations. Their coordinates will be kept. "
                       "Cancel leaves both files unchanged."), QMessageBox::Cancel, parent);
    auto *current = box.addButton(QStringLiteral("Current raster (%1 × %2)").arg(sources.currentRaster.width()).arg(sources.currentRaster.height()), QMessageBox::ActionRole);
    auto *embedded = box.addButton(QStringLiteral("Embedded source (%1 × %2)").arg(sources.embeddedSource.width()).arg(sources.embeddedSource.height()), QMessageBox::ActionRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == current) source = resolveSourceChoice(source, SourceChoice::CurrentRaster);
    else if (box.clickedButton() == embedded) source = resolveSourceChoice(source, SourceChoice::EmbeddedSource);
    else return nullptr;
  }
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
  m_unsupportedDock = new QDockWidget(QStringLiteral("Unsupported annotations"), this);
  m_unsupportedDock->setFeatures(QDockWidget::DockWidgetMovable);
  m_unsupportedDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  m_unsupportedObjects = new QListWidget(m_unsupportedDock);
  m_unsupportedObjects->setObjectName(QStringLiteral("unsupportedAnnotations"));
  m_unsupportedObjects->setAccessibleName(QStringLiteral("Unsupported annotations"));
  m_unsupportedObjects->setAccessibleDescription(QStringLiteral("These objects are preserved in the document. Their tools are not available in this editor version."));
  m_unsupportedDock->setWidget(m_unsupportedObjects);
  addDockWidget(Qt::RightDockWidgetArea, m_unsupportedDock);

  auto *imageMenu = menuBar()->addMenu(QStringLiteral("&Image"));
  QAction *crop = imageMenu->addAction(QStringLiteral("Crop Image…"));
  crop->setObjectName(QStringLiteral("cropImage"));
  QAction *cropTool = imageMenu->addAction(QStringLiteral("Crop tool"));
  cropTool->setCheckable(true);
  cropTool->setObjectName(QStringLiteral("cropTool"));
  connect(crop, &QAction::triggered, this, &EditorWindow::cropImage);
  QAction *autoCrop = imageMenu->addAction(QStringLiteral("Auto Crop…"));
  autoCrop->setObjectName(QStringLiteral("autoCrop"));
  connect(autoCrop, &QAction::triggered, this, &EditorWindow::autoCropImage);
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
  QAction *customRotation = imageMenu->addAction(QStringLiteral("Rotate Image…"));
  customRotation->setObjectName(QStringLiteral("rotateImage"));
  connect(customRotation, &QAction::triggered, this, &EditorWindow::rotateImage);
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
  imageMenu->addSeparator();
  QAction *flatten = imageMenu->addAction(QStringLiteral("Flatten"));
  flatten->setObjectName(QStringLiteral("flattenImage"));
  connect(flatten, &QAction::triggered, this, [this] { CanvasOperation op; op.action = CanvasAction::Flatten; applyCanvas(op); });
  m_clearAnnotations = imageMenu->addAction(QStringLiteral("Clear Annotations"));
  m_clearAnnotations->setObjectName(QStringLiteral("clearAnnotations"));
  connect(m_clearAnnotations, &QAction::triggered, this, [this] { CanvasOperation op; op.action = CanvasAction::ClearAnnotations; applyCanvas(op); });
  QAction *clear = imageMenu->addAction(QStringLiteral("Clear Image and Annotations…"));
  clear->setObjectName(QStringLiteral("clearImage"));
  connect(clear, &QAction::triggered, this, &EditorWindow::clearImage);
  QAction *compare = imageMenu->addAction(QStringLiteral("Compare Images…"));
  compare->setObjectName(QStringLiteral("compareImages"));
  connect(compare, &QAction::triggered, this, &EditorWindow::compareImages);

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
  m_delete->setObjectName(QStringLiteral("deleteSelection"));
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
  if (m_placeholderState != m_session->stateId()) {
    m_placeholderState = m_session->stateId(); m_unsupportedObjects->clear();
    for (const Annotation &a : m_session->document().annotations) if (const auto *object = std::get_if<UnsupportedAnnotation>(&a)) {
      auto *item = new QListWidgetItem(QStringLiteral("Unsupported: %1%2").arg(object->raw.value(u"type").toString(),
          object->raw.value(u"visible").toBool(true) ? QString() : QStringLiteral(" (hidden)")), m_unsupportedObjects);
      item->setData(Qt::UserRole, object->id.toString(QUuid::WithoutBraces));
    }
    m_unsupportedDock->setVisible(m_unsupportedObjects->count() > 0);
  }
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
  m_clearAnnotations->setEnabled(!m_session->document().annotations.empty());
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

void EditorWindow::autoCropImage() {
  QDialog dialog(this);
  dialog.setWindowTitle(QStringLiteral("Auto Crop"));
  auto *form = new QFormLayout(&dialog);
  auto *border = new QComboBox(&dialog);
  border->setObjectName(QStringLiteral("cropBorder"));
  border->addItems({QStringLiteral("Transparent pixels"), QStringLiteral("Match top-left pixel"), QStringLiteral("Match chosen colour")});
  form->addRow(QStringLiteral("Border rule"), border);
  auto *alpha = new QSpinBox(&dialog), *tolerance = new QSpinBox(&dialog);
  alpha->setObjectName(QStringLiteral("cropAlpha")); tolerance->setObjectName(QStringLiteral("cropTolerance"));
  alpha->setRange(0, 255); tolerance->setRange(0, 255); tolerance->setEnabled(false);
  form->addRow(QStringLiteral("Ignore alpha at or below"), alpha);
  form->addRow(QStringLiteral("RGBA channel tolerance"), tolerance);
  Argb selectedColor = 0xFFFFFFFF;
  auto *color = new QPushButton(QStringLiteral("#FFFFFFFF"), &dialog); color->setEnabled(false);
  form->addRow(QStringLiteral("Border colour"), color);
  connect(color, &QPushButton::clicked, &dialog, [&] {
    const QColor chosen = QColorDialog::getColor(QColor::fromRgba(selectedColor), &dialog, QStringLiteral("Border colour"), QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) { selectedColor = chosen.rgba(); color->setText(chosen.name(QColor::HexArgb)); }
  });
  connect(border, &QComboBox::currentIndexChanged, &dialog, [&](int index) {
    tolerance->setEnabled(index != 0); color->setEnabled(index == 2);
  });
  auto *annotations = new QCheckBox(QStringLiteral("Include visible annotations when finding bounds"), &dialog);
  annotations->setObjectName(QStringLiteral("cropAnnotations")); annotations->setChecked(true); form->addRow(annotations);
  auto *rules = new QLabel(QStringLiteral("Trim complete background rows and columns from the four edges. "
      "Colour matching allows the stated difference in every RGBA channel. "
      "An empty image or unchanged bounds leave the document unchanged."), &dialog);
  rules->setWordWrap(true); form->addRow(rules);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  CanvasOperation operation; operation.action = CanvasAction::AutoCrop;
  operation.autoCrop = {CropBorder(border->currentIndex()), alpha->value(), tolerance->value(), selectedColor, annotations->isChecked()};
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

void EditorWindow::rotateImage() {
  const quint64 state = m_session->stateId();
  CanvasRotationDialog dialog(m_session->document(), m_image, this);
  if (dialog.exec() != QDialog::Accepted) return;
  if (auto edit = dialog.takeEdit()) commitCanvas(std::move(*edit), state);
}

void EditorWindow::clearImage() {
  const bool dirty = m_session->dirty();
  QMessageBox box(QMessageBox::Warning, QStringLiteral("Clear Image and Annotations"),
      QStringLiteral("Remove all pixels and annotation objects from this canvas, keeping its dimensions and colour profile? "
                     "You can restore the current document with Undo."),
      dirty ? QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel : QMessageBox::Ok | QMessageBox::Cancel, this);
  box.setDefaultButton(QMessageBox::Cancel);
  if (dirty) {
    box.setInformativeText(QStringLiteral("Save the current changes before clearing?"));
    box.button(QMessageBox::Save)->setText(QStringLiteral("Save, then Clear"));
    box.button(QMessageBox::Discard)->setText(QStringLiteral("Clear without Saving"));
  } else box.button(QMessageBox::Ok)->setText(QStringLiteral("Clear"));
  const int answer = box.exec();
  if (answer == QMessageBox::Save && !save(false)) return;
  if (answer != QMessageBox::Save && answer != QMessageBox::Discard && answer != QMessageBox::Ok) return;
  CanvasOperation op; op.action = CanvasAction::ClearImageAndAnnotations; applyCanvas(op);
}

void EditorWindow::applyCanvas(CanvasOperation operation) {
  if (m_canvasOperationPending) return;
  const QScopedValueRollback<bool> pending(m_canvasOperationPending, true);
  const quint64 state = m_session->stateId();
  const AnnotationDocument document = m_session->document();
  const QImage source = m_image;
  std::atomic<bool> cancelled{false};
  std::atomic<int> progress{0};
  QProgressDialog dialog(QStringLiteral("Editing image…"), QStringLiteral("Cancel"), 0, 100, this);
  dialog.setWindowTitle(QStringLiteral("Image operation"));
  dialog.setWindowModality(Qt::WindowModal);
  dialog.setAutoClose(false); dialog.setAutoReset(false);
  std::future<PreparedCanvasEdit> worker;
  try {
    worker = std::async(std::launch::async, [&] {
      return prepareCanvasEdit(document, source, operation, CanvasControl{
          [&] { return cancelled.load(); }, [&](int value) { progress.store(value); }});
    });
  } catch (const std::exception &) {
    QMessageBox::warning(this, QStringLiteral("Cannot edit image"), QStringLiteral("The image operation could not be started. The document has been kept unchanged."));
    return;
  }
  QEventLoop completion;
  connect(&dialog, &QProgressDialog::canceled, &completion, [&] {
    cancelled.store(true); dialog.setLabelText(QStringLiteral("Cancelling…")); dialog.setCancelButton(nullptr);
  });
  connect(&dialog, &QDialog::rejected, &completion, [&] { cancelled.store(true); });
  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, [&] {
    dialog.setValue(progress.load());
    if (worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { dialog.accept(); completion.quit(); }
  });
  timer.start(20);
  // Closing/cancelling the progress window does not release captured state.
  // Keep processing native events until the same worker actually finishes.
  dialog.open(); completion.exec();
  auto prepared = worker.get();
  if (cancelled.load() || prepared.result.error == QStringLiteral("canvas-cancelled")) return;
  commitCanvas(std::move(prepared), state);
}

void EditorWindow::compareImages() {
  if (m_canvasOperationPending) return;
  const QScopedValueRollback<bool> pending(m_canvasOperationPending, true);
  ImageComparisonDialog dialog(m_session->document(), m_image, this);
  dialog.exec();
}

void EditorWindow::commitCanvas(PreparedCanvasEdit prepared, quint64 state) {
  if (prepared.result.error) {
    QString message = *prepared.result.error;
    if (message == u"canvas-annotation-shear-unsupported") message = QStringLiteral("Keep the aspect ratio locked to preserve rotated or stroked annotations.");
    else if (message == u"canvas-annotation-render-unsupported") message = QStringLiteral("This document contains visible annotation types this editor cannot flatten yet. The document has been kept unchanged.");
    else if (message == u"canvas-annotation-transform-unsupported") message = QStringLiteral("This document contains annotation types this editor cannot transform yet.");
    else if (message == diagnostic::documentTooLarge) message = QStringLiteral("The requested image exceeds the editor's dimensions or memory limits.");
    else if (message == diagnostic::documentInvalid) message = QStringLiteral("The image or annotation data is invalid.");
    else if (message == u"canvas-encoding-failed") message = QStringLiteral("The edited source could not be encoded.");
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
  // A cancelled progress window can hide before its worker exits. Keep the
  // editor and its stack-owned modal objects alive until preparation completes.
  if (m_canvasOperationPending) { event->ignore(); return; }
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
