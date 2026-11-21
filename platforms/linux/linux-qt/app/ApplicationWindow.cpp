#include "ApplicationWindow.h"
#include "AccessibleCue.h"
#include "AppIcons.h"
#include "AppTheme.h"
#include "CommandRegistry.h"
#include "NativeAccessibility.h"
#include "NativeWindowPlacement.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionFocusRect>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace xerahs::app {
namespace {

QStringList localFilePaths(const QMimeData *data) {
  QStringList paths;
  if (!data) return paths;
  for (const auto &url : data->urls())
    if (url.isLocalFile()) paths.append(url.toLocalFile());
  return paths;
}

void acceptLocalFiles(QDropEvent *event) {
  if (localFilePaths(event->mimeData()).isEmpty()) event->ignore();
  else event->acceptProposedAction();
}

void adoptLegacy(CommandRegistry &registry, const QMap<QString, QAction *> &actions) {
  const struct Row { const char *id; CommandGroup group; CommandSurfaces surfaces; } rows[] = {
    {"capture-region", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-last-region", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-full-screen", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-monitor", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-active-window", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-scrolling", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-after-delay", CommandGroup::Schedule, CommandSurface::CaptureMenu},
    {"auto-capture", CommandGroup::Schedule, CommandSurface::CaptureMenu},
    {"stop-auto-capture", CommandGroup::Schedule, CommandSurface::CaptureMenu},
    {"capture-region-choose-actions", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"capture-workflows", CommandGroup::Workflows, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"enable-workflow-shortcuts", CommandGroup::Workflows, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"disable-workflow-shortcuts", CommandGroup::Workflows, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"cancel-capture", CommandGroup::Capture, CommandSurface::Window | CommandSurface::CaptureMenu},
    {"new-image", CommandGroup::Files, CommandSurface::Window | CommandSurface::FileMenu},
    {"open-image", CommandGroup::Files, CommandSurface::Window | CommandSurface::FileMenu},
    {"upload-file", CommandGroup::Upload, CommandSurface::Window | CommandSurface::FileMenu},
    {"captures-folder", CommandGroup::Files, CommandSurface::Window | CommandSurface::FileMenu},
    {"capture-history", CommandGroup::Files, CommandSurface::Window | CommandSurface::FileMenu},
    {"pick-colour", CommandGroup::Tools, CommandSurface::Window | CommandSurface::FileMenu | CommandSurface::ToolsMenu},
    {"upload-destinations", CommandGroup::Upload, CommandSurface::Window | CommandSurface::PreferencesMenu},
    {"settings", CommandGroup::Preferences, CommandSurface::PreferencesMenu},
    {"editor-appearance", CommandGroup::Preferences, CommandSurface::Window | CommandSurface::PreferencesMenu},
    {"quit", CommandGroup::Application, CommandSurface::FileMenu},
  };
  for (const auto &row : rows) {
    auto *action = actions.value(QString::fromLatin1(row.id));
    if (!action) continue;
    registry.adopt(CommandSpec{QString::fromLatin1(row.id), action->text(), row.group, row.surfaces}, action);
  }
}

AppIcon iconForPage(const QString &pageId) {
  if (pageId == QLatin1String("Recording")) return AppIcon::Film;
  if (pageId == QLatin1String("Editor")) return AppIcon::Pencil;
  if (pageId == QLatin1String("History")) return AppIcon::HardDrive;
  if (pageId == QLatin1String("Statistics")) return AppIcon::ChartColumn;
  if (pageId == QLatin1String("Workflows")) return AppIcon::Combine;
  if (pageId == QLatin1String("Tools")) return AppIcon::WandSparkles;
  if (pageId == QLatin1String("Settings_App") || pageId == QLatin1String("Settings_Dest")) return AppIcon::SlidersVertical;
  if (pageId == QLatin1String("Debug")) return AppIcon::Terminal;
  if (pageId == QLatin1String("About")) return AppIcon::Aperture;
  return AppIcon::Camera;
}

AppIcon iconForGroup(const QString &groupId) {
  if (groupId == QLatin1String("Capture")) return AppIcon::Camera;
  if (groupId == QLatin1String("Upload")) return AppIcon::CloudUpload;
  if (groupId == QLatin1String("Settings")) return AppIcon::SlidersVertical;
  if (groupId == QLatin1String("Tools")) return AppIcon::WandSparkles;
  if (groupId == QLatin1String("Workflows")) return AppIcon::Combine;
  if (groupId == QLatin1String("Files")) return AppIcon::FolderOpen;
  return AppIcon::Command;
}

AppIcon iconForCommand(const QString &id) {
  if (id.startsWith(QLatin1String("capture-")) || id == QLatin1String("auto-capture")) return AppIcon::Camera;
  if (id == QLatin1String("cancel-capture") || id == QLatin1String("stop-auto-capture")) return AppIcon::CloseX;
  if (id == QLatin1String("new-image")) return AppIcon::FilePlus;
  if (id == QLatin1String("open-image") || id == QLatin1String("captures-folder")) return AppIcon::FolderOpen;
  if (id == QLatin1String("capture-history")) return AppIcon::HardDrive;
  if (id == QLatin1String("upload-file") || id == QLatin1String("upload-destinations") || id == QLatin1String("watch-folders")) return AppIcon::CloudUpload;
  if (id == QLatin1String("pick-colour")) return AppIcon::Palette;
  if (id == QLatin1String("qr-code")) return AppIcon::QrCode;
  if (id == QLatin1String("scan-qr")) return AppIcon::ScanLine;
  if (id == QLatin1String("recognize-text")) return AppIcon::FileText;
  if (id == QLatin1String("hash-checker")) return AppIcon::Hash;
  if (id == QLatin1String("icon-converter")) return AppIcon::ImageIcon;
  if (id == QLatin1String("background-remover")) return AppIcon::Scissors;
  if (id == QLatin1String("video-converter")) return AppIcon::Film;
  if (id == QLatin1String("command-palette")) return AppIcon::Command;
  if (id == QLatin1String("export-diagnostics")) return AppIcon::Terminal;
  if (id == QLatin1String("editor-appearance") || id == QLatin1String("settings")) return AppIcon::SlidersVertical;
  return AppIcon::Combine;
}

QString stripMnemonic(const QString &raw) {
  QString out;
  out.reserve(raw.size());
  for (int i = 0; i < raw.size(); ++i) {
    if (raw[i] == QLatin1Char('&')) {
      if (i + 1 < raw.size() && raw[i + 1] == QLatin1Char('&')) {
        out.append(QLatin1Char('&'));
        ++i;
      }
      continue;
    }
    out.append(raw[i]);
  }
  return out;
}

class NavItemButton final : public QPushButton {
public:
  NavItemButton(const QString &label, bool isGroup, bool isChild, QWidget *parent)
      : QPushButton(label, parent), m_isGroup(isGroup), m_isChild(isChild) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
  }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int w) const override {
    const int leftPad = m_isChild ? 26 : 10;
    const int rightPad = m_isGroup ? 28 : 10;
    constexpr int vPad = 7;
    const int iconW = icon().isNull() ? 0 : (iconSize().width() + 8);
    const int availW = std::max(60, w - leftPad - iconW - rightPad);
    const QRect br = fontMetrics().boundingRect(
        QRect(0, 0, availW, 10000),
        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
        stripMnemonic(text()));
    const int contentH = std::max(icon().isNull() ? 0 : iconSize().height(), br.height());
    return std::max(34, contentH + vPad * 2);
  }

  QSize minimumSizeHint() const override { return QSize(80, 34); }

  QSize sizeHint() const override {
    const int refW = width() > 60 ? width() : 260;
    return QSize(refW, heightForWidth(refW));
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPushButton::resizeEvent(event);
    if (width() != m_lastWidth) {
      m_lastWidth = width();
      updateGeometry();
    }
  }

  void changeEvent(QEvent *event) override {
    QPushButton::changeEvent(event);
    if (event && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)) {
      updateGeometry();
    }
  }

  void paintEvent(QPaintEvent *) override {
    QStylePainter p(this);
    QStyleOptionButton opt;
    initStyleOption(&opt);
    const QString label = stripMnemonic(text());
    const QIcon ico = icon();
    const QSize icoSz = iconSize();
    opt.text.clear();
    opt.icon = QIcon();
    opt.iconSize = QSize();
    p.drawControl(QStyle::CE_PushButtonBevel, opt);

    const int leftPad = m_isChild ? 26 : 10;
    const int rightPad = m_isGroup ? 28 : 10;
    constexpr int vPad = 7;
    int textLeft = leftPad;
    if (!ico.isNull()) {
      const QIcon::Mode mode = !isEnabled()
          ? QIcon::Disabled
          : (opt.state & QStyle::State_MouseOver) ? QIcon::Active : QIcon::Normal;
      const int lineH = std::max(icoSz.height(), fontMetrics().height());
      const int iconY = vPad + std::max(0, (lineH - icoSz.height()) / 2);
      ico.paint(&p, QRect(leftPad, iconY, icoSz.width(), icoSz.height()), Qt::AlignCenter, mode);
      textLeft += icoSz.width() + 8;
    }

    const QRect textRect(
        textLeft,
        vPad,
        std::max(10, width() - textLeft - rightPad),
        std::max(10, height() - vPad * 2));
    const QPalette::ColorGroup cg = isEnabled() ? QPalette::Active : QPalette::Disabled;
    p.setPen(opt.palette.color(cg, QPalette::ButtonText));
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, label);

    if (m_isGroup) {
      const bool expanded = property("navExpanded").toBool();
      p.save();
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(QPen(opt.palette.color(cg, QPalette::ButtonText), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      const qreal cx = width() - 15.0;
      const qreal cy = height() / 2.0;
      if (expanded) {
        p.drawLine(QPointF(cx - 4.0, cy - 2.0), QPointF(cx, cy + 2.0));
        p.drawLine(QPointF(cx, cy + 2.0), QPointF(cx + 4.0, cy - 2.0));
      } else {
        p.drawLine(QPointF(cx - 2.0, cy - 4.0), QPointF(cx + 2.0, cy));
        p.drawLine(QPointF(cx + 2.0, cy), QPointF(cx - 2.0, cy + 4.0));
      }
      p.restore();
    }

    if (hasFocus()) {
      QStyleOptionFocusRect focusOpt;
      focusOpt.initFrom(this);
      focusOpt.rect = rect().adjusted(2, 2, -2, -2);
      p.drawPrimitive(QStyle::PE_FrameFocusRect, focusOpt);
    }
  }

private:
  bool m_isGroup = false;
  bool m_isChild = false;
  int m_lastWidth = -1;
};

