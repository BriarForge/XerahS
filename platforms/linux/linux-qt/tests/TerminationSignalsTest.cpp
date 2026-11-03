#include "shell/TerminationSignals.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <csignal>

using xerahs::shell::TerminationSignals;

namespace {
void marker(int) {}
bool sameDisposition(int signal, void (*handler)(int)) {
  struct sigaction current {};
  return sigaction(signal, nullptr, &current) == 0 && current.sa_handler == handler;
}
}

class TerminationSignalsTest final : public QObject {
  Q_OBJECT
private slots:
  void requestedSignalsDoNotTerminateAndMapToDocumentedExitStatus() {
    TerminationSignals handlers;
    QVERIFY(handlers.installed());
    QSignalSpy spy(&handlers, &TerminationSignals::requested);
    QCOMPARE(::raise(SIGINT), 0);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), int(SIGINT));
    QCOMPARE(::raise(SIGTERM), 0);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), int(SIGTERM));
    QCOMPARE(TerminationSignals::exitStatus(SIGINT), 130);
    QCOMPARE(TerminationSignals::exitStatus(SIGTERM), 143);
  }
  void destructorRestoresPreviousDispositionsAndAllowsReinstall() {
    signal(SIGINT, marker); signal(SIGTERM, marker);
    {
      TerminationSignals handlers;
      QVERIFY(handlers.installed());
      QVERIFY(!sameDisposition(SIGINT, marker));
      QVERIFY(!sameDisposition(SIGTERM, marker));
    }
    QVERIFY(sameDisposition(SIGINT, marker));
    QVERIFY(sameDisposition(SIGTERM, marker));
    TerminationSignals again;
    QVERIFY(again.installed());
    signal(SIGINT, SIG_DFL); signal(SIGTERM, SIG_DFL);
  }
  void aSecondInstanceNeverStealsTheHandlers() {
    TerminationSignals first;
    TerminationSignals second;
    QVERIFY(first.installed());
    QVERIFY(!second.installed());
    QSignalSpy firstSpy(&first, &TerminationSignals::requested);
    QSignalSpy secondSpy(&second, &TerminationSignals::requested);
    QCOMPARE(::raise(SIGTERM), 0);
    QTRY_COMPARE(firstSpy.count(), 1);
    QCOMPARE(secondSpy.count(), 0);
  }
};
QTEST_GUILESS_MAIN(TerminationSignalsTest)
#include "TerminationSignalsTest.moc"
