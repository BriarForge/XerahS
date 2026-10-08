#include "RegionOverlay.h"

#include "CaptureController.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

namespace xerahs::app {

using namespace xerahs::capture;

namespace {

const QColor kDim(0, 0, 0, 110);
const QColor kPanel(18, 18, 20, 225);
const QColor kPanelText(236, 236, 236);
const QColor kMuted(160, 160, 166);
const QColor kAccent(255, 255, 255);

}  // namespace

RegionOverlay::RegionOverlay(CaptureController &controller, QScreen *screen, Display display, QImage snapshot)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool),
      m_controller(controller),
      m_display(std::move(display)),
      m_snapshot(std::move(snapshot)),
      m_screenOrigin(screen->geometry().topLeft()) {
  setAttribute(Qt::WA_DeleteOnClose, false);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setWindowTitle(QStringLiteral("XerahS region capture"));
  // RC-019: screen-reader name and instructions.
  setAccessibleName(QStringLiteral("XerahS region capture"));
  setAccessibleDescription(CaptureController::instructions());
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setCursor(Qt::CrossCursor);
  setScreen(screen);
  setGeometry(screen->geometry());
}

PhysicalPoint RegionOverlay::toPhysical(const QPointF &widgetPoint) const {
  // RC-022: map on the display that reports the pointer.
  return mapToPhysical(m_display, m_screenOrigin.x() + widgetPoint.x(), m_screenOrigin.y() + widgetPoint.y());
}

QPointF RegionOverlay::toWidget(PhysicalPoint p) const {
  return {(static_cast<double>(p.x - m_display.physical.left)) / m_display.scale + m_display.logical.left -
              m_screenOrigin.x(),
          (static_cast<double>(p.y - m_display.physical.top)) / m_display.scale + m_display.logical.top -
              m_screenOrigin.y()};
}

QRectF RegionOverlay::toWidget(const PhysicalRect &r) const {
  return QRectF(toWidget(PhysicalPoint{r.left, r.top}), toWidget(PhysicalPoint{r.right, r.bottom}));
}

void RegionOverlay::announce(const QString &text) {
  setAccessibleDescription(text);
  QAccessibleEvent event(this, QAccessible::DescriptionChanged);
  QAccessible::updateAccessibility(&event);
}

void RegionOverlay::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) m_controller.pointerDown(toPhysical(event->position()));
  else if (event->button() == Qt::RightButton) m_controller.cancel();
}

void RegionOverlay::mouseMoveEvent(QMouseEvent *event) { m_controller.pointerMove(toPhysical(event->position())); }

void RegionOverlay::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) m_controller.pointerUp(toPhysical(event->position()));
}

void RegionOverlay::keyPressEvent(QKeyEvent *event) {
  if (!m_controller.key(event->key(), event->modifiers(), m_display)) QWidget::keyPressEvent(event);
}

void RegionOverlay::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  // RC-005, RC-014: the frozen snapshot at 1:1 physical pixels.
  painter.drawImage(rect(), m_snapshot);

  const std::optional<PhysicalRect> selection = m_controller.selection();
  std::optional<PhysicalRect> visible;
  if (selection) visible = intersection(*selection, m_display.physical);

  QPainterPath dim;
  dim.addRect(rect());
  if (visible) dim.addRect(toWidget(*visible));
  painter.fillPath(dim, kDim);

  if (selection && selection->positive()) {
    const QRectF r = toWidget(*selection);
    // RC-019: high-contrast boundary that does not rely on colour alone: a
    // white line with a black halo, plus corner handles.
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawRect(r.adjusted(-1, -1, 0, 0));
    painter.setPen(QPen(kAccent, 1));
    painter.drawRect(r.adjusted(-1, -1, 0, 0));
    if (m_controller.selectionSettled()) {
      for (const QPointF &corner : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()}) {
        const QRectF handle(corner.x() - 3, corner.y() - 3, 6, 6);
        painter.fillRect(handle.adjusted(-1, -1, 1, 1), Qt::black);
        painter.fillRect(handle, kAccent);
      }
    }
    if (visible) paintBadge(painter, r, *selection);
  }

  if (const std::optional<PhysicalPoint> cursor = m_controller.keyboardCursor()) {
    if (m_display.physical.contains(cursor->x, cursor->y) || cursor->x == m_display.physical.right ||
        cursor->y == m_display.physical.bottom) {
      const QPointF c = toWidget(*cursor);
      painter.setPen(QPen(Qt::black, 3));
      painter.drawLine(QPointF(c.x() - 9, c.y()), QPointF(c.x() + 9, c.y()));
      painter.drawLine(QPointF(c.x(), c.y() - 9), QPointF(c.x(), c.y() + 9));
      painter.setPen(QPen(kAccent, 1));
      painter.drawLine(QPointF(c.x() - 8, c.y()), QPointF(c.x() + 8, c.y()));
      painter.drawLine(QPointF(c.x(), c.y() - 8), QPointF(c.x(), c.y() + 8));
    }
  }

  if (m_controller.hintScreen() == screen()) paintHint(painter);
}

