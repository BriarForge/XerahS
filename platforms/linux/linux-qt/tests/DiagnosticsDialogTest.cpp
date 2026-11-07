#include "DiagnosticsDialog.h"
#include "ShellFixture.h"
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace xerahs::app;
namespace {
QByteArray contents(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class DiagnosticsDialogTest final : public QObject {
  Q_OBJECT
private slots:
  void registryRequiresReviewAndExportsExactlyTheFrozenPrivateSnapshot() {
    QTemporaryDir dir; Shell shell(dir.path()); shell.attach();
    postTrayMessage(shell.ctx, "Operation", "activation-busy: SYNTHETIC_PRIVATE_VALUE", false, 5000, false);
    postTrayMessage(shell.ctx, "Capture denied", "capture-permission-denied", false, 5000, false);
    auto *action = shell.commands.action("export-diagnostics"); QVERIFY(action); action->trigger();
    QPointer<DiagnosticsDialog> dialog = shell.window().findChild<DiagnosticsDialog *>("diagnosticsDialog"); QVERIFY(dialog); QVERIFY(dialog->isVisible());
    auto *preview = dialog->findChild<QPlainTextEdit *>("diagnosticsPreview"); QVERIFY(preview); QVERIFY(preview->isReadOnly());
    const auto reviewed = QJsonDocument::fromJson(preview->toPlainText().toUtf8()).object();
    QVERIFY(!preview->toPlainText().contains("SYNTHETIC_PRIVATE")); QCOMPARE(reviewed.value("recentErrors").toArray().size(), 2);
    bool denied = false;
    for (const auto &entry : reviewed.value("recentErrors").toArray()) if (entry.toObject().value("code") == "capture-permission-denied") {
      denied = true;
      QVERIFY(shell.window().findChild<QLabel *>("operationStatus")->text().contains(entry.toObject().value("correlationId").toString()));
    }
    QVERIFY(denied);
    QVERIFY(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->isDefault());
    const auto target = dir.filePath("reviewed.json"); QVERIFY(!QFile::exists(target));
    const auto settingsBefore = contents(shell.settings.path()); const auto recordsBefore = shell.journal->list().entries.size();
    QVERIFY(shell.ctx.diagnosticLog->append("activation-unavailable", QUuid::createUuid().toString(QUuid::WithoutBraces), xerahs::diagnostics::Severity::Error));
    auto *exportButton = dialog->findChild<QPushButton *>("exportDiagnosticsFile"); exportButton->click();
    auto *picker = dialog->findChild<QFileDialog *>("diagnosticsExportFile"); QVERIFY(picker); QVERIFY(picker->isVisible());
    picker->selectFile(target); QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    auto *status = dialog->findChild<QLabel *>("diagnosticsExportStatus");
    QTRY_VERIFY(status->text().startsWith("Diagnostics exported.")); QVERIFY(exportButton->isEnabled());
    QCOMPARE(QJsonDocument::fromJson(contents(target)).object(), reviewed);
    QCOMPARE(contents(shell.settings.path()), settingsBefore); QCOMPARE(shell.journal->list().entries.size(), recordsBefore);
    dialog->reject(); QTRY_VERIFY(!dialog);
  }
  void cancellationAndForcedPickerOrOwnerDestructionWriteNothing_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"cancel-picker", "delete-picker", "close-owner", "delete-owner"}) QTest::newRow(mode) << QString(mode);
  }
  void cancellationAndForcedPickerOrOwnerDestructionWriteNothing() {
    QFETCH(QString, mode);
    QTemporaryDir dir; Shell shell(dir.path()); shell.commands.action("export-diagnostics")->trigger();
    QPointer<DiagnosticsDialog> dialog = shell.window().findChild<DiagnosticsDialog *>("diagnosticsDialog"); QVERIFY(dialog);
    auto *preview = dialog->findChild<QPlainTextEdit *>("diagnosticsPreview"); const auto original = preview->toPlainText();
    auto *exportButton = dialog->findChild<QPushButton *>("exportDiagnosticsFile"); exportButton->click();
    QPointer<QFileDialog> picker = dialog->findChild<QFileDialog *>("diagnosticsExportFile"); QVERIFY(picker);
    const auto target = dir.filePath("not-confirmed.json"); picker->selectFile(target);
    if (mode == "cancel-picker") picker->reject();
    else if (mode == "delete-picker") delete picker.data();
    else if (mode == "close-owner") dialog->reject();
    else delete dialog.data();
    QTRY_VERIFY(!picker); QVERIFY(!QFile::exists(target));
    if (dialog) {
      QTRY_VERIFY(exportButton->isEnabled()); QCOMPARE(preview->toPlainText(), original);
      dialog->reject(); QTRY_VERIFY(!dialog);
    }
    QVERIFY(!QFile::exists(target));
  }
  void existingTargetIsPreservedAndANewTargetCanBeChosenWithoutChangingThePreview() {
    QTemporaryDir dir; Shell shell(dir.path()); shell.commands.action("export-diagnostics")->trigger();
    QPointer<DiagnosticsDialog> dialog = shell.window().findChild<DiagnosticsDialog *>("diagnosticsDialog"); QVERIFY(dialog);
    const auto reviewed = dialog->findChild<QPlainTextEdit *>("diagnosticsPreview")->toPlainText();
    const auto existing = dir.filePath("keep.json");
    { QFile file(existing); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("ORIGINAL SYNTHETIC CONTENT"); }
    auto *exportButton = dialog->findChild<QPushButton *>("exportDiagnosticsFile"); exportButton->click();
    auto *picker = dialog->findChild<QFileDialog *>("diagnosticsExportFile"); QVERIFY(picker);
    picker->selectFile(existing); QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    auto *status = dialog->findChild<QLabel *>("diagnosticsExportStatus");
    QTRY_VERIFY(status->text().startsWith("diagnostics-export-existing-target:"));
    QCOMPARE(contents(existing), QByteArray("ORIGINAL SYNTHETIC CONTENT")); QVERIFY(exportButton->isEnabled());
    QCOMPARE(dialog->findChild<QPlainTextEdit *>("diagnosticsPreview")->toPlainText(), reviewed);
    const auto fresh = dir.filePath("new.json"); exportButton->click();
    picker = dialog->findChild<QFileDialog *>("diagnosticsExportFile"); QVERIFY(picker);
    picker->selectFile(fresh); QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    QTRY_VERIFY(status->text().startsWith("Diagnostics exported."));
    QCOMPARE(QJsonDocument::fromJson(contents(fresh)).object(), QJsonDocument::fromJson(reviewed.toUtf8()).object());
    QCOMPARE(contents(existing), QByteArray("ORIGINAL SYNTHETIC CONTENT")); dialog->reject(); QTRY_VERIFY(!dialog);
  }
};
int main(int argc, char **argv) {
  QTemporaryDir profile; qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) qputenv(key, profile.filePath(key).toUtf8());
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + profile.filePath("no-bus")).toUtf8());
  qputenv("DBUS_SYSTEM_BUS_ADDRESS", ("unix:path=" + profile.filePath("no-system-bus")).toUtf8());
  QApplication app(argc, argv); QApplication::setQuitOnLastWindowClosed(false);
  DiagnosticsDialogTest test; return QTest::qExec(&test, argc, argv);
}
#include "DiagnosticsDialogTest.moc"
