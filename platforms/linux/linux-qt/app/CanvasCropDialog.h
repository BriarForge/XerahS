#pragma once
#include "EditorCanvasOperations.h"
#include "image-editor/CropRegion.h"
#include <QDialog>
#include <QTimer>
#include <atomic>
#include <memory>
class QSpinBox;
class QLabel;
class QProgressBar;
class QDialogButtonBox;
namespace xerahs::app {
class CropPreview;
struct PreparedCropEdit { PreparedCanvasEdit edit; xerahs::editor::CropRegion region; QImage displaySource; };
struct CanvasCropHooks { std::function<void()> beforePrepare; };
class CanvasCropDialog final : public QDialog {
  Q_OBJECT
public:
  CanvasCropDialog(xerahs::editor::AnnotationDocument, QImage, xerahs::editor::CropRegion, QWidget *parent = nullptr, CanvasCropHooks = {});
  ~CanvasCropDialog() override;
  std::optional<PreparedCropEdit> takeEdit();
  void accept() override;
  void reject() override;
private:
  void request();
  void start();
  void poll();
  struct Work {
    std::atomic<bool> cancelled{false}, done{false};
    std::atomic<int> progress{0};
    CanvasPreview preview;
    QImage displaySource;
    xerahs::editor::CropRegion region;
    quint64 revision = 0;
  };
  const xerahs::editor::AnnotationDocument m_document;
  const QImage m_source;
  const CanvasCropHooks m_hooks;
  QSpinBox *m_left, *m_top, *m_width, *m_height;
  QLabel *m_status;
  QProgressBar *m_progress;
  QDialogButtonBox *m_buttons;
  CropPreview *m_preview;
  QTimer m_debounce, m_poll;
  std::shared_ptr<Work> m_work;
  std::optional<PreparedCropEdit> m_prepared;
  quint64 m_revision = 0;
  bool m_pending = false, m_closing = false;
};
} // namespace xerahs::app
