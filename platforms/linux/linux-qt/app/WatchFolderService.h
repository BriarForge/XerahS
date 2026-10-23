// CORE-024, CORE-025: native watcher with reconciliation and bounded actions.
#pragma once
#include "watch/WatchFolders.h"
#include <QFileSystemWatcher>
#include <QTimer>
#include <QObject>
#include <functional>

namespace xerahs::app {
class FileUploadController;
class WatchFolderService final : public QObject {
  Q_OBJECT
public:
  WatchFolderService(watch::WatchFolders &store, FileUploadController &uploads,
      std::function<QString()> captures, watch::StabilityTracker::Clock clock = {}, QObject *parent = nullptr);
  void start();
  void stop();
  void rescan();
  QString setRules(const QVector<watch::Rule> &rules);
  const QVector<watch::Rule> &rules() const { return m_store.rules(); }
  QString capturesFolder() const { return m_captures(); }
signals:
  void notify(const QString &message, bool success);
  void processed(const QString &path, bool success);
private:
  void complete(const QString &path, bool success, const QString &error);
  watch::WatchFolders &m_store;
  FileUploadController &m_uploads;
  std::function<QString()> m_captures;
  watch::StabilityTracker m_stability;
  QFileSystemWatcher m_watcher;
  QTimer m_timer;
  QSet<QString> m_attempted, m_reported;
  QString m_pendingPath, m_pendingKey;
  bool m_running = false;
  bool m_blocked = false;
};
}
