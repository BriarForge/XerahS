#include "NativeEditorWindowState.h"
#include "NativeEditorPreferences.h"
#include <QFile>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class NativeEditorWindowStateTest : public QObject {
  Q_OBJECT
private slots:
  void init() {
    initializeNativeEditorPreferences();
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
  }
  void cleanup() { initializeNativeEditorPreferences(); }
  void nativeGeometryRecoversAfterDisplayAndScaleChangesWithoutWriting() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    const auto original = bytes(path); const auto revision = nativeEditorPreferenceRevision();
    NativeWindowDisplays displays{{QRectF(0, 30, 1280, 690)}, 0};
    QWidget window; NativeEditorWindowState state(window, {500, 400}, [&] { return displays; });
    window.show(); QTRY_COMPARE(window.size(), QSize(640, 480));
    window.move(3000, 2000); state.recoverPlacement();
    QVERIFY(displays.available.front().contains(QRectF(window.frameGeometry())));
    displays.available = {QRectF(-640, -400, 640, 360)}; state.recoverPlacement();
    QVERIFY(displays.available.front().contains(QRectF(window.frameGeometry())));
    QVERIFY(window.width() <= 640 && window.height() <= 360);
    displays.available = {QRectF(0, 0, 320, 210)}; state.recoverPlacement();
    QVERIFY(displays.available.front().contains(QRectF(window.frameGeometry())));
    QVERIFY(window.width() < 400 && window.height() < 300);
    const auto geometry = window.geometry(); displays.available.clear(); state.recoverPlacement();
    QCOMPARE(window.geometry(), geometry); QVERIFY(state.snapshot().isEmpty());
    QCOMPARE(state.remember(), QStringLiteral("settings-window-display-unavailable"));
    QCOMPARE(bytes(path), original); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    displays.available = {QRectF(0, 0, 800, 600)}; state.recoverPlacement();
    QVERIFY(displays.available.front().contains(QRectF(window.frameGeometry())));
  }
  void maximizeAndMinimizeRetainNormalSizeAndPersistMaximizedState() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyWindowPreferences({{"WindowWidth", 640}, {"WindowHeight", 480}, {"IsWindowMaximized", false}}, nativeEditorPreferenceRevision()).saved);
    NativeWindowDisplays displays{{QRectF(0, 0, 1280, 720)}, 0};
    QWidget window; NativeEditorWindowState state(window, {500, 400}, [&] { return displays; }); window.show();
    QTRY_COMPARE(window.size(), QSize(640, 480)); window.showMaximized(); QTRY_VERIFY(window.isMaximized());
    auto snapshot = state.snapshot(); QCOMPARE(snapshot.value("WindowWidth"), QJsonValue(640));
    QCOMPARE(snapshot.value("WindowHeight"), QJsonValue(480)); QVERIFY(snapshot.value("IsWindowMaximized").toBool());
    window.showMinimized(); QTRY_VERIFY(window.isMinimized()); snapshot = state.snapshot();
    QCOMPARE(snapshot.value("WindowWidth"), QJsonValue(640)); QVERIFY(snapshot.value("IsWindowMaximized").toBool());
    QVERIFY(state.remember().isEmpty()); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto loaded = windowPreferencesFromPreferences(nativeEditorPreferences());
    QCOMPARE(loaded.normalSize, QSizeF(640, 480)); QVERIFY(loaded.maximized);
    displays.available = {QRectF(0, 30, 500, 360)}; window.showNormal(); QTRY_VERIFY(!window.isMinimized() && !window.isMaximized());
    QTRY_VERIFY(displays.available.front().contains(QRectF(window.frameGeometry())));
    QVERIFY(!state.snapshot().value("IsWindowMaximized").toBool());
  }
  void disabledMemoryStopsWritesAndUsesFallbackWhileFailurePreservesState() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyWindowPreferences({{"RememberWindowState", false}}, nativeEditorPreferenceRevision()).saved);
    const auto disabledBytes = bytes(path); const auto revision = nativeEditorPreferenceRevision();
    QWidget window; NativeEditorWindowState state(window, {500, 400}, [] { return NativeWindowDisplays{{QRectF(0, 0, 1280, 720)}, 0}; });
    window.show(); QTRY_COMPARE(window.size(), QSize(500, 400)); QVERIFY(!window.isMaximized()); window.resize(700, 500);
    QVERIFY(state.remember().isEmpty()); QCOMPARE(bytes(path), disabledBytes); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    QJsonObject enabled = state.snapshot(); enabled.insert("RememberWindowState", true);
    QVERIFY(applyWindowPreferences(enabled, revision).saved); const auto before = bytes(path);
    window.resize(600, 400); QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
    QCOMPARE(state.remember(), QStringLiteral("settings-locked")); QCOMPARE(bytes(path), before);
    QCOMPARE(windowPreferencesFromPreferences(nativeEditorPreferences()).normalSize, QSizeF(700, 500));
    lock.unlock(); QVERIFY(state.remember().isEmpty());
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(windowPreferencesFromPreferences(nativeEditorPreferences()).normalSize, QSizeF(600, 400));
  }
};
QTEST_MAIN(NativeEditorWindowStateTest)
#include "NativeEditorWindowStateTest.moc"
