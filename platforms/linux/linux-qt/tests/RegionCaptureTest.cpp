// Production-path checks that the conformance vectors cannot cover: pixel
// placement and resampling rejection in the compositor, last-region round
// trips, restricted permission, and the outcome published to the caller.

#include "capture/RegionGeometry.h"
#include "capture/RegionSession.h"

#include <QTest>
#include <limits>
#include <stdexcept>

using namespace xerahs::capture;

namespace {

Topology twoDisplays() {
  return {Display{QStringLiteral("a"), {0, 0, 4, 2}, {0, 0, 4, 2}, 1.0},
          Display{QStringLiteral("b"), {4, 0, 8, 4}, {4, 0, 8, 4}, 1.0}};
}

// Each source pixel encodes its display and physical position.
std::optional<RgbaImage> labelled(const Display &display, const PhysicalRect &source) {
  RgbaImage image{source.width(), source.height(), QByteArray(static_cast<qsizetype>(source.width() * source.height() * 4), '\0')};
  for (qint64 y = 0; y < source.height(); ++y) {
    for (qint64 x = 0; x < source.width(); ++x) {
      char *pixel = image.pixels.data() + (y * source.width() + x) * 4;
      pixel[0] = static_cast<char>(source.left + x);
      pixel[1] = static_cast<char>(source.top + y);
      pixel[2] = display.id == QStringLiteral("a") ? 1 : 2;
      pixel[3] = static_cast<char>(0xff);
    }
  }
  return image;
}

}  // namespace

