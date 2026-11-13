#pragma once
#include "settings/ApplicationSettings.h"
#include "settings/ConfigurationTransfer.h"
#include "WorkflowShortcutActions.h"
#include <QDialog>
#include <functional>
#include <memory>
namespace xerahs::app {
// Apply persists; Cancel discards all unapplied edits, including section resets.
class SettingsDialog final : public QDialog {
public:
  SettingsDialog(xerahs::settings::ApplicationSettingsStore &store, QString defaultFolder,
      const xerahs::workflows::WorkflowShortcutPlan &plan, WorkflowShortcutActions &shortcuts,
      std::function<void()> applied, QWidget *parent = nullptr, xerahs::settings::ConfigurationPaths transferPaths = {},
      std::function<void()> imported = {});
  void done(int result) override;
private:
  struct TransferState;
  std::shared_ptr<TransferState> m_transfer;
};
}
