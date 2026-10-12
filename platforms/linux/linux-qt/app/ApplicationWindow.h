#pragma once
#include <QMainWindow>
#include <QMap>
class QAction;
class QLabel;
class QProgressBar;
namespace xerahs::app {
// Native shell surfaces reuse the same actual actions as the tray. No capture,
// editor, preference or publication behavior is reimplemented by this view.
class ApplicationWindow final : public QMainWindow {
public:
  explicit ApplicationWindow(const QMap<QString, QAction *> &actions, QWidget *parent = nullptr);
  void activate();
  void showResult(const QString &title, const QString &body, bool success);
  void showProgress(const QString &title, int percent);
  void showActivity(const QString &title, const QString &body);
  void cancelPendingProgress();
private:
  QLabel *m_status = nullptr;
  QProgressBar *m_progress = nullptr;
  QString m_progressTitle;
};
} // namespace xerahs::app
