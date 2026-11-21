#include "ShellPages.h"

#include "AppContext.h"
#include "AppIcons.h"
#include "AppTheme.h"
#include "ApplicationIcon.h"
#include "ApplicationWindow.h"
#include "CaptureController.h"
#include "CaptureWorkflows.h"
#include "CommandRegistry.h"
#include "EditorCanvas.h"
#include "EditorOpen.h"
#include "EditorWindow.h"
#include "EffectsDialog.h"
#include "NativeAnnotationTools.h"
#include "NativeEditorPreferences.h"
#include "NativePipelineJournal.h"
#include "RecordingActions.h"
#include "RecordingController.h"
#include "diagnostics/DiagnosticBundle.h"
#include "features/PreferencesFeature.h"
#include "history/CaptureHistory.h"
#include "image-editor/EditorSession.h"
#include "settings/ApplicationSettings.h"
#include "uploads/DestinationCatalog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace xerahs::app {
namespace {

QString trPage(const char *source) {
  return QCoreApplication::translate("ShellPages", source);
}

QColor currentIconColor(const ApplicationWindow &window) {
  if (isHighContrastSystemPalette(QApplication::palette())) {
    return QApplication::palette().color(QPalette::WindowText);
  }
  return appThemePalette(window.isDarkTheme()).textPrimary;
}

QFrame *makeCard(QWidget *parent, int padding = 16) {
  auto *card = new QFrame(parent);
  card->setProperty("card", true);
  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(padding, padding, padding, padding);
  layout->setSpacing(12);
  return card;
}

QLabel *makeLabel(const QString &text, const char *role, QWidget *parent) {
  auto *label = new QLabel(text, parent);
  if (role && *role) label->setProperty("role", QString::fromLatin1(role));
  label->setWordWrap(true);
  return label;
}

QScrollArea *wrapScroll(QWidget *content, QWidget *parent, const QString &accessibleName) {
  auto *scroll = new QScrollArea(parent);
  scroll->setFocusPolicy(Qt::NoFocus);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setAccessibleName(accessibleName);
  scroll->viewport()->setAutoFillBackground(false);
  content->setAutoFillBackground(false);
  scroll->setWidget(content);
  return scroll;
}

// ---------------------------------------------------------------------------
// 1. Embedded Editor Page (matches ShareX.ImageEditor EditorView.axaml)
// ---------------------------------------------------------------------------
class EmbeddedEditorPage final : public QWidget {
public:
  EmbeddedEditorPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    setObjectName(QStringLiteral("editorSurfaceHost"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Prepare an initial blank canvas so the embedded editor is immediately
    // interactive (matching ShareX/XerahS opening on the Editor page).
    m_image = QImage(960, 560, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);
    xerahs::editor::AnnotationDocument doc;
    doc.canvasWidth = m_image.width();
    doc.canvasHeight = m_image.height();
    m_session = std::make_unique<xerahs::editor::EditorSession>(std::move(doc), xerahs::editor::EditorSession::InitialPersistence::Saved);
    applyDefaults();

    // Row 0: Floating Top Annotation Toolbar (centered pill bar).
    auto *topWrap = new QWidget(this);
    auto *topWrapLayout = new QVBoxLayout(topWrap);
    topWrapLayout->setContentsMargins(8, 8, 8, 0);
    topWrapLayout->setSpacing(0);

    m_topToolbar = new QFrame(topWrap);
    m_topToolbar->setObjectName(QStringLiteral("editorTopToolbar"));
    auto *tbLayout = new QHBoxLayout(m_topToolbar);
    tbLayout->setContentsMargins(4, 4, 4, 4);
    tbLayout->setSpacing(3);

    // File dropdown button
    m_fileBtn = new QPushButton(m_topToolbar);
    m_fileBtn->setObjectName(QStringLiteral("editorFileMenuButton"));
    m_fileBtn->setAccessibleName(trPage("Editor file menu"));
    m_fileBtn->setToolTip(trPage("File"));
    m_fileBtn->setFixedSize(46, 34);
    auto *fileMenu = new QMenu(m_fileBtn);
    auto *actNew = fileMenu->addAction(trPage("New image…"));
    connect(actNew, &QAction::triggered, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("new-image"))) a->trigger();
    });
    auto *actOpen = fileMenu->addAction(trPage("Open image…"));
    connect(actOpen, &QAction::triggered, this, &EmbeddedEditorPage::openImageInteractive);
    fileMenu->addSeparator();
    auto *actSave = fileMenu->addAction(trPage("Save…"));
    connect(actSave, &QAction::triggered, this, &EmbeddedEditorPage::saveImageInteractive);
    auto *actPopout = fileMenu->addAction(trPage("Open in standalone Editor window"));
    connect(actPopout, &QAction::triggered, this, &EmbeddedEditorPage::openStandalone);
    m_fileBtn->setMenu(fileMenu);
    tbLayout->addWidget(m_fileBtn);

    auto addSep = [&]() {
      auto *sep = new QFrame(m_topToolbar);
      sep->setFrameShape(QFrame::VLine);
      sep->setFixedWidth(1);
      sep->setFixedHeight(22);
      tbLayout->addWidget(sep);
    };
    addSep();

    m_canvas = new EditorCanvas(*m_session, m_image, this);
    m_canvas->setObjectName(QStringLiteral("embeddedEditorCanvas"));

    struct ToolEntry { EditorCanvas::Tool tool; AppIcon icon; const char *label; };
    const std::array<ToolEntry, 15> tools{{
      {EditorCanvas::Tool::Select, AppIcon::MousePointer2, "Select"},
      {EditorCanvas::Tool::Rectangle, AppIcon::Square, "Rectangle"},
      {EditorCanvas::Tool::Ellipse, AppIcon::Circle, "Ellipse"},
      {EditorCanvas::Tool::Line, AppIcon::Minus, "Line"},
      {EditorCanvas::Tool::Arrow, AppIcon::ArrowRight, "Arrow"},
      {EditorCanvas::Tool::Freehand, AppIcon::Pencil, "Freehand"},
      {EditorCanvas::Tool::Highlight, AppIcon::Highlighter, "Highlight"},
      {EditorCanvas::Tool::Text, AppIcon::Type, "Text"},
      {EditorCanvas::Tool::SpeechBalloon, AppIcon::MessageSquare, "Speech Balloon"},
      {EditorCanvas::Tool::Step, AppIcon::Hash, "Step"},
      {EditorCanvas::Tool::Blur, AppIcon::Droplet, "Blur"},
      {EditorCanvas::Tool::Pixelate, AppIcon::Grid2x2, "Pixelate"},
      {EditorCanvas::Tool::Magnify, AppIcon::Search, "Magnify"},
      {EditorCanvas::Tool::Spotlight, AppIcon::Lightbulb, "Spotlight"},
      {EditorCanvas::Tool::Crop, AppIcon::Crop, "Crop"},
    }};

    for (const auto &entry : tools) {
      auto *btn = new QPushButton(m_topToolbar);
      btn->setCheckable(true);
      btn->setFixedSize(32, 32);
      btn->setProperty("iconOnly", true);
      btn->setToolTip(trPage(entry.label));
      btn->setAccessibleName(trPage(entry.label));
      btn->setProperty("editorTool", int(entry.tool));
      btn->setProperty("editorIcon", int(entry.icon));
      connect(btn, &QPushButton::clicked, this, [this, tool = entry.tool] {
        m_canvas->setTool(tool);
        syncToolButtons();
      });
      tbLayout->addWidget(btn);
      m_toolButtons.append(btn);
    }

    addSep();

    auto addIconAction = [&](AppIcon icon, const char *tip, const std::function<void()> &fn) -> QPushButton * {
      auto *btn = new QPushButton(m_topToolbar);
      btn->setFixedSize(32, 32);
      btn->setProperty("iconOnly", true);
      btn->setToolTip(trPage(tip));
      btn->setAccessibleName(trPage(tip));
      btn->setProperty("editorIcon", int(icon));
      connect(btn, &QPushButton::clicked, this, fn);
      tbLayout->addWidget(btn);
      m_iconButtons.append(btn);
      return btn;
    };

    m_undoBtn = addIconAction(AppIcon::Undo2, "Undo", [this] {
      if (m_session->undo()) { m_canvas->viewport()->update(); syncState(); }
    });
    m_redoBtn = addIconAction(AppIcon::Redo2, "Redo", [this] {
      if (m_session->redo()) { m_canvas->viewport()->update(); syncState(); }
    });
    m_deleteBtn = addIconAction(AppIcon::Trash2, "Delete selected annotation", [this] {
      m_canvas->deleteSelection();
      syncState();
    });
    m_effectsBtn = addIconAction(AppIcon::Sparkles, "Image effects", [this] { openStandalone(); });

    // Wrap top toolbar in a horizontal scroll strip (like AnnotationToolbar.axaml
    // PrimaryToolbarScrollViewer) so narrow windows scroll instead of overflowing.
    auto *topScroll = new QScrollArea(topWrap);
    topScroll->setFocusPolicy(Qt::NoFocus);
    topScroll->setFrameShape(QFrame::NoFrame);
    topScroll->setWidgetResizable(true);
    topScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    topScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    topScroll->setFixedHeight(48);
    topScroll->setMinimumWidth(0);
    topScroll->viewport()->setAutoFillBackground(false);
    auto *topCenter = new QWidget(topScroll);
    topCenter->setAutoFillBackground(false);
    auto *topCenterLayout = new QHBoxLayout(topCenter);
    topCenterLayout->setContentsMargins(0, 0, 0, 0);
    topCenterLayout->addStretch();
    topCenterLayout->addWidget(m_topToolbar);
    topCenterLayout->addStretch();
    topScroll->setWidget(topCenter);
    topWrapLayout->addWidget(topScroll);

    // Row 0b: Contextual Tool Options Strip directly under top toolbar.
    m_optionsBar = new QFrame(topWrap);
    m_optionsBar->setObjectName(QStringLiteral("editorOptionsBar"));
    auto *optLayout = new QHBoxLayout(m_optionsBar);
    optLayout->setContentsMargins(8, 3, 8, 3);
    optLayout->setSpacing(6);

    m_activeToolLabel = new QLabel(trPage("Rectangle"), m_optionsBar);
    m_activeToolLabel->setProperty("role", QStringLiteral("caption"));
    optLayout->addWidget(m_activeToolLabel);

    m_strokeBtn = new QPushButton(trPage("Stroke"), m_optionsBar);
    m_strokeBtn->setAccessibleName(trPage("Stroke color"));
    m_strokeBtn->setFixedHeight(28);
    connect(m_strokeBtn, &QPushButton::clicked, this, [this] { pickStyleColor(true); });
    optLayout->addWidget(m_strokeBtn);

    m_fillBtn = new QPushButton(trPage("Fill"), m_optionsBar);
    m_fillBtn->setAccessibleName(trPage("Fill color"));
    m_fillBtn->setFixedHeight(28);
    connect(m_fillBtn, &QPushButton::clicked, this, [this] { pickStyleColor(false); });
    optLayout->addWidget(m_fillBtn);

    auto *thickMinus = new QPushButton(trPage("−"), m_optionsBar);
    thickMinus->setAccessibleName(trPage("Decrease stroke width"));
    thickMinus->setProperty("iconOnly", true);
    thickMinus->setFixedSize(28, 28);
    connect(thickMinus, &QPushButton::clicked, this, [this] { adjustStroke(-1); });
    optLayout->addWidget(thickMinus);

    m_thicknessLabel = new QLabel(trPage("4 px"), m_optionsBar);
    m_thicknessLabel->setProperty("role", QStringLiteral("caption"));
    optLayout->addWidget(m_thicknessLabel);

    auto *thickPlus = new QPushButton(trPage("+"), m_optionsBar);
    thickPlus->setAccessibleName(trPage("Increase stroke width"));
    thickPlus->setProperty("iconOnly", true);
    thickPlus->setFixedSize(28, 28);
    connect(thickPlus, &QPushButton::clicked, this, [this] { adjustStroke(1); });
    optLayout->addWidget(thickPlus);

