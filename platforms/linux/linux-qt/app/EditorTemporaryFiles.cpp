#include "EditorTemporaryFiles.h"
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include "storage/PrivateJsonFile.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace xerahs::app {
namespace {
int openPrivateDirectory(const QString &path) {
  const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  struct stat info{};
  if (fd >= 0 && ::fstat(fd, &info) == 0 && info.st_uid == ::geteuid() && (info.st_mode & 0777) == 0700) return fd;
  if (fd >= 0) ::close(fd);
  return -1;
}
}
QString editorTemporaryDirectory() {
  const auto cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
  if (cache.isEmpty()) return {};
  const auto parent = QDir(cache).filePath("com.xerahs.app/linux-qt");
  if (!QDir().mkpath(parent)) return {};
  const auto path = QDir(parent).filePath("editor-temporary");
  // Create private from the outset. Never chmod an existing foreign/symlink directory.
  ::mkdir(QFile::encodeName(path).constData(), 0700);
  const int fd = openPrivateDirectory(path); if (fd < 0) return {};
  ::close(fd); return path;
}
QString editorTemporaryTemplate() {
  const auto directory = editorTemporaryDirectory();
  return directory.isEmpty() ? QString{} : QDir(directory).filePath("xerahs-editor-XXXXXX");
}
bool sweepEditorTemporaryFiles(const QString &directory, const QDateTime &now, int retentionDays, bool secureDelete) {
  if (!now.isValid()) return false;
  const int fd = openPrivateDirectory(directory); if (fd < 0) return false;
  const qint64 bound = now.addSecs(-qint64(retentionDays) * 24 * 60 * 60).toSecsSinceEpoch();
  bool ok = true;
  for (const auto &name : QDir(directory).entryList({"xerahs-editor-*"}, QDir::Files | QDir::Hidden | QDir::System)) {
    const auto encoded = QFile::encodeName(name); struct stat info{};
    if (::fstatat(fd, encoded.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; continue; }
    if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || (info.st_mode & 0777) != 0600 || info.st_nlink != 1) continue;
    if (info.st_mtim.tv_sec >= bound) continue;
    // ES-020: the checks above proved this regular 0600 file is this user's
    // own; the shared storage primitive then does the best-effort zero-fill.
    if (secureDelete && !xerahs::storage::overwriteForRemoval(QDir(directory).filePath(name))) ok = false;
    if (::unlinkat(fd, encoded.constData(), 0) != 0) ok = false;
  }
  ::close(fd); return ok;
}
}
