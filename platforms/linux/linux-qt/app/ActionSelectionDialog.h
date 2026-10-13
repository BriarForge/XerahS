#pragma once
#include <QDialog>
#include <QStringList>
#include <vector>
class QCheckBox;
class QLabel;
namespace xerahs::app {
class ActionSelectionDialog final : public QDialog {
  Q_OBJECT
public:
  ActionSelectionDialog(const QStringList &configured, const QSize &dimensions, QWidget *parent = nullptr);
  QStringList selectedActions() const { return m_selected; }
public slots:
  void accept() override;
private:
  std::vector<std::pair<QString, QCheckBox *>> m_choices;
  QStringList m_selected;
  QLabel *m_error = nullptr;
};
} // namespace xerahs::app
