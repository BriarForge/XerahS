// CORE-015: vertical alignment and merge of scrolled frames. Pure image maths
// (Qt Core + Gui), deterministic, no desktop required.
#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <functional>
#include <vector>

namespace xerahs::capture {

// CORE-015 says "the approved threshold" but the contract defines no value.
// This is a conservative placeholder pending a product-owner decision: a join
// is accepted only when at least 95% of the informative overlap rows agree and
// the best offset is clearly better than every other offset.
inline constexpr double kScrollAlignmentThreshold = 0.95;

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
