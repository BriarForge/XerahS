// CORE-007, EU-001: the handles a feature installer needs. Installers register
// commands and connect them; they do not own the process lifetime.
#pragma once

#include "CommandRegistry.h"
#include "NativePipelineJournal.h"
#include "shell/ActivationProtocol.h"
#include "uploads/CredentialStore.h"

#include <QString>

#include <functional>
#include <memory>

class QApplication;
class QSystemTrayIcon;
class QWidget;

namespace xerahs::settings {
class ApplicationSettingsStore;
}
namespace xerahs::shell {
class ActivationDispatcher;
}

namespace xerahs::app {

class CaptureController;
class RecordingController;
class NativeCaptureWorkflows;
class WorkflowShortcutActions;
class RecordingActions;
class ScheduledCapture;

struct AppContext {
  CommandRegistry &commands;
  QApplication &app;
  QSystemTrayIcon &tray;
  CaptureController &capture;
  RecordingController &recording;
  settings::ApplicationSettingsStore &settings;
  QString uploadCatalogPath;
  std::shared_ptr<uploads::CredentialStore> uploadCredentials;

  // Set before feature installers run. Dialogs are created later, so installers
  // call dialogParent() when a command actually opens a window.
  shell::ActivationDispatcher *activation = nullptr;
  std::function<QWidget *()> dialogParent = {};
  std::function<void(const QString &title, const QString &body, bool ok, bool raiseWindow)> notify = {};

  // Filled by main or by the installer that owns the object, before a later
  // installer reads it. invocationOrigin is read by capture admission.
  NativeCaptureWorkflows *workflows = nullptr;
  WorkflowShortcutActions *shortcuts = nullptr;
  RecordingActions *recordingActions = nullptr;
  ScheduledCapture *schedule = nullptr;
  PipelineJournalStorage journal = {};
  QString defaultCaptureFolder = {};
  QString invocationOrigin = QStringLiteral("command-line");
  std::function<void(shell::ActivationAction action, const QString &path, bool chooseActions, const QString &workflowId, const QString &source)> invoke = {};
};

}  // namespace xerahs::app
