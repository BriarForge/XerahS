#include "EditorWindow.h"

#include "EditorCanvas.h"
#include "EditorCanvasOperations.h"
#include "CanvasRotationDialog.h"
#include "ImageComparisonDialog.h"
#include "NewImageDialog.h"
#include "EditorSource.h"
#include "EditorOpen.h"
#include "RecentImageMenu.h"
#include "DrawingDefaultsDialog.h"
#include "NativeEditorWindowState.h"
#include "NativeEditorAppearance.h"
#include "AppearanceDialog.h"
#include "NativeEditorToolbar.h"
#include "NativeAnnotationTools.h"
#include "RectanglePropertiesDialog.h"
#include "SelectionRotationDialog.h"
#include "AnnotationColorDialog.h"
#include "AnnotationObjectNavigator.h"
#include "tools/ColorFormats.h"
#include <algorithm>

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
#include <QLabel>
#include <QListWidget>
#include <QFormLayout>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QPointer>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace xerahs::app {

using namespace xerahs::editor;

namespace {

void disposeCanvasDialog(QPointer<QDialog> dialog) {
  if (dialog) dialog->hide();
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
}

void editorWarning(QWidget *parent, const QString &title, const QString &text) {
  QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, title, text, QMessageBox::Ok, parent));
  box->setTextFormat(Qt::PlainText); box->exec(); disposeCanvasDialog(box.data());
}

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

EditorWindow::~EditorWindow() {
  // QWidget teardown can send focus events to the active canvas. Destroy the
  // canvas while its referenced image/session still exist, before members and
  // the QMainWindow base are destroyed; no refresh may run during that teardown.
  delete m_toolbarController; m_toolbarController = nullptr;
  m_canvas->disconnect(this);
  delete takeCentralWidget(); m_canvas = nullptr;
}

EditorWindow *EditorWindow::open(const QString &path, QWidget *parent) {
  const QPointer<QWidget> owner(parent); const auto editor = qobject_cast<EditorWindow *>(parent);
  if (editor && (editor->m_canvasOperationPending || editor->m_closePending || editor->m_outputPending || editor->m_canvas->interactionActive())) return nullptr;
  const auto hooks = editor ? editor->m_openHooks : EditorOpenHooks{};
  QPointer<EditorWindow> window;
  const auto operation = [owner, parent, path, hooks, &window] {
    if (parent && !owner) return false;
    const auto target = absoluteImagePath(path); auto source = openSource(target, owner, hooks);
    if ((parent && !owner) || source.diagnostic == u"canvas-cancelled") return false;
    if (!source.document) { editorWarning(owner, "Cannot open image", QStringLiteral("%1\n\n%2").arg(QFileInfo(path).fileName(), describe(source.diagnostic))); return false; }
    try { window = new EditorWindow(target, std::move(source.image), std::move(*source.document), EditorSession::InitialPersistence::Saved, std::move(source.displayImage)); }
    catch (const std::bad_alloc &) { editorWarning(owner, "Cannot open image", describe(diagnostic::documentTooLarge)); return false; }
    window->setAttribute(Qt::WA_DeleteOnClose);
    if (!source.warnings.isEmpty()) {
      QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Information, "Annotations", source.warnings.join(QLatin1Char('\n')), QMessageBox::Ok, window)); box->setTextFormat(Qt::PlainText); box->exec(); disposeCanvasDialog(box.data());
    }
    if (parent && !owner) { delete window.data(); return false; }
    if (window) window->recordRecentImage(target);
    return bool(window);
  };
  if (editor) editor->withOutputBusy(operation); else operation();
  return window.data();
}

EditorWindow *EditorWindow::createNew(QWidget *parent) {
  const QPointer<QWidget> owner(parent); const auto editor = qobject_cast<EditorWindow *>(parent);
  if (editor && (editor->m_canvasOperationPending || editor->m_closePending || editor->m_outputPending || editor->m_canvas->interactionActive())) return nullptr;
  const auto hooks = editor ? editor->m_newImageHooks : NewImageHooks{}; QPointer<EditorWindow> window;
  const auto operation = [parent, owner, hooks, &window] {
    if (parent && !owner) return false;
    QPointer<NewImageDialog> dialog(new NewImageDialog(owner, hooks)); const auto answer = dialog->exec();
    auto prepared = dialog && answer == QDialog::Accepted ? dialog->takeImage() : std::optional<PreparedNewImage>{};
    disposeCanvasDialog(dialog.data()); if ((parent && !owner) || !prepared) return false;
    try { window = new EditorWindow({}, std::move(prepared->source), std::move(*prepared->document), EditorSession::InitialPersistence::Unsaved, std::move(prepared->displaySource)); window->setAttribute(Qt::WA_DeleteOnClose); }
    catch (const std::bad_alloc &) { editorWarning(owner, "Cannot create image", "There is not enough memory to open the new image."); }
    return bool(window);
  };
  if (editor) editor->withOutputBusy(operation); else operation();
  return window.data();
}

EditorWindow *EditorWindow::fromCapturedImage(const QImage &image, QWidget *parent) {
  const QPointer<QWidget> owner(parent); auto source = openCapturedSource(image, parent);
  if (parent && !owner) return nullptr;
  if (source.diagnostic == u"canvas-cancelled") return nullptr;
  if (!source.document) {
    QMessageBox::warning(parent, QStringLiteral("Cannot edit retained capture"), describe(source.diagnostic)); return nullptr;
  }
  try {
    auto *window = new EditorWindow({}, std::move(source.image), std::move(*source.document),
        EditorSession::InitialPersistence::Unsaved, std::move(source.displayImage));
    window->setAttribute(Qt::WA_DeleteOnClose); return window;
  } catch (const std::bad_alloc &) {
    QMessageBox::warning(parent, QStringLiteral("Cannot edit retained capture"), describe(diagnostic::documentTooLarge)); return nullptr;
  }
}

