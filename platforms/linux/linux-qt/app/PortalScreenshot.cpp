#include "PortalScreenshot.h"
#include "RasterSource.h"
#include "capture/RegionGeometry.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDir>
#include <QThread>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>

namespace xerahs::app {
namespace {
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kScreenshot = QStringLiteral("org.freedesktop.portal.Screenshot");
const QString kRequest = QStringLiteral("org.freedesktop.portal.Request");
const QString kRequestPrefix = kPath + QStringLiteral("/request/");
bool requestPath(const QString &path) { return path.startsWith(kRequestPrefix) && path.size() > kRequestPrefix.size(); }
bool permissionDenied(const QDBusError &error) {
  return error.type() == QDBusError::AccessDenied || error.name() == u"org.freedesktop.portal.Error.NotAllowed";
}
}
struct ScreenshotDecode {
  std::atomic<bool> cancelled{false};
  std::mutex mutex;
  bool ready = false;
  QImage image;
};
PortalScreenshot::PortalScreenshot(QObject *parent)
    : PortalScreenshot(QDBusConnection::sessionBus(), {}, parent) {}
PortalScreenshot::PortalScreenshot(QDBusConnection bus, PortalScreenshotConfig config, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_config(std::move(config)) {
  // Qt 6.4 requires every input type registered before the first DBus connect.
  qRegisterMetaType<QDBusMessage>(); qRegisterMetaType<QVariantMap>(); qRegisterMetaType<Outcome>();
  m_callTimer = new QTimer(this); m_callTimer->setSingleShot(true);
  connect(m_callTimer, &QTimer::timeout, this, [this] {
    finish(Outcome::Failed);
  });
  m_decodeTimer = new QTimer(this); m_decodeTimer->setInterval(10);
  connect(m_decodeTimer, &QTimer::timeout, this, &PortalScreenshot::pollDecode);
}
PortalScreenshot::~PortalScreenshot() {
  if (!m_done && m_callIssued && !m_responseReceived) closeRequest(m_requestPath);
  detach();
}
void PortalScreenshot::request() {
  if (m_started || m_done) return;
  m_started = true;
  if (!m_bus.isConnected()) { finish(Outcome::Failed); return; }
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
void PortalScreenshot::resolveOwner() {
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                           QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
  call << m_config.service;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QString> reply = *pending; pending->deleteLater(); if (m_done) return;
    if (reply.isError()) {
      if (!m_activationTried && reply.error().name() == u"org.freedesktop.DBus.Error.NameHasNoOwner") { startService(); return; }
      finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed); return;
    }
    m_owner = reply.value();
    if (!m_owner.startsWith(QLatin1Char(':')) || (m_ownerChanged && m_observedOwner != m_owner)) { finish(Outcome::Failed); return; }
    callScreenshot();
  });
}
void PortalScreenshot::startService() {
  m_activationTried = true;
  auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                           QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
  call << m_config.service << uint(0);
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<uint> reply = *pending; pending->deleteLater(); if (m_done) return;
    if (reply.isError()) { finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed); return; }
    resolveOwner();
  });
}
void PortalScreenshot::callScreenshot() {
  // Subscribe to the unique owner before calling; buffer bounded early replies
  // until the returned handle identifies this request, including legacy paths.
  // No URI is read before its handle has been verified.
  m_subscribed = m_bus.connect(m_owner, QString(), kRequest, QStringLiteral("Response"), this,
                               SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
  if (!m_subscribed) { finish(Outcome::Failed); return; }
  auto call = QDBusMessage::createMethodCall(m_owner, kPath, kScreenshot, QStringLiteral("Screenshot"));
  call << QString() << QVariantMap{{QStringLiteral("handle_token"), m_token}, {QStringLiteral("interactive"), false},
                                   {QStringLiteral("modal"), false}};
  m_callIssued = true;
  m_waitingHandle = true;
  auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call, std::max(1, m_config.callTimeoutMs)), this);
  connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
    const QDBusPendingReply<QDBusObjectPath> reply = *pending; pending->deleteLater();
    m_waitingHandle = false;
    const auto retirement = qScopeGuard([this] { if (m_retiring) deleteLater(); });
    if (m_done) {
      if (m_closeOnReply && !reply.isError()) closeRequest(reply.value().path());
      return;
    }
    if (reply.isError()) { finish(permissionDenied(reply.error()) ? Outcome::Denied : Outcome::Failed); return; }
    const QString handle = reply.value().path();
    if (!requestPath(handle)) { finish(Outcome::Failed); return; }
    m_requestPath = handle; m_handleReady = true; m_callTimer->stop();
    const auto found = m_earlyResponses.constFind(handle);
    const std::optional<Response> early = found == m_earlyResponses.cend() ? std::nullopt : std::optional<Response>(*found);
    m_earlyResponses.clear(); if (early) consumeResponse(*early);
  });
}
void PortalScreenshot::onResponse(uint response, const QVariantMap &results, const QDBusMessage &message) {
  if (m_done || !m_subscribed || message.service() != m_owner || !requestPath(message.path())) return;
  QVariant uri = results.value(QStringLiteral("uri"));
  if (uri.metaType().id() != QMetaType::QString || uri.toString().size() > 16384) uri = {};
  const Response value{response, std::move(uri)};
  if (!m_handleReady) {
    if (m_earlyResponses.size() < 8 && !m_earlyResponses.contains(message.path())) m_earlyResponses.insert(message.path(), value);
  } else if (message.path() == m_requestPath) consumeResponse(value);
}
void PortalScreenshot::consumeResponse(const Response &response) {
  if (m_done || m_decode) return;
  m_responseReceived = true;
  if (response.code == 1) { finish(Outcome::Cancelled); return; }
  if (response.code != 0 || response.uri.metaType().id() != QMetaType::QString) { finish(Outcome::Failed); return; }
  const QUrl uri(response.uri.toString(), QUrl::StrictMode);
  // Qt represents a file URI with a host as a UNC path. localhost denotes
  // this host, so decode its local path rather than /localhost/path on Linux.
  const QString path = uri.host() == u"localhost" ? uri.path(QUrl::FullyDecoded) : uri.toLocalFile();
  if (!uri.isValid() || !uri.isLocalFile() || (!uri.host().isEmpty() && uri.host() != u"localhost") ||
      !uri.userInfo().isEmpty() || uri.port() != -1 || uri.hasQuery() || uri.hasFragment() ||
      !QDir::isAbsolutePath(path) || path.contains(QChar(0))) { finish(Outcome::Failed); return; }
  decode(path);
}
void PortalScreenshot::decode(const QString &path) {
  // URI access does not transfer ownership of arbitrary files. Borrow without
  // mutation; never write captured pixels to application-owned storage here.
  try {
    auto job = std::make_shared<ScreenshotDecode>(); m_decode = job;
    auto *worker = QThread::create([job, path] {
      QImage image;
      try {
        auto loaded = loadRaster(path, {[job] { return job->cancelled.load(); }, {}});
        if (loaded.diagnostic.isEmpty()) image = std::move(loaded.image);
      } catch (const std::exception &) {}
      std::lock_guard<std::mutex> lock(job->mutex);
      if (!job->cancelled.load()) job->image = std::move(image);
      job->ready = true;
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater); worker->start(); m_decodeTimer->start();
  } catch (const std::exception &) { finish(Outcome::Failed); }
}
void PortalScreenshot::pollDecode() {
  if (m_done || !m_decode) return;
  QImage image;
  {
    std::lock_guard<std::mutex> lock(m_decode->mutex);
    if (!m_decode->ready) return;
    image = std::move(m_decode->image);
  }
  finish(image.isNull() ? Outcome::Failed : Outcome::Captured, image);
}
void PortalScreenshot::closeRequest(const QString &path) {
  if (m_owner.isEmpty() || !requestPath(path) || m_closedPaths.contains(path)) return;
  m_closedPaths.insert(path);
  m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, kRequest, QStringLiteral("Close")));
}
void PortalScreenshot::cancel() {
  if (m_done) return;
  finish(Outcome::Cancelled);
}
void PortalScreenshot::retire() {
  m_retiring = true;
  if (!m_waitingHandle) deleteLater();
}
void PortalScreenshot::onDisconnected() { finish(Outcome::Failed); }
void PortalScreenshot::detach() {
  m_callTimer->stop(); m_decodeTimer->stop();
  if (m_decode) { m_decode->cancelled.store(true); m_decode.reset(); }
  m_earlyResponses.clear();
  if (m_subscribed) {
    m_bus.disconnect(m_owner, QString(), kRequest, QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap,QDBusMessage)));
    m_subscribed = false;
  }
  m_bus.disconnect(QString(), QStringLiteral("/org/freedesktop/DBus/Local"), QStringLiteral("org.freedesktop.DBus.Local"),
                   QStringLiteral("Disconnected"), this, SLOT(onDisconnected()));
}
void PortalScreenshot::finish(Outcome outcome, const QImage &image) {
  if (m_done) return;
  // Every abandoned native interaction is closed, including owner loss or an
  // invalid reply. A completed Response has already ended its Request object.
  if (outcome != Outcome::Captured && m_callIssued && !m_responseReceived) {
    m_closeOnReply = m_waitingHandle; closeRequest(m_requestPath);
  }
  m_done = true; detach();
  const QString diagnostic = outcome == Outcome::Denied ? xerahs::capture::diagnostic::capturePermissionDenied :
                             outcome == Outcome::Failed ? xerahs::capture::diagnostic::captureSourceFailed : QString();
  emit finished(outcome, outcome == Outcome::Captured ? image : QImage(), diagnostic);
}
} // namespace xerahs::app
