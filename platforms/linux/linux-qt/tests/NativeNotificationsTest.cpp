#include "NativeNotifications.h"
#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusMessage>
#include <QTest>
#include <QUuid>

using namespace xerahs;
class NotificationServer : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
  int calls = 0;
  bool refuse = false;
  QString app, icon, title, body;
  QStringList actions;
  QVariantMap hints;
  uint replaces = 99;
  int duration = 0;
public slots:
  uint Notify(const QString &application, uint replacement, const QString &image,
              const QString &summary, const QString &text, const QStringList &buttons,
              const QVariantMap &attributes, int expires) {
    ++calls; app = application; replaces = replacement; icon = image;
    title = summary; body = text; actions = buttons; hints = attributes; duration = expires;
    if (refuse) sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"), QStringLiteral("Policy denied"));
    return 1;
  }
};
class NativeNotificationsTest : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()};
  NotificationServer endpoint;
private slots:
  void initTestCase() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "notification-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "notification-client");
    QVERIFY(server.isConnected()); QVERIFY(client.isConnected());
    QVERIFY(server.registerService("org.xerahs.TestNotifications"));
    QVERIFY(server.registerObject("/org/freedesktop/Notifications", &endpoint, QDBusConnection::ExportAllSlots));
  }
  void nativeDeliveryContainsOperationResultAndCorrelationWithoutPrivatePreviewsOrActions() {
    const auto event = shell::desktopNotification("Region capture saved", "/private/bank.png https://secret.test/token", true);
    QVERIFY(!QUuid(event.id).isNull());
    app::deliverNotification(event, true, 5000, client, "org.xerahs.TestNotifications");
    QTRY_COMPARE(endpoint.calls, 1);
    QCOMPARE(endpoint.app, QStringLiteral("XerahS"));
    QCOMPARE(endpoint.title, event.desktop.title);
    QVERIFY(endpoint.title.contains("Region capture saved"));
    QVERIFY(endpoint.title.contains(event.id));
    QCOMPARE(endpoint.body, QStringLiteral("Completed. Open XerahS for details."));
    QVERIFY(!endpoint.body.contains("private")); QVERIFY(!endpoint.body.contains("secret"));
    QVERIFY(endpoint.actions.isEmpty());
    QCOMPARE(endpoint.replaces, uint(0)); QCOMPARE(endpoint.duration, 5000);
    QCOMPARE(endpoint.hints.size(), 2);
    QCOMPARE(endpoint.hints.value("desktop-entry").toString(), QStringLiteral("com.xerahs.app"));
    QCOMPARE(endpoint.hints.value("urgency").toUInt(), uint(1));
    QVERIFY(!endpoint.hints.contains("image-data")); QVERIFY(!endpoint.hints.contains("sound-name"));
  }
  void refusalDoesNotRetryBypassPolicyOrCreateAnAction() {
    endpoint.refuse = true;
    const auto event = shell::desktopNotification("Recording without audio", "audio-device-missing: private microphone name", false);
    app::deliverNotification(event, false, 5000, client, "org.xerahs.TestNotifications");
    QTRY_COMPARE(endpoint.calls, 2);
    QCOMPARE(endpoint.body, QStringLiteral("audio-device-missing: attention required. Open XerahS for details."));
    QVERIFY(!endpoint.body.contains("microphone"));
    QVERIFY(endpoint.actions.isEmpty());
    QTest::qWait(100);
    QCOMPARE(endpoint.calls, 2);
  }
  void separateResultsHaveSeparateOpaqueCorrelations() {
    const auto first = shell::desktopNotification("Capture", "recognized private text", true);
    const auto second = shell::desktopNotification("Capture", "recognized private text", true);
    QVERIFY(first.id != second.id);
    QCOMPARE(first.desktop.body, second.desktop.body);
    QVERIFY(!first.desktop.title.contains("private"));
  }
};
QTEST_GUILESS_MAIN(NativeNotificationsTest)
#include "NativeNotificationsTest.moc"
