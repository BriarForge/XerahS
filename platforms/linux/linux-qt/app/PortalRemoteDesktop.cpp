#include "PortalRemoteDesktop.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QUuid>

#include <algorithm>

namespace xerahs::app {
namespace {
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kRemote = QStringLiteral("org.freedesktop.portal.RemoteDesktop");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kSession = QStringLiteral("org.freedesktop.portal.Session");
constexpr uint kPointerDevice = 2;
bool denied(const QDBusError &error) {
  return error.type() == QDBusError::AccessDenied || error.name() == u"org.freedesktop.portal.Error.NotAllowed";
}
}  // namespace

PortalRemoteDesktop::PortalRemoteDesktop(QObject *parent) : PortalRemoteDesktop(QDBusConnection::sessionBus(), {}, parent) {}
PortalRemoteDesktop::PortalRemoteDesktop(QDBusConnection bus, PortalRemoteDesktopConfig config, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_config(std::move(config)) {
  qRegisterMetaType<QDBusMessage>(); qRegisterMetaType<QVariantMap>(); qRegisterMetaType<Outcome>();
}
PortalRemoteDesktop::~PortalRemoteDesktop() { close(); }

void PortalRemoteDesktop::start() {
  if (m_step != Step::Idle) return;
  if (!m_bus.isConnected()) { fail(Outcome::Failed); return; }
  m_step = Step::Creating;
  request(QStringLiteral("CreateSession"), {}, {{QStringLiteral("session_handle_token"), QStringLiteral("xerahs") + QUuid::createUuid().toString(QUuid::Id128)}}, false);
}

void PortalRemoteDesktop::request(const QString &method, QVariantList arguments, QVariantMap options, bool withSession) {
  const QString token = QStringLiteral("xerahs") + QUuid::createUuid().toString(QUuid::Id128);
  QString sender = m_bus.baseService().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  m_expectedPath = kPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
  options.insert(QStringLiteral("handle_token"), token);
  // Subscribe to the exact Request path before calling so no reply can be missed.
  if (!m_bus.connect(m_config.service, m_expectedPath, kRequest, QStringLiteral("Response"), this,
                     SLOT(onResponse(uint,QVariantMap,QDBusMessage)))) { fail(Outcome::Failed); return; }
  auto call = QDBusMessage::createMethodCall(m_config.service, kPath, kRemote, method);
  if (withSession) call << QVariant::fromValue(QDBusObjectPath(m_session));
  for (const auto &argument : arguments) call << argument;
  call << options;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QDBusObjectPath> reply = *pending; pending->deleteLater();
    if (m_step == Step::Ready || m_step == Step::Closed || m_step == Step::Idle) return;
    if (reply.isError()) fail(denied(reply.error()) ? Outcome::Denied : Outcome::Failed);
  });
}

void PortalRemoteDesktop::onResponse(uint code, const QVariantMap &results, const QDBusMessage &message) {
  if (message.path() != m_expectedPath) return;
  m_bus.disconnect(m_config.service, m_expectedPath, kRequest, QStringLiteral("Response"), this,
                   SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
  if (code == 1) { fail(Outcome::Denied); return; }  // the user declined the portal dialog
  if (code != 0) { fail(Outcome::Failed); return; }
  switch (m_step) {
    case Step::Creating:
      m_session = results.value(QStringLiteral("session_handle")).toString();
      if (m_session.isEmpty()) { fail(Outcome::Failed); return; }
      m_step = Step::Selecting;
      request(QStringLiteral("SelectDevices"), {}, {{QStringLiteral("types"), QVariant::fromValue<uint>(kPointerDevice)}}, true);
      return;
    case Step::Selecting:
      m_step = Step::Starting;
      request(QStringLiteral("Start"), {QString()}, {}, true);
      return;
    case Step::Starting:
      // The portal reports the devices actually granted; a pointer is required.
      if (results.contains(QStringLiteral("devices")) && !(results.value(QStringLiteral("devices")).toUInt() & kPointerDevice)) { fail(Outcome::Denied); return; }
      m_step = Step::Ready; m_ready = true;
      emit ready();
      return;
    default: return;
  }
}

void PortalRemoteDesktop::notify(const QString &method, QVariantList arguments, std::function<void(bool)> done) {
  if (m_step != Step::Ready) { if (done) done(false); return; }
  auto call = QDBusMessage::createMethodCall(m_config.service, kPath, kRemote, method);
  call << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariantMap{};
  for (const auto &argument : arguments) call << argument;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [done = std::move(done)](QDBusPendingCallWatcher *pending) {
    const bool ok = !pending->isError(); pending->deleteLater();
    if (done) done(ok);
  });
}

void PortalRemoteDesktop::movePointer(double dx, double dy, std::function<void(bool)> done) {
  notify(QStringLiteral("NotifyPointerMotion"), {dx, dy}, std::move(done));
}
void PortalRemoteDesktop::scroll(int steps, std::function<void(bool)> done) {
  notify(QStringLiteral("NotifyPointerAxisDiscrete"), {QVariant::fromValue<uint>(0), steps}, std::move(done));
}

void PortalRemoteDesktop::fail(Outcome outcome) {
  if (m_step == Step::Closed || m_step == Step::Ready) return;
  close();
  emit unavailable(outcome, outcome == Outcome::Denied ? QStringLiteral("scroll-input-denied") : QStringLiteral("scroll-input-unavailable"));
}

void PortalRemoteDesktop::close() {
  if (m_step == Step::Closed) return;
  const bool hadSession = !m_session.isEmpty();
  m_step = Step::Closed; m_ready = false;
  if (!m_expectedPath.isEmpty())
    m_bus.disconnect(m_config.service, m_expectedPath, kRequest, QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
  if (hadSession && m_bus.isConnected())
    m_bus.asyncCall(QDBusMessage::createMethodCall(m_config.service, m_session, kSession, QStringLiteral("Close")));
}

}  // namespace xerahs::app
