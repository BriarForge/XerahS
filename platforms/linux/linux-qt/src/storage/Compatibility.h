// COMPCAT-004..006: reports contain counts and stable format identifiers only.
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <array>

namespace xerahs::compatibility {
enum class ImportDisposition { Retained, Transformed, Defaulted, Quarantined, Rejected, Ignored };
struct ImportReport {
  std::array<qsizetype, 6> counts{};
  void add(ImportDisposition disposition, qsizetype amount = 1) {
    if (amount > 0) counts.at(static_cast<size_t>(disposition)) += amount;
  }
  qsizetype count(ImportDisposition disposition) const { return counts.at(static_cast<size_t>(disposition)); }
  QString summary() const {
    return QStringLiteral("Retained: %1; transformed: %2; defaulted: %3; quarantined: %4; rejected: %5; ignored: %6")
        .arg(counts[0]).arg(counts[1]).arg(counts[2]).arg(counts[3]).arg(counts[4]).arg(counts[5]);
  }
};
struct ExportReport {
  QString target;
  int version = 1;
  QStringList omissions;  // stable omission codes, never local paths or values
};

// Used only for UNKNOWN fields. Known schema members have their own validation.
// This conservative vocabulary does not grant authority to any imported value.
inline bool unsafeUnknown(const QString &key) {
  QString normalized;
  for (const QChar c : key.toCaseFolded()) if (c.isLetterOrNumber()) normalized += c;
  if (key == u"$ref" || key == u"$type") return true;
  static const QStringList fragments = {"secret", "credential", "password", "apikey", "accesstoken", "refreshtoken",
    "authorization", "authentication", "consent", "approval", "permission", "confirm", "authoriz", "authoris", "trust", "grant", "bypass", "cookie", "token", "executable", "script", "command",
    "shell", "process", "arguments", "nativehandle", "critical", "requiredfeature"};
  if (normalized == u"auth" || normalized == u"token" || normalized == u"required" || normalized == u"code") return true;
  for (const auto &fragment : fragments) if (normalized.contains(fragment)) return true;
  return false;
}
// Path-like extension fields remain inert too; reject traversal rather than
// normalizing away evidence. Arrays inherit the field's path classification.
inline bool containsTraversal(const QJsonValue &value, const QString &key = {}, int depth = 0) {
  if (depth > 64) return true;
  const auto lower = key.toCaseFolded();
  if (value.isString() && (lower.contains(u"path") || lower.contains(u"location") || lower.endsWith(u"file") || lower.contains(u"filename") || lower == u"imagekey")) {
    auto path = value.toString(); path.replace('\\', '/');
    if (path.contains(QChar(0)) || path.split('/').contains(QStringLiteral(".."))) return true;
  }
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
      if (containsTraversal(it.value(), it.key(), depth + 1)) return true;
  } else if (value.isArray()) {
    for (const auto &item : value.toArray()) if (containsTraversal(item, key, depth + 1)) return true;
  }
  return false;
}
inline bool containsUnsafeUnknown(const QJsonValue &value, int depth = 0) {
  if (depth > 64) return true;
  if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
      if (unsafeUnknown(it.key()) || containsUnsafeUnknown(it.value(), depth + 1)) return true;
  } else if (value.isArray()) {
    for (const auto &item : value.toArray()) if (containsUnsafeUnknown(item, depth + 1)) return true;
  }
  return false;
}
} // namespace xerahs::compatibility
