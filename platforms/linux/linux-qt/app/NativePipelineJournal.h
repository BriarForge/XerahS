#pragma once
#include "actions/PipelineJournal.h"
#include <memory>
#include <optional>

namespace xerahs::app {
using JournalWrite = xerahs::actions::JournalWrite;
struct JournalEntry {
  QString taskId;
  QByteArray bytes;
  xerahs::actions::PipelineJournalParse parsed;
};
// CORE-020, CORE-023: source files are references, never capture-created artifacts.
enum class FileUploadJournalState { Terminal, Interrupted };
struct FileUploadHistoryRecord {
  QString taskId, path, origin, destinationName, failureCode;
  QDateTime createdAt;
  xerahs::actions::UploadResult result;
  std::optional<xerahs::actions::FailureClass> failureClass = {};
};
struct JournalListing { std::vector<JournalEntry> entries; QString diagnostic; int quarantined = 0; };
// Linux I/O boundary. Independent descriptor locks serialize writers; snapshots
// compare exact bytes. No method reads, retries or deletes a capture artifact.
class NativePipelineJournal final : public xerahs::actions::PipelineJournalStoragePort {
public:
  explicit NativePipelineJournal(QString directory) : m_directory(std::move(directory)) {}
  static QString defaultDirectory();
  JournalWrite replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const override;
  JournalListing list() const;
  // CORE-023: like list(), but unparseable (corrupt/truncated) records are renamed to <id>.json.corrupt so
  // they stop occupying history slots and are preserved for manual recovery. Unsupported-version, unsafe
  // and unreadable files are left untouched. `quarantined` counts the files set aside.
  JournalListing listRecovering() const;
  JournalWrite recordFileUpload(const FileUploadHistoryRecord &upload, FileUploadJournalState state = FileUploadJournalState::Terminal) const;
  JournalWrite erase(const QString &id, const QByteArray &expected) const;
private:
  QString m_directory;
};
using PipelineJournalStorage = std::shared_ptr<const NativePipelineJournal>;
} // namespace xerahs::app