EditorWindow *EditorWindow::fromCapturedDocument(const AnnotationDocument &document, QWidget *parent) {
  const QPointer<QWidget> owner(parent); auto source = openCapturedDocument(document, parent);
  if (parent && !owner) return nullptr;
  if (source.diagnostic == "canvas-cancelled") return nullptr;
  if (!source.document) { QMessageBox::warning(parent, "Cannot edit retained annotations", describe(source.diagnostic)); return nullptr; }
  try {
    auto *window = new EditorWindow({}, std::move(source.image), std::move(*source.document), EditorSession::InitialPersistence::Unsaved, std::move(source.displayImage));
    window->setAttribute(Qt::WA_DeleteOnClose); return window;
  } catch (const std::bad_alloc &) { QMessageBox::warning(parent, "Cannot edit retained annotations", describe(diagnostic::documentTooLarge)); return nullptr; }
}
EditorWindow::EditorWindow(const QString &path, QImage image, AnnotationDocument document,
                           EditorSession::InitialPersistence persistence, QImage displaySource)
    : m_path(path), m_image(std::move(image)), m_session(std::make_unique<EditorSession>(std::move(document), persistence)) {
  applyCreationDefaults();
  m_canvas = new EditorCanvas(*m_session, m_image, nullptr, displaySource);
  const auto tools = annotationToolActions(*m_canvas, this);
  auto *instructions = annotationToolInstructions(*m_canvas, this);
  instructions->setMaximumWidth(460);
  statusBar()->addPermanentWidget(instructions, 1);
  // EDITOR-UTILITIES-001: the pointer readout. One permanent label on the right
  // of the status bar carries the document pixel, its source pixel colour and
  // the image size.
  m_pointerStatus = new QLabel(this);
  m_pointerStatus->setObjectName(QStringLiteral("editorPointerStatus"));
  m_pointerStatus->setAccessibleName(QStringLiteral("Pointer pixel"));
  m_pointerStatus->setAccessibleDescription(QStringLiteral("Document position, source pixel colour and image size."));
  statusBar()->addPermanentWidget(m_pointerStatus);
  connect(m_canvas, &EditorCanvas::pointerPixelChanged, this, [this](std::optional<QPoint> pixel) {
    if (pixel == m_pointerPixel) return;
    m_pointerPixel = pixel;
    updatePointerStatus();
  });
  connect(m_canvas, &EditorCanvas::editRejected, this, [this](const QString &diagnostic) {
    m_annotationMessage = annotationEditMessage(diagnostic); m_annotationMessageState = m_session->stateId();
  });
  m_sourceBytes = m_session->document().sourceImagePng;
  setCentralWidget(m_canvas);
  m_annotationsDock = new QDockWidget("Annotation objects", this);
  m_annotationsDock->setObjectName("annotationObjectsDock");
  m_annotationsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
  m_annotationsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  m_annotationObjects = new AnnotationObjectNavigator(*m_canvas, m_annotationsDock, [this] { return !m_canvasOperationPending && !m_outputPending && !m_closePending; });
  m_annotationsDock->setWidget(m_annotationObjects); addDockWidget(Qt::RightDockWidgetArea, m_annotationsDock);
  m_annotationsDock->setVisible(std::any_of(m_session->document().annotations.begin(), m_session->document().annotations.end(),
      [](const Annotation &a) { return std::holds_alternative<UnsupportedAnnotation>(a); }));

  auto *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  auto *create = fileMenu->addAction(QStringLiteral("New Image…"));
  create->setObjectName(QStringLiteral("newImage"));
  create->setShortcut(QKeySequence::New);
  connect(create, &QAction::triggered, this, &EditorWindow::newImage);
  auto *open = fileMenu->addAction(QStringLiteral("Open Image…"));
  open->setObjectName(QStringLiteral("openImage")); open->setShortcut(QKeySequence::Open);
  connect(open, &QAction::triggered, this, [this] { openImage(); });
  addRecentImagesMenu(fileMenu, [this](const QString &path) { openImage(path); }, [this] {
    if (m_canvasOperationPending) return;
    const QScopedValueRollback<bool> pending(m_canvasOperationPending, true);
    if (manageRecentImages(this)) { m_preferenceDiagnostic.clear(); refresh(); }
  });
  auto *preferences = menuBar()->addMenu(QStringLiteral("&Preferences"));
  auto *customizeToolbar = preferences->addAction(QStringLiteral("Toolbar…"));
  customizeToolbar->setObjectName(QStringLiteral("customizeToolbar"));
  connect(customizeToolbar, &QAction::triggered, this, [this] {
    configurePreferences([this] { return manageEditorToolbar(this, m_toolbarActions); });
  });
  auto *drawingDefaults = preferences->addAction(QStringLiteral("Drawing Defaults…"));
  drawingDefaults->setObjectName(QStringLiteral("drawingDefaults"));
  connect(drawingDefaults, &QAction::triggered, this, [this] {
    const QPointer<EditorWindow> owner(this);
    const bool saved = configurePreferences([this] { return manageDrawingDefaults(this); });
    if (owner && saved) { applyCreationDefaults(); m_drawingPreferenceDiagnostic.clear(); refresh(); }
  });
  connect(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged, this, [this] {
    applyCreationDefaults(); m_drawingPreferenceDiagnostic.clear(); refresh();
  });
  auto *rememberWindow = preferences->addAction(QStringLiteral("Remember Window Size and Maximized State"));
  auto *appearance = preferences->addAction(QStringLiteral("Appearance…")); appearance->setObjectName(QStringLiteral("manageAppearance"));
  connect(appearance, &QAction::triggered, this, [this] {
    configurePreferences([this] { return manageEditorAppearance(this); });
  });
  rememberWindow->setObjectName(QStringLiteral("rememberWindowState")); rememberWindow->setCheckable(true);
  rememberWindow->setChecked(windowPreferencesFromPreferences(nativeEditorPreferences()).remember);
  connect(rememberWindow, &QAction::triggered, this, [this, rememberWindow](bool enabled) {
    if (m_canvasOperationPending || m_closePending) {
      const QSignalBlocker blocker(rememberWindow);
      rememberWindow->setChecked(windowPreferencesFromPreferences(nativeEditorPreferences()).remember); return;
    }
    const QScopedValueRollback<bool> pending(m_canvasOperationPending, true);
    QJsonObject changes = enabled ? m_windowState->snapshot() : QJsonObject{};
    changes.insert(QStringLiteral("RememberWindowState"), enabled);
    const auto written = applyWindowPreferences(changes, nativeEditorPreferenceRevision());
    if (!written.saved) {
      const QSignalBlocker blocker(rememberWindow);
      rememberWindow->setChecked(windowPreferencesFromPreferences(nativeEditorPreferences()).remember);
      QMessageBox::warning(this, QStringLiteral("Preferences not saved"), editorPreferenceDiagnosticMessage(written.diagnostic));
    }
  });
  connect(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::windowMemoryChanged, rememberWindow,
      [rememberWindow](bool enabled) { const QSignalBlocker blocker(rememberWindow); rememberWindow->setChecked(enabled); });

  auto *imageMenu = menuBar()->addMenu(QStringLiteral("&Image"));
  auto *toolMenu = imageMenu->addMenu(QStringLiteral("Annotation Tools"));
  for (const auto &id : {"select-tool", "rectangle-tool", "ellipse-tool", "line-tool", "arrow-tool", "freehand-tool", "text-tool",
                         "step-tool", "highlight-tool", "blur-tool", "pixelate-tool", "speechballoon-tool", "magnify-tool", "spotlight-tool",
                         "smarteraser-tool", "image-tool", "emoji-tool", "cursor-tool", "cutout-tool", "crop-tool"}) toolMenu->addAction(tools.value(id));
  QAction *crop = imageMenu->addAction(QStringLiteral("Crop Image…"));
  crop->setObjectName(QStringLiteral("cropImage"));
  QAction *cropTool = tools.value("crop-tool");
  imageMenu->addAction(cropTool);
  connect(crop, &QAction::triggered, this, [this] { cropImage(); });
  QAction *autoCrop = imageMenu->addAction(QStringLiteral("Auto Crop…"));
  autoCrop->setObjectName(QStringLiteral("autoCrop"));
  connect(autoCrop, &QAction::triggered, this, &EditorWindow::autoCropImage);
  connect(m_canvas, &EditorCanvas::cropRequested, this, [this](QPointF start, QPointF end) {
    const auto region = normalizedCropRegion(m_image.width(), m_image.height(), {start.x(), start.y()}, {end.x(), end.y()});
    if (region) cropImage(*region);
  });
  connect(m_canvas, &EditorCanvas::cutOutRequested, this, [this](QPointF start, QPointF end, bool columns) {
    CanvasOperation operation; operation.action = CanvasAction::CutOut;
    operation.start = {start.x(), start.y()}; operation.end = {end.x(), end.y()}; operation.cutColumns = columns;
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
  QMap<CanvasAction, QAction *> canvasActions;
  for (const auto &entry : std::array<std::pair<QString, CanvasAction>, 5>{{
      {QStringLiteral("Rotate Image 90° Clockwise"), CanvasAction::RotateClockwise},
      {QStringLiteral("Rotate Image 90° Counter-clockwise"), CanvasAction::RotateCounterClockwise},
      {QStringLiteral("Rotate Image 180°"), CanvasAction::Rotate180},
      {QStringLiteral("Flip Image Horizontally"), CanvasAction::FlipHorizontal},
      {QStringLiteral("Flip Image Vertically"), CanvasAction::FlipVertical}}}) {
    QAction *action = imageMenu->addAction(entry.first);
    action->setObjectName(QStringLiteral("canvasAction%1").arg(int(entry.second)));
    canvasActions.insert(entry.second, action);
    connect(action, &QAction::triggered, this, [this, command = entry.second] {
      CanvasOperation operation; operation.action = command; applyCanvas(operation);
    });
  }
  imageMenu->addSeparator();
  QAction *flatten = imageMenu->addAction(QStringLiteral("Flatten"));
  flatten->setObjectName(QStringLiteral("flattenImage"));
  connect(flatten, &QAction::triggered, this, [this] { CanvasOperation op; op.action = CanvasAction::Flatten; applyCanvas(op); });
  flatten->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
  m_clearAnnotations = imageMenu->addAction(QStringLiteral("Clear Annotations"));
  m_clearAnnotations->setObjectName(QStringLiteral("clearAnnotations"));
  m_clearAnnotations->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
  connect(m_clearAnnotations, &QAction::triggered, this, [this] { CanvasOperation op; op.action = CanvasAction::ClearAnnotations; applyCanvas(op); });
  QAction *clear = imageMenu->addAction(QStringLiteral("Clear Image and Annotations…"));
  clear->setObjectName(QStringLiteral("clearImage"));
  connect(clear, &QAction::triggered, this, &EditorWindow::clearImage);
  QAction *compare = imageMenu->addAction(QStringLiteral("Compare Images…"));
  compare->setObjectName(QStringLiteral("compareImages"));
  connect(compare, &QAction::triggered, this, &EditorWindow::compareImages);

  auto *bar = addToolBar(QStringLiteral("Edit"));
  bar->setObjectName(QStringLiteral("editorToolbar"));
  bar->setMovable(false);
  QAction *saveAction = bar->addAction(QStringLiteral("Save"));
  saveAction->setObjectName(QStringLiteral("saveImage"));
  saveAction->setShortcut(QKeySequence::Save);
  QAction *saveAs = bar->addAction(QStringLiteral("Save As…"));
  saveAs->setObjectName(QStringLiteral("saveAsImage"));
  saveAs->setShortcut(QKeySequence::SaveAs);
  bar->addSeparator();
  m_undo = bar->addAction(QStringLiteral("Undo"));
  m_undo->setObjectName(QStringLiteral("undo"));
  m_undo->setShortcut(QKeySequence::Undo);
  m_redo = bar->addAction(QStringLiteral("Redo"));
  m_redo->setObjectName(QStringLiteral("redo"));
  m_redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
  m_delete = annotationDeleteAction(*m_canvas, this, [this] { deleteSelected(); }, [this] {
    return !m_outputPending && !m_canvasOperationPending && !m_closePending;
  });
  bar->addAction(m_delete);
  m_delete->setShortcut(QKeySequence(Qt::Key_Delete));
  m_rotate = bar->addAction(QStringLiteral("Rotate selection…"));
  m_rotate->setObjectName(QStringLiteral("rotateSelection"));
  bar->addSeparator();
  QAction *stroke = bar->addAction(QStringLiteral("Stroke colour…"));
  QAction *fill = bar->addAction(QStringLiteral("Fill colour…"));
  QAction *width = bar->addAction(QStringLiteral("Stroke +"));
  QAction *thin = bar->addAction(QStringLiteral("Stroke −"));
  stroke->setObjectName(QStringLiteral("strokeColour")); fill->setObjectName(QStringLiteral("fillColour"));
  width->setObjectName(QStringLiteral("increaseStroke")); thin->setObjectName(QStringLiteral("decreaseStroke"));

  connect(saveAction, &QAction::triggered, this, [this] { save(false); });
  connect(saveAs, &QAction::triggered, this, [this] { save(true); });
  connect(m_undo, &QAction::triggered, this, [this] { restoreHistory(false); });
  connect(m_redo, &QAction::triggered, this, [this] { restoreHistory(true); });
  connect(m_rotate, &QAction::triggered, this, [this] { rotateSelected(); });
  connect(stroke, &QAction::triggered, this, [this] { editColor(EditorSession::ColorTarget::Stroke); });
  connect(fill, &QAction::triggered, this, [this] { editColor(EditorSession::ColorTarget::Fill); });
  for (auto *action : {width, thin}) action->setToolTip("Change selected rectangles in one undo step and save the default for new rectangles in editor windows.");
  connect(width, &QAction::triggered, this, [this] { changeStrokeWidth(true); });
  connect(thin, &QAction::triggered, this, [this] { changeStrokeWidth(false); });
  // Fixed command menus remain available even with an empty custom toolbar.
  fileMenu->addSeparator(); fileMenu->addAction(saveAction); fileMenu->addAction(saveAs);
  m_retrySave = fileMenu->addAction("Retry &annotation save…"); m_retrySave->setObjectName("retryEditorSidecar");
  m_retrySave->setStatusTip("Retry only the incomplete annotation companion. Keep the already saved PNG.");
  addAction(m_retrySave); connect(m_retrySave, &QAction::triggered, this, [this] { retrySave(); });
  m_printAction = fileMenu->addAction("&Print Image…"); m_printAction->setObjectName("printImage");
  m_printAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)); m_printAction->setAutoRepeat(false);
  m_printAction->setStatusTip("Print one snapshot of the source and visible supported annotations on white paper using sRGB. Preview and native printer settings precede submission; the document stays unsaved.");
  connect(m_printAction, &QAction::triggered, this, [this] { printImage(); });
  m_retryPrint = fileMenu->addAction("Retry image &print…"); m_retryPrint->setObjectName("retryImagePrint");
  m_retryPrint->setStatusTip("Retry a known failed print with the unchanged prepared snapshot and fresh native printer confirmation.");
  addAction(m_retryPrint); connect(m_retryPrint, &QAction::triggered, this, [this] { retryPrint(); });
  auto *closeEditor = fileMenu->addAction(QStringLiteral("Close Editor"));
  closeEditor->setObjectName(QStringLiteral("closeEditor")); closeEditor->setShortcut(QKeySequence::Close);
  connect(closeEditor, &QAction::triggered, this, [this] { close(); });
  auto *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
  m_createRectangle = edit->addAction("Create &Rectangle…"); m_createRectangle->setObjectName("createRectangle");
  m_createRectangle->setAutoRepeat(false); m_createRectangle->setStatusTip("Enter numeric bounds and create one rectangle with captured Drawing Defaults. The active tool stays unchanged.");
  connect(m_createRectangle, &QAction::triggered, this, [this] { rectangleDialog(true); });
  m_rectangleProperties = edit->addAction("Rectangle &Properties…"); m_rectangleProperties->setObjectName("rectangleProperties");
  m_rectangleProperties->setAutoRepeat(false); m_rectangleProperties->setStatusTip("Edit the selected rectangle's bounds, stroke, fill, opacity and rotation. Creation defaults stay unchanged.");
  connect(m_rectangleProperties, &QAction::triggered, this, [this] { rectangleDialog(false); });
  m_copyAction = edit->addAction("&Copy Image"); m_copyAction->setObjectName("copyImage");
  m_copyAction->setShortcut(QKeySequence::Copy);
  m_copyAction->setAutoRepeat(false);
  m_copyAction->setStatusTip("Copy visible annotations as sRGB pixels with alpha and PNG. The source and editing data stay in the editor.");
  connect(m_copyAction, &QAction::triggered, this, [this] { copyImage(); });
  m_retryCopy = edit->addAction("Retry image &copy…"); m_retryCopy->setObjectName("retryImageCopy");
  m_retryCopy->setStatusTip("Retry only a known failed clipboard publication of the unchanged rendered snapshot.");
  addAction(m_retryCopy); connect(m_retryCopy, &QAction::triggered, this, [this] { retryCopy(); });
  edit->addSeparator();
  const auto annotationCommand = [&](const QString &id, const QString &name, const QString &label, const QKeySequence &key) {
    auto *action = edit->addAction(label); action->setObjectName(name); action->setShortcut(key); action->setAutoRepeat(false);
    action->setStatusTip(id == "paste" ? "Paste supported annotations with fresh identities and an offset; source pixels stay unchanged."
        : id == "duplicate-selection" ? "Duplicate supported visible, unlocked annotations as one undoable operation; source pixels and embedded assets stay unchanged."
        : "Copy selected supported annotations and their compatible fields. Source pixels, document paths and embedded assets are excluded; asset-bearing copies are refused.");
    m_annotationActions.insert(id, action); connect(action, &QAction::triggered, this, [this, id] { transferAnnotations(id); });
  };
  annotationCommand("cut-annotation", "cutAnnotations", "Cu&t Annotations", QKeySequence::Cut);
  annotationCommand("copy-annotation", "copyAnnotations", "Copy &Annotations", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
  annotationCommand("paste", "pasteAnnotations", "&Paste Annotations", QKeySequence::Paste);
  annotationCommand("duplicate-selection", "duplicateAnnotations", "&Duplicate Annotations", QKeySequence(Qt::CTRL | Qt::Key_D));
  edit->addSeparator();
  for (auto *action : {m_undo, m_redo, m_delete, m_rotate, stroke, fill, width, thin}) edit->addAction(action);
  edit->addSeparator();
  m_orderActions = annotationOrderActions(*m_canvas, this, [this](EditorSession::Order order) { reorderSelected(order); }, [this] {
    return !m_outputPending && !m_canvasOperationPending && !m_closePending;
  });
  for (const auto *id : {"bring-forward", "send-backward", "bring-to-front", "send-to-back"}) edit->addAction(m_orderActions.value(id));
  auto *view = menuBar()->addMenu(QStringLiteral("&View"));
  QAction *zoomIn = view->addAction(QStringLiteral("Zoom In"));
  zoomIn->setObjectName(QStringLiteral("zoomIn"));
  zoomIn->setShortcuts(QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)});
  QAction *zoomOut = view->addAction(QStringLiteral("Zoom Out"));
  zoomOut->setObjectName(QStringLiteral("zoomOut"));
  zoomOut->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
  QAction *resetZoom = view->addAction(QStringLiteral("100%"));
  resetZoom->setObjectName(QStringLiteral("resetZoom"));
  resetZoom->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
  QAction *fit = view->addAction(QStringLiteral("Fit"));
  fit->setObjectName(QStringLiteral("fitImage"));
  fit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_9));
  connect(zoomIn, &QAction::triggered, m_canvas, &EditorCanvas::zoomIn);
  connect(zoomOut, &QAction::triggered, m_canvas, &EditorCanvas::zoomOut);
  connect(resetZoom, &QAction::triggered, m_canvas, &EditorCanvas::resetZoom);
  connect(fit, &QAction::triggered, m_canvas, &EditorCanvas::zoomToFit);
  connect(m_canvas, &EditorCanvas::viewportChanged, this, &EditorWindow::refresh);
  connect(m_canvas, &EditorCanvas::changed, this, &EditorWindow::refresh);
  connect(m_canvas, &EditorCanvas::toolChanged, this, &EditorWindow::refresh);
  auto *toggleToolbar = bar->toggleViewAction(); toggleToolbar->setText(QStringLiteral("Show Toolbar"));
  toggleToolbar->setObjectName(QStringLiteral("toggleToolbars")); toggleToolbar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
  view->addSeparator(); view->addAction(toggleToolbar);
  auto *objects = view->addAction("Select &Annotations…"); objects->setObjectName("focusAnnotationObjects"); objects->setAutoRepeat(false);
  objects->setStatusTip("Focus the object list for keyboard selection. Enter returns to the canvas; Escape clears selection.");
  connect(objects, &QAction::triggered, this, [this] {
    if (m_canvasOperationPending || m_closePending || m_outputPending || m_canvas->interactionActive()) return;
    const QPointer<EditorWindow> owner(this);
    m_annotationsDock->show(); if (owner) m_annotationObjects->focusObjects();
  });
  m_toolbarActions = {
    {"save", saveAction}, {"save-as", saveAs}, {"undo", m_undo}, {"redo", m_redo},
    {"delete-selection", m_delete}, {"rotate-selection", m_rotate},
    {"stroke-colour", stroke}, {"fill-colour", fill}, {"increase-stroke", width}, {"decrease-stroke", thin},
    {"zoom-in", zoomIn}, {"zoom-out", zoomOut}, {"reset-zoom", resetZoom}, {"fit-image", fit},
    {"new-image", create}, {"open-image", open}, {"crop-image", crop}, {"crop-tool", cropTool},
    {"auto-crop", autoCrop}, {"resize-image", resizePixels}, {"resize-canvas", resizeCanvas},
    {"rotate-image", customRotation},
    {"rotate-clockwise", canvasActions.value(CanvasAction::RotateClockwise)},
    {"rotate-counter-clockwise", canvasActions.value(CanvasAction::RotateCounterClockwise)},
    {"rotate-180", canvasActions.value(CanvasAction::Rotate180)},
    {"flip-horizontal", canvasActions.value(CanvasAction::FlipHorizontal)},
    {"flip-vertical", canvasActions.value(CanvasAction::FlipVertical)},
    {"flatten-image", flatten}, {"clear-annotations", m_clearAnnotations}, {"clear-image", clear},
    {"compare-images", compare}, {"drawing-defaults", drawingDefaults}, {"appearance-preferences", appearance},
    {"remember-window-state", rememberWindow}, {"customize-toolbar", customizeToolbar},
    {"toggle-toolbars", toggleToolbar}, {"close-editor", closeEditor}, {"copy-rendered-image", m_copyAction}, {"print", m_printAction}
  };
  for (auto it = m_annotationActions.cbegin(); it != m_annotationActions.cend(); ++it) m_toolbarActions.insert(it.key(), it.value());
  for (auto it = tools.cbegin(); it != tools.cend(); ++it) m_toolbarActions.insert(it.key(), it.value());
  for (auto it = m_orderActions.cbegin(); it != m_orderActions.cend(); ++it) m_toolbarActions.insert(it.key(), it.value());
  m_toolbarActions.insert("rectangle-properties", m_rectangleProperties);
  m_toolbarActions.insert("create-rectangle", m_createRectangle);
  m_toolbarActions.insert("focus-annotations", objects);
  for (auto *action : m_toolbarActions) { action->setParent(this); addAction(action); }
  m_toolbarController = new NativeEditorToolbar(*this, *bar, m_toolbarActions, [this] {
    return m_canvas && !m_canvasOperationPending && !m_closePending && !m_outputPending && !m_canvas->interactionActive();
  });

  m_windowState = new NativeEditorWindowState(*this, {std::min(m_image.width() + 40, 1400), std::min(m_image.height() + 120, 900)});
  new NativeEditorAppearance(*this);
  refresh();
  m_canvas->setFocus();
}

