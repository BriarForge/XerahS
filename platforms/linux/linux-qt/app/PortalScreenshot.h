// Native Screenshot/Request transport. Consent belongs to the desktop portal;
// product selection and post-capture behavior belong to the Qt Core libraries.
#pragma once

#include <QDBusConnection>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariant>
#include <memory>

class QDBusMessage;
class QDBusServiceWatcher;
class QTimer;

namespace xerahs::app {
struct ScreenshotDecode;
struct PortalScreenshotConfig {
  QString service = QStringLiteral("org.freedesktop.portal.Desktop");
  // Bound the method reply, not the user's subsequent consent interaction.
  int callTimeoutMs = 25000;
};

class PortalScreenshot final : public QObject {
  Q_OBJECT

public:
  enum class Outcome { Captured, Denied, Failed, Cancelled };
  Q_ENUM(Outcome)

  explicit PortalScreenshot(QObject *parent = nullptr);
  PortalScreenshot(QDBusConnection bus, PortalScreenshotConfig config = {}, QObject *parent = nullptr);
  ~PortalScreenshot() override;

  // Single-use: duplicate starts and events after the one terminal result
  // are ignored. Cancelling before request() prevents later acquisition.
  void request();
  void cancel();
  // Release a terminal client after any outstanding method reply has allowed
  // cancellation to close a portal's actual legacy handle. Reply time is bounded.
  void retire();

signals:
  void finished(xerahs::app::PortalScreenshot::Outcome outcome, const QImage &image, const QString &diagnostic);

private slots:
  void onResponse(uint response, const QVariantMap &results, const QDBusMessage &message);
  void onDisconnected();

private:
  struct Response { uint code; QVariant uri; };
  void resolveOwner();
  void startService();
  void callScreenshot();
  void consumeResponse(const Response &response);
  void decode(const QString &path);
  void pollDecode();
  void closeRequest(const QString &path);
  void detach();
  void finish(Outcome outcome, const QImage &image = {});

  QDBusConnection m_bus;
  PortalScreenshotConfig m_config;
  QDBusServiceWatcher *m_ownerWatcher = nullptr;
  QTimer *m_callTimer = nullptr;
  QTimer *m_decodeTimer = nullptr;
  QString m_owner, m_observedOwner, m_requestPath, m_token;
  bool m_ownerChanged = false, m_started = false, m_done = false;
  bool m_activationTried = false, m_subscribed = false, m_callIssued = false, m_handleReady = false;
  bool m_closeOnReply = false;
  bool m_responseReceived = false;
  bool m_waitingHandle = false, m_retiring = false;
  QMap<QString, Response> m_earlyResponses;
  QSet<QString> m_closedPaths;
  std::shared_ptr<ScreenshotDecode> m_decode;
};

}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalScreenshot::Outcome)
