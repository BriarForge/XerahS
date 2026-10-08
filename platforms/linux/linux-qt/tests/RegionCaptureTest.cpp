// Production-path checks that the conformance vectors cannot cover: pixel
// placement and resampling rejection in the compositor, last-region round
// trips, restricted permission, and the outcome published to the caller.

#include "capture/RegionGeometry.h"
#include "capture/RegionSession.h"

#include <QTest>

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
