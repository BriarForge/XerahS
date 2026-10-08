#include "actions/PostCapturePipeline.h"

#include <QSet>

#include <algorithm>

namespace xerahs::actions {

namespace {

const QString kWorkingMedia = QStringLiteral("working-media");
const QString kSavedFile = QStringLiteral("saved-file");
const QString kSaveFile = QStringLiteral("save.file");
const QString kSaveAsFile = QStringLiteral("save-as.file");
const QString kTemporaryFile = QStringLiteral("temporary.file");
const QString kUpload = QStringLiteral("upload");
const QString kDelete = QStringLiteral("delete");

ActionSpec spec(const char *id, QStringList legacyFlags, ActionKind kind, QStringList requirements,
                QStringList produces, bool interruptible) {
  return ActionSpec{QString::fromLatin1(id), std::move(legacyFlags), kind, std::move(requirements),
                    std::move(produces), interruptible};
}

// Outcome of resolving one action's required artifacts (PCA-018, PCA-019).
struct Resolution {
  std::optional<QString> inputArtifact;
  std::optional<QString> missingArtifact;
};

class ArtifactSet {
public:
  ArtifactSet() { m_available.insert(kWorkingMedia); }

  void add(const QStringList &artifacts) {
    for (const QString &artifact : artifacts) m_available.insert(artifact);
  }

  // PCA-018: `saved-file` resolves to save.file, then save-as.file.
  std::optional<QString> savedFile() const {
    if (m_available.contains(kSaveFile)) return kSaveFile;
    if (m_available.contains(kSaveAsFile)) return kSaveAsFile;
    return std::nullopt;
  }

