#pragma once
#include "OwnedWork.h"
#include "diagnostics/DiagnosticBundle.h"
#include <QDialog>
#include <QPointer>
class QFileDialog;
class QLabel;
class QPushButton;

namespace xerahs::app {
class DiagnosticsDialog final : public QDialog {
  Q_OBJECT
public:
  explicit DiagnosticsDialog(diagnostics::DiagnosticBundle bundle, QWidget *parent = nullptr);
  ~DiagnosticsDialog() override;
  void done(int result) override;
private:
  void chooseFile();
  void write(const QString &path);
  diagnostics::DiagnosticBundle m_bundle;
  QPointer<QFileDialog> m_picker;
  QPushButton *m_export = nullptr;
  QLabel *m_status = nullptr;
  OwnedWork m_work;
  bool m_closing = false, m_writing = false;
};
}
