// COMPCAT-011: .sxcu files are untrusted data. Nothing in them is executed.
#pragma once

#include "UploadDestination.h"
#include "storage/Compatibility.h"

#include <QVector>

namespace xerahs::uploads {

// A header, argument, or query value replaced by {secret:<name>}.
struct Secret {
  QString name;
  QString value;
};

struct ImportWarning {
  QString code;
  QString detail;
};

// A header, query, or argument value the importer did not auto-extract.
// Review fix A5: the import surface shows these values so the user can mark
// more of them as secrets before the destination is stored.
struct ImportCandidate {
  QString family;  // "headers", "query" or "arguments"
  QString name;
  QString value;
};

struct SxcuImportResult {
  bool ok = false;
  QString code;
  UploadDestination destination;
  QVector<Secret> secrets;
  QVector<ImportWarning> warnings;
  compatibility::ImportReport report;
  // Review fix A5: non-extracted header/query/argument values, offered for
  // explicit secret marking at import time.
  QVector<ImportCandidate> candidates;
  // Quarantined metadata is inspectable only; it is never persisted or applied.
  QJsonObject quarantine;
  bool acceptanceBlocked = false;
};

// Parses a ShareX custom-uploader document. Secret-shaped header, query, and
// argument values are removed from the destination and returned in secrets.
// Failure codes: invalid-json, too-large, too-deep.
SxcuImportResult importSxcu(const QByteArray &bytes);

// Review fix A5: moves the named import candidate's value into the secrets
// list, replacing it with {secret:<ref>} in the destination. Refuses an
// unknown family or name and values that are already secret references.
bool markImportedSecret(SxcuImportResult *import, const QString &family, const QString &name);

// The name inside {secret:<name>}, when value is exactly one reference.
std::optional<QString> referencedSecret(const QString &value);

}  // namespace xerahs::uploads
