#pragma once
#include "image-editor/RasterSaveOptions.h"
#include <QFileDialog>
class QLabel;
class QLineEdit;
namespace xerahs::app {
class EditorSaveDialog final : public QFileDialog {
public:
  EditorSaveDialog(const QString &path, QWidget *parent = nullptr);
  xerahs::editor::RasterSaveOptions encodingOptions() const;
  void accept() override;
private:
  xerahs::editor::RasterFormat selectedFormat() const;
  void updateOptions();
  QLineEdit *m_quality, *m_matte;
  QLabel *m_qualityLabel, *m_matteLabel, *m_policy, *m_error;
};
}
