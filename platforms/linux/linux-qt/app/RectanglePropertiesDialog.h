#pragma once
#include "image-editor/RectangleGeometry.h"
#include <QDialog>
#include <QMap>
#include <QSize>
class QLineEdit;
class QLabel;
namespace xerahs::app {
class RectanglePropertiesDialog final : public QDialog {
  Q_OBJECT
public:
  enum class Purpose { Edit, Create };
  explicit RectanglePropertiesDialog(const xerahs::editor::RectangleAnnotation &, QWidget *parent = nullptr,
      Purpose purpose = Purpose::Edit);
  std::optional<xerahs::editor::RectangleProperties> properties() const { return m_properties; }
  void accept() override;
private:
  xerahs::editor::RectangleProperties m_original;
  Purpose m_purpose;
  QMap<QString, QLineEdit *> m_fields;
  QLabel *m_status;
  std::optional<xerahs::editor::RectangleProperties> m_properties;
};
// Owned result only after explicit OK. Parent destruction is cancellation.
std::optional<xerahs::editor::RectangleProperties> chooseRectangleProperties(
    const xerahs::editor::RectangleAnnotation &, QWidget *parent);
std::optional<xerahs::editor::RectangleProperties> chooseRectangleCreation(
    const xerahs::editor::RectangleStyle &, QSize imageSize, QWidget *parent);
} // namespace xerahs::app
