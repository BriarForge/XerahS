#include "DestinationCatalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace xerahs::uploads {
namespace {
constexpr qsizetype kMaxCatalogBytes = 8 * 1024 * 1024;

QJsonObject destinationJson(const UploadDestination &destination) {
  return QJsonDocument::fromJson(writeDestination(destination)).object();
}
}  // namespace

DestinationCatalog::DestinationCatalog(QString path) : m_path(std::move(path)) {
  QFile file(m_path);
  if (!file.exists()) return;
  if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxCatalogBytes) { m_loadCode = QStringLiteral("catalog-read-failed"); return; }
  m_snapshot = file.readAll();
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(m_snapshot, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) { m_loadCode = QStringLiteral("catalog-invalid"); return; }
  m_envelope = document.object();
  if (m_envelope.value(QStringLiteral("version")) != QJsonValue(kCatalogVersion)) { m_loadCode = QStringLiteral("catalog-version-unsupported"); return; }
  for (const auto &value : m_envelope.value(QStringLiteral("destinations")).toArray()) {
    const auto read = value.isObject() ? readDestination(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)) : DestinationRead{};
    if (read.destination && !read.destination->id.isNull()) m_destinations.append(*read.destination);
    else m_unreadable.append(value);
  }
  m_default = QUuid::fromString(m_envelope.value(QStringLiteral("default")).toString());
  if (!find(m_default)) m_default = {};
}

QVector<CatalogEntry> DestinationCatalog::entries() const {
  QVector<CatalogEntry> entries;
  for (const auto &destination : m_destinations) entries.append({destination, validateDestination(destination)});
  return entries;
}

std::optional<UploadDestination> DestinationCatalog::find(const QUuid &id) const {
  if (id.isNull()) return std::nullopt;
  for (const auto &destination : m_destinations)
    if (destination.id == id) return destination;
  return std::nullopt;
}

std::optional<UploadDestination> DestinationCatalog::defaultDestination() const { return find(m_default); }

QStringList DestinationCatalog::secretNames(const UploadDestination &destination) {
  static const QRegularExpression reference(QStringLiteral("\\{secret:([^{}]+)\\}"));
  QStringList names;
  const auto scan = [&](const QString &text) {
    for (auto it = reference.globalMatch(text); it.hasNext();) {
      const auto name = it.next().captured(1);
      if (!names.contains(name)) names.append(name);
    }
  };
  scan(destination.requestUrl);
  for (const auto *fields : {&destination.query, &destination.headers, &destination.arguments})
    for (const auto &field : *fields) scan(field.value);
  scan(destination.data);
  return names;
}

DestinationCatalog::ImportOutcome DestinationCatalog::addImported(const SxcuImportResult &import, CredentialStore &credentials) {
  if (!import.ok) return {import.code.isEmpty() ? QStringLiteral("import-failed") : import.code, {}, {}};
  if (!writable()) return {QStringLiteral("catalog-read-only"), {}, {}};
  auto destination = import.destination;
  if (destination.id.isNull() || find(destination.id)) destination.id = QUuid::createUuid();
  QStringList stored;
  for (const auto &secret : import.secrets) {
    if (credentials.store(destination.id, secret.name, secret.value).ok()) { stored.append(secret.name); continue; }
    for (const auto &name : stored) credentials.remove(destination.id, name);
    return {QStringLiteral("secret-store-failed"), {}, secret.name};
  }
  if (const auto code = add(destination); !code.isEmpty()) {
    credentials.removeAll(destination.id);
    return {code, {}, {}};
  }
  return {{}, destination.id, {}};
}

QString DestinationCatalog::add(const UploadDestination &destination) {
  if (destination.id.isNull()) return QStringLiteral("empty-id");
  if (find(destination.id)) return QStringLiteral("duplicate-id");
  auto destinations = m_destinations;
  destinations.append(destination);
  return save(destinations, m_default.isNull() ? destination.id : m_default);
}

QString DestinationCatalog::remove(const QUuid &id, CredentialStore &credentials) {
  if (!find(id)) return QStringLiteral("not-found");
  auto destinations = m_destinations;
  destinations.erase(std::remove_if(destinations.begin(), destinations.end(), [&](const auto &d) { return d.id == id; }), destinations.end());
  const auto nextDefault = m_default == id ? (destinations.isEmpty() ? QUuid() : destinations.first().id) : m_default;
  if (const auto code = save(destinations, nextDefault); !code.isEmpty()) return code;
  // The destination is gone either way; a failed secret removal leaves only an orphaned keyring item.
  const auto removed = credentials.removeAll(id);
  return removed.ok() ? QString() : QStringLiteral("secrets-not-removed");
}

QString DestinationCatalog::setDefault(const QUuid &id) {
  if (!find(id)) return QStringLiteral("not-found");
  return id == m_default ? QString() : save(m_destinations, id);
}

QString DestinationCatalog::save(const QVector<UploadDestination> &destinations, const QUuid &defaultId) {
  if (!writable()) return QStringLiteral("catalog-read-only");
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return QStringLiteral("catalog-directory-failed");
  QLockFile lock(m_path + QStringLiteral(".lock"));
  if (!lock.tryLock(0)) return QStringLiteral("catalog-busy");
  QFile existing(m_path);
  QByteArray disk;
  if (existing.exists()) {
    if (!existing.open(QIODevice::ReadOnly)) return QStringLiteral("catalog-write-failed");
    disk = existing.readAll();
  }
  if (disk != m_snapshot) return QStringLiteral("catalog-changed-reopen");
  QJsonArray array;
  for (const auto &destination : destinations) array.append(destinationJson(destination));
  for (const auto &value : m_unreadable) array.append(value);
  auto envelope = m_envelope;
  envelope.insert(QStringLiteral("version"), kCatalogVersion);
  envelope.insert(QStringLiteral("default"), defaultId.isNull() ? QString() : defaultId.toString(QUuid::WithoutBraces));
  envelope.insert(QStringLiteral("destinations"), array);
  const auto bytes = QJsonDocument(envelope).toJson();
  if (bytes.size() > kMaxCatalogBytes) return QStringLiteral("catalog-too-large");
  QSaveFile file(m_path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) return QStringLiteral("catalog-write-failed");
  file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  if (file.write(bytes) != bytes.size() || !file.commit()) return QStringLiteral("catalog-write-failed");
  m_snapshot = bytes;
  m_envelope = envelope;
  m_destinations = destinations;
  m_default = defaultId;
  return {};
}

}  // namespace xerahs::uploads
