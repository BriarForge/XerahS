#include "PortalColorPicker.h"

#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <QtGlobal>

#include <cmath>
#include <limits>
#include <optional>

namespace xerahs::app {
namespace {
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenshot = QStringLiteral("org.freedesktop.portal.Screenshot");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kRequestPrefix = kPath + QStringLiteral("/request/");

bool requestPath(const QString &path) {
  return path.startsWith(kRequestPrefix) && path.size() > kRequestPrefix.size();
}

bool permissionDenied(const QDBusError &error) {
  return error.type() == QDBusError::AccessDenied || error.name() == u"org.freedesktop.portal.Error.NotAllowed";
}

QVariant unpackVariant(QVariant value) {
  if (value.canConvert<QDBusVariant>()) value = value.value<QDBusVariant>().variant();
  return value;
}

// PickColor returns color as (ddd) in 0..1. Non-finite components are refused.
std::optional<QColor> colorFromPortal(const QVariant &packed) {
  const QVariant value = unpackVariant(packed);
  double red = std::numeric_limits<double>::quiet_NaN();
  double green = red, blue = red;
  if (value.userType() == qMetaTypeId<QDBusArgument>()) {
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::StructureType) return std::nullopt;
    argument.beginStructure();
    argument >> red >> green >> blue;
    argument.endStructure();
  } else if (value.typeId() == QMetaType::QVariantList) {
    const QVariantList channels = value.toList();
    if (channels.size() != 3) return std::nullopt;
    red = channels.at(0).toDouble();
    green = channels.at(1).toDouble();
    blue = channels.at(2).toDouble();
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue)) return std::nullopt;
  const auto channel = [](double value) { return qBound(0, qRound(qBound(0.0, value, 1.0) * 255.0), 255); };
  return QColor(channel(red), channel(green), channel(blue));
}

}  // namespace

PortalColorPicker::PortalColorPicker(QObject *parent)
    : PortalColorPicker(QDBusConnection::sessionBus(), {}, parent) {}

PortalColorPicker::PortalColorPicker(QDBusConnection bus, PortalColorPickerConfig config, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_config(std::move(config)) {
  // Qt 6.4 requires every input type registered before the first DBus connect.
  qRegisterMetaType<QDBusMessage>();
  qRegisterMetaType<QVariantMap>();
  qRegisterMetaType<Outcome>();
  m_callTimer = new QTimer(this);
  m_callTimer->setSingleShot(true);
  connect(m_callTimer, &QTimer::timeout, this, [this] { finish(Outcome::Failed); });
}

PortalColorPicker::~PortalColorPicker() {
  if (!m_done && m_callIssued && !m_responseReceived) closeRequest(m_requestPath);
  detach();
}

void PortalColorPicker::request() {
  if (m_started || m_done) return;
  m_started = true;
  if (!m_bus.isConnected()) {
    finish(Outcome::Failed);
    return;
  }
  m_token = QStringLiteral("xerahs") + QUuid::createUuid().toString(QUuid::Id128);
  QString sender = m_bus.baseService().mid(1);
  sender.replace(QLatin1Char('.'), QLatin1Char('_'));
  m_requestPath = kRequestPrefix + sender + QLatin1Char('/') + m_token;
  m_ownerWatcher = new QDBusServiceWatcher(m_config.service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(m_ownerWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
          [this](const QString &, const QString &, const QString &owner) {
            if (m_done) return;
            m_ownerChanged = true;
            m_observedOwner = owner;
            if (!m_owner.isEmpty() && owner != m_owner) finish(Outcome::Failed);
          });
  m_bus.connect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                QStringLiteral("Disconnected"), this, SLOT(onDisconnected()));
  m_callTimer->start(std::max(1, m_config.callTimeoutMs));
  resolveOwner();
}

void PortalColorPicker::resolveOwner() {
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                              QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  call << m_config.service;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QString> reply = *pending;
    pending->deleteLater();
    if (m_done) return;
    if (reply.isError()) {
      if (!m_activationTried && reply.error().name() == u"org.freedesktop.DBus.Error.NameHasNoOwner") {
        startService();
        return;
      }
      finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed);
      return;
    }
    m_owner = reply.value();
    if (!m_owner.startsWith(QLatin1Char(':')) || (m_ownerChanged && m_observedOwner != m_owner)) {
      finish(Outcome::Failed);
      return;
    }
    callPickColor();
  });
}

