#pragma once
#include "EditorSource.h"
#include "CanvasCropDialog.h"
#include <QDialog>
#include <QPointer>
class QLabel;
class QCheckBox;
class QProgressDialog;
namespace xerahs::app {
class EditorCanvas;
class AnnotationObjectNavigator;
class CaptureAnnotationDialog final : public QDialog {
  Q_OBJECT
public:
  CaptureAnnotationDialog(const LoadedSource &source, QWidget *parent = nullptr, const xerahs::editor::RectangleStyle &style = {});
  ~CaptureAnnotationDialog() override;
  std::optional<xerahs::editor::AnnotationDocument> document() const { return m_accepted; }
  bool sidecarIntent() const { return m_sidecarIntent; }
  std::optional<xerahs::editor::CropRegion> sourceRegion() const { return m_session.retainedSourceRegion(); }
  void setCropHooks(CanvasCropHooks hooks) { m_cropHooks = std::move(hooks); }
public slots:
  void accept() override;
  void reject() override;
private:
  bool editingReady() const;
  void cropImage(xerahs::editor::CropRegion);
  void restoreHistory(bool redo);
  void finishOperation();
  struct HistoryWork;
  std::shared_ptr<HistoryWork> m_historyWork;
  QPointer<QProgressDialog> m_historyDialog;
  QPointer<CanvasCropDialog> m_cropDialog;
  CanvasCropHooks m_cropHooks;
  bool m_operationPending = false;
  bool m_closing = false;
  QImage m_image;
  xerahs::editor::EditorSession m_session;
  EditorCanvas *m_canvas = nullptr;
  AnnotationObjectNavigator *m_annotationObjects = nullptr;
  QLabel *m_status = nullptr;
  std::optional<xerahs::editor::AnnotationDocument> m_accepted;
  QCheckBox *m_saveSidecar = nullptr;
  bool m_sidecarIntent = false;
};
} // namespace xerahs::app
