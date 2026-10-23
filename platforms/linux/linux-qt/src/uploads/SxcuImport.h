// COMPCAT-011: .sxcu files are untrusted data. Nothing in them is executed.
#pragma once

#include "UploadDestination.h"

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

struct SxcuImportResult {
  bool ok = false;
  QString code;
  UploadDestination destination;
  QVector<Secret> secrets;
  QVector<ImportWarning> warnings;
};

// Parses a ShareX custom-uploader document. Secret-shaped header, query, and
// argument values are removed from the destination and returned in secrets.
// Failure codes: invalid-json, too-large, too-deep.
SxcuImportResult importSxcu(const QByteArray &bytes);

// The name inside {secret:<name>}, when value is exactly one reference.
std::optional<QString> referencedSecret(const QString &value);

}  // namespace xerahs::uploads
