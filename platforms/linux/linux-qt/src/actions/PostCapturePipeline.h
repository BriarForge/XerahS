// POST-CAPTURE-ACTIONS-001: ordered post-capture action engine. Normative
// behaviour lives in product-contract/capabilities/POST-CAPTURE-ACTIONS-001/
// (SPEC.md, STATE_MACHINE.md, actions.json); requirement IDs in comments refer
// to those files.
//
// The engine orchestrates; it does not perform actions. An ActionExecutor owns
// the working media and does each action's own work, so one engine serves
// configured workflows, quick actions, and conformance alike (PCA-014).
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QJsonObject>

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace xerahs::actions {

enum class ActionKind {
  Selection,
  Transformation,
  Output,
  Confirmation,
  Network,
  Analysis,
  Presentation,
  External,
  Destructive,
};

// PCA-017: one entry of the normative action catalogue.
struct ActionSpec {
  QString id;
  QStringList legacyFlags;
  ActionKind kind;
  // Each entry is an artifact name, the `saved-file` alias, or alternatives
  // joined by '|', exactly as in actions.json.
  QStringList requirements;
  QStringList produces;
  bool interruptible;
};

// PCA-017: catalogue in execution order.
const std::vector<ActionSpec> &catalogue();
// Catalogue index of an action ID, or nullopt when the ID is unknown.
std::optional<int> catalogueIndex(const QString &id);

// PCA-023: legacy AfterCaptureTasks flags to action IDs in catalogue order.
struct LegacyFlagMapping {
  QStringList selected;
  QStringList unknownFlags;
};
LegacyFlagMapping mapLegacyFlags(const QStringList &flags);

enum class ActionState { Succeeded, Failed, Cancelled, Skipped };
QString actionStateName(ActionState state);

enum class PipelineState { Completed, Cancelled, Interrupted };
QString pipelineStateName(PipelineState state);

// Stable diagnostic codes (PCA-013). Never carry secrets, clipboard contents,
// recognized text, or credentials.
namespace diagnostic {
inline const QString dependencyUnavailable = QStringLiteral("dependency-unavailable");
inline const QString uploadDeclined = QStringLiteral("upload-declined");
inline const QString deleteGuard = QStringLiteral("delete-guard");
// Pending clarification (ROOT-ESCALATE-001): the contract does not name codes
// for a generic action failure or for a temporary upload file that could not be
// created. These are the engine's defaults until it does.
inline const QString actionFailed = QStringLiteral("action-failed");
inline const QString temporaryFileUnavailable = QStringLiteral("temporary-file-unavailable");
}  // namespace diagnostic

// Cooperative cancellation shared by the caller, the engine, and the executor
// of one pipeline (PCA-010, PCA-015).
class CancellationToken {
public:
  void request() { m_requested.store(true); }
  bool requested() const { return m_requested.load(); }

private:
  std::atomic<bool> m_requested{false};
};

struct ActionRequest {
  const ActionSpec &spec;
  // Resolved `saved-file` or `temporary.file` artifact this action consumes.
  std::optional<QString> inputArtifact;
  const CancellationToken &cancellation;
};

// ES-011/012: a paired save has two independently acknowledged outcomes.
// These facts never grant restart authority. `absent` means the empty-document
// sidecar policy completed without a companion file (ES-014).
struct ArtifactOutcome {
  QString artifact;
  ActionState state = ActionState::Skipped;
  std::optional<QString> diagnostic;
  bool absent = false;
};
bool validSavedOutputOutcomes(const QString &action, ActionState state, const std::vector<ArtifactOutcome> &outcomes);
bool hasAcknowledgedPartialRaster(const QString &action, ActionState state, const std::vector<ArtifactOutcome> &outcomes);

// CORE-020, CORE-021: public upload receipts; credentials never cross this boundary.
struct UploadResult {
  QString url, thumbnailUrl, deletionUrl, errorMessage;
};

struct ActionOutcome {
  // Succeeded, Failed, or Cancelled; the engine alone decides Skipped.
  ActionState state = ActionState::Succeeded;
  std::optional<QString> diagnostic;
  // PCA-009, PCA-023: a successful selection window MAY replace the action set
  // for this run.
  std::optional<QStringList> replacementSelection;
  // No acknowledgement can determine whether an external effect occurred.
  // Stop as interrupted and keep its write-ahead intent pending; inventing a
  // failed/cancelled action result would authorize an unsafe repeat.
  bool outcomeUnknown = false;
  std::vector<ArtifactOutcome> artifacts = {};
  std::optional<UploadResult> upload = {};
};

class ActionExecutor {
public:
  virtual ~ActionExecutor() = default;
  // Performs one action on the current working media. A successful
  // transformation replaces the working media; a failed one MUST leave the
  // last valid working media untouched (PCA-003).
  virtual ActionOutcome run(const ActionRequest &request) = 0;
  // PCA-019: encode the working media to a managed temporary upload file.
  virtual bool createTemporaryUploadFile() = 0;
  // PCA-019: delete the managed temporary file under the cleanup policy.
  virtual void removeTemporaryUploadFile() = 0;
};

struct ActionResult {
  QString action;
  ActionState state = ActionState::Skipped;
  QDateTime startedAt;
  QDateTime finishedAt;
  std::optional<QString> diagnostic;
  std::optional<QString> missingArtifact;
  std::optional<QString> inputArtifact;
  bool cancellationArrivedDuring = false;
  std::vector<ArtifactOutcome> artifacts = {};
  std::optional<UploadResult> upload = {};
};

// Settings snapshot taken before execution (SPEC "Definitions and model").
struct PipelineOptions {
  bool uploadTemporaryAllowed = true;            // PCA-019 default
  bool removeTemporaryFileAfterUpload = true;    // PCA-019 cleanup policy
  // A caller-owned previous save, supplied only after explicit linked retry
  // admission. The executor still validates its native file at consumption.
  // This grants prerequisite availability, never authority to delete it.
  bool inheritedSaveAvailable = false;
  bool inheritedSaveAsAvailable = false;
  QString inheritedWorkingMedia = QStringLiteral("capture");
};

// PCA-011: append-only attempt identity. Preserved successes are provenance,
// never invented results or authority to perform an external action again.
struct PipelineRetryLink {
  QString originalTaskId;
  QString previousTaskId;
  int attempt = 1;
  QStringList preservedSuccessfulActions;
  QStringList preservedRasterActions = {};
};

struct PipelineResult {
  QString taskId;
  PipelineState state = PipelineState::Completed;
  QString finalWorkingMedia = QStringLiteral("capture");
  std::vector<ActionResult> orderedResults;
  bool workingMediaRetained = true;
  QStringList ignoredSelection;  // unknown action IDs that never ran
  std::optional<PipelineRetryLink> retry;
  QJsonObject workflowSnapshot;
  QString workflowExecutionId = {}; // optional admitted invocation, not task identity
  std::optional<UploadResult> upload = {};
};

struct PipelineHooks {
  // Called after each action result is recorded and before the next action is
  // considered, so a caller can request cancellation between actions.
  std::function<void(const ActionResult &)> actionFinished;
  std::function<QDateTime()> clock;
  // Effective full plan after a successful per-run replacement, before its
  // selector result is recorded. Journals update their plan in that same
  // observation, before any newly selected action can begin.
  std::function<void(const QStringList &)> selectionChanged;
};

// Runs one pipeline. Every call is isolated: no state is shared between runs
// beyond what the caller passes in (PCA-015).
PipelineResult runPipeline(const QString &taskId, const QStringList &selected,
                           const PipelineOptions &options, ActionExecutor &executor,
                           CancellationToken &cancellation, const PipelineHooks &hooks = {});

}  // namespace xerahs::actions
