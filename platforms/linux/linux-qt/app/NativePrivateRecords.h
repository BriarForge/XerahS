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
private:
  QString m_directory;
  Validator m_validate;
};
} // namespace xerahs::app
