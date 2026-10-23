// CORE-015: scrolled frames merge only when alignment is trustworthy.
#include "capture/ScrollStitch.h"

#include <QTest>

using namespace xerahs::capture;

namespace {
constexpr int kWidth = 160;

quint32 mix(quint32 v) { v ^= v >> 16; v *= 0x7feb352dU; v ^= v >> 15; v *= 0x846ca68bU; v ^= v >> 16; return v; }

// Each row is its own noise line, so every offset is distinguishable.
QImage tallPage(int height, int periodRows = 0) {
  QImage page(kWidth, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) {
    const int source = periodRows ? y % periodRows : y;
    for (int x = 0; x < kWidth; ++x) {
      const quint32 v = mix(quint32(source) * 977U + quint32(x) * 31U + 7U) & 0xff;
      page.setPixel(x, y, qRgb(int(v), int(v), int(v)));
    }
  }
  return page;
}

QImage slice(const QImage &page, int top, int height) { return page.copy(0, top, page.width(), height); }

// Frame with a sticky header and footer painted over the scrolled content.
QImage framed(const QImage &page, int top, int height, int header, int footer) {
  QImage frame = slice(page, top, height);
  for (int y = 0; y < header; ++y) for (int x = 0; x < kWidth; ++x) frame.setPixel(x, y, qRgb(20, 40, 200));
  for (int y = height - footer; y < height; ++y) for (int x = 0; x < kWidth; ++x) frame.setPixel(x, y, qRgb(200, 40, 20));
  return frame;
}

QVector<QImage> framesAt(const QImage &page, const QList<int> &tops, int height) {
  QVector<QImage> out;
  for (int top : tops) out.append(slice(page, top, height));
  return out;
}
}  // namespace

class ScrollStitchTest : public QObject {
  Q_OBJECT
private slots:
  void joinsOverlappingFramesExactlyIncludingOddOverlaps() {
    const auto page = tallPage(900);
    const auto result = stitchScrollFrames(framesAt(page, {0, 150, 321, 454, 600}, 300), {});
    QCOMPARE(result.status, ScrollStatus::Continue);
    QVERIFY(result.seamless);
    QCOMPARE(result.image.size(), QSize(kWidth, 900));
    QCOMPARE(result.image.convertToFormat(QImage::Format_ARGB32), page.convertToFormat(QImage::Format_ARGB32));
    QCOMPARE(int(result.joins.size()), 4);
    const int expected[] = {150, 171, 133, 146};
    for (int i = 0; i < 4; ++i) {
      QCOMPARE(result.joins[i].offset, expected[i]);
      QVERIFY(result.joins[i].accepted);
      QVERIFY(result.joins[i].confidence >= kScrollAlignmentThreshold);
    }
  }

  void fixedHeaderAndFooterAreKeptOnceAndIgnoredForMatching() {
    const auto page = tallPage(900);
    const int header = 24, footer = 16;
    QVector<QImage> frames;
    for (int top : {0, 200, 391, 560}) frames.append(framed(page, top, 300, header, footer));
    ScrollStitchOptions options; options.headerBand = header; options.footerBand = footer;
    const auto result = stitchScrollFrames(frames, options);
    QVERIFY(result.seamless);
    // Content rows [header, 300-footer) of every frame; last frame's content ends at 560+284.
    const int contentEnd = 560 + 300 - footer;
    QCOMPARE(result.image.height(), header + (contentEnd - header) + footer);
    QCOMPARE(result.image.pixel(5, 3), qRgb(20, 40, 200));
    QCOMPARE(result.image.pixel(5, result.image.height() - 2), qRgb(200, 40, 20));
    for (int y = header; y < result.image.height() - footer; y += 37)
      QCOMPARE(result.image.pixel(9, y), page.pixel(9, y));
    // The sticky bands never appear inside the body.
    for (int y = header; y < result.image.height() - footer; ++y) QVERIFY(result.image.pixel(5, y) != qRgb(20, 40, 200));
  }

  void repeatingRowsAreReportedAsLowConfidenceNotGuessed() {
    const auto page = tallPage(900, 10);
    ScrollStitcher stitcher;
    QCOMPARE(stitcher.add(slice(page, 0, 300)), ScrollStatus::Continue);
    QCOMPARE(stitcher.add(slice(page, 25, 300)), ScrollStatus::LowConfidence);
    QCOMPARE(stitcher.code(), QString("scroll-low-confidence"));
    QVERIFY(!stitcher.seamless());
    QVERIFY(!stitcher.joins().back().accepted);
    QVERIFY(stitcher.joins().back().confidence < kScrollAlignmentThreshold);
    // Partial output stops at the uncertain join; both frames stay available.
    QCOMPARE(stitcher.image().height(), 300);
    QCOMPARE(stitcher.frames().size(), 2);
    // Terminal: later frames are not merged.
    QCOMPARE(stitcher.add(slice(page, 50, 300)), ScrollStatus::LowConfidence);
    QCOMPARE(stitcher.frames().size(), 2);
  }

