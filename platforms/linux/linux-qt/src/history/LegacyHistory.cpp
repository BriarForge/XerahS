#include "LegacyHistory.h"
#include "storage/JsonBoundary.h"
#include <QDateTime>

namespace xerahs::history {
namespace {
LegacyHistoryPreview jsonPreview(const QByteArray &bytes, qint64 maximum, qint64 elements, LegacyHistoryFormat format) {
  LegacyHistoryPreview result;
  result.format = format;
  if (bytes.size() > maximum) { result.code = "legacy-history-too-large"; return result; }
  // HistoryManagerJSON appends objects separated by commas, without brackets.
  const auto parsed = compatibility::readJson('[' + bytes + ']', {maximum + 2, 64, elements});
  if (!parsed.ok()) { result.code = "legacy-history-" + parsed.code; return result; }
  const QStringList strings{"FileName", "FilePath", "DateTime", "Type", "Host", "URL", "ThumbnailURL", "DeletionURL", "ShortenedURL"};
  const auto records = parsed.document.array();
  for (qsizetype index = 0; index < records.size(); ++index) {
    const auto value = records.at(index);
    const auto fields = value.toObject();
    bool valid = value.isObject();
    if (format == LegacyHistoryFormat::Sqlite) {
      const auto id = fields.value("Id").toString(); bool parsedId = false;
      const auto number = id.toLongLong(&parsedId);
      valid = valid && fields.value("Id").isString() && parsedId && QString::number(number) == id;
    }
    for (const auto &key : strings) if (fields.contains(key) && !fields.value(key).isString()) valid = false;
    valid = valid && !fields.value("FileName").toString().isEmpty() &&
        (!fields.value("FilePath").toString().isEmpty() || !fields.value("URL").toString().isEmpty()) &&
        QDateTime::fromString(fields.value("DateTime").toString(), Qt::ISODate).isValid();
    if (fields.contains("Tags")) {
      valid = valid && fields.value("Tags").isObject();
      const auto tags = fields.value("Tags").toObject();
      for (auto it = tags.begin(); it != tags.end(); ++it) if (!it.value().isNull() && !it.value().isString()) valid = false;
    }
    auto extra = fields;
    for (const auto &key : strings) extra.remove(key);
    auto unknownTags = extra.take("Tags").toObject();
    for (const auto *key : {"AnnotationSidecarPath", "WindowTitle", "ProcessName", "Favorite", "Tag", "Errors"}) unknownTags.remove(QLatin1String(key));
    extra.insert("Tags", unknownTags);
    const bool unsafe = compatibility::containsUnsafeUnknown(extra);
    if (!valid || unsafe) {
      result.quarantine.append(QJsonObject{{"index", index}, {"record", value}});
      result.report.add(compatibility::ImportDisposition::Quarantined);
      result.report.addRecord(index, "legacy-history", {}, compatibility::ImportDisposition::Quarantined,
          unsafe ? "unsafe-content" : "invalid-record");
    } else {
      // Keep timestamps and compatible unknown fields verbatim. The legacy JSON
      // serializer supplies no task ID or operation state; neither is invented.
      result.records.push_back({index, fields, {}, format});
      result.report.add(compatibility::ImportDisposition::Retained);
    }
  }
  return result;
}
}
LegacyHistoryPreview previewLegacyHistory(const QByteArray &bytes) {
  if (bytes.size() > kLegacyHistoryMaxBytes) { LegacyHistoryPreview result; result.code = "legacy-history-too-large"; return result; }
  auto text = bytes;
  if (text.startsWith(QByteArray::fromHex("efbbbf"))) text.remove(0, 3);
  const auto start = text.trimmed();
  if (start.startsWith('<') || start.startsWith(QByteArray::fromHex("fffe")) || start.startsWith(QByteArray::fromHex("feff")))
    return previewLegacyHistoryXml(bytes);
  return jsonPreview(text, kLegacyHistoryMaxBytes, 100000, LegacyHistoryFormat::Json);
}
LegacyHistoryPreview previewLegacyHistoryRecords(const QByteArray &records, LegacyHistoryFormat format) {
  return jsonPreview(records, kLegacySqliteRowsMaxBytes, 1000000, format);
}
LegacyHistoryPreview previewLegacyHistorySnapshot(const LegacyHistorySnapshot &snapshot) {
  if (const auto *text = std::get_if<QByteArray>(&snapshot)) return previewLegacyHistory(*text);
  const auto &sqlite = std::get<SqliteHistorySnapshot>(snapshot);
  if (sqlite.database.size() < 100 || sqlite.database.size() > kLegacySqliteMaxBytes ||
      !sqlite.database.startsWith(QByteArray("SQLite format 3\0", 16))) {
    LegacyHistoryPreview result; result.format = LegacyHistoryFormat::Sqlite; result.code = "legacy-history-sqlite-snapshot-invalid"; return result;
  }
  return previewLegacyHistoryRecords(sqlite.records, LegacyHistoryFormat::Sqlite);
}
const QByteArray &legacyHistorySnapshotBytes(const LegacyHistorySnapshot &snapshot) {
  if (const auto *text = std::get_if<QByteArray>(&snapshot)) return *text;
  return std::get<SqliteHistorySnapshot>(snapshot).database;
}
QString legacyHistoryFormatName(LegacyHistoryFormat format) {
  switch (format) {
    case LegacyHistoryFormat::Json: return "legacy-json-fragments";
    case LegacyHistoryFormat::Xml: return "legacy-xml-fragments";
    case LegacyHistoryFormat::Sqlite: return "legacy-sqlite-snapshot";
    case LegacyHistoryFormat::Mixed: return "mixed";
  }
  return {};
}
}
