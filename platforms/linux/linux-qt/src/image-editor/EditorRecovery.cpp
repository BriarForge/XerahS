#include "EditorRecovery.h"

#include "storage/PrivateJsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>

namespace xerahs::editor {
namespace {
using storage::PrivateJsonFile;
using storage::StoreOptions;
using storage::StoreStatus;

StoreOptions options() {
  StoreOptions o;
  o.format = QStringLiteral("xerahs.editor-recovery");
  o.maxVersion = 1;
  o.maxBytes = EditorRecoveryStore::kMaxFileBytes;
  return o;
}
}  // namespace

EditorRecoveryStore::EditorRecoveryStore(QString directory, std::function<QDateTime()> clock)
    : m_directory(std::move(directory)), m_clock(std::move(clock)) {
  if (!m_clock) m_clock = [] { return QDateTime::currentDateTimeUtc(); };
}

QString EditorRecoveryStore::defaultDirectory() {
  const auto configured = qEnvironmentVariable("XDG_STATE_HOME");
  const auto state = QDir::isAbsolutePath(configured) ? configured : QDir::homePath() + QStringLiteral("/.local/state");
  return QDir::cleanPath(state + QStringLiteral("/com.xerahs.app/editor-recovery"));
}

QString EditorRecoveryStore::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

bool EditorRecoveryStore::validId(const QString &id) {
  static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
  return pattern.match(id).hasMatch();
}

QString EditorRecoveryStore::pathFor(const QString &id) const { return m_directory + QLatin1Char('/') + id + QStringLiteral(".json"); }

QString EditorRecoveryStore::write(const QString &id, const QString &sourcePath, const AnnotationDocument &document, qint64 ownerPid) {
  if (!validId(id)) return QStringLiteral("recovery-id-invalid");
  QJsonObject payload;
  payload.insert(QStringLiteral("savedAt"), m_clock().toUTC().toString(Qt::ISODateWithMs));
  payload.insert(QStringLiteral("sourcePath"), sourcePath);
  payload.insert(QStringLiteral("ownerPid"), double(ownerPid));
  payload.insert(QStringLiteral("document"), serializeDocument(document));
  const PrivateJsonFile file(pathFor(id), options());
  prune(id);
  for (int attempt = 0; attempt < 2; ++attempt) {
    QByteArray expected = m_snapshots.value(id);
    if (attempt == 1) {
      const auto current = file.read();
      if (current.status != StoreStatus::Ok && current.status != StoreStatus::Missing) return QStringLiteral("recovery-write-failed");
      expected = current.snapshot;
    }
    QByteArray written;
    const auto status = file.write(payload, 1, expected, &written);
    if (status == StoreStatus::Ok) { m_snapshots.insert(id, written); return {}; }
    if (status != StoreStatus::Changed) return QStringLiteral("recovery-write-failed");
  }
  return QStringLiteral("recovery-write-failed");
}

QString EditorRecoveryStore::remove(const QString &id) {
  if (!validId(id)) return QStringLiteral("recovery-id-invalid");
  m_snapshots.remove(id);
  const auto path = pathFor(id);
  const QFileInfo info(path);
  if (!info.isFile() && !info.isSymLink()) return {};
  return QFile::remove(path) ? QString() : QStringLiteral("recovery-remove-failed");
}

void EditorRecoveryStore::prune(const QString &keepId) {
  const QDir dir(m_directory);
  const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::NoSymLinks, QDir::Time);  // newest first
  QList<QFileInfo> others;
  for (const auto &info : files) if (info.completeBaseName() != keepId) others.append(info);
  for (int i = kMaxRecords - 1; i < others.size(); ++i) {
    QFile::remove(others.at(i).absoluteFilePath());
    m_snapshots.remove(others.at(i).completeBaseName());
  }
}