void RegionOverlay::paintBadge(QPainter &painter, const QRectF &r, const PhysicalRect &physical) {
  // RC-011: physical bounds are visible before confirmation.
  QFont font = painter.font();
  font.setPixelSize(12);
  font.setFeature(QFont::Tag("tnum"), 1);
  painter.setFont(font);
  const QString size = QStringLiteral("%1 × %2").arg(physical.width()).arg(physical.height());
  const QString origin = QStringLiteral("%1, %2").arg(physical.left).arg(physical.top);
  const QFontMetricsF metrics(font);
  const double width = metrics.horizontalAdvance(size) + metrics.horizontalAdvance(origin) + 26;
  const double height = metrics.height() + 8;
  QPointF at(r.left(), r.top() - height - 6);
  if (at.y() < 4) at.setY(r.bottom() + 6);
  if (at.y() + height > this->height() - 4) at.setY(r.top() + 6);
  at.setX(std::clamp(at.x(), 4.0, std::max(4.0, this->width() - width - 4)));
  const QRectF box(at, QSizeF(width, height));
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(kPanel);
  painter.drawRoundedRect(box, 3, 3);
  painter.setPen(kPanelText);
  painter.drawText(box.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, size);
  painter.setPen(kMuted);
  painter.drawText(box.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, origin);
}

void RegionOverlay::paintHint(QPainter &painter) {
  // RC-010: the precision modifier is documented in the UI.
  struct Item {
    const char *key;
    const char *label;
  };
  static const Item items[] = {
      {"Drag / Space", "select"}, {"Enter", "capture"},      {"Esc", "cancel"},
      {"Arrows", "move"},         {"Shift", "×10 step"},     {"Alt+Arrows", "resize"},
  };
  QFont font = painter.font();
  font.setPixelSize(12);
  QFont bold = font;
  bold.setWeight(QFont::DemiBold);
  const QFontMetricsF regular(font);
  const QFontMetricsF strong(bold);
  double width = 16;
  for (const Item &item : items) {
    width += strong.horizontalAdvance(QString::fromUtf8(item.key)) + 5 +
             regular.horizontalAdvance(QString::fromUtf8(item.label)) + 16;
  }
  const double height = regular.height() + 12;
  const QRectF box((this->width() - width) / 2, this->height() - height - 24, width, height);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(kPanel);
  painter.drawRoundedRect(box, 4, 4);
  double x = box.left() + 16;
  for (const Item &item : items) {
    const QString key = QString::fromUtf8(item.key);
    const QString label = QString::fromUtf8(item.label);
    painter.setFont(bold);
    painter.setPen(kPanelText);
    painter.drawText(QRectF(x, box.top(), strong.horizontalAdvance(key) + 1, box.height()), Qt::AlignVCenter, key);
    x += strong.horizontalAdvance(key) + 5;
    painter.setFont(font);
    painter.setPen(kMuted);
    painter.drawText(QRectF(x, box.top(), regular.horizontalAdvance(label) + 1, box.height()), Qt::AlignVCenter, label);
    x += regular.horizontalAdvance(label) + 16;
  }
}

}  // namespace xerahs::app
