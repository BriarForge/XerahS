// Read-only desktop appearance I/O. No host settings are changed or persisted.
#pragma once
#include "image-editor/EditorAppearance.h"
#include <QDBusConnection>
#include <QDBusVariant>
#include <QObject>
#include <QVariantMap>
#include <array>
namespace xerahs::app {
using PortalAppearanceSettings = QMap<QString, QVariantMap>;
class NativeDesktopAppearance final : public QObject {
  Q_OBJECT
public:
  explicit NativeDesktopAppearance(QObject *parent = nullptr);
  ~NativeDesktopAppearance() override;
  void start(const QDBusConnection &connection = QDBusConnection::sessionBus(),
             const QString &service = QStringLiteral("org.freedesktop.portal.Desktop"));
  xerahs::editor::DesktopAppearance appearance() const;
signals:
  void changed();
private slots:
  void portalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);
private:
  void readAll();
  void publish(QJsonObject settings);
  void restoreMotion();
  QDBusConnection m_bus;
  QString m_service;
  QJsonObject m_settings;
  quint64 m_generation = 0;
  bool m_started = false, m_ownerAvailable = true, m_readPending = false;
  bool m_motionDisabled = false;
  std::array<bool, 6> m_previousEffects{};
};
NativeDesktopAppearance &nativeDesktopAppearance();
}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::PortalAppearanceSettings)
