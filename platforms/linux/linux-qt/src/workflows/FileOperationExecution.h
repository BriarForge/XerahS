// WFCAT-002, WFCAT-004, WFCAT-017: file-upload and watch-folder operations share
// the WorkflowExecution contract. Qt Core only; no I/O.
#pragma once
#include "WorkflowExecution.h"
#include <optional>
namespace xerahs::workflows {
// Redacted effective-settings snapshot. operation: upload, copy or move. The
// destination (id/name) and rule (action, recursive, filters) are optional.
// Paths, URLs and credentials never enter it.
QJsonObject fileOperationSnapshot(const QString &operation, const QString &destinationId = {}, const QString &destinationName = {},
    const std::optional<QJsonObject> &rule = std::nullopt);
// WFCAT-004: failure class of a stable file-operation or upload diagnostic code.
xerahs::actions::FailureClass classifyFileOperationFailure(const QString &code, int httpStatus = 0);
// Stable diagnostic code which may be stored (unknown codes become action-failed).
QString fileOperationDiagnostic(const QString &code);
struct FileOperationTerminal {
  QString state;        // shared taxonomy: succeeded, partial, cancelled, permission-denied, ...
  QString diagnostic;   // stable code, empty on success
  QStringList outputs;  // opaque task-scoped references
};
// Terminal state from the ordered step facts. A step must be recorded as
// succeeded only after its durable effect committed. A move whose copy committed
// but whose source removal failed is partial and keeps the copy reference.
FileOperationTerminal fileOperationTerminal(const QString &executionId, const QString &operation, const QJsonArray &steps, const QString &diagnostic);
// Presentation shared by dialogs, notifications and history. Only a succeeded
// execution reads as completed; partial and failed never do.
struct FileOperationNotice {
  QString state, headline;
  bool completed = false;
};
FileOperationNotice fileOperationNotice(const QString &operation, const QString &state, const QString &fileName);
FileOperationNotice fileOperationNotice(const WorkflowExecutionRecord &record, const QString &fileName);
} // namespace xerahs::workflows
