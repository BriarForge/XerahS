#include "shell/NotificationPolicy.h"

#include <QRegularExpression>
#include <QCoreApplication>
#include <QUuid>

namespace xerahs::shell {

QString stableCodeOf(const QString &body) {
  static const QRegularExpression code(QStringLiteral("^([a-z][a-z0-9]*(?:-[a-z0-9]+)+):"));
  const auto match = code.match(body);
  return match.hasMatch() ? match.captured(1) : QString();
}

NotificationText presentNotification(const QString &title, const QString &body, bool ok, bool privatePreview) {
  if (!privatePreview) return {title, body};
  if (ok) return {title, QCoreApplication::translate("NativeNotifications", "Completed. Open XerahS for details.")};
  const auto code = stableCodeOf(body);
  return {title, code.isEmpty() ? QCoreApplication::translate("NativeNotifications", "Attention required. Open XerahS for details.") : code + QStringLiteral(": ") + QCoreApplication::translate("NativeNotifications", "attention required. Open XerahS for details.")};
}

NotificationEvent desktopNotification(const QString &operation, const QString &body, bool ok) {
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto text = presentNotification(operation, body, ok, true);
  text.title += QStringLiteral(" [%1]").arg(id);
  return {id, text};
}

bool privatePreviewsEnabled(const QJsonObject &settings) {
  const auto value = settings.value(QLatin1String(kPrivateNotificationsKey));
  return value.isBool() && value.toBool();
}

}  // namespace xerahs::shell
