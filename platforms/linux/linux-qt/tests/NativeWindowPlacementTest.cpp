#include "ApplicationWindow.h"
#include "CaptureHistoryWindow.h"
#include "NativeEditorPreferences.h"
#include "NativeEditorWindowState.h"
#include "NativeWindowPlacement.h"
#include "PipelineHistoryDialog.h"
#include "SettingsDialog.h"
#include "WorkflowExecutionHistoryDialog.h"
#include <QApplication>
#include <QDirIterator>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <memory>
using namespace xerahs::app;
class NativeWindowPlacementTest : public QObject {
  Q_OBJECT
private slots:
  void transientSurfaces_data() {
    QTest::addColumn<QString>("surface");
    for (const auto *name : {"shell", "settings", "capture-history", "pipeline-history", "workflow-history"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void transientSurfaces() {
    QFETCH(QString, surface);
    QTemporaryDir dir;
    xerahs::settings::ApplicationSettingsStore settings(dir.filePath("settings.json"), dir.path());
    WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    auto create = [&]() -> QWidget * {
      if (surface == "shell") return new ApplicationWindow(QMap<QString, QAction *>{});
      if (surface == "settings") return new SettingsDialog(settings, dir.path(), {}, shortcuts, [] {});
      if (surface == "capture-history") return new CaptureHistoryWindow(journal, [] { return false; }, {});
      if (surface == "pipeline-history") return new PipelineHistoryDialog(journal, [] { return false; });
      return new WorkflowExecutionHistoryDialog({[] { return WorkflowExecutionListing{}; }, {}}, [] { return false; });
    };
    auto window = std::unique_ptr<QWidget>(create());
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    QVERIFY(window->findChild<QObject *>("nativeWindowPlacement"));
    window->show(); QTest::qWait(20); const auto freshSize = window->size();
    // Replace only display input; exercise the production observer and real UI.
    delete window->findChild<QObject *>("nativeWindowPlacement");
    QList<QRectF> areas{QRectF(0, 24, 1920, 1056), QRectF(1920, 0, 1920, 1080)};
    NativeWindowPlacement placement(*window, [&] { return areas; });
    window->move(2400, 200); placement.recover();
    QVERIFY(areas[1].contains(QRectF(window->frameGeometry())));
    areas = {QRectF(-1280, 30, 1280, 994)}; placement.recover();
    QVERIFY(areas[0].contains(QRectF(window->frameGeometry())));
    areas = {QRectF(0, 40, 1024, 728)}; placement.recover();
    QVERIFY2(areas[0].contains(QRectF(window->frameGeometry())), qPrintable(surface));
    auto checkTargets = [&] {
      for (auto *target : window->findChildren<QWidget *>()) {
        if (!target->isVisible() || !target->isEnabled() || !(target->focusPolicy() & Qt::TabFocus)) continue;
        target->setFocus(Qt::TabFocusReason); QCoreApplication::processEvents();
        QVERIFY2(!target->visibleRegion().isEmpty(), qPrintable(target->accessibleName()));
        const auto visible = target->visibleRegion().boundingRect();
        QVERIFY(areas[0].contains(QRectF(QRect(target->mapToGlobal(visible.topLeft()), visible.size()))));
      }
    };
    if (auto *tabs = window->findChild<QTabWidget *>()) {
      for (int i = 0; i < tabs->count(); ++i) { tabs->setCurrentIndex(i); checkTargets(); }
    } else checkTargets();
    bool closeFound = false;
    for (auto *button : window->findChildren<QPushButton *>()) {
      if (!button->isVisible() || !button->isEnabled()) continue;
      if (button->text().contains("Close") || button->text().contains("Cancel")) {
        closeFound = true;
        QVERIFY(window->rect().contains(QRect(button->mapTo(window.get(), QPoint()), button->size())));
        button->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), button);
      }
    }
    QVERIFY(surface == "shell" || surface == "capture-history" || closeFound);
    const auto before = window->geometry(); areas.clear(); placement.recover(); QCOMPARE(window->geometry(), before);
    QVERIFY(window->close()); QVERIFY(!window->isVisible());
    auto reopened = std::unique_ptr<QWidget>(create()); reopened->setAttribute(Qt::WA_DeleteOnClose, false);
    reopened->show(); QTest::qWait(20); QCOMPARE(reopened->size(), freshSize);
    QVERIFY(!QFile::exists(dir.filePath("settings.json")));
    QDirIterator files(dir.path(), QDir::Files, QDirIterator::Subdirectories); QVERIFY(!files.hasNext());
  }
  void editorSnapshotHasOnlyPortablePlacementFields() {
    initializeNativeEditorPreferences();
    QWidget window; NativeEditorWindowState state(window, {640, 480});
    window.show(); window.winId(); window.setProperty("requestId", "transient");
    auto keys = state.snapshot().keys(); keys.sort();
    QCOMPARE(keys, QStringList({"IsWindowMaximized", "WindowHeight", "WindowWidth"}));
    window.showMinimized(); keys = state.snapshot().keys(); keys.sort();
    QCOMPARE(keys, QStringList({"IsWindowMaximized", "WindowHeight", "WindowWidth"}));
  }
};
QTEST_MAIN(NativeWindowPlacementTest)
#include "NativeWindowPlacementTest.moc"
