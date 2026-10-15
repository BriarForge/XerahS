// EC-012: cancellable native preview, outside session history until accepted.
#pragma once

#include "EditorCanvasWorker.h"

#include <QDialog>
#include <QTimer>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;

namespace xerahs::app {
class CanvasRotationPreview;

class CanvasRotationDialog final : public QDialog {
  Q_OBJECT
public:
  CanvasRotationDialog(xerahs::editor::AnnotationDocument document, QImage source, QWidget *parent = nullptr, EditorCanvasHooks hooks = {});
  ~CanvasRotationDialog() override;
  std::optional<PreparedCanvasEdit> takeEdit();
  void accept() override;
  void reject() override;

private:
  void requestPreview();
  void startPreview();
  void pollPreview();

  struct Work {
    std::atomic<bool> cancelled{false}, ready{false};
    std::atomic<int> progress{0};
    CanvasPreview preview;
    quint64 revision = 0;
  };
  const xerahs::editor::AnnotationDocument m_document;
  const QImage m_source;
  const EditorCanvasHooks m_hooks;
  xerahs::editor::Argb m_fill = 0;
  QDoubleSpinBox *m_angle;
  QComboBox *m_interpolation;
  QCheckBox *m_expand;
  QDialogButtonBox *m_buttons;
  QLabel *m_status;
  QProgressBar *m_progress;
  CanvasRotationPreview *m_preview;
  QTimer m_debounce, m_poll;
  std::shared_ptr<Work> m_work;
  std::optional<PreparedCanvasEdit> m_prepared;
  quint64 m_revision = 0;
  bool m_pending = false, m_closing = false;
};
}  // namespace xerahs::app
