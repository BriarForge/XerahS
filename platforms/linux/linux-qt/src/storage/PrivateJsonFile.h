// The one way XerahS persists small private JSON state (destinations,
// counters, watch folders, settings, preferences). SETCAT-005, FN-016,
// CORE-012, CORE-013.
//
// Every file is an envelope {"format": "<id>", "version": N, ...payload}:
//  - Owner-only (0600) file in a directory owned by the user. Symlinked
//    ancestors are resolved and allowed (/home -> /var/home, stowed ~/.config).
//  - Reads refuse a symlinked file, other owners of the file or its nearest
//    existing directory, and non-regular files (Unsafe),
//    files over maxBytes (TooLarge), bad JSON or the wrong format (Invalid),
//    and versions above the reader's (NewerVersion). None of these is ever
//    "repaired" by writing over the file; callers open read-only instead.
//  - Writes are atomic (QSaveFile, no direct-write fallback) and guarded by
//    <file>.lock. A write refuses with Changed if the bytes on disk differ from
//    the snapshot the caller last read, so two writers never silently lose
//    each other's update.
//  - Lock is exposed for stores that must hold it across a read and a later
//    write, such as counter reservation (FN-016).
//
// Callers keep their own stable diagnostic strings by mapping StoreStatus.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLockFile>
#include <QString>

#include <memory>

namespace xerahs::storage {

enum class StoreStatus { Ok, Missing, Unsafe, TooLarge, Invalid, NewerVersion, Busy, Changed, ReadFailed, WriteFailed };

struct StoreRead {
  StoreStatus status = StoreStatus::Missing;  // Missing: no file yet; document is empty
  QJsonObject document;                        // the whole envelope, format and version included
  int version = 0;
  QByteArray snapshot;                         // exact bytes read; empty when Missing
};

struct StoreOptions {
  QString format;               // required envelope "format" value
  int maxVersion = 1;           // newest version this build understands
  qint64 maxBytes = 1024 * 1024;
  int lockWaitMs = 0;           // 0: fail Busy at once
  int staleLockMs = 30000;
  // Files written before this helper have no "format" key. When true, such a
  // file is read as this format and gains the key on its next write.
  bool acceptLegacyWithoutFormat = false;
};

// A held <file>.lock. Destroying it releases the lock.
class StoreLock {
public:
  bool held() const { return m_lock && m_lock->isLocked(); }
private:
  friend class PrivateJsonFile;
  std::unique_ptr<QLockFile> m_lock;
};

class PrivateJsonFile {
public:
  PrivateJsonFile(QString path, StoreOptions options);
  const QString &path() const { return m_path; }

  StoreRead read() const;
  StoreStatus lock(StoreLock &lock) const;  // Busy when another holder keeps it
  // Writes envelope (format/version are set from options and the given version).
  // expectedSnapshot is the snapshot from the caller's last read() (empty if the
  // file was Missing). On Ok, *newSnapshot receives the bytes now on disk.
  // If held is null the write takes and releases the lock itself.
  StoreStatus write(QJsonObject payload, int version, const QByteArray &expectedSnapshot,
                    QByteArray *newSnapshot, const StoreLock *held = nullptr) const;

private:
  QString m_path;
  StoreOptions m_options;
};

QString storeStatusName(StoreStatus status);  // "ok", "missing", "unsafe", ...

}  // namespace xerahs::storage
