#include "CaptureSave.h"
#include "naming/FilenameGenerator.h"
#include "naming/CounterStore.h"
#include <QStandardPaths>
#include "actions/SaveAsTarget.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>
namespace xerahs::app {
namespace {
class NoFileAccess final : public xerahs::naming::RandomFileAccess {
public:
  xerahs::naming::RandomFileRead read(const QString &) override {
    return {xerahs::naming::RandomFileRead::Status::Denied, {}};
  }
};
CaptureSaveResult failure(const QString &code) { return {std::nullopt, code}; }
bool encodePrivatePng(QTemporaryFile &encoded, const QImage &media, const CaptureSaveHooks &hooks) {
  if (!encoded.open() || !encoded.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
  const bool written = hooks.encode ? hooks.encode(media, &encoded) : media.save(&encoded, "PNG");
  return written && encoded.flush() && ::fsync(encoded.handle()) == 0 && encoded.size() > 0;
}
}
QString defaultCounterPath() { return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("filename-counters.json")); }
CaptureSaveResult saveCapture(const QImage &media, const QString &directory, const QString &pattern,
                              const CaptureSaveHooks &hooks) {
  using namespace xerahs::naming;
  try {
    if (media.isNull()) return failure(QStringLiteral("save-write-failed"));
    const QDir destination(QDir(directory).absolutePath());
    auto random = makeSystemRandomSource(); NoFileAccess files;
    const auto now = QDateTime::currentDateTime();
    ExpansionContext context;
    context.date = now.date(); context.time = now.time(); context.unixTime = now.toSecsSinceEpoch();
    context.locale = QLocale::system(); context.counter = 0;
    context.width = media.width(); context.height = media.height(); context.random = random.get(); context.files = &files;
    auto name = expand({pattern, ParseMode::Filename, QStringLiteral("png"), std::nullopt}, context);
    if (!name.ok()) return failure(QStringLiteral("filename-") + errorCodeName(name.error().code));
    // FN-007/FN-016: a counter token reserves the persisted value now (lock held
    // across publication) and commits only after the name is published; every
    // earlier return rolls back by destroying the reservation.
    CounterStore counters(hooks.counterPath.isEmpty() ? defaultCounterPath() : hooks.counterPath);
    CounterReservation reservation;
    if (name.nextCounter != context.counter && !hooks.counterProfile.isEmpty()) {
      auto reserved = counters.reserve(hooks.counterProfile);
      if (reserved.error != CounterError::None) return failure(counterErrorName(reserved.error));
      reservation = std::move(reserved.reservation); context.counter = reservation.value();
      name = expand({pattern, ParseMode::Filename, QStringLiteral("png"), std::nullopt}, context);
      if (!name.ok()) return failure(QStringLiteral("filename-") + errorCodeName(name.error().code));
    }
    // Validation precedes directory creation and private encoding (FN-026).
    if (!QDir().mkpath(destination.absolutePath())) return failure(QStringLiteral("save-directory-unavailable"));
    // The filename context is immutable for this action. Random collisions use
    // fresh randomness; a counter collision advances the reserved counter.
    QJsonObject filenameContext{{"pattern", pattern}, {"mode", "filename"}, {"extension", "png"},
        {"date", context.date.toString(Qt::ISODate)}, {"time", context.time.toString(Qt::ISODateWithMs)},
        {"unix_time", context.unixTime}, {"locale", context.locale.name()}, {"counter", context.counter},
        {"width", media.width()}, {"height", media.height()}};
    // Encode once into a private file in the destination filesystem. No final
    // pathname exposes partial bytes; failed attempts remove only our own temp.
    QTemporaryFile encoded(destination.filePath(QStringLiteral(".xerahs-capture-XXXXXX")));
    if (!encodePrivatePng(encoded, media, hooks)) return failure(QStringLiteral("save-write-failed"));
    const auto length = encoded.size();
    const auto source = QFile::encodeName(encoded.fileName());
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (attempt) { if (reservation) context.counter = name.nextCounter; name = expand({pattern, ParseMode::Filename, QStringLiteral("png"), std::nullopt}, context); }
      if (!name.ok()) return failure(QStringLiteral("filename-") + errorCodeName(name.error().code));
      const auto path = destination.filePath(name.value());
      filenameContext.insert("counter", context.counter);
      filenameContext.insert("next_counter", name.nextCounter);
      CaptureFile artifact{path, QStringLiteral("image/png"), length, filenameContext};
      if (hooks.publicationIntent) hooks.publicationIntent(path, false);
      if (hooks.beforePublish) hooks.beforePublish(path);
      const auto target = QFile::encodeName(path);
      // PCA-007: Linux atomic publication refuses every existing destination,
      // including dangling symlinks and files created after name generation.
      // Unsupported filesystems fail explicitly; never fall back to replacement.
      if (::syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD, target.constData(), RENAME_NOREPLACE) == 0) {
        encoded.setAutoRemove(false);
        artifact.identity = captureFileIdentity(encoded.handle());
        // The name is published; a failed commit cannot unpublish it and the
        // exclusive rename already guards against reuse (FN-016).
        if (reservation) {
          CounterError committed = CounterError::WriteFailed;
          try { committed = counters.commit(reservation, name.nextCounter); } catch (...) {}
          if (committed != CounterError::None) return {std::move(artifact), counterErrorName(committed)};
        }
        return {std::move(artifact), {}};
      }
      if (errno != EEXIST) return failure(QStringLiteral("save-write-failed"));
    }
    return failure(QStringLiteral("save-name-collision"));
  } catch (...) { return failure(QStringLiteral("save-write-failed")); }
}
CaptureSaveResult saveCaptureAs(const QImage &media, const QString &requested, const xerahs::actions::CancellationToken &cancellation,
                               const CaptureSaveHooks &hooks) {
  try {
    if (cancellation.requested()) return {{}, {}, true};
    const auto target = xerahs::actions::captureSaveAsTarget(requested);
    if (!target.valid()) return failure(target.diagnostic);
    if (media.isNull()) return failure("save-write-failed");
    const QFileInfo info(target.path); const auto directory = info.dir();
    if (!directory.exists()) return failure("save-directory-unavailable");
    QTemporaryFile encoded(directory.filePath(".xerahs-capture-XXXXXX"));
    if (!encodePrivatePng(encoded, media, hooks)) return failure("save-write-failed");
    if (cancellation.requested()) return {{}, {}, true};
    QJsonObject context{{"mode", "explicit-save-as"}, {"extension", "png"}, {"file_name", info.fileName()},
        {"width", media.width()}, {"height", media.height()}};
    CaptureFile artifact{target.path, "image/png", encoded.size(), context}; artifact.artifact = "save-as.file";
    if (hooks.publicationIntent) hooks.publicationIntent(target.path, false);
    if (hooks.beforePublish) hooks.beforePublish(target.path);
    if (cancellation.requested()) return {{}, {}, true};
    const auto from = QFile::encodeName(encoded.fileName()); const auto to = QFile::encodeName(target.path);
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) != 0)
      return failure(errno == EEXIST ? "save-as-destination-exists" : "save-write-failed");
    encoded.setAutoRemove(false); artifact.identity = captureFileIdentity(encoded.handle());
    return {std::move(artifact), {}};
  } catch (...) { return failure("save-write-failed"); }
}
namespace {
class CaptureSidecarStore final : public xerahs::editor::SidecarStore {
public:
  CaptureSidecarStore(const CaptureFile &raster, const xerahs::actions::CancellationToken &token, const CaptureSaveHooks &hooks)
      : m_raster(raster), m_token(token), m_hooks(hooks) {}
  bool exists(const QString &path) override {
    struct stat info{}; const auto name = QFile::encodeName(path);
    // Treat every error except definite absence as an existing destination.
    return ::lstat(name.constData(), &info) == 0 || errno != ENOENT;
  }
  bool remove(const QString &) override {
    // New capture paths have no previous owned companion. Preserve colliding
    // files rather than infer deletion authority from their suffix.
    return false;
  }
  bool write(const QString &path, const QByteArray &bytes) override {
    if (m_token.requested() || !validateCaptureFile(m_raster).isEmpty()) return false;
    QTemporaryFile encoded(QFileInfo(path).dir().filePath(".xerahs-sidecar-XXXXXX"));
    if (!encoded.open() || !encoded.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        encoded.write(bytes) != bytes.size() || !encoded.flush() || ::fsync(encoded.handle()) != 0) return false;
    if (m_token.requested()) return false;
    if (m_hooks.publicationIntent) m_hooks.publicationIntent(path, true);
    if (m_hooks.beforePublish) m_hooks.beforePublish(path);
    if (m_token.requested() || !validateCaptureFile(m_raster).isEmpty()) return false;
    CaptureFile companion{path, "application/gzip", encoded.size(), {}};
    companion.artifact = m_raster.artifact == "save.file" ? "save.sidecar" : "save-as.sidecar";
    const auto from = QFile::encodeName(encoded.fileName()); const auto to = QFile::encodeName(path);
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) != 0) return false;
    encoded.setAutoRemove(false);
    companion.identity = captureFileIdentity(encoded.handle()); m_file = std::move(companion); return true;
  }
  std::optional<CaptureFile> file() const { return m_file; }
