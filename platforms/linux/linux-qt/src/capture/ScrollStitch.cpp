// CORE-015: vertical alignment and merge of scrolled frames.
#include "capture/ScrollStitch.h"

#include <QPainter>

#include <algorithm>
#include <cstdlib>

namespace xerahs::capture {
namespace {

constexpr int kSignatureColumns = 64;     // columns sampled per row
constexpr int kRowTolerance = 3;          // mean gray difference below which two rows agree
constexpr int kInformativeContrast = 12;  // max-min gray range that makes a row evidence
constexpr double kAmbiguityMargin = 0.15; // best must beat the runner-up by this much

struct Signature {
  int rows = 0;
  std::vector<unsigned char> data;       // rows * kSignatureColumns
  std::vector<unsigned char> informative;
  const unsigned char *row(int y) const { return data.data() + size_t(y) * kSignatureColumns; }
};

Signature makeSignature(const QImage &frame) {
  const QImage gray = frame.convertToFormat(QImage::Format_Grayscale8);
  Signature sig; sig.rows = gray.height();
  sig.data.resize(size_t(sig.rows) * kSignatureColumns); sig.informative.assign(sig.rows, 0);
  const int width = gray.width();
  for (int y = 0; y < sig.rows; ++y) {
    const uchar *line = gray.constScanLine(y);
    unsigned char *out = sig.data.data() + size_t(y) * kSignatureColumns;
    int lo = 255, hi = 0;
    for (int i = 0; i < kSignatureColumns; ++i) {
      const int x = int((qint64(i) * width + width / 2) / kSignatureColumns);
      out[i] = line[std::min(x, width - 1)];
      lo = std::min<int>(lo, out[i]); hi = std::max<int>(hi, out[i]);
    }
    sig.informative[y] = hi - lo >= kInformativeContrast;
  }
  return sig;
}

bool rowsAgree(const unsigned char *a, const unsigned char *b) {
  int sum = 0;
  for (int i = 0; i < kSignatureColumns; ++i) sum += std::abs(int(a[i]) - int(b[i]));
  return sum <= kRowTolerance * kSignatureColumns;
}

struct Score { double value = 0; int informative = 0; };

// Frame B row y corresponds to frame A row y + d. Rows in [top, bottom) of
// the content band take part.
Score scoreOffset(const Signature &a, const Signature &b, int d, int top, int bottom) {
  int informative = 0, matched = 0;
  for (int y = top; y + d < bottom; ++y) {
    if (!b.informative[y] && !a.informative[y + d]) continue;
    ++informative;
    if (rowsAgree(b.row(y), a.row(y + d))) ++matched;
  }
  return {informative ? double(matched) / informative : 0.0, informative};
}

}  // namespace

QString scrollStatusCode(ScrollStatus status) {
  switch (status) {
    case ScrollStatus::Continue: return {};
    case ScrollStatus::EndOfPage: return QStringLiteral("scroll-end-of-page");
    case ScrollStatus::MaxHeight: return QStringLiteral("scroll-max-height");
    case ScrollStatus::MaxFrames: return QStringLiteral("scroll-max-frames");
    case ScrollStatus::LowConfidence: return QStringLiteral("scroll-low-confidence");
    case ScrollStatus::Invalid: return QStringLiteral("scroll-frame-invalid");
    case ScrollStatus::Cancelled: return QStringLiteral("scroll-cancelled");
  }
  return {};
}

ScrollStitcher::ScrollStitcher(ScrollStitchOptions options) : m_options(options) {
  m_options.headerBand = std::max(0, m_options.headerBand);
  m_options.footerBand = std::max(0, m_options.footerBand);
  m_options.minOverlapRows = std::max(1, m_options.minOverlapRows);
  m_options.endOfPageFrames = std::max(1, m_options.endOfPageFrames);
  m_options.maxHeight = std::max(1, m_options.maxHeight);
  m_options.maxFrames = std::max(1, m_options.maxFrames);
}

void ScrollStitcher::cancel() { if (m_status == ScrollStatus::Continue) m_status = ScrollStatus::Cancelled; }

int ScrollStitcher::height() const {
  return m_previous.isNull() ? 0 : m_header.height() + m_bodyRows + m_footer.height();
}

QImage ScrollStitcher::image() const {
  if (m_previous.isNull()) return {};
  QImage out(m_previous.width(), height(), QImage::Format_ARGB32);
  out.fill(Qt::white);
  QPainter painter(&out);
  int y = 0;
  painter.drawImage(0, y, m_header); y += m_header.height();
  painter.drawImage(0, y, m_body, 0, 0, -1, m_bodyRows); y += m_bodyRows;
  painter.drawImage(0, y, m_footer);
  painter.end();
  return out;
}

void ScrollStitcher::append(const QImage &frame, int firstRow, int rowCount) {
  if (rowCount <= 0) return;
  // Grow in chunks so appending N frames is not quadratic in copies.
  if (m_body.isNull() || m_bodyRows + rowCount > m_body.height()) {
    QImage grown(frame.width(), std::max(m_bodyRows + rowCount, m_body.height() * 2), QImage::Format_ARGB32);
    grown.fill(Qt::white);
    if (!m_body.isNull()) { QPainter p(&grown); p.drawImage(0, 0, m_body, 0, 0, -1, m_bodyRows); }
    m_body = grown;
  }
  QPainter painter(&m_body);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.drawImage(0, m_bodyRows, frame, 0, firstRow, -1, rowCount);
  m_bodyRows += rowCount;
}

ScrollStatus ScrollStitcher::add(const QImage &input) {
  if (m_status != ScrollStatus::Continue) return m_status;
  const QImage frame = input.convertToFormat(QImage::Format_ARGB32);
  const int contentTop = m_options.headerBand, contentBottom = frame.height() - m_options.footerBand;
  const bool first = m_previous.isNull();
  if (frame.isNull() || frame.width() < 1 || contentBottom - contentTop < m_options.minOverlapRows + 1 ||
      (!first && frame.size() != m_previous.size())) return finish(ScrollStatus::Invalid);
  if (int(m_frames.size()) >= m_options.maxFrames) return finish(ScrollStatus::MaxFrames);
  m_frames.append(frame);
  const int index = int(m_frames.size()) - 1;

  if (first) {
    m_header = frame.copy(0, 0, frame.width(), m_options.headerBand);
    m_footer = frame.copy(0, contentBottom, frame.width(), m_options.footerBand);
    m_previous = frame;
    append(frame, contentTop, contentBottom - contentTop);
    if (height() >= m_options.maxHeight) {
      m_bodyRows = std::max(0, m_options.maxHeight - m_header.height() - m_footer.height());
      return finish(ScrollStatus::MaxHeight);
    }
    return ScrollStatus::Continue;
  }

  const Signature a = makeSignature(m_previous), b = makeSignature(frame);
  const int span = contentBottom - contentTop;
  // No new rows: the content band is unchanged. Flat bands count as unchanged.
  bool same = true;
  for (int y = contentTop; y < contentBottom && same; ++y) same = rowsAgree(a.row(y), b.row(y));
  if (same) {
    m_joins.push_back({index, 0, 1.0, true});
    m_previous = frame;
    return ++m_noNew >= m_options.endOfPageFrames ? finish(ScrollStatus::EndOfPage) : ScrollStatus::Continue;
  }

  double best = 0, runnerUp = 0; int bestOffset = 0;
  std::vector<double> scores(span, 0.0);
  for (int d = 1; d <= span - m_options.minOverlapRows; ++d) {
    const Score s = scoreOffset(a, b, d, contentTop, contentBottom);
    scores[d] = s.informative >= m_options.minInformativeRows ? s.value : 0.0;
    if (scores[d] > best) { best = scores[d]; bestOffset = d; }  // ties keep the smaller offset
  }
  for (int d = 1; d <= span - m_options.minOverlapRows; ++d)
    if (std::abs(d - bestOffset) > 1) runnerUp = std::max(runnerUp, scores[d]);
  // Ambiguous content (repeating rows) scales confidence down instead of guessing.
  const double confidence = bestOffset ? best * std::min(1.0, (best - runnerUp) / kAmbiguityMargin) : 0.0;
  if (confidence < m_options.confidenceThreshold) {
    m_joins.push_back({index, bestOffset, confidence, false});
    return finish(ScrollStatus::LowConfidence);
  }
  m_joins.push_back({index, bestOffset, confidence, true});
  m_noNew = 0;
  m_previous = frame;
  const int fixed = m_header.height() + m_footer.height();
  const int room = m_options.maxHeight - fixed - m_bodyRows;
  append(frame, contentBottom - bestOffset, std::min(bestOffset, std::max(0, room)));
  if (bestOffset >= room) return finish(ScrollStatus::MaxHeight);
  return ScrollStatus::Continue;
}

ScrollStitchResult stitchScrollFrames(const QVector<QImage> &frames, const ScrollStitchOptions &options,
                                      const std::function<bool()> &cancelled) {
  ScrollStitcher stitcher(options);
  for (const auto &frame : frames) {
    if (cancelled && cancelled()) { stitcher.cancel(); break; }
    if (stitcher.add(frame) != ScrollStatus::Continue) break;
  }
  return {stitcher.status(), stitcher.code(), stitcher.seamless(), stitcher.image(), stitcher.frames(), stitcher.joins()};
}

}  // namespace xerahs::capture
