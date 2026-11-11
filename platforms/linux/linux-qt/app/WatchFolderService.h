// CORE-024, CORE-025: native watcher with reconciliation and bounded actions.
#pragma once
#include "watch/WatchFolders.h"
#include "OwnedWork.h"
#include <QFileSystemWatcher>
#include <QTimer>
#include <QObject>
#include "workflows/FileOperationExecution.h"
#include <QJsonObject>
#include <functional>
#include <memory>

namespace xerahs::app {
class FileUploadController;
class WatchFolderService final : public QObject {
  Q_OBJECT
public:
  WatchFolderService(watch::WatchFolders &store, FileUploadController &uploads,
      std::function<QString()> captures, watch::StabilityTracker::Clock clock = {}, QObject *parent = nullptr);
  ~WatchFolderService() override;
  // Test seam: called on the worker thread before each copied chunk.
  void setFileOperationHook(std::function<void(const QString &)> hook);
  // WFCAT-002: execution records for copy/move; empty uses the shared directory.
  // Test seam: called on the worker thread after the copy committed, before the source is removed.
  void setBeforeRemoveHook(std::function<void(const QString &)> hook);
  void setExecutionWriter(workflows::WorkflowExecution::Writer writer);
  void start();
  void stop();
  void rescan();
  QString setRules(const QVector<watch::Rule> &rules);
  const QVector<watch::Rule> &rules() const { return m_store.rules(); }
  QString capturesFolder() const { return m_captures(); }
signals:
  void notify(const QString &message, bool success);
  void activityChanged(const QString &text);
  void terminalObserved(const QJsonObject &observation);
  void processed(const QString &path, bool success);
  void progress(const QString &path, qint64 done, qint64 total);
private:
  void startFileWork(const QString &path, const QString &key, const watch::Rule &rule);
  struct FileResult { QString error; bool read = false, copied = false; };
  void finishFileWork(const QString &path, const FileResult &result);
  void complete(const QString &path, bool success, const QString &error);
  watch::WatchFolders &m_store;
  FileUploadController &m_uploads;
  std::function<QString()> m_captures;
  watch::StabilityTracker m_stability;
  QFileSystemWatcher m_watcher;
  QTimer m_timer;
  QSet<QString> m_attempted, m_reported;
  QString m_pendingPath, m_pendingKey;
  std::unique_ptr<workflows::WorkflowExecution> m_execution;
  workflows::WorkflowExecution::Writer m_executionWriter;
  QJsonObject m_terminalObservation;
  QString m_terminalState;  // shared terminal projection of the pending operation
  OwnedWork m_work;
  std::function<void(const QString &)> m_fileHook, m_removeHook;
  bool m_running = false;
  bool m_blocked = false;
};
}