bool EditorWindow::rectangleDialog(bool creation) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || (!creation && !m_canvas->canEditRectangle())) return false;
  const auto snapshot = creation ? RectangleAnnotation{} : *m_session->selectedRectangle();
  const auto style = m_session->toolStyle(); const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const QSize imageSize = m_image.size(); const QPointer<EditorWindow> owner(this);
  return withOutputBusy([owner, snapshot, state, generation, style, selection, imageSize, creation] {
    if (!owner) return false;
    const auto properties = creation ? chooseRectangleCreation(style, imageSize, owner) : chooseRectangleProperties(snapshot, owner);
    if (!owner || !properties) return false;
    if (generation != owner->m_canvas->documentGeneration()) {
      owner->m_annotationMessage = annotationEditMessage("annotation-edit-stale"); owner->m_annotationMessageState = owner->m_session->stateId();
      owner->refresh(); return false;
    }
    const bool changed = creation ? bool(owner->m_session->createRectangle(*properties, state, selection))
        : owner->m_session->editRectangleProperties(snapshot.id, *properties, state);
    owner->m_annotationMessage = owner->m_session->editDiagnostic().isEmpty() ? QString() : annotationEditMessage(owner->m_session->editDiagnostic());
    owner->m_annotationMessageState = owner->m_session->stateId();
    owner->m_canvas->viewport()->update(); owner->refresh(); return changed;
  });
}

bool EditorWindow::rotateSelected() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_session->canRotateSelection()) return false;
  AnnotationDocument document; QSet<QUuid> selection;
  try { document = m_session->document(); selection = m_session->selection(); }
  catch (const std::bad_alloc &) {
    m_annotationMessage = annotationEditMessage("annotation-edit-failed"); m_annotationMessageState = m_session->stateId(); refresh(); return false;
  }
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const QPointer<EditorWindow> owner(this);
  return withOutputBusy([owner, document, selection, state, generation] {
    const auto choice = chooseSelectionRotation(document, selection, owner); if (!owner) return false;
    bool changed = false; QString diagnostic = choice.diagnostic;
    if (choice.degrees) {
      if (generation != owner->m_canvas->documentGeneration()) diagnostic = "annotation-edit-stale";
      else { changed = owner->m_session->rotateSelection(*choice.degrees, state, selection); diagnostic = owner->m_session->editDiagnostic(); }
    }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId(); owner->refresh(); return changed;
  });
}

void EditorWindow::applyCreationDefaults() {
  const auto defaults = drawingStyleFromPreferences(nativeEditorPreferences());
  auto style = m_session->toolStyle();
  style.strokeColor = defaults.strokeColor; style.fillColor = defaults.fillColor; style.strokeWidth = defaults.strokeWidth;
  m_session->setCreationStyle(style);
}

bool EditorWindow::editColor(EditorSession::ColorTarget target) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_session->canEditColor()) return false;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(), revision = nativeEditorPreferenceRevision();
  const auto selection = m_session->selection(); const auto defaults = m_session->toolStyle(); const QPointer<EditorWindow> owner(this);
  const auto initial = target == EditorSession::ColorTarget::Stroke ? defaults.strokeColor : defaults.fillColor;
  const QString scope = selection.isEmpty() ? "This colour changes only the default for new rectangles in editor windows. Current objects and history stay unchanged."
      : "Selected rectangles change in one undo step. This colour also becomes the default for new rectangles in editor windows. Unsupported objects stay unchanged.";
  return withOutputBusy([owner, target, state, generation, revision, selection, defaults, initial, scope] {
    const auto choice = chooseAnnotationColor(target, initial, scope, owner); if (!owner) return false;
    QString diagnostic = choice.diagnostic; bool changed = false;
    if (choice.color) {
      if (generation != owner->m_canvas->documentGeneration() || revision != nativeEditorPreferenceRevision()) diagnostic = "annotation-edit-stale";
      else {
        const auto edit = owner->m_session->editColor(target, *choice.color, state, selection, defaults); diagnostic = edit.diagnostic; changed = edit.documentChanged;
        if (edit.accepted) {
          owner->persistDrawingDefaults(revision);
          if (!owner) return false;
        }
      }
    }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId(); owner->refresh(); return changed;
  });
}