    auto *captureQuick = new QPushButton(trPage("Capture"), m_optionsBar);
    captureQuick->setProperty("accent", true);
    captureQuick->setFixedHeight(28);
    captureQuick->setAccessibleName(trPage("Capture region"));
    connect(captureQuick, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("capture-region"))) a->trigger();
    });
    optLayout->addWidget(captureQuick);

    auto *openQuick = new QPushButton(trPage("Open…"), m_optionsBar);
    openQuick->setFixedHeight(28);
    openQuick->setAccessibleName(trPage("Open image into editor"));
    connect(openQuick, &QPushButton::clicked, this, &EmbeddedEditorPage::openImageInteractive);
    optLayout->addWidget(openQuick);

    auto *optScroll = new QScrollArea(topWrap);
    optScroll->setFocusPolicy(Qt::NoFocus);
    optScroll->setFrameShape(QFrame::NoFrame);
    optScroll->setWidgetResizable(true);
    optScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    optScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    optScroll->setFixedHeight(40);
    optScroll->setMinimumWidth(0);
    optScroll->viewport()->setAutoFillBackground(false);
    auto *optCenter = new QWidget(optScroll);
    optCenter->setAutoFillBackground(false);
    auto *optCenterLayout = new QHBoxLayout(optCenter);
    optCenterLayout->setContentsMargins(0, 0, 0, 0);
    optCenterLayout->addStretch();
    optCenterLayout->addWidget(m_optionsBar);
    optCenterLayout->addStretch();
    optScroll->setWidget(optCenter);
    topWrapLayout->addWidget(optScroll);
    root->addWidget(topWrap);

    // Row 1: Central Checkered Editor Canvas
    root->addWidget(m_canvas, 1);

    // Row 2: Floating Bottom Zoom & Output Action Bar (EditorBuiltInToolbars.axaml)
    auto *bottomWrap = new QWidget(this);
    auto *bottomWrapLayout = new QHBoxLayout(bottomWrap);
    bottomWrapLayout->setContentsMargins(8, 0, 8, 8);

    m_bottomBar = new QFrame(bottomWrap);
    m_bottomBar->setObjectName(QStringLiteral("editorBottomBar"));
    auto *botLayout = new QHBoxLayout(m_bottomBar);
    botLayout->setContentsMargins(6, 4, 6, 4);
    botLayout->setSpacing(4);

    auto addBottomBtn = [&](AppIcon icon, const char *tip, const std::function<void()> &fn) -> QPushButton * {
      auto *btn = new QPushButton(m_bottomBar);
      btn->setFixedSize(34, 32);
      btn->setProperty("iconOnly", true);
      btn->setToolTip(trPage(tip));
      btn->setAccessibleName(trPage(tip));
      btn->setProperty("editorIcon", int(icon));
      connect(btn, &QPushButton::clicked, this, fn);
      botLayout->addWidget(btn);
      m_iconButtons.append(btn);
      return btn;
    };

    addBottomBtn(AppIcon::ZoomOut, "Zoom out", [this] { m_canvas->zoomOut(); });
    m_zoomLabel = new QPushButton(trPage("100%"), m_bottomBar);
    m_zoomLabel->setProperty("compact", true);
    m_zoomLabel->setAccessibleName(trPage("Reset zoom to 100 percent"));
    m_zoomLabel->setToolTip(trPage("Reset zoom (100%)"));
    m_zoomLabel->setFixedHeight(32);
    m_zoomLabel->setMinimumWidth(54);
    connect(m_zoomLabel, &QPushButton::clicked, this, [this] { m_canvas->resetZoom(); });
    botLayout->addWidget(m_zoomLabel);
    addBottomBtn(AppIcon::ZoomIn, "Zoom in", [this] { m_canvas->zoomIn(); });
    addBottomBtn(AppIcon::Maximize, "Zoom to fit", [this] { m_canvas->zoomToFit(); });

    auto *bSep = new QFrame(m_bottomBar);
    bSep->setFrameShape(QFrame::VLine);
    bSep->setFixedWidth(1);
    bSep->setFixedHeight(24);
    botLayout->addWidget(bSep);

    addBottomBtn(AppIcon::Clipboard, "Copy image to clipboard", [this] { copyImageToClipboard(); });
    addBottomBtn(AppIcon::Save, "Save image…", [this] { saveImageInteractive(); });
    addBottomBtn(AppIcon::SaveAll, "Open in full Editor window", [this] { openStandalone(); });
    addBottomBtn(AppIcon::CloudUpload, "Upload file…", [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("upload-file"))) a->trigger();
    });

    bottomWrapLayout->addStretch();
    bottomWrapLayout->addWidget(m_bottomBar);
    bottomWrapLayout->addStretch();
    root->addWidget(bottomWrap);

    // Wire menu bar delegates.
    auto *aUndo = new QAction(this);
    connect(aUndo, &QAction::triggered, this, [this] { if (m_session->undo()) { m_canvas->viewport()->update(); syncState(); } });
    auto *aRedo = new QAction(this);
    connect(aRedo, &QAction::triggered, this, [this] { if (m_session->redo()) { m_canvas->viewport()->update(); syncState(); } });
    auto *aCopy = new QAction(this);
    connect(aCopy, &QAction::triggered, this, &EmbeddedEditorPage::copyImageToClipboard);
    auto *aZoomIn = new QAction(this);
    connect(aZoomIn, &QAction::triggered, m_canvas, &EditorCanvas::zoomIn);
    auto *aZoomOut = new QAction(this);
    connect(aZoomOut, &QAction::triggered, m_canvas, &EditorCanvas::zoomOut);
    auto *aResetZoom = new QAction(this);
    connect(aResetZoom, &QAction::triggered, m_canvas, &EditorCanvas::resetZoom);
    auto *aFit = new QAction(this);
    connect(aFit, &QAction::triggered, m_canvas, &EditorCanvas::zoomToFit);
    m_menuUndo = aUndo;
    m_menuRedo = aRedo;
    m_window.setEditorMenuActions({
      {QStringLiteral("undo"), aUndo},
      {QStringLiteral("redo"), aRedo},
      {QStringLiteral("copy"), aCopy},
      {QStringLiteral("zoom-in"), aZoomIn},
      {QStringLiteral("zoom-out"), aZoomOut},
      {QStringLiteral("reset-zoom"), aResetZoom},
      {QStringLiteral("fit-image"), aFit},
    });

    connect(m_canvas, &EditorCanvas::changed, this, &EmbeddedEditorPage::syncState);
    connect(m_canvas, &EditorCanvas::toolChanged, this, &EmbeddedEditorPage::syncToolButtons);
    connect(m_canvas, &EditorCanvas::viewportChanged, this, [this](double zoom) {
      m_zoomLabel->setText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    });
    connect(&m_window, &ApplicationWindow::themeChanged, this, [this](bool) { refreshTheme(); });

    refreshTheme();
    syncToolButtons();
    syncState();
  }

  void loadImageFile(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage loaded = reader.read();
    if (loaded.isNull()) return;
    m_path = path;
    m_image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    xerahs::editor::AnnotationDocument doc;
    doc.canvasWidth = m_image.width();
    doc.canvasHeight = m_image.height();
    m_session = std::make_unique<xerahs::editor::EditorSession>(std::move(doc), xerahs::editor::EditorSession::InitialPersistence::Saved);
    applyDefaults();
    m_canvas->resetDocument(*m_session, m_image);
    m_canvas->zoomToFit();
    syncState();
  }

