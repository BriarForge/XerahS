// CORE-027: value-free, current-session diagnostic metadata and explicit export.
#pragma once
#include "settings/ApplicationSettings.h"
#include "storage/PrivateJsonFile.h"
#include <QDateTime>
#include <QJsonArray>
#include <QMap>
#include <QMutex>
#include <utility>

namespace xerahs::actions { struct PipelineResult; }
namespace xerahs::diagnostics {
enum class Severity { Warning, Error };

// One latest observation per allowlisted code, in RAM only. Unknown codes are
// grouped into a fixed generic code; they cannot grow the buffer or leak text.
class DiagnosticLog {
public:
  bool append(const QString &code, const QString &correlation, Severity severity,
              const QDateTime &at = QDateTime::currentDateTimeUtc());
  QJsonArray recent() const;
private:
  struct Event { QString code, correlation; Severity severity = Severity::Warning; QDateTime at; };
  mutable QMutex m_mutex;
  QMap<QString, Event> m_events;
};
void recordPipelineErrors(DiagnosticLog &, const actions::PipelineResult &);

// Immutable once reviewed. The exporter cannot acquire new settings, paths,
// payloads or credentials between preview and publication.
class DiagnosticBundle {
public:
  const QJsonObject &document() const { return m_document; }
  QByteArray bytes() const;
  // New files only; existing/changed/unsafe files are kept untouched.
  storage::StoreStatus save(const QString &path) const;
private:
  friend DiagnosticBundle diagnosticBundle(const settings::SettingsRead &, const DiagnosticLog &, const QDateTime &);
  explicit DiagnosticBundle(QJsonObject document) : m_document(std::move(document)) {}
  QJsonObject m_document;
};
DiagnosticBundle diagnosticBundle(const settings::SettingsRead &, const DiagnosticLog &,
                                  const QDateTime &at = QDateTime::currentDateTimeUtc());
}
