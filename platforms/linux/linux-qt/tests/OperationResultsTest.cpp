// IFCAT-006 on the real shell: long-running tools run in the background, show
// determinate or indeterminate progress, cancel cooperatively, and their
// terminal result is still available after the transient window is closed.
#include "HashCheckerDialog.h"
#include "ShellFixture.h"
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;

class OperationResultsTest final : public QObject {
  Q_OBJECT
  QTemporaryDir root;
  static HashCheckerDialog *hashDialog() {
    for (auto *widget : QApplication::topLevelWidgets()) if (auto *dialog = qobject_cast<HashCheckerDialog *>(widget)) return dialog;
    return nullptr;
  }
private slots:
  void initTestCase() { QVERIFY(root.isValid()); }
  // Progress is indeterminate when the size is unknowable, the read can be cancelled, and cancelling
  // is a visible terminal state with no digest.
  void unknownSizeWorkShowsIndeterminateProgressAndCancelsCooperatively() {
    Shell shell(root.path()); shell.attach();
    shell.commands.action("hash-checker")->trigger();
    auto *hash = hashDialog(); QVERIFY(hash);
    auto *progress = hash->findChild<QProgressBar *>("hashProgress");
    hash->setFile("/dev/zero"); hash->compute();
    QTRY_VERIFY(hash->busy()); QTRY_COMPARE(progress->maximum(), 0); QCOMPARE(progress->minimum(), 0);  // indeterminate
    QVERIFY(hash->findChild<QPushButton *>("cancelHash")->isEnabled());
    hash->cancel(); QTRY_VERIFY(!hash->busy());
    QVERIFY(hash->digest().isEmpty()); QVERIFY(!hash->statusText().isEmpty());
    hash->close();
  }
  // Background behaviour: closing the transient window of a finished operation keeps its terminal result,
  // and re-running the same command shows that same window and result rather than starting empty.
  void aFinishedResultSurvivesClosingTheToolWindowAndReopeningIt() {
    Shell shell(root.path()); shell.attach();
    QFile file(root.filePath("abc")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("abc"); file.close();
    shell.commands.action("hash-checker")->trigger();
    auto *hash = hashDialog(); QVERIFY(hash);
    hash->setFile(file.fileName()); hash->compute(); QTRY_VERIFY(!hash->digest().isEmpty());
    const auto digest = hash->digest(), status = hash->statusText();
    QVERIFY(digest.size() >= 32);
    hash->close(); QVERIFY(!hash->isVisible());
    shell.commands.action("hash-checker")->trigger();
    QCOMPARE(hashDialog(), hash); QVERIFY(hash->isVisible());
    QCOMPARE(hash->digest(), digest); QCOMPARE(hash->statusText(), status);
    hash->close();
  }
  // The shell window is the durable terminal-result surface: hiding it mid-operation does not cancel the
  // operation, and the progress and the final result are there when it is shown again.
  void hidingTheShellWindowDoesNotCancelAndKeepsTheLatestResult() {
    Shell shell(root.path()); shell.attach(); auto &window = shell.window();
    auto *status = window.findChild<QLabel *>("operationStatus"); auto *progress = window.findChild<QProgressBar *>("operationProgress");
    window.activate(); window.showProgress("Region capture", 30);
    window.hide(); QVERIFY(!window.isVisible());
    window.showProgress("Region capture", 80);                       // The operation continues while hidden.
    window.activate(); QVERIFY(window.isVisible()); QVERIFY(!progress->isHidden()); QCOMPARE(progress->value(), 80);
    window.showResult("Capture", "Saved", true); window.hide(); window.activate();
    QVERIFY(progress->isHidden()); QCOMPARE(status->text(), QString("Capture: Saved"));
    shell.ctx.notify("Upload", "Failed: the destination refused the file", false, false);   // main()'s notify: later terminal results replace, in text
    window.hide(); window.activate(); QVERIFY(status->text().contains("Failed"));
  }
};

int main(int argc, char **argv) {
  QTemporaryDir profile; qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) qputenv(key, profile.filePath(key).toUtf8());
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + profile.filePath("no-bus")).toUtf8());
  QApplication app(argc, argv); QApplication::setQuitOnLastWindowClosed(false);
  OperationResultsTest test; return QTest::qExec(&test, argc, argv);
}
#include "OperationResultsTest.moc"