private:
  void applyDefaults() {
    const auto defaults = xerahs::editor::drawingStyleFromPreferences(nativeEditorPreferences());
    auto style = m_session->toolStyle();
    style.strokeColor = defaults.strokeColor;
    style.fillColor = defaults.fillColor;
    style.strokeWidth = defaults.strokeWidth;
    style.cornerRadius = defaults.cornerRadius;
    style.borderStyle = defaults.borderStyle;
    style.arrowStyle = defaults.arrowStyle;
    style.shadow = defaults.shadow;
    m_session->setCreationStyle(style);
  }

  void refreshTheme() {
    applyEditorTheme(*this, m_window.themeMode());
    if (m_canvas) {
      m_canvas->setProperty("darkTheme", m_window.isDarkTheme());
      m_canvas->viewport()->update();
    }
    const QColor fg = isHighContrastSystemPalette(QApplication::palette())
        ? QApplication::palette().color(QPalette::WindowText)
        : appThemePalette(m_window.isDarkTheme()).editorText;
    m_fileBtn->setIcon(appIcon(AppIcon::FolderOpen, fg, 18));
    for (auto *btn : m_toolButtons) {
      btn->setIcon(appIcon(static_cast<AppIcon>(btn->property("editorIcon").toInt()), fg, 18));
    }
    for (auto *btn : m_iconButtons) {
      btn->setIcon(appIcon(static_cast<AppIcon>(btn->property("editorIcon").toInt()), fg, 18));
    }
    updateSwatches();
  }

  void updateSwatches() {
    const auto style = m_session->toolStyle();
    const QColor stroke = QColor::fromRgba(style.strokeColor);
    const QColor fill = QColor::fromRgba(style.fillColor);
    m_strokeBtn->setIcon(swatchIcon(stroke, true, 16));
    m_fillBtn->setIcon(swatchIcon(fill, false, 16));
    m_thicknessLabel->setText(trPage("%1 px").arg(qRound(style.strokeWidth)));
  }

  void syncToolButtons() {
    const auto active = m_canvas->tool();
    for (auto *btn : m_toolButtons) {
      const bool checked = (static_cast<EditorCanvas::Tool>(btn->property("editorTool").toInt()) == active);
      btn->setChecked(checked);
      if (checked) m_activeToolLabel->setText(btn->toolTip());
    }
  }

  void syncState() {
    const bool canUndo = m_session->undoCount() > 0;
    const bool canRedo = m_session->redoCount() > 0;
    m_undoBtn->setEnabled(canUndo);
    m_redoBtn->setEnabled(canRedo);
    m_deleteBtn->setEnabled(m_canvas->canDeleteSelection());
    if (m_menuUndo) m_menuUndo->setEnabled(canUndo);
    if (m_menuRedo) m_menuRedo->setEnabled(canRedo);
    updateSwatches();
  }

  void pickStyleColor(bool stroke) {
    auto style = m_session->toolStyle();
    const QColor initial = QColor::fromRgba(stroke ? style.strokeColor : style.fillColor);
    const QColor chosen = QColorDialog::getColor(initial, &m_window,
        stroke ? trPage("Stroke color") : trPage("Fill color"), QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    const xerahs::editor::Argb argb = chosen.rgba();
    if (stroke) style.strokeColor = argb;
    else style.fillColor = argb;
    m_session->setCreationStyle(style);
    updateSwatches();
  }

  void adjustStroke(int delta) {
    auto style = m_session->toolStyle();
    style.strokeWidth = std::clamp(style.strokeWidth + delta, 1.0, 64.0);
    m_session->setCreationStyle(style);
    updateSwatches();
  }

  void openImageInteractive() {
    const QString path = QFileDialog::getOpenFileName(&m_window, trPage("Open image"),
        m_ctx.capture.saveDirectory(), imageFileFilter());
    if (!path.isEmpty()) loadImageFile(path);
  }

  void saveImageInteractive() {
    const QString target = QFileDialog::getSaveFileName(&m_window, trPage("Save image"),
        m_path.isEmpty() ? QDir(m_ctx.capture.saveDirectory()).filePath(QStringLiteral("edited.png")) : m_path,
        trPage("PNG Image (*.png);;JPEG Image (*.jpg *.jpeg);;WebP Image (*.webp)"));
    if (target.isEmpty()) return;
    QImage rendered = m_image;
    if (rendered.save(target)) {
      m_path = target;
      m_window.showResult(trPage("Editor"), trPage("Saved %1").arg(QFileInfo(target).fileName()), true);
    } else {
      m_window.showResult(trPage("Editor"), trPage("Could not save image."), false);
    }
  }

  void copyImageToClipboard() {
    QApplication::clipboard()->setImage(m_image);
    m_window.showResult(trPage("Editor"), trPage("Copied image to clipboard."), true);
  }

  void openStandalone() {
    if (!m_path.isEmpty()) {
      if (auto *win = EditorWindow::open(m_path, &m_window)) win->show();
    } else if (auto *win = EditorWindow::fromCapturedDocument(m_session->document(), &m_window)) {
      win->show();
    }
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QString m_path;
  QImage m_image;
  std::unique_ptr<xerahs::editor::EditorSession> m_session;
  EditorCanvas *m_canvas = nullptr;
  QFrame *m_topToolbar = nullptr;
  QFrame *m_optionsBar = nullptr;
  QFrame *m_bottomBar = nullptr;
  QPushButton *m_fileBtn = nullptr;
  QPushButton *m_undoBtn = nullptr;
  QPushButton *m_redoBtn = nullptr;
  QPushButton *m_deleteBtn = nullptr;
  QPushButton *m_effectsBtn = nullptr;
  QPushButton *m_strokeBtn = nullptr;
  QPushButton *m_fillBtn = nullptr;
  QPushButton *m_zoomLabel = nullptr;
  QLabel *m_activeToolLabel = nullptr;
  QLabel *m_thicknessLabel = nullptr;
  QList<QPushButton *> m_toolButtons;
  QList<QPushButton *> m_iconButtons;
  QAction *m_menuUndo = nullptr;
  QAction *m_menuRedo = nullptr;
};

// ---------------------------------------------------------------------------
// 2. Recording Page (matches RecordingView.axaml + RecordingToolbarView.axaml)
// ---------------------------------------------------------------------------
class RecordingPage final : public QWidget {
public:
  RecordingPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    // Header
    auto *header = new QVBoxLayout;
    header->setSpacing(4);
    header->addWidget(makeLabel(trPage("Screen Recording"), "pageTitle", body));
    header->addWidget(makeLabel(trPage("Record your screen, region, or animated GIF using native Wayland/PipeWire capture."), "caption", body));
    layout->addLayout(header);

    // Recording Controls Card
    auto *controlsCard = makeCard(body, 20);
    auto *controlsLayout = qobject_cast<QVBoxLayout *>(controlsCard->layout());
    controlsLayout->addWidget(makeLabel(trPage("Recording Controls"), "sectionTitle", controlsCard));

    // Inline Recording Toolbar matching RecordingToolbarView.axaml
    auto *toolbar = new QFrame(controlsCard);
    toolbar->setProperty("card", true);
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(14, 10, 14, 10);
    tb->setSpacing(12);

    m_dot = new QLabel(toolbar);
    m_dot->setFixedSize(12, 12);
    tb->addWidget(m_dot);

    m_duration = new QLabel(trPage("00:00:00"), toolbar);
    m_duration->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 600;"));
    m_duration->setMinimumWidth(74);
    tb->addWidget(m_duration);

    m_stateLabel = makeLabel(trPage("Idle"), "caption", toolbar);
    m_stateLabel->setMinimumWidth(68);
    tb->addWidget(m_stateLabel);

    auto *sep = new QFrame(toolbar);
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedHeight(22);
    tb->addWidget(sep);

    auto addCmdBtn = [&](const QString &cmdId, const QString &label, bool accent = false) {
      auto *btn = new QPushButton(label, toolbar);
      btn->setAccessibleName(label);
      if (accent) btn->setProperty("accent", true);
      connect(btn, &QPushButton::clicked, this, [this, cmdId] {
        if (auto *a = m_ctx.commands.action(cmdId)) a->trigger();
      });
      tb->addWidget(btn);
      return btn;
    };

    m_startBtn = addCmdBtn(QStringLiteral("start-recording"), trPage("Start"), true);
    m_regionBtn = addCmdBtn(QStringLiteral("start-region-recording"), trPage("Region"));
    m_gifBtn = addCmdBtn(QStringLiteral("start-gif-recording"), trPage("GIF"));
    m_pauseBtn = addCmdBtn(QStringLiteral("pause-recording"), trPage("Pause"));
    m_stopBtn = addCmdBtn(QStringLiteral("stop-recording"), trPage("Stop"), true);
    m_abortBtn = addCmdBtn(QStringLiteral("abort-recording"), trPage("Abort"));
    m_reviewBtn = addCmdBtn(QStringLiteral("review-recording"), trPage("Review"));
    tb->addStretch();
    controlsLayout->addWidget(toolbar);

    m_statusInfo = makeLabel(trPage("Ready to record."), "caption", controlsCard);
    controlsLayout->addWidget(m_statusInfo);
    layout->addWidget(controlsCard);

    // Recording Settings Card
    auto *settingsCard = makeCard(body, 20);
    auto *settingsLayout = qobject_cast<QVBoxLayout *>(settingsCard->layout());
    settingsLayout->addWidget(makeLabel(trPage("Recording Settings"), "sectionTitle", settingsCard));

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(12);

    m_fpsCombo = new QComboBox(settingsCard);
    m_fpsCombo->setAccessibleName(trPage("Frame Rate"));
    for (int fps : {15, 24, 30, 60}) m_fpsCombo->addItem(QString::number(fps), fps);
    form->addRow(trPage("Frame Rate (FPS):"), m_fpsCombo);

    m_systemAudio = new QCheckBox(trPage("Capture system audio output"), settingsCard);
    m_systemAudio->setAccessibleName(trPage("Capture system audio output"));
    form->addRow(trPage("System Audio:"), m_systemAudio);

    m_micAudio = new QCheckBox(trPage("Capture microphone input"), settingsCard);
    m_micAudio->setAccessibleName(trPage("Capture microphone input"));
    form->addRow(trPage("Microphone:"), m_micAudio);

    settingsLayout->addLayout(form);

    auto *saveRecBtn = new QPushButton(trPage("Apply Recording Settings"), settingsCard);
    saveRecBtn->setAccessibleName(trPage("Apply recording settings"));
    connect(saveRecBtn, &QPushButton::clicked, this, &RecordingPage::saveSettings);
    settingsLayout->addWidget(saveRecBtn, 0, Qt::AlignLeft);

    const QString mp4Diag = m_ctx.recording.unavailable(xerahs::recording::Format::Mp4);
    const QString webmDiag = m_ctx.recording.unavailable(xerahs::recording::Format::WebM);
    const QString encoderSummary = mp4Diag.isEmpty() ? trPage("Native PipeWire / GStreamer H.264 encoder available.")
        : webmDiag.isEmpty() ? trPage("Native PipeWire / GStreamer WebM encoder available.")
                             : trPage("Encoder status: %1").arg(mp4Diag);
    settingsLayout->addWidget(makeLabel(encoderSummary, "caption", settingsCard));
    layout->addWidget(settingsCard);

    // How to Record Card
    auto *howCard = makeCard(body, 20);
    auto *howLayout = qobject_cast<QVBoxLayout *>(howCard->layout());
    howLayout->addWidget(makeLabel(trPage("How to Record"), "sectionTitle", howCard));
    howLayout->addWidget(makeLabel(trPage("1. Click 'Start' to begin recording your screen, or 'Region' to pick an area."), nullptr, howCard));
    howLayout->addWidget(makeLabel(trPage("2. Perform the actions you want to capture."), nullptr, howCard));
    howLayout->addWidget(makeLabel(trPage("3. Click 'Stop' to end recording and save the video."), nullptr, howCard));
    layout->addWidget(howCard);

    layout->addStretch();
    outer->addWidget(wrapScroll(body, this, trPage("Screen recording page")));

    m_tick = new QTimer(this);
    m_tick->setInterval(250);
    connect(m_tick, &QTimer::timeout, this, &RecordingPage::updateClock);
    connect(&m_ctx.recording, &RecordingController::changed, this, &RecordingPage::refreshState);
    loadSettings();
    refreshState();
  }

private:
  void loadSettings() {
    const auto vals = m_ctx.settings.current().values;
    const int fps = vals.value(QStringLiteral("recordingFrameRate")).toInt(30);
    const int idx = m_fpsCombo->findData(fps);
    if (idx >= 0) m_fpsCombo->setCurrentIndex(idx);
    m_systemAudio->setChecked(vals.value(QStringLiteral("recordSystemAudio")).toBool(false));
    m_micAudio->setChecked(vals.value(QStringLiteral("recordMicrophone")).toBool(false));
  }

  void saveSettings() {
    QJsonObject next = m_ctx.settings.current().values;
    next.insert(QStringLiteral("recordingFrameRate"), m_fpsCombo->currentData().toInt());
    next.insert(QStringLiteral("recordSystemAudio"), m_systemAudio->isChecked());
    next.insert(QStringLiteral("recordMicrophone"), m_micAudio->isChecked());
    const QString err = m_ctx.settings.save(next);
    m_statusInfo->setText(err.isEmpty() ? trPage("Recording settings saved.") : err);
  }

  void updateClock() {
    const qint64 secs = m_elapsed.isValid() ? m_elapsed.elapsed() / 1000 : 0;
    const int h = int(secs / 3600);
    const int m = int((secs % 3600) / 60);
    const int s = int(secs % 60);
    m_duration->setText(QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0')));
  }

  void refreshState() {
    const bool busy = m_ctx.recording.busy();
    const auto st = m_ctx.recording.state();
    const bool paused = (st == xerahs::recording::State::Paused);
    m_dot->setStyleSheet(busy
        ? QStringLiteral("background-color: %1; border-radius: 6px;").arg(paused ? QStringLiteral("#F59E0B") : QStringLiteral("#DC2626"))
        : QStringLiteral("background-color: #6B7280; border-radius: 6px;"));
    m_stateLabel->setText(paused ? trPage("Paused") : busy ? trPage("Recording") : trPage("Idle"));

    if (busy && !m_tick->isActive()) {
      m_elapsed.restart();
      m_tick->start();
    } else if (!busy && m_tick->isActive()) {
      m_tick->stop();
      m_duration->setText(trPage("00:00:00"));
    }

    auto syncEnabled = [&](QPushButton *btn, const QString &cmdId) {
      auto *a = m_ctx.commands.action(cmdId);
      btn->setEnabled(a && a->isEnabled());
    };
    syncEnabled(m_startBtn, QStringLiteral("start-recording"));
    syncEnabled(m_regionBtn, QStringLiteral("start-region-recording"));
    syncEnabled(m_gifBtn, QStringLiteral("start-gif-recording"));
    syncEnabled(m_pauseBtn, QStringLiteral("pause-recording"));
    syncEnabled(m_stopBtn, QStringLiteral("stop-recording"));
    syncEnabled(m_abortBtn, QStringLiteral("abort-recording"));
    syncEnabled(m_reviewBtn, QStringLiteral("review-recording"));

    m_startBtn->setVisible(!busy);
    m_regionBtn->setVisible(!busy);
    m_gifBtn->setVisible(!busy);
    m_pauseBtn->setVisible(busy);
    m_stopBtn->setVisible(busy);
    m_abortBtn->setVisible(busy);
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QLabel *m_dot = nullptr;
  QLabel *m_duration = nullptr;
  QLabel *m_stateLabel = nullptr;
  QLabel *m_statusInfo = nullptr;
  QPushButton *m_startBtn = nullptr;
  QPushButton *m_regionBtn = nullptr;
  QPushButton *m_gifBtn = nullptr;
  QPushButton *m_pauseBtn = nullptr;
  QPushButton *m_stopBtn = nullptr;
  QPushButton *m_abortBtn = nullptr;
  QPushButton *m_reviewBtn = nullptr;
  QComboBox *m_fpsCombo = nullptr;
  QCheckBox *m_systemAudio = nullptr;
  QCheckBox *m_micAudio = nullptr;
  QTimer *m_tick = nullptr;
  QElapsedTimer m_elapsed;
};

// ---------------------------------------------------------------------------
// 3. History Page (matches HistoryView.axaml)
// ---------------------------------------------------------------------------
class HistoryPage final : public QWidget {
public:
  HistoryPage(AppContext &ctx, ApplicationWindow &window, EmbeddedEditorPage *editorPage, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window), m_editorPage(editorPage) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Top Toolbar (HistoryView.axaml Row 0)
    auto *toolbar = new QFrame(this);
    toolbar->setProperty("headerBar", true);
    auto *tbLayout = new QVBoxLayout(toolbar);
    tbLayout->setContentsMargins(16, 12, 16, 12);
    tbLayout->setSpacing(8);

    auto *row1 = new QHBoxLayout;
    row1->setSpacing(6);
    m_search = new QLineEdit(toolbar);
    m_search->setPlaceholderText(trPage("Search history"));
    m_search->setAccessibleName(trPage("Search history"));
    m_search->setMinimumWidth(80);
    m_search->setClearButtonEnabled(true);
    row1->addWidget(m_search, 1);

    auto *searchBtn = new QPushButton(trPage("Search"), toolbar);
    searchBtn->setAccessibleName(trPage("Search history"));
    connect(searchBtn, &QPushButton::clicked, this, &HistoryPage::rebuildView);
    row1->addWidget(searchBtn);

    auto *refreshBtn = new QPushButton(trPage("Refresh"), toolbar);
    refreshBtn->setAccessibleName(trPage("Refresh history"));
    connect(refreshBtn, &QPushButton::clicked, this, &HistoryPage::reload);
    row1->addWidget(refreshBtn);

    m_toggleViewBtn = new QPushButton(trPage("List View"), toolbar);
    m_toggleViewBtn->setAccessibleName(trPage("Toggle grid or list view"));
    connect(m_toggleViewBtn, &QPushButton::clicked, this, [this] {
      m_gridMode = !m_gridMode;
      applyViewMode();
    });
    row1->addWidget(m_toggleViewBtn);

    m_countLabel = makeLabel(trPage("0 items"), "caption", toolbar);
    row1->addWidget(m_countLabel);
    tbLayout->addLayout(row1);

    // Secondary action row for selected item
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(6);
    auto addActionBtn = [&](const QString &label, const std::function<void()> &fn) {
      auto *btn = new QPushButton(label, toolbar);
      btn->setAccessibleName(label);
      connect(btn, &QPushButton::clicked, this, fn);
      row2->addWidget(btn);
      return btn;
    };
    m_openBtn = addActionBtn(trPage("Open"), [this] { openSelected(); });
    m_editBtn = addActionBtn(trPage("Edit"), [this] { editSelected(); });
    m_copyPathBtn = addActionBtn(trPage("Copy Path"), [this] { copySelectedPath(); });
    m_copyUrlBtn = addActionBtn(trPage("Copy URL"), [this] { copySelectedUrl(); });
    addActionBtn(trPage("Statistics"), [this] { m_window.navigateTo(QStringLiteral("Statistics")); });
    m_fullWindowBtn = addActionBtn(trPage("Window…"), [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("capture-history"))) a->trigger();
    });
    m_fullWindowBtn->setToolTip(trPage("Open full capture history window"));
    row2->addStretch();
    tbLayout->addLayout(row2);
    root->addWidget(toolbar);

    // List Header (shown only in List View)
    m_listHeader = new QFrame(this);
    m_listHeader->setProperty("headerBar", true);
    auto *lh = new QHBoxLayout(m_listHeader);
    lh->setContentsMargins(16, 6, 16, 6);
    lh->addWidget(makeLabel(trPage("Filename"), "caption", m_listHeader), 3);
    lh->addWidget(makeLabel(trPage("Source / Destination"), "caption", m_listHeader), 2);
    lh->addWidget(makeLabel(trPage("Date"), "caption", m_listHeader), 1);
    m_listHeader->hide();
    root->addWidget(m_listHeader);

    // Main Items View
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("shellHistoryList"));
    m_list->setAccessibleName(trPage("History items"));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setSpacing(8);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &HistoryPage::syncActionAvailability);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { editSelected(); });
    root->addWidget(m_list, 1);

    // Pagination Footer (HistoryView.axaml Row 3)
    auto *footer = new QFrame(this);
    footer->setProperty("footerBar", true);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(16, 10, 16, 10);
    fl->setSpacing(16);
    fl->addStretch();
    m_prevBtn = new QPushButton(trPage("Previous"), footer);
    m_prevBtn->setAccessibleName(trPage("Previous page"));
    connect(m_prevBtn, &QPushButton::clicked, this, [this] { if (m_pageIndex > 0) { --m_pageIndex; rebuildView(); } });
    fl->addWidget(m_prevBtn);
    m_pageInfo = makeLabel(trPage("Page 1 of 1"), "caption", footer);
    fl->addWidget(m_pageInfo);
    m_nextBtn = new QPushButton(trPage("Next"), footer);
    m_nextBtn->setAccessibleName(trPage("Next page"));
    connect(m_nextBtn, &QPushButton::clicked, this, [this] { ++m_pageIndex; rebuildView(); });
    fl->addWidget(m_nextBtn);
    fl->addStretch();
    root->addWidget(footer);

    connect(m_search, &QLineEdit::textChanged, this, [this] { m_pageIndex = 0; rebuildView(); });
    connect(&m_ctx.capture, &CaptureController::busyChanged, this, [this](bool busy) { if (!busy) reload(); });
    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("History")) reload();
    });
    connect(&m_window, &ApplicationWindow::themeChanged, this, [this](bool) { applyViewMode(); });

    applyViewMode();
    syncActionAvailability();
  }

  void reload() {
    m_allItems.clear();
    if (m_ctx.journal) {
      const auto listing = m_ctx.journal->list();
      std::vector<xerahs::actions::RecoveredPipelineJournal> recs;
      recs.reserve(listing.entries.size());
      for (const auto &entry : listing.entries)
        if (entry.parsed.record) recs.push_back(*entry.parsed.record);
      m_allItems = xerahs::history::captureHistoryItems(recs);
    }
    m_pageIndex = 0;
    rebuildView();
  }

