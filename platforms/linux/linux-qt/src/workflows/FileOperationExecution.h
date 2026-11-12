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
// A recorded failure only. An unacknowledged upload is not a failure class.
xerahs::actions::FailureClass classifyFileOperationFailure(const QString &code, int httpStatus = 0);
// PCA-012: timeout or network-error after at least one request was sent. The
// remote publication is unknown. Attempts of zero never left this machine.
bool uploadRemoteOutcomeUnknown(const QString &code, int attempts);
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
// An upload that was sent and not acknowledged has no failed step: its state is
// interrupted, with no receipt. A recorded failed step keeps its failure class.
// uploadAttempts is zero when no upload request has been started.
FileOperationTerminal fileOperationTerminal(const QString &executionId, const QString &operation, const QJsonArray &steps, const QString &diagnostic, int uploadAttempts);
// Presentation shared by dialogs, notifications and history. Only a succeeded
// execution reads as completed; partial and failed never do.
struct FileOperationNotice {
  QString state, headline;
  bool completed = false;
};
FileOperationNotice fileOperationNotice(const QString &operation, const QString &state, const QString &fileName);
FileOperationNotice fileOperationNotice(const WorkflowExecutionRecord &record, const QString &fileName);
} // namespace xerahs::workflows
