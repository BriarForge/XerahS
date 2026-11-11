#pragma once
#include "storage/Compatibility.h"
#include <QByteArray>
#include <vector>

namespace xerahs::history {
// Read-only candidates for the pinned baseline's comma-separated JSON objects.
// Paths, URLs and tags remain data, never grants to read media or run actions.
struct LegacyHistoryRecord {
  qsizetype index;
  QJsonObject fields;
};
struct LegacyHistoryPreview {
  std::vector<LegacyHistoryRecord> records;
  QJsonArray quarantine;
  compatibility::ImportReport report;
  QString code;
  bool ok() const { return code.isEmpty(); }
};
constexpr qint64 kLegacyHistoryMaxBytes = 1024 * 1024;
LegacyHistoryPreview previewLegacyHistory(const QByteArray &bytes);
}
