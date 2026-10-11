#include "NativeDesktopAppearance.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonArray>

namespace xerahs::app {
namespace {
const QString appearanceNamespace = QStringLiteral("org.freedesktop.appearance");
const QString portalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString portalInterface = QStringLiteral("org.freedesktop.portal.Settings");
constexpr std::array<Qt::UIEffect, 6> effects{Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo,
    Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox};
bool known(const QString &key) {
  return key == u"color-scheme" || key == u"accent-color" || key == u"contrast" || key == u"reduced-motion";
}
QJsonValue translated(QVariant value, const QString &key) {
  if (value.metaType() == QMetaType::fromType<QDBusVariant>()) value = value.value<QDBusVariant>().variant();
  if (key == u"accent-color") {
    if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return QJsonValue::Null;
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != u"(ddd)") return QJsonValue::Null;
    double r = 0, g = 0, b = 0; argument.beginStructure(); argument >> r >> g >> b; argument.endStructure();
    return QJsonArray{r, g, b};
  }
  // Standard flags are uint32; strings, bools and signed values do not acquire
  // meaning through QVariant conversion.
  return value.metaType().id() == QMetaType::UInt ? QJsonValue(double(value.toUInt())) : QJsonValue(QJsonValue::Null);
}
}
NativeDesktopAppearance::NativeDesktopAppearance(QObject *parent) : QObject(parent), m_bus(QString()) {
  qDBusRegisterMetaType<PortalAppearanceSettings>();
}
NativeDesktopAppearance::~NativeDesktopAppearance() { restoreMotion(); }
xerahs::editor::DesktopAppearance NativeDesktopAppearance::appearance() const { return xerahs::editor::decodeDesktopAppearance(m_settings); }
void NativeDesktopAppearance::start(const QDBusConnection &connection, const QString &service) {
  if (m_started) return;
  m_started = true; m_bus = connection; m_service = service;
  if (!m_bus.isConnected()) return;
  m_bus.connect(m_service, portalPath, portalInterface, QStringLiteral("SettingChanged"), this,
      SLOT(portalSettingChanged(QString,QString,QDBusVariant)));
  auto *watcher = new QDBusServiceWatcher(m_service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
  connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &owner) {
    ++m_generation; m_ownerAvailable = !owner.isEmpty(); publish({});
    if (m_ownerAvailable) readAll();
  });
  readAll();
}
void NativeDesktopAppearance::readAll() {
  if (m_readPending || !m_ownerAvailable) return;
  m_readPending = true; const auto generation = m_generation;
  auto message = QDBusMessage::createMethodCall(m_service, portalPath, portalInterface, QStringLiteral("ReadAll"));
  message << QStringList{appearanceNamespace};
  auto *pending = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 1500), this);
  connect(pending, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *call) {
    QDBusPendingReply<PortalAppearanceSettings> reply = *call; call->deleteLater(); m_readPending = false;
    // A newer signal or owner change wins over an in-flight startup snapshot.
    if (generation != m_generation) { if (m_ownerAvailable) readAll(); return; }
    if (reply.isError()) return; // No portal/unsupported keys: use the native Qt palette.
    QJsonObject settings; const auto values = reply.value().value(appearanceNamespace);
    for (auto it = values.begin(); it != values.end(); ++it) if (known(it.key())) settings.insert(it.key(), translated(it.value(), it.key()));
    publish(std::move(settings));
  });
}
void NativeDesktopAppearance::portalSettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value) {
  if (nameSpace != appearanceNamespace || !known(key)) return;
  ++m_generation; auto next = m_settings; next.insert(key, translated(value.variant(), key)); publish(std::move(next));
}
void NativeDesktopAppearance::publish(QJsonObject settings) {
  if (settings == m_settings) return;
  m_settings = std::move(settings);
  if (qApp && appearance().reducedMotion && !m_motionDisabled) {
    for (size_t i = 0; i < effects.size(); ++i) {
      m_previousEffects[i] = QApplication::isEffectEnabled(effects[i]); QApplication::setEffectEnabled(effects[i], false);
    }
    m_motionDisabled = true;
  } else if (!appearance().reducedMotion) restoreMotion();
  emit changed();
}
void NativeDesktopAppearance::restoreMotion() {
  if (qApp && m_motionDisabled)
    for (size_t i = 0; i < effects.size(); ++i) QApplication::setEffectEnabled(effects[i], m_previousEffects[i]);
  m_motionDisabled = false;
}
NativeDesktopAppearance &nativeDesktopAppearance() { static NativeDesktopAppearance source; return source; }
}  // namespace xerahs::app
