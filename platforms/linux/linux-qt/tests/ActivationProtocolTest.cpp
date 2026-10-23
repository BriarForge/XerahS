#include "shell/ActivationProtocol.h"
#include "shell/ActivationDispatcher.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <stdexcept>
using namespace xerahs::shell;
namespace {
const QString id = QStringLiteral("01234567-89ab-4cde-8123-456789abcdef");
QByteArray json(QJsonObject object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
}
class ActivationProtocolTest final : public QObject {
  Q_OBJECT
private slots:
  void validRequestsRetainExactPathsAndIndependentOperationNames() {
    for (const auto &name : {QString("activate"), QString("capture-region"), QString("edit-image"), QString("run-workflow"), QString("capture-region-choose-actions"), QString("capture-full-screen"), QString("capture-monitor"), QString("start-recording"), QString("stop-recording")}) {
      QJsonObject input{{"version", 1}, {"id", id}, {"action", name}};
      const QString path = QString::fromUtf8("/tmp/ λ file\nwith a newline .png ");
      if (name == u"edit-image") input.insert("path", path);
      if (name == u"run-workflow") input.insert("workflow_id", id);
      const auto parsed = parseActivation(json(input)); QVERIFY(parsed.request); QVERIFY(parsed.diagnostic.isEmpty());
      QCOMPARE(parsed.request->id, id); QCOMPARE(parsed.request->path, name == u"edit-image" ? path : QString{});
      QCOMPARE(parsed.request->workflowId, name == u"run-workflow" ? id : QString{});
      QCOMPARE(QJsonDocument::fromJson(serializeActivation(*parsed.request)).object(), input);
    }
    const auto generated = makeActivationRequest(ActivationAction::Activate); QVERIFY(generated.id != makeActivationRequest(ActivationAction::Activate).id);
    QVERIFY(parseActivation(serializeActivation(generated)).request);
  }
  void malformedDataCannotManufactureAuthority_data() {
    QTest::addColumn<QByteArray>("input");
    const QJsonObject base{{"version", 1}, {"id", id}, {"action", "activate"}};
    QTest::newRow("empty") << QByteArray{}; QTest::newRow("array") << QByteArray("[]");
    QTest::newRow("invalid-json") << QByteArray("{private fixture}");
    QTest::newRow("duplicate-action") << QByteArray("{\"version\":1,\"id\":\"01234567-89ab-4cde-8123-456789abcdef\",\"action\":\"capture-region\",\"action\":\"activate\"}");
    QTest::newRow("escaped-duplicate") << QByteArray("{\"version\":1,\"id\":\"01234567-89ab-4cde-8123-456789abcdef\",\"action\":\"capture-region\",\"\\u0061ction\":\"activate\"}");
    QTest::newRow("invalid-utf8") << QByteArray("{\"version\":1,\"id\":\"") + QByteArray(1, char(0xff)) + "\"}";
    for (const auto &field : {QString("id"), QString("action"), QString("version")}) {
      auto missing = base; missing.remove(field); QTest::newRow(qPrintable("missing-" + field)) << json(missing);
      auto wrong = base; wrong.insert(field, true); QTest::newRow(qPrintable("wrong-type-" + field)) << json(wrong);
    }
    for (const auto &field : {QString("authority"), QString("approved"), QString("uid"), QString("path")}) {
      auto extra = base; extra.insert(field, "human-approved"); QTest::newRow(qPrintable("extra-" + field)) << json(extra);
    }
    auto unknown = base; unknown.insert("action", "upload-private-file"); QTest::newRow("unknown-operation") << json(unknown);
    auto badId = base; badId.insert("id", "bad-id"); QTest::newRow("invalid-id") << json(badId);
    auto relative = base; relative.insert("action", "edit-image"); relative.insert("path", "relative.png"); QTest::newRow("relative-path") << json(relative);
    relative.insert("path", QString("/tmp/private") + QChar(0) + ".png"); QTest::newRow("nul-path") << json(relative);
    relative.insert("path", 17); QTest::newRow("wrong-path-type") << json(relative);
    QTest::newRow("oversized") << QByteArray(maximumActivationBytes + 1, ' ');
  }
  void malformedDataCannotManufactureAuthority() {
    QFETCH(QByteArray, input); const auto parsed = parseActivation(input);
    QVERIFY(!parsed.request); QCOMPARE(parsed.diagnostic, QString("activation-invalid"));
  }
  void futureVersionsAreDistinctAndTheByteBoundaryIsInclusive() {
    QCOMPARE(parseActivation(json({{"version", 2}, {"id", id}, {"action", "activate"}})).diagnostic, QString("activation-version-unsupported"));
    auto input = json({{"version", 1}, {"id", id}, {"action", "activate"}});
    input.append(QByteArray(maximumActivationBytes - input.size(), ' ')); QVERIFY(parseActivation(input).request);
    input.append(' '); QVERIFY(!parseActivation(input).request);
  }
  void repliesRequireCorrelationAndOnlyPublishStableErrors() {
    for (auto state : {ActivationReplyState::Accepted, ActivationReplyState::Busy, ActivationReplyState::Rejected}) {
      const auto wire = serializeActivationReply({id, state, "activation-busy"}); auto parsed = parseActivationReply(wire, id);
      QVERIFY(parsed); QCOMPARE(parsed->state, state); QCOMPARE(parsed->id, id);
      QVERIFY(!parseActivationReply(wire, "11234567-89ab-4cde-8123-456789abcdef"));
    }
    const auto redacted = serializeActivationReply({id, ActivationReplyState::Rejected, "private fixture path and token"});
    QVERIFY(!redacted.contains("private fixture")); QCOMPARE(parseActivationReply(redacted, id)->diagnostic, QString("activation-handler-failed"));
    QVERIFY(!parseActivationReply(json({{"version", 1}, {"id", id}, {"state", "accepted"}, {"path", "/private"}}), id));
    QVERIFY(!parseActivationReply(json({{"version", 1}, {"id", id}, {"state", "rejected"}, {"error", "private"}}), id));
    QVERIFY(!parseActivationReply(QByteArray(maximumActivationReplyBytes + 1, ' '), id));
    QVERIFY(!parseActivationReply(QByteArray("{\"version\":1,\"id\":\"01234567-89ab-4cde-8123-456789abcdef\",\"state\":\"busy\",\"state\":\"accepted\"}"), id));
  }
  void authenticationRequiresAnActualMatchingKernelUser() {
    QVERIFY(authorizedActivationPeer(1000, 1000)); QVERIFY(!authorizedActivationPeer(1000, 1001));
    QVERIFY(!authorizedActivationPeer(1000, {})); QVERIFY(authorizedActivationPeer(0, 0));
  }
  void admissionReservesBeforeDeferredDispatchAndRejectsNestedOpen() {
    int captures = 0, edits = 0, activations = 0;
    bool captureBusy = false, modal = false;
    ActivationDispatcher *active = nullptr;
    const auto capture = makeActivationRequest(ActivationAction::CaptureRegion);
    const auto edit = makeActivationRequest(ActivationAction::EditImage, QString::fromUtf8("/fixture/λ\n image.png"));
    QString opened;
    ActivationDispatcher dispatcher({[&] { ++activations; }, [&] { ++captures; captureBusy = true; },
        [&](const QString &path) {
          ++edits; opened = path;
          QCOMPARE(active->admit(capture), ActivationReplyState::Busy);
          QCOMPARE(active->admit(makeActivationRequest(ActivationAction::EditImage, "/fixture/other.png")), ActivationReplyState::Busy);
        }, [&] { return captureBusy; }, [&] { return modal; }});
    active = &dispatcher;
    QCOMPARE(dispatcher.admit(edit), ActivationReplyState::Accepted);
    QCOMPARE(dispatcher.admit(capture), ActivationReplyState::Busy);
    auto changed = edit; changed.path = "/fixture/changed.png";
    QCOMPARE(dispatcher.dispatch(changed), QString("activation-invalid"));
    QVERIFY(dispatcher.dispatch(edit).isEmpty()); QCOMPARE(edits, 1); QCOMPARE(opened, edit.path);
    QCOMPARE(dispatcher.dispatch(edit), QString("activation-invalid")); QCOMPARE(edits, 1);
    QCOMPARE(dispatcher.admit(capture), ActivationReplyState::Accepted);
    QVERIFY(dispatcher.dispatch(capture).isEmpty()); QCOMPARE(captures, 1);
    QCOMPARE(dispatcher.admit(makeActivationRequest(ActivationAction::CaptureRegion)), ActivationReplyState::Busy);
    modal = true;
    QCOMPARE(dispatcher.admit(edit), ActivationReplyState::Busy);
    const auto activate = makeActivationRequest(ActivationAction::Activate);
    QCOMPARE(dispatcher.admit(activate), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(activate).isEmpty()); QCOMPARE(activations, 1);
  }
  void changedAvailabilityAndExceptionsReleaseReservationWithoutFalseSuccess() {
    bool modal = false; int calls = 0;
    const auto edit = makeActivationRequest(ActivationAction::EditImage, "/fixture/private.png");
    ActivationDispatcher dispatcher({{}, {}, [&](const QString &) { ++calls; throw std::runtime_error("private path and secret fixture"); }, {}, [&] { return modal; }});
    QCOMPARE(dispatcher.admit(makeActivationRequest(ActivationAction::Activate)), ActivationReplyState::Rejected);
    QCOMPARE(dispatcher.admit(edit), ActivationReplyState::Accepted);
    modal = true; QCOMPARE(dispatcher.dispatch(edit), QString("activation-busy")); QCOMPARE(calls, 0);
    modal = false; QCOMPARE(dispatcher.admit(edit), ActivationReplyState::Accepted);
    QCOMPARE(dispatcher.dispatch(edit), QString("activation-handler-failed")); QCOMPARE(calls, 1);
    QCOMPARE(dispatcher.admit(edit), ActivationReplyState::Accepted);
    QCOMPARE(dispatcher.dispatch(edit), QString("activation-handler-failed")); QCOMPARE(calls, 2);
  }
  void workflowRequestsCannotCarryDefinitionsOrAmbiguousIds() {
    const QJsonObject base{{"version", 1}, {"id", id}, {"action", "run-workflow"}, {"workflow_id", id}};
    for (const auto &value : {QJsonValue(), QJsonValue(1), QJsonValue("name"), QJsonValue(id.left(8)), QJsonValue(id.toUpper()), QJsonValue("00000000-0000-0000-0000-000000000000")}) {
      auto bad = base; bad.insert("workflow_id", value); QVERIFY(!parseActivation(json(bad)).request);
    }
    auto missing = base; missing.remove("workflow_id"); QVERIFY(!parseActivation(json(missing)).request);
    for (const auto *field : {"path", "actions", "output", "approved", "workflowId"}) {
      auto bad = base; bad.insert(field, "private fixture payload"); QVERIFY(!parseActivation(json(bad)).request);
    }
    auto other = base; other.insert("action", "capture-region"); QVERIFY(!parseActivation(json(other)).request);
    auto duplicate = json(base); duplicate.chop(1); duplicate += ",\"workflow_id\":\"" + id.toUtf8() + "\"}"; QVERIFY(!parseActivation(duplicate).request);
  }
  void deliveryJsonDoesNotExposePathsDefinitionsOrUnknownErrors() {
    auto request = makeActivationRequest(ActivationAction::RunWorkflow, "/private/path", id);
    auto result = activationLaunchResult(request, "running-instance"); QCOMPARE(result.value("schemaVersion"), QJsonValue(1)); QCOMPARE(result.value("workflowId"), QJsonValue(id)); QCOMPARE(result.value("state"), QJsonValue("accepted"));
    QVERIFY(!json(result).contains("private")); QVERIFY(!result.contains("outputs"));
    result = activationLaunchResult(request, {}, "private secret diagnostic"); QVERIFY(!result.value("ok").toBool()); QCOMPARE(result.value("error").toObject().value("code"), QJsonValue("activation-handler-failed")); QVERIFY(!json(result).contains("secret"));
    result = activationLaunchResult(request, "private delivery"); QVERIFY(!json(result).contains("private delivery"));
    request.id = "private correlation"; request.workflowId = "private workflow"; QVERIFY(!json(activationLaunchResult(request, {}, "workflow-unavailable")).contains("private"));
  }
  void workflowAdmissionFreezesSettingsAndReservesBeforePreparation() {
    QString setting = "original", used; bool busy = false, modal = false, nestedRefused = false; int prepared = 0, calls = 0, chooser = 0;
    ActivationDispatcher *active = nullptr; ActivationOperations operations;
    operations.captureRegion = [] {}; operations.captureBusy = [&] { return busy; }; operations.interactionBlocked = [&] { return modal; };
    operations.captureRegionWithActions = [&] { ++chooser; };
    operations.prepareWorkflow = [&](const QString &workflow) -> std::function<void()> {
      ++prepared; if (workflow != id) return {};
      const auto nested = makeActivationRequest(ActivationAction::CaptureRegion); nestedRefused = active->admit(nested) == ActivationReplyState::Busy;
      return [&, snapshot = setting] { ++calls; used = snapshot; };
    };
    ActivationDispatcher dispatcher(operations); active = &dispatcher;
    const auto request = makeActivationRequest(ActivationAction::RunWorkflow, {}, id);
    busy = true; QCOMPARE(dispatcher.admit(request), ActivationReplyState::Busy); QCOMPARE(prepared, 0); busy = false;
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); QVERIFY(nestedRefused); setting = "later";
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Busy); auto changed = request; changed.workflowId = makeActivationRequest(ActivationAction::Activate).id;
    QCOMPARE(dispatcher.dispatch(changed), QString("activation-invalid")); QVERIFY(dispatcher.dispatch(request).isEmpty()); QCOMPARE(calls, 1); QCOMPARE(used, QString("original"));
    QCOMPARE(dispatcher.dispatch(request), QString("activation-invalid")); QCOMPARE(calls, 1);
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); modal = true; QCOMPARE(dispatcher.dispatch(request), QString("activation-busy")); QCOMPARE(calls, 1); modal = false;
    const auto choose = makeActivationRequest(ActivationAction::CaptureRegionChooseActions); QCOMPARE(dispatcher.admit(choose), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(choose).isEmpty()); QCOMPARE(chooser, 1);
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(request).isEmpty()); QCOMPARE(used, QString("later"));
  }
  void rejectedOrThrowingWorkflowPreparationDoesNotLeaveAReservation() {
    int calls = 0; bool throwing = false; ActivationOperations operations;
    operations.prepareWorkflow = [&](const QString &) -> std::function<void()> { ++calls; if (throwing) throw std::runtime_error("private"); return {}; };
    ActivationDispatcher dispatcher(operations); const auto request = makeActivationRequest(ActivationAction::RunWorkflow, {}, id);
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Rejected); throwing = true; QCOMPARE(dispatcher.admit(request), ActivationReplyState::Rejected); throwing = false;
    QCOMPARE(dispatcher.admit(request), ActivationReplyState::Rejected); QCOMPARE(calls, 3); QCOMPARE(dispatcher.dispatch(request), QString("activation-invalid"));
  }
};
QTEST_APPLESS_MAIN(ActivationProtocolTest)
#include "ActivationProtocolTest.moc"
