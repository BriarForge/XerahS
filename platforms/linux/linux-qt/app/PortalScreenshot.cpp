#include "PortalScreenshot.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QRandomGenerator>
#include <QUrl>

namespace xerahs::app {

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenshot = QStringLiteral("org.freedesktop.portal.Screenshot");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");

}  // namespace

PortalScreenshot::PortalScreenshot(QObject *parent) : QObject(parent) {}

void PortalScreenshot::request() {
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    fail(Outcome::Failed, QStringLiteral("session bus unavailable"));
    return;
  }
  // Subscribe to the predictable request path before calling, so a fast
  // response cannot be missed.
  const QString token = QStringLiteral("xerahs%1").arg(QRandomGenerator::global()->generate());
  QString sender = bus.baseService().mid(1);
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
  bus.connect(kService, m_requestPath, kRequest, QStringLiteral("Response"), this,
              SLOT(onResponse(uint, QVariantMap)));

  QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kScreenshot, QStringLiteral("Screenshot"));
  call << QString() << QVariantMap{{QStringLiteral("handle_token"), token},
                                   {QStringLiteral("interactive"), false},
                                   {QStringLiteral("modal"), false}};
  auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
    const QDBusPendingReply<QDBusObjectPath> reply = *w;
    w->deleteLater();
    if (reply.isError()) {
      fail(Outcome::Failed, reply.error().message());
      return;
    }
    // Older portals return a different handle; follow it.
    const QString handle = reply.value().path();
    if (handle != m_requestPath) {
      QDBusConnection bus = QDBusConnection::sessionBus();
      bus.disconnect(kService, m_requestPath, kRequest, QStringLiteral("Response"), this,
                     SLOT(onResponse(uint, QVariantMap)));
      m_requestPath = handle;
      bus.connect(kService, m_requestPath, kRequest, QStringLiteral("Response"), this,
                  SLOT(onResponse(uint, QVariantMap)));
    }
  });
}

void PortalScreenshot::onResponse(uint response, const QVariantMap &results) {
  if (m_done) return;
  QDBusConnection::sessionBus().disconnect(kService, m_requestPath, kRequest, QStringLiteral("Response"), this,
                                           SLOT(onResponse(uint, QVariantMap)));
  // Portal response codes: 0 success, 1 cancelled by the user, 2 other.
  if (response == 1) {
    fail(Outcome::Denied, QStringLiteral("screenshot request was denied or cancelled"));
    return;
  }
  const QUrl uri(results.value(QStringLiteral("uri")).toString());
  if (response != 0 || !uri.isLocalFile()) {
    fail(Outcome::Failed, QStringLiteral("portal response %1").arg(response));
    return;
  }
  const QString file = uri.toLocalFile();
  QImage image(file);
  // RC-018: the portal's file is ours to clean up; keep no extra copy of the
  // screen pixels on disk.
  QFile::remove(file);
  if (image.isNull()) {
    fail(Outcome::Failed, QStringLiteral("portal image could not be decoded"));
    return;
  }
  m_done = true;
  emit finished(Outcome::Captured, image, QString());
}

void PortalScreenshot::fail(Outcome outcome, const QString &detail) {
  if (m_done) return;
  m_done = true;
  emit finished(outcome, QImage(), detail);
}

}  // namespace xerahs::app
