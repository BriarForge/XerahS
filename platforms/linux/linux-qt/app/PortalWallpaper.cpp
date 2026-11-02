#include "PortalWallpaper.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace xerahs::app {
namespace {
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kWallpaper = QStringLiteral("org.freedesktop.portal.Wallpaper");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kRequestPrefix = kPath + QStringLiteral("/request/");
bool requestPath(const QString &path) { return path.startsWith(kRequestPrefix) && path.size() > kRequestPrefix.size(); }
bool permissionDenied(const QDBusError &error) {
  return error.type() == QDBusError::AccessDenied || error.name() == u"org.freedesktop.portal.Error.NotAllowed";
}
bool serviceMissing(const QDBusError &error) {
  const QString name = error.name();
  return name == u"org.freedesktop.DBus.Error.ServiceUnknown" || name == u"org.freedesktop.DBus.Error.NameHasNoOwner" ||
         name == u"org.freedesktop.DBus.Error.UnknownMethod" || name == u"org.freedesktop.DBus.Error.UnknownInterface" ||
         name == u"org.freedesktop.DBus.Error.UnknownObject";
}
using Outcome = PortalWallpaper::Outcome;
Outcome errorOutcome(const QDBusError &error) {
  return permissionDenied(error) ? Outcome::Denied : serviceMissing(error) ? Outcome::Unavailable : Outcome::Failed;
}
}

