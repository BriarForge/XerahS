#include "workflows/WorkflowExecution.h"
#include "workflows/FileOperationExecution.h"
#include "NativeWorkflowExecutions.h"
#include "shell/ActivationDispatcher.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::workflows;
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::shell;
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
CaptureWorkflow workflow(const QString &directory) {
  auto w = defaultCaptureWorkflows().entries[0]; w.name = "Private fixture name"; w.actions = {"save"};
  w.output.directory = directory; w.output.pattern = "private-fixture"; w.extra.insert("secret", "private-extension"); w.output.extra.insert("secret", "private-output-extension"); return w;
}
QByteArray read(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
void write(const QString &path, const QByteArray &data) { QFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) qFatal("Fixture write failed"); }
}
class WorkflowExecutionTest final : public QObject {
  Q_OBJECT
private slots:
  void outputSettingsSnapshotsRemainReadableIndependentOfCurrentAvailability_data() {
    QTest::addColumn<QString>("action");
    for (const auto *id : {"upload", "url-clipboard", "pin", "print", "future-native-action"}) QTest::newRow(id) << QString::fromLatin1(id);
  }
  void outputSettingsSnapshotsRemainReadableIndependentOfCurrentAvailability() {
    QFETCH(QString, action); QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); auto w = workflow(dir.filePath("output")); w.actions = QStringList{action};
    const auto id = uuid(); WorkflowExecution execution(id, "native-shell", w, store.writer()); QVERIFY(execution.admit()); QVERIFY(execution.start()); QVERIFY(execution.finish("cancelled"));
    const auto result = store.result(id); QVERIFY(result.value("ok").toBool()); QCOMPARE(result.value("state"), QJsonValue("cancelled"));
    // A readable observation is not execution authority for an unknown action.
    if (action == "future-native-action") QCOMPARE(workflowAvailability(w, implementedCaptureActions()), QString("unsupported-action"));
    QVERIFY(!result.contains("settings"));
    const auto path = dir.filePath("records/" + id + ".json"); const auto before = read(path);
    NativeWorkflowExecutions reopened(dir.filePath("records")); QCOMPARE(reopened.result(id), result); QCOMPARE(read(path), before);
    QVERIFY(parseWorkflowExecution(before, id));
  }

  void durableAdmissionAbandonmentAndUnconfirmedInFlightDestruction() {
    QTemporaryDir dir; const auto folder = dir.filePath("records"); NativeWorkflowExecutions store(folder); auto w = workflow(dir.filePath("private-output")); const auto id = uuid();
    QCOMPARE(store.result(id).value("error").toObject().value("code"), QJsonValue("workflow-execution-not-found")); QVERIFY(!QFile::exists(folder));
    {
      WorkflowExecution execution(id, "native-shell", w, store.writer()); QVERIFY(execution.admit());
      const auto record = read(folder + "/" + id + ".json"); QVERIFY(!record.contains("private-extension")); QVERIFY(!record.contains("private-output-extension"));
      QVERIFY(record.contains("Private fixture name")); QVERIFY(record.contains("private-output"));
      w.name = "Changed"; QCOMPARE(execution.record().settings.value("name"), QJsonValue("Private fixture name"));
      auto query = store.result(id); QCOMPARE(query.value("state"), QJsonValue("outcome-unconfirmed")); QCOMPARE(query.value("lastRecordedState"), QJsonValue("admitted"));
      QVERIFY(!query.value("terminal").toBool()); QVERIFY(!QJsonDocument(query).toJson().contains("private"));
      WorkflowExecution duplicate(id, "command-line", w, store.writer()); QVERIFY(!duplicate.admit()); QCOMPARE(read(folder + "/" + id + ".json"), record);
    }
    QCOMPARE(store.result(id).value("state"), QJsonValue("not-started")); QVERIFY(store.result(id).value("terminal").toBool());
    const auto running = uuid(); {
      WorkflowExecution execution(running, "external-activation", w, store.writer()); QVERIFY(execution.admit()); QVERIFY(execution.start());
    }
    const auto query = NativeWorkflowExecutions(folder).result(running); QCOMPARE(query.value("state"), QJsonValue("outcome-unconfirmed")); QCOMPARE(query.value("lastRecordedState"), QJsonValue("capturing")); QVERIFY(!query.contains("finishedAt"));
  }
  void actualPipelineStatesAndRedactedOrderedSteps_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"success", "partial", "cancel", "interrupt", "denied", "source"}) QTest::newRow(mode) << QString::fromLatin1(mode);
  }
  void actualPipelineStatesAndRedactedOrderedSteps() {
    QFETCH(QString, mode); QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = uuid();
    WorkflowExecution execution(id, "global-shortcut", workflow(dir.filePath("private-output")), store.writer()); QVERIFY(execution.admit()); QVERIFY(execution.start());
    QString expected;
    if (mode == "denied" || mode == "source") { expected = mode == "denied" ? "permission-denied" : "failed"; QVERIFY(execution.finish(expected, mode == "denied" ? "capture-permission-denied" : "capture-source-failed")); }
    else {
      const auto pipelineId = uuid(); QVERIFY(execution.processing(pipelineId));
      QCOMPARE(store.result(id).value("pipelineTaskId"), QJsonValue(pipelineId)); QCOMPARE(store.result(id).value("state"), QJsonValue("outcome-unconfirmed"));
      PipelineResult result; result.taskId = pipelineId;
      result.state = mode == "cancel" ? PipelineState::Cancelled : mode == "interrupt" ? PipelineState::Interrupted : PipelineState::Completed;
      ActionResult step; step.action = "save"; step.state = mode == "partial" ? ActionState::Failed : ActionState::Succeeded; step.diagnostic = "private-exception-text"; result.orderedResults.push_back(step);
      if (mode == "partial") {
        result.orderedResults[0].state = ActionState::Succeeded;
        ActionResult upload; upload.action = "upload"; upload.state = ActionState::Failed; upload.failureClass = FailureClass::PermanentFailure;
        result.orderedResults.push_back(upload);
      }
      QVERIFY(execution.complete(result)); expected = mode == "partial" ? "partial" : mode == "cancel" ? "cancelled" : mode == "interrupt" ? "interrupted" : "succeeded";
    }
    const auto query = store.result(id); QCOMPARE(query.value("state"), QJsonValue(expected)); QVERIFY(query.value("terminal").toBool()); QVERIFY(query.contains("finishedAt"));
    QCOMPARE(query.value("steps").toArray()[0].toObject().value("action"), QJsonValue("capture-region"));
    QVERIFY(!QJsonDocument(query).toJson().contains("private")); QVERIFY(!read(dir.filePath("records/" + id + ".json")).contains("private-exception"));
    const auto before = read(dir.filePath("records/" + id + ".json")); QVERIFY(!execution.finish("failed")); QCOMPARE(read(dir.filePath("records/" + id + ".json")), before);
  }
  void admissionFailureAndLateBusyNeverRunCapture() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); bool busy = false; int started = 0, prepared = 0; const auto w = workflow(dir.filePath("out"));
    ActivationOperations ops; ops.captureBusy = [&] { return busy; };
    ops.prepareCapture = [&](const ActivationRequest &request) -> std::function<void()> {
      ++prepared; auto execution = std::make_shared<WorkflowExecution>(request.id, "command-line", w, store.writer());
      if (!execution->admit()) return {};
      return [&, execution] { QVERIFY(execution->start()); ++started; QVERIFY(execution->finish("cancelled")); };
    };
    for (const auto action : {ActivationAction::CaptureRegion, ActivationAction::CaptureRegionChooseActions, ActivationAction::RunWorkflow}) {
      auto request = makeActivationRequest(action, {}, action == ActivationAction::RunWorkflow ? w.id.toString(QUuid::WithoutBraces) : QString{});
      ActivationDispatcher dispatcher(ops); busy = true; QCOMPARE(dispatcher.admit(request), ActivationReplyState::Busy); busy = false;
      QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); QCOMPARE(store.result(request.id).value("lastRecordedState"), QJsonValue("admitted"));
      auto mismatch = request; mismatch.id = uuid(); QCOMPARE(dispatcher.dispatch(mismatch), QString("activation-invalid"));
      busy = true; QCOMPARE(dispatcher.dispatch(request), QString("activation-busy")); busy = false;
      QCOMPARE(store.result(request.id).value("state"), QJsonValue("not-started"));
      request.id = uuid(); QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(request).isEmpty()); QCOMPARE(store.result(request.id).value("state"), QJsonValue("cancelled"));
    }
    QCOMPARE(started, 3); QCOMPARE(prepared, 6);
    auto request = makeActivationRequest(ActivationAction::CaptureRegion); { ActivationDispatcher dispatcher(ops); QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); }
    QCOMPARE(store.result(request.id).value("state"), QJsonValue("not-started"));
    ops.prepareCapture = [&](const ActivationRequest &) -> std::function<void()> { return {}; };
    ActivationDispatcher denied(ops); QCOMPARE(denied.admit(makeActivationRequest(ActivationAction::CaptureRegion)), ActivationReplyState::Rejected); QCOMPARE(started, 3);
  }
  void staleAndUnsafeStorageCannotOverwriteOrAuthorizeAnAction() {
    QTemporaryDir dir; const auto root = dir.filePath("records"); NativeWorkflowExecutions store(root); const auto id = uuid();
    WorkflowExecution execution(id, "native-shell", workflow(dir.filePath("out")), store.writer()); QVERIFY(execution.admit());
    const auto path = root + "/" + id + ".json"; auto bytes = read(path); write(path, bytes + " ");
    QVERIFY(!execution.start()); QCOMPARE(read(path), bytes + " "); QCOMPARE(store.result(id).value("lastRecordedState"), QJsonValue("admitted"));
    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
    QCOMPARE(store.result(id).value("error").toObject().value("code"), QJsonValue("workflow-storage-unavailable"));
    QVERIFY(QFile::remove(path)); QVERIFY(QFile::link(dir.filePath("missing-private"), path));
    WorkflowExecution duplicate(id, "native-shell", workflow(dir.filePath("out")), store.writer()); QVERIFY(!duplicate.admit());
    QCOMPARE(store.result(id).value("error").toObject().value("code"), QJsonValue("workflow-storage-unavailable"));
    const auto link = dir.filePath("linked"); QVERIFY(QFile::link(root, link)); NativeWorkflowExecutions linked(link);
    WorkflowExecution unsafe(uuid(), "native-shell", workflow(dir.filePath("out")), linked.writer()); QVERIFY(!unsafe.admit());
  }
  void malformedRecordsCannotInjectPrivateQueryFieldsOrInventTerminalSuccess() {
    const auto id = uuid(); auto w = workflow("/tmp/private-execution-output");
    WorkflowExecution execution(id, "native-shell", w, [](const QString &, const std::optional<QByteArray> &, const QByteArray &) { return JournalWrite{true, {}}; });
    QVERIFY(execution.admit()); auto valid = QJsonDocument::fromJson(serializeWorkflowExecution(execution.record())).object();
    QVERIFY(parseWorkflowExecution(QJsonDocument(valid).toJson(), id));
    for (const auto &field : {QString("version"), QString("id"), QString("origin"), QString("state"), QString("startedAt"), QString("finishedAt"), QString("settings"), QString("pipelineId"), QString("diagnostic"), QString("steps")}) {
      auto bad = valid; bad.insert(field, "private-injected-value"); QVERIFY2(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id), qPrintable(field));
      bad = valid; bad.remove(field); QVERIFY2(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id), qPrintable(field));
    }
    auto bad = valid; bad.insert("private-extra", "secret"); QVERIFY(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id));
    bad = valid; auto settings = bad.value("settings").toObject(); settings.insert("private-extra", "secret"); bad.insert("settings", settings); QVERIFY(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id));
    bad = valid; bad.insert("state", "succeeded"); bad.insert("finishedAt", bad.value("startedAt")); QVERIFY(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id));
    bad = valid; bad.insert("steps", QJsonArray{QJsonObject{{"action", "private-output"}, {"state", "succeeded"}}}); QVERIFY(!parseWorkflowExecution(QJsonDocument(bad).toJson(), id));
    QVERIFY(!parseWorkflowExecution(QJsonDocument(valid).toJson(), uuid()));
    QVERIFY(!parseWorkflowExecution(QByteArray(maximumJournalBytes + 1, ' '), id));
    QCOMPARE(workflowExecutionError("private-secret").value("error").toObject().value("code"), QJsonValue("workflow-storage-unavailable"));
  }
  void actualHeadlessLookup_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"complete", "unconfirmed", "missing", "corrupt", "newer", "unsafe", "symlink", "invalid-id", "conflict", "unknown", "empty"}) QTest::newRow(mode) << QString::fromLatin1(mode);
  }
  void actualHeadlessLookup() {
    QFETCH(QString, mode); QTemporaryDir dir; auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) environment.insert(key, dir.filePath(key));
    environment.insert("QT_QPA_PLATFORM", "xcb"); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + dir.filePath("no-bus")); environment.insert("QT_QPA_PLATFORMTHEME", "");
    const auto root = dir.filePath("XDG_STATE_HOME/com.xerahs.app/workflow-executions"); const auto id = uuid(); const auto path = root + "/" + id + ".json";
    if (mode != "missing" && mode != "invalid-id" && mode != "conflict" && mode != "unknown" && mode != "empty") {
      NativeWorkflowExecutions store(root); WorkflowExecution execution(id, "command-line", workflow(dir.filePath("private-out")), store.writer()); QVERIFY(execution.admit()); QVERIFY(execution.start());
      if (mode != "unconfirmed") QVERIFY(execution.finish("permission-denied", "capture-permission-denied"));
    }
    if (mode == "corrupt") write(path, "{private-secret");
    if (mode == "newer") { auto object = QJsonDocument::fromJson(read(path)).object(); object.insert("version", 999); write(path, QJsonDocument(object).toJson()); }
    if (mode == "unsafe") QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
    if (mode == "symlink") { const auto target = dir.filePath("private-record"); QVERIFY(QFile::rename(path, target)); QVERIFY(QFile::link(target, path)); }
    const auto before = read(path); QStringList args{"--workflow-result", mode == "invalid-id" ? "private-secret" : mode == "empty" ? "" : id, "--non-interactive", "--json"};
    if (mode == "conflict") args << "--list-workflows"; if (mode == "unknown") args << "--private-secret";
    QProcess app; app.setProcessEnvironment(environment); app.start(QCoreApplication::applicationDirPath() + "/xerahs", args); QVERIFY(app.waitForFinished(5000)); QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    const auto bytes = app.readAllStandardOutput(); QCOMPARE(bytes.count('\n'), 1); const auto result = QJsonDocument::fromJson(bytes).object();
    const bool good = mode == "complete" || mode == "unconfirmed"; QCOMPARE(app.exitCode(), good ? 0 : 1); QCOMPARE(result.value("ok").toBool(), good);
    if (good) { QCOMPARE(result.value("requestId"), QJsonValue(id)); QCOMPARE(result.value("state"), QJsonValue(mode == "complete" ? "permission-denied" : "outcome-unconfirmed")); }
    else QCOMPARE(result.value("error").toObject().value("code"), QJsonValue(mode == "missing" ? "workflow-execution-not-found" : mode == "corrupt" || mode == "newer" ? "workflow-execution-invalid" : mode == "invalid-id" || mode == "empty" ? "workflow-execution-id-invalid" : mode == "conflict" || mode == "unknown" ? "usage-invalid" : "workflow-storage-unavailable"));
    QVERIFY(!bytes.contains("private")); QVERIFY(app.readAllStandardError().isEmpty()); QCOMPARE(read(path), before);
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) QVERIFY(!QFile::exists(dir.filePath(key)));
    if (mode == "missing" || mode == "invalid-id" || mode == "conflict" || mode == "unknown" || mode == "empty") QVERIFY(!QFile::exists(dir.filePath("XDG_STATE_HOME")));
  }

  // WFCAT-002: file-upload and watch-folder operations use the same execution record.
  void fileOperationExecutionHasTheSharedRecordFields_data() {
    QTest::addColumn<QString>("origin"); QTest::addColumn<QString>("operation");
    QTest::newRow("upload") << "file-upload" << "upload";
    QTest::newRow("watch-upload") << "watch-folder" << "upload";
    QTest::newRow("watch-copy") << "watch-folder" << "copy";
    QTest::newRow("watch-move") << "watch-folder" << "move";
  }
  void fileOperationExecutionHasTheSharedRecordFields() {
    QFETCH(QString, origin); QFETCH(QString, operation);
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); const auto id = uuid();
    const bool upload = operation == "upload";
    WorkflowExecution execution(id, origin, fileOperationSnapshot(operation, upload ? "dest-1" : "", upload ? "Fixture" : "",
        QJsonObject{{"action", operation}, {"recursive", true}, {"filters", QJsonArray{"*.png"}}}), store.writer());
    QVERIFY(execution.admit()); QVERIFY(execution.start());
    QVERIFY(execution.step("read-file", "succeeded"));
    QVERIFY(execution.step(upload ? "upload-file" : "copy-file", "succeeded"));
    if (operation == "move") QVERIFY(execution.step("remove-source", "succeeded"));
    const auto terminal = fileOperationTerminal(id, operation, execution.record().steps, {});
    QCOMPARE(terminal.state, QString("succeeded")); QCOMPARE(terminal.outputs, QStringList{id + (upload ? "/upload.receipt" : "/watch.copy")});
    QVERIFY(execution.finish(terminal.state, terminal.diagnostic, terminal.outputs));
    const auto result = store.result(id);
    QVERIFY(result.value("ok").toBool()); QCOMPARE(result.value("requestId").toString(), id); QCOMPARE(result.value("origin").toString(), origin);
    QCOMPARE(result.value("workflowId").toString(), operation); QCOMPARE(result.value("state").toString(), QString("succeeded"));
    QVERIFY(result.contains("startedAt") && result.contains("finishedAt")); QVERIFY(result.value("terminal").toBool());
    QCOMPARE(result.value("steps").toArray().size(), operation == "move" ? 3 : 2);
    QCOMPARE(result.value("outputs").toArray().size(), 1);
    QVERIFY(!result.contains("settings"));
    const auto bytes = read(dir.filePath("records/" + id + ".json")); const auto parsed = parseWorkflowExecution(bytes, id); QVERIFY(parsed);
    QCOMPARE(parsed->settings.value("operation").toString(), operation);
    QCOMPARE(parsed->settings.value("rule").toObject().value("recursive").toBool(), true);
    QCOMPARE(parsed->settings.value("destination").isNull(), !upload);
  }

  void fileOperationSnapshotsRejectSecretsPathsAndUnknownFields() {
    const auto id = uuid(); WorkflowExecutionRecord r; r.id = id; r.origin = "file-upload"; r.startedAt = QDateTime::currentDateTimeUtc();
    r.settings = fileOperationSnapshot("upload", "dest", "Fixture"); QVERIFY(parseWorkflowExecution(serializeWorkflowExecution(r), id));
    auto with = [&](const QString &key, const QJsonValue &value) { auto copy = r; copy.settings.insert(key, value); return copy; };
    QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(with("token", "fixture-secret")), id));
    QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(with("destination", QJsonObject{{"id", "d"}, {"name", "n"}, {"secret", "s"}})), id));
    QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(with("rule", QJsonObject{{"action", "copy"}, {"recursive", false}, {"filters", QJsonArray{}}, {"path", "/home/x"}})), id));
    auto wrongOrigin = r; wrongOrigin.origin = "native-shell"; QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(wrongOrigin), id));
    auto capture = r; capture.origin = "native-shell"; capture.settings = workflowSnapshot(defaultCaptureWorkflows().entries[0]);
    auto fileOrigin = capture; fileOrigin.origin = "watch-folder"; QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(fileOrigin), id));
    // An uploaded file has no copy reference, and an output may never be a path or URL.
    auto output = r; output.state = "partial"; output.finishedAt = output.startedAt; output.outputs = {id + "/upload.receipt"};
    QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(output), id));
    output.state = "succeeded"; output.outputs = {"/home/user/file"}; QVERIFY(!parseWorkflowExecution(serializeWorkflowExecution(output), id));
    // Unknown diagnostics are normalised, so free text or secrets cannot enter a record.
    QCOMPARE(fileOperationDiagnostic("http-error: Bearer fixture-secret"), QString("http-error"));
    QCOMPARE(fileOperationDiagnostic("Bearer fixture-secret"), QString("action-failed"));
  }

  // WFCAT-004: shared taxonomy for upload and watch outcomes.
  void fileOperationFailureTaxonomy_data() {
    QTest::addColumn<QString>("code"); QTest::addColumn<int>("http"); QTest::addColumn<QString>("state");
    QTest::newRow("http-4xx") << "http-error" << 404 << "permanent-failure";
    QTest::newRow("http-401") << "http-error" << 401 << "permanent-failure";
    QTest::newRow("http-5xx") << "http-error" << 503 << "transient-failure";
    QTest::newRow("timeout") << "timeout" << 0 << "transient-failure";
    QTest::newRow("network") << "network-error" << 0 << "transient-failure";
    QTest::newRow("missing-file") << "input-missing" << 0 << "permanent-failure";
    QTest::newRow("not-a-file") << "input-not-file" << 0 << "validation-failure";
    QTest::newRow("too-large") << "input-too-large" << 0 << "validation-failure";
    QTest::newRow("unreadable") << "input-unreadable" << 0 << "permission-denied";
    QTest::newRow("no-destination") << "destination-missing" << 0 << "unavailable-dependency";
    QTest::newRow("missing-secret") << "missing-secret" << 0 << "unavailable-dependency";
    QTest::newRow("copy-failed") << "watch-copy-failed" << 0 << "permanent-failure";
  }
  void fileOperationFailureTaxonomy() {
    QFETCH(QString, code); QFETCH(int, http); QFETCH(QString, state);
    const auto failure = classifyFileOperationFailure(code, http);
    QJsonArray steps{QJsonObject{{"action", "read-file"}, {"state", "succeeded"}},
        QJsonObject{{"action", "upload-file"}, {"state", "failed"}, {"failure_class", failureClassName(failure)}}};
    const auto terminal = fileOperationTerminal(uuid(), "upload", steps, code);
    QCOMPARE(terminal.state, state); QCOMPARE(terminal.diagnostic, code); QVERIFY(terminal.outputs.isEmpty());
  }
  void cancellationAndPartialStatesAndNoSuccessBeforeCommit() {
    const auto id = uuid();
    const QJsonObject read{{"action", "read-file"}, {"state", "succeeded"}}, copy{{"action", "copy-file"}, {"state", "succeeded"}};
    // Cancelled before a durable effect: cancelled, nothing to reference.
    auto t = fileOperationTerminal(id, "copy", {read, QJsonObject{{"action", "copy-file"}, {"state", "cancelled"}}}, "cancelled");
    QCOMPARE(t.state, QString("cancelled")); QVERIFY(t.outputs.isEmpty());
    // Move: the copy committed but source removal failed, so the outcome is partial and keeps the copy.
    t = fileOperationTerminal(id, "move", {read, copy, QJsonObject{{"action", "remove-source"}, {"state", "failed"}, {"failure_class", "permanent-failure"}}}, "watch-move-remove-failed");
    QCOMPARE(t.state, QString("partial")); QCOMPARE(t.outputs, QStringList{id + "/watch.copy"}); QCOMPARE(t.diagnostic, QString("watch-move-remove-failed"));
    // Nothing committed (no upload/copy step succeeded) can never be succeeded.
    t = fileOperationTerminal(id, "upload", {read}, {}); QCOMPARE(t.state, QString("permanent-failure"));
    t = fileOperationTerminal(id, "move", {read}, {}); QVERIFY(t.state != "succeeded"); QVERIFY(t.outputs.isEmpty());
    // Presentation: only succeeded reads as completed.
    for (const auto *state : {"partial", "cancelled", "permanent-failure", "transient-failure", "validation-failure"}) {
      const auto notice = fileOperationNotice("move", state, "a.png");
      QVERIFY2(!notice.completed && !notice.headline.startsWith("Processed") && notice.headline.contains(QString(state).section('-', 0, 0)), state);
    }
    QVERIFY(fileOperationNotice("upload", "succeeded", "a.png").completed);
    // A step is rejected once the execution is terminal, so late success cannot overwrite a failure.
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records"));
    WorkflowExecution execution(id, "watch-folder", fileOperationSnapshot("move", {}, {}, std::nullopt), store.writer());
    QVERIFY(execution.admit()); QVERIFY(execution.start()); QVERIFY(execution.finish("partial", "watch-move-remove-failed", {id + "/watch.copy"}));
    QVERIFY(!execution.step("remove-source", "succeeded")); QVERIFY(!execution.finish("succeeded"));
    QCOMPARE(store.result(id).value("state").toString(), QString("partial"));
  }
};
QTEST_GUILESS_MAIN(WorkflowExecutionTest)
#include "WorkflowExecutionTest.moc"
