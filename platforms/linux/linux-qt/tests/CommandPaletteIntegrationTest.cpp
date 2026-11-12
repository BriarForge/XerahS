// CORE-007 on the real shell: installShellFeatures fills the registry, and the
// palette runs those same QActions. Capture last region must produce the real
// last-region-invalid result, and Review recording must open the real result
// dialog only once the command is available. Nothing here fakes a handler.
//
// These tests construct CommandPalette directly. The palette command and shortcut
// are a separate hook; this file does not require them.
#include "CommandPalette.h"
#include "ShellFixture.h"

#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

using namespace xerahs::app;

namespace {

struct Palette {
  QLineEdit *search = nullptr;
  QListWidget *commands = nullptr;
  QPushButton *run = nullptr;
  QLabel *status = nullptr;
};

struct Probe {
  QWidget *palette = nullptr;
  bool insideExec = false;
  bool ranDuringExec = false;
  bool ranWhileModal = false;
};

Palette bind(QWidget &palette) {
  return {palette.findChild<QLineEdit *>(QStringLiteral("commandPaletteSearch")),
      palette.findChild<QListWidget *>(QStringLiteral("commandPaletteCommands")),
      palette.findChild<QPushButton *>(QStringLiteral("runCommand")),
      palette.findChild<QLabel *>(QStringLiteral("commandPaletteStatus"))};
}

bool ready(const Palette &ui) { return ui.search && ui.commands && ui.run && ui.status; }

QString withoutMnemonic(const QString &text) {
  QString out;
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i) != QLatin1Char('&')) {
      out.append(text.at(i));
      continue;
    }
    if (i + 1 >= text.size()) {
      out.append(text.at(i));
      break;
    }
    if (text.at(i + 1) == QLatin1Char('&')) {
      out.append(QLatin1Char('&'));
      ++i;
    }
  }
  return out;
}

QStringList visibleIds(const QListWidget *list) {
  QStringList ids;
  for (int row = 0; row < list->count(); ++row) {
    if (list->isRowHidden(row)) continue;
    ids.append(list->item(row)->data(CommandPalette::CommandIdRole).toString());
  }
  return ids;
}

QStringList sorted(QStringList ids) {
  std::sort(ids.begin(), ids.end());
  return ids;
}

const CommandSurface kSurfaces[] = {CommandSurface::Tray, CommandSurface::Window, CommandSurface::FileMenu,
    CommandSurface::CaptureMenu, CommandSurface::ToolsMenu, CommandSurface::PreferencesMenu};

QStringList shellIds(const CommandRegistry &commands) {
  QSet<QString> seen;
  QStringList ids;
  for (auto surface : kSurfaces) {
    for (const auto &id : commands.ids(surface)) {
      if (seen.contains(id)) continue;
      auto *action = commands.action(id);
      if (!action || !action->isVisible()) continue;
      seen.insert(id);
      ids.append(id);
    }
  }
  return ids;
}

QStringList matching(const CommandRegistry &commands, const QString &query) {
  const auto needle = query.trimmed();
  QStringList ids;
  for (const auto &id : shellIds(commands)) {
    auto *action = commands.action(id);
    const auto *spec = commands.spec(id);
    const auto live = withoutMnemonic(action->text());
    const auto registered = spec ? withoutMnemonic(spec->label) : QString();
    if (needle.isEmpty() || live.contains(needle, Qt::CaseInsensitive) || registered.contains(needle, Qt::CaseInsensitive)
        || id.contains(needle, Qt::CaseInsensitive))
      ids.append(id);
  }
  return ids;
}

void flushDeferred() { QTest::qWait(50); }

void arm(QAction *action, QObject *context, Probe *probe) {
  QObject::connect(action, &QAction::triggered, context, [probe] {
    probe->ranDuringExec = probe->insideExec;
    probe->ranWhileModal = probe->palette->isVisible() || probe->palette->isModal()
        || probe->palette->windowModality() != Qt::NonModal || probe->palette->testAttribute(Qt::WA_ShowModal)
        || QApplication::activeModalWidget() == probe->palette;
  });
}