void EditorRecoveryStore::trimSetAside() {
  const QDir dir(m_directory);
  const auto aside = dir.entryInfoList({QStringLiteral("*.corrupt")}, QDir::Files | QDir::NoSymLinks, QDir::Time);
  for (int i = kMaxSetAside; i < aside.size(); ++i) QFile::remove(aside.at(i).absoluteFilePath());
}

RecoveryListing EditorRecoveryStore::list() {
  RecoveryListing out;
  const QDir dir(m_directory);
  if (!dir.exists()) return out;
  const auto files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::NoDotAndDotDot);
  for (const auto &info : files) {
    const auto id = info.completeBaseName();
    if (!validId(id)) continue;
    const auto path = info.absoluteFilePath();
    const auto read = PrivateJsonFile(path, options()).read();
    const auto setAside = [&](const QString &code) {
      out.diagnostics << code;
      if (QFile::exists(path + QStringLiteral(".corrupt"))) QFile::remove(path + QStringLiteral(".corrupt"));
      if (QFile::rename(path, path + QStringLiteral(".corrupt"))) ++out.setAside;
    };
    if (read.status == StoreStatus::Missing) continue;
    if (read.status == StoreStatus::Unsafe || read.status == StoreStatus::ReadFailed || read.status == StoreStatus::NewerVersion) {
      out.diagnostics << QStringLiteral("recovery-unreadable"); continue;  // left in place untouched
    }
    if (read.status != StoreStatus::Ok) { setAside(QStringLiteral("recovery-corrupt")); continue; }
    RecoveryRecord record;
    record.id = id;
    record.savedAt = QDateTime::fromString(read.document.value(QStringLiteral("savedAt")).toString(), Qt::ISODateWithMs);
    record.savedAt.setTimeSpec(Qt::UTC);
    record.sourcePath = read.document.value(QStringLiteral("sourcePath")).toString();
    record.ownerPid = qint64(read.document.value(QStringLiteral("ownerPid")).toDouble());
    const auto documentValue = read.document.value(QStringLiteral("document"));
    if (!record.savedAt.isValid() || !documentValue.isObject()) { setAside(QStringLiteral("recovery-corrupt")); continue; }
    auto parsed = parseDocument(documentValue.toObject());
    if (!parsed.document) { setAside(QStringLiteral("recovery-corrupt")); continue; }
    record.document = std::move(*parsed.document);
    m_snapshots.insert(id, read.snapshot);
    out.records.push_back(std::move(record));
  }
  std::sort(out.records.begin(), out.records.end(), [](const RecoveryRecord &a, const RecoveryRecord &b) { return a.savedAt > b.savedAt; });
  if (out.setAside) trimSetAside();
  return out;
}

QString describeRecoveryAge(qint64 seconds) {
  if (seconds < 0) return QStringLiteral("an unknown time ago (the system clock changed)");
  if (seconds < 60) return QStringLiteral("less than a minute ago");
  const auto unit = [](qint64 n, const char *name) {
    return QStringLiteral("%1 %2%3 ago").arg(n).arg(QLatin1String(name), n == 1 ? QString() : QStringLiteral("s"));
  };
  if (seconds < 3600) return unit(seconds / 60, "minute");
  if (seconds < 86400) return unit(seconds / 3600, "hour");
  return unit(seconds / 86400, "day");
}

QString describeRecoverySource(const RecoveryRecord &record) {
  return record.sourcePath.isEmpty() ? QStringLiteral("an unsaved capture or new image")
                                     : QStringLiteral("the image %1").arg(record.sourcePath);
}

QString recoveryDisclosure(const RecoveryRecord &record, const QDateTime &now) {
  return QStringLiteral("Recovered editor session from %1 (saved %2). Source: %3. Your last explicit save was not changed.")
      .arg(describeRecoveryAge(record.savedAt.secsTo(now)), record.savedAt.toLocalTime().toString(Qt::ISODate), describeRecoverySource(record));
}

}  // namespace xerahs::editor
