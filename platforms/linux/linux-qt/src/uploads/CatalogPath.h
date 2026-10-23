// CORE-020: where the saved upload-destination catalog lives.
#pragma once

#include <QString>

namespace xerahs::uploads {

// An explicit path is kept. An empty path is upload-destinations.json under
// the application config directory, the same file Capture and Upload file use.
QString defaultCatalogPath(const QString &overridePath = {});

}  // namespace xerahs::uploads
