#pragma once
#include "EditorSource.h"
#include <QDialog>
class QLabel;
class QCheckBox;
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
public slots:
  void accept() override;
private:
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