class NavActionButton final : public QToolButton {
public:
  explicit NavActionButton(QWidget *parent) : QToolButton(parent) {
    QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sp.setHeightForWidth(true);
    setSizePolicy(sp);
  }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int w) const override {
    constexpr int kLeftPad = 24;
    constexpr int kRightPad = 10;
    constexpr int kVPad = 6;
    const int iconW = icon().isNull() ? 0 : (iconSize().width() + 8);
    const int availW = std::max(60, w - kLeftPad - iconW - kRightPad);
    const QRect br = fontMetrics().boundingRect(
        QRect(0, 0, availW, 10000),
        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
        stripMnemonic(text()));
    const int contentH = std::max(icon().isNull() ? 0 : iconSize().height(), br.height());
    return std::max(30, contentH + kVPad * 2);
  }

  QSize minimumSizeHint() const override { return QSize(80, 30); }

  QSize sizeHint() const override {
    const int refW = width() > 60 ? width() : 260;
    return QSize(refW, heightForWidth(refW));
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QToolButton::resizeEvent(event);
    if (width() != m_lastWidth) {
      m_lastWidth = width();
      updateGeometry();
    }
  }

  void actionEvent(QActionEvent *event) override {
    QToolButton::actionEvent(event);
    updateGeometry();
  }

  void changeEvent(QEvent *event) override {
    QToolButton::changeEvent(event);
    if (event && (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)) {
      updateGeometry();
    }
  }

  void paintEvent(QPaintEvent *) override {
    QStylePainter p(this);
    QStyleOptionToolButton opt;
    initStyleOption(&opt);
    const QString label = stripMnemonic(text());
    const QIcon ico = icon();
    const QSize icoSz = iconSize();
    opt.text.clear();
    opt.icon = QIcon();
    opt.iconSize = QSize();
    opt.features = QStyleOptionToolButton::None;
    p.drawComplexControl(QStyle::CC_ToolButton, opt);

    constexpr int kLeftPad = 24;
    constexpr int kRightPad = 10;
    constexpr int kVPad = 6;
    int textLeft = kLeftPad;
    if (!ico.isNull()) {
      const QIcon::Mode mode = !isEnabled()
          ? QIcon::Disabled
          : (opt.state & QStyle::State_MouseOver) ? QIcon::Active : QIcon::Normal;
      const int lineH = std::max(icoSz.height(), fontMetrics().height());
      const int iconY = kVPad + std::max(0, (lineH - icoSz.height()) / 2);
      ico.paint(&p, QRect(kLeftPad, iconY, icoSz.width(), icoSz.height()), Qt::AlignCenter, mode);
      textLeft += icoSz.width() + 8;
    }

    const QRect textRect(
        textLeft,
        kVPad,
        std::max(10, width() - textLeft - kRightPad),
        std::max(10, height() - kVPad * 2));
    const QPalette::ColorGroup cg = isEnabled() ? QPalette::Active : QPalette::Disabled;
    const bool hovered = isEnabled() && (opt.state & QStyle::State_MouseOver);
    p.setPen(opt.palette.color(cg, hovered ? QPalette::WindowText : QPalette::ButtonText));
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, label);

    if (hasFocus()) {
      QStyleOptionFocusRect focusOpt;
      focusOpt.initFrom(this);
      focusOpt.rect = rect().adjusted(2, 2, -2, -2);
      p.drawPrimitive(QStyle::PE_FrameFocusRect, focusOpt);
    }
  }

