// Production-path checks that the conformance vectors cannot cover: the
// compiled catalogue against actions.json, temporary upload files, executor
// misuse, unknown legacy flags, and per-run isolation.

#include "actions/PostCapturePipeline.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <map>

using namespace xerahs::actions;

namespace {

class ScriptedExecutor final : public ActionExecutor {
public:
  std::map<QString, ActionState> states;
  bool temporaryFileCreatable = true;
  int temporaryFilesCreated = 0;
  int temporaryFilesRemoved = 0;
  QStringList ran;

  ActionOutcome run(const ActionRequest &request) override {
    ran.append(request.spec.id);
    const auto it = states.find(request.spec.id);
    return ActionOutcome{it == states.end() ? ActionState::Succeeded : it->second, std::nullopt, std::nullopt};
  }
  bool createTemporaryUploadFile() override {
    if (!temporaryFileCreatable) return false;
    ++temporaryFilesCreated;
    return true;
  }
  void removeTemporaryUploadFile() override { ++temporaryFilesRemoved; }
};

QString kindName(ActionKind kind) {
  switch (kind) {
    case ActionKind::Selection: return QStringLiteral("selection");
    case ActionKind::Transformation: return QStringLiteral("transformation");
    case ActionKind::Output: return QStringLiteral("output");
    case ActionKind::Confirmation: return QStringLiteral("confirmation");
    case ActionKind::Network: return QStringLiteral("network");
    case ActionKind::Analysis: return QStringLiteral("analysis");
    case ActionKind::Presentation: return QStringLiteral("presentation");
    case ActionKind::External: return QStringLiteral("external");
    case ActionKind::Destructive: return QStringLiteral("destructive");
  }
  return {};
}

QStringList strings(const QJsonValue &value) {
  QStringList list;
  for (const QJsonValue &item : value.toArray()) list.append(item.toString());
  return list;
}

}  // namespace

class PostCapturePipelineTest : public QObject {
  Q_OBJECT

private slots:
  // PCA-017: the compiled catalogue is the contract's catalogue.
  void catalogueMatchesContract() {
    QFile file(QStringLiteral(XERAHS_ACTIONS_CATALOGUE));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonArray contract = QJsonDocument::fromJson(file.readAll()).object().value(u"actions").toArray();
    QCOMPARE(static_cast<std::size_t>(contract.size()), catalogue().size());
    for (int i = 0; i < contract.size(); ++i) {
      const QJsonObject expected = contract.at(i).toObject();
      const ActionSpec &actual = catalogue()[static_cast<std::size_t>(i)];
      QCOMPARE(actual.id, expected.value(u"id").toString());
      QCOMPARE(actual.legacyFlags, strings(expected.value(u"legacy_flags")));
      QCOMPARE(kindName(actual.kind), expected.value(u"kind").toString());
      QCOMPARE(actual.requirements, strings(expected.value(u"requires")));
      QCOMPARE(actual.produces, strings(expected.value(u"produces")));
      QCOMPARE(actual.interruptible, expected.value(u"interruptible").toBool());
    }
  }

  // PCA-019: the managed temporary file is created and cleaned up.
  void temporaryUploadFileIsCleanedUp() {
    ScriptedExecutor executor;
    CancellationToken token;
    const PipelineResult result = runPipeline(QStringLiteral("t"), {QStringLiteral("upload")}, {}, executor, token);
    QCOMPARE(result.orderedResults.size(), std::size_t{1});
    QCOMPARE(result.orderedResults[0].inputArtifact, std::optional<QString>(QStringLiteral("temporary.file")));
    QCOMPARE(executor.temporaryFilesCreated, 1);
    QCOMPARE(executor.temporaryFilesRemoved, 1);
  }

  void temporaryUploadFileFailureFailsUploadOnly() {
    ScriptedExecutor executor;
    executor.temporaryFileCreatable = false;
    CancellationToken token;
    const PipelineResult result = runPipeline(
        QStringLiteral("t"), {QStringLiteral("upload"), QStringLiteral("pin")}, {}, executor, token);
    QCOMPARE(result.orderedResults[0].state, ActionState::Failed);
    QCOMPARE(result.orderedResults[0].diagnostic, std::optional<QString>(diagnostic::temporaryFileUnavailable));
    QCOMPARE(result.orderedResults[1].state, ActionState::Succeeded);
    QCOMPARE(executor.ran, QStringList{QStringLiteral("pin")});
  }

  // Only the engine decides a skip.
  void executorSkipBecomesFailure() {
    ScriptedExecutor executor;
    executor.states[QStringLiteral("print")] = ActionState::Skipped;
    CancellationToken token;
    const PipelineResult result = runPipeline(QStringLiteral("t"), {QStringLiteral("print")}, {}, executor, token);
    QCOMPARE(result.orderedResults[0].state, ActionState::Failed);
    QCOMPARE(result.orderedResults[0].diagnostic, std::optional<QString>(diagnostic::actionFailed));
  }

  // PCA-009: a cancelled selection window cancels the pipeline even when no
  // other action was selected.
  void loneSelectionCancelCancelsPipeline() {
    ScriptedExecutor executor;
    executor.states[QStringLiteral("quick-task-menu")] = ActionState::Cancelled;
    CancellationToken token;
    const PipelineResult result =
        runPipeline(QStringLiteral("t"), {QStringLiteral("quick-task-menu")}, {}, executor, token);
    QCOMPARE(result.state, PipelineState::Cancelled);
  }

  void unknownSelectionIsReportedAndNeverRuns() {
    ScriptedExecutor executor;
    CancellationToken token;
    const PipelineResult result = runPipeline(
        QStringLiteral("t"), {QStringLiteral("teleport"), QStringLiteral("pin")}, {}, executor, token);
    QCOMPARE(result.ignoredSelection, QStringList{QStringLiteral("teleport")});
    QCOMPARE(executor.ran, QStringList{QStringLiteral("pin")});
  }

  void unknownLegacyFlagIsReported() {
    const LegacyFlagMapping mapping = mapLegacyFlags({QStringLiteral("DoOCR"), QStringLiteral("Teleport")});
    QCOMPARE(mapping.selected, QStringList{QStringLiteral("ocr")});
    QCOMPARE(mapping.unknownFlags, QStringList{QStringLiteral("Teleport")});
  }

  // PCA-015: cancelling one run leaves another untouched.
  void pipelinesAreIsolated() {
    ScriptedExecutor first;
    ScriptedExecutor second;
    CancellationToken cancelled;
    CancellationToken live;
    cancelled.request();
    const PipelineResult a = runPipeline(QStringLiteral("a"), {QStringLiteral("save")}, {}, first, cancelled);
    const PipelineResult b = runPipeline(QStringLiteral("b"), {QStringLiteral("save")}, {}, second, live);
    QCOMPARE(a.state, PipelineState::Cancelled);
    QCOMPARE(b.state, PipelineState::Completed);
    QCOMPARE(a.taskId, QStringLiteral("a"));
    QCOMPARE(b.orderedResults[0].state, ActionState::Succeeded);
  }
};

QTEST_GUILESS_MAIN(PostCapturePipelineTest)
#include "PostCapturePipelineTest.moc"
