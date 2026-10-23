// CORE-008: opens the setup guide on first interactive launch and from "Setup guide…".
// Store I/O runs through OwnedWork; the wizard never touches the disk itself.
#include "features/OnboardingFeature.h"

#include "AppContext.h"
#include "CaptureController.h"
#include "CaptureWorkflows.h"
#include "OnboardingWizard.h"
#include "OwnedWork.h"
#include "settings/ApplicationSettings.h"
#include "features/PreferencesFeature.h"

#include <QApplication>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <memory>
#include <optional>

namespace xerahs::app {
namespace {

class OnboardingController final : public QObject {
public:
  OnboardingController(AppContext &ctx, bool interactiveLaunch)
      : QObject(&ctx.app), m_ctx(ctx), m_interactive(interactiveLaunch),
        m_store(std::make_shared<OnboardingStore>(OnboardingStore::defaultPath())) {}

  void start() {
    OwnedWork::run<OnboardingLoad>(this, [store = m_store](const OwnedWork::Context &) { return store->load(); },
        [this](OnboardingLoad load) {
          m_load = std::move(load);
          m_loaded = true;
          if (!m_load.diagnostic.isEmpty()) report(m_load.diagnostic);
          if (m_openRequested || autoOpenOnboarding(m_interactive, m_load)) open();
        });
    connect(&m_ctx.capture, &CaptureController::notify, this, [this](const QString &, const QString &body, bool success) {
      if (!m_testing) return;
      m_lastBody = body;
      m_lastSuccess = success;
      m_gotResult = true;
      if (!m_ctx.capture.busy()) finishTest();
    });
    connect(&m_ctx.capture, &CaptureController::busyChanged, this, [this](bool busy) {
      if (!busy && m_testing && m_gotResult) finishTest();
    });
  }

  void open() {
    if (!m_loaded) { m_openRequested = true; return; }
    m_openRequested = false;
    if (m_wizard) { m_wizard->show(); m_wizard->raise(); m_wizard->activateWindow(); return; }
    auto &ctx = m_ctx;
    OnboardingPorts ports;
    ports.defaultFolder = ctx.defaultCaptureFolder;
    ports.settings = [&ctx] { return ctx.settings.current().values; };
    ports.saveSettings = [&ctx](const QJsonObject &values) {
      const auto error = ctx.settings.save(values);
      if (error.isEmpty()) applyShellVisibility(ctx);
      return error;
    };
    ports.defaultWorkflow = [&ctx] {
      OnboardingWorkflowInfo info;
      auto choice = ctx.workflows->resolve({});
      if (!choice.workflow) return info;
      const auto workflow = xerahs::settings::withApplicationDefaults(*choice.workflow, ctx.settings.current().values,
          ctx.workflows->store().hasSavedDefinitions());
      info.actions = workflow.actions;
      info.folder = workflow.output.directory;
      info.pattern = workflow.output.pattern;
      return info;
    };
    ports.runCommand = [&ctx](const QString &id) { if (auto *action = ctx.commands.action(id)) action->trigger(); };
    ports.persist = [this](const OnboardingState &state) { persist(state); };
    ports.testCapture = [this] { startTest(); };
    const QString notice = m_load.diagnostic.isEmpty() ? QString() : onboardingDiagnosticMessage(m_load.diagnostic);
    m_wizard = new OnboardingWizard(std::move(ports), m_load.state, notice, m_ctx.dialogParent ? m_ctx.dialogParent() : nullptr);
    m_wizard->setAttribute(Qt::WA_DeleteOnClose);
    m_wizard->show();
  }

private:
  void persist(const OnboardingState &state) {
    m_load.state = state;
    m_pending = state;
    flush();
  }

  // One save at a time, newest state wins, so writes never race on the snapshot.
  void flush() {
    if (m_saving || !m_pending) return;
    const auto state = *m_pending;
    m_pending.reset();
    m_saving = true;
    OwnedWork::run<QString>(this, [store = m_store, state](const OwnedWork::Context &) { return store->save(state); },
        [this](QString error) {
          m_saving = false;
          if (!error.isEmpty()) report(error);
          flush();
        });
  }

  void report(const QString &code) {
    if (m_reported.contains(code)) return;
    m_reported.insert(code);
    if (m_ctx.notify) m_ctx.notify(QStringLiteral("Setup guide"), onboardingDiagnosticMessage(code), false, false);
  }

  void startTest() {
    if (m_ctx.capture.busy()) {
      if (m_wizard) m_wizard->reportCaptureResult(false, QStringLiteral("XerahS is busy with another capture. Try again when it finishes."));
      return;
    }
    m_testing = true;
    m_gotResult = false;
    m_lastBody.clear();
    m_ctx.invoke(xerahs::shell::ActivationAction::CaptureFullScreen, {}, false, {}, QStringLiteral("native-shell"));
    // Admission or workflow resolution failed: nothing started and the capture
    // controller will not report, so say so instead of waiting.
    if (m_testing && !m_gotResult && !m_ctx.capture.busy()) {
      m_testing = false;
      if (m_wizard) m_wizard->reportCaptureResult(false, QStringLiteral("The capture could not be started. Check Capture workflows and try again."));
    }
  }

  void finishTest() {
    m_testing = false;
    if (m_wizard) m_wizard->reportCaptureResult(m_lastSuccess, m_lastBody);
  }

  AppContext &m_ctx;
  bool m_interactive;
  std::shared_ptr<OnboardingStore> m_store;
  OnboardingLoad m_load;
  bool m_loaded = false, m_openRequested = false, m_saving = false;
  bool m_testing = false, m_gotResult = false, m_lastSuccess = false;
  QString m_lastBody;
  std::optional<OnboardingState> m_pending;
  QSet<QString> m_reported;
  QPointer<OnboardingWizard> m_wizard;
};

}  // namespace

void installOnboarding(AppContext &ctx, bool interactiveLaunch) {
  auto *controller = new OnboardingController(ctx, interactiveLaunch);
  auto *action = ctx.commands.add(setupGuideCommand());
  if (action) QObject::connect(action, &QAction::triggered, controller, [controller] { controller->open(); });
  controller->start();
}

}  // namespace xerahs::app
