// Builds the shell exactly as main() does after instance election (offscreen tests).
#pragma once
#include "AppContext.h"
#include "ApplicationWindow.h"
#include "CaptureActions.h"
#include "CaptureController.h"
#include "CaptureWorkflows.h"
#include "CommandRegistry.h"
#include "NativeEditorPreferences.h"
#include "NativePipelineJournal.h"
#include "RecordingController.h"
#include "SecretServiceCredentialStore.h"
#include "features/CommandWindowAttach.h"
#include "features/ShellFeatures.h"
#include "settings/ApplicationSettings.h"
#include "shell/ActivationDispatcher.h"
#include <QApplication>
#include <QDir>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>
#include <memory>

namespace xerahs::app {
struct Shell {
  QString folder; xerahs::settings::ApplicationSettingsStore settings;
  QString catalog; std::shared_ptr<SecretServiceCredentialStore> credentials = std::make_shared<SecretServiceCredentialStore>();
  std::shared_ptr<NativePipelineJournal> journal; CaptureActionPorts ports; CaptureController capture;
  QSystemTrayIcon tray; QMenu menu; RecordingController recording; CommandRegistry commands; NativeCaptureWorkflows workflows;
  QPointer<ApplicationWindow> home; AppContext ctx; std::unique_ptr<xerahs::shell::ActivationDispatcher> dispatcher; std::unique_ptr<ApplicationWindow> windowOwner;

  static CaptureActionPorts makePorts(const QString &catalogPath) {
    CaptureActionPorts ports; ports.uploadCatalogPath = catalogPath;
    ports.editorPreferences = [] { return nativeEditorPreferences().values; }; return ports;
  }
  explicit Shell(const QString &root)
      : folder(QDir(root).filePath("captures")), settings(QDir(root).filePath("application-settings.json"), folder),
        catalog(QDir(root).filePath("upload-destinations.json")),
        journal(std::make_shared<NativePipelineJournal>(QDir(root).filePath("journal"))), ports(makePorts(catalog)),
        capture(settings.current().values.value("saveFolder").toString(), nullptr, {}, {}, ports, journal),
        commands(QApplication::instance()), workflows(QDir(root).filePath("capture-workflows.json")),
        ctx{commands, *qobject_cast<QApplication *>(QApplication::instance()), tray, capture, recording, settings, catalog, credentials} {
    ctx.workflows = &workflows; ctx.journal = journal; ctx.defaultCaptureFolder = folder;
    ctx.dialogParent = [this]() -> QWidget * { return home.data(); };
    ctx.notify = [this](const QString &title, const QString &body, bool ok, bool raiseWindow) {
      if (home) { home->showResult(title, body, ok); if (raiseWindow) home->activate(); }
    };
    dispatcher = installShellFeatures(ctx, false);
    windowOwner = std::make_unique<ApplicationWindow>(commands); home = windowOwner.get();  // After the installers, as in main().
  }
  ApplicationWindow &window() { return *windowOwner; }
  void attach() { commands.populate(&menu, CommandSurface::Tray); tray.setContextMenu(&menu); attachCommandWindow(ctx, *windowOwner, menu); }
};

}  // namespace xerahs::app
