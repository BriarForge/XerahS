#include "shell/NotificationPolicy.h"

#include <QRegularExpression>

namespace xerahs::shell {

QString stableCodeOf(const QString &body) {
  static const QRegularExpression code(QStringLiteral("^([a-z][a-z0-9]*(?:-[a-z0-9]+)+):"));
  const auto match = code.match(body);
  return match.hasMatch() ? match.captured(1) : QString();
}

NotificationText presentNotification(const QString &title, const QString &body, bool ok, bool privatePreview) {
  if (!privatePreview) return {title, body};
  if (ok) return {title, QStringLiteral("Completed. Open XerahS for details.")};
  const auto code = stableCodeOf(body);
  return {title, code.isEmpty() ? QStringLiteral("Failed. Open XerahS for details.") : code + QStringLiteral(": failed. Open XerahS for details.")};
}

bool privatePreviewsEnabled(const QJsonObject &settings) {
  const auto value = settings.value(QLatin1String(kPrivateNotificationsKey));
  return value.isBool() && value.toBool();
}

}  // namespace xerahs::shell