  Resolution resolve(const ActionSpec &action, const PipelineOptions &options) const {
    Resolution resolution;
    for (const QString &requirement : action.requirements) {
      const QStringList alternatives = requirement.split(QLatin1Char('|'));
      std::optional<QString> resolved;
      for (const QString &alternative : alternatives) {
        if (alternative == kSavedFile) {
          resolved = savedFile();
        } else if (alternative == kTemporaryFile) {
          // PCA-019: the engine creates temporary.file on demand when permitted.
          if (options.uploadTemporaryAllowed) resolved = kTemporaryFile;
        } else if (m_available.contains(alternative)) {
          resolved = alternative;
        }
        if (resolved) break;
      }
      if (!resolved) {
        // PCA-018: canonical name of the first unavailable requirement; an
        // unresolved saved-file is reported as save.file.
        const QString &first = alternatives.first();
        resolution.missingArtifact = first == kSavedFile ? kSaveFile : first;
        return resolution;
      }
      if (*resolved == kSaveFile || *resolved == kSaveAsFile || *resolved == kTemporaryFile) {
        resolution.inputArtifact = resolved;
      }
    }
    return resolution;
  }

private:
  QSet<QString> m_available;
};

bool requiresSavedFile(const ActionSpec &action) {
  return std::any_of(action.requirements.begin(), action.requirements.end(),
                     [](const QString &requirement) {
                       return requirement.split(QLatin1Char('|')).contains(kSavedFile);
                     });
}

// PCA-002, PCA-017: selected IDs as catalogue indices in catalogue order.
std::vector<int> planFor(const QStringList &selected, int after, QStringList *ignored) {
  std::vector<int> plan;
  for (const QString &id : selected) {
    const std::optional<int> index = catalogueIndex(id);
    if (!index) {
      if (ignored && !ignored->contains(id)) ignored->append(id);
      continue;
    }
    // PCA-023: replacement entries at or before the selection window are ignored.
    if (*index <= after) continue;
    if (std::find(plan.begin(), plan.end(), *index) == plan.end()) plan.push_back(*index);
  }
  std::sort(plan.begin(), plan.end());
  return plan;
}

}  // namespace

const std::vector<ActionSpec> &catalogue() {
  using K = ActionKind;
  static const std::vector<ActionSpec> actions = {
      spec("quick-task-menu", {"ShowQuickTaskMenu"}, K::Selection, {"working-media"}, {}, true),
      spec("after-capture-window", {"ShowAfterCaptureWindow"}, K::Selection, {"working-media"}, {}, true),
      spec("image-effects", {"AddImageEffects"}, K::Transformation, {"working-media"}, {"working-media"}, true),
      spec("annotation", {"AnnotateMedia", "AnnotateImage"}, K::Transformation, {"working-media"},
           {"working-media", "annotation.document"}, true),
      spec("save", {"SaveImageToFile"}, K::Output, {"working-media"}, {"save.file"}, false),
      spec("save-as", {"SaveImageToFileWithDialog"}, K::Output, {"working-media"}, {"save-as.file"}, true),
      spec("save-thumbnail", {"SaveThumbnailImageToFile"}, K::Output, {"working-media"}, {"thumbnail.file"}, false),
      spec("image-clipboard", {"CopyImageToClipboard"}, K::Output, {"working-media"}, {}, false),
      spec("before-upload-window", {"ShowBeforeUploadWindow"}, K::Confirmation, {"working-media"}, {}, true),
      spec("upload", {"UploadImageToHost"}, K::Network, {"saved-file|temporary.file"}, {"upload.result"}, false),
      spec("ocr", {"DoOCR"}, K::Analysis, {"working-media"}, {"ocr.text"}, true),
      spec("ocr-clipboard", {"CopyOcrTextToClipboard"}, K::Output, {"ocr.text"}, {}, false),
      spec("qr-scan", {"ScanQRCode"}, K::Analysis, {"working-media"}, {"qr.text"}, true),
      spec("pin", {"PinToScreen"}, K::Presentation, {"working-media"}, {}, false),
      spec("print", {"SendImageToPrinter"}, K::Output, {"working-media"}, {}, true),
      spec("custom-actions", {"PerformActions"}, K::External, {"saved-file"}, {}, false),
      spec("file-clipboard", {"CopyFileToClipboard"}, K::Output, {"saved-file"}, {}, false),
      spec("path-clipboard", {"CopyFilePathToClipboard"}, K::Output, {"saved-file"}, {}, false),
      spec("reveal", {"ShowInExplorer"}, K::Presentation, {"saved-file"}, {}, false),
      spec("analyze", {"AnalyzeImage"}, K::Analysis, {"working-media"}, {}, true),
      spec("delete", {"DeleteFile"}, K::Destructive, {"saved-file"}, {}, false),
  };
  return actions;
}

std::optional<int> catalogueIndex(const QString &id) {
  const auto &actions = catalogue();
  for (std::size_t i = 0; i < actions.size(); ++i) {
    if (actions[i].id == id) return static_cast<int>(i);
  }
  return std::nullopt;
}

LegacyFlagMapping mapLegacyFlags(const QStringList &flags) {
  LegacyFlagMapping mapping;
  std::vector<int> indices;
  for (const QString &flag : flags) {
    bool known = false;
    const auto &actions = catalogue();
    for (std::size_t i = 0; i < actions.size(); ++i) {
      if (!actions[i].legacyFlags.contains(flag)) continue;
      known = true;
      const int index = static_cast<int>(i);
      if (std::find(indices.begin(), indices.end(), index) == indices.end()) indices.push_back(index);
    }
    // Pending clarification (ROOT-ESCALATE-001): the contract does not say how
    // an unrecognised flag imports. It is reported rather than dropped silently
    // so the importer can surface it.
    if (!known && !mapping.unknownFlags.contains(flag)) mapping.unknownFlags.append(flag);
  }
  std::sort(indices.begin(), indices.end());
  for (const int index : indices) mapping.selected.append(catalogue()[static_cast<std::size_t>(index)].id);
  return mapping;
}

QString actionStateName(ActionState state) {
  switch (state) {
    case ActionState::Succeeded: return QStringLiteral("succeeded");
    case ActionState::Failed: return QStringLiteral("failed");
    case ActionState::Cancelled: return QStringLiteral("cancelled");
    case ActionState::Skipped: return QStringLiteral("skipped");
  }
  return {};
}

QString pipelineStateName(PipelineState state) {
  switch (state) {
    case PipelineState::Completed: return QStringLiteral("completed");
    case PipelineState::Cancelled: return QStringLiteral("cancelled");
    case PipelineState::Interrupted: return QStringLiteral("interrupted");
  }
  return {};
}

PipelineResult runPipeline(const QString &taskId, const QStringList &selected,
                           const PipelineOptions &options, ActionExecutor &executor,
                           CancellationToken &cancellation, const PipelineHooks &hooks) {
  const auto now = [&hooks] { return hooks.clock ? hooks.clock() : QDateTime::currentDateTimeUtc(); };
  const auto &actions = catalogue();

  PipelineResult result;
  result.taskId = taskId;
  std::vector<int> plan = planFor(selected, -1, &result.ignoredSelection);
  ArtifactSet artifacts;
  bool uploadDeclined = false;
  bool cancellationPreventedStart = false;
  bool selectionCancelled = false;

  const auto record = [&](ActionResult actionResult) {
    result.orderedResults.push_back(std::move(actionResult));
    if (hooks.actionFinished) hooks.actionFinished(result.orderedResults.back());
  };

  for (std::size_t position = 0; position < plan.size(); ++position) {
    const int index = plan[position];
    const ActionSpec &action = actions[static_cast<std::size_t>(index)];
    ActionResult actionResult;
    actionResult.action = action.id;

    // PCA-010: once cancellation is requested no new action starts.
    if (cancellation.requested()) {
      for (std::size_t rest = position; rest < plan.size(); ++rest) {
        ActionResult cancelled;
        cancelled.action = actions[static_cast<std::size_t>(plan[rest])].id;
        cancelled.state = ActionState::Cancelled;
        result.orderedResults.push_back(std::move(cancelled));
      }
      cancellationPreventedStart = true;
      break;
    }

    // PCA-020: a declined before-upload window skips upload only.
    if (action.id == kUpload && uploadDeclined) {
      actionResult.state = ActionState::Skipped;
      actionResult.diagnostic = diagnostic::uploadDeclined;
      record(std::move(actionResult));
      continue;
    }

    // PCA-004, PCA-018, PCA-019: dependencies.
    const Resolution resolution = artifacts.resolve(action, options);
    if (resolution.missingArtifact) {
      actionResult.state = ActionState::Skipped;
      actionResult.diagnostic = diagnostic::dependencyUnavailable;
      actionResult.missingArtifact = resolution.missingArtifact;
      record(std::move(actionResult));
      continue;
    }
    actionResult.inputArtifact = resolution.inputArtifact;

    // PCA-021: delete only after every earlier saved-file consumer succeeded.
    // The only files delete can resolve are save.file and save-as.file, which
    // this pipeline wrote.
    if (action.id == kDelete) {
      const bool guarded = std::any_of(
          result.orderedResults.begin(), result.orderedResults.end(), [&](const ActionResult &earlier) {
            const std::optional<int> earlierIndex = catalogueIndex(earlier.action);
            return earlierIndex && requiresSavedFile(actions[static_cast<std::size_t>(*earlierIndex)]) &&
                   earlier.state != ActionState::Succeeded;
          });
      if (guarded) {
        actionResult.state = ActionState::Skipped;
        actionResult.diagnostic = diagnostic::deleteGuard;
        record(std::move(actionResult));
        continue;
      }
    }

    const bool usesTemporaryFile = resolution.inputArtifact == kTemporaryFile;
    actionResult.startedAt = now();
    if (usesTemporaryFile && !executor.createTemporaryUploadFile()) {
      actionResult.finishedAt = now();
      actionResult.state = ActionState::Failed;
      actionResult.diagnostic = diagnostic::temporaryFileUnavailable;
      record(std::move(actionResult));
      continue;
    }

    ActionOutcome outcome = executor.run(ActionRequest{action, resolution.inputArtifact, cancellation});
    actionResult.finishedAt = now();
    if (outcome.state == ActionState::Skipped) {
      // Only the engine skips; an executor that claims a skip did not do the work.
      outcome.state = ActionState::Failed;
    }
    actionResult.state = outcome.state;
    actionResult.diagnostic = outcome.diagnostic;
    if (outcome.state == ActionState::Failed && !actionResult.diagnostic) {
      actionResult.diagnostic = diagnostic::actionFailed;
    }
    // PCA-010: an action that finished despite cancellation says so.
    actionResult.cancellationArrivedDuring =
        cancellation.requested() && outcome.state != ActionState::Cancelled;

    if (usesTemporaryFile && options.removeTemporaryFileAfterUpload) executor.removeTemporaryUploadFile();

    if (outcome.state == ActionState::Succeeded) {
      artifacts.add(action.produces);
      // PCA-003, PCA-022: the last successful transformation is the working media.
      if (action.kind == ActionKind::Transformation) result.finalWorkingMedia = action.id;
    }

    switch (action.kind) {
      case ActionKind::Selection:
        if (outcome.state == ActionState::Cancelled) {
          // PCA-009: cancelling a selection window cancels the pipeline.
          selectionCancelled = true;
          cancellation.request();
        } else if (outcome.state == ActionState::Succeeded && outcome.replacementSelection) {
          // PCA-023: replace the rest of the plan for this run only.
          std::vector<int> replacement = planFor(*outcome.replacementSelection, index, &result.ignoredSelection);
          plan.resize(position + 1);
          plan.insert(plan.end(), replacement.begin(), replacement.end());
        }
        // Pending clarification (ROOT-ESCALATE-001): the contract does not say
        // what a failed selection window does. It is treated as an independent
        // failure (PCA-004) and the configured action set runs unchanged.
        break;
      case ActionKind::Confirmation:
        // PCA-020. Pending clarification (ROOT-ESCALATE-001): a failed
        // before-upload window is treated like a declined one, so nothing is
        // uploaded without the confirmation the user asked for.
        if (outcome.state != ActionState::Succeeded) uploadDeclined = true;
        break;
      default:
        // Pending clarification (ROOT-ESCALATE-001): an interactive action other
        // than a selection window that the user cancels (for example the
        // save-as dialog or the annotation editor) cancels only that action;
        // dependants are skipped and independent actions continue.
        break;
    }

    record(std::move(actionResult));
  }

  // PCA-022. Pending clarification (ROOT-ESCALATE-001): PCA-022 alone would
  // call a run "completed" when a cancelled selection window was the last
  // selected action, but PCA-009 says cancelling it cancels the pipeline; the
  // engine follows PCA-009.
  result.state = (cancellationPreventedStart || selectionCancelled) ? PipelineState::Cancelled
                                                                    : PipelineState::Completed;
  return result;
}

}  // namespace xerahs::actions
