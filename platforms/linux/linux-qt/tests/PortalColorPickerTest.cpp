// INTERFACE-CATALOG-001: PickColor on a private, non-activating bus.
#include "PortalColorPicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using namespace xerahs::app;

namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");

class FakeRequest final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
  FakeRequest(QString path, QStringList &closed, QObject *parent) : QObject(parent), path(std::move(path)), closed(closed) {}
  QString path;
  QStringList &closed;
public slots:
  void Close() { closed.append(path); }
};

class FakeColorPortal final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Screenshot")
public:
  explicit FakeColorPortal(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerObject(root, this, QDBusConnection::ExportAllSlots)) qFatal("Fixture portal registration failed");
  }
  ~FakeColorPortal() override { bus.unregisterObject(root); }

  QDBusConnection bus;
  int calls = 0;
  QString parentWindow;
  QVariantMap options;
  QString handle;
  uint code = 0;
  bool sendColor = false;
  double red = 0, green = 0, blue = 0;
  QString error;
  QStringList closed;

  void respond() {
    auto signal = QDBusMessage::createSignal(handle, requestInterface, QStringLiteral("Response"));
    QVariantMap results;
    if (sendColor) {
      QDBusArgument color;
      color.beginStructure();
      color << red << green << blue;
      color.endStructure();
      results.insert(QStringLiteral("color"), QVariant::fromValue(color));
    }
    signal << code << results;
    if (!bus.send(signal)) qFatal("Fixture response failed");
  }

public slots:
  QDBusObjectPath PickColor(const QString &parent, const QVariantMap &requested) {
    ++calls;
    parentWindow = parent;
    options = requested;
    if (!error.isEmpty()) {
      sendErrorReply(error, QStringLiteral("Private fixture details must not escape"));
      return QDBusObjectPath(QStringLiteral("/"));
    }
    QString sender = message().service().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    handle = root + "/request/" + sender + "/" + requested.value(QStringLiteral("handle_token")).toString();
    auto *request = new FakeRequest(handle, closed, this);
    if (!bus.registerObject(handle, request, QDBusConnection::ExportAllSlots)) qFatal("Fixture request registration failed");
    return QDBusObjectPath(handle);
  }
};
}  // namespace

class PortalColorPickerTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()};
  using Outcome = PortalColorPicker::Outcome;
  static Outcome outcome(const QSignalSpy &spy) { return spy.at(0).at(0).value<Outcome>(); }
  static QColor resultColor(const QSignalSpy &spy) { return spy.at(0).at(1).value<QColor>(); }

private slots:
  void init() {
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("color-picker-test-server"));
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("color-picker-test-client"));
    QVERIFY(server.isConnected() && client.isConnected());
    QVERIFY(server.registerService(service));
  }
  void cleanup() {
    server.unregisterService(service);
    QDBusConnection::disconnectFromBus(QStringLiteral("color-picker-test-client"));
    QDBusConnection::disconnectFromBus(QStringLiteral("color-picker-test-server"));
  }

  void pickColorReturnsThePortalColor() {
    FakeColorPortal portal(server);
    portal.sendColor = true;
    portal.red = 1.0;
    portal.green = 0.5;
    portal.blue = 0.0;
    PortalColorPicker picker(client);
    QSignalSpy done(&picker, &PortalColorPicker::finished);
    picker.request();
    QTRY_COMPARE(portal.calls, 1);
    portal.respond();
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Picked);
    QCOMPARE(resultColor(done), QColor(255, 128, 0));
    QCOMPARE(done.at(0).at(2).toString(), QString());
    QCOMPARE(portal.parentWindow, QString());
    QVERIFY(portal.options.value(QStringLiteral("handle_token")).toString().startsWith(QStringLiteral("xerahs")));
    QVERIFY(portal.closed.isEmpty());
    portal.respond();
    picker.request();
    QTest::qWait(20);
    QCOMPARE(done.count(), 1);
    QCOMPARE(portal.calls, 1);
  }

  void userCancelLeavesNoColor() {
    FakeColorPortal portal(server);
    portal.code = 1;
    portal.sendColor = true;
    portal.red = 1.0;
    PortalColorPicker picker(client);
    QSignalSpy done(&picker, &PortalColorPicker::finished);
    picker.request();
    QTRY_COMPARE(portal.calls, 1);
    portal.respond();
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Cancelled);
    QVERIFY(!resultColor(done).isValid());
    QCOMPARE(done.at(0).at(2).toString(), QString());
    QVERIFY(portal.closed.isEmpty());
  }

  void otherFailureIsNotAColor() {
    FakeColorPortal portal(server);
    portal.code = 2;
    portal.sendColor = true;
    portal.red = 1.0;
    PortalColorPicker picker(client);
    QSignalSpy done(&picker, &PortalColorPicker::finished);
    picker.request();
    QTRY_COMPARE(portal.calls, 1);
    portal.respond();
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Failed);
    QVERIFY(!resultColor(done).isValid());
    QCOMPARE(done.at(0).at(2).toString(), QStringLiteral("color-picker-unavailable"));
  }

  void missingPortalFails() {
    QVERIFY(server.unregisterService(service));
    PortalColorPicker picker(client);
    QSignalSpy done(&picker, &PortalColorPicker::finished);
    picker.request();
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Failed);
    QVERIFY(!resultColor(done).isValid());
    QCOMPARE(done.at(0).at(2).toString(), QStringLiteral("color-picker-unavailable"));
  }

  void permissionDenialIsDistinctFromAMissingPortal() {
    FakeColorPortal portal(server);
    portal.error = QStringLiteral("org.freedesktop.portal.Error.NotAllowed");
    PortalColorPicker picker(client);
    QSignalSpy done(&picker, &PortalColorPicker::finished);
    picker.request();
    QTRY_COMPARE(done.count(), 1);
    QCOMPARE(outcome(done), Outcome::Denied);
    QVERIFY(!resultColor(done).isValid());
    QCOMPARE(done.at(0).at(2).toString(), QStringLiteral("color-picker-permission-denied"));
    QCOMPARE(portal.calls, 1);
  }
};

QTEST_MAIN(PortalColorPickerTest)
#include "PortalColorPickerTest.moc"
