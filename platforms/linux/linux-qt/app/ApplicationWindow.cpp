#include "ApplicationWindow.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QVBoxLayout>
namespace xerahs::app {
ApplicationWindow::ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent) : QMainWindow(parent) {
  setObjectName(QStringLiteral("applicationWindow")); setWindowTitle(QStringLiteral("XerahS"));
  setAccessibleName(QStringLiteral("XerahS")); setAccessibleDescription(QStringLiteral("Screen capture and image editor commands"));
  auto *central = new QWidget(this); auto *layout = new QVBoxLayout(central);
  auto *intro = new QLabel(QStringLiteral("Capture your screen or open an image to edit."), central); intro->setWordWrap(true); layout->addWidget(intro);
  auto *commands = new QGridLayout; layout->addLayout(commands);
  int index = 0;
  for (const auto &id : {QString("capture-region"), QString("cancel-capture"), QString("new-image"), QString("open-image"), QString("captures-folder"), QString("editor-appearance")}) {
    auto *action = actions.value(id); if (!action) continue;
    auto *button = new QToolButton(central); button->setObjectName(id); button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly); button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); commands->addWidget(button, index / 2, index % 2); ++index;
  }
  layout->addStretch(); m_status = new QLabel(QStringLiteral("Ready"), central);
  m_status->setObjectName(QStringLiteral("operationStatus")); m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(QStringLiteral("Most recent operation result")); layout->addWidget(m_status);
  setCentralWidget(central);
  auto *file = menuBar()->addMenu(QStringLiteral("&File"));
  for (const auto &id : {QString("new-image"), QString("open-image"), QString("captures-folder"), QString("quit")}) if (auto *action = actions.value(id)) file->addAction(action);
  auto *capture = menuBar()->addMenu(QStringLiteral("&Capture"));
  for (const auto &id : {QString("capture-region"), QString("cancel-capture")}) if (auto *action = actions.value(id)) capture->addAction(action);
  auto *preferences = menuBar()->addMenu(QStringLiteral("&Preferences"));
  if (auto *action = actions.value(QStringLiteral("editor-appearance"))) preferences->addAction(action);
  resize(640, 360);
}
void ApplicationWindow::activate() {
  // Restore a hidden/minimized shell without unmaximizing the user's window.
  if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
  show(); raise(); activateWindow();
  if (auto *modal = QApplication::activeModalWidget()) { modal->raise(); modal->activateWindow(); }
}
void ApplicationWindow::showResult(const QString &title, const QString &body, bool success) {
  const auto text = body.isEmpty() ? title : title + QStringLiteral(": ") + body;
  m_status->setText(text); m_status->setAccessibleDescription(text);
  QAccessibleEvent event(m_status, success ? QAccessible::DescriptionChanged : QAccessible::Alert);
  QAccessible::updateAccessibility(&event);
}
} // namespace xerahs::app
