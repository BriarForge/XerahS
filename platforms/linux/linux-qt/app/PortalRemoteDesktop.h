// CORE-015: pointer scrolling through org.freedesktop.portal.RemoteDesktop.
// Consent belongs to the desktop portal; a refusal or absence is reported so
// the caller can fall back to manual scrolling, never silently ignored.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class QDBusMessage;

namespace xerahs::app {

struct PortalRemoteDesktopConfig {
  QString service = QStringLiteral("org.freedesktop.portal.Desktop");
  int callTimeoutMs = 25000;
};

class PortalRemoteDesktop final : public QObject {
  Q_OBJECT

public:
  enum class Outcome { Ready, Denied, Failed, Cancelled };
  Q_ENUM(Outcome)

  explicit PortalRemoteDesktop(QObject *parent = nullptr);
  PortalRemoteDesktop(QDBusConnection bus, PortalRemoteDesktopConfig config = {}, QObject *parent = nullptr);
  ~PortalRemoteDesktop() override;

  // CreateSession, SelectDevices(pointer), Start. Emits ready() or unavailable() once.
  void start();
  bool isReady() const { return m_ready; }
  // Relative pointer motion in logical pixels; done(true) when the portal accepted it.
  void movePointer(double dx, double dy, std::function<void(bool)> done = {});
  // Vertical wheel detents; positive scrolls content down (wheel toward the user).
  void scroll(int steps, std::function<void(bool)> done = {});
  // Closes the session and fails anything still pending. Idempotent.
  void close();

signals:
  void ready();
  void unavailable(xerahs::app::PortalRemoteDesktop::Outcome outcome, const QString &diagnostic);

private slots:
  void onResponse(uint code, const QVariantMap &results, const QDBusMessage &message);

private:
  enum class Step { Idle, Creating, Selecting, Starting, Ready, Closed };
  void request(const QString &method, QVariantList arguments, QVariantMap options, bool withSession);
  void notify(const QString &method, QVariantList arguments, std::function<void(bool)> done);
  void fail(Outcome outcome);

  QDBusConnection m_bus;
  PortalRemoteDesktopConfig m_config;
  Step m_step = Step::Idle;
  QString m_session, m_expectedPath;
  bool m_ready = false;
};

}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalRemoteDesktop::Outcome)