private:
  static constexpr int kPageSize = 24;

  void applyViewMode() {
    const QColor fg = currentIconColor(m_window);
    if (m_gridMode) {
      m_toggleViewBtn->setText(trPage("List View"));
      m_toggleViewBtn->setIcon(appIcon(AppIcon::List, fg, 15));
      m_listHeader->hide();
      m_list->setViewMode(QListView::IconMode);
      m_list->setIconSize(QSize(160, 110));
      m_list->setGridSize(QSize(192, 174));
    } else {
      m_toggleViewBtn->setText(trPage("Grid View"));
      m_toggleViewBtn->setIcon(appIcon(AppIcon::LayoutGrid, fg, 15));
      m_listHeader->show();
      m_list->setViewMode(QListView::ListMode);
      m_list->setIconSize(QSize(36, 36));
      m_list->setGridSize(QSize());
    }
  }

  void rebuildView() {
    const QString query = m_search->text().trimmed();
    m_filtered.clear();
    for (const auto &item : m_allItems) {
      if (query.isEmpty() || xerahs::history::matchesHistoryQuery(item, query)) m_filtered.push_back(item);
    }
    const int total = int(m_filtered.size());
    const int totalPages = std::max(1, (total + kPageSize - 1) / kPageSize);
    m_pageIndex = std::clamp(m_pageIndex, 0, totalPages - 1);
    const int start = m_pageIndex * kPageSize;
    const int end = std::min(total, start + kPageSize);

    m_list->clear();
    for (int i = start; i < end; ++i) {
      const auto &item = m_filtered[size_t(i)];
      const QString path = item.files.empty() ? QString{} : item.files.front().path;
      const QString name = path.isEmpty() ? item.taskId : QFileInfo(path).fileName();
      const QString ts = item.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
      const QString label = m_gridMode
          ? QStringLiteral("%1\n%2").arg(name, ts)
          : QStringLiteral("%1   |   %2   |   %3").arg(name, item.destinationName.isEmpty() ? item.captureSource : item.destinationName, ts);
      auto *row = new QListWidgetItem(label, m_list);
      row->setData(Qt::UserRole, i);
      if (!path.isEmpty() && QFileInfo::exists(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize sz = reader.size();
        if (sz.isValid() && qint64(sz.width()) * sz.height() <= 67108864) {
          reader.setScaledSize(sz.scaled(160, 110, Qt::KeepAspectRatio).expandedTo(QSize(1, 1)));
          const QImage img = reader.read();
          if (!img.isNull()) row->setIcon(QIcon(QPixmap::fromImage(img)));
        }
      }
      if (row->icon().isNull()) {
        row->setIcon(appIcon(AppIcon::ImageIcon, currentIconColor(m_window), 36));
      }
    }

    m_countLabel->setText(trPage("%1 items").arg(total));
    m_pageInfo->setText(trPage("Page %1 of %2").arg(m_pageIndex + 1).arg(totalPages));
    m_prevBtn->setEnabled(m_pageIndex > 0);
    m_nextBtn->setEnabled(m_pageIndex + 1 < totalPages);
    syncActionAvailability();
  }

  const xerahs::history::CaptureHistoryItem *selectedItem() const {
    auto *item = m_list->currentItem();
    if (!item) return nullptr;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= int(m_filtered.size())) return nullptr;
    return &m_filtered[size_t(idx)];
  }

  void syncActionAvailability() {
    const auto *item = selectedItem();
    const bool hasFile = item && !item->files.empty() && QFileInfo::exists(item->files.front().path);
    m_openBtn->setEnabled(hasFile);
    m_editBtn->setEnabled(hasFile);
    m_copyPathBtn->setEnabled(item && !item->files.empty());
    m_copyUrlBtn->setEnabled(item && !item->resultUrl.isEmpty());
  }

  void openSelected() {
    const auto *item = selectedItem();
    if (!item || item->files.empty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(item->files.front().path));
  }

  void editSelected() {
    const auto *item = selectedItem();
    if (!item || item->files.empty()) return;
    const QString path = item->files.front().path;
    if (!QFileInfo::exists(path)) return;
    if (m_editorPage) {
      m_editorPage->loadImageFile(path);
      m_window.navigateTo(QStringLiteral("Editor"));
    } else if (auto *win = EditorWindow::open(path, &m_window)) {
      win->show();
    }
  }

  void copySelectedPath() {
    const auto *item = selectedItem();
    if (!item || item->files.empty()) return;
    QApplication::clipboard()->setText(item->files.front().path);
    m_window.showResult(trPage("History"), trPage("Copied file path."), true);
  }

  void copySelectedUrl() {
    const auto *item = selectedItem();
    if (!item || item->resultUrl.isEmpty()) return;
    QApplication::clipboard()->setText(item->resultUrl);
    m_window.showResult(trPage("History"), trPage("Copied URL."), true);
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  EmbeddedEditorPage *m_editorPage = nullptr;
  QLineEdit *m_search = nullptr;
  QPushButton *m_toggleViewBtn = nullptr;
  QPushButton *m_openBtn = nullptr;
  QPushButton *m_editBtn = nullptr;
  QPushButton *m_copyPathBtn = nullptr;
  QPushButton *m_copyUrlBtn = nullptr;
  QPushButton *m_fullWindowBtn = nullptr;
  QPushButton *m_prevBtn = nullptr;
  QPushButton *m_nextBtn = nullptr;
  QFrame *m_listHeader = nullptr;
  QListWidget *m_list = nullptr;
  QLabel *m_countLabel = nullptr;
  QLabel *m_pageInfo = nullptr;
  std::vector<xerahs::history::CaptureHistoryItem> m_allItems;
  std::vector<xerahs::history::CaptureHistoryItem> m_filtered;
  bool m_gridMode = true;
  int m_pageIndex = 0;
};

// ---------------------------------------------------------------------------
// 4. Statistics Page (matches StatisticsView.axaml)
// ---------------------------------------------------------------------------
class StatsSplitBarWidget final : public QWidget {
public:
  explicit StatsSplitBarWidget(const ApplicationWindow &window, QWidget *parent = nullptr)
      : QWidget(parent), m_window(window) { setFixedHeight(14); }
  void setCounts(int images, int recordings, int other) {
    m_images = images; m_recordings = recordings; m_other = other; update();
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = rect().adjusted(0, 1, 0, -1);
    p.setPen(Qt::NoPen);
    const bool hc = isHighContrastSystemPalette(QApplication::palette());
    const QColor track = hc ? palette().color(QPalette::Mid)
                            : (m_window.isDarkTheme() ? QColor(0x2D, 0x35, 0x42) : QColor(0xE2, 0xE7, 0xEF));
    p.setBrush(track);
    p.drawRoundedRect(r, 6, 6);
    const int total = m_images + m_recordings + m_other;
    if (total <= 0) return;
    double x = r.left();
    auto drawSeg = [&](int count, const QColor &color) {
      if (count <= 0) return;
      const double w = r.width() * (double(count) / double(total));
      p.setBrush(color);
      p.drawRoundedRect(QRectF(x, r.top(), std::max(4.0, w), r.height()), 4, 4);
      x += w;
    };
    drawSeg(m_images, QColor(0x3E, 0x83, 0xF2));
    drawSeg(m_recordings, QColor(0x10, 0xB9, 0x81));
    drawSeg(m_other, QColor(0xF5, 0x9E, 0x0B));
  }
private:
  const ApplicationWindow &m_window;
  int m_images = 0, m_recordings = 0, m_other = 0;
};

class StatsHeatmapWidget final : public QWidget {
public:
  explicit StatsHeatmapWidget(const ApplicationWindow &window, QWidget *parent = nullptr)
      : QWidget(parent), m_window(window) { setMinimumHeight(106); setMinimumWidth(0); }
  void setDailyCounts(QMap<QDate, int> counts) { m_counts = std::move(counts); update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int cell = 11, gap = 3;
    const int weeks = std::clamp(width() / (cell + gap), 12, 36);
    const QDate today = QDate::currentDate();
    const QDate start = today.addDays(-(weeks * 7 - 1));
    int maxVal = 1;
    for (auto v : m_counts) maxVal = std::max(maxVal, v);
    const bool hc = isHighContrastSystemPalette(QApplication::palette());
    const QColor empty = hc ? palette().color(QPalette::AlternateBase)
                            : (m_window.isDarkTheme() ? QColor(0x2D, 0x35, 0x42) : QColor(0xE2, 0xE7, 0xEF));
    const QColor accent(0x3E, 0x83, 0xF2);
    for (int w = 0; w < weeks; ++w) {
      for (int d = 0; d < 7; ++d) {
        const QDate day = start.addDays(w * 7 + d);
        if (day > today) continue;
        const int count = m_counts.value(day, 0);
        QColor fill = empty;
        if (count > 0) {
          const double ratio = double(count) / double(maxVal);
          fill = accent;
          fill.setAlpha(int(75 + ratio * 180));
        }
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(w * (cell + gap), d * (cell + gap), cell, cell), 2.5, 2.5);
      }
    }
  }
