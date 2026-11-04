#pragma once
#include <QColor>
#include <QDialog>
#include <QImage>
#include <QPoint>
namespace xerahs::app {
// Samples a consented frozen screenshot. Coordinates are image pixels, never an
// assumed desktop/DPI transform. No clipboard, save or upload operation exists.
class ScreenColorSampler final : public QDialog {
  Q_OBJECT
public:
  explicit ScreenColorSampler(QImage image, QWidget *parent = nullptr);
signals:
  void sampled(const QColor &color, const QPoint &pixel);
};
}