QString PortalWallpaper::targetName(Target target) {
  return target == Target::Lockscreen ? QStringLiteral("lockscreen") : target == Target::Both ? QStringLiteral("both") : QStringLiteral("background");
}
PortalWallpaper::PortalWallpaper(QObject *parent) : PortalWallpaper(QDBusConnection::sessionBus(), {}, parent) {}
PortalWallpaper::PortalWallpaper(QDBusConnection bus, PortalWallpaperConfig config, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_config(std::move(config)) {
  qRegisterMetaType<QDBusMessage>(); qRegisterMetaType<QVariantMap>(); qRegisterMetaType<Outcome>();
  m_callTimer = new QTimer(this); m_callTimer->setSingleShot(true);
  connect(m_callTimer, &QTimer::timeout, this, [this] { finish(Outcome::Failed); });
}
PortalWallpaper::~PortalWallpaper() {
  if (!m_done && m_callIssued && !m_responseReceived) closeRequest(m_requestPath);
  detach();
}
void PortalWallpaper::request(const QString &filePath, Target target, bool showPreview) {
  if (m_started || m_done) return;
  m_started = true; m_file = filePath; m_target = target; m_showPreview = showPreview;
  const QFileInfo info(filePath);
  if (!m_bus.isConnected() || !info.isAbsolute() || info.isSymLink() || !info.isFile()) { finish(Outcome::Failed); return; }
  m_token = QStringLiteral("xerahs") + QUuid::createUuid().toString(QUuid::Id128);
  QString sender = m_bus.baseService().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  m_requestPath = kRequestPrefix + sender + QLatin1Char('/') + m_token;
  m_ownerWatcher = new QDBusServiceWatcher(m_config.service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(m_ownerWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          [this](const QString &, const QString &, const QString &owner) {
    if (m_done) return;
    m_ownerChanged = true; m_observedOwner = owner;
    if (!m_owner.isEmpty() && owner != m_owner) finish(Outcome::Failed);
  });
  m_bus.connect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                QStringLiteral("Disconnected"), this, SLOT(onDisconnected()));
  m_callTimer->start(std::max(1, m_config.callTimeoutMs));
  resolveOwner();
}
void PortalWallpaper::resolveOwner() {
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                           QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  call << m_config.service;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QString> reply = *pending; pending->deleteLater(); if (m_done) return;
    if (reply.isError()) {
      if (!m_activationTried && reply.error().name() == u"org.freedesktop.DBus.Error.NameHasNoOwner") { startService(); return; }
      finish(errorOutcome(reply.error())); return;
    }
    m_owner = reply.value();
    if (!m_owner.startsWith(QLatin1Char(':')) || (m_ownerChanged && m_observedOwner != m_owner)) { finish(Outcome::Failed); return; }
    callWallpaper();
  });
}
void PortalWallpaper::startService() {
  m_activationTried = true;
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                           QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
  call << m_config.service << uint(0);
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<uint> reply = *pending; pending->deleteLater(); if (m_done) return;
    if (reply.isError()) { finish(errorOutcome(reply.error())); return; }
    resolveOwner();
  });
}
void PortalWallpaper::callWallpaper() {
  m_subscribed = m_bus.connect(m_owner, QString(), kRequest, QStringLiteral("Response"), this,
                               SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
  if (!m_subscribed) { finish(Outcome::Failed); return; }
  QFile file(m_file);
  if (!file.open(QIODevice::ReadOnly)) { finish(Outcome::Failed); return; }
  QDBusUnixFileDescriptor descriptor(file.handle());  // duplicates; the QFile may close at once
  if (!descriptor.isValid()) { finish(Outcome::Failed); return; }
  auto call = QDBusMessage::createMethodCall(m_owner, kPath, kWallpaper, QStringLiteral("SetWallpaperFile"));
  call << QString() << QVariant::fromValue(descriptor)
       << QVariantMap{{QStringLiteral("handle_token"), m_token}, {QStringLiteral("show-preview"), m_showPreview},
                      {QStringLiteral("set-on"), targetName(m_target)}};
  m_callIssued = true; m_waitingHandle = true;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QDBusObjectPath> reply = *pending; pending->deleteLater();
    m_waitingHandle = false;
    const auto retirement = qScopeGuard([this] { if (m_retiring) deleteLater(); });
    if (m_done) {
      if (m_closeOnReply && !reply.isError()) closeRequest(reply.value().path());
      return;
    }
    if (reply.isError()) { finish(errorOutcome(reply.error())); return; }
    const QString handle = reply.value().path();
    if (!requestPath(handle)) { finish(Outcome::Failed); return; }
    m_requestPath = handle; m_handleReady = true; m_callTimer->stop();
    const auto found = m_earlyResponses.constFind(handle);
    const bool early = found != m_earlyResponses.cend(); const uint code = early ? *found : 0;
    m_earlyResponses.clear();
    if (early) { m_responseReceived = true; finish(code == 0 ? Outcome::Applied : code == 1 ? Outcome::Cancelled : Outcome::Failed); }
  });
}
void PortalWallpaper::onResponse(uint response, const QVariantMap &, const QDBusMessage &message) {
  if (m_done || !m_subscribed || message.service() != m_owner || !requestPath(message.path())) return;
  if (!m_handleReady) {
    if (m_earlyResponses.size() < 8 && !m_earlyResponses.contains(message.path())) m_earlyResponses.insert(message.path(), response);
  } else if (message.path() == m_requestPath) {
    m_responseReceived = true;
    finish(response == 0 ? Outcome::Applied : response == 1 ? Outcome::Cancelled : Outcome::Failed);
  }
}
void PortalWallpaper::closeRequest(const QString &path) {
  if (m_owner.isEmpty() || !requestPath(path) || m_closedPaths.contains(path)) return;
  m_closedPaths.insert(path);
  m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, kRequest, QStringLiteral("Close")));
}
void PortalWallpaper::cancel() {
  if (m_done) return;
  finish(Outcome::Cancelled);
}
void PortalWallpaper::retire() {
  m_retiring = true;
  if (!m_waitingHandle) deleteLater();
}
void PortalWallpaper::onDisconnected() { finish(Outcome::Failed); }
void PortalWallpaper::detach() {
  m_callTimer->stop();
  m_earlyResponses.clear();
  if (m_subscribed) {
    m_bus.disconnect(m_owner, QString(), kRequest, QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
    m_subscribed = false;
  }
  m_bus.disconnect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                   QStringLiteral("Disconnected"), this, SLOT(onDisconnected()));
}
void PortalWallpaper::finish(Outcome outcome) {
  if (m_done) return;
  // Every abandoned interaction is closed; a completed Response already ended its Request.
  if (outcome != Outcome::Applied && m_callIssued && !m_responseReceived) {
    m_closeOnReply = m_waitingHandle; closeRequest(m_requestPath);
  }
  m_done = true; detach();
  emit finished(outcome);
}
} // namespace xerahs::app
