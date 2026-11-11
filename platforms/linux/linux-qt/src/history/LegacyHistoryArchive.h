#pragma once
#include "LegacyHistory.h"
#include <functional>

namespace xerahs::history {
struct LegacyHistoryArchiveRead {
  LegacyHistoryPreview preview;
  QByteArray snapshot;
  QByteArray recoverySource;
  QString lastImportId;
  QString recoveryImportId;
  QString code;
};
// Imported records are an inert private archive, never pipeline receipts or
// grants to open media, follow URLs, upload, retry or delete original files.
class LegacyHistoryArchive final {
public:
  explicit LegacyHistoryArchive(QString path) : m_path(std::move(path)) {}
  LegacyHistoryArchiveRead read() const;
  QString importSnapshot(const QByteArray &source, const QByteArray &expected, bool confirmed,
      const std::function<bool()> &cancelled = {}) const;
  QString undo(const QString &importId, const QByteArray &expected, bool confirmed,
      const std::function<bool()> &cancelled = {}) const;
  QString removeRecovery(const QString &importId, const QByteArray &expected, bool confirmed,
      const std::function<bool()> &cancelled = {}) const;
private:
  QString m_path;
};
}
