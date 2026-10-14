#pragma once
#include "image-editor/RectangleGeometry.h"
#include <QDialog>
#include <QMap>
class QLineEdit;
class QLabel;
namespace xerahs::app {
class RectanglePropertiesDialog final : public QDialog {
  Q_OBJECT
public:
  explicit RectanglePropertiesDialog(const xerahs::editor::RectangleAnnotation &, QWidget *parent = nullptr);
  std::optional<xerahs::editor::RectangleProperties> properties() const { return m_properties; }
  void accept() override;
private:
  xerahs::editor::RectangleProperties m_original;
  QMap<QString, QLineEdit *> m_fields;
  QLabel *m_status;
  std::optional<xerahs::editor::RectangleProperties> m_properties;
};
// Owned result only after explicit OK. Parent destruction is cancellation.
std::optional<xerahs::editor::RectangleProperties> chooseRectangleProperties(
    const xerahs::editor::RectangleAnnotation &, QWidget *parent);
} // namespace xerahs::app