private:
  const ApplicationWindow &m_window;
  QMap<QDate, int> m_counts;
};

class StatsHourChartWidget final : public QWidget {
public:
  explicit StatsHourChartWidget(const ApplicationWindow &window, QWidget *parent = nullptr)
      : QWidget(parent), m_window(window) { setMinimumHeight(150); }
  void setHours(std::array<int, 24> hours) { m_hours = hours; update(); }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int maxV = std::max(1, *std::max_element(m_hours.begin(), m_hours.end()));
    const int bottomPad = 20;
    const double usableH = std::max(10.0, height() - bottomPad - 8.0);
    const double slotW = double(width()) / 24.0;
    const bool hc = isHighContrastSystemPalette(QApplication::palette());
    const auto tokens = appThemePalette(m_window.isDarkTheme());
    const QColor accent(0x3E, 0x83, 0xF2);
    const QColor base = hc ? palette().color(QPalette::AlternateBase)
                           : (m_window.isDarkTheme() ? QColor(0x2D, 0x35, 0x42) : QColor(0xE2, 0xE7, 0xEF));
    const QColor axisText = hc ? palette().color(QPalette::WindowText) : tokens.textSecondary;
    for (int h = 0; h < 24; ++h) {
      const double barW = std::max(4.0, slotW - 6.0);
      const double x = h * slotW + (slotW - barW) / 2.0;
      const double frac = double(m_hours[size_t(h)]) / double(maxV);
      const double barH = m_hours[size_t(h)] > 0 ? std::max(4.0, usableH * frac) : 3.0;
      p.setPen(Qt::NoPen);
      p.setBrush(m_hours[size_t(h)] > 0 ? accent : base);
      p.drawRoundedRect(QRectF(x, height() - bottomPad - barH, barW, barH), 3, 3);
      if (h % 3 == 0) {
        p.setPen(axisText);
        p.drawText(QRectF(h * slotW, height() - bottomPad + 2, slotW * 3, 16), Qt::AlignLeft, QStringLiteral("%1h").arg(h));
      }
    }
  }
private:
  const ApplicationWindow &m_window;
  std::array<int, 24> m_hours{};
};

class StatisticsPage final : public QWidget {
public:
  StatisticsPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    // Title Row
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    titleRow->addWidget(makeLabel(trPage("Statistics"), "pageTitleAccent", body));
    m_subtitle = makeLabel(trPage("All recorded captures"), "caption", body);
    titleRow->addWidget(m_subtitle, 1);
    auto *refreshBtn = new QPushButton(trPage("Refresh"), body);
    refreshBtn->setAccessibleName(trPage("Refresh statistics"));
    connect(refreshBtn, &QPushButton::clicked, this, &StatisticsPage::recompute);
    titleRow->addWidget(refreshBtn);
    layout->addLayout(titleRow);

    // Total Card
    auto *totalCard = makeCard(body, 16);
    auto *totalGrid = new QHBoxLayout;
    totalGrid->setSpacing(24);
    auto *totalLeft = new QVBoxLayout;
    totalLeft->addWidget(makeLabel(trPage("Total"), "sectionTitle", totalCard));
    m_totalValue = makeLabel(trPage("0"), "statValue", totalCard);
    totalLeft->addWidget(m_totalValue);
    m_rateLabel = makeLabel(trPage("0 per day"), "caption", totalCard);
    totalLeft->addWidget(m_rateLabel);
    totalGrid->addLayout(totalLeft);

    auto *totalRight = new QVBoxLayout;
    totalRight->setSpacing(10);
    m_splitBar = new StatsSplitBarWidget(m_window, totalCard);
    totalRight->addWidget(m_splitBar);
    m_legendLabel = makeLabel(trPage("Images: 0  ·  Recordings: 0  ·  Other: 0"), "caption", totalCard);
    totalRight->addWidget(m_legendLabel);
    totalGrid->addLayout(totalRight, 1);
    qobject_cast<QVBoxLayout *>(totalCard->layout())->addLayout(totalGrid);
    layout->addWidget(totalCard);

    // 4 Highlight Cards
    auto *hiRow = new QHBoxLayout;
    hiRow->setSpacing(10);
    auto addHighlight = [&](const QString &title, QLabel *&valueOut) {
      auto *c = makeCard(body, 14);
      auto *cl = qobject_cast<QVBoxLayout *>(c->layout());
      cl->addWidget(makeLabel(title, "sectionTitle", c));
      valueOut = makeLabel(trPage("0"), "statValue", c);
      cl->addWidget(valueOut);
      hiRow->addWidget(c, 1);
    };
    addHighlight(trPage("Today"), m_todayVal);
    addHighlight(trPage("Past 7 days"), m_weekVal);
    addHighlight(trPage("Streak"), m_streakVal);
    addHighlight(trPage("Busiest day"), m_busiestVal);
    layout->addLayout(hiRow);

    // Every Day Heatmap Card
    auto *calCard = makeCard(body, 16);
    auto *calL = qobject_cast<QVBoxLayout *>(calCard->layout());
    calL->addWidget(makeLabel(trPage("Every day"), "sectionTitle", calCard));
    m_heatmap = new StatsHeatmapWidget(m_window, calCard);
    calL->addWidget(m_heatmap);
    layout->addWidget(calCard);

    // Time of Day Card
    auto *hourCard = makeCard(body, 16);
    auto *hourL = qobject_cast<QVBoxLayout *>(hourCard->layout());
    hourL->addWidget(makeLabel(trPage("Time of day"), "sectionTitle", hourCard));
    m_hourChart = new StatsHourChartWidget(m_window, hourCard);
    hourL->addWidget(m_hourChart);
    layout->addWidget(hourCard);

    // Breakdown Cards
    auto *breakRow = new QHBoxLayout;
    breakRow->setSpacing(10);
    auto *destCard = makeCard(body, 16);
    qobject_cast<QVBoxLayout *>(destCard->layout())->addWidget(makeLabel(trPage("Destinations"), "sectionTitle", destCard));
    m_destSummary = makeLabel(trPage("No data yet"), "caption", destCard);
    qobject_cast<QVBoxLayout *>(destCard->layout())->addWidget(m_destSummary);
    breakRow->addWidget(destCard, 1);

    auto *typeCard = makeCard(body, 16);
    qobject_cast<QVBoxLayout *>(typeCard->layout())->addWidget(makeLabel(trPage("File types"), "sectionTitle", typeCard));
    m_typeSummary = makeLabel(trPage("No data yet"), "caption", typeCard);
    qobject_cast<QVBoxLayout *>(typeCard->layout())->addWidget(m_typeSummary);
    breakRow->addWidget(typeCard, 1);
    layout->addLayout(breakRow);

    layout->addStretch();
    outer->addWidget(wrapScroll(body, this, trPage("Statistics page")));

    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("Statistics")) recompute();
    });
    connect(&m_window, &ApplicationWindow::themeChanged, this, [this](bool) {
      m_splitBar->update();
      m_heatmap->update();
      m_hourChart->update();
    });
  }

  void recompute() {
    std::vector<xerahs::history::CaptureHistoryItem> items;
    if (m_ctx.journal) {
      const auto listing = m_ctx.journal->list();
      std::vector<xerahs::actions::RecoveredPipelineJournal> recs;
      recs.reserve(listing.entries.size());
      for (const auto &entry : listing.entries)
        if (entry.parsed.record) recs.push_back(*entry.parsed.record);
      items = xerahs::history::captureHistoryItems(recs);
    }

    int images = 0, recordings = 0, other = 0;
    int todayCount = 0, weekCount = 0;
    const QDate today = QDate::currentDate();
    QMap<QDate, int> daily;
    std::array<int, 24> hours{};
    QMap<QString, int> destCounts;
    QMap<QString, int> extCounts;

    for (const auto &it : items) {
      const QDateTime dt = it.createdAt.toLocalTime();
      const QDate d = dt.date();
      if (d.isValid()) {
        daily[d] += 1;
        if (d == today) ++todayCount;
        if (d.daysTo(today) >= 0 && d.daysTo(today) < 7) ++weekCount;
        hours[size_t(std::clamp(dt.time().hour(), 0, 23))] += 1;
      }
      QString ext;
      if (!it.files.empty()) ext = QFileInfo(it.files.front().path).suffix().toLower();
      if (ext == QLatin1String("mp4") || ext == QLatin1String("webm") || ext == QLatin1String("mkv")) ++recordings;
      else if (ext == QLatin1String("png") || ext == QLatin1String("jpg") || ext == QLatin1String("jpeg") || ext == QLatin1String("webp") || ext == QLatin1String("gif") || ext.isEmpty()) ++images;
      else ++other;

      if (!ext.isEmpty()) extCounts[ext.toUpper()] += 1;
      if (!it.destinationName.isEmpty()) destCounts[it.destinationName] += 1;
    }

    int streak = 0;
    for (QDate cursor = today; daily.value(cursor, 0) > 0; cursor = cursor.addDays(-1)) ++streak;
    int busiestCount = 0;
    for (auto v : daily) busiestCount = std::max(busiestCount, v);

    const int total = int(items.size());
    m_totalValue->setText(QString::number(total));
    m_rateLabel->setText(trPage("%1 active days").arg(daily.size()));
    m_splitBar->setCounts(images, recordings, other);
    m_legendLabel->setText(trPage("Images: %1  ·  Recordings: %2  ·  Other: %3").arg(images).arg(recordings).arg(other));
    m_todayVal->setText(QString::number(todayCount));
    m_weekVal->setText(QString::number(weekCount));
    m_streakVal->setText(trPage("%1 d").arg(streak));
    m_busiestVal->setText(QString::number(busiestCount));
    m_heatmap->setDailyCounts(daily);
    m_hourChart->setHours(hours);

    auto formatMap = [](const QMap<QString, int> &m) {
      if (m.isEmpty()) return trPage("No data yet");
      QStringList lines;
      for (auto it = m.cbegin(); it != m.cend(); ++it)
        lines << QStringLiteral("%1: %2").arg(it.key()).arg(it.value());
      return lines.join(QLatin1Char('\n'));
    };
    m_destSummary->setText(formatMap(destCounts));
    m_typeSummary->setText(formatMap(extCounts));
  }

private:
  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QLabel *m_subtitle = nullptr;
  QLabel *m_totalValue = nullptr;
  QLabel *m_rateLabel = nullptr;
  QLabel *m_legendLabel = nullptr;
  QLabel *m_todayVal = nullptr;
  QLabel *m_weekVal = nullptr;
  QLabel *m_streakVal = nullptr;
  QLabel *m_busiestVal = nullptr;
  QLabel *m_destSummary = nullptr;
  QLabel *m_typeSummary = nullptr;
  StatsSplitBarWidget *m_splitBar = nullptr;
  StatsHeatmapWidget *m_heatmap = nullptr;
  StatsHourChartWidget *m_hourChart = nullptr;
};

// ---------------------------------------------------------------------------
// 5. Workflows Page (matches WorkflowsView.axaml)
// ---------------------------------------------------------------------------
class WorkflowsPage final : public QWidget {
public:
  WorkflowsPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Left Sidebar
    auto *sidebar = new QFrame(this);
    sidebar->setProperty("card", true);
    sidebar->setFixedWidth(186);
    auto *sb = new QVBoxLayout(sidebar);
    sb->setContentsMargins(14, 16, 14, 16);
    sb->setSpacing(12);
    sb->addWidget(makeLabel(trPage("Workflows"), "sectionTitle", sidebar));

