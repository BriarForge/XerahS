#include "NativeDesktopAppearance.h"
#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include <QApplication>
#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <limits>
using namespace xerahs::app;
using namespace xerahs::editor;
struct PortalRgb { double red = 0, green = 0, blue = 0; };
Q_DECLARE_METATYPE(PortalRgb)
QDBusArgument &operator<<(QDBusArgument &a, const PortalRgb &rgb) { a.beginStructure(); a << rgb.red << rgb.green << rgb.blue; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, PortalRgb &rgb) { a.beginStructure(); a >> rgb.red >> rgb.green >> rgb.blue; a.endStructure(); return a; }
class FakePortal final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")
public:
  QVariantMap values;
  QStringList namespaces;
  int reads = 0;
  bool delayNext = false, rejectReads = false;
  QDBusMessage pending;
  QDBusConnection replyBus{QString()};
  void finishDelayed(const QVariantMap &snapshot) {
    QVERIFY(replyBus.send(pending.createReply(QVariant::fromValue(PortalAppearanceSettings{{"org.freedesktop.appearance", snapshot}}))));
  }
  void change(const QString &key, const QVariant &value) {
    values.insert(key, value); emit SettingChanged(QStringLiteral("org.freedesktop.appearance"), key, QDBusVariant(value));
  }
