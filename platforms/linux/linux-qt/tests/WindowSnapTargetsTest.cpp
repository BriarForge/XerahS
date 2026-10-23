#include "capture/WindowSnapTargets.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
using namespace xerahs::capture;
namespace {
const QByteArray monitors = R"([{"id":0,"name":"left","activeWorkspace":{"id":1},"specialWorkspace":{"id":-99}},
{"id":1,"name":"right","activeWorkspace":{"id":2},"specialWorkspace":{"id":0}}])";
Topology topology() {
  return {{"left", {0, 0, 1000, 800}, {-1000, 0, 0, 800}, 1},
          {"right", {1000, 100, 2500, 1300}, {0, 0, 1000, 800}, 1.5}};
}
QJsonObject window(int x = -900, int y = 100, int width = 200, int height = 100) {
  return {{"mapped", true}, {"hidden", false}, {"pid", 123}, {"monitor", 0},
          {"workspace", QJsonObject{{"id", 1}}}, {"at", QJsonArray{x, y}},
          {"size", QJsonArray{width, height}}, {"floating", false}, {"fullscreen", 0}, {"focusHistoryID", 0}};
}
std::vector<SnapTarget> discover(const QJsonArray &windows, const QByteArray &screens = monitors) {
  return hyprlandSnapTargets(QJsonDocument(windows).toJson(), screens, topology(), 999);
}
}
class WindowSnapTargetsTest : public QObject {
  Q_OBJECT
private slots:
  void visibilityFiltering() {
    QJsonArray clients{window()};
    auto w = window(); w["mapped"] = false; clients.append(w);
    w = window(); w["hidden"] = true; clients.append(w);
    w = window(); w["workspace"] = QJsonObject{{"id", 3}}; clients.append(w);
    w = window(); w["size"] = QJsonArray{0, 100}; clients.append(w);
    w = window(); w["size"] = QJsonArray{100, -1}; clients.append(w);
    w = window(); w["monitor"] = 8; clients.append(w);
    w = window(); w["workspace"] = QJsonObject{{"id", 2}}; clients.append(w);
    const auto targets = discover(clients);
    QCOMPARE(targets.size(), std::size_t(1));
    QCOMPARE(targets[0].physical, (PhysicalRect{100, 100, 300, 200}));
    QVERIFY(discover({window()}, R"([{"id":0,"name":"left","disabled":true,"activeWorkspace":{"id":1}}])").empty());
  }
  void specialWorkspace() {
    auto w = window(); w["workspace"] = QJsonObject{{"id", -99}};
    QCOMPARE(discover({w}).size(), std::size_t(1));
    w["workspace"] = QJsonObject{{"id", -98}}; QVERIFY(discover({w}).empty());
    w["monitor"] = 1; w["workspace"] = QJsonObject{{"id", 0}}; QVERIFY(discover({w}).empty());
  }
  void ownPidExclusion() {
    auto own = window(); own["pid"] = 999;
    QVERIFY(discover({own}).empty());
    QCOMPARE(discover({own, window()}).size(), std::size_t(1));
  }
  void orderingAndClick_data() {
    QTest::addColumn<bool>("quick");
    QTest::newRow("explicit") << false; QTest::newRow("quick") << true;
  }
  void orderingAndClick() {
    QFETCH(bool, quick);
    auto tiled = window(-1000, 0, 900, 700);
    auto floatOld = window(-990, 10, 800, 600); floatOld["floating"] = true; floatOld["focusHistoryID"] = 8;
    auto floatNew = window(-980, 20, 700, 500); floatNew["floating"] = true; floatNew["focusHistoryID"] = 1;
    auto fullOld = window(-970, 30, 600, 400); fullOld["fullscreen"] = 2; fullOld["focusHistoryID"] = 6;
    auto fullNew = window(-960, 40, 500, 300); fullNew["fullscreen"] = 1; fullNew["focusHistoryID"] = 3;
    const auto targets = discover({floatOld, tiled, fullOld, floatNew, fullNew});
    QCOMPARE(targets.size(), std::size_t(5));
    for (std::size_t i = 0; i < targets.size(); ++i) QCOMPARE(targets[i].physical.left, qint64(40) - qint64(10 * i));
    RegionSession session(topology(), {quick, true}, targets, {}); session.resumeAt(SessionState::Idle);
    const PhysicalPoint point{100, 100};
    const auto expected = std::optional<PhysicalRect>(PhysicalRect{40, 40, 540, 340});
    QCOMPARE(session.snapPreviewAt(point), expected); QVERIFY(!session.selection());
    session.keyEnter(); QCOMPARE(session.state(), SessionState::Idle);
    session.pointerDown(point); QVERIFY(!session.snapPreviewAt(point)); session.pointerUp(point);
    QCOMPARE(session.selection(), expected);
    QCOMPARE(session.state(), quick ? SessionState::Confirming : SessionState::Selected);
    if (!quick) { session.keyEnter(); QCOMPARE(session.state(), SessionState::Confirming); }
  }
  void mixedScaleMappingAndClamping() {
    auto w = window(11, 21, 100, 101); w["monitor"] = 1; w["workspace"] = QJsonObject{{"id", 2}};
    auto targets = discover({window(), w}); QCOMPARE(targets.size(), std::size_t(2));
    QCOMPARE(targets[0].physical, (PhysicalRect{100, 100, 300, 200}));
    QCOMPARE(targets[1].physical, (PhysicalRect{1016, 131, 1166, 283}));
    w["at"] = QJsonArray{-10, -20}; w["size"] = QJsonArray{1200, 1000};
    targets = discover({w}); QCOMPARE(targets.size(), std::size_t(1));
    QCOMPARE(targets[0].physical, (PhysicalRect{1000, 100, 2500, 1300}));
    w["at"] = QJsonArray{1100, 0}; QVERIFY(discover({w}).empty());
    w["at"] = QJsonArray{0.6666664, 0}; w["size"] = QJsonArray{1, 1};
    targets = discover({w});
    QCOMPARE(targets[0].physical, (PhysicalRect{1001, 100, 1002, 101}));
    w["at"] = QJsonArray{1e100, 0}; QVERIFY(discover({w}).empty());
    targets = discover({window(-100, 0, 300, 100)});
    QCOMPARE(targets[0].physical, (PhysicalRect{900, 0, 1000, 100}));
  }
  void malformedJson() {
    for (const auto &json : {QByteArray("["), QByteArray("{}"), QByteArray("null"), QByteArray("garbage")}) {
      QVERIFY(hyprlandSnapTargets(json, monitors, topology(), 999).empty());
      QVERIFY(hyprlandSnapTargets("[]", json, topology(), 999).empty());
    }
    auto w = window(); w["at"] = QJsonArray{"bad", 0}; QVERIFY(discover({w, QJsonObject{}}).empty());
    w = window(); w.remove("workspace"); QVERIFY(discover({w}).empty());
    w = window(); w.remove("hidden"); QVERIFY(discover({w}).empty());
  }
  void disabledSnappingAndDragOverride() {
    const auto targets = discover({window()});
    RegionSession disabled(topology(), {true, false}, targets, {}); disabled.resumeAt(SessionState::Idle);
    QVERIFY(!disabled.snapPreviewAt({150, 150}));
    disabled.pointerDown({150, 150}); disabled.pointerUp({150, 150});
    QCOMPARE(disabled.state(), SessionState::Idle); QVERIFY(!disabled.selection());
    RegionSession drag(topology(), {false, true}, targets, {}); drag.resumeAt(SessionState::Idle);
    QVERIFY(drag.snapPreviewAt({150, 150})); drag.pointerDown({150, 150}); drag.pointerMove({160, 160});
    QVERIFY(!drag.snapPreviewAt({160, 160})); drag.pointerUp({160, 160});
    QCOMPARE(drag.selection(), std::optional<PhysicalRect>(PhysicalRect{150, 150, 160, 160}));
  }
};
QTEST_GUILESS_MAIN(WindowSnapTargetsTest)
#include "WindowSnapTargetsTest.moc"