    auto *addBtn = new QPushButton(trPage("Manage Workflows…"), sidebar);
    addBtn->setAccessibleName(trPage("Add or manage workflows"));
    connect(addBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("capture-workflows"))) a->trigger();
    });
    sb->addWidget(addBtn);

    m_list = new QListWidget(sidebar);
    m_list->setAccessibleName(trPage("Configured workflows"));
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { showSelected(); });
    sb->addWidget(m_list, 1);

    auto *enableShortcuts = new QPushButton(trPage("Enable Global Shortcuts"), sidebar);
    enableShortcuts->setAccessibleName(trPage("Enable global workflow shortcuts"));
    connect(enableShortcuts, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("enable-workflow-shortcuts"))) a->trigger();
    });
    sb->addWidget(enableShortcuts);
    root->addWidget(sidebar);

    // Right Detail Panel
    auto *detail = new QWidget(this);
    auto *dl = new QVBoxLayout(detail);
    dl->setContentsMargins(24, 24, 24, 24);
    dl->setSpacing(16);

    auto *header = new QHBoxLayout;
    m_title = makeLabel(trPage("Workflows"), "pageTitle", detail);
    header->addWidget(m_title, 1);

    m_runBtn = new QPushButton(trPage("Run Workflow"), detail);
    m_runBtn->setProperty("accent", true);
    m_runBtn->setAccessibleName(trPage("Run selected workflow"));
    connect(m_runBtn, &QPushButton::clicked, this, &WorkflowsPage::runSelected);
    header->addWidget(m_runBtn);

    auto *editBtn = new QPushButton(trPage("Edit…"), detail);
    editBtn->setAccessibleName(trPage("Edit workflows"));
    connect(editBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("capture-workflows"))) a->trigger();
    });
    header->addWidget(editBtn);
    dl->addLayout(header);

    auto *infoCard = makeCard(detail, 20);
    auto *form = new QFormLayout;
    form->setSpacing(10);
    m_captureSource = makeLabel(trPage("—"), nullptr, infoCard);
    m_actions = makeLabel(trPage("—"), nullptr, infoCard);
    m_directory = makeLabel(trPage("—"), nullptr, infoCard);
    m_pattern = makeLabel(trPage("—"), nullptr, infoCard);
    m_defaultBadge = makeLabel(trPage("—"), "caption", infoCard);
    form->addRow(trPage("Capture Source:"), m_captureSource);
    form->addRow(trPage("After-Capture Actions:"), m_actions);
    form->addRow(trPage("Save Folder:"), m_directory);
    form->addRow(trPage("Filename Pattern:"), m_pattern);
    form->addRow(trPage("Status:"), m_defaultBadge);
    qobject_cast<QVBoxLayout *>(infoCard->layout())->addLayout(form);
    dl->addWidget(infoCard);
    dl->addStretch();

    root->addWidget(detail, 1);

    if (m_ctx.workflows) {
      connect(m_ctx.workflows, &NativeCaptureWorkflows::changed, this, &WorkflowsPage::reload);
    }
    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("Workflows")) reload();
    });
    reload();
  }

  void reload() {
    m_entries.clear();
    m_defaultId = QUuid();
    m_list->clear();
    if (m_ctx.workflows && m_ctx.workflows->store().current().catalog) {
      const auto &cat = *m_ctx.workflows->store().current().catalog;
      m_defaultId = cat.defaultId;
      for (const auto &entry : cat.entries) {
        m_entries.push_back(entry);
        const QString suffix = entry.id == m_defaultId ? trPage(" (Default)") : QString{};
        m_list->addItem(entry.name + suffix);
      }
    }
    if (!m_entries.empty()) m_list->setCurrentRow(0);
    showSelected();
  }

private:
  void showSelected() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= int(m_entries.size())) {
      m_title->setText(trPage("No workflows configured"));
      m_runBtn->setEnabled(false);
      return;
    }
    const auto &wf = m_entries[size_t(row)];
    m_title->setText(wf.name);
    m_runBtn->setEnabled(true);
    m_captureSource->setText(wf.capture.isEmpty() ? trPage("Region") : wf.capture);
    m_actions->setText(wf.actions.isEmpty() ? trPage("None") : wf.actions.join(QStringLiteral(", ")));
    m_directory->setText(wf.output.directory.isEmpty() ? trPage("Profile default") : wf.output.directory);
    m_pattern->setText(wf.output.pattern.isEmpty() ? trPage("Profile default") : wf.output.pattern);
    m_defaultBadge->setText(wf.id == m_defaultId ? trPage("Default workflow") : trPage("Custom workflow"));
  }

  void runSelected() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= int(m_entries.size()) || !m_ctx.invoke) return;
    m_ctx.invoke(xerahs::shell::ActivationAction::RunWorkflow, {}, false,
        m_entries[size_t(row)].id.toString(QUuid::WithoutBraces), QStringLiteral("native-shell"));
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QListWidget *m_list = nullptr;
  QLabel *m_title = nullptr;
  QPushButton *m_runBtn = nullptr;
  QLabel *m_captureSource = nullptr;
  QLabel *m_actions = nullptr;
  QLabel *m_directory = nullptr;
  QLabel *m_pattern = nullptr;
  QLabel *m_defaultBadge = nullptr;
  std::vector<xerahs::workflows::CaptureWorkflow> m_entries;
  QUuid m_defaultId;
};

// ---------------------------------------------------------------------------
// 6. Tools Page (matches ToolsView.axaml + quick launcher cards)
// ---------------------------------------------------------------------------
class ToolsPage final : public QWidget {
public:
  ToolsPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(24, 28, 24, 24);
    layout->setSpacing(18);

    // Header matching ToolsView.axaml
    m_heroIcon = new QLabel(body);
    m_heroIcon->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_heroIcon);

    auto *title = makeLabel(trPage("XerahS Tools"), "pageTitle", body);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *sub = makeLabel(trPage("Select a tool below or from the navigation menu to get started."), "caption", body);
    sub->setAlignment(Qt::AlignCenter);
    layout->addWidget(sub);

    auto *grid = new QGridLayout;
    grid->setSpacing(12);

    struct ToolTile { const char *cmdId; const char *name; const char *desc; AppIcon icon; };
    const std::array<ToolTile, 8> tiles{{
      {"pick-colour", "Color Picker", "Sample any pixel on screen and copy HEX, RGB, HSL, or CMYK values.", AppIcon::Palette},
      {"qr-code", "QR Code Generator", "Generate QR codes from text or URLs, save, copy, or upload.", AppIcon::QrCode},
      {"scan-qr", "Scan QR Codes", "Decode QR codes from images or screen captures locally.", AppIcon::ScanLine},
      {"recognize-text", "OCR (Recognize Text)", "Extract text from screenshots or local images with Tesseract.", AppIcon::FileText},
      {"hash-checker", "Hash Checker", "Compute and verify SHA-256, SHA-512, SHA-1, and MD5 checksums.", AppIcon::Hash},
      {"icon-converter", "Icon Converter", "Convert images into multi-resolution ICO icons.", AppIcon::ImageIcon},
      {"background-remover", "Background Remover", "Remove image backgrounds locally with verified ONNX models.", AppIcon::Scissors},
      {"video-converter", "Video Converter", "Transcode screen recordings between MP4, WebM, and GIF.", AppIcon::Film},
    }};

    for (size_t i = 0; i < tiles.size(); ++i) {
      const auto &tile = tiles[i];
      auto *card = makeCard(body, 14);
      auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
      cl->addWidget(makeLabel(trPage(tile.name), "sectionTitle", card));
      cl->addWidget(makeLabel(trPage(tile.desc), "caption", card));
      auto *openBtn = new QPushButton(trPage("Open %1").arg(trPage(tile.name)), card);
      openBtn->setAccessibleName(trPage("Open %1").arg(trPage(tile.name)));
      const QString cmdId = QString::fromLatin1(tile.cmdId);
      auto *action = m_ctx.commands.action(cmdId);
      openBtn->setEnabled(action && action->isEnabled());
      if (action && !action->toolTip().isEmpty()) openBtn->setToolTip(action->toolTip());
      connect(openBtn, &QPushButton::clicked, this, [this, cmdId] {
        if (auto *a = m_ctx.commands.action(cmdId)) a->trigger();
      });
      cl->addWidget(openBtn, 0, Qt::AlignLeft);
      grid->addWidget(card, int(i / 2), int(i % 2));
    }
    layout->addLayout(grid);
    layout->addStretch();
    outer->addWidget(wrapScroll(body, this, trPage("Tools page")));

    connect(&m_window, &ApplicationWindow::themeChanged, this, [this](bool) { updateHero(); });
    updateHero();
  }

private:
  void updateHero() {
    m_heroIcon->setPixmap(appIcon(AppIcon::WandSparkles, QColor(0x3E, 0x83, 0xF2), 56).pixmap(56, 56));
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QLabel *m_heroIcon = nullptr;
};

// ---------------------------------------------------------------------------
// 7. Application Settings Page (matches ApplicationSettingsView.axaml)
// ---------------------------------------------------------------------------
class ApplicationSettingsPage final : public QWidget {
public:
  ApplicationSettingsPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *tabStrip = new QFrame(this);
    tabStrip->setProperty("headerBar", true);
    auto *tabBarLayout = new QHBoxLayout(tabStrip);
    tabBarLayout->setContentsMargins(20, 8, 20, 8);
    tabBarLayout->setSpacing(6);
    root->addWidget(tabStrip);

    m_tabStack = new QStackedWidget(this);
    auto addSettingsTab = [&](QWidget *page, const QString &title) {
      const int idx = m_tabStack->addWidget(page);
      auto *btn = new QPushButton(title, tabStrip);
      btn->setAccessibleName(title);
      btn->setProperty("navRole", QStringLiteral("page"));
      btn->setCursor(Qt::PointingHandCursor);
      tabBarLayout->addWidget(btn);
      m_tabButtons.append(btn);
      connect(btn, &QPushButton::clicked, this, [this, idx] { selectTab(idx); });
    };

    // Tab 0: General
    auto *genBody = new QWidget(m_tabStack);
    auto *genLayout = new QVBoxLayout(genBody);
    genLayout->setContentsMargins(24, 20, 24, 20);
    genLayout->setSpacing(18);

