// CORE-021, SETCAT-014: the saved upload destinations and the default one.
// Secrets stay in a CredentialStore; this file holds only {secret:name} references.
#pragma once

#include "CredentialStore.h"
#include "SxcuImport.h"
#include "UploadDestination.h"

#include <QJsonArray>
#include <QJsonObject>

namespace xerahs::uploads {

inline constexpr int kCatalogVersion = 1;

struct CatalogEntry {
  UploadDestination destination;
  QStringList problems;  // validateDestination codes; a destination with problems cannot upload
};

// Load codes: catalog-read-failed, catalog-invalid, catalog-version-unsupported.
// Save codes: catalog-read-only, catalog-directory-failed, catalog-busy,
// catalog-changed-reopen, catalog-too-large, catalog-write-failed.
class DestinationCatalog {
public:
  explicit DestinationCatalog(QString path);

  QString loadCode() const { return m_loadCode; }
  bool writable() const { return m_loadCode.isEmpty(); }
  QVector<CatalogEntry> entries() const;
  // Entries that could not be read stay in the file untouched.
  int unreadableCount() const { return m_unreadable.size(); }
  std::optional<UploadDestination> find(const QUuid &id) const;
  QUuid defaultId() const { return m_default; }
  std::optional<UploadDestination> defaultDestination() const;
  // Names of the {secret:name} references a destination uses, in first-use order.
  static QStringList secretNames(const UploadDestination &destination);

  // Stores the import's secrets first; if any fails, the ones already stored
  // are removed and nothing is saved. The first destination becomes the default.
  struct ImportOutcome { QString code; QUuid id; QString secretName; };
  ImportOutcome addImported(const SxcuImportResult &import, CredentialStore &credentials);
  QString add(const UploadDestination &destination);
  QString remove(const QUuid &id, CredentialStore &credentials);
  QString setDefault(const QUuid &id);

private:
  QString save(const QVector<UploadDestination> &destinations, const QUuid &defaultId);

  QString m_path, m_loadCode;
  QByteArray m_snapshot;
  QJsonObject m_envelope;
  QVector<UploadDestination> m_destinations;
  QJsonArray m_unreadable;
  QUuid m_default;
};

}  // namespace xerahs::uploads
