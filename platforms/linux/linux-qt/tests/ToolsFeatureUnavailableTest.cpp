// Compile ToolsFeature without the optional runner to exercise the actual missing-host build.
#include "features/ToolsFeature.h"
#include <QMenu>
#include <QTest>

using namespace xerahs::app;
class ToolsFeatureUnavailableTest final : public QObject {
  Q_OBJECT
private slots:
  void converterStaysVisibleWithAnActionableDependencyExplanation() {
    CommandRegistry commands;
    installToolDialogs(commands, {});
    auto *converter = commands.action("video-converter");
    QVERIFY(converter); QVERIFY(!converter->isEnabled());
    QVERIFY(converter->text().contains("requires GStreamer"));
    QVERIFY(converter->toolTip().contains("core, base and pbutils"));
    QVERIFY(converter->statusTip().contains("install"));
    QCOMPARE(commands.spec("video-converter")->accessibleDescription, converter->toolTip());
    for (const auto surface : {CommandSurface::Tray, CommandSurface::Window, CommandSurface::ToolsMenu}) {
      QVERIFY(commands.actions(surface).contains(converter));
      QMenu menu; commands.populate(&menu, surface);
      QVERIFY(menu.actions().contains(converter)); QVERIFY(converter->isVisible());
    }
  }
};
QTEST_MAIN(ToolsFeatureUnavailableTest)
#include "ToolsFeatureUnavailableTest.moc"
