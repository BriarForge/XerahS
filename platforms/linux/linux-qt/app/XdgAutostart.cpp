#include "XdgAutostart.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
namespace xerahs::app {
QString setAutostart(bool enabled, const QString &executable, const QString &configHome) {
  QString root = configHome.isEmpty() ? qEnvironmentVariable("XDG_CONFIG_HOME") : configHome;
  if (!QDir::isAbsolutePath(root)) root = QDir::homePath() + "/.config";
  const auto path = QDir(root).filePath("autostart/com.xerahs.app.desktop");
  if (QFileInfo(path).isSymLink()) return "autostart-unsafe-path";
  if (!enabled) return !QFile::exists(path) || QFile::remove(path) ? QString{} : QStringLiteral("autostart-remove-failed");
  if (!QDir::isAbsolutePath(executable) || executable.contains('\n') || executable.contains('\r') || executable.contains(QChar(0))) return "autostart-executable-invalid";
  QString quoted = executable;
  quoted.replace("\\", "\\\\\\\\"); quoted.replace("\"", "\\\\\""); quoted.replace("`", "\\\\`"); quoted.replace("$", "\\\\$"); quoted.replace("%", "%%");
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return "autostart-directory-failed";
  const auto bytes = QString("[Desktop Entry]\nType=Application\nName=XerahS\nExec=\"%1\"\nIcon=com.xerahs.app\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").arg(quoted).toUtf8();
  QSaveFile file(path); file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return "autostart-write-failed";
  return {};
}
}
