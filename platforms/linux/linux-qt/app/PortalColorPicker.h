// INTERFACE-CATALOG-001: screen colour picker transport
// (EDITOR-UTILITIES-001 screen-color-picker). The desktop portal owns consent.
#pragma once

#include <QDBusConnection>
#include <QColor>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariant>

class QDBusMessage;
class QDBusServiceWatcher;
class QTimer;

namespace xerahs::app {

struct PortalColorPickerConfig {
  QString service = QStringLiteral("org.freedesktop.portal.Desktop");
  // Bound the method reply, not the time the user spends picking.
  int callTimeoutMs = 25000;
};

class PortalColorPicker final : public QObject {
  Q_OBJECT

public:
  enum class Outcome { Picked, Cancelled, Denied, Failed };
  Q_ENUM(Outcome)

  explicit PortalColorPicker(QObject *parent = nullptr);
  PortalColorPicker(QDBusConnection bus, PortalColorPickerConfig config = {}, QObject *parent = nullptr);
  ~PortalColorPicker() override;

  // Single-use. A second request() and any result after the first are ignored.
  // cancel() before request() prevents the portal call.
  void request();
  void cancel();

signals:
  void finished(xerahs::app::PortalColorPicker::Outcome outcome, const QColor &color, const QString &diagnostic);

private slots:
  void onResponse(uint response, const QVariantMap &results, const QDBusMessage &message);
  void onDisconnected();

private:
  struct Response {
    uint code = 0;
    QVariant color;
  };
  void resolveOwner();
  void startService();
  void callPickColor();
  void consumeResponse(const Response &response);
  void closeRequest(const QString &path);
  void detach();
  void finish(Outcome outcome, const QColor &color = {});

  QDBusConnection m_bus;
  PortalColorPickerConfig m_config;
  QDBusServiceWatcher *m_ownerWatcher = nullptr;
  QTimer *m_callTimer = nullptr;
  QString m_owner, m_observedOwner, m_requestPath, m_token;
  bool m_ownerChanged = false, m_started = false, m_done = false;
  bool m_activationTried = false, m_subscribed = false, m_callIssued = false, m_handleReady = false;
  bool m_closeOnReply = false, m_responseReceived = false, m_waitingHandle = false;
  QMap<QString, Response> m_earlyResponses;
  QSet<QString> m_closedPaths;
};

}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalColorPicker::Outcome)
