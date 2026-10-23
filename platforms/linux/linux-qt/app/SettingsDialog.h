#pragma once
#include "settings/ApplicationSettings.h"
#include "WorkflowShortcutActions.h"
#include <QDialog>
#include <functional>
namespace xerahs::app {
// Apply persists; Cancel discards all unapplied edits, including section resets.
class SettingsDialog final : public QDialog {
public:
  SettingsDialog(xerahs::settings::ApplicationSettingsStore &store, QString defaultFolder,
      const xerahs::workflows::WorkflowShortcutPlan &plan, WorkflowShortcutActions &shortcuts,
      std::function<void()> applied, QWidget *parent = nullptr);
};
}
