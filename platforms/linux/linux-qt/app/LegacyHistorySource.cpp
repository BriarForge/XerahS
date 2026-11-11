#include "LegacyHistorySource.h"
#include "LegacyHistorySqlite.h"
#include <QFile>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace xerahs::app {
LegacyHistorySourceRead readLegacyHistorySource(const QString &path, const std::function<bool()> &stop,
    const std::function<void(int)> &progress) {
  LegacyHistorySourceRead result;
  const auto fail = [&](const QString &code) { result.preview.code = code; result.source.reset(); return result; };
  if (stop && stop()) return fail("cancelled");
  const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return fail("legacy-history-unreadable");
  struct stat info{};
  if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) { ::close(fd); return fail("legacy-history-unreadable"); }
  QFile file;
  if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); return fail("legacy-history-unreadable"); }
  const auto header = file.peek(16);
  if (header == QByteArray("SQLite format 3\0", 16)) {
    file.close();
    auto sqlite = readLegacySqliteHistory(path, stop, progress);
    if (!sqlite.snapshot) return fail(sqlite.code);
    result.source = std::move(*sqlite.snapshot);
  } else {
    const auto bytes = file.read(history::kLegacyHistoryMaxBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail("legacy-history-unreadable");
    result.source = bytes;
  }
  if (stop && stop()) return fail("cancelled");
  result.preview = history::previewLegacyHistorySnapshot(*result.source);
  if (stop && stop()) return fail("cancelled");
  if (!result.preview.ok()) result.source.reset();
  if (progress) progress(100);
  return result;
}
}
