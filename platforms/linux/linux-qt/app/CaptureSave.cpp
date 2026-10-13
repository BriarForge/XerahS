#include "CaptureSave.h"
#include "naming/FilenameGenerator.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
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
}
CaptureSaveResult saveCapture(const QImage &media, const QString &directory, const QString &pattern,
                              const CaptureSaveHooks &hooks) {
  using namespace xerahs::naming;
  try {
    if (media.isNull()) return failure(QStringLiteral("save-write-failed"));
    const QDir destination(QDir(directory).absolutePath());
    if (!QDir().mkpath(destination.absolutePath())) return failure(QStringLiteral("save-directory-unavailable"));
    auto random = makeSystemRandomSource(); NoFileAccess files;
    const auto now = QDateTime::currentDateTime();
    ExpansionContext context;
    context.date = now.date(); context.time = now.time(); context.unixTime = now.toSecsSinceEpoch();
    context.locale = QLocale::system(); context.counter = 0;
    context.width = media.width(); context.height = media.height(); context.random = random.get(); context.files = &files;
    // The filename context is immutable for this action. Random collisions use
    // fresh randomness; workflow counter persistence remains separate work.
    QJsonObject filenameContext{{"pattern", pattern}, {"mode", "filename"}, {"extension", "png"},
        {"date", context.date.toString(Qt::ISODate)}, {"time", context.time.toString(Qt::ISODateWithMs)},
        {"unix_time", context.unixTime}, {"locale", context.locale.name()}, {"counter", context.counter},
        {"width", media.width()}, {"height", media.height()}};
    // Encode once into a private file in the destination filesystem. No final
    // pathname exposes partial bytes; failed attempts remove only our own temp.
    QTemporaryFile encoded(destination.filePath(QStringLiteral(".xerahs-capture-XXXXXX")));
    if (!encoded.open() || !encoded.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return failure(QStringLiteral("save-write-failed"));
    const bool written = hooks.encode ? hooks.encode(media, &encoded) : media.save(&encoded, "PNG");
    if (!written || !encoded.flush() || ::fsync(encoded.handle()) != 0 || encoded.size() <= 0) return failure(QStringLiteral("save-write-failed"));
    const auto length = encoded.size();
    const auto source = QFile::encodeName(encoded.fileName());
    for (int attempt = 0; attempt < 3; ++attempt) {
      const auto name = expand({pattern, ParseMode::Filename, QStringLiteral("png"), std::nullopt}, context);
      if (!name.ok()) return failure(QStringLiteral("filename-") + errorCodeName(name.error().code));
      const auto path = destination.filePath(name.value());
      filenameContext.insert("next_counter", name.nextCounter);
      CaptureFile artifact{path, QStringLiteral("image/png"), length, filenameContext};
      if (hooks.beforePublish) hooks.beforePublish(path);
      const auto target = QFile::encodeName(path);
      // PCA-007: Linux atomic publication refuses every existing destination,
      // including dangling symlinks and files created after name generation.
      // Unsupported filesystems fail explicitly; never fall back to replacement.
      if (::syscall(SYS_renameat2, AT_FDCWD, source.constData(), AT_FDCWD, target.constData(), RENAME_NOREPLACE) == 0) {
        encoded.setAutoRemove(false);
        artifact.identity = captureFileIdentity(encoded.handle());
        return {std::move(artifact), {}};
      }
      if (errno != EEXIST) return failure(QStringLiteral("save-write-failed"));
    }
    return failure(QStringLiteral("save-name-collision"));
  } catch (...) { return failure(QStringLiteral("save-write-failed")); }
}
} // namespace xerahs::app
