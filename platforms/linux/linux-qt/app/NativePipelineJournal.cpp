#include "NativePipelineJournal.h"
#include <QDir>
#include <QFile>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace xerahs::app {
using namespace xerahs::actions;
namespace {
constexpr size_t maximumEntries = 1024;
constexpr qsizetype maximumListingBytes = 16 * 1024 * 1024;
struct Descriptor {
  int value = -1;
  Descriptor() = default;
  explicit Descriptor(int fd) : value(fd) {}
  ~Descriptor() { if (value >= 0) ::close(value); }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
  Descriptor(Descriptor &&other) noexcept : value(other.value) { other.value = -1; }
  Descriptor &operator=(Descriptor &&other) noexcept {
    if (value >= 0) ::close(value);
    value = other.value; other.value = -1; return *this;
  }
};
JournalWrite failure(const char *code) { return {false, QString::fromLatin1(code)}; }
Descriptor root(const QString &path, bool create) {
  if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null) || QDir::cleanPath(path) != path || path == "/") return {};
  Descriptor directory(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
  const auto components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const auto &component : components) {
    const auto name = QFile::encodeName(component);
    int fd = ::openat(directory.value, name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT && create) {
      if (::mkdirat(directory.value, name.constData(), 0700) != 0 && errno != EEXIST) return {};
      if (::fsync(directory.value) != 0) return {};
      fd = ::openat(directory.value, name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    }
    if (fd < 0) return {};
    directory = Descriptor(fd);
  }
  struct stat info{};
  if (::fstat(directory.value, &info) != 0 || info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0) return {};
  return directory;
}
QByteArray filename(const QString &id) { return id.toLatin1() + ".json"; }
struct Read { std::optional<QByteArray> bytes; QString diagnostic; };
Read read(int directory, const QByteArray &name) {
  Descriptor file(::openat(directory, name.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
  if (file.value < 0) return {{}, errno == ENOENT ? QString{} : QStringLiteral("pipeline-journal-read-failed")};
  struct stat info{};
  if (::fstat(file.value, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() ||
      (info.st_mode & 0077) != 0 || info.st_nlink != 1 || info.st_size < 0 || info.st_size > maximumJournalBytes)
    return {{}, QStringLiteral("pipeline-journal-unsafe-file")};
  QByteArray bytes;
  char buffer[16384];
  for (;;) {
    const auto length = ::read(file.value, buffer, sizeof(buffer));
    if (length < 0 && errno == EINTR) continue;
    if (length < 0 || bytes.size() + length > maximumJournalBytes) return {{}, QStringLiteral("pipeline-journal-read-failed")};
    if (length == 0) break;
    bytes.append(buffer, length);
  }
  return {bytes, {}};
}
struct Names { QStringList ids; QString diagnostic; };
Names names(int directory) {
  // A separate directory description keeps iteration offsets out of the root
  // descriptor used for publication, locking and fsync.
  const int scan = ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  DIR *stream = scan < 0 ? nullptr : ::fdopendir(scan);
  if (!stream) { if (scan >= 0) ::close(scan); return {{}, QStringLiteral("pipeline-journal-read-failed")}; }
  Names result;
  for (;;) {
    errno = 0; const auto *entry = ::readdir(stream);
    if (!entry) { if (errno != 0) result.diagnostic = QStringLiteral("pipeline-journal-read-failed"); break; }
    const auto name = QString::fromLocal8Bit(entry->d_name);
    if (!name.endsWith(".json")) continue;
    const auto id = name.chopped(5);
    if (!validJournalTaskId(id)) { result.diagnostic = QStringLiteral("pipeline-journal-unrecognized-file"); continue; }
    if (result.ids.size() >= qsizetype(maximumEntries)) { result.diagnostic = QStringLiteral("pipeline-journal-capacity"); break; }
    result.ids.append(id);
  }
  ::closedir(stream); result.ids.sort(); return result;
}
}
QString NativePipelineJournal::defaultDirectory() {
  const auto configured = qEnvironmentVariable("XDG_STATE_HOME");
  const auto state = QDir::isAbsolutePath(configured) ? configured : QDir::homePath() + QStringLiteral("/.local/state");
  return QDir::cleanPath(state + QStringLiteral("/com.xerahs.app/pipelines"));
}
JournalWrite NativePipelineJournal::replace(const QString &id, const std::optional<QByteArray> &expected, const QByteArray &bytes) const {
  if (!validJournalTaskId(id) || !parsePipelineJournal(bytes, id).record) return failure("pipeline-journal-invalid");
  auto directory = root(m_directory, true);
  if (directory.value < 0) return failure("pipeline-journal-private-storage-required");
  if (::flock(directory.value, LOCK_EX | LOCK_NB) != 0) return failure("pipeline-journal-busy");
  const auto target = filename(id); const auto actual = read(directory.value, target);
  if (!actual.diagnostic.isEmpty()) return {false, actual.diagnostic};
  if (actual.bytes != expected) return failure("pipeline-journal-changed");
  if (!expected) {
    const auto existing = names(directory.value);
    if (!existing.diagnostic.isEmpty() || existing.ids.size() >= qsizetype(maximumEntries)) return failure("pipeline-journal-capacity");
  }
  const auto temporary = QByteArray(".pending-") + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
  Descriptor file(::openat(directory.value, temporary.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
  if (file.value < 0) return failure("pipeline-journal-write-failed");
  const auto abandon = [&] { ::unlinkat(directory.value, temporary.constData(), 0); return failure("pipeline-journal-write-failed"); };
  qsizetype position = 0;
  while (position < bytes.size()) {
    const auto written = ::write(file.value, bytes.constData() + position, size_t(bytes.size() - position));
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return abandon();
    position += written;
  }
  if (::fsync(file.value) != 0) return abandon();
  const int renamed = expected ? ::renameat(directory.value, temporary.constData(), directory.value, target.constData()) :
      int(::syscall(SYS_renameat2, directory.value, temporary.constData(), directory.value, target.constData(), RENAME_NOREPLACE));
  if (renamed != 0) return abandon();
  if (::fsync(directory.value) != 0) return failure("pipeline-journal-durability-unknown");
  return {true, {}};
}
JournalListing NativePipelineJournal::list() const {
  // A missing store is an empty history; reading never creates persistent data.
  if (!QDir::isAbsolutePath(m_directory) || m_directory.contains(QChar::Null) || QDir::cleanPath(m_directory) != m_directory)
    return {{}, QStringLiteral("pipeline-journal-private-storage-required")};
  struct stat info{};
  if (::lstat(QFile::encodeName(m_directory).constData(), &info) != 0)
    return errno == ENOENT ? JournalListing{} : JournalListing{{}, QStringLiteral("pipeline-journal-read-failed")};
  auto directory = root(m_directory, false);
  if (directory.value < 0) return {{}, QStringLiteral("pipeline-journal-private-storage-required")};
  if (::flock(directory.value, LOCK_SH | LOCK_NB) != 0) return {{}, QStringLiteral("pipeline-journal-busy")};
  const auto existing = names(directory.value); JournalListing result; result.diagnostic = existing.diagnostic;
  qsizetype totalBytes = 0;
  for (const auto &id : existing.ids) {
    const auto entry = read(directory.value, filename(id));
    if (entry.bytes && totalBytes + entry.bytes->size() > maximumListingBytes) {
      result.diagnostic = QStringLiteral("pipeline-journal-capacity"); break;
    }
    if (entry.bytes) totalBytes += entry.bytes->size();
    result.entries.push_back({id, entry.bytes.value_or(QByteArray{}), entry.diagnostic.isEmpty() && entry.bytes ?
        parsePipelineJournal(*entry.bytes, id) : PipelineJournalParse{{}, entry.diagnostic.isEmpty() ? QStringLiteral("pipeline-journal-changed") : entry.diagnostic}});
  }
  std::sort(result.entries.begin(), result.entries.end(), [](const JournalEntry &left, const JournalEntry &right) {
    const auto a = left.parsed.record ? left.parsed.record->createdAt : QDateTime{};
    const auto b = right.parsed.record ? right.parsed.record->createdAt : QDateTime{};
    return a == b ? left.taskId < right.taskId : a > b;
  });
  return result;
}
JournalWrite NativePipelineJournal::erase(const QString &id, const QByteArray &expected) const {
  if (!validJournalTaskId(id) || !parsePipelineJournal(expected, id).record) return failure("pipeline-journal-invalid");
  auto directory = root(m_directory, false);
  if (directory.value < 0) return failure("pipeline-journal-private-storage-required");
  if (::flock(directory.value, LOCK_EX | LOCK_NB) != 0) return failure("pipeline-journal-busy");
  const auto target = filename(id); const auto actual = read(directory.value, target);
  if (!actual.diagnostic.isEmpty()) return {false, actual.diagnostic};
  if (!actual.bytes || *actual.bytes != expected) return failure("pipeline-journal-changed");
  if (::unlinkat(directory.value, target.constData(), 0) != 0) return failure("pipeline-journal-write-failed");
  if (::fsync(directory.value) != 0) return failure("pipeline-journal-durability-unknown");
  return {true, {}};
}
} // namespace xerahs::app
