#pragma once
#include "PipelineResultJson.h"
#include <QByteArray>
#include <optional>
#include <memory>

namespace xerahs::actions {
constexpr qint64 maximumJournalBytes = 1048576;
bool validJournalTaskId(const QString &id);
// A write-ahead observation, never authority to repeat an action or delete an
// artifact. Only the result metadata is durable; image pixels are not stored.
struct PipelineJournalSnapshot {
  PipelineResult result;
  QStringList selected;
  QDateTime createdAt;
  std::vector<SavedFileArtifact> files;
  std::optional<QString> pendingAction;
  std::optional<QString> pendingSavePath;
  bool terminal = false;
};
QByteArray serializePipelineJournal(const PipelineJournalSnapshot &snapshot);
struct RecoveredPipelineJournal {
  QString taskId;
  QStringList selected;
  QDateTime createdAt;
  QJsonObject report;
  std::optional<QString> pendingAction;
  std::optional<QString> pendingSavePath;
  bool terminal = false;
};
struct PipelineJournalParse {
  std::optional<RecoveredPipelineJournal> record;
  QString diagnostic;
};
PipelineJournalParse parsePipelineJournal(const QByteArray &bytes, const QString &expectedTaskId);
struct JournalWrite { bool stored = false; QString diagnostic; };
class PipelineJournalStoragePort {
public:
  virtual ~PipelineJournalStoragePort() = default;
  virtual JournalWrite replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const = 0;
};
// Qt Core-only write-ahead decisions, shared with the native pipeline. Storage
// performs atomic I/O; the recorder decides what is known and when a new
// external action is permitted. No restart record can authorize a retry.
class PipelineJournalRecorder {
public:
  PipelineJournalRecorder(std::shared_ptr<const PipelineJournalStoragePort> storage, const QString &id,
                          const QStringList &selected, const QDateTime &createdAt,
                          const std::optional<PipelineRetryLink> &retry = {}, const std::vector<SavedFileArtifact> &initialFiles = {});
  bool begin();
  bool before(const ActionSpec &action);
  bool beforePublish(const QString &path);
  void selectionChanged(const QStringList &selected);
  void completed(const ActionResult &result, const std::vector<SavedFileArtifact> &files);
  void finish(const PipelineResult &result, const std::vector<SavedFileArtifact> &files);
  const QString &diagnostic() const { return m_diagnostic; }
private:
  bool persist();
  std::shared_ptr<const PipelineJournalStoragePort> m_storage;
  PipelineJournalSnapshot m_snapshot;
  std::optional<QByteArray> m_expected;
  QString m_diagnostic;
};
} // namespace xerahs::actions
