#include "LegacyHistory.h"
#include "storage/JsonBoundary.h"
#include <QDateTime>

namespace xerahs::history {
LegacyHistoryPreview previewLegacyHistory(const QByteArray &bytes) {
  LegacyHistoryPreview result;
  if (bytes.size() > kLegacyHistoryMaxBytes) { result.code = "legacy-history-too-large"; return result; }
  // HistoryManagerJSON appends objects separated by commas, without brackets.
  const auto parsed = compatibility::readJson('[' + bytes + ']', {kLegacyHistoryMaxBytes + 2, 64, 100000});
  if (!parsed.ok()) { result.code = "legacy-history-" + parsed.code; return result; }
  const QStringList strings{"FileName", "FilePath", "DateTime", "Type", "Host", "URL", "ThumbnailURL", "DeletionURL", "ShortenedURL"};
  const auto records = parsed.document.array();
  for (qsizetype index = 0; index < records.size(); ++index) {
    const auto value = records.at(index);
    const auto fields = value.toObject();
    bool valid = value.isObject();
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
      result.records.push_back({index, fields});
      result.report.add(compatibility::ImportDisposition::Retained);
    }
  }
  return result;
}
}
