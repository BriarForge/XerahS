#pragma once
#include "image-editor/AnnotationDocument.h"
#include <QDialog>
#include <QSet>
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
namespace xerahs::app {
struct SelectionRotationChoice { std::optional<double> degrees; QString diagnostic; };
// Owns the displayed objects. Preview is text describing Core-validated angles,
// never a mutation of the caller's session or a render of unknown payloads.
class SelectionRotationDialog final : public QDialog {
  Q_OBJECT
public:
  SelectionRotationDialog(xerahs::editor::AnnotationDocument, QSet<QUuid>, QWidget *parent = nullptr);
  std::optional<double> degrees() const { return m_degrees; }
  void accept() override;
private:
  void refreshPreview();
  const xerahs::editor::AnnotationDocument m_document;
  const QSet<QUuid> m_selection;
  QLineEdit *m_angle;
  QPlainTextEdit *m_preview;
  QLabel *m_status;
  QPushButton *m_rotate;
  std::optional<double> m_previewDegrees, m_degrees;
};
SelectionRotationChoice chooseSelectionRotation(const xerahs::editor::AnnotationDocument &, const QSet<QUuid> &, QWidget *parent);
} // namespace xerahs::app
