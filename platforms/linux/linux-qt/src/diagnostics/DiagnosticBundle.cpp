#include "DiagnosticBundle.h"
#include "shell/ErrorPresentation.h"
#include "actions/PostCapturePipeline.h"
#include "capture/RegionGeometry.h"
#include "image-editor/AnnotationDocument.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QSysInfo>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <mutex>

namespace xerahs::diagnostics {
namespace {
const QString format = QStringLiteral("com.xerahs.diagnostics");
QString timestamp(const QDateTime &at) { return at.toUTC().toString(Qt::ISODateWithMs); }
bool recognizedCode(const QString &code) {
  if (shell::findErrorPresentation(code)) return true;
  // Reuse the production domains' declared vocabulary. Never accept a code
  // just because arbitrary message text happens to resemble a stable ID.
  static const QSet<QString> native{
    capture::diagnostic::capturePermissionDenied, capture::diagnostic::displayTopologyChanged,
    capture::diagnostic::captureSourceFailed, capture::diagnostic::captureTopologyMismatch, capture::diagnostic::lastRegionInvalid,
    actions::diagnostic::dependencyUnavailable, actions::diagnostic::uploadDeclined, actions::diagnostic::deleteGuard,
    actions::diagnostic::actionFailed, actions::diagnostic::temporaryFileUnavailable,
    editor::diagnostic::sourceUnsupported, editor::diagnostic::sourceCorrupt, editor::diagnostic::sourceEmpty,
    editor::diagnostic::sourceTooLarge, editor::diagnostic::documentVersionUnsupported, editor::diagnostic::documentInvalid,
    editor::diagnostic::documentTooLarge, editor::diagnostic::rasterSaveFailed, editor::diagnostic::sidecarSaveFailed,
    QStringLiteral("audio-device-missing"),
    actions::failureClassName(actions::FailureClass::PermissionDenied), actions::failureClassName(actions::FailureClass::ValidationFailure),
    actions::failureClassName(actions::FailureClass::UnavailableDependency), actions::failureClassName(actions::FailureClass::TransientFailure),
    actions::failureClassName(actions::FailureClass::PermanentFailure)
  };
  return native.contains(code);
}
}
bool DiagnosticLog::append(const QString &code, const QString &correlation, Severity severity, const QDateTime &at) {
  const QUuid uuid(correlation);
  if (uuid.isNull() || uuid.toString(QUuid::WithoutBraces) != correlation || !at.isValid()) return false;
  const auto safeCode = recognizedCode(code) ? code : QStringLiteral("operation-attention-required");
  std::lock_guard<QMutex> lock(m_mutex);
  m_events.insert(safeCode, {safeCode, correlation, severity, at.toUTC()});
  return true;
}
QJsonArray DiagnosticLog::recent() const {
  QList<Event> events;
  { std::lock_guard<QMutex> lock(m_mutex); events = m_events.values(); }
  std::stable_sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.at > b.at; });
  QJsonArray result;
  for (const auto &event : events)
    result.append(QJsonObject{{"code", event.code}, {"correlationId", event.correlation},
        {"severity", event.severity == Severity::Error ? "error" : "warning"}, {"timestamp", timestamp(event.at)}});
  return result;
}
void recordPipelineErrors(DiagnosticLog &log, const actions::PipelineResult &result) {
  for (const auto &action : result.orderedResults) {
    if (action.state != actions::ActionState::Failed) continue;
    const auto code = action.diagnostic.value_or(actions::diagnostic::actionFailed);
    const auto safe = recognizedCode(code) ? code : actions::failureClassName(action.failureClass.value_or(actions::classifyFailure(code)));
    log.append(safe, result.taskId, Severity::Error);
  }
}
DiagnosticBundle diagnosticBundle(const settings::SettingsRead &read, const DiagnosticLog &log, const QDateTime &at) {
  QJsonArray fields;
  // Never traverse values, unknown field names, diagnostic strings or import
  // reports: even a private value in a known field cannot enter the export.
  for (const auto &field : settings::settingFields())
    fields.append(QJsonObject{{"field", field.key}, {"state", settings::fieldStateName(read.fields.value(field.key, settings::FieldState::Missing))}});
  const auto appVersion = QCoreApplication::applicationVersion();
  return DiagnosticBundle({{"format", format}, {"version", 1}, {"generatedAt", timestamp(at)},
      {"runtime", QJsonObject{{"applicationId", "com.xerahs.app"},
          {"applicationVersion", appVersion.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(appVersion)},
          {"applicationVersionState", appVersion.isEmpty() ? "unversioned-development-build" : "assigned"},
          {"qtVersion", QString::fromLatin1(qVersion())}, {"os", QSysInfo::productType()},
          {"osVersion", QSysInfo::productVersion()}, {"kernelVersion", QSysInfo::kernelVersion()}}},
      {"settings", QJsonObject{{"writable", read.writable}, {"fields", fields}}},
      {"recentErrors", log.recent()}, {"errorRetention", "latest occurrence per recognized code; current session only"}});
}
QByteArray DiagnosticBundle::bytes() const { return QJsonDocument(m_document).toJson(); }
storage::StoreStatus DiagnosticBundle::save(const QString &path) const {
  if (!QDir::isAbsolutePath(path)) return storage::StoreStatus::Unsafe;
  storage::PrivateJsonFile file(path, {format});
  // The empty expected snapshot reserves only an absent target. The shared
  // writer locks and checks again before atomic owner-private publication.
  return file.write(m_document, 1, {}, nullptr);
}
}
