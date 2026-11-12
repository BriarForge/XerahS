#include "FileOperationExecution.h"
namespace xerahs::workflows {
using xerahs::actions::FailureClass;
QJsonObject fileOperationSnapshot(const QString &operation, const QString &destinationId, const QString &destinationName, const std::optional<QJsonObject> &rule) {
  return {{"definitionVersion", 1}, {"id", operation}, {"capture", "file-operation"}, {"operation", operation},
      {"destination", destinationId.isEmpty() && destinationName.isEmpty() ? QJsonValue() : QJsonValue(QJsonObject{{"id", destinationId}, {"name", destinationName}})},
      {"rule", rule ? QJsonValue(*rule) : QJsonValue()}};
}
FailureClass classifyFileOperationFailure(const QString &code, int httpStatus) {
  if (httpStatus >= 400) return xerahs::actions::classifyFailure(code, httpStatus);
  if (code == "input-not-file" || code == "input-too-large") return FailureClass::ValidationFailure;
  if (code == "input-unreadable") return FailureClass::PermissionDenied;
  if (code == "destination-missing" || code == "destination-catalog-unreadable") return FailureClass::UnavailableDependency;
  return xerahs::actions::classifyFailure(code, httpStatus);
}
bool uploadRemoteOutcomeUnknown(const QString &code, int attempts) {
  return attempts > 0 && (code == QLatin1String("timeout") || code == QLatin1String("network-error"));
}
QString fileOperationDiagnostic(const QString &code) {
  const auto trimmed = code.section(QLatin1Char(';'), 0, 0).section(QLatin1Char(':'), 0, 0).trimmed();
  static const QStringList known{"input-missing", "input-not-file", "input-unreadable", "input-too-large", "destination-catalog-unreadable", "destination-missing",
      "missing-secret", "credential-unavailable", "http-error", "parse-failed", "invalid-result-url", "timeout", "network-error", "response-too-large",
      "cancelled", "upload-failed", "watch-capture-directory-failed", "watch-copy-open-failed", "watch-copy-failed", "watch-file-changed",
      "watch-copy-commit-failed", "watch-move-remove-failed"};
  return known.contains(trimmed) ? trimmed : QStringLiteral("action-failed");
}
FileOperationTerminal fileOperationTerminal(const QString &executionId, const QString &operation, const QJsonArray &steps, const QString &diagnostic, int uploadAttempts) {
  bool committed = false, failed = false, cancelled = false, denied = false;
  std::optional<FailureClass> failure;
  const auto isUpload = operation == QLatin1String("upload");
  for (const auto &value : steps) {
    const auto step = value.toObject(); const auto state = step.value("state").toString(); const auto action = step.value("action").toString();
    if (state == "succeeded" && (action == "upload-file" || action == "copy-file")) committed = true;
    if (state == "failed") { failed = true; if (!failure) failure = xerahs::actions::parseFailureClass(step.value("failure_class").toString()); }
    cancelled |= state == "cancelled"; denied |= state == "permission-denied";
  }
  FileOperationTerminal result; result.diagnostic = diagnostic;
  // PCA-012, STATE_MACHINE: no acknowledgement after send is interrupted, not a
  // failure class. The missing upload step is the unknown action, not a success.
  const bool unacknowledgedUpload = isUpload && !failed && !committed && !cancelled && !denied && uploadRemoteOutcomeUnknown(diagnostic, uploadAttempts);
  if (unacknowledgedUpload) result.state = QStringLiteral("interrupted");
  else if (failed && committed) result.state = "partial";
  else if (failed) result.state = xerahs::actions::failureClassName(failure.value_or(FailureClass::PermanentFailure));
  else if (denied) result.state = "permission-denied";
  else if (cancelled) result.state = committed ? QStringLiteral("partial") : QStringLiteral("cancelled");
  else if (!committed) result.state = QStringLiteral("permanent-failure");
  else result.state = "succeeded";
  if (result.state == "succeeded") result.diagnostic.clear();
  else if (result.diagnostic.isEmpty()) result.diagnostic = QStringLiteral("action-failed");
  if (committed) result.outputs = {executionId + (isUpload ? "/upload.receipt" : "/watch.copy")};
  // An uploaded file is a single remote effect: it either committed or it did not.
  if (isUpload && result.state != "succeeded") result.outputs.clear();
  return result;
}
FileOperationNotice fileOperationNotice(const QString &operation, const QString &state, const QString &fileName) {
  FileOperationNotice notice; notice.state = state; notice.completed = state == "succeeded";
  const auto upload = operation == QLatin1String("upload");
  if (notice.completed) notice.headline = (upload ? QStringLiteral("Uploaded %1") : QStringLiteral("Processed %1")).arg(fileName);
  else if (state == "partial") notice.headline = QStringLiteral("%1 was only partly processed (partial)").arg(fileName);
  else if (state == "cancelled") notice.headline = QStringLiteral("%1 was cancelled").arg(fileName);
  else if (state == QLatin1String("interrupted") && upload)
    notice.headline = QStringLiteral("%1: remote publication is unconfirmed (interrupted)").arg(fileName);
  else if (state == QLatin1String("interrupted")) notice.headline = QStringLiteral("%1 is interrupted").arg(fileName);
  else notice.headline = QStringLiteral("%1 did not complete (%2)").arg(fileName, state);
  return notice;
}
FileOperationNotice fileOperationNotice(const WorkflowExecutionRecord &record, const QString &fileName) {
  return fileOperationNotice(record.settings.value("operation").toString(), record.state, fileName);
}
} // namespace xerahs::workflows