bool EditorWindow::deleteSelected() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_session->canDeleteSelection()) return false;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const QPointer<EditorWindow> owner(this);
  return withOutputBusy([owner, state, generation, selection] {
    if (!owner) return false;
    bool changed = false; QString diagnostic;
    if (generation != owner->m_canvas->documentGeneration()) diagnostic = "annotation-edit-stale";
    else { changed = owner->m_session->deleteSelection(state, selection); diagnostic = owner->m_session->editDiagnostic(); }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId(); owner->refresh(); return changed;
  });
}
bool EditorWindow::reorderSelected(EditorSession::Order order) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_session->canOrderSelection()) return false;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const QPointer<EditorWindow> owner(this);
  return withOutputBusy([owner, state, generation, selection, order] {
    if (!owner) return false;
    bool changed = false; QString diagnostic;
    if (generation != owner->m_canvas->documentGeneration()) diagnostic = "annotation-edit-stale";
    else { changed = owner->m_session->reorderSelection(order, state, selection); diagnostic = owner->m_session->editDiagnostic(); }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId(); owner->refresh(); return changed;
  });
}
bool EditorWindow::changeStrokeWidth(bool increase) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_session->canEditStyle()) return false;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(), revision = nativeEditorPreferenceRevision();
  const auto selection = m_session->selection(); const auto defaults = m_session->toolStyle(); const QPointer<EditorWindow> owner(this);
  const double width = increase ? std::min(10000.0, defaults.strokeWidth + 1) : std::max(1.0, defaults.strokeWidth - 1);
  return withOutputBusy([owner, state, generation, revision, selection, defaults, width] {
    if (!owner) return false;
    bool changed = false; QString diagnostic;
    if (generation != owner->m_canvas->documentGeneration() || revision != nativeEditorPreferenceRevision()) diagnostic = "annotation-edit-stale";
    else {
      const auto edit = owner->m_session->editStrokeWidth(width, state, selection, defaults); changed = edit.documentChanged; diagnostic = edit.diagnostic;
      if (edit.accepted) { owner->persistDrawingDefaults(revision); if (!owner) return false; }
    }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId(); owner->refresh(); return changed;
  });
}
void EditorWindow::persistDrawingDefaults(quint64 expectedRevision) {
  const auto style = m_session->toolStyle(); const QPointer<EditorWindow> owner(this);
  const auto written = applyDrawingDefaults({{QStringLiteral("BorderColorHex"), formatColor(style.strokeColor)},
      {QStringLiteral("FillColorHex"), formatColor(style.fillColor)}, {QStringLiteral("Thickness"), style.strokeWidth}},
      expectedRevision);
  if (owner) owner->m_drawingPreferenceDiagnostic = written.diagnostic;
}

void EditorWindow::refresh() {
  const bool ready = !m_outputPending && !m_canvas->interactionActive();
  const QPointer<EditorWindow> owner(this);
  for (auto *action : m_orderActions) {
    action->setEnabled(ready && !m_canvasOperationPending && !m_closePending && m_canvas->isEnabled() && m_session->canOrderSelection()); if (!owner) return;
  }
  for (const auto *id : {"save", "save-as"}) {
    m_toolbarActions.value(id)->setEnabled(ready); if (!owner) return;
  }
  for (const auto *id : {"increase-stroke", "decrease-stroke"}) { m_toolbarActions.value(id)->setEnabled(ready && m_session->canEditStyle()); if (!owner) return; }
  for (const auto *id : {"stroke-colour", "fill-colour"}) { m_toolbarActions.value(id)->setEnabled(ready && m_session->canEditColor()); if (!owner) return; }
  m_createRectangle->setEnabled(!m_outputPending && !m_canvasOperationPending && !m_closePending && !m_canvas->interactionActive()); if (!owner) return;
  m_rectangleProperties->setEnabled(!m_outputPending && !m_canvasOperationPending && !m_closePending && !m_canvas->interactionActive() && m_canvas->canEditRectangle()); if (!owner) return;
  m_rectangleProperties->setToolTip(m_canvas->canEditRectangle() ? m_rectangleProperties->statusTip() : "Select one visible, unlocked rectangle to edit its properties.");
  if (!owner) return;
  if (m_annotationMessageState != m_session->stateId()) { m_annotationMessage.clear(); m_annotationMessageState.reset(); }
  for (auto it = m_annotationActions.cbegin(); it != m_annotationActions.cend(); ++it) {
    const bool available = it.key() == "duplicate-selection" || (it.key() == "paste" ? bool(m_annotationClipboard.read) : bool(m_annotationClipboard.publish));
    it.value()->setEnabled(ready && available && (it.key() == "paste" || !m_session->selection().isEmpty())); if (!owner) return;
    it.value()->setToolTip(available ? it.value()->statusTip() : "Annotation clipboard service is unavailable.");
    if (!owner) return;
  }
  if (m_printMessageState != m_session->stateId()) { m_printMessage.clear(); m_printMessageState.reset(); }
  if (m_copyMessageState != m_session->stateId()) { m_copyMessage.clear(); m_copyMessageState.reset(); }
  if (m_saveMessageState != m_session->stateId()) { m_saveMessage.clear(); m_saveMessageState.reset(); }
  m_annotationObjects->refresh();
  if (!owner) return;
  m_toolbarActions.value("focus-annotations")->setEnabled(!m_outputPending && !m_canvasOperationPending && !m_closePending && !m_canvas->interactionActive()); if (!owner) return;
  if (m_sourceBytes != m_session->document().sourceImagePng) {
    const QImage next = QImage::fromData(m_session->document().sourceImagePng, "PNG").convertToFormat(QImage::Format_ARGB32);
    if (!next.isNull()) {
      m_sourceBytes = m_session->document().sourceImagePng;
      m_image = next;
      m_canvas->sourceChanged();
      if (!owner) return;
    }
  }
  m_undo->setEnabled(ready && m_session->undoCount() > 0); if (!owner) return;
  m_redo->setEnabled(ready && m_session->redoCount() > 0); if (!owner) return;
  m_delete->setEnabled(ready && m_session->canDeleteSelection()); if (!owner) return;
  m_rotate->setEnabled(ready && m_session->canRotateSelection()); if (!owner) return;
  m_delete->setToolTip(m_session->canDeleteSelection() ? m_delete->statusTip() : m_session->selection().isEmpty() ? "Select annotations to delete." : "Hidden or locked selections are protected from editing.");
  if (!owner) return;
  m_rotate->setToolTip(m_session->canRotateSelection() ? "Rotate selected rectangles about their own bounds centres" : "Select visible, unlocked rectangles. Hidden, locked and unsupported objects cannot rotate.");
  if (!owner) return;
  m_clearAnnotations->setEnabled(ready && !m_session->document().annotations.empty()); if (!owner) return;
  m_retrySave->setEnabled(ready && m_partialSave && m_partialSaveState == m_session->stateId()); if (!owner) return;
  m_retrySave->setToolTip(m_partialSave && m_partialSaveState != m_session->stateId()
      ? "The document changed since this PNG was saved. Undo to that state before retrying its annotations, or save a new snapshot."
      : m_retrySave->statusTip());
  if (!owner) return;
  m_copyAction->setEnabled(ready && bool(m_copyPorts.publish)); if (!owner) return;
  m_copyAction->setToolTip(m_copyPorts.publish ? m_copyAction->statusTip() : "Copy Image is unavailable: no clipboard service was provided.");
  if (!owner) return;
  m_retryCopy->setEnabled(ready && m_copyPorts.publish && m_failedCopy && m_failedCopyState == m_session->stateId()); if (!owner) return;
  m_retryCopy->setToolTip(m_failedCopy && m_failedCopyState != m_session->stateId()
      ? "The document changed. Undo to the failed snapshot before retrying it, or use Copy Image for a new snapshot." : m_retryCopy->statusTip());
  if (!owner) return;
  const bool printAvailable = m_printPorts.configure && m_printPorts.submit;
  m_printAction->setEnabled(ready && printAvailable); if (!owner) return;
  m_printAction->setToolTip(printAvailable ? m_printAction->statusTip() : "Print Image is unavailable: no print service was provided.");
  if (!owner) return;
  m_retryPrint->setEnabled(ready && printAvailable && m_failedPrint && m_failedPrintState == m_session->stateId()); if (!owner) return;
  m_retryPrint->setToolTip(m_failedPrint && m_failedPrintState != m_session->stateId()
      ? "The document changed. Undo to the failed snapshot before retrying it, or use Print Image for a new snapshot." : m_retryPrint->statusTip());
  if (!owner) return;
  setWindowTitle(QStringLiteral("%1%2 — XerahS").arg(m_path.isEmpty() ? QStringLiteral("Untitled image") : QFileInfo(m_path).fileName(),
                                                    m_session->dirty() ? QStringLiteral(" •") : QString()));
  if (!owner) return;
  updatePointerStatus();
  QString status = QStringLiteral("%1 × %2 · %3 annotation(s) · %4 selected · %5%")
                               .arg(m_image.width()).arg(m_image.height())
                               .arg(m_session->document().annotations.size()).arg(m_session->selection().size())
                               .arg(m_canvas->viewState().zoom() * 100, 0, 'f', 1);
  if (!m_preferenceDiagnostic.isEmpty()) status += QStringLiteral(" · Recent images could not be saved: %1")
      .arg(editorPreferenceDiagnosticMessage(m_preferenceDiagnostic));
  if (!m_drawingPreferenceDiagnostic.isEmpty()) status += QStringLiteral(" · Drawing defaults could not be saved: %1")
      .arg(editorPreferenceDiagnosticMessage(m_drawingPreferenceDiagnostic));
  if (!m_saveMessage.isEmpty()) status += " · " + m_saveMessage;
  if (!m_copyMessage.isEmpty()) status += " · " + m_copyMessage;
  if (!m_printMessage.isEmpty()) status += " · " + m_printMessage;
  if (!m_annotationMessage.isEmpty()) status += " · " + m_annotationMessage;
  auto *bar = statusBar(); const bool notify = bar->currentMessage() != status;
  // QStatusBar emits during its internal hide/show traversal. Complete that
  // traversal before notifying observers that can destroy this window/bar.
  { const QSignalBlocker blocker(bar); bar->showMessage(status); }
  if (notify) emit bar->messageChanged(status);
  if (!owner) return;
  m_canvas->viewport()->update();
}

