#include "CapturePermissionHelp.h"
#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::app;
namespace {
QString executable(const QDir &dir, const QString &name) {
  QFile file(dir.filePath(name)); if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) qFatal("fixture failed");
  return file.fileName();
}
}
class CapturePermissionHelpTest final : public QObject {
  Q_OBJECT
private slots:
  void routeFollowsTheRunningDesktopAndOtherwiseWhateverExists() {
    QTemporaryDir dir; const auto gnome = executable(dir.path(), "gnome-control-center"), kde = executable(dir.path(), "systemsettings"); const QStringList path{dir.path()};
    const auto onGnome = captureSettingsRoute(path, "GNOME"); QVERIFY(onGnome); QCOMPARE(onGnome->program, gnome); QCOMPARE(onGnome->arguments, QStringList{"privacy"});
    const auto onKde = captureSettingsRoute(path, "KDE"); QVERIFY(onKde); QCOMPARE(onKde->program, kde); QVERIFY(onKde->arguments.isEmpty());
    QVERIFY(QFile::remove(gnome)); QCOMPARE(captureSettingsRoute(path, "GNOME")->program, kde);
    QVERIFY(QFile::remove(kde)); QVERIFY(!captureSettingsRoute(path, "KDE")); QVERIFY(!captureSettingsRoute({}, ""));
  }
  void guidanceWithASettingsAppOpensItThroughTheInjectedLauncherAndStaysOpen() {
    QTemporaryDir dir; const auto gnome = executable(dir.path(), "gnome-control-center"); QString program; QStringList arguments; int launches = 0;
    CapturePermissionHelpPorts ports; ports.searchPath = [&] { return QStringList{dir.path()}; }; ports.desktop = [] { return QString("GNOME"); };
    ports.launch = [&](const QString &p, const QStringList &a) { program = p; arguments = a; ++launches; return true; };
    std::unique_ptr<QDialog> box(makeCapturePermissionHelp(nullptr, ports)); box->show(); QCOMPARE(box->windowTitle(), QString("How to allow screen capture")); QCOMPARE(box->accessibleName(), box->windowTitle());
    for (const auto &needle : {"GNOME: Settings", "KDE Plasma: System Settings", "flatpak permission-reset"}) QVERIFY2(box->findChild<QLabel *>("capturePermissionHelpText")->text().contains(needle), needle);
    auto *open = box->findChild<QAbstractButton *>("openCaptureSettings"); QVERIFY(open); QCOMPARE(launches, 0);
    open->click(); QCOMPARE(launches, 1); QCOMPARE(program, gnome); QCOMPARE(arguments, QStringList{"privacy"}); QVERIFY(box->isVisible()); QVERIFY(box->findChild<QLabel *>("capturePermissionHelpStatus")->text().isEmpty());
  }
  void startFailureIsShownAndWithoutASettingsAppOnlyManualGuidanceIsOffered() {
    QTemporaryDir dir; executable(dir.path(), "systemsettings"); CapturePermissionHelpPorts ports; ports.searchPath = [&] { return QStringList{dir.path()}; }; ports.desktop = [] { return QString("KDE"); }; ports.launch = [](const QString &, const QStringList &) { return false; };
    std::unique_ptr<QDialog> failing(makeCapturePermissionHelp(nullptr, ports)); failing->show(); failing->findChild<QAbstractButton *>("openCaptureSettings")->click(); QVERIFY(failing->findChild<QLabel *>("capturePermissionHelpStatus")->text().contains("could not be started")); QVERIFY(failing->isVisible());
    int launches = 0; ports.searchPath = [] { return QStringList{}; }; ports.launch = [&](const QString &, const QStringList &) { ++launches; return true; };
    std::unique_ptr<QDialog> manual(makeCapturePermissionHelp(nullptr, ports)); QVERIFY(!manual->findChild<QAbstractButton *>("openCaptureSettings")); QVERIFY(manual->findChild<QLabel *>("capturePermissionHelpText")->text().contains("No desktop settings app was found")); QVERIFY(manual->findChild<QLabel *>("capturePermissionHelpText")->text().contains("flatpak permission-reset")); QCOMPARE(launches, 0);
  }
  void onlyOneGuidanceDialogIsEverShown() {
    CapturePermissionHelpPorts ports; ports.searchPath = [] { return QStringList{}; }; QWidget parent;
    showCapturePermissionHelp(&parent, ports); showCapturePermissionHelp(&parent, ports); showCapturePermissionHelp(&parent, ports);
    QCOMPARE(parent.findChildren<QDialog *>("capturePermissionHelpDialog").size(), 1);
    parent.findChild<QDialog *>("capturePermissionHelpDialog")->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click(); QTRY_VERIFY(parent.findChildren<QDialog *>("capturePermissionHelpDialog").isEmpty());
  }
};
QTEST_MAIN(CapturePermissionHelpTest)
#include "CapturePermissionHelpTest.moc"
