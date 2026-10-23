#include "DestinationCatalog.h"
#include "storage/PrivateJsonFile.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace xerahs::uploads {
namespace {
constexpr qsizetype kMaxCatalogBytes = 8 * 1024 * 1024;
storage::PrivateJsonFile catalogFile(const QString &path) {
  return {path, {QStringLiteral("com.xerahs.app/linux-qt/upload-destinations"), kCatalogVersion, kMaxCatalogBytes, 0, 30000, true}};
}

QJsonObject destinationJson(const UploadDestination &destination) {
  return QJsonDocument::fromJson(writeDestination(destination)).object();
}
}  // namespace

DestinationCatalog::DestinationCatalog(QString path) : m_path(std::move(path)) {
  const auto read = catalogFile(m_path).read();
  if (read.status == storage::StoreStatus::Missing) return;
  if (read.status == storage::StoreStatus::Invalid) {
    // Distinguish an invalid envelope from an unsupported legacy version.
    const auto format = read.document.value("format");
    const bool unsupportedVersion = (format.isUndefined() || format == QJsonValue(QStringLiteral("com.xerahs.app/linux-qt/upload-destinations"))) &&
        QJsonDocument::fromJson(read.snapshot).isObject() && read.document.value("version") != QJsonValue(kCatalogVersion);
    m_loadCode = unsupportedVersion ? QStringLiteral("catalog-version-unsupported") : QStringLiteral("catalog-invalid"); return;
  }
  if (read.status == storage::StoreStatus::NewerVersion ||
      (read.status == storage::StoreStatus::Ok && read.version != kCatalogVersion)) {
    m_loadCode = QStringLiteral("catalog-version-unsupported"); return;
  }
  if (read.status != storage::StoreStatus::Ok) { m_loadCode = QStringLiteral("catalog-read-failed"); return; }
  m_snapshot = read.snapshot;
  m_envelope = read.document;
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
  QJsonArray array;
  for (const auto &destination : destinations) array.append(destinationJson(destination));
  for (const auto &value : m_unreadable) array.append(value);
  auto envelope = m_envelope;
  envelope.insert(QStringLiteral("version"), kCatalogVersion);
  envelope.insert(QStringLiteral("default"), defaultId.isNull() ? QString() : defaultId.toString(QUuid::WithoutBraces));
  envelope.insert(QStringLiteral("destinations"), array);
  const auto status = catalogFile(m_path).write(envelope, kCatalogVersion, m_snapshot, &m_snapshot);
  switch (status) {
    case storage::StoreStatus::Ok: break;
    case storage::StoreStatus::Busy: return QStringLiteral("catalog-busy");
    case storage::StoreStatus::Invalid:
    case storage::StoreStatus::NewerVersion:
    case storage::StoreStatus::Changed: return QStringLiteral("catalog-changed-reopen");
    case storage::StoreStatus::TooLarge: return QStringLiteral("catalog-too-large");
    default: return QStringLiteral("catalog-write-failed");
  }
  m_envelope = envelope;
  m_destinations = destinations;
  m_default = defaultId;
  return {};
}

}  // namespace xerahs::uploads
