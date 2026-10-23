#pragma once
#include <QMainWindow>
#include <QMap>
#include <QStringList>
class QAction;
class QLabel;
class QProgressBar;
namespace xerahs::app {
// Native shell surfaces reuse the same actual actions as the tray. No capture,
// editor, preference or publication behavior is reimplemented by this view.
class ApplicationWindow final : public QMainWindow {
  Q_OBJECT
public:
  explicit ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent = nullptr);
  void activate();
  void showResult(const QString &title, const QString &body, bool success);
  void showProgress(const QString &title, int percent);
  void showActivity(const QString &title, const QString &body);
  void cancelPendingProgress();
  void addCaptureRecoveryActions(QAction *edit, QAction *discard, QAction *review = nullptr, QAction *retry = nullptr);
signals:
  // Local files dropped on this window, in drop order. Non-local URLs are omitted.
  void filesDropped(const QStringList &paths);
protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  void adoptDropTargets();
  QLabel *m_status = nullptr;
  QProgressBar *m_progress = nullptr;
  QString m_progressTitle;
};
} // namespace xerahs::app