private:
  CaptureFile m_raster;
  const xerahs::actions::CancellationToken &m_token;
  const CaptureSaveHooks &m_hooks;
  std::optional<CaptureFile> m_file;
};
}
CaptureSidecarResult saveCaptureSidecar(const CaptureFile &raster, const xerahs::editor::AnnotationDocument &document,
    const xerahs::actions::CancellationToken &cancellation, const CaptureSaveHooks &hooks) {
  using namespace xerahs::editor;
  try {
    if (cancellation.requested()) return {false, {}, diagnostic::sidecarSaveFailed};
    auto saved = document; saved.imagePath = raster.path; saved.imageHash = captureFileHash(raster, cancellation);
    if (saved.imageHash.isEmpty()) return {false, {}, diagnostic::sidecarSaveFailed};
    saved.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto parsed = parseDocument(serializeDocument(saved));
    if (!parsed.document || cancellation.requested()) return {false, {}, diagnostic::sidecarSaveFailed};
    CaptureSidecarStore store(raster, cancellation, hooks);
    const auto result = saveSidecar(store, raster.path, *parsed.document, true);
    return {result.ok, store.file(), result.diagnostic.value_or(QString{})};
  } catch (...) { return {false, {}, diagnostic::sidecarSaveFailed}; }
}
} // namespace xerahs::app