void PortalColorPicker::startService() {
  m_activationTried = true;
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                              QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
  call << m_config.service << uint(0);
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<uint> reply = *pending;
    pending->deleteLater();
    if (m_done) return;
    if (reply.isError()) {
      finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed);
      return;
    }
    resolveOwner();
  });
}

void PortalColorPicker::callPickColor() {
  // Subscribe to the unique owner before calling, and keep a reply that
  // arrives before the handle is known. The colour is read only after that.
  m_subscribed = m_bus.connect(m_owner, QString(), kRequest, QStringLiteral("Response"), this,
                               SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
  if (!m_subscribed) {
    finish(Outcome::Failed);
    return;
  }
  auto call = QDBusMessage::createMethodCall(m_owner, kPath, kScreenshot, QStringLiteral("PickColor"));
  call << QString() << QVariantMap{{QStringLiteral("handle_token"), m_token}};
  m_callIssued = true;
  m_waitingHandle = true;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QDBusObjectPath> reply = *pending;
    pending->deleteLater();
    m_waitingHandle = false;
    if (m_done) {
      if (m_closeOnReply && !reply.isError()) closeRequest(reply.value().path());
      return;
    }
    if (reply.isError()) {
      finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed);
      return;
    }
    const QString handle = reply.value().path();
    if (!requestPath(handle)) {
      finish(Outcome::Failed);
      return;
    }
    m_requestPath = handle;
    m_handleReady = true;
    m_callTimer->stop();
    const auto found = m_earlyResponses.constFind(handle);
    const std::optional<Response> early = found == m_earlyResponses.cend() ? std::nullopt : std::optional<Response>(*found);
    m_earlyResponses.clear();
    if (early) consumeResponse(*early);
  });
}

void PortalColorPicker::onResponse(uint response, const QVariantMap &results, const QDBusMessage &message) {
  if (m_done || !m_subscribed || message.service() != m_owner || !requestPath(message.path())) return;
  const Response value{response, results.value(QStringLiteral("color"))};
  if (!m_handleReady) {
    if (m_earlyResponses.size() < 8 && !m_earlyResponses.contains(message.path()))
      m_earlyResponses.insert(message.path(), value);
  } else if (message.path() == m_requestPath) {
    consumeResponse(value);
  }
}

void PortalColorPicker::consumeResponse(const Response &response) {
  if (m_done) return;
  m_responseReceived = true;
  // Response 1 is the user dismissing the picker. 2 is any other ending.
  if (response.code == 1) {
    finish(Outcome::Cancelled);
    return;
  }
  if (response.code != 0) {
    finish(Outcome::Failed);
    return;
  }
  const std::optional<QColor> color = colorFromPortal(response.color);
  if (!color || !color->isValid()) {
    finish(Outcome::Failed);
    return;
  }
  finish(Outcome::Picked, *color);
}

void PortalColorPicker::closeRequest(const QString &path) {
  if (m_owner.isEmpty() || !requestPath(path) || m_closedPaths.contains(path)) return;
  m_closedPaths.insert(path);
  m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, kRequest, QStringLiteral("Close")));
}

void PortalColorPicker::cancel() {
  if (m_done) return;
  finish(Outcome::Cancelled);
}

void PortalColorPicker::onDisconnected() { finish(Outcome::Failed); }

void PortalColorPicker::detach() {
  m_callTimer->stop();
  m_earlyResponses.clear();
  if (m_subscribed) {
    m_bus.disconnect(m_owner, QString(), kRequest, QStringLiteral("Response"), this,
                     SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
    m_subscribed = false;
  }
  m_bus.disconnect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                   QStringLiteral("Disconnected"), this, SLOT(onDisconnected()));
}

void PortalColorPicker::finish(Outcome outcome, const QColor &color) {
  if (m_done) return;
  if (outcome != Outcome::Picked && m_callIssued && !m_responseReceived) {
    m_closeOnReply = m_waitingHandle;
    closeRequest(m_requestPath);
  }
  m_done = true;
  detach();
  const QString diagnostic = outcome == Outcome::Denied ? QStringLiteral("color-picker-permission-denied")
                             : outcome == Outcome::Failed ? QStringLiteral("color-picker-unavailable")
                                                          : QString();
  emit finished(outcome, outcome == Outcome::Picked ? color : QColor(), diagnostic);
}

}  // namespace xerahs::app
