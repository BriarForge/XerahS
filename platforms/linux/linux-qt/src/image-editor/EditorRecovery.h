// EDITOR-SESSION-001 ES-018: optional editor autosave / crash recovery. A
// recovery document is a separate private file under the XDG state directory.
// It is never the user's save target or its sidecar, so it can never overwrite
// the last explicit save. Each file is one PrivateJsonFile envelope holding the
// canonical annotation document (serializeDocument) plus the source reference
// and the time it was written, which the restart notice discloses.
#pragma once

#include "image-editor/AnnotationDocument.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace xerahs::editor {

struct RecoveryRecord {
  QString id;                  // lowercase UUID without braces; names the file
  QDateTime savedAt;           // UTC time of the last recovery write
  QString sourcePath;          // absolute raster path of the opened file; empty = unsaved capture
  qint64 ownerPid = 0;         // writing process, so a live editor is never offered back to itself
  AnnotationDocument document;
};

struct RecoveryListing {
  std::vector<RecoveryRecord> records;  // newest first
  int setAside = 0;                     // corrupt files renamed to *.corrupt during this listing
  QStringList diagnostics;              // value-free codes only
};

class EditorRecoveryStore {
public:
  // Bounds: only supported records with exited owners may be count-pruned.
  // A protected full collection refuses a new record; existing records remain
  // writable. A record above kMaxFileBytes is not written.
  static constexpr int kMaxRecords = 8;
  static constexpr int kMaxSetAside = 4;
  static constexpr qint64 kMaxFileBytes = 192LL * 1024 * 1024;

  explicit EditorRecoveryStore(QString directory, std::function<QDateTime()> clock = {});
  // $XDG_STATE_HOME/com.xerahs.app/editor-recovery, or ~/.local/state/...
  static QString defaultDirectory();
  static QString newId();
  static bool validId(const QString &id);
  const QString &directory() const { return m_directory; }

  // Returns an empty string on success, otherwise a stable value-free code.
  QString write(const QString &id, const QString &sourcePath, const AnnotationDocument &document, qint64 ownerPid);
  QString remove(const QString &id);
  RecoveryListing list();
  // Retain only this listed record's optimistic-write snapshot for its editor.
  EditorRecoveryStore forRecord(const QString &id) const;

private:
  QString pathFor(const QString &id) const;
  void trimSetAside();
  QString m_directory;
  std::function<QDateTime()> m_clock;
  QMap<QString, QByteArray> m_snapshots;  // last bytes written per id, for the optimistic-write guard
};

// Restart disclosure text: age and source, never document content.
QString describeRecoveryAge(qint64 seconds);
QString describeRecoverySource(const RecoveryRecord &record);
QString recoveryDisclosure(const RecoveryRecord &record, const QDateTime &now);

}  // namespace xerahs::editor
