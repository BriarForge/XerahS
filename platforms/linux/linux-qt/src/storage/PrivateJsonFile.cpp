// SETCAT-005, FN-016, CORE-012, CORE-013: private atomic JSON storage.
#include "PrivateJsonFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace xerahs::storage {
namespace {
StoreStatus safePath(const QString &path) {
  // The threat is another user swapping the file, so: the file itself is never
  // a symlink and is a regular file we own, and the nearest existing directory
  // above it (symlinks resolved, so /home -> /var/home and stowed ~/.config
  // work) is ours.
  if (path.isEmpty() || path.contains(QChar(0))) return StoreStatus::Unsafe;
  const auto absolute = QFileInfo(path).absoluteFilePath();
  struct stat info {};
  if (::lstat(QFile::encodeName(absolute).constData(), &info) == 0) {
    if (S_ISLNK(info.st_mode) || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()) return StoreStatus::Unsafe;
  } else if (errno != ENOENT) {
    return errno == ENOTDIR ? StoreStatus::Unsafe : StoreStatus::ReadFailed;
  }
  QString directory = QFileInfo(absolute).absolutePath();
  while (::stat(QFile::encodeName(directory).constData(), &info) != 0) {
    if (errno != ENOENT) return errno == ENOTDIR ? StoreStatus::Unsafe : StoreStatus::ReadFailed;
    const auto up = QFileInfo(directory).absolutePath();
    if (up == directory) return StoreStatus::ReadFailed;
    directory = up;
  }
  if (!S_ISDIR(info.st_mode) || info.st_uid != ::geteuid()) return StoreStatus::Unsafe;
  return StoreStatus::Ok;
}
}

PrivateJsonFile::PrivateJsonFile(QString path, StoreOptions options)
    : m_path(std::move(path)), m_options(std::move(options)) {}

StoreRead PrivateJsonFile::read() const {
  StoreRead out;
  out.status = safePath(m_path);
  if (out.status != StoreStatus::Ok) return out;
  const int fd = ::open(QFile::encodeName(m_path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    out.status = errno == ENOENT ? StoreStatus::Missing : (errno == ELOOP ? StoreStatus::Unsafe : StoreStatus::ReadFailed);
    return out;
  }
  QFile file;
  if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(fd); out.status = StoreStatus::ReadFailed; return out;
  }
  struct stat info {};
  if (::fstat(fd, &info) != 0) { out.status = StoreStatus::ReadFailed; return out; }
  if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid()) { out.status = StoreStatus::Unsafe; return out; }
  if (m_options.maxBytes < 0 || info.st_size > m_options.maxBytes) { out.status = StoreStatus::TooLarge; return out; }
  const auto limit = m_options.maxBytes == std::numeric_limits<qint64>::max() ? m_options.maxBytes : m_options.maxBytes + 1;
  out.snapshot = file.read(limit);
  if (file.error() != QFileDevice::NoError) { out.status = StoreStatus::ReadFailed; return out; }
  if (out.snapshot.size() > m_options.maxBytes) { out.status = StoreStatus::TooLarge; return out; }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(out.snapshot, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) { out.status = StoreStatus::Invalid; return out; }
  out.document = document.object();
  const auto format = out.document.value("format");
  const auto version = out.document.value("version");
  if (m_options.format.isEmpty() || (format != QJsonValue(m_options.format) && !(format.isUndefined() && m_options.acceptLegacyWithoutFormat)) ||
      !version.isDouble() || version.toDouble() < 1 || std::floor(version.toDouble()) != version.toDouble()) {
    out.status = StoreStatus::Invalid; return out;
  }
  out.version = version.toInt();
  if (version.toDouble() > m_options.maxVersion) { out.status = StoreStatus::NewerVersion; return out; }
  return out;
}

StoreStatus PrivateJsonFile::lock(StoreLock &lock) const {
  if (lock.held()) return StoreStatus::Busy;
  const auto safe = safePath(m_path);
  if (safe != StoreStatus::Ok) return safe;
  if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return StoreStatus::WriteFailed;
  const auto safeLock = safePath(m_path + ".lock");
  if (safeLock != StoreStatus::Ok) return safeLock;
  auto acquired = std::make_unique<QLockFile>(m_path + ".lock");
  acquired->setStaleLockTime(m_options.staleLockMs);
  if (!acquired->tryLock(m_options.lockWaitMs))
    return acquired->error() == QLockFile::LockFailedError ? StoreStatus::Busy : StoreStatus::WriteFailed;
  lock.m_lock = std::move(acquired);
  return StoreStatus::Ok;
}

StoreStatus PrivateJsonFile::write(QJsonObject payload, int version, const QByteArray &expectedSnapshot,
                                   QByteArray *newSnapshot, const StoreLock *held) const {
  if (m_options.format.isEmpty() || version < 1) return StoreStatus::Invalid;
  if (version > m_options.maxVersion) return StoreStatus::NewerVersion;
  StoreLock acquired;
  if (!held) {
    const auto status = lock(acquired);
    if (status != StoreStatus::Ok) return status;
  } else if (!held->held()) return StoreStatus::Busy;
  const auto current = read();
  if (current.status != StoreStatus::Ok && current.status != StoreStatus::Missing) return current.status;
  if (current.snapshot != expectedSnapshot) return StoreStatus::Changed;
  payload.insert("format", m_options.format);
  payload.insert("version", version);
  const auto bytes = QJsonDocument(payload).toJson();
  if (bytes.size() > m_options.maxBytes) return StoreStatus::TooLarge;
  QSaveFile file(m_path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      file.write(bytes) != bytes.size() || !file.commit()) return StoreStatus::WriteFailed;
  if (newSnapshot) *newSnapshot = bytes;
  return StoreStatus::Ok;
}

QString storeStatusName(StoreStatus status) {
  switch (status) {
    case StoreStatus::Ok: return QStringLiteral("ok");
    case StoreStatus::Missing: return QStringLiteral("missing");
    case StoreStatus::Unsafe: return QStringLiteral("unsafe");
    case StoreStatus::TooLarge: return QStringLiteral("too-large");
    case StoreStatus::Invalid: return QStringLiteral("invalid");
    case StoreStatus::NewerVersion: return QStringLiteral("newer-version");
    case StoreStatus::Busy: return QStringLiteral("busy");
    case StoreStatus::Changed: return QStringLiteral("changed");
    case StoreStatus::ReadFailed: return QStringLiteral("read-failed");
    case StoreStatus::WriteFailed: return QStringLiteral("write-failed");
  }
  return {};
}

}  // namespace xerahs::storage
