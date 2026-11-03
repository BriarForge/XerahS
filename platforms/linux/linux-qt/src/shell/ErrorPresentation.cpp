#include "ErrorPresentation.h"

namespace xerahs::shell {
namespace {
constexpr unsigned kReplyAndLaunch = WireReplySurface | LaunchSurface;
constexpr unsigned kStorage = DiscoverySurface | ExecutionSurface;
}

const std::vector<ErrorPresentation> &errorPresentations() {
  using R = RetryAdvice;
  static const std::vector<ErrorPresentation> table{
    {"usage-invalid", LaunchSurface | DiscoverySurface | ExecutionSurface, 1, R::AfterChange, "Choose one operation and use --help for supported arguments."},
    {"workflow-id-invalid", LaunchSurface, 1, R::AfterChange, "Use a full workflow UUID from Capture workflows."},
    {"workflow-unavailable", LaunchSurface, 1, R::AfterChange, "The saved workflow is missing or unavailable, or private execution storage could not be written. Review Capture workflows and private workflow storage before retrying."},
    {"presentation-required", LaunchSurface, 1, R::AfterChange, "This operation needs the graphical XerahS application for capture, consent or editing. No operation was started."},
    {"instance-unavailable", LaunchSurface, 1, R::Safe, "The running XerahS instance is not ready. Try again after startup finishes."},
    {"instance-storage-denied", LaunchSurface, 1, R::AfterChange, "XerahS could not secure its runtime files. Check the ownership and permissions of XDG_RUNTIME_DIR."},
    {"instance-endpoint-conflict", LaunchSurface, 1, R::AfterChange, "The XerahS runtime endpoint is in use or unsafe. Close conflicting instances and check runtime permissions."},
    {"instance-already-started", LaunchSurface, 1, R::AfterChange, "XerahS was already started in this process. Start a new XerahS command instead."},
    {"instance-lock-failed", LaunchSurface, 1, R::AfterChange, "XerahS could not take its instance lock. Check the ownership and permissions of XDG_RUNTIME_DIR before retrying."},
    {"instance-listen-failed", LaunchSurface, 1, R::AfterChange, "XerahS could not open its runtime endpoint. Check the ownership and permissions of XDG_RUNTIME_DIR before retrying."},
    {"activation-delivery-unknown", LaunchSurface, 1, R::CheckFirst, "XerahS did not acknowledge the request. Check the running instance before retrying."},
    {"activation-invalid", kReplyAndLaunch, 1, R::AfterChange, "The activation request is invalid or incompatible. Check the command and installed XerahS version."},
    {"activation-version-unsupported", kReplyAndLaunch, 1, R::AfterChange, "The activation request is invalid or incompatible. Check the command and installed XerahS version."},
    {"activation-busy", kReplyAndLaunch, 2, R::Safe, "XerahS is busy. Try again after the current capture or dialog finishes."},
    {"activation-unavailable", kReplyAndLaunch, 1, R::AfterChange, "The operation is unavailable. Review the saved workflow and private execution storage ownership, permissions and capacity before retrying."},
    {"activation-handler-failed", kReplyAndLaunch, 1, R::CheckFirst, "XerahS could not complete the activation. Check the running instance before retrying."},
    {"activation-peer-denied", kReplyAndLaunch, 1, R::AfterChange, "XerahS could not authenticate the running instance. Check your per-user runtime environment."},
    {"workflow-invalid", DiscoverySurface, 1, R::AfterChange, "Saved workflows are invalid. Open Capture workflows to review the saved configuration."},
    {"workflow-too-large", DiscoverySurface, 1, R::AfterChange, "Saved workflows exceed supported limits. Open Capture workflows to review the saved configuration."},
    {"workflow-newer-version", DiscoverySurface, 1, R::AfterChange, "Saved workflows need a newer XerahS version. They were kept unchanged."},
    {"workflow-upgrade-conflict", DiscoverySurface, 1, R::AfterChange, "Saved workflows need an explicit migration before use. They were kept unchanged."},
    {"workflow-changed", DiscoverySurface, 1, R::Safe, "Saved workflows changed while they were being read. Try again."},
    {"settings-unsafe-path", DiscoverySurface, 1, R::AfterChange, "The workflow storage location is unsafe. Check its ownership and permissions."},
    {"settings-private-storage-required", DiscoverySurface, 1, R::AfterChange, "Workflow storage must be private to your user. Check its permissions."},
    {"settings-too-large", DiscoverySurface, 1, R::AfterChange, "The saved workflow file is too large. Open Capture workflows to review the saved configuration."},
    {"settings-read-failed", DiscoverySurface, 1, R::AfterChange, "Saved workflows could not be read. Check the workflow storage permissions."},
    {"workflow-storage-unavailable", kStorage, 1, R::AfterChange, "Workflow storage is unavailable. Check the private workflow storage ownership, permissions and capacity."},
    {"workflow-execution-id-invalid", ExecutionSurface, 1, R::AfterChange, "Use the full launch request UUID reported when the workflow was started."},
    {"workflow-execution-not-found", ExecutionSurface, 1, R::AfterChange, "No execution was recorded for that request UUID. Check the UUID."},
    {"workflow-execution-invalid", ExecutionSurface, 1, R::AfterChange, "The recorded execution could not be read safely. It was kept unchanged."},
  };
  return table;
}

const ErrorPresentation *findErrorPresentation(const QString &code) {
  for (const auto &row : errorPresentations()) if (code == QLatin1String(row.code)) return &row;
  return nullptr;
}
bool isStableErrorCode(const QString &code, unsigned surface) {
  const auto *row = findErrorPresentation(code);
  return row && (row->surfaces & surface) != 0;
}
QString errorMessage(const QString &code) {
  if (const auto *row = findErrorPresentation(code)) return QString::fromUtf8(row->message);
  return QStringLiteral("XerahS could not complete the request. Check the running instance before retrying.");
}
int errorExitCode(const QString &code) {
  const auto *row = findErrorPresentation(code);
  return row ? row->exitCode : 1;
}
RetryAdvice retryAdvice(const QString &code) {
  const auto *row = findErrorPresentation(code);
  return row ? row->retry : RetryAdvice::CheckFirst;
}
} // namespace xerahs::shell
