#pragma once
#include "actions/PipelineJournal.h"
#include <functional>
namespace xerahs::app {
using RecordWrite = xerahs::actions::JournalWrite;
struct PrivateRecord { QString id; QByteArray bytes; QString diagnostic; };
struct RecordListing { std::vector<PrivateRecord> entries; QString diagnostic; };
// Format-independent private descriptor I/O. Validation belongs to Qt Core.
// Error codes retain the original pipeline-journal prefix for compatibility.
class NativePrivateRecords final {
public:
  using Validator = std::function<bool(const QByteArray &, const QString &)>;
  NativePrivateRecords(QString directory, Validator validate) : m_directory(std::move(directory)), m_validate(std::move(validate)) {}
  RecordWrite replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const;
  RecordListing list() const;
  RecordWrite erase(const QString &id, const QByteArray &expected) const;
  // Renames an unreadable record to <id>.json.corrupt[.N] (never deletes it) when the bytes still match.
  RecordWrite quarantine(const QString &id, const QByteArray &expected) const;
  // CORE-027 on decision CORE-027-STORE-RETENTION-2026-11-23: removes records
  // for which `expired` returns true, under the same directory flock and
  // read/validate discipline as erase. Unreadable, unsafe or invalid records
  // are never retention candidates (the quarantine paths own them); secure
  // delete, when requested, overwrites through the shared storage primitive
  // before the unlink. Returns the removed count.
  int sweep(const std::function<bool(const QByteArray &bytes, const QString &id)> &expired, bool secureDelete) const;
private:
  QString m_directory;
  Validator m_validate;
};
} // namespace xerahs::app
