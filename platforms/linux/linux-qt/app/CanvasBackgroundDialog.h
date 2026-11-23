// EC-013 on decision EC-013-BACKGROUND-COMPOSITION-2026-11-23: composed
// background options. A pure form - the committed canvas operation and its
// typed failures are owned by CanvasOperations and run through the shared
// editor canvas worker, so this dialog never touches pixels itself.
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace xerahs::editor {
struct CanvasOperation;
}

namespace xerahs::app {

class CanvasBackgroundDialog final : public QDialog {
  Q_OBJECT
public:
  explicit CanvasBackgroundDialog(QWidget *parent = nullptr);
  ~CanvasBackgroundDialog() override;
  // The decided defaults prefilled; every field is written explicitly so the
  // policy never depends on struct defaults after this dialog.
  xerahs::editor::CanvasOperation operation() const;
  void accept() override;

private:
  void pickColor(QPushButton *button, QColor &color);
  void refreshKinds();

  QComboBox *m_kind = nullptr;
  QPushButton *m_solid = nullptr, *m_gradientFrom = nullptr, *m_gradientTo = nullptr;
  QDoubleSpinBox *m_margin = nullptr, *m_padding = nullptr, *m_corner = nullptr, *m_shadow = nullptr;
  QCheckBox *m_smart = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  QLabel *m_status = nullptr;
  QColor m_solidColor{34, 34, 34};          // #FF222222, the reference default
  QColor m_gradientFromColor{221, 36, 118};  // #FFDD2476, the reference default
  QColor m_gradientToColor{255, 81, 47};     // #FFFF512F, the reference default
};

}  // namespace xerahs::app