void EditorWindow::updatePointerStatus() {
  if (!m_pointerStatus) return;
  const QSize size = m_image.size();
  QString text;
  // The colour is the source pixel of the image the editor already holds; the
  // annotated document is never rendered for a pointer move.
  if (m_pointerPixel && QRect(QPoint(0, 0), size).contains(*m_pointerPixel))
    text = QStringLiteral("X: %1  Y: %2   %3   ")
               .arg(m_pointerPixel->x()).arg(m_pointerPixel->y())
               .arg(xerahs::tools::formatColor(m_image.pixelColor(*m_pointerPixel), xerahs::tools::ColorFormat::Hex));
  m_pointerStatus->setText(text + QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
}

void EditorWindow::newImage() {
  if (m_canvasOperationPending || m_outputPending || m_closePending || m_canvas->interactionActive()) return;
  const QPointer<EditorWindow> guard(this); const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection(); const auto hooks = m_newImageHooks;
  m_canvasOperationPending = true;
  const auto pending = qScopeGuard([guard] { if (guard) { guard->m_canvasOperationPending = false; guard->refresh(); } });
  std::optional<PreparedNewImage> prepared;
  withOutputBusy([guard, hooks, &prepared] {
    if (!guard) return false;
    QPointer<NewImageDialog> dialog(new NewImageDialog(guard, hooks)); const auto answer = dialog->exec();
    if (dialog && answer == QDialog::Accepted) prepared = dialog->takeImage();
    disposeCanvasDialog(dialog.data()); return bool(prepared);
  });
  if (!guard || !prepared) return;
  const auto current = [&] { return replacementCurrent(state, generation, selection); };
  if (!current()) return;
  std::unique_ptr<EditorSession> next;
  try { next = std::make_unique<EditorSession>(std::move(*prepared->document), EditorSession::InitialPersistence::Unsaved); }
  catch (const std::bad_alloc &) { editorWarning(this, "Cannot create image", "There is not enough memory for a new session. The current document is unchanged."); return; }
  if (confirmReplacement(QStringLiteral("the new image")) == QMessageBox::Cancel || !guard) return;
  if (!current()) return;
  withOutputBusy([guard, &current, &next, &prepared] {
    if (!guard || !current()) return false;
    guard->adoptSession(std::move(next), {}, std::move(prepared->source), prepared->displaySource); return bool(guard);
  });
}

int EditorWindow::confirmReplacement(const QString &replacement) {
  const QPointer<EditorWindow> guard(this); if (!m_session->dirty()) return QMessageBox::Discard;
  const auto name = m_path.isEmpty() ? QStringLiteral("Untitled image") : QFileInfo(m_path).fileName(); int choice = QMessageBox::Cancel;
  withOutputBusy([guard, replacement, name, &choice] {
    if (!guard) return false;
    QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Replace current image",
        QStringLiteral("Save changes to %1 before replacing it with %2?").arg(name, replacement), QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, guard));
    box->setObjectName("replaceImageConfirmation"); box->setTextFormat(Qt::PlainText); box->setDefaultButton(QMessageBox::Cancel);
    choice = box->exec(); disposeCanvasDialog(box.data()); return false;
  });
  if (!guard) return QMessageBox::Cancel;
  if (choice == QMessageBox::Save && !save(false)) return QMessageBox::Cancel;
  if (!guard) return QMessageBox::Cancel;
  return choice == QMessageBox::Save || choice == QMessageBox::Discard ? choice : QMessageBox::Cancel;
}

bool EditorWindow::replacementCurrent(quint64 state, quint64 generation, const QSet<QUuid> &selection) {
  if (state == m_session->stateId() && generation == m_canvas->documentGeneration() && selection == m_session->selection()) return true;
  m_annotationMessage = "The document or selection changed. The image was not replaced. Try again."; m_annotationMessageState = m_session->stateId(); refresh(); return false;
}

void EditorWindow::adoptSession(std::unique_ptr<EditorSession> next, const QString &path, QImage source, const QImage &display) {
  const QPointer<EditorWindow> owner(this);
  // Keep the old session alive until the canvas has cancelled its gesture and
  // switched its session pointer. Retaining the widget preserves action bindings.
  auto previous = std::move(m_session);
  m_session = std::move(next);
  applyCreationDefaults();
  m_image = std::move(source);
  m_sourceBytes = m_session->document().sourceImagePng;
  m_path = path;
  m_partialSave.reset(); m_partialSaveState.reset();
  m_saveMessage.clear(); m_saveMessageState.reset();
  m_failedCopy.reset(); m_failedCopyState.reset(); m_copyMessage.clear(); m_copyMessageState.reset();
  m_failedPrint.reset(); m_failedPrintState.reset(); m_printMessage.clear(); m_printMessageState.reset();
  m_annotationMessage.clear(); m_annotationMessageState.reset(); m_lastAnnotationPaste.clear(); m_annotationPasteOffset = 0;
  m_canvas->resetDocument(*m_session, display);
  if (!owner) return;
  findChild<QAction *>(QStringLiteral("cropTool"))->setChecked(false);
  if (!owner) return;
  refresh(); if (owner) m_canvas->setFocus();
}

bool EditorWindow::openImage(const QString &path) {
  if (m_canvasOperationPending || m_outputPending || m_closePending || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this); const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection(); const auto hooks = m_openHooks; const auto oldPath = m_path;
  m_canvasOperationPending = true;
  const auto pending = qScopeGuard([guard] { if (guard) { guard->m_canvasOperationPending = false; guard->refresh(); } });
  QString target = path; LoadedSource source;
  withOutputBusy([guard, hooks, oldPath, &target, &source] {
    if (!guard) return false;
    if (target.isEmpty()) {
      QPointer<QFileDialog> picker(new QFileDialog(guard, "Open image", oldPath, imageFileFilter())); picker->setObjectName("openImagePicker"); picker->setFileMode(QFileDialog::ExistingFile);
      const auto answer = picker->exec(); if (picker && answer == QDialog::Accepted && !picker->selectedFiles().isEmpty()) target = picker->selectedFiles().first();
      disposeCanvasDialog(picker.data()); if (!guard || target.isEmpty()) return false;
    }
    target = absoluteImagePath(target); source = openSource(target, guard, hooks); return bool(source.document);
  });
  if (!guard || target.isEmpty() || source.diagnostic == u"canvas-cancelled") return false;
  const auto current = [&] { return replacementCurrent(state, generation, selection); };
  if (!current()) return false;
  const auto usable = [&] {
    if (source.diagnostic == u"canvas-cancelled") return false;
    if (source.document) return true;
    withOutputBusy([guard, target, diagnostic = source.diagnostic] { if (guard) editorWarning(guard, "Cannot open image", QStringLiteral("%1\n\n%2").arg(QFileInfo(target).fileName(), describe(diagnostic))); return false; });
    return false;
  };
  if (!usable() || !guard) return false;
  const auto prepareSession = [&]() -> std::unique_ptr<EditorSession> {
    try { return std::make_unique<EditorSession>(*source.document); }
    catch (const std::bad_alloc &) { editorWarning(guard, "Cannot open image", describe(diagnostic::documentTooLarge)); return {}; }
  };
  auto next = prepareSession(); if (!guard || !next) return false;
  const int choice = confirmReplacement(QFileInfo(target).fileName());
  if (!guard || choice == QMessageBox::Cancel || !current()) return false;
  const QString savedIdentity = QFileInfo(m_path).canonicalFilePath();
  if (choice == QMessageBox::Save && !savedIdentity.isEmpty() && savedIdentity == QFileInfo(target).canonicalFilePath()) {
    withOutputBusy([guard, target, hooks, &source] { if (!guard) return false; source = openSource(target, guard, hooks); return bool(source.document); });
    if (!guard || !current() || !usable() || !guard) return false;
    next = prepareSession(); if (!guard || !next) return false;
  }
  const bool adopted = withOutputBusy([guard, &current, &next, &source, target] {
    if (!guard || !current()) return false;
    guard->adoptSession(std::move(next), target, std::move(source.image), source.displayImage); return bool(guard);
  });
  if (!guard || !adopted) return false;
  recordRecentImage(target); return bool(guard);
}

void EditorWindow::restoreHistory(bool redo) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  const auto *target = m_session->historyTarget(redo); const auto targetId = m_session->historyTargetId(redo); if (!target || !targetId) return;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const bool sourceChanged = target->sourceImagePng != m_session->document().sourceImagePng;
  // A borrowed history target can be invalidated by busy notifications. Copy it
  // first; only value-owned preparation crosses the worker boundary.
  AnnotationDocument document;
  try { if (sourceChanged) document = *target; }
  catch (...) { m_annotationMessage = "Canvas history could not be prepared. History was kept."; m_annotationMessageState = state; refresh(); return; }
  const auto hooks = m_historyHooks; const QPointer<EditorWindow> owner(this);
  withOutputBusy([owner, redo, state, generation, selection, targetId, document, sourceChanged, hooks] {
    if (!owner) return false;
    LoadedSource prepared;
    if (sourceChanged) prepared = runEditorHistory(document, owner, hooks);
    if (!owner) return false;
    if (prepared.diagnostic == "canvas-cancelled") return false;
    QString diagnostic;
    bool changed = false;
    if (generation != owner->m_canvas->documentGeneration() || state != owner->m_session->stateId() || selection != owner->m_session->selection() || targetId != owner->m_session->historyTargetId(redo)) diagnostic = "annotation-edit-stale";
    else if (sourceChanged && !prepared.document) diagnostic = prepared.diagnostic.isEmpty() ? "canvas-history-failed" : prepared.diagnostic;
    else {
      changed = owner->m_session->restoreHistory(redo, state, selection, *targetId);
      if (!changed) diagnostic = owner->m_session->editDiagnostic();
      else if (sourceChanged) {
        owner->m_sourceBytes = owner->m_session->document().sourceImagePng;
        owner->m_image = std::move(prepared.image);
        owner->m_canvas->sourceChanged(prepared.displayImage);
        if (!owner) return false;
      }
    }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : diagnostic == "annotation-edit-stale" ? annotationEditMessage(diagnostic)
        : QString("Canvas history could not be restored (%1). History was kept.").arg(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId();
    return changed;
  });
}

void EditorWindow::cropImage() {
  cropImage({0, 0, m_image.width(), m_image.height()});
}
void EditorWindow::cropImage(CropRegion region) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  // Freeze before disabling controls: native notifications may replace the
  // session, change its selection, or delete this window.
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration();
  const auto selection = m_session->selection(); const auto document = m_session->document();
  const auto source = m_image; const auto hooks = m_cropHooks; const QPointer<EditorWindow> owner(this);
  withOutputBusy([owner, state, generation, selection, document, source, region, hooks] {
    if (!owner) return false;
    QPointer<CanvasCropDialog> dialog(new CanvasCropDialog(document, source, region, owner, hooks));
    const int answer = dialog->exec();
    auto prepared = dialog && answer == QDialog::Accepted ? dialog->takeEdit() : std::optional<PreparedCropEdit>{};
    delete dialog.data();
    if (!owner || !prepared) return false;
    QString diagnostic;
    bool changed = false;
    if (generation != owner->m_canvas->documentGeneration() || state != owner->m_session->stateId() || selection != owner->m_session->selection()) diagnostic = "annotation-edit-stale";
    else if (prepared->edit.result.error) diagnostic = *prepared->edit.result.error;
    else if (prepared->edit.result.changed) {
      // Main-editor crops remain valid after resize/rotation, which deliberately
      // invalidate the capture-only lineage proof. The frozen native admission
      // protects the ordinary Core canvas commit without requiring that proof.
      changed = prepared->edit.result.document && owner->m_session->commitCanvas(*prepared->edit.result.document, state, selection);
      if (!changed) diagnostic = owner->m_session->editDiagnostic().isEmpty() ? "document-invalid" : owner->m_session->editDiagnostic();
      else {
        owner->m_sourceBytes = owner->m_session->document().sourceImagePng;
        owner->m_image = std::move(prepared->edit.source);
        owner->m_canvas->sourceChanged(prepared->displaySource);
        if (!owner) return false;
      }
    }
    owner->m_annotationMessage = diagnostic.isEmpty() ? QString() : annotationEditMessage(diagnostic);
    owner->m_annotationMessageState = owner->m_session->stateId();
    return changed;
  });
}