  void framesWithoutOverlapAreLowConfidence() {
    const auto page = tallPage(1500);
    const auto result = stitchScrollFrames(framesAt(page, {0, 100, 800}, 300), {});
    QCOMPARE(result.status, ScrollStatus::LowConfidence);
    QCOMPARE(result.image.height(), 400);  // frames 0 and 1 only
    QCOMPARE(result.frames.size(), 3);
    QVERIFY(!result.seamless);
  }

  void flatFramesNeverProduceAConfidentJoin() {
    QImage flat(kWidth, 300, QImage::Format_ARGB32); flat.fill(Qt::white);
    QImage other = flat.copy(); other.setPixel(3, 3, qRgb(0, 0, 0));
    ScrollStitcher stitcher;
    stitcher.add(flat);
    QCOMPARE(stitcher.add(other), ScrollStatus::LowConfidence);
  }

  void endOfPageStopsAfterConfiguredStaleFrames() {
    const auto page = tallPage(500);
    auto frames = framesAt(page, {0, 100, 200, 200, 200, 200}, 300);
    ScrollStitchOptions options; options.endOfPageFrames = 2;
    const auto result = stitchScrollFrames(frames, options);
    QCOMPARE(result.status, ScrollStatus::EndOfPage);
    QVERIFY(result.seamless);
    QCOMPARE(result.image.height(), 500);
    QCOMPARE(result.frames.size(), 5);  // the sixth frame is never consumed
    QCOMPARE(result.joins.back().offset, 0);
  }

  void maxHeightClipsExactlyAtTheBound() {
    const auto page = tallPage(1200);
    ScrollStitchOptions options; options.maxHeight = 650;
    const auto result = stitchScrollFrames(framesAt(page, {0, 200, 400, 600, 800}, 300), options);
    QCOMPARE(result.status, ScrollStatus::MaxHeight);
    QCOMPARE(result.code, QString("scroll-max-height"));
    QCOMPARE(result.image.height(), 650);
    QCOMPARE(result.image, page.copy(0, 0, kWidth, 650).convertToFormat(result.image.format()));
    QVERIFY(result.seamless);  // bounded, not uncertain
  }

  void maxFramesStopsWithoutDroppingTheStitchedPart() {
    const auto page = tallPage(900);
    ScrollStitchOptions options; options.maxFrames = 3;
    const auto result = stitchScrollFrames(framesAt(page, {0, 150, 300, 450}, 300), options);
    QCOMPARE(result.status, ScrollStatus::MaxFrames);
    QCOMPARE(result.image.height(), 600);
    QCOMPARE(result.frames.size(), 3);
  }

  void cancellationKeepsPartialOutput() {
    const auto page = tallPage(900);
    int polls = 0;
    const auto result = stitchScrollFrames(framesAt(page, {0, 150, 300, 450}, 300), {}, [&polls] { return ++polls > 2; });
    QCOMPARE(result.status, ScrollStatus::Cancelled);
    QCOMPARE(result.code, QString("scroll-cancelled"));
    QCOMPARE(result.image.height(), 450);
    ScrollStitcher stitcher; stitcher.add(slice(page, 0, 300)); stitcher.cancel();
    QCOMPARE(stitcher.add(slice(page, 150, 300)), ScrollStatus::Cancelled);
  }

  void invalidFramesAreRejected() {
    const auto page = tallPage(900);
    ScrollStitcher stitcher;
    stitcher.add(slice(page, 0, 300));
    QCOMPARE(stitcher.add(page.copy(0, 0, kWidth - 1, 300)), ScrollStatus::Invalid);
    QCOMPARE(ScrollStitcher().add(QImage()), ScrollStatus::Invalid);
  }

  void resultIsDeterministic() {
    const auto page = tallPage(900);
    const auto frames = framesAt(page, {0, 150, 321, 454, 600}, 300);
    const auto one = stitchScrollFrames(frames, {}), two = stitchScrollFrames(frames, {});
    QCOMPARE(one.image, two.image);
    QCOMPARE(one.joins.size(), two.joins.size());
    for (size_t i = 0; i < one.joins.size(); ++i) { QCOMPARE(one.joins[i].offset, two.joins[i].offset); QCOMPARE(one.joins[i].confidence, two.joins[i].confidence); }
  }
};

QTEST_APPLESS_MAIN(ScrollStitchTest)
#include "ScrollStitchTest.moc"