class RegionCaptureTest : public QObject {
  Q_OBJECT

private slots:
  // RC-012: pixels land at their physical offset, once, in desktop order.
  void compositePlacesPixelsByPhysicalPosition() {
    const CompositeResult result = composite({2, 0, 6, 3}, twoDisplays(), labelled);
    QVERIFY(result.image);
    QCOMPARE(result.sourceDisplays, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    const auto at = [&](qint64 x, qint64 y) { return result.image->pixels.constData() + (y * 4 + x) * 4; };
    QCOMPARE(int(at(0, 0)[0]), 2);  // physical (2, 0) on a
    QCOMPARE(int(at(0, 0)[2]), 1);
    QCOMPARE(int(at(2, 2)[0]), 4);  // physical (4, 2) on b
    QCOMPARE(int(at(2, 2)[2]), 2);
    QCOMPARE(int(at(0, 2)[3]), 0);  // physical (2, 2): no display, transparent black
    QCOMPARE(transparentBlackPixels(*result.image), qint64{2});
  }

  // RC-012: a source of the wrong size would need resampling, so it fails.
  void compositeRejectsResampledSource() {
    const CompositeResult result = composite({0, 0, 4, 2}, twoDisplays(), [](const Display &, const PhysicalRect &) {
      return std::optional<RgbaImage>(RgbaImage{2, 1, QByteArray(8, '\x7f')});
    });
    QVERIFY(!result.image);
    QCOMPARE(result.diagnostic, std::optional<QString>(diagnostic::captureSourceFailed));
  }
  void compositionCancellationDiscardsAllPartialPixels_data() {
    QTest::addColumn<int>("cancelAt");
    for (int percent : {0, 20, 50, 80, 100}) QTest::newRow(qPrintable(QString::number(percent))) << percent;
  }
  void compositionCancellationDiscardsAllPartialPixels() {
    QFETCH(int, cancelAt); bool cancelled = false; int last = -1;
    const auto result = composite({2, 0, 6, 3}, twoDisplays(), labelled, {[&] { return cancelled; }, [&](int percent) {
      QVERIFY(percent >= last); QVERIFY(percent >= 0 && percent <= 100); last = percent;
      if (percent >= cancelAt) cancelled = true;
    }});
    QVERIFY(result.cancelled); QVERIFY(!result.image); QVERIFY(!result.diagnostic); QVERIFY(result.sourceDisplays.isEmpty());
  }
  void progressCompletesWithoutChangingPhysicalPixels() {
    QList<int> progress;
    const auto result = composite({2, 0, 6, 3}, twoDisplays(), labelled, {{}, [&](int percent) { progress.append(percent); }});
    QVERIFY(result.image); QVERIFY(!result.cancelled); QVERIFY(!result.diagnostic);
    QCOMPARE(progress.first(), 0); QCOMPARE(progress.last(), 100);
    QCOMPARE(transparentBlackPixels(*result.image), qint64(2));
    const auto pixel = result.image->pixels.mid((2 * 4 + 2) * 4, 4);
    QCOMPARE(pixel, QByteArray::fromHex("040202ff"));
  }
  void excessiveAndOverflowingRectanglesFailBeforeSourceAccess() {
    int calls = 0;
    const auto source = [&](const Display &d, const PhysicalRect &r) { ++calls; return labelled(d, r); };
    for (const auto &rectangle : {PhysicalRect{0, 0, 100001, 1}, PhysicalRect{0, 0, 100000, 100000},
        PhysicalRect{std::numeric_limits<qint64>::min(), 0, std::numeric_limits<qint64>::max(), 1}}) {
      const auto result = composite(rectangle, twoDisplays(), source);
      QVERIFY(!result.image); QVERIFY(!result.cancelled); QCOMPARE(result.diagnostic, std::optional<QString>(diagnostic::captureSourceFailed));
    }
    QCOMPARE(calls, 0);
    const Topology invalid{{"broken", {std::numeric_limits<qint64>::max(), 0, std::numeric_limits<qint64>::min(), 2}, {}, 1}};
    const auto corrupt = composite({0, 0, 4, 2}, invalid, source);
    QVERIFY(!corrupt.image); QCOMPARE(corrupt.diagnostic, std::optional<QString>(diagnostic::captureSourceFailed)); QCOMPARE(calls, 0);
  }
  void sourceAndProgressExceptionsBecomeValueFreeFailure() {
    const auto failed = composite({0, 0, 4, 2}, twoDisplays(), [](const Display &, const PhysicalRect &) -> std::optional<RgbaImage> {
      throw std::runtime_error("private fixture token");
    });
    QCOMPARE(failed.diagnostic, std::optional<QString>(diagnostic::captureSourceFailed)); QVERIFY(!failed.image);
    const auto callback = composite({0, 0, 4, 2}, twoDisplays(), labelled, {{}, [](int) { throw std::runtime_error("private progress fixture"); }});
    QCOMPARE(callback.diagnostic, std::optional<QString>(diagnostic::captureSourceFailed)); QVERIFY(!callback.image);
  }

  // RC-017, RC-027: a recorded region stays valid on the same topology.
  void lastRegionRoundTrips() {
    const LastRegion region = makeLastRegion({3, 0, 5, 1}, twoDisplays());
    QCOMPARE(region.displays.size(), std::size_t{2});
    QCOMPARE(validateLastRegion(region, twoDisplays()), std::optional<PhysicalRect>(PhysicalRect{3, 0, 5, 1}));
    Topology unplugged = twoDisplays();
    unplugged.pop_back();
    QVERIFY(!validateLastRegion(region, unplugged));
  }

  void restrictedPermissionFails() {
    RegionSession session(twoDisplays(), {}, {}, {});
    session.start();
    session.permission(Permission::Restricted);
    QCOMPARE(session.state(), SessionState::Failed);
    QCOMPARE(session.diagnostic(), std::optional<QString>(diagnostic::capturePermissionDenied));
    QVERIFY(!session.overlayShown());
  }

  // RC-016, RC-017: the caller receives one outcome carrying the last region.
  void completionPublishesOnceWithLastRegion() {
    int calls = 0;
    std::optional<SessionOutcome> outcome;
    RegionSession session(twoDisplays(), SessionSettings{true, false}, {}, [&](const SessionOutcome &o) {
      ++calls;
      outcome = o;
    });
    session.resumeAt(SessionState::Idle);
    session.pointerDown({1, 1});
    session.pointerUp({6, 3});
    session.captureSucceeded();
    session.captureSucceeded();
    QCOMPARE(calls, 1);
    QVERIFY(outcome && outcome->lastRegion);
    QCOMPARE(outcome->rectangle, std::optional<PhysicalRect>(PhysicalRect{1, 1, 6, 3}));
    QCOMPARE(outcome->lastRegion->displays.size(), std::size_t{2});
  }

  // RC-010, RC-024: keyboard steps adjust only a selected rectangle.
  void keyboardAdjustsSelectedOnly() {
    RegionSession session(twoDisplays(), SessionSettings{false, false}, {}, {});
    session.resumeAt(SessionState::Idle);
    KeyboardStep right;
    right.dx = 1;
    QVERIFY(!session.keyboardAdjust(right));
    session.pointerDown({1, 1});
    session.pointerUp({3, 2});
    QVERIFY(session.keyboardAdjust(right));
    QCOMPARE(session.selection(), std::optional<PhysicalRect>(PhysicalRect{2, 1, 4, 2}));
    right.precision = true;
    QVERIFY(session.keyboardAdjust(right));
    QCOMPARE(session.selection(), std::optional<PhysicalRect>(PhysicalRect{6, 1, 8, 2}));
    QVERIFY(!session.keyboardAdjust(right));  // already at the edge
  }

  void cornerResizeClampsAllFourHandlesAndPreservesOppositeCorner() {
    const Topology topology{{"desktop", {0, 0, 100, 80}, {0, 0, 100, 80}, 1}};
    struct Case { Corner corner; PhysicalPoint point; PhysicalRect expected; };
    const Case cases[]{
      {Corner::TopLeft, {-10, -10}, {0, 0, 60, 50}},
      {Corner::TopRight, {120, -10}, {20, 0, 100, 50}},
      {Corner::BottomLeft, {-10, 100}, {0, 20, 60, 80}},
      {Corner::BottomRight, {120, 100}, {20, 20, 100, 80}},
      {Corner::TopLeft, {99, 79}, {59, 49, 60, 50}},
      {Corner::TopRight, {0, 79}, {20, 49, 21, 50}},
      {Corner::BottomLeft, {99, 0}, {59, 20, 60, 21}},
      {Corner::BottomRight, {0, 0}, {20, 20, 21, 21}}};
    for (const auto &entry : cases) {
      int published = 0; RegionSession session(topology, {false, false}, {}, [&](const SessionOutcome &) { ++published; });
      session.resumeAt(SessionState::Idle); QVERIFY(!session.resizeCorner(entry.corner, entry.point));
      session.pointerDown({20, 20}); session.pointerUp({60, 50});
      QVERIFY(session.resizeCorner(entry.corner, entry.point)); QCOMPARE(session.selection(), std::optional<PhysicalRect>(entry.expected));
      QCOMPARE(session.state(), SessionState::Selected); QCOMPARE(published, 0);
      QVERIFY(!session.resizeCorner(entry.corner, entry.point));
      session.keyEnter(); QVERIFY(!session.resizeCorner(entry.corner, {40, 40}));
      session.captureSucceeded(); QCOMPARE(published, 1);
      QVERIFY(!session.resizeCorner(entry.corner, {40, 40})); QCOMPARE(session.selection(), std::optional<PhysicalRect>(entry.expected));
    }
  }
  void cornerResizeAllowsDesktopGapAndCancelPublishesNoRegion() {
    const Topology topology{{"left", {-100, -50, 0, 50}, {-100, -50, 0, 50}, 1}, {"right", {50, -50, 150, 50}, {50, -50, 150, 50}, 1}};
    std::optional<SessionOutcome> outcome; RegionSession session(topology, {false, false}, {}, [&](const SessionOutcome &o) { outcome = o; });
    session.resumeAt(SessionState::Idle); session.pointerDown({-20, -20}); session.pointerUp({80, 20});
    QVERIFY(session.resizeCorner(Corner::BottomRight, {20, 0}));
    QCOMPARE(session.selection(), std::optional<PhysicalRect>(PhysicalRect{-20, -20, 20, 0}));
    session.cancel(); QVERIFY(outcome); QVERIFY(!outcome->rectangle); QVERIFY(!outcome->lastRegion); QVERIFY(!session.resizeCorner(Corner::TopLeft, {-40, -40}));
  }

  // RC-011: dragging overrides snapping.
  void dragOverridesSnapping() {
    RegionSession session(twoDisplays(), SessionSettings{false, true}, {SnapTarget{{0, 0, 8, 4}}}, {});
    session.resumeAt(SessionState::Idle);
    session.pointerDown({1, 1});
    session.pointerUp({3, 2});
    QCOMPARE(session.state(), SessionState::Selected);
    QCOMPARE(session.selection(), std::optional<PhysicalRect>(PhysicalRect{1, 1, 3, 2}));
  }
};

QTEST_GUILESS_MAIN(RegionCaptureTest)
#include "RegionCaptureTest.moc"