void EditorWindow::autoCropImage() {
  editCanvas([](const QImage &, QWidget *parent) -> std::optional<CanvasOperation> {
  QPointer<QDialog> dialog(new QDialog(parent));
  dialog->setWindowTitle(QStringLiteral("Auto Crop"));
  auto *form = new QFormLayout(dialog.data());
  auto *border = new QComboBox(dialog.data());
  border->setObjectName(QStringLiteral("cropBorder"));
  border->addItems({QStringLiteral("Transparent pixels"), QStringLiteral("Match top-left pixel"), QStringLiteral("Match chosen colour")});
  form->addRow(QStringLiteral("Border rule"), border);
  auto *alpha = new QSpinBox(dialog.data()), *tolerance = new QSpinBox(dialog.data());
  alpha->setObjectName(QStringLiteral("cropAlpha")); tolerance->setObjectName(QStringLiteral("cropTolerance"));
  alpha->setRange(0, 255); tolerance->setRange(0, 255); tolerance->setEnabled(false);
  form->addRow(QStringLiteral("Ignore alpha at or below"), alpha);
  form->addRow(QStringLiteral("RGBA channel tolerance"), tolerance);
  Argb selectedColor = 0xFFFFFFFF;
  auto *color = new QPushButton(QStringLiteral("#FFFFFFFF"), dialog.data()); color->setEnabled(false);
  form->addRow(QStringLiteral("Border colour"), color);
  connect(color, &QPushButton::clicked, dialog.data(), [&] {
    QPointer<QColorDialog> picker(new QColorDialog(QColor::fromRgba(selectedColor), dialog)); picker->setOption(QColorDialog::ShowAlphaChannel); picker->setWindowTitle("Border colour");
    const auto accepted = picker->exec(); const auto chosen = picker && accepted == QDialog::Accepted ? picker->selectedColor() : QColor{};
    disposeCanvasDialog(picker.data()); if (dialog && chosen.isValid()) { selectedColor = chosen.rgba(); color->setText(chosen.name(QColor::HexArgb)); }
  });
  connect(border, &QComboBox::currentIndexChanged, dialog.data(), [&](int index) {
    tolerance->setEnabled(index != 0); color->setEnabled(index == 2);
  });
  auto *annotations = new QCheckBox(QStringLiteral("Include visible annotations when finding bounds"), dialog.data());
  annotations->setObjectName(QStringLiteral("cropAnnotations")); annotations->setChecked(true); form->addRow(annotations);
  auto *rules = new QLabel(QStringLiteral("Trim complete background rows and columns from the four edges. "
      "Colour matching allows the stated difference in every RGBA channel. "
      "An empty image or unchanged bounds leave the document unchanged."), dialog.data());
  rules->setWordWrap(true); form->addRow(rules);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog.data()); form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog.data(), &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog.data(), &QDialog::reject);
  const int answer = dialog->exec();
  if (!dialog || answer != QDialog::Accepted) { disposeCanvasDialog(dialog); return std::nullopt; }
  CanvasOperation operation; operation.action = CanvasAction::AutoCrop;
  operation.autoCrop = {CropBorder(border->currentIndex()), alpha->value(), tolerance->value(), selectedColor, annotations->isChecked()};
  disposeCanvasDialog(dialog); return operation;
  });
}

void EditorWindow::resizeImage(bool canvasOnly) {
  editCanvas([canvasOnly](const QImage &source, QWidget *parent) -> std::optional<CanvasOperation> {
  QPointer<QDialog> dialog(new QDialog(parent));
  dialog->setWindowTitle(canvasOnly ? QStringLiteral("Resize Canvas") : QStringLiteral("Resize Image"));
  auto *form = new QFormLayout(dialog.data());
  auto *width = new QSpinBox(dialog.data()), *height = new QSpinBox(dialog.data());
  width->setObjectName(QStringLiteral("canvasWidth")); height->setObjectName(QStringLiteral("canvasHeight"));
  width->setRange(1, int(kMaxDimension)); height->setRange(1, int(kMaxDimension));
  width->setValue(source.width()); height->setValue(source.height());
  width->setAccessibleName(QStringLiteral("Width in pixels")); height->setAccessibleName(QStringLiteral("Height in pixels"));
  form->addRow(QStringLiteral("Width (pixels)"), width); form->addRow(QStringLiteral("Height (pixels)"), height);
  auto *choice = new QComboBox(dialog.data());
  auto *lock = new QCheckBox(QStringLiteral("Lock aspect ratio"), dialog.data()); lock->setChecked(true);
  auto *color = new QPushButton(QStringLiteral("Transparent"), dialog.data());
  Argb fill = 0;
  if (canvasOnly) {
    choice->addItems({QStringLiteral("Top left"), QStringLiteral("Top"), QStringLiteral("Top right"),
                     QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right"),
                     QStringLiteral("Bottom left"), QStringLiteral("Bottom"), QStringLiteral("Bottom right")});
    choice->setCurrentIndex(4);
    form->addRow(QStringLiteral("Anchor"), choice); form->addRow(QStringLiteral("Added area"), color);
    connect(color, &QPushButton::clicked, dialog.data(), [&] {
      QPointer<QColorDialog> picker(new QColorDialog(QColor::fromRgba(fill), dialog)); picker->setOption(QColorDialog::ShowAlphaChannel); picker->setWindowTitle("Added area colour");
      const auto accepted = picker->exec(); const auto selected = picker && accepted == QDialog::Accepted ? picker->selectedColor() : QColor{};
      disposeCanvasDialog(picker.data()); if (dialog && selected.isValid()) { fill = selected.rgba(); color->setText(selected.name(QColor::HexArgb)); }
    });
  } else {
    choice->addItems({QStringLiteral("Nearest neighbour"), QStringLiteral("Bilinear")});
    form->addRow(lock); form->addRow(QStringLiteral("Interpolation"), choice);
    connect(width, &QSpinBox::valueChanged, dialog.data(), [&](int value) {
      if (lock->isChecked()) { const QSignalBlocker block(height); height->setValue(int(std::floor(double(value) * source.height() / source.width() + .5))); }
    });
    connect(height, &QSpinBox::valueChanged, dialog.data(), [&](int value) {
      if (lock->isChecked()) { const QSignalBlocker block(width); width->setValue(int(std::floor(double(value) * source.width() / source.height() + .5))); }
    });
    connect(lock, &QCheckBox::toggled, dialog.data(), [&](bool active) {
      if (active) height->setValue(int(std::floor(double(width->value()) * source.height() / source.width() + .5)));
    });
  }
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog.data());
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, dialog.data(), &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, dialog.data(), &QDialog::reject);
  const int answer = dialog->exec();
  if (!dialog || answer != QDialog::Accepted) { disposeCanvasDialog(dialog); return std::nullopt; }
  CanvasOperation operation;
  operation.action = canvasOnly ? CanvasAction::ResizeCanvas : CanvasAction::ResizeImage;
  operation.width = width->value(); operation.height = height->value();
  operation.anchor = CanvasAnchor(choice->currentIndex()); operation.fill = fill;
  operation.interpolation = Interpolation(choice->currentIndex()); operation.lockAspect = lock->isChecked();
  disposeCanvasDialog(dialog); return operation;
  });
}

void EditorWindow::rotateImage() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const auto document = m_session->document(); const auto source = m_image; const auto hooks = m_canvasHooks; const QPointer<EditorWindow> owner(this);
  withOutputBusy([owner, state, generation, selection, document, source, hooks] {
    if (!owner) return false;
    QPointer<CanvasRotationDialog> dialog(new CanvasRotationDialog(document, source, owner, hooks));
    const auto answer = dialog->exec(); auto edit = dialog && answer == QDialog::Accepted ? dialog->takeEdit() : std::optional<PreparedCanvasEdit>{};
    disposeCanvasDialog(dialog.data()); if (!owner || !edit) return false;
    return owner->commitCanvas(std::move(*edit), state, selection, generation);
  });
}

void EditorWindow::clearImage() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const bool dirty = m_session->dirty(); const QPointer<EditorWindow> owner(this); int answer = QMessageBox::Cancel;
  withOutputBusy([owner, dirty, &answer] {
    if (!owner) return false;
    QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, "Clear Image and Annotations",
        "Remove all pixels and annotation objects from this canvas, keeping its dimensions and colour profile? You can restore the current document with Undo.",
        dirty ? QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel : QMessageBox::Ok | QMessageBox::Cancel, owner));
    box->setObjectName("clearImageConfirmation"); box->setTextFormat(Qt::PlainText); box->setDefaultButton(QMessageBox::Cancel);
    if (dirty) {
      box->setInformativeText("Save the current changes before clearing?"); box->button(QMessageBox::Save)->setText("Save, then Clear"); box->button(QMessageBox::Discard)->setText("Clear without Saving");
    } else box->button(QMessageBox::Ok)->setText("Clear");
    answer = box->exec(); disposeCanvasDialog(box.data()); return false;
  });
  if (!owner || (answer != QMessageBox::Save && answer != QMessageBox::Discard && answer != QMessageBox::Ok)) return;
  const auto current = [&] { return state == m_session->stateId() && generation == m_canvas->documentGeneration() && selection == m_session->selection(); };
  const auto stale = [&] { m_annotationMessage = annotationEditMessage("annotation-edit-stale"); m_annotationMessageState = m_session->stateId(); refresh(); };
  if (!current()) { stale(); return; }
  if (answer == QMessageBox::Save && !save(false)) return;
  if (!owner) return;
  if (!current()) { stale(); return; }
  CanvasOperation operation; operation.action = CanvasAction::ClearImageAndAnnotations; applyCanvas(operation);
}

void EditorWindow::applyCanvas(CanvasOperation operation) {
  editCanvas([operation](const QImage &, QWidget *) { return std::optional<CanvasOperation>(operation); });
}
void EditorWindow::editCanvas(const std::function<std::optional<CanvasOperation>(const QImage &, QWidget *)> &choose) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  const auto state = m_session->stateId(), generation = m_canvas->documentGeneration(); const auto selection = m_session->selection();
  const auto document = m_session->document(); const auto source = m_image; const auto hooks = m_canvasHooks; const QPointer<EditorWindow> owner(this);
  withOutputBusy([owner, state, generation, selection, document, source, hooks, choose] {
    if (!owner) return false;
    const auto operation = choose(source, owner); if (!owner || !operation) return false;
    auto prepared = runEditorCanvas(document, source, *operation, owner, hooks);
    if (!owner || prepared.result.error == "canvas-cancelled") return false;
    return owner->commitCanvas(std::move(prepared), state, selection, generation);
  });
}

void EditorWindow::compareImages() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return;
  const auto document = m_session->document(); const auto source = m_image; const auto hooks = m_comparisonHooks;
  const QPointer<EditorWindow> owner(this);
  withOutputBusy([owner, document, source, hooks] {
    if (!owner) return false;
    QPointer<ImageComparisonDialog> dialog(new ImageComparisonDialog(document, source, owner, hooks));
    dialog->exec(); disposeCanvasDialog(dialog.data());
    return false;
  });
}