    // Theme Card
    auto *themeCard = makeCard(genBody, 16);
    auto *tl = qobject_cast<QVBoxLayout *>(themeCard->layout());
    tl->addWidget(makeLabel(trPage("Theme"), "sectionTitle", themeCard));
    auto *themeRow = new QHBoxLayout;
    themeRow->addWidget(makeLabel(trPage("Application Theme:"), nullptr, themeCard));
    m_themeCombo = new QComboBox(themeCard);
    m_themeCombo->setAccessibleName(trPage("Application Theme"));
    m_themeCombo->addItem(trPage("System default"), int(ShellThemeMode::System));
    m_themeCombo->addItem(trPage("Dark"), int(ShellThemeMode::Dark));
    m_themeCombo->addItem(trPage("Light"), int(ShellThemeMode::Light));
    const auto syncThemeCombo = [this] {
      const QSignalBlocker blocker(m_themeCombo);
      const int idx = m_themeCombo->findData(int(m_window.themeMode()));
      if (idx >= 0) m_themeCombo->setCurrentIndex(idx);
    };
    syncThemeCombo();
    connect(&m_window, &ApplicationWindow::themeChanged, this, [syncThemeCombo](bool) { syncThemeCombo(); });
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
      m_window.setThemeMode(static_cast<ShellThemeMode>(m_themeCombo->currentData().toInt()));
    });
    themeRow->addWidget(m_themeCombo, 1);
    tl->addLayout(themeRow);
    tl->addWidget(makeLabel(trPage("'System default' follows your operating system's light/dark mode preference."), "caption", themeCard));
    genLayout->addWidget(themeCard);

    // Tray & Startup Card
    auto *trayCard = makeCard(genBody, 16);
    auto *trL = qobject_cast<QVBoxLayout *>(trayCard->layout());
    trL->addWidget(makeLabel(trPage("Tray Icon & Startup"), "sectionTitle", trayCard));
    m_showTray = new QCheckBox(trPage("Show tray icon"), trayCard);
    m_showTray->setAccessibleName(trPage("Show tray icon"));
    trL->addWidget(m_showTray);
    m_startAtLogin = new QCheckBox(trPage("Start XerahS at login"), trayCard);
    m_startAtLogin->setAccessibleName(trPage("Start XerahS at login"));
    trL->addWidget(m_startAtLogin);
    genLayout->addWidget(trayCard);

    // Notifications Card
    auto *notifCard = makeCard(genBody, 16);
    auto *nL = qobject_cast<QVBoxLayout *>(notifCard->layout());
    nL->addWidget(makeLabel(trPage("Notifications"), "sectionTitle", notifCard));
    m_showNotif = new QCheckBox(trPage("Show notification after capture"), notifCard);
    m_showNotif->setAccessibleName(trPage("Show notification after capture"));
    nL->addWidget(m_showNotif);
    m_playSound = new QCheckBox(trPage("Play capture sound"), notifCard);
    m_playSound->setAccessibleName(trPage("Play capture sound"));
    nL->addWidget(m_playSound);
    m_privateNotif = new QCheckBox(trPage("Hide private details in notifications"), notifCard);
    m_privateNotif->setAccessibleName(trPage("Hide private details in notifications"));
    nL->addWidget(m_privateNotif);
    genLayout->addWidget(notifCard);

    // Configuration Transfer / Full Settings Card
    auto *advCard = makeCard(genBody, 16);
    auto *aL = qobject_cast<QVBoxLayout *>(advCard->layout());
    aL->addWidget(makeLabel(trPage("Configuration & Migration"), "sectionTitle", advCard));
    aL->addWidget(makeLabel(trPage("Export, import, or migrate settings from previous XerahS installations, and edit keyboard shortcuts."), "caption", advCard));
    auto *fullSettingsBtn = new QPushButton(trPage("Open Full Settings & Migration Dialog…"), advCard);
    fullSettingsBtn->setAccessibleName(trPage("Open full settings dialog"));
    connect(fullSettingsBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("settings"))) a->trigger();
    });
    aL->addWidget(fullSettingsBtn, 0, Qt::AlignLeft);
    genLayout->addWidget(advCard);
    genLayout->addStretch();
    addSettingsTab(wrapScroll(genBody, m_tabStack, trPage("General settings")), trPage("General"));

    // Tab 1: Paths
    auto *pathsBody = new QWidget(m_tabStack);
    auto *pathsLayout = new QVBoxLayout(pathsBody);
    pathsLayout->setContentsMargins(24, 20, 24, 20);
    pathsLayout->setSpacing(18);

    auto *folderCard = makeCard(pathsBody, 16);
    auto *fL = qobject_cast<QVBoxLayout *>(folderCard->layout());
    fL->addWidget(makeLabel(trPage("Screenshots Folder"), "sectionTitle", folderCard));
    auto *fRow = new QHBoxLayout;
    m_saveFolder = new QLineEdit(folderCard);
    m_saveFolder->setAccessibleName(trPage("Screenshots folder path"));
    fRow->addWidget(m_saveFolder, 1);
    auto *browseBtn = new QPushButton(trPage("Browse…"), folderCard);
    browseBtn->setAccessibleName(trPage("Browse for screenshots folder"));
    connect(browseBtn, &QPushButton::clicked, this, [this] {
      const QString dir = QFileDialog::getExistingDirectory(&m_window, trPage("Screenshots Folder"), m_saveFolder->text());
      if (!dir.isEmpty()) m_saveFolder->setText(dir);
    });
    fRow->addWidget(browseBtn);
    fL->addLayout(fRow);
    pathsLayout->addWidget(folderCard);

    auto *patCard = makeCard(pathsBody, 16);
    auto *pL = qobject_cast<QVBoxLayout *>(patCard->layout());
    pL->addWidget(makeLabel(trPage("Filename Pattern"), "sectionTitle", patCard));
    m_pattern = new QLineEdit(patCard);
    m_pattern->setAccessibleName(trPage("Filename pattern"));
    pL->addWidget(m_pattern);
    pL->addWidget(makeLabel(trPage("Saved workflow filename patterns override this default when set."), "caption", patCard));
    pathsLayout->addWidget(patCard);
    pathsLayout->addStretch();
    addSettingsTab(wrapScroll(pathsBody, m_tabStack, trPage("Paths settings")), trPage("Paths"));

    // Tab 2: Watch Folders
    auto *watchBody = new QWidget(m_tabStack);
    auto *watchLayout = new QVBoxLayout(watchBody);
    watchLayout->setContentsMargins(24, 20, 24, 20);
    auto *watchCard = makeCard(watchBody, 16);
    auto *wL = qobject_cast<QVBoxLayout *>(watchCard->layout());
    wL->addWidget(makeLabel(trPage("Watch Folders"), "sectionTitle", watchCard));
    wL->addWidget(makeLabel(trPage("Monitor folders for new files and run file upload workflows automatically."), "caption", watchCard));
    auto *openWatchBtn = new QPushButton(trPage("Manage Watch Folders…"), watchCard);
    openWatchBtn->setProperty("accent", true);
    openWatchBtn->setAccessibleName(trPage("Manage watch folders"));
    connect(openWatchBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("watch-folders"))) a->trigger();
    });
    wL->addWidget(openWatchBtn, 0, Qt::AlignLeft);
    watchLayout->addWidget(watchCard);
    watchLayout->addStretch();
    addSettingsTab(wrapScroll(watchBody, m_tabStack, trPage("Watch folders settings")), trPage("Watch Folders"));

    // Tab 3: Capture
    auto *capBody = new QWidget(m_tabStack);
    auto *capLayout = new QVBoxLayout(capBody);
    capLayout->setContentsMargins(24, 20, 24, 20);
    auto *capCard = makeCard(capBody, 16);
    auto *cL = qobject_cast<QVBoxLayout *>(capCard->layout());
    cL->addWidget(makeLabel(trPage("Region Capture"), "sectionTitle", capCard));
    m_quickConfirm = new QCheckBox(trPage("Confirm region on pointer release"), capCard);
    m_quickConfirm->setAccessibleName(trPage("Confirm region on pointer release"));
    cL->addWidget(m_quickConfirm);
    m_windowSnapping = new QCheckBox(trPage("Snap clicks to detected windows or controls"), capCard);
    m_windowSnapping->setAccessibleName(trPage("Snap clicks to detected windows or controls"));
    cL->addWidget(m_windowSnapping);
    capLayout->addWidget(capCard);
    capLayout->addStretch();
    addSettingsTab(wrapScroll(capBody, m_tabStack, trPage("Capture settings")), trPage("Capture"));

    // Tab 4: Editor
    auto *edBody = new QWidget(m_tabStack);
    auto *edLayout = new QVBoxLayout(edBody);
    edLayout->setContentsMargins(24, 20, 24, 20);
    auto *edCard = makeCard(edBody, 16);
    auto *eL = qobject_cast<QVBoxLayout *>(edCard->layout());
    eL->addWidget(makeLabel(trPage("Editor Appearance & Defaults"), "sectionTitle", edCard));
    eL->addWidget(makeLabel(trPage("Configure editor theme, high-contrast behavior, drawing defaults, and toolbar layout."), "caption", edCard));
    auto *appearanceBtn = new QPushButton(trPage("Editor Appearance…"), edCard);
    appearanceBtn->setAccessibleName(trPage("Open editor appearance dialog"));
    connect(appearanceBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("editor-appearance"))) a->trigger();
    });
    eL->addWidget(appearanceBtn, 0, Qt::AlignLeft);
    edLayout->addWidget(edCard);
    edLayout->addStretch();
    addSettingsTab(wrapScroll(edBody, m_tabStack, trPage("Editor settings")), trPage("Editor"));

    tabBarLayout->addStretch();
    root->addWidget(m_tabStack, 1);
    selectTab(0);

    // Footer Apply Bar
    auto *footer = new QFrame(this);
    footer->setProperty("footerBar", true);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(20, 10, 20, 10);
    m_saveFeedback = makeLabel(trPage("Changes are saved when you click Apply."), "caption", footer);
    fl->addWidget(m_saveFeedback, 1);
    auto *reloadBtn = new QPushButton(trPage("Reload"), footer);
    reloadBtn->setAccessibleName(trPage("Reload saved settings"));
    connect(reloadBtn, &QPushButton::clicked, this, &ApplicationSettingsPage::loadFromStore);
    fl->addWidget(reloadBtn);
    auto *applyBtn = new QPushButton(trPage("Apply"), footer);
    applyBtn->setProperty("accent", true);
    applyBtn->setAccessibleName(trPage("Apply application settings"));
    connect(applyBtn, &QPushButton::clicked, this, &ApplicationSettingsPage::saveToStore);
    fl->addWidget(applyBtn);
    root->addWidget(footer);

    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("Settings_App")) loadFromStore();
    });
    loadFromStore();
  }

  void loadFromStore() {
    const auto vals = m_ctx.settings.current().values;
    m_saveFolder->setText(vals.value(QStringLiteral("saveFolder")).toString());
    m_pattern->setText(vals.value(QStringLiteral("filenamePattern")).toString());
    m_showTray->setChecked(vals.value(QStringLiteral("showTrayIcon")).toBool(true));
    m_startAtLogin->setChecked(vals.value(QStringLiteral("startAtLogin")).toBool(false));
    m_showNotif->setChecked(vals.value(QStringLiteral("showNotification")).toBool(true));
    m_playSound->setChecked(vals.value(QStringLiteral("playCaptureSound")).toBool(false));
    m_privateNotif->setChecked(vals.value(QStringLiteral("hidePrivateNotificationDetails")).toBool(false));
    m_quickConfirm->setChecked(vals.value(QStringLiteral("regionQuickConfirm")).toBool(false));
    m_windowSnapping->setChecked(vals.value(QStringLiteral("windowSnapping")).toBool(true));
  }

private:
  void saveToStore() {
    QJsonObject next = m_ctx.settings.current().values;
    next.insert(QStringLiteral("saveFolder"), m_saveFolder->text().trimmed());
    next.insert(QStringLiteral("filenamePattern"), m_pattern->text());
    next.insert(QStringLiteral("showTrayIcon"), m_showTray->isChecked());
    next.insert(QStringLiteral("startAtLogin"), m_startAtLogin->isChecked());
    next.insert(QStringLiteral("showNotification"), m_showNotif->isChecked());
    next.insert(QStringLiteral("playCaptureSound"), m_playSound->isChecked());
    next.insert(QStringLiteral("hidePrivateNotificationDetails"), m_privateNotif->isChecked());
    next.insert(QStringLiteral("regionQuickConfirm"), m_quickConfirm->isChecked());
    next.insert(QStringLiteral("windowSnapping"), m_windowSnapping->isChecked());
    const QString err = m_ctx.settings.save(next);
    if (err.isEmpty()) {
      applyShellVisibility(m_ctx);
      m_saveFeedback->setText(trPage("Settings saved."));
    } else {
      m_saveFeedback->setText(err);
    }
  }

  void selectTab(int idx) {
    m_tabStack->setCurrentIndex(idx);
    for (int i = 0; i < m_tabButtons.size(); ++i) {
      m_tabButtons[i]->setProperty("navActive", i == idx);
      m_tabButtons[i]->style()->unpolish(m_tabButtons[i]);
      m_tabButtons[i]->style()->polish(m_tabButtons[i]);
    }
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QStackedWidget *m_tabStack = nullptr;
  QList<QPushButton *> m_tabButtons;
  QComboBox *m_themeCombo = nullptr;
  QCheckBox *m_showTray = nullptr;
  QCheckBox *m_startAtLogin = nullptr;
  QCheckBox *m_showNotif = nullptr;
  QCheckBox *m_playSound = nullptr;
  QCheckBox *m_privateNotif = nullptr;
  QCheckBox *m_quickConfirm = nullptr;
  QCheckBox *m_windowSnapping = nullptr;
  QLineEdit *m_saveFolder = nullptr;
  QLineEdit *m_pattern = nullptr;
  QLabel *m_saveFeedback = nullptr;
};

// ---------------------------------------------------------------------------
// 8. Destination Settings Page (matches DestinationSettingsView.axaml)
// ---------------------------------------------------------------------------
class DestinationSettingsPage final : public QWidget {
public:
  DestinationSettingsPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Left Categories Sidebar
    auto *sidebar = new QFrame(this);
    sidebar->setProperty("card", true);
    sidebar->setFixedWidth(150);
    auto *sb = new QVBoxLayout(sidebar);
    sb->setContentsMargins(12, 14, 12, 14);
    sb->setSpacing(10);
    sb->addWidget(makeLabel(trPage("Categories"), "sectionTitle", sidebar));

    m_categories = new QListWidget(sidebar);
    m_categories->setAccessibleName(trPage("Destination categories"));
    m_categories->addItems({trPage("Image Uploaders"), trPage("Text Uploaders"), trPage("File Uploaders"), trPage("URL Shorteners")});
    m_categories->setCurrentRow(0);
    connect(m_categories, &QListWidget::currentRowChanged, this, [this](int) {
      if (auto *it = m_categories->currentItem()) m_categoryTitle->setText(it->text());
    });
    sb->addWidget(m_categories, 1);
    root->addWidget(sidebar);

    // Right Main Content
    auto *main = new QWidget(this);
    auto *ml = new QVBoxLayout(main);
    ml->setContentsMargins(18, 18, 18, 18);
    ml->setSpacing(14);

    auto *header = new QHBoxLayout;
    m_categoryTitle = makeLabel(trPage("Image Uploaders"), "pageTitle", main);
    header->addWidget(m_categoryTitle, 1);

