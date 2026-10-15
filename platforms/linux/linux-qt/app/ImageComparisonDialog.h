// EC-016: native comparison controls; source snapshots never enter history.
#pragma once
#include "ComparisonImages.h"
#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

class QComboBox;
class QFileDialog;
class QDialogButtonBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;

namespace xerahs::app {
// Worker-only boundary hook. Captures must own their data beyond UI cancellation.
struct ComparisonHooks {
  std::function<void()> beforePrepare;
};
class ComparisonView final : public QWidget {
  Q_OBJECT
public:
  explicit ComparisonView(QWidget *parent = nullptr);
  void setImages(QImage first, QImage second, xerahs::editor::ComparisonLayout layout);
  void setReveal(int percent);
  const std::optional<xerahs::editor::ComparisonLayout> &comparisonLayout() const { return m_layout; }
  QPointF toView(QPointF logicalPoint) const;
signals:
  void revealRequested(int percent);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void focusOutEvent(QFocusEvent *) override;

private:
  QRectF imageRect() const;
  void revealAt(QPointF point);
  std::array<QImage, 2> m_images;
  std::optional<xerahs::editor::ComparisonLayout> m_layout;
  int m_reveal = 50;
  bool m_dragging = false;
};

class ImageComparisonDialog final : public QDialog {
  Q_OBJECT
public:
  ImageComparisonDialog(xerahs::editor::AnnotationDocument document, QImage source, QWidget *parent = nullptr,
                        ComparisonHooks hooks = {});
  ~ImageComparisonDialog() override;
  // Shared entry points for the native file pickers and application integrations.
  bool loadFile(int input, const QString &path);
  bool useCurrentImage();
  void reject() override;
  void done(int result) override;

private:
  void chooseFile(int input);
  bool startLoad(int input, const QString &path, bool current);
  void pollLoad();
  void cancelLoad();
  void updateView();
  void setBusy(bool busy);
  struct Input {
    QImage image;
    QString label;
  };
  struct Work {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> ready{false};
    std::atomic<int> progress{0};
    PreparedComparisonImage prepared;
    int input = 0;
    QString label;
  };
  const xerahs::editor::AnnotationDocument m_document;
  const QImage m_source;
  const ComparisonHooks m_hooks;
  std::array<Input, 2> m_inputs;
  std::array<QPushButton *, 3> m_inputButtons;
  std::array<QLabel *, 2> m_inputLabels;
  ComparisonView *m_view;
  QComboBox *m_alignment;
  QSlider *m_reveal;
  QLabel *m_percent, *m_status, *m_geometry;
  QProgressBar *m_progress;
  QPushButton *m_cancelLoad;
  QDialogButtonBox *m_buttons;
  QTimer m_poll;
  std::shared_ptr<Work> m_work;
  QPointer<QFileDialog> m_picker;
  bool m_pickerOpen = false;
  bool m_transition = false;
  bool m_closing = false;
};
} // namespace xerahs::app