bool EditorWindow::commitCanvas(PreparedCanvasEdit prepared, quint64 state, const QSet<QUuid> &selection, quint64 generation) {
  const QPointer<EditorWindow> owner(this);
  if (generation != m_canvas->documentGeneration() || state != m_session->stateId() || selection != m_session->selection()) {
    m_annotationMessage = annotationEditMessage("annotation-edit-stale"); m_annotationMessageState = m_session->stateId(); return false;
  }
  if (!prepared.result.error && prepared.result.changed && (!prepared.result.document ||
      !m_session->commitCanvas(*prepared.result.document, state, selection))) prepared.result.error = m_session->editDiagnostic().isEmpty() ? "document-invalid" : m_session->editDiagnostic();
  if (prepared.result.error) {
    QString message = *prepared.result.error;
    if (message == u"canvas-annotation-shear-unsupported") message = QStringLiteral("Keep the aspect ratio locked to preserve rotated or stroked annotations.");
    else if (message == u"canvas-annotation-render-unsupported") message = QStringLiteral("This document contains visible annotation types this editor cannot flatten yet. The document has been kept unchanged.");
    else if (message == u"canvas-annotation-transform-unsupported") message = QStringLiteral("This document contains annotation types this editor cannot transform yet.");
    else if (message == diagnostic::documentTooLarge) message = QStringLiteral("The requested image exceeds the editor's dimensions or memory limits.");
    else if (message == diagnostic::documentInvalid) message = QStringLiteral("The image or annotation data is invalid.");
    else if (message == u"canvas-encoding-failed") message = QStringLiteral("The edited source could not be encoded.");
    else if (message == u"canvas-preparation-failed") message = QStringLiteral("The image could not be prepared. The document has been kept unchanged. Try again.");
    m_annotationMessage = message; m_annotationMessageState = m_session->stateId();
    QPointer<QMessageBox> warning(new QMessageBox(QMessageBox::Warning, "Cannot edit image", message, QMessageBox::Ok, this));
    warning->setObjectName("canvasOperationError"); warning->setTextFormat(Qt::PlainText); warning->exec(); disposeCanvasDialog(warning.data()); return false;
  }
  if (!prepared.result.changed) return false;
  if (m_sourceBytes != m_session->document().sourceImagePng) {
    m_sourceBytes = m_session->document().sourceImagePng; m_image = std::move(prepared.source);
    m_canvas->sourceChanged(prepared.displaySource); if (!owner) return false;
  }
  m_annotationMessage.clear(); m_annotationMessageState = m_session->stateId(); return true;
}

bool EditorWindow::save(bool chooseFile) {
  if (m_outputPending || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this);
  QString target = m_path;
  if (chooseFile || m_path.isEmpty()) {
    QPointer<QFileDialog> picker(new QFileDialog(this, "Save image", m_path.isEmpty() ? "Untitled.png" : m_path, "PNG image (*.png)"));
    picker->setObjectName("editorSaveAsDialog"); picker->setAccessibleName("Save image destination");
    picker->setOption(QFileDialog::DontUseNativeDialog); picker->setAcceptMode(QFileDialog::AcceptSave);
    picker->setFileMode(QFileDialog::AnyFile); picker->setDefaultSuffix("png");
    const auto accepted = picker->exec();
    target = picker && accepted == QDialog::Accepted ? picker->selectedFiles().value(0) : QString{};
    delete picker.data();
    if (!guard || target.isEmpty()) return false;
    if (QFileInfo(target).suffix().isEmpty()) target += QStringLiteral(".png");
  }
  if (QFileInfo(target).suffix().compare("png", Qt::CaseInsensitive) != 0) {
    QMessageBox::warning(this, "Format unavailable", "This editor currently saves PNG. Use Save As with a .png destination; the existing file has been kept."); return false;
  }
  return performSave(target);
}
bool EditorWindow::retrySave() {
  if (m_outputPending || !m_partialSave || m_partialSaveState != m_session->stateId() || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this); const auto expected = m_partialSave; const auto state = m_partialSaveState;
  QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Retry annotation save?",
      QString("Retry the editable annotations for %1? The already saved PNG will be kept without rewriting it.").arg(QFileInfo(expected->path).fileName()),
      QMessageBox::Retry | QMessageBox::Cancel, this));
  box->setTextFormat(Qt::PlainText);
  box->setObjectName("retryEditorSidecarDialog"); box->setDefaultButton(QMessageBox::Cancel); box->setEscapeButton(QMessageBox::Cancel);
  const auto choice = box->exec(); delete box.data();
  if (!guard || choice != QMessageBox::Retry || m_outputPending || m_partialSave != expected || m_partialSaveState != state || state != m_session->stateId()) return false;
  return performSave(expected->path, expected);
}
bool EditorWindow::configurePreferences(const std::function<bool()> &operation) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> owner(this); m_canvasOperationPending = true;
  const auto restore = qScopeGuard([owner] { if (owner) { owner->m_canvasOperationPending = false; owner->refresh(); } });
  return withOutputBusy([owner, operation] { return owner ? operation() : false; });
}
bool EditorWindow::withOutputBusy(const std::function<bool()> &operation) {
  if (m_outputPending || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this);
  QMap<QAction *, bool> enabled;
  for (auto *action : m_toolbarActions) enabled.insert(action, action->isEnabled());
  const bool canvasEnabled = m_canvas->isEnabled(), menusEnabled = menuBar()->isEnabled();
  m_outputPending = true;
  const auto restore = qScopeGuard([guard, enabled, canvasEnabled, menusEnabled] {
    if (!guard) return;
    guard->m_outputPending = false; guard->m_canvas->setEnabled(canvasEnabled); if (!guard) return;
    guard->menuBar()->setEnabled(menusEnabled); if (!guard) return;
    for (auto it = enabled.cbegin(); it != enabled.cend(); ++it) { it.key()->setEnabled(it.value()); if (!guard) return; }
    guard->refresh();
  });
  m_annotationObjects->refresh(); if (!guard) return false;
  for (auto it = enabled.cbegin(); it != enabled.cend(); ++it) { it.key()->setEnabled(false); if (!guard) return false; }
  m_retrySave->setEnabled(false); if (!guard) return false;
  m_retryCopy->setEnabled(false); if (!guard) return false;
  m_retryPrint->setEnabled(false); if (!guard) return false;
  m_canvas->setEnabled(false); if (!guard) return false;
  menuBar()->setEnabled(false); if (!guard) return false;
  return operation();
}
bool EditorWindow::performSave(const QString &target, const EditorSaveReceiptPtr &repair) {
  const QPointer<EditorWindow> guard(this);
  const auto state = m_session->stateId(); const auto document = m_session->document(); const auto source = m_image; const auto hooks = m_saveHooks;
  return withOutputBusy([this, guard, state, document, source, hooks, target, repair] {
    const SaveOutcome outcome = runEditorSave(target, source, document, this, hooks, repair);
    if (!guard) return false;
    if (outcome.cancelled && !outcome.rasterSaved) {
      emit saveFinished(false, false, true); return false;
    }
    // ES-012: clean only when both artifacts persisted; say which one failed.
    const QStringList failed = m_session->recordSnapshotSave(state, outcome.rasterSaved, outcome.sidecarSaved);
    if (outcome.rasterSaved && !outcome.sidecarSaved && outcome.receipt) { m_partialSave = outcome.receipt; m_partialSaveState = state; }
    if (!failed.isEmpty()) {
      refresh(); emit saveFinished(outcome.rasterSaved, outcome.sidecarSaved, outcome.cancelled); if (!guard) return false;
      QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, "Save incomplete",
          outcome.rasterSaved ? "PNG saved; editable annotation persistence failed. The document remains unsaved. Retry annotation save keeps the PNG."
                              : "PNG was not saved. The previous destination and editable document have been kept.", QMessageBox::Ok, this));
      box->setTextFormat(Qt::PlainText);
      box->setInformativeText(QString("Destination: %1\nFormat: PNG\n%2").arg(target, failed.join(", "))); box->exec(); delete box.data();
      return false;
    }
    m_partialSave.reset(); m_partialSaveState.reset();
    m_saveMessage = outcome.sidecarPath ? "Saved PNG and editable annotations" : "Saved PNG; no annotation companion needed";
    if (outcome.cancelled) m_saveMessage += " (completed despite cancellation)";
    m_saveMessageState = state;
    m_path = target;
    recordRecentImage(absoluteImagePath(target));
    if (!guard) return false;
    emit saveFinished(true, true, outcome.cancelled);
    return bool(guard);
  });
}

void EditorWindow::setCopyPorts(EditorCopyPorts ports) { m_copyPorts = std::move(ports); refresh(); }
bool EditorWindow::copyImage(const EditorCopyPayloadPtr &retry) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || !m_copyPorts.publish || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this);
  const auto state = m_session->stateId(); const auto document = m_session->document(); const auto source = m_image; const auto ports = m_copyPorts;
  return withOutputBusy([this, guard, state, document, source, ports, retry] {
    const auto copied = runEditorCopy(source, document, this, ports, retry);
    if (!guard) return false;
    const auto &action = copied.action;
    const bool success = !action.outcomeUnknown && action.state == xerahs::actions::ActionState::Succeeded;
    if (success) { m_failedCopy.reset(); m_failedCopyState.reset(); m_copyMessage = "Image copied with alpha and PNG; clipboard uses sRGB"; }
    else if (action.outcomeUnknown) {
      m_failedCopy.reset(); m_failedCopyState.reset();
      m_copyMessage = "Image copy delivery is unknown; the clipboard may have changed. Check the destination before copying again";
    } else if (action.state == xerahs::actions::ActionState::Failed) {
      m_failedCopy = copied.payload; m_failedCopyState = copied.payload ? std::optional<quint64>{state} : std::nullopt;
      m_copyMessage = "Image copy failed; the document is unchanged";
    } else m_copyMessage = "Image copy cancelled; the clipboard was kept";
    if (copied.payload && copied.payload->disclosures.contains("clipboard-colour-converted-to-srgb")) m_copyMessage += " (source colours converted to sRGB)";
    if (copied.payload && copied.payload->disclosures.contains("clipboard-untagged-assumed-srgb")) m_copyMessage += " (untagged source interpreted as sRGB)";
    if (copied.cancellationArrivedDuring) m_copyMessage += " (cancellation arrived during publication)";
    m_copyMessageState = state;
    const auto diagnostic = action.diagnostic.value_or(QString{});
    emit copyFinished(action.outcomeUnknown ? "unknown" : xerahs::actions::actionStateName(action.state), diagnostic, copied.cancellationArrivedDuring);
    if (!guard) return false;
    if (action.state == xerahs::actions::ActionState::Failed || action.outcomeUnknown) {
      QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, "Image copy incomplete",
          action.outcomeUnknown ? "Clipboard delivery could not be confirmed. Its contents may have changed; check the destination before a new Copy Image action. The editable document is kept."
                               : diagnostic == "clipboard-annotation-unsupported" ? "A visible unsupported annotation cannot be copied without losing it. The editable document and clipboard contents are kept."
                               : copied.payload ? "The image was not copied. The editable document and previous clipboard contents are kept. Retry image copy can publish a prepared snapshot without rendering it again."
                                                : "The image could not be prepared. The editable document and clipboard contents are kept. Use Copy Image to try again.", QMessageBox::Ok, this));
      box->setTextFormat(Qt::PlainText); box->setInformativeText(diagnostic); box->exec(); delete box.data();
    }
    return success && bool(guard);
  });
}
bool EditorWindow::retryCopy() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || !m_copyPorts.publish || !m_failedCopy || m_failedCopyState != m_session->stateId() || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this); const auto expected = m_failedCopy; const auto state = m_failedCopyState;
  QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Retry image copy?",
      "Publish the unchanged rendered snapshot to the clipboard? No pixels will be rendered again. This replaces current clipboard contents.", QMessageBox::Retry | QMessageBox::Cancel, this));
  box->setObjectName("retryImageCopyDialog"); box->setDefaultButton(QMessageBox::Cancel); box->setEscapeButton(QMessageBox::Cancel);
  const auto choice = box->exec(); delete box.data();
  if (!guard || choice != QMessageBox::Retry || m_outputPending || m_failedCopy != expected || m_failedCopyState != state || state != m_session->stateId()) return false;
  return copyImage(expected);
}

