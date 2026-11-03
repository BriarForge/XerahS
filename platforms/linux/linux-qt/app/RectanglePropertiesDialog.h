#pragma once
#include "image-editor/RectangleGeometry.h"
#include <QDialog>
#include <QMap>
#include <QSize>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QFormLayout;
class QLabel;
namespace xerahs::app {
class RectanglePropertiesDialog final : public QDialog {
  Q_OBJECT
public:
  enum class Purpose { Edit, Create, Defaults };
  explicit RectanglePropertiesDialog(const xerahs::editor::RectangleAnnotation &, QWidget *parent = nullptr,
      Purpose purpose = Purpose::Edit);
  std::optional<xerahs::editor::RectangleProperties> properties() const { return m_properties; }
  void accept() override;
private:
  // EA-005 rows: border style, corner radius (Rectangle only), shadow.
  void addDecorationRows(QFormLayout *, const xerahs::editor::RectangleAnnotation &);
  bool readDecoration(xerahs::editor::RectangleStyle &);
  QComboBox *m_borderStyle = nullptr;
  QComboBox *m_arrowStyle = nullptr;  // EA-006: Arrow and the defaults dialog
  QCheckBox *m_curve = nullptr;       // EA-006: Line and Arrow curve anchor rows
  xerahs::editor::RectangleAnnotation m_source;
  QCheckBox *m_shadow = nullptr;
  bool m_hasCorner = false;
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
namespace xerahs::app {
// EA-005/EA-014: edits only the border style, corner radius and shadow
// creation defaults. Owned result only after explicit Apply.
std::optional<xerahs::editor::RectangleStyle> chooseDecorationDefaults(const xerahs::editor::RectangleStyle &, QWidget *parent);
} // namespace xerahs::app
