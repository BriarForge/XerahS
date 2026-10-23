// CORE-007, EU-001: tool windows. Each command keeps one non-modal dialog.
#pragma once

#include "CommandRegistry.h"

#include <QPointer>

#include <functional>

class QWidget;

namespace xerahs::app {

struct AppContext;

template <typename Dialog>
void presentOwnedDialog(QPointer<Dialog> &dialog, const std::function<Dialog *()> &create) {
  if (!dialog && create) dialog = create();
  if (!dialog) return;
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void installToolDialogs(CommandRegistry &commands, const std::function<QWidget *()> &parent);
void installTools(AppContext &ctx);

}  // namespace xerahs::app
