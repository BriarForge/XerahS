#pragma once
#include "storage/Compatibility.h"
#include <QByteArray>
#include <vector>
#include <variant>

namespace xerahs::history {
enum class LegacyHistoryFormat { Json, Xml, Sqlite, Mixed };
struct SqliteHistorySnapshot { QByteArray database; QByteArray records; };
using LegacyHistorySnapshot = std::variant<QByteArray, SqliteHistorySnapshot>;
// Read-only candidates normalized from the pinned legacy history formats.
// Paths, URLs and tags remain data, never grants to read media or run actions.
struct LegacyHistoryRecord {
  qsizetype index;
  QJsonObject fields;
  QString archiveImportId = {}; // snapshot provenance, never a legacy task ID
  LegacyHistoryFormat format = LegacyHistoryFormat::Json;
};
struct LegacyHistoryPreview {
  std::vector<LegacyHistoryRecord> records;
  QJsonArray quarantine;
  compatibility::ImportReport report;
  QString code;
  LegacyHistoryFormat format = LegacyHistoryFormat::Json;
  bool ok() const { return code.isEmpty(); }
};
constexpr qint64 kLegacyHistoryMaxBytes = 1024 * 1024;
constexpr qint64 kLegacySqliteMaxBytes = 32 * 1024 * 1024;
constexpr qint64 kLegacySqliteRowsMaxBytes = 16 * 1024 * 1024;
LegacyHistoryPreview previewLegacyHistory(const QByteArray &bytes);
LegacyHistoryPreview previewLegacyHistoryRecords(const QByteArray &records, LegacyHistoryFormat format);
LegacyHistoryPreview previewLegacyHistoryXml(const QByteArray &bytes);
LegacyHistoryPreview previewLegacyHistorySnapshot(const LegacyHistorySnapshot &snapshot);
const QByteArray &legacyHistorySnapshotBytes(const LegacyHistorySnapshot &snapshot);
QString legacyHistoryFormatName(LegacyHistoryFormat format);
}
