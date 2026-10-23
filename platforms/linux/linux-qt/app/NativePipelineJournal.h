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
struct FileUploadHistoryRecord {
  QString taskId, path, origin, destinationName, failureCode;
  QDateTime createdAt;
  xerahs::actions::UploadResult result;
};
struct JournalListing { std::vector<JournalEntry> entries; QString diagnostic; };
// Linux I/O boundary. Independent descriptor locks serialize writers; snapshots
// compare exact bytes. No method reads, retries or deletes a capture artifact.
class NativePipelineJournal final : public xerahs::actions::PipelineJournalStoragePort {
public:
  explicit NativePipelineJournal(QString directory) : m_directory(std::move(directory)) {}
  static QString defaultDirectory();
  JournalWrite replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const override;
  JournalListing list() const;
  JournalWrite recordFileUpload(const FileUploadHistoryRecord &upload) const;
  JournalWrite erase(const QString &id, const QByteArray &expected) const;
private:
  QString m_directory;
};
using PipelineJournalStorage = std::shared_ptr<const NativePipelineJournal>;
} // namespace xerahs::app
