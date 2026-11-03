// IFCAT-016: every stable interface error code maps to fixed user text, an exit
// status and a retry rule; producers accept exactly the table's codes; launch
// results preserve the correlation ID and expose nothing else.
#include "shell/ActivationProtocol.h"
#include "shell/ErrorPresentation.h"
#include "workflows/CaptureWorkflows.h"
#include "workflows/WorkflowExecution.h"
#include <QJsonObject>
#include <QSet>
#include <QTest>

using namespace xerahs::shell;

class ErrorPresentationTest final : public QObject {
  Q_OBJECT
private slots:
  void everyStableCodeHasFixedTextExitStatusAndRetryRule() {
    QSet<QString> seen;
    for (const auto &row : errorPresentations()) {
      const QString code = QString::fromLatin1(row.code), message = QString::fromUtf8(row.message);
      QVERIFY2(!seen.contains(code), qPrintable(code)); seen.insert(code);
      QVERIFY2(row.surfaces != 0, qPrintable(code));
      QVERIFY2(!message.isEmpty() && message.size() < 300 && message.endsWith('.'), qPrintable(code));
      QVERIFY2(!message.contains('%') && !message.contains('\n') && !message.contains("0x") && !message.contains("Traceback") && !message.contains("at /"), qPrintable(code));
      QCOMPARE(errorMessage(code), message); QCOMPARE(errorExitCode(code), row.exitCode); QVERIFY(retryAdvice(code) == row.retry);
      QVERIFY2(row.exitCode == 1 || row.exitCode == 2, qPrintable(code));
      // Safe-retry rule: only Safe codes invite a plain retry; unsafe or unknown outcomes only say "before retrying".
      const bool invitesRetry = message.contains("Try again", Qt::CaseInsensitive) || message.contains("try again", Qt::CaseInsensitive);
      QCOMPARE(invitesRetry, row.retry == RetryAdvice::Safe);
      if (row.retry == RetryAdvice::CheckFirst) QVERIFY2(message.contains("before retrying"), qPrintable(code));
      // Busy is the one distinct exit status, and it is the safe, nothing-started case.
      QCOMPARE(row.exitCode == 2, code == u"activation-busy");
      if (row.exitCode == 2) QVERIFY(row.retry == RetryAdvice::Safe);
      QCOMPARE(retryIsSafe(code), row.retry == RetryAdvice::Safe);
    }
    // The distinct, actionable CLI results named by the specification.
    for (const auto *code : {"presentation-required", "activation-busy", "activation-unavailable", "activation-delivery-unknown", "workflow-unavailable", "usage-invalid"})
      QVERIFY(findErrorPresentation(code));
  }
  void unknownCodesGetTheGenericPresentationWithoutEcho() {
    for (const auto &code : {QString("not-a-code"), QString("/home/user/private-secret"), QString("Traceback at 0x1234"), QString()}) {
      QVERIFY(!findErrorPresentation(code));
      const auto message = errorMessage(code);
      QVERIFY(!message.isEmpty()); QVERIFY(code.isEmpty() || !message.contains(code));
      QCOMPARE(errorExitCode(code), 1); QVERIFY(retryAdvice(code) == RetryAdvice::CheckFirst); QVERIFY(!retryIsSafe(code));
      QVERIFY(!message.contains("try again", Qt::CaseInsensitive));
    }
  }
  void producersAcceptExactlyTheTableCodesAndLaunchResultsKeepTheCorrelationId() {
    const auto request = makeActivationRequest(ActivationAction::CaptureRegion);
    for (const auto &row : errorPresentations()) {
      const QString code = QString::fromLatin1(row.code);
      const bool wire = row.surfaces & WireReplySurface, launch = row.surfaces & LaunchSurface, discovery = row.surfaces & DiscoverySurface, execution = row.surfaces & ExecutionSurface;
      const auto reply = parseActivationReply(serializeActivationReply({request.id, ActivationReplyState::Rejected, code}), request.id);
      QVERIFY2(reply, qPrintable(code)); QCOMPARE(reply->diagnostic, wire ? code : QString("activation-handler-failed"));
      const auto launchResult = activationLaunchResult(request, {}, code);
      QCOMPARE(launchResult.value("error").toObject().value("code").toString(), launch ? code : QString("activation-handler-failed"));
      QCOMPARE(launchResult.value("requestId").toString(), request.id); QVERIFY(!launchResult.value("ok").toBool());
      QCOMPARE(launchResult.keys().size(), 4);  // schemaVersion, ok, requestId, error: no payload, path or trace.
      QCOMPARE(launchResult.value("error").toObject().keys(), QStringList{"code"});
      QCOMPARE(xerahs::workflows::workflowDiscoveryError(code).value("error").toObject().value("code").toString(), discovery ? code : QString("workflow-storage-unavailable"));
      QCOMPARE(xerahs::workflows::workflowExecutionError(code).value("error").toObject().value("code").toString(), execution ? code : QString("workflow-storage-unavailable"));
    }
  }
};
QTEST_GUILESS_MAIN(ErrorPresentationTest)
#include "ErrorPresentationTest.moc"
