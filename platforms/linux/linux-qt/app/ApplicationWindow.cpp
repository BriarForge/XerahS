#include "ApplicationWindow.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QProgressBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
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
}  // namespace
ApplicationWindow::ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("applicationWindow")); setWindowTitle(QStringLiteral("XerahS"));
  setAccessibleName(QStringLiteral("XerahS")); setAccessibleDescription(QStringLiteral("Screen capture and image editor commands"));
  auto *central = new QWidget(this); auto *layout = new QVBoxLayout(central);
  auto *intro = new QLabel(QStringLiteral("Capture your screen or open an image to edit."), central); intro->setWordWrap(true); layout->addWidget(intro);
  auto *commands = new QGridLayout; layout->addLayout(commands);
  int index = 0;
  for (const auto &id : {QString("capture-region"), QString("capture-full-screen"), QString("capture-monitor"), QString("capture-active-window"), QString("capture-region-choose-actions"), QString("capture-workflows"), QString("enable-workflow-shortcuts"), QString("disable-workflow-shortcuts"), QString("cancel-capture"), QString("new-image"), QString("open-image"), QString("upload-file"), QString("captures-folder"), QString("capture-history"), QString("editor-appearance"), QString("upload-destinations"), QString("pick-colour")}) {
    auto *action = actions.value(id); if (!action) continue;
    auto *button = new QToolButton(central); button->setObjectName(id); button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly); button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); commands->addWidget(button, index / 2, index % 2); ++index;
  }
  layout->addStretch(); m_status = new QLabel(QStringLiteral("Ready"), central);
  m_status->setObjectName(QStringLiteral("operationStatus")); m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(QStringLiteral("Most recent operation result")); layout->addWidget(m_status);
  m_progress = new QProgressBar(central); m_progress->setObjectName(QStringLiteral("operationProgress"));
  m_progress->setAccessibleName(QStringLiteral("Operation progress")); m_progress->setRange(0, 100);
  m_progress->hide(); layout->addWidget(m_progress);
  setCentralWidget(central);
  auto *file = menuBar()->addMenu(QStringLiteral("&File"));
  for (const auto &id : {QString("new-image"), QString("open-image"), QString("upload-file"), QString("captures-folder"), QString("capture-history"), QString("pick-colour"), QString("quit")}) if (auto *action = actions.value(id)) file->addAction(action);
  auto *capture = menuBar()->addMenu(QStringLiteral("&Capture"));
  for (const auto &id : {QString("capture-region"), QString("capture-full-screen"), QString("capture-monitor"), QString("capture-active-window"), QString("capture-region-choose-actions"), QString("capture-workflows"), QString("enable-workflow-shortcuts"), QString("disable-workflow-shortcuts"), QString("cancel-capture")}) if (auto *action = actions.value(id)) capture->addAction(action);
  if (auto *cancel = actions.value(QStringLiteral("cancel-capture"))) {
    cancel->setShortcut(QKeySequence(Qt::Key_Escape)); cancel->setShortcutContext(Qt::WindowShortcut); addAction(cancel);
  }
  auto *preferences = menuBar()->addMenu(QStringLiteral("&Preferences"));
  if (auto *action = actions.value(QStringLiteral("upload-destinations"))) preferences->addAction(action);
  if (auto *action = actions.value(QStringLiteral("settings"))) preferences->addAction(action);
  if (auto *action = actions.value(QStringLiteral("editor-appearance"))) preferences->addAction(action);
  resize(640, 360);
  setAcceptDrops(true);
  adoptDropTargets();
}
void ApplicationWindow::activate() {
  // Restore a hidden/minimized shell without unmaximizing the user's window.
  if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
  show(); raise(); activateWindow();
  if (auto *modal = QApplication::activeModalWidget()) { modal->raise(); modal->activateWindow(); }
}
void ApplicationWindow::showResult(const QString &title, const QString &body, bool success) {
  m_progress->hide(); m_progressTitle.clear();
  const auto text = body.isEmpty() ? title : title + QStringLiteral(": ") + body;
  m_status->setText(text); m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, success ? QAccessible::DescriptionChanged : QAccessible::Alert);
  QAccessible::updateAccessibility(&event);
}
void ApplicationWindow::showProgress(const QString &title, int percent) {
  m_progressTitle = title;
  m_progress->setRange(0, 100);
  m_progress->setValue(qBound(0, percent, 100)); m_progress->show();
  const auto text = title + QStringLiteral(": Preparing pixels · %1% · Cancel Capture or Escape stops this operation.").arg(m_progress->value());
  m_status->setText(text); m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
}
void ApplicationWindow::showActivity(const QString &title, const QString &body) {
  m_progressTitle = title; m_progress->setRange(0, 0); m_progress->show();
  const auto text = title + QStringLiteral(": ") + body;
  m_status->setText(text); m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, QAccessible::DescriptionChanged); QAccessible::updateAccessibility(&event);
}
void ApplicationWindow::cancelPendingProgress() {
  if (m_progress->isHidden()) return;
  const auto title = m_progressTitle;
  showResult(title, QStringLiteral("Cancelled"), true);
}
void ApplicationWindow::addCaptureRecoveryActions(QAction *edit, QAction *discard, QAction *review, QAction *retry) {
  auto *layout = qobject_cast<QVBoxLayout *>(centralWidget()->layout());
  auto *commands = new QGridLayout;
  int index = 0;
  for (auto *action : {edit, review, retry, discard}) {
    if (!action) continue;
    auto *button = new QToolButton(centralWidget()); button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly); button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setObjectName(action == edit ? QStringLiteral("edit-retained-capture") :
        action == review ? QStringLiteral("review-retained-capture") : action == retry ? QStringLiteral("retry-retained-capture") : QStringLiteral("discard-retained-capture"));
    commands->addWidget(button, index / 2, index % 2); ++index;
  }
  layout->insertLayout(2, commands);
  auto *menu = menuBar()->addMenu(QStringLiteral("&Retained Capture"));
  if (edit) menu->addAction(edit);
  if (review) menu->addAction(review);
  if (retry) menu->addAction(retry);
  if (discard) menu->addAction(discard);
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
} // namespace xerahs::app