int execFiltered(CommandPalette &palette, const QString &id, Probe *probe) {
  QTimer safety;
  safety.setSingleShot(true);
  QObject::connect(&safety, &QTimer::timeout, &palette, [&palette] {
    if (palette.isVisible()) palette.reject();
  });
  QTimer::singleShot(0, &palette, [&palette, id] {
    auto *search = palette.findChild<QLineEdit *>(QStringLiteral("commandPaletteSearch"));
    auto *list = palette.findChild<QListWidget *>(QStringLiteral("commandPaletteCommands"));
    if (!search || !list) {
      palette.reject();
      return;
    }
    search->setFocus();
    search->setText(id);
    if (visibleIds(list) != QStringList{id}) {
      palette.reject();
      return;
    }
    QTest::keyClick(search, Qt::Key_Return);
  });
  safety.start(1500);
  if (probe) probe->insideExec = true;
  const int code = palette.exec();
  if (probe) probe->insideExec = false;
  safety.stop();
  flushDeferred();
  return code;
}

QDialog *recordingResult() {
  for (auto *widget : QApplication::topLevelWidgets())
    if (widget->objectName() == QLatin1String("recordingResultDialog")) return qobject_cast<QDialog *>(widget);
  return nullptr;
}

}  // namespace

class CommandPaletteIntegrationTest final : public QObject {
  Q_OBJECT
private slots:
  void realCommandsAreListedOnceAndFoundByLabelOrStableId() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Shell shell(root.path());
    shell.attach();
    CommandPalette palette(shell.commands);
    const auto ui = bind(palette);
    QVERIFY2(ready(ui), "commandPaletteSearch, commandPaletteCommands, runCommand, commandPaletteStatus");
    palette.show();
    const auto registered = shellIds(shell.commands);
    QVERIFY2(registered.size() >= 30, qPrintable(QString::number(registered.size())));
    QCOMPARE(sorted(visibleIds(ui.commands)), sorted(registered));

