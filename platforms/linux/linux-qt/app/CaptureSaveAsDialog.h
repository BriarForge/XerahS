#pragma once
#include <QFileDialog>
class QLabel;
namespace xerahs::app {
class CaptureSaveAsDialog final : public QFileDialog {
  Q_OBJECT
public:
  CaptureSaveAsDialog(const QString &directory, QWidget *parent = nullptr);
  QString targetPath() const { return m_target; }
public slots:
  void accept() override;
private:
  QString m_target;
  QLabel *m_error = nullptr;
};
} // namespace xerahs::app
