#include "CaptureSave.h"
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace xerahs::app {
bool CaptureFileIdentity::operator==(const CaptureFileIdentity &o) const {
  return device == o.device && inode == o.inode && length == o.length && modifiedSeconds == o.modifiedSeconds &&
      modifiedNanos == o.modifiedNanos && changedSeconds == o.changedSeconds && changedNanos == o.changedNanos;
}
std::optional<CaptureFileIdentity> captureFileIdentity(int descriptor) {
  struct stat info{};
  if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1 ||
      (info.st_mode & 0077) || info.st_size <= 0) return {};
  return CaptureFileIdentity{quint64(info.st_dev), quint64(info.st_ino), info.st_size, info.st_mtim.tv_sec,
      info.st_mtim.tv_nsec, info.st_ctim.tv_sec, info.st_ctim.tv_nsec};
}
namespace {
int openBoundPath(const CaptureFile &file) {
  if (!file.identity || (file.artifact != "save.file" && file.artifact != "save-as.file" && file.artifact != "save.sidecar" && file.artifact != "save-as.sidecar") ||
      !QDir::isAbsolutePath(file.path) || file.path.contains(QChar::Null)) return -1;
  const auto components = file.path.split('/', Qt::SkipEmptyParts);
  if (components.isEmpty()) return -1;
  int descriptor = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  for (qsizetype i = 0; descriptor >= 0 && i < components.size(); ++i) {
    if (components[i] == "." || components[i] == "..") { ::close(descriptor); descriptor = -1; break; }
    const auto name = QFile::encodeName(components[i]);
    const int next = ::openat(descriptor, name.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK |
        (i + 1 == components.size() ? 0 : O_DIRECTORY));
    ::close(descriptor); descriptor = next;
  }
  return descriptor;
}
bool matches(const CaptureFile &file, int descriptor) {
  const auto observed = captureFileIdentity(descriptor);
  return observed && file.identity && *observed == *file.identity && observed->length == file.byteLength;
}
}
QString validateCaptureFile(const CaptureFile &file) {
  const int descriptor = openBoundPath(file);
  if (descriptor < 0) return QStringLiteral("saved-file-unavailable");
  const auto observed = captureFileIdentity(descriptor); ::close(descriptor);
  return observed && *observed == *file.identity && observed->length == file.byteLength
      ? QString{} : QStringLiteral("saved-file-changed");
}
QString captureFileHash(const CaptureFile &file, const xerahs::actions::CancellationToken &cancellation) {
  if ((file.artifact != "save.file" && file.artifact != "save-as.file") || file.byteLength <= 0 || file.byteLength > 1073741824 || cancellation.requested()) return {};
  const int descriptor = openBoundPath(file); if (descriptor < 0) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256); std::array<char, 131072> buffer{}; qint64 length = 0;
  bool valid = matches(file, descriptor);
  while (valid && !cancellation.requested()) {
    const auto count = ::read(descriptor, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { valid = false; break; }
    if (count == 0) break;
    length += count;
    if (length > file.byteLength) { valid = false; break; }
    hash.addData(QByteArrayView(buffer.data(), count));
  }
  valid = valid && !cancellation.requested() && length == file.byteLength && matches(file, descriptor);
  ::close(descriptor);
  return valid ? QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex()) : QString{};
}
} // namespace xerahs::app
