// Native org.freedesktop.portal.Wallpaper transport (EDITOR-OUTPUT-ACTIONS-001
// EO-010). The desktop portal owns the actual wallpaper change and offers no
// per-display selection: it applies one image to every display, using the
// desktop's own fill/fit layout. The same Request/Response handling as
// PortalScreenshot is used: subscribe to the unique owner before the call and
// buffer early responses until the returned handle identifies the request.
#pragma once

#include <QDBusConnection>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariant>

class QDBusMessage;
class QDBusServiceWatcher;
class QTimer;

namespace xerahs::app {

struct PortalWallpaperConfig {
  QString service = QStringLiteral("org.freedesktop.portal.Desktop");
  // Bound the method reply, not the user's subsequent preview interaction.
  int callTimeoutMs = 25000;
};

class PortalWallpaper final : public QObject {
  Q_OBJECT

public:
  enum class Target { Background, Lockscreen, Both };
  Q_ENUM(Target)
  enum class Outcome { Applied, Cancelled, Denied, Unavailable, Failed };
  Q_ENUM(Outcome)

  static QString targetName(Target target);  // the portal's "set-on" value

  explicit PortalWallpaper(QObject *parent = nullptr);
  PortalWallpaper(QDBusConnection bus, PortalWallpaperConfig config = {}, QObject *parent = nullptr);
  ~PortalWallpaper() override;

  // Single-use. The file is opened here and handed to the portal as a file
  // descriptor; a missing or non-regular file fails without any portal call.
  void request(const QString &filePath, Target target, bool showPreview);
  void cancel();
  // Release a terminal client after any outstanding method reply has allowed
  // cancellation to close the portal's actual request handle. Bounded by the call timeout.
  void retire();

signals:
  void finished(xerahs::app::PortalWallpaper::Outcome outcome);

private slots:
  void onResponse(uint response, const QVariantMap &results, const QDBusMessage &message);
  void onDisconnected();

private:
  void resolveOwner();
  void startService();
  void callWallpaper();
  void closeRequest(const QString &path);
  void detach();
  void finish(Outcome outcome);

  QDBusConnection m_bus;
  PortalWallpaperConfig m_config;
  QDBusServiceWatcher *m_ownerWatcher = nullptr;
  QTimer *m_callTimer = nullptr;
  QString m_owner, m_observedOwner, m_requestPath, m_token, m_file;
  Target m_target = Target::Background;
  bool m_showPreview = true;
  bool m_ownerChanged = false, m_started = false, m_done = false;
  bool m_activationTried = false, m_subscribed = false, m_callIssued = false, m_handleReady = false;
  bool m_closeOnReply = false, m_responseReceived = false, m_waitingHandle = false, m_retiring = false;
  QMap<QString, uint> m_earlyResponses;
  QSet<QString> m_closedPaths;
};

}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalWallpaper::Outcome)