public slots:
  PortalAppearanceSettings ReadAll(const QStringList &requested) {
    ++reads; namespaces = requested;
    if (rejectReads) { sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"), QStringLiteral("Unsupported")); return {}; }
    if (delayNext) { delayNext = false; setDelayedReply(true); pending = message(); replyBus = connection(); return {}; }
    return {{"org.freedesktop.appearance", values}};
  }
signals:
  void SettingChanged(const QString &nameSpace, const QString &key, const QDBusVariant &value);
};
class NativeDesktopAppearanceTest : public QObject {
  Q_OBJECT
  QDBusConnection m_server{QString()}, m_client{QString()};
  const QString m_service = QStringLiteral("org.xerahs.TestAppearance");
  QPalette m_native;
  void registerPortal(FakePortal &portal) {
    QVERIFY(m_server.registerService(m_service));
    QVERIFY(m_server.registerObject("/org/freedesktop/portal/desktop", &portal, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
  }
private slots:
  void init() {
    qDBusRegisterMetaType<PortalRgb>(); qDBusRegisterMetaType<PortalAppearanceSettings>();
    m_server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "appearance-test-server");
    m_client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "appearance-test-client");
    QVERIFY(m_server.isConnected() && m_client.isConnected()); m_native = QApplication::palette();
    // Supply an explicit native light palette; the developer's desktop may
    // already be dark, in which case retaining its colors is the correct result.
    m_native.setColor(QPalette::Window, QColor(240, 240, 240)); m_native.setColor(QPalette::WindowText, Qt::black);
    m_native.setColor(QPalette::Base, Qt::white); m_native.setColor(QPalette::Text, Qt::black);
    m_native.setColor(QPalette::Button, QColor(240, 240, 240)); m_native.setColor(QPalette::ButtonText, Qt::black);
    QApplication::setPalette(m_native); initializeNativeEditorPreferences();
  }
  void cleanup() {
    m_server.unregisterObject("/org/freedesktop/portal/desktop"); m_server.unregisterService(m_service);
    QDBusConnection::disconnectFromBus("appearance-test-client"); QDBusConnection::disconnectFromBus("appearance-test-server");
    initializeNativeEditorPreferences(); QApplication::setPalette(m_native);
  }
  void readOnlyPortalSnapshotAndSignalsUpdateNativeWidgetsWithoutSaving() {
    FakePortal portal; portal.values = {{"color-scheme", uint(1)}, {"accent-color", QVariant::fromValue(PortalRgb{1, 0.5, 0})},
        {"contrast", uint(0)}, {"reduced-motion", uint(0)}, {"private-unrelated", "never read"}}; registerPortal(portal);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyAppearancePreferences({{"UseSystemTheme", true}, {"UseSystemAccentColor", true}}, nativeEditorPreferenceRevision()).saved);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close(); const auto revision = nativeEditorPreferenceRevision();
    NativeDesktopAppearance source; QWidget window; NativeEditorAppearance appearance(window, source); window.show(); source.start(m_client, m_service);
    QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Dark);
    QCOMPARE(source.appearance().accent, std::optional<Argb>(0xFFFF8000)); QCOMPARE(portal.namespaces, QStringList{"org.freedesktop.appearance"});
    QCOMPARE(window.palette().color(QPalette::Window), QColor(41, 41, 41));
    portal.change("color-scheme", uint(2)); QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Light);
    QTRY_COMPARE(window.palette().color(QPalette::Window), m_native.color(QPalette::Window));
    portal.change("accent-color", QVariant::fromValue(PortalRgb{-0.1, 0, 1})); QTRY_VERIFY(!source.appearance().accent);
    portal.change("accent-color", QVariant::fromValue(PortalRgb{0, 0.5, 1})); QTRY_COMPARE(source.appearance().accent, std::optional<Argb>(0xFF0080FF));
    portal.change("contrast", uint(1)); QTRY_VERIFY(source.appearance().highContrast);
    QCOMPARE(window.palette().color(QPalette::Window), QColor(Qt::white));
    QVERIFY(appearanceContrastRatio(window.palette().color(QPalette::Text).rgba(), window.palette().color(QPalette::Base).rgba()) >= 7);
    portal.change("color-scheme", QString("1")); QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Unspecified);
    QVERIFY(source.appearance().highContrast); portal.change("contrast", uint(9)); QTRY_VERIFY(!source.appearance().highContrast);
    QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    QCOMPARE(QApplication::palette(), m_native);
  }
  void newerSignalWinsOverDelayedStartupSnapshot() {
    FakePortal portal; portal.values = {{"color-scheme", uint(1)}}; portal.delayNext = true; registerPortal(portal);
    NativeDesktopAppearance source; QList<DesktopColorScheme> observed;
    connect(&source, &NativeDesktopAppearance::changed, [&] { observed.append(source.appearance().colorScheme); }); source.start(m_client, m_service);
    QTRY_COMPARE(portal.reads, 1); const auto old = portal.values; portal.change("color-scheme", uint(2));
    QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Light); portal.finishDelayed(old);
    QTRY_COMPARE(portal.reads, 2); QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Light);
    QVERIFY(!observed.contains(DesktopColorScheme::Dark));
  }
  void ownerLossRejectsLateRepliesAndRestartReadsFreshHints() {
    FakePortal portal; portal.values = {{"color-scheme", uint(1)}}; portal.delayNext = true; registerPortal(portal);
    NativeDesktopAppearance source; source.start(m_client, m_service); QTRY_COMPARE(portal.reads, 1);
    portal.change("color-scheme", uint(2)); QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Light);
    QVERIFY(m_server.unregisterService(m_service)); QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Unspecified);
    portal.finishDelayed({{"color-scheme", uint(1)}}); QTest::qWait(50);
    QCOMPARE(source.appearance().colorScheme, DesktopColorScheme::Unspecified); QCOMPARE(portal.reads, 1);
    portal.values = {{"color-scheme", uint(2)}, {"accent-color", QVariant::fromValue(PortalRgb{0, 1, 0})}};
    QVERIFY(m_server.registerService(m_service)); QTRY_COMPARE(portal.reads, 2);
    QTRY_COMPARE(source.appearance().colorScheme, DesktopColorScheme::Light); QCOMPARE(source.appearance().accent, std::optional<Argb>(0xFF00FF00));
  }
  void unsupportedMissingAndDisconnectedPortalsKeepNativeFallback() {
    FakePortal portal; portal.rejectReads = true; registerPortal(portal); NativeDesktopAppearance source; QSignalSpy changes(&source, &NativeDesktopAppearance::changed);
    source.start(m_client, m_service); QTRY_COMPARE(portal.reads, 1); QTest::qWait(50);
    QCOMPARE(source.appearance().colorScheme, DesktopColorScheme::Unspecified); QVERIFY(!source.appearance().accent); QCOMPARE(changes.count(), 0);
    NativeDesktopAppearance absent; absent.start(m_client, "org.xerahs.NoSuchAppearancePortal"); QTest::qWait(50);
    QCOMPARE(absent.appearance().colorScheme, DesktopColorScheme::Unspecified);
    NativeDesktopAppearance disconnected; disconnected.start(QDBusConnection(QString()), m_service);
    QCOMPARE(disconnected.appearance().colorScheme, DesktopColorScheme::Unspecified);
  }
  void reducedMotionDisablesAndRestoresFrameworkEffectsWithoutSaving() {
    FakePortal portal; portal.values = {{"reduced-motion", uint(0)}}; registerPortal(portal);
    QApplication::setEffectEnabled(Qt::UI_General, true); QApplication::setEffectEnabled(Qt::UI_AnimateMenu, true);
    const bool initial = QApplication::isEffectEnabled(Qt::UI_AnimateMenu); QVERIFY(initial);
    const auto revision = nativeEditorPreferenceRevision();
    {
      NativeDesktopAppearance source; source.start(m_client, m_service); QTRY_COMPARE(portal.reads, 1);
      portal.change("reduced-motion", uint(1)); QTRY_VERIFY(source.appearance().reducedMotion);
      for (auto effect : {Qt::UI_AnimateMenu, Qt::UI_FadeMenu, Qt::UI_AnimateCombo, Qt::UI_AnimateTooltip, Qt::UI_FadeTooltip, Qt::UI_AnimateToolBox})
        QVERIFY(!QApplication::isEffectEnabled(effect));
      portal.change("reduced-motion", uint(0)); QTRY_VERIFY(!source.appearance().reducedMotion);
      QCOMPARE(QApplication::isEffectEnabled(Qt::UI_AnimateMenu), initial);
      portal.change("reduced-motion", uint(1)); QTRY_VERIFY(source.appearance().reducedMotion);
    }
    QCOMPARE(QApplication::isEffectEnabled(Qt::UI_AnimateMenu), initial); QCOMPARE(nativeEditorPreferenceRevision(), revision);
  }
};
QTEST_MAIN(NativeDesktopAppearanceTest)
#include "NativeDesktopAppearanceTest.moc"
