#pragma once
#include "AppTheme.h"
#include "CommandRegistry.h"
#include <QMainWindow>
#include <QMap>
#include <QStringList>

class QAction;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace xerahs::app {
class CommandRegistry;

// Native shell window matching the ShareX/XerahS reference UI model:
// top menu bar, left searchable navigation tree with Lucide icons, right
// content frame hosting embedded pages, and a bottom operation status strip.
// All command buttons reuse the exact shared QActions from CommandRegistry.
class ApplicationWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit ApplicationWindow(CommandRegistry &commands, QWidget *parent = nullptr);
  explicit ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent = nullptr);

  void activate();
  void showResult(const QString &title, const QString &body, bool success);
  void showProgress(const QString &title, int percent);
  void showActivity(const QString &title, const QString &body);
  void cancelPendingProgress();
  void addCaptureRecoveryActions(QAction *edit, QAction *discard, QAction *review = nullptr, QAction *retry = nullptr);

  // Embedded shell page hosting (Editor, Recording, History, Statistics,
  // Workflows, Tools, Settings_App, Settings_Dest, Debug, About).
  void setPageWidget(const QString &pageId, QWidget *widget);
  QWidget *pageWidget(const QString &pageId) const;
  void navigateTo(const QString &pageId);
  QString currentPageId() const;

  void setThemeMode(ShellThemeMode mode);
  ShellThemeMode themeMode() const { return m_themeMode; }
  bool isDarkTheme() const { return m_darkTheme; }

  // Connects top menu bar Edit/View actions (undo, redo, copy, save, save-as,
  // zoom-in, zoom-out, reset-zoom, fit-image) to an embedded editor page.
  void setEditorMenuActions(const QMap<QString, QAction *> &actions);

signals:
  // Local files dropped on this window, in drop order. Non-local URLs are omitted.
  void filesDropped(const QStringList &paths);
  void pageChanged(const QString &pageId);
  void navigationSearchChanged(const QString &query);
  void themeChanged(bool dark);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  struct NavGroupEntry {
    QString id;
    QPushButton *header = nullptr;
    QWidget *container = nullptr;
    QString searchText;
    bool expanded = true;
    QList<QWidget *> items;
  };
  struct NavPageEntry {
    QString pageId;
    QPushButton *button = nullptr;
    QString searchText;
    QString groupId;
  };

  void build(CommandRegistry &commands);
  void buildMenus(CommandRegistry &commands);
  void buildNavigationPane(CommandRegistry &commands, QVBoxLayout *navLayout);
  void buildDefaultPages(CommandRegistry &commands);
  void applyNavigationFilter(const QString &query);
  void updateNavigationIcons();
  void applyCurrentTheme();
  void adoptDropTargets();

  CommandRegistry m_legacyCommands;
  ShellThemeMode m_themeMode = ShellThemeMode::System;
  bool m_darkTheme = true;

  QLineEdit *m_navSearch = nullptr;
  QStackedWidget *m_contentStack = nullptr;
  QFrame *m_recoveryStrip = nullptr;
  QLabel *m_status = nullptr;
  QProgressBar *m_progress = nullptr;
  QString m_progressTitle;
  QString m_currentPageId;

  QMap<QString, QWidget *> m_pages;
  QMap<QString, NavPageEntry> m_navPages;
  QList<NavGroupEntry> m_navGroups;
  QMap<QString, QToolButton *> m_commandButtons;
  QMap<QString, QString> m_commandSearchText;
  QMap<QString, QAction *> m_editorMenuDelegates;
};

}  // namespace xerahs::app
