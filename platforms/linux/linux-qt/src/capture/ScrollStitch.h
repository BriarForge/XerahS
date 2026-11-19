// CORE-015: vertical alignment and merge of scrolled frames. Pure image maths
// (Qt Core + Gui), deterministic, no desktop required.
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <functional>
#include <vector>

namespace xerahs::capture {

// CORE-015's "approved threshold", decided 2026-11-20 by
// CORE-015-SCROLL-THRESHOLD-2026-11-20 (product-contract/decisions/
// CORE-015-scroll-threshold.md): a join is accepted only when at least 95%
// of the informative overlap rows agree and the best offset is clearly
// better than every other offset. The two leading references agree on a
// 5%-difference budget: ShareX's scrolling combiner demands unbroken runs
// of byte-identical rows (ShareX.ScreenCaptureLib ScrollingCaptureManager),
// and long-shot-rs treats a row as identical when at most 5% of its pixels
// differ (long-shot-rs/src/stitch.rs, width/20). This bar keeps the
// requirement's promise: a result is never claimed seamless below it.
inline constexpr double kScrollAlignmentThreshold = 0.95;
// The decided ambiguity margin: a runner-up offset within 15% agreement of
// the best means repeating content, and confidence scales down by the
// shortfall instead of guessing.
inline constexpr double kScrollAmbiguityMargin = 0.15;

struct ScrollStitchOptions {
  int headerBand = 0;              // fixed rows at the top of every frame (sticky toolbar)
  int footerBand = 0;              // fixed rows at the bottom of every frame
  int minOverlapRows = 24;         // smallest overlap the matcher will consider
  int minInformativeRows = 8;      // overlap must hold this many non-flat rows to count as evidence
  double confidenceThreshold = kScrollAlignmentThreshold;
  int endOfPageFrames = 2;         // consecutive frames with no new rows that end the page
  int maxHeight = 30000;           // bound on the stitched height, in pixels
  int maxFrames = 40;              // bound on frames accepted into one capture
};

// Facts one join's alignment produced, at the best offset found. The same
// production decision runs in the stitcher, the conformance adapter and the
// published CORE-015 vectors (decision CORE-015-SCROLL-THRESHOLD-2026-11-20).
struct ScrollJoinFacts {
  int informativeRows = 0;        // informative overlap rows at the best offset
  int agreeingRows = 0;           // of those, the rows that agree
  int bestOffset = 0;             // rows scrolled; 0 means no offset was found
  double runnerUpAgreement = 0.0; // best agreement at any offset more than one row away
};

struct ScrollJoinDecision {
  double confidence = 0.0;
  bool accepted = false;
};

// The decided join disposition. Reproduces the stitcher's exact arithmetic:
// agreement is the agreeing fraction of informative rows, ambiguous content
// (a runner-up close behind the best) scales confidence down by the same
// margin factor instead of guessing, thin overlaps carry no evidence, and a
// join is accepted only at or above the confidence threshold.
ScrollJoinDecision scrollJoinDecision(const ScrollJoinFacts &facts, const ScrollStitchOptions &options = {});

enum class ScrollStatus { Continue, EndOfPage, MaxHeight, MaxFrames, LowConfidence, Invalid, Cancelled };

struct ScrollJoin {
  int frame = 0;          // index of the frame joined to its predecessor
  int offset = 0;         // rows scrolled; 0 means no new rows
  double confidence = 0;  // 0..1
  bool accepted = false;
};

// Stable diagnostic code for a terminal status; empty for Continue.
QString scrollStatusCode(ScrollStatus status);

class ScrollStitcher {
public:
  explicit ScrollStitcher(ScrollStitchOptions options = {});

  // Adds the next frame. After a terminal status further frames are ignored
  // and the same status is returned.
  ScrollStatus add(const QImage &frame);
  void cancel();

  ScrollStatus status() const { return m_status; }
  QString code() const { return scrollStatusCode(m_status); }
  // Rows stitched with accepted joins only. After LowConfidence this is the
  // partial result up to the uncertain join; it is never a seamless claim
  // about the rest.
  QImage image() const;
  int height() const;
  // Every frame handed to add(), including a frame whose join was refused.
  const QVector<QImage> &frames() const { return m_frames; }
  const std::vector<ScrollJoin> &joins() const { return m_joins; }
  // True only when no join was refused: the image is a seamless result.
  bool seamless() const { return m_status != ScrollStatus::LowConfidence && m_status != ScrollStatus::Invalid; }

private:
  struct Rows;
  ScrollStatus finish(ScrollStatus status) { m_status = status; return status; }
  void append(const QImage &frame, int firstRow, int rowCount);

  ScrollStitchOptions m_options;
  ScrollStatus m_status = ScrollStatus::Continue;
  QVector<QImage> m_frames;
  std::vector<ScrollJoin> m_joins;
  QImage m_header, m_footer, m_body;  // body grows by whole-row appends
  int m_bodyRows = 0;
  int m_noNew = 0;
  QImage m_previous;
};

struct ScrollStitchResult {
  ScrollStatus status = ScrollStatus::Continue;
  QString code;
  bool seamless = false;
  QImage image;
  QVector<QImage> frames;
  std::vector<ScrollJoin> joins;
};

// Batch form over ordered frames. cancelled is polled between frames.
ScrollStitchResult stitchScrollFrames(const QVector<QImage> &frames, const ScrollStitchOptions &options,
                                      const std::function<bool()> &cancelled = {});

}  // namespace xerahs::capture
