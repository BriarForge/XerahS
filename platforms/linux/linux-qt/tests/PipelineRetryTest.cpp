#include "actions/PipelineRetry.h"
#include "actions/PipelineJournal.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QUuid>
#include <limits>
using namespace xerahs::actions;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
PipelineResult result(ActionState save, ActionState clipboard) {
  PipelineResult value; value.taskId = id();
  value.orderedResults = {ActionResult{"save", save}, ActionResult{"image-clipboard", clipboard}}; return value;
}
}
class PipelineRetryTest final : public QObject {
  Q_OBJECT
private slots:
  void saveAsArtifactsSatisfyConsumersAndDoNotAuthorizeSuccessfulSaveReplay() {
    PipelineResult source; source.taskId = id(); source.orderedResults = {ActionResult{"save-as", ActionState::Succeeded}, ActionResult{"path-clipboard", ActionState::Failed}};
    const auto plan = planRetainedCaptureRetry(source, true, false, true); QVERIFY(plan.available()); QCOMPARE(plan.selected, QStringList{"path-clipboard"});
    QCOMPARE(plan.link->preservedSuccessfulActions, QStringList{"save-as"});
    QCOMPARE(planRetainedCaptureRetry(source, true, false, false).diagnostic, QString("retry-artifact-unavailable"));
    source.orderedResults[0].state = ActionState::Failed; source.orderedResults[1].state = ActionState::Skipped;
    source.orderedResults[1].diagnostic = diagnostic::dependencyUnavailable;
    QCOMPARE(planRetainedCaptureRetry(source, true, false, false).selected, (QStringList{"save-as", "path-clipboard"}));
  }
  void fileConsumersReuseKnownSaveOrRetryOnlyItsOriginallyFailedPrerequisite() {
    auto source = result(ActionState::Succeeded, ActionState::Succeeded);
    source.orderedResults.push_back(ActionResult{"file-clipboard", ActionState::Succeeded});
    source.orderedResults.push_back(ActionResult{"path-clipboard", ActionState::Failed});
    source.orderedResults.push_back(ActionResult{"reveal", ActionState::Failed});
    auto plan = planRetainedCaptureRetry(source, true, true); QVERIFY(plan.available());
    QCOMPARE(plan.selected, (QStringList{"path-clipboard", "reveal"}));
    QCOMPARE(plan.link->preservedSuccessfulActions, (QStringList{"save", "image-clipboard", "file-clipboard"}));
    source.orderedResults[0].state = ActionState::Failed;
    source.orderedResults[2].state = ActionState::Skipped; source.orderedResults[2].diagnostic = diagnostic::dependencyUnavailable;
    plan = planRetainedCaptureRetry(source, true, false); QVERIFY(plan.available());
    QCOMPARE(plan.selected, (QStringList{"save", "file-clipboard", "path-clipboard", "reveal"}));
    source.orderedResults.erase(source.orderedResults.begin());
    QCOMPARE(planRetainedCaptureRetry(source, true, false).diagnostic, QString("retry-prerequisite-unavailable"));
    source = result(ActionState::Succeeded, ActionState::Succeeded);
    source.orderedResults.push_back(ActionResult{"reveal", ActionState::Failed}); source.orderedResults.back().diagnostic = "reveal-outcome-unknown";
    QCOMPARE(planRetainedCaptureRetry(source, true, true).diagnostic, QString("retry-outcome-unknown"));
  }
  void retriesOnlyFailedOrDependencySkippedNativeActionsInCatalogueOrder() {
    const auto failedSave = result(ActionState::Failed, ActionState::Succeeded);
    const auto savePlan = planRetainedCaptureRetry(failedSave, true, false); QVERIFY(savePlan.available());
    QCOMPARE(savePlan.selected, QStringList{"save"}); QCOMPARE(savePlan.link->preservedSuccessfulActions, QStringList{"image-clipboard"});
    QCOMPARE(savePlan.link->originalTaskId, failedSave.taskId); QCOMPARE(savePlan.link->previousTaskId, failedSave.taskId); QCOMPARE(savePlan.link->attempt, 1);
    const auto failedCopy = result(ActionState::Succeeded, ActionState::Failed);
    const auto copyPlan = planRetainedCaptureRetry(failedCopy, true, true); QVERIFY(copyPlan.available());
    QCOMPARE(copyPlan.selected, QStringList{"image-clipboard"}); QCOMPARE(copyPlan.link->preservedSuccessfulActions, QStringList{"save"});
    auto skipped = result(ActionState::Failed, ActionState::Skipped); skipped.orderedResults[1].diagnostic = diagnostic::dependencyUnavailable;
    QCOMPARE(planRetainedCaptureRetry(skipped, true, false).selected, (QStringList{"save", "image-clipboard"}));
    skipped.orderedResults[1].diagnostic = diagnostic::uploadDeclined;
    QCOMPARE(planRetainedCaptureRetry(skipped, true, false).selected, QStringList{"save"});
  }
  void interruptedUnknownCancelledSuccessfulAndUnsupportedActionsDoNotRunAgain() {
    auto value = result(ActionState::Succeeded, ActionState::Failed); value.state = PipelineState::Interrupted;
    QCOMPARE(planRetainedCaptureRetry(value, true, true).diagnostic, QString("retry-outcome-unknown"));
    value.state = PipelineState::Completed; QCOMPARE(planRetainedCaptureRetry(value, false, true).diagnostic, QString("retry-media-unavailable"));
    value.workingMediaRetained = false; QVERIFY(!planRetainedCaptureRetry(value, true, true).available());
    value = result(ActionState::Succeeded, ActionState::Succeeded); QCOMPARE(planRetainedCaptureRetry(value, true, true).diagnostic, QString("retry-no-failed-actions"));
    value.orderedResults[1].state = ActionState::Cancelled; QVERIFY(!planRetainedCaptureRetry(value, true, true).available());
    value = result(ActionState::Failed, ActionState::Failed); value.orderedResults.push_back(ActionResult{"upload", ActionState::Failed});
    QCOMPARE(planRetainedCaptureRetry(value, true, false).diagnostic, QString("retry-action-unavailable"));
  }
  void invalidSourceOrConflictingArtifactAndSuccessAuthorityFailsClosed() {
    auto value = result(ActionState::Failed, ActionState::Failed);
    QCOMPARE(planRetainedCaptureRetry(value, true, true).diagnostic, QString("retry-invalid-source"));
    value = result(ActionState::Succeeded, ActionState::Failed);
    QCOMPARE(planRetainedCaptureRetry(value, true, false).diagnostic, QString("retry-artifact-unavailable"));
    value.orderedResults.push_back(value.orderedResults.back()); QVERIFY(!planRetainedCaptureRetry(value, true, true).available());
    value = result(ActionState::Failed, ActionState::Failed); value.taskId = "../not-authority"; QVERIFY(!planRetainedCaptureRetry(value, true, false).available());
  }
  void repeatedFailureKeepsTheRootAndLinksTheImmediatePrecedingAttempt() {
    const auto root = result(ActionState::Succeeded, ActionState::Failed); const auto first = planRetainedCaptureRetry(root, true, true);
    PipelineResult again; again.taskId = id(); again.retry = first.link; again.orderedResults = {ActionResult{"image-clipboard", ActionState::Failed}};
    const auto second = planRetainedCaptureRetry(again, true, true); QVERIFY(second.available());
    QCOMPARE(second.link->originalTaskId, root.taskId); QCOMPARE(second.link->previousTaskId, again.taskId); QCOMPARE(second.link->attempt, 2);
    QCOMPARE(second.link->preservedSuccessfulActions, QStringList{"save"}); QCOMPARE(second.selected, QStringList{"image-clipboard"});
    again.retry->previousTaskId = id(); again.retry->attempt = std::numeric_limits<int>::max();
    QCOMPARE(planRetainedCaptureRetry(again, true, true).diagnostic, QString("retry-limit-reached"));
  }
  void canonicalAndJournalLinksAreNamespacedTypedAndNeverRetryAuthority() {
    const auto root = result(ActionState::Succeeded, ActionState::Failed); const auto plan = planRetainedCaptureRetry(root, true, true);
    PipelineResult retry; retry.taskId = id(); retry.retry = plan.link; retry.orderedResults = {ActionResult{"image-clipboard", ActionState::Succeeded}};
    SavedFileArtifact file{"/fixture/old-save.png", "image/png", 123, {}, "save.file", root.taskId};
    const auto report = pipelineResultJson(retry, {file}); QVERIFY(!report.contains("retry"));
    const auto native = report.value("extensions").toObject().value("com.xerahs.app").toObject();
    QCOMPARE(native.value("retry").toObject().value("original_task_id"), QJsonValue(root.taskId));
    QCOMPARE(native.value("file_artifacts").toArray().first().toObject().value("creator_task_id"), QJsonValue(root.taskId));
    PipelineJournalSnapshot snapshot; snapshot.result = retry; snapshot.selected = plan.selected; snapshot.createdAt = QDateTime::currentDateTimeUtc();
    snapshot.files = {file}; snapshot.terminal = true;
    const auto encoded = serializePipelineJournal(snapshot); QVERIFY(parsePipelineJournal(encoded, retry.taskId).record);
    auto bad = retry; bad.retry->originalTaskId = retry.taskId; snapshot.result = bad;
    QVERIFY(!parsePipelineJournal(serializePipelineJournal(snapshot), retry.taskId).record);
    snapshot.result = retry; snapshot.result.retry->preservedSuccessfulActions.append("image-clipboard");
    QVERIFY(!parsePipelineJournal(serializePipelineJournal(snapshot), retry.taskId).record);
  }
};
QTEST_GUILESS_MAIN(PipelineRetryTest)
#include "PipelineRetryTest.moc"
