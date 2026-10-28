#pragma once
#include "image-editor/AnnotationDocument.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QDoubleSpinBox;
class QLabel;
namespace xerahs::app {
// Edits existing content only. Caller guards document/selection identity and
// commits the complete choice through EditorSession as one history operation.
class TextContentDialog final : public QDialog {
  Q_OBJECT
public:
  TextContentDialog(const xerahs::editor::RectangleAnnotation &shape, QWidget *parent);
  std::optional<xerahs::editor::ShapeContent> choice() const { return m_choice; }
  void accept() override;
private:
  xerahs::editor::RectangleAnnotation m_shape;
  QPlainTextEdit *m_text;
  QDoubleSpinBox *m_size;
  double m_initialSize = 0;
  QCheckBox *m_tail = nullptr;
  QComboBox *m_alignment;
  QComboBox *m_family;
  QCheckBox *m_bold, *m_italic;
  QLineEdit *m_color;
  QLineEdit *m_x = nullptr, *m_y = nullptr;
  QLabel *m_error;
  std::optional<xerahs::editor::ShapeContent> m_choice;
};
}
