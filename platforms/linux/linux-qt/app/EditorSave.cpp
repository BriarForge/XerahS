#include "EditorSave.h"
#include "EditorSource.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
namespace xerahs::app {
using namespace xerahs::editor;
bool EditorFileIdentity::operator==(const EditorFileIdentity &o) const {
  return device == o.device && inode == o.inode && length == o.length && modifiedSeconds == o.modifiedSeconds &&
      modifiedNanos == o.modifiedNanos && changedSeconds == o.changedSeconds && changedNanos == o.changedNanos && owner == o.owner && mode == o.mode;
}
namespace {
constexpr qint64 maximumBytes = 1073741824;
bool cancelled(const EditorSaveControl &control) { return control.cancelled && control.cancelled(); }
void progress(const EditorSaveControl &control, EditorSavePhase phase, int percent) { if (control.progress) control.progress(phase, percent); }
struct Descriptor {
  int value = -1;
  explicit Descriptor(int fd) : value(fd) {}
  ~Descriptor() { if (value >= 0) ::close(value); }
  Descriptor(const Descriptor &) = delete;
  Descriptor &operator=(const Descriptor &) = delete;
};
EditorFileIdentity identity(const struct stat &info) {
  return {quint64(info.st_dev), quint64(info.st_ino), info.st_size, info.st_mtim.tv_sec, info.st_mtim.tv_nsec,
      info.st_ctim.tv_sec, info.st_ctim.tv_nsec, quint32(info.st_uid), quint32(info.st_mode & 0777)};
}
std::optional<EditorFileIdentity> fileIdentity(int fd) {
  struct stat info{};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1) return {};
  return identity(info);
}
EditorDestination destination(int directory, const QByteArray &name) {
  struct stat info{};
  if (::fstatat(directory, name.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0)
    return {errno == ENOENT ? EditorDestinationKind::Missing : EditorDestinationKind::Other, {}};
  if (!S_ISREG(info.st_mode) || info.st_uid != ::geteuid() || info.st_nlink != 1) return {};
  return {EditorDestinationKind::Regular, identity(info)};
}
bool unchanged(const EditorDestination &current, const EditorDestination &expected) {
  return current.kind == EditorDestinationKind::Missing || (current.kind == EditorDestinationKind::Regular &&
      expected.kind == EditorDestinationKind::Regular && current.identity && expected.identity && *current.identity == *expected.identity);
}
class Directory {
public:
  explicit Directory(const QString &path) : path(path), fd(::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
    struct stat info{}; if (fd.value >= 0 && ::fstat(fd.value, &info) == 0) { device = info.st_dev; inode = info.st_ino; }
  }
  bool current() const {
    Descriptor current(::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)); struct stat info{};
    return current.value >= 0 && ::fstat(current.value, &info) == 0 && quint64(info.st_dev) == device && quint64(info.st_ino) == inode;
  }
  QString path;
  Descriptor fd;
  quint64 device = 0, inode = 0;
};
class Temporary {
public:
  explicit Temporary(Directory &directory) : directory(directory), name((".xerahs-editor-" + QUuid::createUuid().toString(QUuid::WithoutBraces)).toUtf8()),
      fd(::openat(directory.fd.value, name.constData(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)) {}
  ~Temporary() {
    if (!published && isOwnName()) ::unlinkat(directory.fd.value, name.constData(), 0);
  }
  bool isOwnName() const {
    const auto own = fileIdentity(fd.value); const auto observed = destination(directory.fd.value, name);
    return own && observed.identity && own->device == observed.identity->device && own->inode == observed.identity->inode;
  }
  bool publish(const QByteArray &target, const EditorDestination &expected) {
    if (!directory.current() || !isOwnName()) return false;
    const auto current = destination(directory.fd.value, target);
    if (!unchanged(current, expected)) return false;
    const int result = current.kind == EditorDestinationKind::Missing
        ? int(::syscall(SYS_renameat2, directory.fd.value, name.constData(), directory.fd.value, target.constData(), RENAME_NOREPLACE))
        : ::renameat(directory.fd.value, name.constData(), directory.fd.value, target.constData());
    if (result != 0) return false;
    published = true; return true;
  }
  Directory &directory;
  QByteArray name;
  Descriptor fd;
  bool published = false;
};
QString hashDescriptor(int fd, const EditorSaveControl &control) {
  const auto before = fileIdentity(fd);
  if (!before || before->length <= 0 || before->length > maximumBytes || ::lseek(fd, 0, SEEK_SET) < 0) return {};
  QCryptographicHash digest(QCryptographicHash::Sha256); std::array<char, 65536> buffer{}; qint64 count = 0;
  while (!cancelled(control)) {
    const auto n = ::read(fd, buffer.data(), buffer.size());
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return {};
    if (n == 0) break;
    count += n; if (count > before->length) return {};
    digest.addData(QByteArrayView(buffer.data(), n));
  }
  const auto after = fileIdentity(fd);
  return !cancelled(control) && count == before->length && after && *after == *before
      ? QString("sha256:") + QString::fromLatin1(digest.result().toHex()) : QString{};
}
bool boundRaster(Directory &directory, const EditorSaveReceipt &receipt, const EditorSaveControl &control, bool hash) {
  if (directory.device != receipt.directoryDevice || directory.inode != receipt.directoryInode || !directory.current()) return false;
  const auto name = QFile::encodeName(QFileInfo(receipt.path).fileName());
  Descriptor fd(::openat(directory.fd.value, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  const auto observed = fileIdentity(fd.value);
  if (!observed || !(*observed == receipt.raster)) return false;
  return !hash || hashDescriptor(fd.value, control) == receipt.rasterHash;
}
bool supportedPreviousSidecar(Directory &directory, const QByteArray &name, const EditorDestination &expected, const EditorSaveControl &control) {
  const auto current = destination(directory.fd.value, name);
  if (current.kind == EditorDestinationKind::Missing) return true;
  if (current.kind != EditorDestinationKind::Regular || expected.kind != EditorDestinationKind::Regular ||
      !current.identity || !expected.identity || !(*current.identity == *expected.identity) || current.identity->length > maximumBytes) return false;
  Descriptor fd(::openat(directory.fd.value, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  const auto observed = fileIdentity(fd.value); if (!observed || !(*observed == *current.identity)) return false;
  QByteArray bytes; std::array<char, 65536> buffer{};
  while (!cancelled(control)) {
    const auto count = ::read(fd.value, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 || bytes.size() + count > maximumBytes) return false;
    if (count == 0) break;
    bytes.append(buffer.data(), count);
  }
  const auto after = fileIdentity(fd.value);
  // Never overwrite/remove an unsupported future or damaged companion.
  return !cancelled(control) && after && *after == *observed && readXann(bytes).document.has_value();
}
class NativeSidecarStore final : public SidecarStore {
public:
  NativeSidecarStore(Directory &directory, const EditorSaveReceipt &receipt, const EditorSaveControl &control, const EditorSaveHooks &hooks)
      : directory(directory), receipt(receipt), control(control), hooks(hooks), name(QFile::encodeName(QFileInfo(receipt.path).fileName() + ".xann")) {}
  bool exists(const QString &) override { return destination(directory.fd.value, name).kind != EditorDestinationKind::Missing; }
  bool write(const QString &path, const QByteArray &bytes) override {
    if (bytes.size() > maximumBytes || cancelled(control) || !supportedPreviousSidecar(directory, name, receipt.previousSidecar, control)) return false;
    Temporary temporary(directory); QFile output;
    if (temporary.fd.value < 0 || !output.open(temporary.fd.value, QIODevice::WriteOnly, QFileDevice::DontCloseHandle) ||
        output.write(bytes) != bytes.size() || !output.flush() || ::fsync(temporary.fd.value) != 0) return false;
    if (hooks.beforePublish) hooks.beforePublish(EditorSaveArtifact::Sidecar, path);
    if (cancelled(control) || !boundRaster(directory, receipt, control, false)) return false;
    return temporary.publish(name, receipt.previousSidecar);
  }
  bool remove(const QString &path) override {
    if (!supportedPreviousSidecar(directory, name, receipt.previousSidecar, control)) return false;
    if (hooks.beforePublish) hooks.beforePublish(EditorSaveArtifact::Sidecar, path);
    if (cancelled(control) || !boundRaster(directory, receipt, control, false) || !directory.current()) return false;
    const auto current = destination(directory.fd.value, name);
    if (current.kind == EditorDestinationKind::Missing) return true;
    if (current.kind != EditorDestinationKind::Regular || receipt.previousSidecar.kind != EditorDestinationKind::Regular ||
        !current.identity || !receipt.previousSidecar.identity || !(*current.identity == *receipt.previousSidecar.identity)) return false;
    return ::unlinkat(directory.fd.value, name.constData(), 0) == 0;
  }
  Directory &directory;
  const EditorSaveReceipt &receipt;
  const EditorSaveControl &control;
  const EditorSaveHooks &hooks;
  QByteArray name;
};
void saveAnnotations(SaveOutcome &outcome, Directory &directory, const EditorSaveControl &control, const EditorSaveHooks &hooks) {
  progress(control, EditorSavePhase::SavingAnnotations, 85);
  if (cancelled(control) || !outcome.receipt || !boundRaster(directory, *outcome.receipt, control, false)) { outcome.sidecarError = diagnostic::sidecarSaveFailed; return; }
  NativeSidecarStore store(directory, *outcome.receipt, control, hooks);
  const auto saved = saveSidecar(store, outcome.receipt->path, outcome.receipt->document, true);
  outcome.sidecarSaved = saved.ok; outcome.sidecarPath = saved.sidecarPath;
  if (!saved.ok) outcome.sidecarError = diagnostic::sidecarSaveFailed;
}
}
SaveOutcome saveEdit(const QString &requested, const QImage &source, const AnnotationDocument &document,
                    const EditorSaveControl &control, const EditorSaveHooks &hooks) {
  SaveOutcome outcome;
  try {
    if (cancelled(control)) { outcome.cancelled = true; return outcome; }
    if (requested.isEmpty() || requested.size() > 32768 || requested.contains(QChar::Null)) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    const auto path = QDir::isAbsolutePath(requested) ? requested : QDir::currentPath() + '/' + requested;
    const QFileInfo info(path); Directory directory(info.path()); const auto name = QFile::encodeName(info.fileName());
    if (directory.fd.value < 0 || name.isEmpty() || name == "." || name == "..") { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    const auto expected = destination(directory.fd.value, name);
    if (expected.kind == EditorDestinationKind::Other) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    // Allocate all acknowledgement/document state before the first publication.
    auto receipt = std::make_shared<EditorSaveReceipt>(); receipt->path = path;
    receipt->directoryDevice = directory.device; receipt->directoryInode = directory.inode;
    receipt->previousSidecar = destination(directory.fd.value, name + ".xann"); receipt->document = document;
    const auto parsed = parseDocument(serializeDocument(document));
    if (!parsed.document) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    progress(control, EditorSavePhase::Rendering, 0);
    const auto rendered = renderEditorImage(source, document, {control.cancelled, [&](int p) { progress(control, EditorSavePhase::Rendering, p / 2); }});
    if (rendered.image.isNull() || cancelled(control)) { outcome.cancelled = cancelled(control); if (!outcome.cancelled) outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    progress(control, EditorSavePhase::Encoding, 55); Temporary temporary(directory); QFile output;
    if (temporary.fd.value < 0 || !output.open(temporary.fd.value, QIODevice::WriteOnly, QFileDevice::DontCloseHandle)) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    const bool encoded = hooks.encode ? hooks.encode(rendered.image, &output) : rendered.image.save(&output, "PNG");
    if (!encoded || !output.flush() || ::fsync(temporary.fd.value) != 0) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    receipt->rasterHash = hashDescriptor(temporary.fd.value, control);
    if (receipt->rasterHash.isEmpty() || cancelled(control)) { outcome.cancelled = cancelled(control); if (!outcome.cancelled) outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    receipt->document.imagePath = path; receipt->document.imageHash = receipt->rasterHash;
    receipt->document.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    progress(control, EditorSavePhase::PublishingRaster, 80);
    if (hooks.beforePublish) hooks.beforePublish(EditorSaveArtifact::Raster, path);
    if (cancelled(control)) { outcome.cancelled = true; return outcome; }
    if (!temporary.publish(name, expected)) { outcome.rasterError = diagnostic::rasterSaveFailed; return outcome; }
    outcome.rasterSaved = true; outcome.receipt = receipt;
    const auto published = fileIdentity(temporary.fd.value);
    if (!published) { outcome.sidecarError = diagnostic::sidecarSaveFailed; return outcome; }
    receipt->raster = *published;
    saveAnnotations(outcome, directory, control, hooks);
    outcome.cancelled = cancelled(control); progress(control, EditorSavePhase::Complete, 100); return outcome;
  } catch (...) {
    if (!outcome.rasterSaved) outcome.rasterError = diagnostic::rasterSaveFailed;
    else if (!outcome.sidecarSaved) outcome.sidecarError = diagnostic::sidecarSaveFailed;
    return outcome;
  }
}
SaveOutcome retryEditorSidecar(const EditorSaveReceiptPtr &receipt, const EditorSaveControl &control, const EditorSaveHooks &hooks) {
  SaveOutcome outcome; outcome.receipt = receipt; outcome.rasterReused = true;
  try {
    if (!receipt) { outcome.sidecarError = diagnostic::sidecarSaveFailed; return outcome; }
    // Historical publication is acknowledged even when validation now fails;
    // it is never permission to rewrite or recreate the raster.
    outcome.rasterSaved = true; Directory directory(QFileInfo(receipt->path).path());
    if (cancelled(control) || !boundRaster(directory, *receipt, control, true)) {
      outcome.cancelled = cancelled(control); outcome.sidecarError = diagnostic::sidecarSaveFailed; return outcome;
    }
    saveAnnotations(outcome, directory, control, hooks);
    outcome.cancelled = cancelled(control); progress(control, EditorSavePhase::Complete, 100); return outcome;
  } catch (...) { if (!outcome.sidecarSaved) outcome.sidecarError = diagnostic::sidecarSaveFailed; return outcome; }
}
} // namespace xerahs::app