    auto *refreshBtn = new QPushButton(trPage("Refresh"), main);
    refreshBtn->setAccessibleName(trPage("Refresh destinations"));
    connect(refreshBtn, &QPushButton::clicked, this, &DestinationSettingsPage::reload);
    header->addWidget(refreshBtn);

    auto *manageBtn = new QPushButton(trPage("Import .sxcu…"), main);
    manageBtn->setProperty("accent", true);
    manageBtn->setAccessibleName(trPage("Manage upload destinations"));
    connect(manageBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("upload-destinations"))) a->trigger();
    });
    header->addWidget(manageBtn);
    ml->addLayout(header);

    auto *split = new QHBoxLayout;
    split->setSpacing(12);

    // Instance List Card
    auto *listCard = makeCard(main, 8);
    listCard->setFixedWidth(148);
    m_instances = new QListWidget(listCard);
    m_instances->setAccessibleName(trPage("Destination instances"));
    connect(m_instances, &QListWidget::currentRowChanged, this, [this](int) { showSelected(); });
    qobject_cast<QVBoxLayout *>(listCard->layout())->addWidget(m_instances, 1);
    split->addWidget(listCard);

    // Configuration Details Card
    auto *cfgCard = makeCard(main, 16);
    cfgCard->setMinimumWidth(140);
    auto *cfgL = qobject_cast<QVBoxLayout *>(cfgCard->layout());
    cfgL->addWidget(makeLabel(trPage("Destination Configuration"), "sectionTitle", cfgCard));
    auto *form = new QFormLayout;
    form->setSpacing(8);
    m_nameVal = makeLabel(trPage("—"), nullptr, cfgCard);
    m_methodVal = makeLabel(trPage("—"), nullptr, cfgCard);
    m_urlVal = makeLabel(trPage("—"), nullptr, cfgCard);
    m_secretsVal = makeLabel(trPage("—"), "caption", cfgCard);
    m_statusVal = makeLabel(trPage("—"), "caption", cfgCard);
    form->addRow(trPage("Name:"), m_nameVal);
    form->addRow(trPage("Method:"), m_methodVal);
    form->addRow(trPage("URL:"), m_urlVal);
    form->addRow(trPage("Secrets:"), m_secretsVal);
    form->addRow(trPage("Status:"), m_statusVal);
    cfgL->addLayout(form);
    cfgL->addStretch();
    split->addWidget(cfgCard, 1);

    ml->addLayout(split, 1);
    root->addWidget(main, 1);

    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("Settings_Dest")) reload();
    });
    reload();
  }

  void reload() {
    m_entries.clear();
    m_instances->clear();
    xerahs::uploads::DestinationCatalog catalog(m_ctx.uploadCatalogPath);
    m_defaultId = catalog.defaultId();
    m_entries = catalog.entries();
    for (const auto &entry : m_entries) {
      const QString prefix = (entry.destination.id == m_defaultId) ? QStringLiteral("✓ ") : QString{};
      m_instances->addItem(prefix + entry.destination.name);
    }
    if (!m_entries.isEmpty()) m_instances->setCurrentRow(0);
    showSelected();
  }

private:
  void showSelected() {
    const int row = m_instances->currentRow();
    if (row < 0 || row >= m_entries.size()) {
      m_nameVal->setText(trPage("No destinations configured"));
      m_methodVal->setText(trPage("—"));
      m_urlVal->setText(trPage("Import a .sxcu custom uploader to configure an upload destination."));
      m_secretsVal->setText(trPage("None"));
      m_statusVal->setText(trPage("Ready"));
      return;
    }
    const auto &entry = m_entries[row];
    m_nameVal->setText(entry.destination.name + (entry.destination.id == m_defaultId ? trPage(" (Default)") : QString{}));
    m_methodVal->setText(xerahs::uploads::requestMethodString(entry.destination.method));
    m_urlVal->setText(entry.destination.requestUrl);
    const auto secrets = xerahs::uploads::DestinationCatalog::secretNames(entry.destination);
    m_secretsVal->setText(secrets.isEmpty() ? trPage("None") : secrets.join(QStringLiteral(", ")));
    m_statusVal->setText(entry.problems.isEmpty() ? trPage("Valid") : entry.problems.join(QStringLiteral(", ")));
  }

  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QListWidget *m_categories = nullptr;
  QListWidget *m_instances = nullptr;
  QLabel *m_categoryTitle = nullptr;
  QLabel *m_nameVal = nullptr;
  QLabel *m_methodVal = nullptr;
  QLabel *m_urlVal = nullptr;
  QLabel *m_secretsVal = nullptr;
  QLabel *m_statusVal = nullptr;
  QVector<xerahs::uploads::CatalogEntry> m_entries;
  QUuid m_defaultId;
};

// ---------------------------------------------------------------------------
// 9. Debug Page (matches DebugView.axaml)
// ---------------------------------------------------------------------------
class DebugPage final : public QWidget {
public:
  DebugPage(AppContext &ctx, ApplicationWindow &window, QWidget *parent)
      : QWidget(parent), m_ctx(ctx), m_window(window) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Toolbar (DebugView.axaml Row 0)
    auto *toolbar = new QFrame(this);
    toolbar->setProperty("headerBar", true);
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(14, 10, 14, 10);
    tb->setSpacing(6);

    auto *openCaptures = new QPushButton(trPage("Captures Folder"), toolbar);
    openCaptures->setAccessibleName(trPage("Open captures folder"));
    connect(openCaptures, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("captures-folder"))) a->trigger();
    });
    tb->addWidget(openCaptures);

    auto *openSettings = new QPushButton(trPage("Settings Folder"), toolbar);
    openSettings->setAccessibleName(trPage("Open settings folder"));
    connect(openSettings, &QPushButton::clicked, this, [this] {
      const QString dir = QFileInfo(m_ctx.settings.path()).absolutePath();
      if (!dir.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    tb->addWidget(openSettings);

    auto *copyBtn = new QPushButton(trPage("Copy"), toolbar);
    copyBtn->setAccessibleName(trPage("Copy diagnostics log"));
    connect(copyBtn, &QPushButton::clicked, this, [this] {
      QApplication::clipboard()->setText(m_logView->toPlainText());
      m_window.showResult(trPage("Debug"), trPage("Copied diagnostics output."), true);
    });
    tb->addWidget(copyBtn);

    auto *refreshBtn = new QPushButton(trPage("Refresh"), toolbar);
    refreshBtn->setAccessibleName(trPage("Refresh diagnostics"));
    connect(refreshBtn, &QPushButton::clicked, this, &DebugPage::refresh);
    tb->addWidget(refreshBtn);

    auto *exportBtn = new QPushButton(trPage("Export…"), toolbar);
    exportBtn->setProperty("accent", true);
    exportBtn->setAccessibleName(trPage("Export diagnostics bundle"));
    connect(exportBtn, &QPushButton::clicked, this, [this] {
      if (auto *a = m_ctx.commands.action(QStringLiteral("export-diagnostics"))) a->trigger();
    });
    tb->addWidget(exportBtn);
    tb->addStretch();
    root->addWidget(toolbar);

    // Secret Store Diagnostics Banner (DebugView.axaml Row 1)
    auto *diagBanner = new QFrame(this);
    diagBanner->setProperty("headerBar", true);
    auto *db = new QVBoxLayout(diagBanner);
    db->setContentsMargins(16, 12, 16, 12);
    db->setSpacing(4);
    db->addWidget(makeLabel(trPage("Secret Store & Runtime Diagnostics"), "sectionTitle", diagBanner));
    db->addWidget(makeLabel(trPage("Backend: freedesktop.org Secret Service (D-Bus)"), "caption", diagBanner));
    db->addWidget(makeLabel(trPage("Platform: %1  ·  Qt %2").arg(QGuiApplication::platformName(), QString::fromLatin1(qVersion())), "caption", diagBanner));
    root->addWidget(diagBanner);

    // Log Output (DebugView.axaml Row 2)
    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setTabChangesFocus(true);
    m_logView->setAccessibleName(trPage("Diagnostics log output"));
    m_logView->setFont(QFont(QStringLiteral("monospace"), 10));
    root->addWidget(m_logView, 1);

    connect(&m_window, &ApplicationWindow::pageChanged, this, [this](const QString &pageId) {
      if (pageId == QLatin1String("Debug")) refresh();
    });
    refresh();
  }

  void refresh() {
    if (!m_ctx.diagnosticLog) {
      m_logView->setPlainText(trPage("Diagnostic log not initialized."));
      return;
    }
    const auto bundle = xerahs::diagnostics::diagnosticBundle(m_ctx.settings.current(), *m_ctx.diagnosticLog);
    m_logView->setPlainText(QString::fromUtf8(QJsonDocument(bundle.document()).toJson(QJsonDocument::Indented)));
  }

private:
  AppContext &m_ctx;
  ApplicationWindow &m_window;
  QPlainTextEdit *m_logView = nullptr;
};

// ---------------------------------------------------------------------------
// 10. About Page (matches AboutView.axaml)
// ---------------------------------------------------------------------------
class AboutPage final : public QWidget {
public:
  explicit AboutPage(ApplicationWindow &window, QWidget *parent) : QWidget(parent), m_window(window) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(24, 28, 24, 28);
    layout->setSpacing(20);

    // Logo & Product Title
    auto *logo = new QLabel(body);
    logo->setAlignment(Qt::AlignCenter);
    logo->setPixmap(applicationIcon().pixmap(96, 96));
    layout->addWidget(logo);

    auto *title = makeLabel(trPage("XerahS (Linux Qt Native)"), "pageTitle", body);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *copy = makeLabel(trPage("Copyright (c) 2007-2026 ShareX Team · GNU GPL v3"), "caption", body);
    copy->setAlignment(Qt::AlignCenter);
    layout->addWidget(copy);

    auto addLinkCard = [&](const QString &sectionTitle, const QList<QPair<QString, QString>> &links) {
      auto *card = makeCard(body, 16);
      auto *cl = qobject_cast<QVBoxLayout *>(card->layout());
      cl->addWidget(makeLabel(sectionTitle, "sectionTitle", card));
      for (const auto &entry : links) {
        auto *btn = new QPushButton(QStringLiteral("%1   —   %2").arg(entry.first, entry.second), card);
        btn->setProperty("settingsRow", true);
        btn->setAccessibleName(entry.first);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [url = entry.second] { QDesktopServices::openUrl(QUrl(url)); });
        cl->addWidget(btn);
      }
      layout->addWidget(card);
    };

    addLinkCard(trPage("Links"), {
      {trPage("Website"), QStringLiteral("https://getsharex.com/")},
      {trPage("GitHub Project"), QStringLiteral("https://github.com/ShareX/XerahS")},
      {trPage("Issues"), QStringLiteral("https://github.com/ShareX/XerahS/issues")},
      {trPage("License"), QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html")},
    });

    addLinkCard(trPage("Social"), {
      {trPage("X"), QStringLiteral("https://x.com/ShareX")},
      {trPage("Discord"), QStringLiteral("https://discord.gg/ShareX")},
      {trPage("Reddit"), QStringLiteral("https://www.reddit.com/r/sharex/")},
    });

    // Loaded Runtime Libraries Card
    auto *libCard = makeCard(body, 16);
    auto *ll = qobject_cast<QVBoxLayout *>(libCard->layout());
    ll->addWidget(makeLabel(trPage("Loaded Runtime & Libraries"), "sectionTitle", libCard));
    ll->addWidget(makeLabel(trPage("Qt Runtime: %1 (%2)").arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName()), "caption", libCard));
    ll->addWidget(makeLabel(trPage("Application Identity: com.xerahs.app"), "caption", libCard));
    layout->addWidget(libCard);

    layout->addStretch();
    outer->addWidget(wrapScroll(body, this, trPage("About XerahS page")));
  }

private:
  ApplicationWindow &m_window;
};

}  // namespace

void installShellPages(AppContext &ctx, ApplicationWindow &window) {
  auto *editor = new EmbeddedEditorPage(ctx, window, &window);
  window.setPageWidget(QStringLiteral("Editor"), editor);
  window.setPageWidget(QStringLiteral("Recording"), new RecordingPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("History"), new HistoryPage(ctx, window, editor, &window));
  window.setPageWidget(QStringLiteral("Statistics"), new StatisticsPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("Workflows"), new WorkflowsPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("Tools"), new ToolsPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("Settings_App"), new ApplicationSettingsPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("Settings_Dest"), new DestinationSettingsPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("Debug"), new DebugPage(ctx, window, &window));
  window.setPageWidget(QStringLiteral("About"), new AboutPage(window, &window));
  window.navigateTo(QStringLiteral("Editor"));
}

}  // namespace xerahs::app