private:
  int m_lastWidth = -1;
};

QWidget *makeFallbackPage(const QString &title, const QString &subtitle, QWidget *parent) {
  auto *page = new QWidget(parent);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(24, 24, 24, 24);
  layout->setSpacing(8);
  layout->addStretch();
  auto *heading = new QLabel(title, page);
  heading->setProperty("role", QStringLiteral("pageTitle"));
  heading->setAlignment(Qt::AlignCenter);
  heading->setWordWrap(true);
  layout->addWidget(heading);
  auto *body = new QLabel(subtitle, page);
  body->setProperty("role", QStringLiteral("caption"));
  body->setAlignment(Qt::AlignCenter);
  body->setWordWrap(true);
  layout->addWidget(body);
  layout->addStretch();
  return page;
}

}  // namespace

ApplicationWindow::ApplicationWindow(CommandRegistry &commands, QWidget *parent) : QMainWindow(parent) {
  build(commands);
}

ApplicationWindow::ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent)
    : QMainWindow(parent), m_legacyCommands(this) {
  adoptLegacy(m_legacyCommands, actions);
  build(m_legacyCommands);
}

void ApplicationWindow::build(CommandRegistry &commands) {
  setObjectName(QStringLiteral("applicationWindow"));
  setWindowTitle(QStringLiteral("XerahS"));
  setAccessibleName(QStringLiteral("XerahS"));
  setAccessibleDescription(tr("Screen capture, image editor, workflows, history, tools and settings."));

  buildMenus(commands);

  auto *central = new QWidget(this);
  central->setObjectName(QStringLiteral("mainShellCentral"));
  auto *rootLayout = new QHBoxLayout(central);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // Main 2-pane split (mirrors MainWindow.axaml ColumnDefinitions="300,*", user-resizable).
  auto *splitter = new QSplitter(Qt::Horizontal, central);
  splitter->setObjectName(QStringLiteral("mainShellSplitter"));
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(4);

  // Column 0: Left Navigation Pane.
  auto *navPane = new QFrame(splitter);
  navPane->setObjectName(QStringLiteral("navigationPane"));
  navPane->setMinimumWidth(220);
  navPane->setMaximumWidth(420);
  auto *navOuter = new QVBoxLayout(navPane);
  navOuter->setContentsMargins(10, 10, 6, 10);
  navOuter->setSpacing(8);

  m_navSearch = new QLineEdit(navPane);
  m_navSearch->setObjectName(QStringLiteral("navigationSearch"));
  m_navSearch->setPlaceholderText(tr("Search"));
  m_navSearch->setAccessibleName(tr("Navigation search"));
  m_navSearch->setClearButtonEnabled(true);
  m_navSearch->setMinimumHeight(32);
  navOuter->addWidget(m_navSearch);

  auto *navScroll = new QScrollArea(navPane);
  navScroll->setObjectName(QStringLiteral("navigationScrollArea"));
  navScroll->setFocusPolicy(Qt::NoFocus);
  navScroll->setWidgetResizable(true);
  navScroll->setFrameShape(QFrame::NoFrame);
  navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  navScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  navScroll->viewport()->setAutoFillBackground(false);

  auto *navTreeHost = new QWidget(navScroll);
  navTreeHost->setObjectName(QStringLiteral("navigationTree"));
  navTreeHost->setAutoFillBackground(false);
  auto *navLayout = new QVBoxLayout(navTreeHost);
  navLayout->setContentsMargins(0, 2, 8, 2);
  navLayout->setSpacing(2);
  buildNavigationPane(commands, navLayout);
  navLayout->addStretch();
  navScroll->setWidget(navTreeHost);
  navOuter->addWidget(navScroll, 1);

  splitter->addWidget(navPane);

  // Column 1: Right Content Area + Retained Capture Banner + Status Strip.
  auto *contentHost = new QWidget(splitter);
  contentHost->setObjectName(QStringLiteral("contentFrameHost"));
  auto *contentLayout = new QVBoxLayout(contentHost);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  m_contentStack = new QStackedWidget(contentHost);
  m_contentStack->setObjectName(QStringLiteral("contentFrame"));
  contentLayout->addWidget(m_contentStack, 1);

  m_recoveryStrip = new QFrame(contentHost);
  m_recoveryStrip->setObjectName(QStringLiteral("retainedCaptureStrip"));
  m_recoveryStrip->setProperty("footerBar", true);
  m_recoveryStrip->hide();
  contentLayout->addWidget(m_recoveryStrip);

  auto *statusStrip = new QFrame(contentHost);
  statusStrip->setObjectName(QStringLiteral("shellStatusStrip"));
  auto *statusLayout = new QHBoxLayout(statusStrip);
  statusLayout->setContentsMargins(14, 6, 14, 6);
  statusLayout->setSpacing(12);
  m_status = new QLabel(tr("Ready"), statusStrip);
  m_status->setObjectName(QStringLiteral("operationStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  m_status->setAccessibleName(tr("Most recent operation result"));
  statusLayout->addWidget(m_status, 1);

  m_progress = new QProgressBar(statusStrip);
  m_progress->setObjectName(QStringLiteral("operationProgress"));
  m_progress->setAccessibleName(tr("Operation progress"));
  m_progress->setRange(0, 100);
  m_progress->setFixedWidth(180);
  m_progress->hide();
  statusLayout->addWidget(m_progress);
  contentLayout->addWidget(statusStrip);

  splitter->addWidget(contentHost);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({300, 800});
  if (auto *handle = splitter->handle(1)) handle->setFocusPolicy(Qt::NoFocus);
  rootLayout->addWidget(splitter, 1);
  setCentralWidget(central);

  buildDefaultPages(commands);

  connect(m_navSearch, &QLineEdit::textChanged, this, [this](const QString &query) {
    applyNavigationFilter(query);
    emit navigationSearchChanged(query);
  });

  if (auto *cancel = commands.action(QStringLiteral("cancel-capture"))) {
    cancel->setShortcut(QKeySequence(Qt::Key_Escape));
    cancel->setShortcutContext(Qt::WindowShortcut);
    addAction(cancel);
  }
  if (auto *palette = commands.action(QStringLiteral("command-palette"))) addAction(palette);

  new NativeWindowPlacement(*this);
  new NativeAccessibility(*this, tr("XerahS main navigation, editor, tools, history, settings and operation status. Use menus or Tab to navigate."), false);

  applyCurrentTheme();
  navigateTo(QStringLiteral("Editor"));
  resize(1100, 650);
  setAcceptDrops(true);
  adoptDropTargets();
}

void ApplicationWindow::buildMenus(CommandRegistry &commands) {
  auto *fileMenu = menuBar()->addMenu(tr("&File"));
  commands.populate(fileMenu, CommandSurface::FileMenu);

  auto *editMenu = menuBar()->addMenu(tr("&Edit"));
  const auto addEditorDelegate = [this, editMenu](const QString &key, const QString &label, const QKeySequence &shortcut) {
    auto *action = editMenu->addAction(label);
    action->setObjectName(QStringLiteral("shellEdit_") + key);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    action->setEnabled(false);
    m_editorMenuDelegates.insert(key, action);
  };
  addEditorDelegate(QStringLiteral("undo"), tr("&Undo"), QKeySequence::Undo);
  addEditorDelegate(QStringLiteral("redo"), tr("&Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y));
  editMenu->addSeparator();
  addEditorDelegate(QStringLiteral("copy"), tr("&Copy to Clipboard"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));

  auto *viewMenu = menuBar()->addMenu(tr("&View"));
  const auto addNavViewAction = [this, viewMenu](const QString &label, const QString &pageId) {
    auto *action = viewMenu->addAction(label);
    connect(action, &QAction::triggered, this, [this, pageId] { navigateTo(pageId); });
  };
  addNavViewAction(tr("&Editor"), QStringLiteral("Editor"));
  addNavViewAction(tr("&History"), QStringLiteral("History"));
  addNavViewAction(tr("&Statistics"), QStringLiteral("Statistics"));
  addNavViewAction(tr("&Recording"), QStringLiteral("Recording"));
  viewMenu->addSeparator();
  const auto addZoomDelegate = [this, viewMenu](const QString &key, const QString &label, const QKeySequence &shortcut) {
    auto *action = viewMenu->addAction(label);
    action->setObjectName(QStringLiteral("shellView_") + key);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    action->setEnabled(false);
    m_editorMenuDelegates.insert(key, action);
  };
  addZoomDelegate(QStringLiteral("reset-zoom"), tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0));
  addZoomDelegate(QStringLiteral("zoom-in"), tr("Zoom &In"), QKeySequence(Qt::CTRL | Qt::Key_Plus));
  addZoomDelegate(QStringLiteral("zoom-out"), tr("Zoom &Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus));
  addZoomDelegate(QStringLiteral("fit-image"), tr("Zoom to &Fit"), QKeySequence(Qt::CTRL | Qt::Key_9));

  auto *captureMenu = menuBar()->addMenu(tr("&Capture"));
  commands.populate(captureMenu, CommandSurface::CaptureMenu);

  auto *workflowsMenu = menuBar()->addMenu(tr("&Workflows"));
  auto *manageWorkflows = workflowsMenu->addAction(tr("&Manage Workflows"));
  connect(manageWorkflows, &QAction::triggered, this, [this] { navigateTo(QStringLiteral("Workflows")); });
  if (auto *wfAction = commands.action(QStringLiteral("capture-workflows"))) {
    workflowsMenu->addSeparator();
    workflowsMenu->addAction(wfAction);
  }

  auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
  commands.populate(toolsMenu, CommandSurface::ToolsMenu);

  auto *settingsMenu = menuBar()->addMenu(tr("&Settings"));
  auto *appSettings = settingsMenu->addAction(tr("&Application Settings"));
  connect(appSettings, &QAction::triggered, this, [this] { navigateTo(QStringLiteral("Settings_App")); });
  auto *destSettings = settingsMenu->addAction(tr("&Destination Settings"));
  connect(destSettings, &QAction::triggered, this, [this] { navigateTo(QStringLiteral("Settings_Dest")); });
  if (!commands.actions(CommandSurface::PreferencesMenu).isEmpty()) {
    settingsMenu->addSeparator();
    commands.populate(settingsMenu, CommandSurface::PreferencesMenu);
  }

  auto *helpMenu = menuBar()->addMenu(tr("&Help"));
  auto *aboutAction = helpMenu->addAction(tr("&About"));
  connect(aboutAction, &QAction::triggered, this, [this] { navigateTo(QStringLiteral("About")); });
  auto *debugAction = helpMenu->addAction(tr("&Debug"));
  connect(debugAction, &QAction::triggered, this, [this] { navigateTo(QStringLiteral("Debug")); });
}

void ApplicationWindow::buildNavigationPane(CommandRegistry &commands, QVBoxLayout *navLayout) {
  auto *host = navLayout->parentWidget();

  auto makeGroup = [&](const QString &id, const QString &label, const QString &keywords, bool expandedByDefault) -> NavGroupEntry & {
    auto *header = new NavItemButton(label, true, false, host);
    header->setObjectName(QStringLiteral("navGroup_") + id);
    header->setAccessibleName(label);
    header->setProperty("navRole", QStringLiteral("group"));
    header->setProperty("navExpanded", expandedByDefault);
    header->setCursor(Qt::PointingHandCursor);
    header->setFlat(true);
    header->setIconSize(QSize(18, 18));
    navLayout->addWidget(header);

    auto *container = new QWidget(host);
    auto *box = new QVBoxLayout(container);
    box->setContentsMargins(0, 0, 0, 2);
    box->setSpacing(2);
    container->setVisible(expandedByDefault);
    navLayout->addWidget(container);

    NavGroupEntry entry;
    entry.id = id;
    entry.header = header;
    entry.container = container;
    entry.searchText = (label + QLatin1Char(' ') + keywords).toLower();
    entry.expanded = expandedByDefault;
    m_navGroups.append(entry);
    const int idx = m_navGroups.size() - 1;
    connect(header, &QPushButton::clicked, this, [this, idx, host] {
      auto &g = m_navGroups[idx];
      g.expanded = !g.expanded;
      g.header->setProperty("navExpanded", g.expanded);
      g.header->update();
      g.container->setVisible(g.expanded);
      g.container->updateGeometry();
      if (host) {
        host->updateGeometry();
        if (auto *lay = host->layout()) lay->activate();
      }
    });
    return m_navGroups[idx];
  };

  auto addPageButton = [&](const QString &pageId, const QString &label, const QString &keywords,
                           QWidget *parentWidget, QVBoxLayout *targetLayout, const QString &groupId = {}) -> QPushButton * {
    auto *btn = new NavItemButton(label, false, !groupId.isEmpty(), parentWidget);
    btn->setObjectName(QStringLiteral("navPage_") + pageId);
    btn->setAccessibleName(label);
    btn->setProperty("navRole", QStringLiteral("page"));
    if (!groupId.isEmpty()) btn->setProperty("navChild", true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFlat(true);
    btn->setIconSize(QSize(18, 18));
    targetLayout->addWidget(btn);
    connect(btn, &QPushButton::clicked, this, [this, pageId] { navigateTo(pageId); });
    m_navPages.insert(pageId, NavPageEntry{pageId, btn, (label + QLatin1Char(' ') + keywords).toLower(), groupId});
    return btn;
  };

  auto addActionButton = [&](const QString &id, QAction *action, NavGroupEntry &group) {
    if (!action || m_commandButtons.contains(id)) return;
    auto *btn = new NavActionButton(group.container);
    btn->setObjectName(id);
    btn->setDefaultAction(action);
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setFocusPolicy(Qt::StrongFocus);
    btn->setProperty("navRole", QStringLiteral("action"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setAutoRaise(true);
    btn->setIconSize(QSize(15, 15));
    qobject_cast<QVBoxLayout *>(group.container->layout())->addWidget(btn);
    group.items.append(btn);
    m_commandButtons.insert(id, btn);
    m_commandSearchText.insert(id, (id + QLatin1Char(' ') + action->text().remove(QLatin1Char('&'))).toLower());
  };

  const QStringList windowIds = commands.ids(CommandSurface::Window);
  auto groupCommands = [&](std::initializer_list<CommandGroup> groups) {
    QStringList matched;
    for (const auto &id : windowIds) {
      const auto *spec = commands.spec(id);
      if (!spec) continue;
      for (auto g : groups) {
        if (spec->group == g) { matched.append(id); break; }
      }
    }
    return matched;
  };

  // 1. Capture (expanded group matching MainWindow.Navigation.cs)
  const auto captureIds = groupCommands({CommandGroup::Capture, CommandGroup::Schedule});
  {
    auto &captureGroup = makeGroup(QStringLiteral("Capture"), tr("Capture"),
        QStringLiteral("screenshot region fullscreen monitor window scrolling delay"), true);
    for (const auto &id : captureIds) addActionButton(id, commands.action(id), captureGroup);
  }

  // 2. Recording
  addPageButton(QStringLiteral("Recording"), tr("Recording"), QStringLiteral("screen record video gif audio mp4 webm"), host, navLayout);

  // 3. Editor (default landing page)
  addPageButton(QStringLiteral("Editor"), tr("Editor"), QStringLiteral("image annotate draw crop effects canvas"), host, navLayout);

  // 4. History
  addPageButton(QStringLiteral("History"), tr("History"), QStringLiteral("captures screenshots files recent"), host, navLayout);

  // 5. Statistics
  addPageButton(QStringLiteral("Statistics"), tr("Statistics"), QStringLiteral("stats heatmap chart count analytics"), host, navLayout);

  // 6. Workflows
  const auto workflowIds = groupCommands({CommandGroup::Workflows});
  addPageButton(QStringLiteral("Workflows"), tr("Workflows"), QStringLiteral("hotkeys shortcuts automation tasks"), host, navLayout);
  if (!workflowIds.isEmpty()) {
    auto &wfGroup = makeGroup(QStringLiteral("Workflows"), tr("Workflow Actions"), QStringLiteral("workflows shortcuts hotkeys"), true);
    for (const auto &id : workflowIds) addActionButton(id, commands.action(id), wfGroup);
  }

  // 7. Upload
  const auto uploadIds = groupCommands({CommandGroup::Upload});
  {
    auto &uploadGroup = makeGroup(QStringLiteral("Upload"), tr("Upload"), QStringLiteral("upload file destination watch folder"), true);
    for (const auto &id : uploadIds) addActionButton(id, commands.action(id), uploadGroup);
  }

  // 8. Files / Quick Actions (if registered on Window surface)
  const auto fileIds = groupCommands({CommandGroup::Files, CommandGroup::Application});
  if (!fileIds.isEmpty()) {
    auto &filesGroup = makeGroup(QStringLiteral("Files"), tr("Files & Actions"), QStringLiteral("open new image folder history palette diagnostics"), true);
    for (const auto &id : fileIds) addActionButton(id, commands.action(id), filesGroup);
  }

  // 9. Tools (page + tool command actions)
  addPageButton(QStringLiteral("Tools"), tr("Tools"), QStringLiteral("color picker qr ocr hash icon background video"), host, navLayout);
  const auto toolIds = groupCommands({CommandGroup::Tools});
  if (!toolIds.isEmpty()) {
    auto &toolsGroup = makeGroup(QStringLiteral("Tools"), tr("Tool Launchers"), QStringLiteral("color picker qr ocr hash icon background video"), true);
    for (const auto &id : toolIds) addActionButton(id, commands.action(id), toolsGroup);
  }

  // 10. Settings (expanded group with Application Settings & Destination Settings)
  const auto prefIds = groupCommands({CommandGroup::Preferences});
  {
    auto &settingsGroup = makeGroup(QStringLiteral("Settings"), tr("Settings"),
        QStringLiteral("preferences application destination theme paths watch folders"), true);
    auto *containerLayout = qobject_cast<QVBoxLayout *>(settingsGroup.container->layout());
    auto *appBtn = addPageButton(QStringLiteral("Settings_App"), tr("Application Settings"),
        QStringLiteral("theme general paths watch folders tray notifications"), settingsGroup.container, containerLayout, QStringLiteral("Settings"));
    settingsGroup.items.append(appBtn);
    auto *destBtn = addPageButton(QStringLiteral("Settings_Dest"), tr("Destination Settings"),
        QStringLiteral("uploaders sxcu destinations providers credentials"), settingsGroup.container, containerLayout, QStringLiteral("Settings"));
    settingsGroup.items.append(destBtn);
    for (const auto &id : prefIds) addActionButton(id, commands.action(id), settingsGroup);
  }

  // 11. Debug
  addPageButton(QStringLiteral("Debug"), tr("Debug"), QStringLiteral("logs diagnostics secret store troubleshooting"), host, navLayout);

  // 12. About
  addPageButton(QStringLiteral("About"), tr("About"), QStringLiteral("version links github license libraries"), host, navLayout);

  // Catch any command whose group was not explicitly listed above.
  for (const auto &id : windowIds) {
    if (m_commandButtons.contains(id)) continue;
    addActionButton(id, commands.action(id), m_navGroups.first());
  }
}

void ApplicationWindow::buildDefaultPages(CommandRegistry &) {
  const struct DefaultSpec { const char *id; QString title; QString subtitle; } defaults[] = {
    {"Editor", tr("Image Editor"), tr("Capture a region, open an image, or drop a file here to start editing.")},
    {"Recording", tr("Screen Recording"), tr("Configure and control screen and region recordings.")},
    {"History", tr("Capture History"), tr("Browse, search, edit, and share your captured images and files.")},
    {"Statistics", tr("Statistics"), tr("Review capture totals, daily heatmaps, hourly charts, and breakdowns.")},
    {"Workflows", tr("Workflows"), tr("Manage capture workflows, after-capture actions, and global shortcuts.")},
    {"Tools", tr("XerahS Tools"), tr("Select a tool from the menu to get started.")},
    {"Settings_App", tr("Application Settings"), tr("Configure theme, paths, notifications, tray behavior, and watch folders.")},
    {"Settings_Dest", tr("Destination Settings"), tr("Manage upload destinations, custom uploaders, and credentials.")},
    {"Debug", tr("Debug & Diagnostics"), tr("Inspect application diagnostics, secret store status, and logs.")},
    {"About", tr("About XerahS"), tr("Native cross-platform screen capture, annotation, and sharing tool.")},
  };
  for (const auto &spec : defaults) {
    setPageWidget(QString::fromLatin1(spec.id), makeFallbackPage(spec.title, spec.subtitle, m_contentStack));
  }
}

void ApplicationWindow::setPageWidget(const QString &pageId, QWidget *widget) {
  if (!widget || !m_contentStack) return;
  if (auto *existing = m_pages.value(pageId)) {
    const bool wasCurrent = (m_contentStack->currentWidget() == existing);
    m_contentStack->removeWidget(existing);
    existing->deleteLater();
    m_pages.insert(pageId, widget);
    m_contentStack->addWidget(widget);
    if (wasCurrent) m_contentStack->setCurrentWidget(widget);
  } else {
    m_pages.insert(pageId, widget);
    m_contentStack->addWidget(widget);
  }
  adoptDropTargets();
}

QWidget *ApplicationWindow::pageWidget(const QString &pageId) const {
  return m_pages.value(pageId, nullptr);
}

void ApplicationWindow::navigateTo(const QString &pageId) {
  auto *target = m_pages.value(pageId, nullptr);
  if (!target || !m_contentStack) return;
  m_currentPageId = pageId;
  m_contentStack->setCurrentWidget(target);

  for (auto it = m_navPages.begin(); it != m_navPages.end(); ++it) {
    const bool active = (it.key() == pageId);
    it->button->setProperty("navActive", active);
    it->button->style()->unpolish(it->button);
    it->button->style()->polish(it->button);
    if (active && !it->groupId.isEmpty()) {
      for (auto &group : m_navGroups) {
        if (group.id == it->groupId && !group.expanded) {
          group.expanded = true;
          group.header->setProperty("navExpanded", true);
          group.header->update();
          group.container->setVisible(true);
          group.container->updateGeometry();
        }
      }
    }
  }
  emit pageChanged(pageId);
}

QString ApplicationWindow::currentPageId() const {
  return m_currentPageId;
}

void ApplicationWindow::setThemeMode(ShellThemeMode mode) {
  m_themeMode = mode;
  applyCurrentTheme();
}

void ApplicationWindow::setEditorMenuActions(const QMap<QString, QAction *> &actions) {
  for (auto it = m_editorMenuDelegates.begin(); it != m_editorMenuDelegates.end(); ++it) {
    QAction *menuAction = it.value();
    menuAction->disconnect(this);
    QAction *target = actions.value(it.key(), nullptr);
    if (!target) {
      menuAction->setEnabled(false);
      continue;
    }
    menuAction->setEnabled(target->isEnabled());
    connect(target, &QAction::changed, menuAction, [menuAction, target] {
      menuAction->setEnabled(target->isEnabled());
    });
    connect(menuAction, &QAction::triggered, target, [target] {
      if (target->isEnabled()) target->trigger();
    });
  }
}

void ApplicationWindow::applyNavigationFilter(const QString &query) {
  const QString needle = query.trimmed().toLower();
  const bool filtering = !needle.isEmpty();

  for (auto &entry : m_navPages) {
    if (!entry.groupId.isEmpty()) continue;
    entry.button->setVisible(!filtering || entry.searchText.contains(needle));
  }

  for (auto &group : m_navGroups) {
    const bool headerMatches = !filtering || group.searchText.contains(needle);
    int visibleChildren = 0;
    for (auto *child : group.items) {
      QString text;
      if (auto *tool = qobject_cast<QToolButton *>(child)) {
        text = m_commandSearchText.value(tool->objectName());
      } else if (auto *btn = qobject_cast<QPushButton *>(child)) {
        for (const auto &pe : m_navPages) {
          if (pe.button == btn) { text = pe.searchText; break; }
        }
      }
      const bool childVisible = !filtering || headerMatches || text.contains(needle);
      child->setVisible(childVisible);
      if (childVisible) ++visibleChildren;
    }
    const bool groupVisible = !filtering || headerMatches || visibleChildren > 0;
    const bool showChildren = groupVisible && (filtering ? visibleChildren > 0 : group.expanded);
    group.header->setVisible(groupVisible);
    group.header->setProperty("navExpanded", showChildren);
    group.header->update();
    group.container->setVisible(showChildren);
    group.container->updateGeometry();
  }
}

void ApplicationWindow::updateNavigationIcons() {
  const QColor iconColor = isHighContrastSystemPalette(QApplication::palette())
      ? QApplication::palette().color(QPalette::WindowText)
      : appThemePalette(m_darkTheme).textSecondary;

  for (auto &group : m_navGroups) {
    if (group.header) group.header->setIcon(appIcon(iconForGroup(group.id), iconColor, 18));
  }
  for (auto it = m_navPages.begin(); it != m_navPages.end(); ++it) {
    if (it->button) it->button->setIcon(appIcon(iconForPage(it.key()), iconColor, 18));
  }
  for (auto it = m_commandButtons.begin(); it != m_commandButtons.end(); ++it) {
    if (it.value()) it.value()->setIcon(appIcon(iconForCommand(it.key()), iconColor, 15));
  }
}

void ApplicationWindow::applyCurrentTheme() {
  m_darkTheme = applyShellTheme(*this, m_themeMode);
  updateNavigationIcons();
  emit themeChanged(m_darkTheme);
}

void ApplicationWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (event && event->type() == QEvent::ApplicationPaletteChange) {
    applyCurrentTheme();
  }
}

void ApplicationWindow::activate() {
  // Restore a hidden/minimized shell without unmaximizing the user's window.
  if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
  show();
  raise();
  activateWindow();
  if (auto *modal = QApplication::activeModalWidget()) {
    modal->raise();
    modal->activateWindow();
  }
}

void ApplicationWindow::showResult(const QString &title, const QString &body, bool success) {
  m_progress->hide();
  m_progressTitle.clear();
  const auto text = body.isEmpty() ? title : title + QStringLiteral(": ") + body;
  m_status->setText(text);
  m_status->setAccessibleDescription(text);
  raiseAccessibleCue(m_status, success ? QAccessible::DescriptionChanged : QAccessible::Alert);
}

void ApplicationWindow::showProgress(const QString &title, int percent) {
  m_progressTitle = title;
  m_progress->setRange(0, 100);
  m_progress->setValue(qBound(0, percent, 100));
  m_progress->show();
  const auto text = title + tr(": Preparing pixels · %1% · Cancel Capture or Escape stops this operation.").arg(m_progress->value());
  m_status->setText(text);
  m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&event);
}

void ApplicationWindow::showActivity(const QString &title, const QString &body) {
  m_progressTitle = title;
  m_progress->setRange(0, 0);
  m_progress->show();
  const auto text = title + QStringLiteral(": ") + body;
  m_status->setText(text);
  m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&event);
}

void ApplicationWindow::cancelPendingProgress() {
  if (m_progress->isHidden()) return;
  const auto title = m_progressTitle;
  showResult(title, tr("Cancelled"), true);
}

void ApplicationWindow::addCaptureRecoveryActions(QAction *edit, QAction *discard, QAction *review, QAction *retry) {
  auto *layout = qobject_cast<QHBoxLayout *>(m_recoveryStrip->layout());
  if (!layout) {
    layout = new QHBoxLayout(m_recoveryStrip);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(8);
  }
  QList<QAction *> recoveryActions;
  for (auto *action : {edit, review, retry, discard}) {
    if (!action) continue;
    recoveryActions.append(action);
    auto *button = new QToolButton(m_recoveryStrip);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setObjectName(action == edit ? QStringLiteral("edit-retained-capture") :
        action == review ? QStringLiteral("review-retained-capture") :
        action == retry ? QStringLiteral("retry-retained-capture") : QStringLiteral("discard-retained-capture"));
    layout->addWidget(button);
  }
  layout->addStretch();
  auto *menu = menuBar()->addMenu(tr("&Retained Capture"));
  if (edit) menu->addAction(edit);
  if (review) menu->addAction(review);
  if (retry) menu->addAction(retry);
  if (discard) menu->addAction(discard);

  const auto syncRecoveryStrip = [this, recoveryActions, menu] {
    bool anyEnabled = false;
    for (auto *a : recoveryActions) anyEnabled |= (a && a->isEnabled());
    m_recoveryStrip->setVisible(anyEnabled);
    menu->menuAction()->setVisible(anyEnabled);
  };
  for (auto *action : recoveryActions) connect(action, &QAction::changed, m_recoveryStrip, syncRecoveryStrip);
  syncRecoveryStrip();
  adoptDropTargets();
}

void ApplicationWindow::adoptDropTargets() {
  // The central widget covers the window, so a drop lands on a child unless
  // that child accepts it and this filter forwards the local paths.
  for (auto *widget : findChildren<QWidget *>()) {
    widget->setAcceptDrops(true);
    widget->installEventFilter(this);
  }
}

void ApplicationWindow::dragEnterEvent(QDragEnterEvent *event) { acceptLocalFiles(event); }
void ApplicationWindow::dragMoveEvent(QDragMoveEvent *event) { acceptLocalFiles(event); }
void ApplicationWindow::dropEvent(QDropEvent *event) {
  const auto paths = localFilePaths(event->mimeData());
  if (paths.isEmpty()) { event->ignore(); return; }
  event->acceptProposedAction();
  emit filesDropped(paths);
}

bool ApplicationWindow::eventFilter(QObject *watched, QEvent *event) {
  Q_UNUSED(watched);
  if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove && event->type() != QEvent::Drop) return false;
  if (event->type() == QEvent::Drop) dropEvent(static_cast<QDropEvent *>(event));
  else acceptLocalFiles(static_cast<QDropEvent *>(event));
  return true;
}

}  // namespace xerahs::app
