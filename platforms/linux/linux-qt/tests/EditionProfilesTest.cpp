// CORE-034: every edition profile in linux-editions.json declares every required field, and the
// linux-qt profile's declared mechanisms are the ones the native application uses.
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

namespace {
QJsonDocument loadJson(const QString &path) {
  QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
  return QJsonDocument::fromJson(file.readAll());
}
QString source(const QString &relative) {
  QFile file(QStringLiteral(XERAHS_LINUX_QT_DIR "/") + relative);
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}
bool text(const QJsonValue &v) { return v.isString() && !v.toString().trimmed().isEmpty(); }
bool textArray(const QJsonValue &v) {
  if (!v.isArray() || v.toArray().isEmpty()) return false;
  for (const auto &item : v.toArray()) if (!text(item)) return false;
  return true;
}
}

class EditionProfilesTest : public QObject {
  Q_OBJECT
private slots:
  void everyEditionDeclaresEveryRequiredField() {
    const auto doc = loadJson(XERAHS_EDITIONS_JSON); QVERIFY2(doc.isObject(), "linux-editions.json must parse as an object");
    const auto editions = doc.object().value("editions").toArray(); QVERIFY(!editions.isEmpty());
    QSet<QString> ids; const QSet<QString> statuses{"supported", "candidate", "retired"}; int supported = 0;
    for (const auto &value : editions) {
      QVERIFY(value.isObject()); const auto e = value.toObject(); const auto id = e.value("id").toString();
      QVERIFY2(text(e.value("id")), "edition id"); QVERIFY2(!ids.contains(id), qPrintable("duplicate id " + id)); ids.insert(id);
      QVERIFY2(id.startsWith("linux-"), qPrintable(id));
      QVERIFY2(statuses.contains(e.value("status").toString()), qPrintable(id + " status"));
      if (e.value("status").toString() == "supported") ++supported;
      const auto targets = e.value("targets").toArray(); QVERIFY2(!targets.isEmpty(), qPrintable(id + " targets"));
      for (const auto &t : targets) {
        QVERIFY(t.isObject());
        QVERIFY2(text(t.toObject().value("distribution")) && text(t.toObject().value("versions")), qPrintable(id + " target"));
      }
      QVERIFY2(textArray(e.value("desktops")), qPrintable(id + " desktops"));
      QVERIFY2(text(e.value("display_server")), qPrintable(id + " display_server"));
      QVERIFY2(e.value("toolkit").isObject(), qPrintable(id + " toolkit"));
      const auto toolkit = e.value("toolkit").toObject();
      QVERIFY2(text(toolkit.value("value")), qPrintable(id + " toolkit value"));
      QVERIFY2(text(toolkit.value("decision")) && toolkit.value("decision").toString().startsWith("D-LIN-"),
               qPrintable(id + " deciding architecture record"));
      QVERIFY2(e.value("mechanisms").isObject(), qPrintable(id + " mechanisms"));
      const auto mechanisms = e.value("mechanisms").toObject();
      for (const auto *key : {"capture", "global_shortcuts", "notifications", "tray", "credentials"})
        QVERIFY2(text(mechanisms.value(key)), qPrintable(id + " mechanism " + key));
      QVERIFY2(textArray(e.value("packaging")), qPrintable(id + " packaging"));
    }
    QVERIFY(ids.contains("linux-qt")); QVERIFY2(supported >= 1, "at least one supported edition");
  }
  void linuxQtMechanismsMatchTheNativeApplication() {
    QJsonObject qt;
    for (const auto &v : loadJson(XERAHS_EDITIONS_JSON).object().value("editions").toArray())
      if (v.toObject().value("id") == "linux-qt") qt = v.toObject();
    QVERIFY(!qt.isEmpty()); QCOMPARE(qt.value("status").toString(), QString("supported"));
    const auto mechanisms = qt.value("mechanisms").toObject();
    // Toolkit: Qt 6 Widgets, C++17, as built.
    const auto toolkit = qt.value("toolkit").toObject().value("value").toString();
    QVERIFY(toolkit.startsWith("Qt 6 Widgets")); QVERIFY(toolkit.contains("C++17"));
    QCOMPARE(int(QT_VERSION_MAJOR), 6); QVERIFY(__cplusplus >= 201703L);
    QVERIFY(source("app/main.cpp").contains("QApplication"));
    // Capture: xdg-desktop-portal Screenshot and ScreenCast.
    QVERIFY(mechanisms.value("capture").toString().contains("Screenshot") && mechanisms.value("capture").toString().contains("ScreenCast"));
    QVERIFY(source("app/PortalScreenshot.cpp").contains("org.freedesktop.portal.Screenshot"));
    QVERIFY(source("app/RecordingController.cpp").contains("org.freedesktop.portal.ScreenCast"));
    // Global shortcuts: GlobalShortcuts portal.
    QVERIFY(mechanisms.value("global_shortcuts").toString().contains("GlobalShortcuts"));
    QVERIFY(source("app/PortalWorkflowShortcuts.cpp").contains("org.freedesktop.portal.GlobalShortcuts"));
    // Credentials: Secret Service API.
    QVERIFY(mechanisms.value("credentials").toString().contains("Secret Service"));
    QVERIFY(source("app/SecretServiceCredentialStore.h").contains("org.freedesktop.secrets") && source("app/SecretServiceCredentialStore.cpp").contains("org.freedesktop.Secret"));
    // Tray and notifications: QSystemTrayIcon, which Qt 6 implements over StatusNotifierItem and
    // org.freedesktop.Notifications on Linux; the profile names those protocols.
    QVERIFY(mechanisms.value("tray").toString().contains("StatusNotifierItem"));
    QVERIFY(mechanisms.value("notifications").toString().contains("org.freedesktop.Notifications"));
    const auto context = source("app/AppContext.cpp");
    QVERIFY(context.contains("QSystemTrayIcon") && context.contains("showMessage"));
    // Declared packaging formats for the reference edition.
    QStringList packaging; for (const auto &p : qt.value("packaging").toArray()) packaging << p.toString();
    QVERIFY(packaging.contains(".deb") && packaging.contains(".rpm") && packaging.contains("AUR") && packaging.contains(".tar.gz"));
  }
};
QTEST_APPLESS_MAIN(EditionProfilesTest)
#include "EditionProfilesTest.moc"