void EditorWindow::setPrintPorts(EditorPrintPorts ports) { m_printPorts = std::move(ports); refresh(); }
bool EditorWindow::printImage(const EditorPrintSnapshotPtr &retry) {
  if (m_outputPending || m_canvasOperationPending || m_closePending || !m_printPorts.configure || !m_printPorts.submit || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this);
  const auto state = m_session->stateId(); const auto document = m_session->document(); const auto source = m_image; const auto ports = m_printPorts;
  return withOutputBusy([this, guard, state, document, source, ports, retry] {
    const auto printed = runEditorPrint(source, document, this, ports, retry); if (!guard) return false;
    const auto &action = printed.action; const bool success = action.state == xerahs::actions::ActionState::Succeeded && !action.outcomeUnknown;
    m_failedPrint.reset(); m_failedPrintState.reset();
    if (success) m_printMessage = "Image submitted to the printer; physical delivery is not confirmed";
    else if (action.outcomeUnknown) m_printMessage = "Print delivery is unknown. Check the printer queue before starting another print";
    else if (action.state == xerahs::actions::ActionState::Failed) {
      m_failedPrint = printed.snapshot; m_failedPrintState = printed.snapshot ? std::optional<quint64>{state} : std::nullopt;
      m_printMessage = "Print failed before a confirmed submission; the document is unchanged";
    } else m_printMessage = "Print cancelled before submission; the document is unchanged";
    if (printed.cancellationArrivedDuring) m_printMessage += " (cancellation requested during submission)";
    m_printMessageState = state;
    const auto diagnostic = action.diagnostic.value_or(QString{});
    try { emit printFinished(action.outcomeUnknown ? "unknown" : xerahs::actions::actionStateName(action.state), diagnostic, printed.cancellationArrivedDuring); }
    catch (...) { if (guard) m_printMessage += " (completion observer failed)"; }
    if (!guard) return false;
    if (action.state == xerahs::actions::ActionState::Failed || action.outcomeUnknown) {
      QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, "Image print incomplete",
          action.outcomeUnknown ? "The printer may have received the job. Check its queue before a new Print Image action. Automatic retry is unavailable; the editable document is kept."
                               : diagnostic == "print-annotation-unsupported" ? "A visible unsupported annotation cannot be printed without losing it. The editable document is kept."
                               : printed.snapshot ? "The image was not submitted. Retry image print can reuse this snapshot with fresh printer confirmation. The editable document is kept."
                                                  : "The image could not be prepared. Use Print Image to try again. The editable document is kept.", QMessageBox::Ok, this));
      box->setTextFormat(Qt::PlainText); box->setInformativeText(diagnostic); box->exec(); delete box.data();
    }
    return success && bool(guard);
  });
}
bool EditorWindow::retryPrint() {
  if (m_outputPending || m_canvasOperationPending || m_closePending || !m_printPorts.configure || !m_printPorts.submit || !m_failedPrint || m_failedPrintState != m_session->stateId() || m_canvas->interactionActive()) return false;
  const QPointer<EditorWindow> guard(this); const auto expected = m_failedPrint; const auto state = m_failedPrintState;
  QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Question, "Retry image print?",
      "Prepare the unchanged snapshot for a new native printer confirmation? No pixels will be rendered again. A print is submitted only after confirmation.", QMessageBox::Retry | QMessageBox::Cancel, this));
  box->setObjectName("retryImagePrintDialog"); box->setDefaultButton(QMessageBox::Cancel); box->setEscapeButton(QMessageBox::Cancel);
  const auto choice = box->exec(); delete box.data();
  if (!guard || choice != QMessageBox::Retry || m_outputPending || m_failedPrint != expected || m_failedPrintState != state || state != m_session->stateId()) return false;
  return printImage(expected);
}

void EditorWindow::setAnnotationClipboardPorts(AnnotationClipboardPorts ports) { m_annotationClipboard = std::move(ports); refresh(); }
bool EditorWindow::transferAnnotations(const QString &command) {
  using namespace xerahs::actions;
  if (m_outputPending || m_canvasOperationPending || m_closePending || m_canvas->interactionActive() || !m_annotationActions.contains(command)) return false;
  const QPointer<EditorWindow> guard(this); const auto state = m_session->stateId(); const auto selection = m_session->selection(); const auto ports = m_annotationClipboard;
  if ((command == "paste" && !ports.read) || ((command == "copy-annotation" || command == "cut-annotation") && !ports.publish)) return false;
  return withOutputBusy([this, guard, state, selection, ports, command] {
    ActionOutcome action{ActionState::Failed, "annotation-transfer-failed", std::nullopt}; bool changed = false, published = false;
    const auto recordEdit = [&](const EditorSession::TransferEditResult &edit) { changed = edit.changed; action = changed ? ActionOutcome{} : ActionOutcome{ActionState::Failed, edit.diagnostic, std::nullopt}; };
    const double step = std::max(1.0, 10.0 / m_canvas->viewState().zoom());
    if (command == "duplicate-selection") recordEdit(m_session->duplicateSelection(step));
    else if (command == "paste") {
      AnnotationClipboardRead read;
      try { read = ports.read(); } catch (...) { read.diagnostic = "annotation-clipboard-read-failed"; }
      if (!guard) return false;
      if (!read.diagnostic.isEmpty()) {
        const auto code = read.diagnostic == "annotation-clipboard-format-unavailable" || read.diagnostic == "annotation-clipboard-unavailable" || read.diagnostic == "annotation-transfer-too-large" ? read.diagnostic : QString("annotation-clipboard-read-failed");
        action.diagnostic = code;
      } else {
        const auto parsed = parseAnnotationClipboard(read.bytes);
        if (!parsed.ok()) action.diagnostic = parsed.diagnostic;
        else {
          const double offset = m_lastAnnotationPaste == read.bytes ? m_annotationPasteOffset + step : step;
          recordEdit(m_session->insertAnnotations(parsed.annotations, state, offset));
          if (changed) { m_lastAnnotationPaste = std::move(read.bytes); m_annotationPasteOffset = offset; }
        }
      }
    } else {
      const auto prepared = selectedAnnotationTransfer(m_session->document(), selection, true);
      if (!prepared.ok()) action.diagnostic = prepared.diagnostic;
      else {
        try { action = ports.publish(prepared.bytes); }
        catch (...) { action.outcomeUnknown = true; }
        if (!guard) return false;
        if (action.outcomeUnknown || action.replacementSelection || !action.artifacts.empty() ||
            (action.state != ActionState::Succeeded && action.state != ActionState::Failed && action.state != ActionState::Cancelled)) {
          action = {ActionState::Failed, "annotation-clipboard-delivery-unknown", std::nullopt}; action.outcomeUnknown = true;
        } else if (action.state == ActionState::Succeeded) { action = {}; published = true; }
        else if (action.state == ActionState::Cancelled) action = {ActionState::Cancelled, std::nullopt, std::nullopt};
        else action.diagnostic = action.diagnostic == std::optional<QString>{"annotation-clipboard-unavailable"} ? "annotation-clipboard-unavailable" : "annotation-clipboard-failed";
        if (published && command == "cut-annotation") recordEdit(m_session->cutAnnotations(state, selection));
      }
    }
    const bool partial = published && command == "cut-annotation" && !changed;
    const bool success = action.state == ActionState::Succeeded && !action.outcomeUnknown;
    if (action.outcomeUnknown) m_annotationMessage = "Clipboard delivery is unknown; selected annotations are kept. Check the clipboard before copying again";
    else if (partial) m_annotationMessage = "Annotations were copied, but Cut did not complete; the selection is kept";
    else if (success) m_annotationMessage = command == "copy-annotation" ? "Annotations copied; the document is unchanged" : command == "cut-annotation" ? "Annotations cut; Undo restores them" : command == "paste" ? "Annotations pasted as one undoable operation" : "Annotations duplicated as one undoable operation";
    else if (action.state == ActionState::Cancelled) m_annotationMessage = "Annotation transfer cancelled; the document is unchanged";
    else m_annotationMessage = "Annotation action failed; the document is unchanged";
    m_annotationMessageState = m_session->stateId();
    const auto diagnostic = action.diagnostic.value_or(QString{});
    try { emit annotationTransferFinished(command, partial ? "partial" : action.outcomeUnknown ? "unknown" : actionStateName(action.state), diagnostic, changed); }
    catch (...) { if (guard) m_annotationMessage += " (completion observer failed)"; }
    if (!guard) return false;
    if (!success && action.state != ActionState::Cancelled) {
      const auto explanation = action.outcomeUnknown ? "The clipboard may have changed, so Cut kept the selected objects. Check the clipboard before copying again."
          : partial ? "The clipboard contains the selected annotations, but they could not be removed. The editable document is kept."
          : diagnostic == "annotation-clipboard-assets-unavailable" ? "Embedded-asset copying needs compatible asset support. No annotations or assets were dropped; the document and clipboard are kept."
          : diagnostic == "annotation-type-unsupported" ? "This selection contains an unsupported annotation. The whole selection is kept; no supported subset is transferred."
          : diagnostic == "annotation-selection-protected" ? "Hidden or locked annotations cannot be transferred by this command. The selection is kept."
          : diagnostic == "annotation-clipboard-format-unavailable" ? "The clipboard has no supported annotation payload. Image and text clipboard contents are kept."
          : diagnostic == "annotation-transfer-too-large" ? "The selection or clipboard data exceeds the transfer limit. Use fewer annotations or smaller annotation metadata. The document is kept."
          : "The annotation transfer could not complete. The document is kept. Check the selection or copy supported annotations again.";
      QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, "Annotation action incomplete", explanation, QMessageBox::Ok, this));
      box->setTextFormat(Qt::PlainText); box->setInformativeText(diagnostic); box->exec(); delete box.data();
    }
    return success && bool(guard);
  });
}

void EditorWindow::recordRecentImage(const QString &absolutePath) {
  const QPointer<EditorWindow> guard(this);
  const auto diagnostic = rememberEditorImage(absolutePath);
  if (!guard) return;
  m_preferenceDiagnostic = diagnostic;
  refresh();
}

void EditorWindow::closeEvent(QCloseEvent *event) {
  // A cancelled progress window can hide before its worker exits. Keep the
  // editor and its stack-owned modal objects alive until preparation completes.
  if (m_canvasOperationPending || m_closePending || m_outputPending || m_canvas->interactionActive()) { event->ignore(); return; }
  const QPointer<EditorWindow> guard(this); m_closePending = true;
  const auto closing = qScopeGuard([guard] { if (guard) guard->m_closePending = false; });
  if (m_session->dirty()) {
    // ES-008: Save, Discard, Cancel.
    QMessageBox box(QMessageBox::Question, QStringLiteral("Unsaved changes"),
                    QStringLiteral("Save changes to %1?").arg(m_path.isEmpty() ? QStringLiteral("Untitled image") : QFileInfo(m_path).fileName()),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setDefaultButton(QMessageBox::Save);
    const auto choice = box.exec(); box.setParent(nullptr);
    switch (choice) {
      case QMessageBox::Save: if (!save(false)) { event->ignore(); return; } break;
      case QMessageBox::Discard: break;
      default: event->ignore(); return;
    }
  }
  if (!guard) { event->ignore(); return; }
  const auto diagnostic = m_windowState->remember();
  if (!diagnostic.isEmpty()) QMessageBox::warning(this, QStringLiteral("Window preferences not saved"), editorPreferenceDiagnosticMessage(diagnostic));
  event->accept();
}

}  // namespace xerahs::app
