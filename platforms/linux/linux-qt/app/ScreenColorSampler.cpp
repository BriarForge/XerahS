#include "NativeAccessibility.h"
#include "ScreenColorSampler.h"
#include "NativeWindowPlacement.h"
#include "tools/ColorFormats.h"
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
namespace xerahs::app {
namespace {
class SampleView final : public QWidget {
public:
  QImage image;
  QPoint pixel;
  std::function<void()> changed;
  explicit SampleView(QImage source, QWidget *parent) : QWidget(parent), image(std::move(source)), pixel(image.width()/2, image.height()/2) {
    setObjectName("screenColorSampleView"); setAccessibleName("Frozen screenshot pixel sampler");
    setAccessibleDescription("Move the pointer over the screenshot or use arrow keys to sample image pixels. Shift moves ten pixels. Use sampled colour accepts; Escape cancels.");
    setFocusPolicy(Qt::StrongFocus); setMouseTracking(true); setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  QRectF imageRect() const {
    if (image.isNull()) return {};
    QSizeF fitted = image.size(); fitted.scale(size(), Qt::KeepAspectRatio);
    return QRectF(QPointF((width()-fitted.width())/2, (height()-fitted.height())/2), fitted);
  }
  void select(QPoint next) {
    if (image.isNull()) return;
    pixel = {std::clamp(next.x(), 0, image.width()-1), std::clamp(next.y(), 0, image.height()-1)};
    setAccessibleDescription(QStringLiteral("Snapshot pixel X %1, Y %2. %3. Arrow keys sample; Escape cancels.").arg(pixel.x()).arg(pixel.y()).arg(tools::formatColor(image.pixelColor(pixel), tools::ColorFormat::Hex)));
    update(); if (changed) changed();
  }
protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this); painter.fillRect(rect(), palette().brush(QPalette::Base));
    if (image.isNull()) return;
    const auto target = imageRect(); painter.drawImage(target, image);
    const QPointF point(target.x() + (pixel.x()+0.5)*target.width()/image.width(), target.y() + (pixel.y()+0.5)*target.height()/image.height());
    painter.setPen(QPen(palette().color(QPalette::WindowText), 3)); painter.drawEllipse(point, 6, 6);
    painter.setPen(QPen(palette().color(QPalette::Base), 1)); painter.drawEllipse(point, 6, 6);
    if (hasFocus()) { painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine)); painter.drawRect(rect().adjusted(2,2,-3,-3)); }
  }
  void mouseMoveEvent(QMouseEvent *event) override {
    const auto target = imageRect(); if (!target.contains(event->position())) return;
    select({int((event->position().x()-target.x())*image.width()/target.width()), int((event->position().y()-target.y())*image.height()/target.height())});
  }
  void mousePressEvent(QMouseEvent *event) override { if (event->button() == Qt::LeftButton) { setFocus(); mouseMoveEvent(event); } }
  void keyPressEvent(QKeyEvent *event) override {
    const int step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
    QPoint next = pixel;
    if (event->key() == Qt::Key_Left) next.rx() -= step;
    else if (event->key() == Qt::Key_Right) next.rx() += step;
    else if (event->key() == Qt::Key_Up) next.ry() -= step;
    else if (event->key() == Qt::Key_Down) next.ry() += step;
    else { QWidget::keyPressEvent(event); return; }
    select(next); event->accept();
  }
};
}
ScreenColorSampler::ScreenColorSampler(QImage image, QWidget *parent) : QDialog(parent) {
  new NativeAccessibility(*this, "Sample consented screenshot pixels with pointer or arrows. Use sampled colour accepts; Escape cancels without copying.", false);
  setObjectName("screenColorSampler"); setWindowTitle("Sample screen colour"); setAccessibleName(windowTitle());
  setAccessibleDescription("Sample a frozen, consented screenshot. Nothing is copied automatically.");
  new NativeWindowPlacement(*this);
  auto *layout = new QVBoxLayout(this);
  auto *notice = new QLabel("Frozen screenshot. Coordinates are zero-based screenshot pixels, independent of desktop scaling. Move the pointer or use arrows (Shift: ten pixels). Escape cancels. Nothing is copied automatically.", this);
  notice->setWordWrap(true); notice->setTextFormat(Qt::PlainText); layout->addWidget(notice);
  auto *view = new SampleView(std::move(image), this); layout->addWidget(view, 1);
  auto *row = new QHBoxLayout;
  auto *zoom = new QLabel(this); zoom->setObjectName("screenColorMagnifier"); zoom->setAccessibleName("Magnified sampled area, fifteen times, nearest pixels");
  auto *values = new QLabel(this); values->setObjectName("screenColorCoordinates"); values->setWordWrap(true); values->setTextFormat(Qt::PlainText);
  values->setAccessibleName("Sample pixel coordinates and canonical colour values"); row->addWidget(zoom); row->addWidget(values, 1); layout->addLayout(row);
  view->changed = [view, zoom, values] {
    QImage patch(11, 11, QImage::Format_ARGB32); patch.fill(view->palette().color(QPalette::Base));
    for (int y = -5; y <= 5; ++y) for (int x = -5; x <= 5; ++x) {
      const auto source = view->pixel + QPoint(x,y);
      if (view->image.rect().contains(source)) patch.setPixelColor(x+5,y+5,view->image.pixelColor(source));
    }
    auto magnified = patch.scaled(165,165,Qt::IgnoreAspectRatio,Qt::FastTransformation);
    QPainter border(&magnified); border.setPen(QPen(view->palette().color(QPalette::WindowText), 3)); border.drawRect(75,75,15,15); border.end();
    zoom->setPixmap(QPixmap::fromImage(magnified));
    const auto color = view->image.pixelColor(view->pixel);
    values->setText(QStringLiteral("Snapshot pixel X: %1, Y: %2\n%3\n%4\nHSV (HSB): %5").arg(view->pixel.x()).arg(view->pixel.y())
        .arg(tools::formatColor(color,tools::ColorFormat::Hex)).arg(tools::formatColor(color,tools::ColorFormat::Rgb)).arg(tools::formatColor(color,tools::ColorFormat::Hsb)));
    values->setAccessibleDescription(values->text());
  };
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  auto *use = buttons->button(QDialogButtonBox::Ok); use->setText("Use sampled colour"); use->setObjectName("acceptScreenColorSample"); use->setAccessibleName(use->text()); use->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  layout->addWidget(buttons); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,this,[this,view] {
    if (view->image.isNull()) return;
    const QPointer<ScreenColorSampler> owner(this); emit sampled(view->image.pixelColor(view->pixel),view->pixel); if (owner) accept();
  });
  use->setEnabled(!view->image.isNull());
  if (!view->image.isNull()) view->select(view->pixel);
  resize(760,640); view->setFocus();
}
}
