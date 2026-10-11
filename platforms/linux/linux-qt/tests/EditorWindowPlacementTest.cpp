#include "image-editor/EditorWindowPlacement.h"
#include <QTest>
#include <limits>
using namespace xerahs::editor;

class EditorWindowPlacementTest : public QObject {
  Q_OBJECT
private slots:
  void analyticalPlacement_data() {
    QTest::addColumn<QRectF>("requested"); QTest::addColumn<QRectF>("display");
    QTest::addColumn<QMarginsF>("margins"); QTest::addColumn<QRectF>("expected");
    QTest::newRow("visible-position") << QRectF(100, 100, 640, 480) << QRectF(0, 30, 1280, 690) << QMarginsF() << QRectF(100, 100, 640, 480);
    QTest::newRow("partial-right-edge") << QRectF(1100, 100, 640, 480) << QRectF(0, 30, 1280, 690) << QMarginsF() << QRectF(640, 100, 640, 480);
    QTest::newRow("disconnected-display") << QRectF(2500, 100, 640, 480) << QRectF(0, 30, 1280, 690) << QMarginsF() << QRectF(320, 135, 640, 480);
    QTest::newRow("negative-display-origin") << QRectF(2500, 100, 640, 480) << QRectF(-1280, -720, 1280, 690) << QMarginsF() << QRectF(-960, -615, 640, 480);
    QTest::newRow("changed-logical-scale") << QRectF(0, 0, 1280, 720) << QRectF(0, 0, 960, 540) << QMarginsF() << QRectF(0, 0, 960, 540);
    QTest::newRow("decorated-oversize") << QRectF(0, 0, 1800, 1200) << QRectF(0, 30, 1280, 690) << QMarginsF(4, 28, 4, 4) << QRectF(4, 58, 1272, 658);
    QTest::newRow("small-display-overrides-minimum") << QRectF(0, 0, 800, 600) << QRectF(0, 30, 320, 210) << QMarginsF(4, 28, 4, 4) << QRectF(4, 58, 312, 178);
    QTest::newRow("minimum-size") << QRectF(50, 50, 1, 1) << QRectF(0, 0, 1280, 720) << QMarginsF() << QRectF(50, 50, 400, 300);
    QTest::newRow("fractional-saved-size") << QRectF(100, 100, 640.75, 480.5) << QRectF(0, 0, 1280, 720) << QMarginsF() << QRectF(100, 100, 640, 480);
  }
  void analyticalPlacement() {
    QFETCH(QRectF, requested); QFETCH(QRectF, display); QFETCH(QMarginsF, margins); QFETCH(QRectF, expected);
    const auto placement = recoverEditorWindowPlacement(requested, {display}, 0, margins); QVERIFY(placement);
    QCOMPARE(placement->clientBounds, expected); QCOMPARE(placement->displayIndex, 0);
    QVERIFY(display.contains(placement->frameBounds));
    const auto again = recoverEditorWindowPlacement(placement->clientBounds, {display}, 0, margins); QVERIFY(again);
    QCOMPARE(again->clientBounds, expected);
  }
  void bestVisibleDisplayWinsAndRecoveryUsesPreferredDisplay() {
    const QList<QRectF> displays{{-1280, 0, 1280, 720}, {0, 30, 1920, 1050}};
    auto visible = recoverEditorWindowPlacement(QRectF(-200, 100, 640, 480), displays); QVERIFY(visible);
    QCOMPARE(visible->displayIndex, 1); QCOMPARE(visible->clientBounds, QRectF(0, 100, 640, 480));
    auto missing = recoverEditorWindowPlacement(QRectF(3000, 2000, 640, 480), displays, 0); QVERIFY(missing);
    QCOMPARE(missing->displayIndex, 0); QCOMPARE(missing->clientBounds, QRectF(-960, 120, 640, 480));
    auto primary = recoverEditorWindowPlacement(QRectF(3000, 2000, 640, 480), displays, 1); QVERIFY(primary);
    QCOMPARE(primary->displayIndex, 1); QCOMPARE(primary->clientBounds, QRectF(640, 315, 640, 480));
  }
  void malformedAndUnavailableDisplaysNeverProduceUnsafePlacement() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const QList<QRectF> displays{{0, 0, 0, 720}, {nan, 0, 1280, 720}, {0, 0, 1280, 720}};
    const auto recovered = recoverEditorWindowPlacement(QRectF(nan, nan, nan, -1), displays, 0); QVERIFY(recovered);
    QCOMPARE(recovered->displayIndex, 2); QCOMPARE(recovered->clientBounds, QRectF(0, 0, 1280, 720));
    QVERIFY(!recoverEditorWindowPlacement(QRectF(0, 0, 640, 480), {}));
    QVERIFY(!recoverEditorWindowPlacement(QRectF(0, 0, 640, 480), {{1e300, 0, 1280, 720}}));
    QVERIFY(!recoverEditorWindowPlacement(QRectF(0, 0, 640, 480), {{0, 0, 1, 1}}, 0, {2, 2, 2, 2}));
    QVERIFY(!recoverEditorWindowPlacement(QRectF(0, 0, 640, 480), displays, 0, {nan, 0, 0, 0}));
    QVERIFY(!recoverEditorWindowPlacement(QRectF(0, 0, 640, 480), displays, 0, {-1, 0, 0, 0}));
  }
};
QTEST_GUILESS_MAIN(EditorWindowPlacementTest)
#include "EditorWindowPlacementTest.moc"
