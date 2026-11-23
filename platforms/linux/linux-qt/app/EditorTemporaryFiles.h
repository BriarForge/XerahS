#pragma once
#include <QString>
#include <QDateTime>
namespace xerahs::app {
// ES-020 on decision ES-020-RETENTION-2026-11-23: the retention limits for
// owner-private managed preparation/upload files. QTemporaryFile removes them
// at task end; startup unlinks abandoned files older than the configured
// bound (default one day; 0 sweeps every startup, the reference's own
// startup-clean, safe on POSIX because an in-flight task's open handle
// survives unlinking). Secure delete, when configured, overwrites with one
// zero-fill pass and syncs before unlink - shred's documented caveat applies:
// filesystems and devices that do not overwrite in place (SSD wear
// levelling, COW, mirrors) make this a reduction, never a promise.
QString editorTemporaryDirectory();
QString editorTemporaryTemplate();
bool sweepEditorTemporaryFiles(const QString &directory, const QDateTime &now = QDateTime::currentDateTimeUtc(),
                               int retentionDays = 1, bool secureDelete = false);
}
