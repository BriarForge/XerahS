// IFCAT-017 on the real shell: the registry is built by the application's own
// feature installers (installShellFeatures), then every command is checked for
// a handler or an explicit unavailable explanation. Offscreen, temp profile.
#include "ShellFixture.h"
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <memory>

using namespace xerahs::app;

namespace {
struct Peek : QObject { using QObject::receivers; };
int receiversOf(const QObject *object, const char *signal) { return static_cast<const Peek *>(object)->receivers(signal); }
bool hasHandler(const QAction *action) { return action->menu() || receiversOf(action, SIGNAL(triggered(bool))) > 0; }
}

class ShellCommandsTest final : public QObject {
  Q_OBJECT
  QTemporaryDir root;
private slots:
  void initTestCase() { QVERIFY(root.isValid()); }
  void everyRegisteredCommandHasAHandlerOrAnExplicitUnavailableExplanation() {
    Shell shell(root.path()); shell.attach();
    const auto surfaces = {CommandSurface::Tray, CommandSurface::Window, CommandSurface::FileMenu, CommandSurface::CaptureMenu, CommandSurface::ToolsMenu, CommandSurface::PreferencesMenu};
    QSet<QString> ids; for (auto surface : surfaces) for (const auto &id : shell.commands.ids(surface)) ids.insert(id);
    QVERIFY2(ids.size() >= 30, qPrintable(QString::number(ids.size())));  // The real registry, not an empty one.
    int explained = 0;
    for (const auto &id : ids) {
      auto *action = shell.commands.action(id); QVERIFY2(action, qPrintable(id));
      QVERIFY2(!action->text().remove('&').trimmed().isEmpty(), qPrintable(id));
      if (hasHandler(action)) continue;
      // No handler: acceptable only as a disabled command that says why it is unavailable.
      ++explained;
      QVERIFY2(!action->isEnabled(), qPrintable(id + " is enabled but inert"));
      QVERIFY2(!action->toolTip().trimmed().isEmpty() && action->toolTip() != action->text() && !shell.commands.spec(id)->accessibleDescription.isEmpty(), qPrintable(id + " is inert without an explanation"));
    }
    QVERIFY2(explained <= 2, qPrintable(QString::number(explained)));  // Only genuinely unavailable integrations.
  }
  void windowButtonsAndTrayEntriesAreTheSharedWiredActions() {
    Shell shell(root.path()); shell.attach();
    for (auto *button : shell.window().findChildren<QToolButton *>()) {
      if (button->objectName().startsWith("qt_")) continue;  // Qt's own menu-bar overflow button.
      QVERIFY2(button->defaultAction(), qPrintable(button->objectName()));
      QVERIFY2(hasHandler(button->defaultAction()) || !button->defaultAction()->isEnabled(), qPrintable(button->objectName()));
    }
    for (auto *action : shell.menu.actions()) {
      if (action->isSeparator()) continue;
      QVERIFY2(hasHandler(action) || !action->isEnabled(), qPrintable(action->text()));
    }
    QVERIFY(shell.window().findChildren<QToolButton *>().size() >= 10);
    QVERIFY(shell.menu.actions().size() >= 10);
  }
  // Opening each tool through its registry command creates the production dialog; none may hold an
  // enabled button without a handler (a visible but inert control).
  void noEnabledButtonInAnOpenedToolDialogIsInert() {
    Shell shell(root.path()); shell.attach();
    int dialogs = 0, buttons = 0;
    for (const char *id : {"hash-checker", "qr-code", "icon-converter", "video-converter", "scan-qr", "recognize-text"}) {
      auto *action = shell.commands.action(id); QVERIFY2(action, id);
      if (!action->isEnabled()) continue;  // Unavailable: covered by the explanation test.
      action->trigger();
      QDialog *dialog = nullptr;
      for (auto *widget : QApplication::topLevelWidgets()) if (auto *candidate = qobject_cast<QDialog *>(widget); candidate && candidate->isVisible() && !candidate->property("scanned").toBool()) dialog = candidate;
      QVERIFY2(dialog, id); dialog->setProperty("scanned", true); ++dialogs;
      for (auto *button : dialog->findChildren<QAbstractButton *>()) {
        if (!button->isVisibleTo(dialog) || !button->isEnabled() || button->isCheckable() || button->objectName().startsWith("qt_")) continue;
        ++buttons;
        const bool wired = receiversOf(button, SIGNAL(clicked(bool))) > 0 || (qobject_cast<QToolButton *>(button) && qobject_cast<QToolButton *>(button)->menu());
        QVERIFY2(wired, qPrintable(QString("%1 / %2 / '%3' is enabled but inert").arg(id, button->objectName(), button->text())));
      }
    }
    QVERIFY(dialogs >= 4); QVERIFY(buttons >= 8);
    for (auto *widget : QApplication::topLevelWidgets()) if (qobject_cast<QDialog *>(widget)) widget->close();
  }
};

int main(int argc, char **argv) {
  QTemporaryDir profile; qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) qputenv(key, profile.filePath(key).toUtf8());
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + profile.filePath("no-bus")).toUtf8());
  QApplication app(argc, argv); QApplication::setQuitOnLastWindowClosed(false);
  ShellCommandsTest test; return QTest::qExec(&test, argc, argv);
}
#include "ShellCommandsTest.moc"
