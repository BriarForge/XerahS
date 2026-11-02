#include "CaptureDelete.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
struct Descriptor {
  int value = -1;
  explicit Descriptor(int fd = -1) : value(fd) {}
  ~Descriptor() { if (value >= 0) ::close(value); }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
};
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, {}}; }
ActionOutcome unknown() { auto result = failed("delete-outcome-unknown"); result.outcomeUnknown = true; return result; }
bool trustedDirectory(int fd) {
  struct stat info{};
  // Protect the recorded staging pathname from another security principal
  // renaming any ancestor. Sticky directories owned by us/root (e.g. /tmp)
  // protect our entries; ordinary writable shared directories do not.
  return ::fstat(fd, &info) == 0 && S_ISDIR(info.st_mode) &&
      (info.st_uid == ::geteuid() || info.st_uid == 0) &&
      (!(info.st_mode & 0022) || (info.st_mode & S_ISVTX));
}
int openParent(const QString &path) {
  if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null)) return -1;
  const auto parts = path.split('/', Qt::SkipEmptyParts);
  if (parts.isEmpty()) return -1;
  for (const auto &part : parts) if (part == "." || part == "..") return -1;
  int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  for (qsizetype i = 0; fd >= 0 && i + 1 < parts.size(); ++i) {
    if (!trustedDirectory(fd)) { ::close(fd); return -1; }
    const auto name = QFile::encodeName(parts[i]);
    const int next = ::openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    ::close(fd); fd = next;
  }
  if (fd >= 0 && !trustedDirectory(fd)) { ::close(fd); return -1; }
  return fd;
}
bool movedIdentity(const CaptureFileIdentity &observed, const CaptureFileIdentity &expected) {
  // rename changes ctime. All other publication facts must still match.
  return observed.device == expected.device && observed.inode == expected.inode && observed.length == expected.length &&
      observed.modifiedSeconds == expected.modifiedSeconds && observed.modifiedNanos == expected.modifiedNanos;
}
}
ActionOutcome deleteCaptureFile(CaptureFile &file, const CancellationToken &cancellation, const CaptureDeleteHooks &hooks) {
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (!file.identity || (file.artifact != "save.file" && file.artifact != "save-as.file")) return failed("delete-guard");
  if (!hooks.observe) return failed("pipeline-journal-unavailable");
  Descriptor parent(openParent(file.path)); if (parent.value < 0) return failed("delete-staging-unavailable");
  const auto name = QFile::encodeName(QFileInfo(file.path).fileName());
  Descriptor original(::openat(parent.value, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (original.value < 0) return failed("saved-file-unavailable");
  const auto identity = captureFileIdentity(original.value);
  if (!identity || !(*identity == *file.identity) || identity->length != file.byteLength) return failed("saved-file-changed");
  const auto stageName = QFile::encodeName(".xerahs-delete-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (::mkdirat(parent.value, stageName.constData(), 0700) != 0) return failed("delete-staging-unavailable");
  Descriptor stage(::openat(parent.value, stageName.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
  const auto cleanupEmpty = [&] {
    struct stat held{}, named{};
    if (stage.value >= 0 && ::fstat(stage.value, &held) == 0 && held.st_uid == ::geteuid() &&
        (held.st_mode & 0777) == 0700 && ::fstatat(parent.value, stageName.constData(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        held.st_dev == named.st_dev && held.st_ino == named.st_ino)
      ::unlinkat(parent.value, stageName.constData(), AT_REMOVEDIR); // succeeds only while empty
    ::fsync(parent.value);
  };
  struct stat stageInfo{};
  if (stage.value < 0 || ::fstat(stage.value, &stageInfo) != 0 || !S_ISDIR(stageInfo.st_mode) ||
      stageInfo.st_uid != ::geteuid() || (stageInfo.st_mode & 0777) != 0700 ||
      ::fsync(stage.value) != 0 || ::fsync(parent.value) != 0) { cleanupEmpty(); return failed("delete-staging-unavailable"); }
  DeletionRecovery recovery{file.path, QDir(QFileInfo(file.path).absolutePath()).filePath(QString::fromLocal8Bit(stageName) + "/entry"), "pending"};
  bool observed = false;
  try { observed = hooks.observe(recovery); } catch (...) {}
  if (!observed) { cleanupEmpty(); return failed("pipeline-journal-unavailable"); }
  const auto record = [&](const QString &state) {
    recovery.state = state;
    try { return hooks.observe(recovery); } catch (...) { return false; }
  };
  try { if (hooks.beforeMove) hooks.beforeMove(recovery.stagingPath); }
  catch (...) { cleanupEmpty(); record("restored"); return failed("delete-failed"); }
  if (cancellation.requested()) { cleanupEmpty(); record("restored"); return {ActionState::Cancelled, {}, {}}; }
  // A directory substitution during the write-ahead boundary must not move a
  // file into a staging location different from the one history records.
  Descriptor visibleParent(openParent(file.path)); struct stat bound{}, visible{};
  if (visibleParent.value < 0 || ::fstat(parent.value, &bound) != 0 || ::fstat(visibleParent.value, &visible) != 0 ||
      bound.st_dev != visible.st_dev || bound.st_ino != visible.st_ino) {
    cleanupEmpty(); record("restored"); return failed("saved-file-changed");
  }
  if (::syscall(SYS_renameat2, parent.value, name.constData(), stage.value, "entry", RENAME_NOREPLACE) != 0) {
    cleanupEmpty(); record("restored"); return failed("saved-file-unavailable");
  }
  // From the first move onward this is noninterruptible. Finish or retain a
  // discoverable entry; cancellation must never abandon an unrecorded move.
  if (::fsync(stage.value) != 0 || ::fsync(parent.value) != 0) return unknown();
  try { if (hooks.afterMove) hooks.afterMove(recovery.stagingPath); } catch (...) { return unknown(); }
  Descriptor moved(::openat(stage.value, "entry", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  const auto movedFile = moved.value >= 0 ? captureFileIdentity(moved.value) : std::nullopt;
  const bool originalMoved = movedFile && movedIdentity(*movedFile, *file.identity);
  const auto restore = [&](const QString &diagnostic) {
    try { if (hooks.beforeRestore) hooks.beforeRestore(recovery.stagingPath); } catch (...) { return unknown(); }
    if (::syscall(SYS_renameat2, stage.value, "entry", parent.value, name.constData(), RENAME_NOREPLACE) == 0) {
      if (::fsync(stage.value) != 0 || ::fsync(parent.value) != 0) return unknown();
      // Our own rename changes ctime. Preserve the verified original's live
      // binding after restoration, never adopt a displaced foreign entry.
      const auto restored = originalMoved ? captureFileIdentity(moved.value) : std::nullopt;
      if (restored && movedIdentity(*restored, *file.identity)) file.identity = restored;
      cleanupEmpty(); return record("restored") ? failed(diagnostic) : unknown();
    }
    // Preserve any displaced entry indefinitely. No path loaded from history
    // ever becomes authority to remove it; recovery/removal is explicit.
    return record("retained") ? failed("delete-recovery-retained") : unknown();
  };
  if (!originalMoved) return restore("saved-file-changed");
  if (::unlinkat(stage.value, "entry", 0) != 0) return restore("delete-failed");
  file.identity.reset(); // metadata remains historical fact, not file availability
  if (::fsync(stage.value) != 0) return unknown();
  cleanupEmpty(); return record("removed") ? ActionOutcome{} : unknown();
}
}
