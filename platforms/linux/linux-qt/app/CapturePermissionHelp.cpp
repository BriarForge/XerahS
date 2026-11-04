#include "CapturePermissionHelp.h"
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
namespace xerahs::app {
namespace {
// CORE-009: the permission help has no Q_OBJECT class, so it names its own translation context.
QString text(const char *source) { return QCoreApplication::translate("CapturePermissionHelp", source); }
}
std::optional<CaptureSettingsRoute> captureSettingsRoute(const QStringList &searchPath, const QString &desktop) {
  const auto gnome = QStandardPaths::findExecutable(QStringLiteral("gnome-control-center"), searchPath);
  const auto kde = QStandardPaths::findExecutable(QStringLiteral("systemsettings"), searchPath);
  const bool preferKde = desktop.contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
  // The settings programs keep their product names; they are not translated.
  if (!kde.isEmpty() && (preferKde || gnome.isEmpty())) return CaptureSettingsRoute{kde, QStringLiteral("KDE System Settings"), {}};
  if (!gnome.isEmpty()) return CaptureSettingsRoute{gnome, QStringLiteral("GNOME Settings"), {QStringLiteral("privacy")}};
  return std::nullopt;
}
QString capturePermissionHelpText(bool settingsAvailable) {
  return text(
      "The desktop did not allow XerahS to capture the screen, so nothing was captured. XerahS does not ask again by itself.\n\n"
      "To allow it:\n"
      "• GNOME: Settings › Privacy › Screen sharing (or Screen recording), then capture again.\n"
      "• KDE Plasma: System Settings › Privacy, then capture again.\n"
      "• Any desktop that remembers portal answers: run `flatpak permission-reset com.xerahs.app` in a terminal to clear the saved denial, then capture again.\n\n")
      + (settingsAvailable ? text("Use Open Settings to go to your desktop's settings.") : text("No desktop settings app was found on this system, so follow the steps above manually."));
}
QDialog *makeCapturePermissionHelp(QWidget *parent, const CapturePermissionHelpPorts &ports) {
  const auto path = ports.searchPath ? ports.searchPath() : qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts);
  const auto desktop = ports.desktop ? ports.desktop() : qEnvironmentVariable("XDG_CURRENT_DESKTOP");
  const auto launch = ports.launch ? ports.launch : [](const QString &program, const QStringList &arguments) { return QProcess::startDetached(program, arguments); };
  const auto route = captureSettingsRoute(path, desktop);
  auto *dialog = new QDialog(parent); dialog->setObjectName(QStringLiteral("capturePermissionHelpDialog")); dialog->setWindowTitle(text("How to allow screen capture")); dialog->setAccessibleName(dialog->windowTitle());
  auto *layout = new QVBoxLayout(dialog);
  auto *body = new QLabel(capturePermissionHelpText(route.has_value()), dialog); body->setObjectName(QStringLiteral("capturePermissionHelpText")); body->setTextFormat(Qt::PlainText); body->setWordWrap(true); layout->addWidget(body);
  auto *status = new QLabel(dialog); status->setObjectName(QStringLiteral("capturePermissionHelpStatus")); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog); layout->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  if (route) {
    auto *open = buttons->addButton(text("&Open Settings"), QDialogButtonBox::ActionRole); open->setObjectName(QStringLiteral("openCaptureSettings")); open->setToolTip(text("Opens %1").arg(route->label)); open->setAutoDefault(false);
    // The guidance stays on screen: a failed start remains visible, and nothing here triggers a new capture request.
    QObject::connect(open, &QAbstractButton::clicked, dialog, [status, launch, route = *route] {
      status->setText(launch(route.program, route.arguments) ? QString{} : text("%1 could not be started. Follow the steps above manually.").arg(route.label));
    });
  }
  return dialog;
}
void showCapturePermissionHelp(QWidget *parent, const CapturePermissionHelpPorts &ports) {
  static QPointer<QDialog> open;
  if (open) { open->raise(); return; }
  open = makeCapturePermissionHelp(parent, ports); open->setAttribute(Qt::WA_DeleteOnClose); open->setWindowModality(Qt::NonModal); open->show();
}
}
