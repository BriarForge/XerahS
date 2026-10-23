// CORE-020.
#include "CatalogPath.h"

#include <QDir>
#include <QStandardPaths>

namespace xerahs::uploads {

QString defaultCatalogPath(const QString &overridePath) {
  if (!overridePath.isEmpty()) return overridePath;
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
      .filePath(QStringLiteral("upload-destinations.json"));
}

}  // namespace xerahs::uploads