    ui.search->setText(QStringLiteral("capture-last-region"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-last-region")}));
    QCOMPARE(ui.commands->currentItem()->data(CommandPalette::CommandIdRole).toString(), QStringLiteral("capture-last-region"));
    QVERIFY(ui.run->isEnabled());

    ui.search->setText(QStringLiteral("LAST REGION"));
    QCOMPARE(sorted(visibleIds(ui.commands)), sorted(matching(shell.commands, QStringLiteral("LAST REGION"))));
    QVERIFY(visibleIds(ui.commands).contains(QStringLiteral("capture-last-region")));

    auto *action = shell.commands.action(QStringLiteral("capture-last-region"));
    auto *button = shell.window().findChild<QToolButton *>(QStringLiteral("capture-last-region"));
    QVERIFY(action);
    QVERIFY(button);
    QCOMPARE(button->defaultAction(), action);
  }

  void captureLastRegionDispatchesTheSharedHandlerAfterThePaletteReleases() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Shell shell(root.path());
    shell.attach();
    auto *action = shell.commands.action(QStringLiteral("capture-last-region"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    CommandPalette palette(shell.commands);
    QVERIFY(ready(bind(palette)));
    QSignalSpy ran(action, &QAction::triggered);
    QSignalSpy notices(&shell.capture, &CaptureController::notify);
    Probe probe;
    probe.palette = &palette;
    QObject watcher;
    arm(action, &watcher, &probe);
    QCOMPARE(execFiltered(palette, QStringLiteral("capture-last-region"), &probe), int(QDialog::Accepted));
    QCOMPARE(ran.count(), 1);
    QVERIFY(!probe.ranDuringExec);
    QVERIFY(!probe.ranWhileModal);
    QCOMPARE(notices.size(), 1);
    QVERIFY(notices.at(0).at(1).toString().contains(QStringLiteral("last-region-invalid")));
    QVERIFY(notices.at(0).at(1).toString().startsWith(QStringLiteral("Workflow: failed. Output references: none")));
    QVERIFY(!shell.capture.lastPipelineResult());
    QCOMPARE(shell.window().findChild<QToolButton *>(QStringLiteral("capture-last-region"))->defaultAction(), action);
  }

  void disabledReviewStaysInertUntilRecordingMakesItAvailable() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Shell shell(root.path());
    shell.attach();
    auto *review = shell.commands.action(QStringLiteral("review-recording"));
    QVERIFY(review);
    QVERIFY(!review->isEnabled());
    CommandPalette palette(shell.commands);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    ui.search->setText(QStringLiteral("review-recording"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("review-recording")}));
    QVERIFY(!ui.run->isEnabled());
    QVERIFY(ui.status->text().contains(QStringLiteral("is unavailable")));
    QVERIFY(!ui.status->text().contains(QStringLiteral("success"), Qt::CaseInsensitive));
    QSignalSpy ran(review, &QAction::triggered);
    ui.run->click();
    QTest::keyClick(ui.search, Qt::Key_Return);
    flushDeferred();
    QCOMPARE(ran.count(), 0);
    QVERIFY(palette.isVisible());
    QVERIFY(!recordingResult());

    xerahs::recording::Request request;
    request.frameRate = 0;
    shell.recording.start(request);
    QVERIFY2(review->isEnabled(), "recording failure did not make review available");
    QVERIFY(ui.run->isEnabled());
    QVERIFY(!ui.status->text().contains(QStringLiteral("is unavailable")));

    Probe probe;
    probe.palette = &palette;
    QObject watcher;
    arm(review, &watcher, &probe);
    QCOMPARE(execFiltered(palette, QStringLiteral("review-recording"), &probe), int(QDialog::Accepted));
    QCOMPARE(ran.count(), 1);
    QVERIFY(!probe.ranDuringExec);
    QVERIFY(!probe.ranWhileModal);
    auto *opened = recordingResult();
    QVERIFY(opened);
    QVERIFY(opened->isVisible());
    QVERIFY(!palette.isVisible());
    QVERIFY(QApplication::activeModalWidget() != &palette);
    opened->close();
  }

  void escapeDoesNotDispatchARealCommand() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    Shell shell(root.path());
    shell.attach();
    auto *action = shell.commands.action(QStringLiteral("capture-last-region"));
    QVERIFY(action);
    CommandPalette palette(shell.commands);
    const auto ui = bind(palette);
    QVERIFY(ready(ui));
    palette.show();
    ui.search->setText(QStringLiteral("capture-last-region"));
    QCOMPARE(visibleIds(ui.commands), QStringList({QStringLiteral("capture-last-region")}));
    QSignalSpy ran(action, &QAction::triggered);
    QSignalSpy notices(&shell.capture, &CaptureController::notify);
    ui.search->setFocus();
    QTest::keyClick(ui.search, Qt::Key_Escape);
    flushDeferred();
    QCOMPARE(ran.count(), 0);
    QCOMPARE(notices.size(), 0);
    QVERIFY(!shell.capture.lastPipelineResult());
    QCOMPARE(palette.result(), int(QDialog::Rejected));
    QVERIFY(ui.status->text().contains(QStringLiteral("No command was started")));
  }
};

int main(int argc, char **argv) {
  QTemporaryDir profile;
  if (!profile.isValid()) return 2;
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
    qputenv(key, profile.filePath(QString::fromLatin1(key)).toUtf8());
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + profile.filePath(QStringLiteral("no-bus"))).toUtf8());
  QApplication app(argc, argv);
  QApplication::setQuitOnLastWindowClosed(false);
  CommandPaletteIntegrationTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "CommandPaletteIntegrationTest.moc"
