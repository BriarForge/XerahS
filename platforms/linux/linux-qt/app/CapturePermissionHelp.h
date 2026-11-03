// REGION-CAPTURE-001 RC-002: after a denied capture, explain how to re-allow
// screen capture and, where a desktop settings app exists, open it.
#pragma once
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
class QDialog;
class QWidget;
namespace xerahs::app {
struct CaptureSettingsRoute {
  QString program, label;
  QStringList arguments;
};
struct CapturePermissionHelpPorts {
  std::function<QStringList()> searchPath;                                  // default: $PATH
  std::function<QString()> desktop;                                         // default: $XDG_CURRENT_DESKTOP
  std::function<bool(const QString &, const QStringList &)> launch;         // default: QProcess::startDetached
};
// Prefers the running desktop's own settings app; nullopt when none is on the search path.
std::optional<CaptureSettingsRoute> captureSettingsRoute(const QStringList &searchPath, const QString &desktop);
QString capturePermissionHelpText(bool settingsAvailable);
// Builds the (unshown) guidance dialog. "Open Settings" exists only when a route was found.
QDialog *makeCapturePermissionHelp(QWidget *parent, const CapturePermissionHelpPorts &ports = {});
// Shows at most one guidance dialog at a time; never starts a capture or a portal request.
void showCapturePermissionHelp(QWidget *parent, const CapturePermissionHelpPorts &ports = {});
}
