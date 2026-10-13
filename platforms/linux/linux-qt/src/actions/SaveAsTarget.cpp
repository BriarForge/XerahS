#include "SaveAsTarget.h"
#include <QDir>
#include <QFileInfo>
namespace xerahs::actions {
SaveAsTarget captureSaveAsTarget(const QString &requested) {
  if (requested.size() > 32768 || !QDir::isAbsolutePath(requested) || requested.endsWith('/')) return {{}, "save-as-invalid-path"};
  for (qsizetype i = 0; i < requested.size(); ++i) {
    const auto c = requested[i];
    if (c.unicode() < 32 || c.unicode() == 127 || c.isLowSurrogate()) return {{}, "save-as-invalid-path"};
    if (c.isHighSurrogate() && (++i >= requested.size() || !requested[i].isLowSurrogate())) return {{}, "save-as-invalid-path"};
  }
  for (const auto &part : requested.split('/', Qt::SkipEmptyParts))
    if (part == "." || part == "..") return {{}, "save-as-invalid-path"};
  auto path = QDir::cleanPath(requested); QFileInfo info(path);
  if (info.fileName().isEmpty()) return {{}, "save-as-invalid-path"};
  if (info.suffix().isEmpty()) path += ".png";
  else if (info.suffix().compare("png", Qt::CaseInsensitive) != 0) return {{}, "save-as-format-unavailable"};
  if (path.size() > 32768) return {{}, "save-as-invalid-path"};
  return {path, {}};
}
} // namespace xerahs::actions
