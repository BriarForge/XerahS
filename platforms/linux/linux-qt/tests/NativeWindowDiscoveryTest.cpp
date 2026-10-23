#include "WindowDiscovery.h"
#include <QDir>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>
#include <future>

using namespace xerahs;
class NativeWindowDiscoveryTest : public QObject {
  Q_OBJECT
private slots:
  void socketDiscovery_data() {
    QTest::addColumn<QByteArray>("clients");
    QTest::addColumn<bool>("hasTarget");
    const QByteArray client = R"([{"mapped":true,"hidden":false,"pid":123,"monitor":0,"workspace":{"id":1},"at":[10,20],"size":[30,40]}])";
    QTest::newRow("valid") << client << true;
    // Trailing whitespace forces the transport to consume a multi-read reply.
    QTest::newRow("large-reply") << (client + QByteArray(128 * 1024, ' ')) << true;
    QTest::newRow("malformed") << QByteArray("[") << false;
    QTest::newRow("timeout") << QByteArray() << false;
  }
  void socketDiscovery() {
    QFETCH(QByteArray, clients); QFETCH(bool, hasTarget);
    QTemporaryDir directory("/tmp/xerahs-ipc-XXXXXX"); QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.path() + "/hypr/test"));
    const auto runtime = qgetenv("XDG_RUNTIME_DIR"), signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    const auto restore = qScopeGuard([&] {
      if (runtime.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", runtime);
      if (signature.isNull()) qunsetenv("HYPRLAND_INSTANCE_SIGNATURE"); else qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature);
    });
    qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8()); qputenv("HYPRLAND_INSTANCE_SIGNATURE", "test");
    QSemaphore ready;
    QString listenError;
    auto server = std::async(std::launch::async, [&] {
      QLocalServer listener;
      const bool listening = listener.listen(directory.path() + "/hypr/test/.socket.sock");
      if (!listening) listenError = listener.errorString();
      ready.release();
      if (!listening) return false;
      for (const auto &command : {QByteArray("j/clients"), QByteArray("j/monitors")}) {
        if (!listener.waitForNewConnection(1000)) return false;
        std::unique_ptr<QLocalSocket> socket(listener.nextPendingConnection());
        if (!socket->bytesAvailable() && !socket->waitForReadyRead(1000)) return false;
        if (socket->readAll() != command) return false;
        if (clients.isEmpty()) {
          // An accepted connection that never replies must not stall capture.
          socket->waitForDisconnected(1000);
          return true;
        }
        const auto reply = command == "j/clients" ? clients : QByteArray(R"([{"id":0,"name":"display","activeWorkspace":{"id":1}}])");
        if (socket->write(reply) != reply.size()) return false;
        while (socket->bytesToWrite()) if (!socket->waitForBytesWritten(1000)) return false;
        socket->disconnectFromServer();
      }
      return true;
    });
    QVERIFY(ready.tryAcquire(1, 1000));
    QVERIFY2(listenError.isEmpty(), qPrintable(listenError));
    const capture::Topology topology{{"display", {0, 0, 100, 100}, {0, 0, 100, 100}, 1}};
    QElapsedTimer timer; timer.start();
    const auto targets = app::nativeWindowDiscovery()->targets(topology, 999);
    QVERIFY(timer.elapsed() < 1000);
    QVERIFY(server.get());
    QCOMPARE(targets.size(), hasTarget ? std::size_t(1) : std::size_t(0));
    if (hasTarget) QCOMPARE(targets[0].physical, (capture::PhysicalRect{10, 20, 40, 60}));
  }
  void unavailableCompositor() {
    const auto signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    const auto restore = qScopeGuard([&] {
      if (signature.isNull()) qunsetenv("HYPRLAND_INSTANCE_SIGNATURE"); else qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature);
    });
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    QVERIFY(app::nativeWindowDiscovery()->targets({}, 999).empty());
    QVERIFY(!app::nativeWindowDiscovery()->activeWindow({}, 999));
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", "xerahs-nonexistent-test-instance");
    QElapsedTimer timer; timer.start();
    QVERIFY(app::nativeWindowDiscovery()->targets({}, 999).empty());
    QVERIFY(!app::nativeWindowDiscovery()->activeWindow({}, 999));
    QVERIFY(timer.elapsed() < 1000);
  }
  void activeWindowReply_data() {
    QTest::addColumn<QString>("mode");
    QTest::newRow("focused") << QString("focused");
    QTest::newRow("own-pid") << QString("own-pid");
    QTest::newRow("empty") << QString("empty");
    QTest::newRow("off-screen") << QString("off-screen");
  }
  void activeWindowReply() {
    QFETCH(QString, mode);
    const QByteArray monitors = R"([{"id":0,"name":"display","activeWorkspace":{"id":1}}])";
    const QByteArray focused = R"({"mapped":true,"hidden":false,"pid":123,"monitor":0,"workspace":{"id":1},"at":[10,20],"size":[30,40]})";
    const QByteArray clients = R"([{"mapped":true,"hidden":false,"pid":999,"monitor":0,"workspace":{"id":1},"at":[50,50],"size":[10,10]},{"mapped":true,"hidden":false,"pid":123,"monitor":0,"workspace":{"id":1},"at":[1,1],"size":[8,8],"floating":false,"focusHistoryID":0},{"mapped":true,"hidden":false,"pid":124,"monitor":0,"workspace":{"id":1},"at":[10,20],"size":[30,40],"floating":true,"focusHistoryID":1}])";
    QList<QPair<QByteArray, QByteArray>> steps;
    if (mode == "focused") steps = {{"j/activewindow", focused}, {"j/monitors", monitors}};
    else if (mode == "own-pid") steps = {{"j/activewindow", R"({"mapped":true,"hidden":false,"pid":999,"monitor":0,"workspace":{"id":1},"at":[50,50],"size":[10,10]})"}, {"j/clients", clients}, {"j/monitors", monitors}};
    else if (mode == "empty") steps = {{"j/activewindow", "{}"}};
    else steps = {{"j/activewindow", R"({"mapped":true,"hidden":false,"pid":123,"monitor":0,"workspace":{"id":1},"at":[200,0],"size":[10,10]})"}, {"j/monitors", monitors}};
    QTemporaryDir directory("/tmp/xerahs-ipc-XXXXXX"); QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.path() + "/hypr/test"));
    const auto runtime = qgetenv("XDG_RUNTIME_DIR"), signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    const auto restore = qScopeGuard([&] {
      if (runtime.isNull()) qunsetenv("XDG_RUNTIME_DIR"); else qputenv("XDG_RUNTIME_DIR", runtime);
      if (signature.isNull()) qunsetenv("HYPRLAND_INSTANCE_SIGNATURE"); else qputenv("HYPRLAND_INSTANCE_SIGNATURE", signature);
    });
    qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8()); qputenv("HYPRLAND_INSTANCE_SIGNATURE", "test");
    QSemaphore ready; QString listenError;
    auto server = std::async(std::launch::async, [&] {
      QLocalServer listener;
      const bool listening = listener.listen(directory.path() + "/hypr/test/.socket.sock");
      if (!listening) listenError = listener.errorString();
      ready.release();
      if (!listening) return false;
      for (const auto &step : steps) {
        if (!listener.waitForNewConnection(1000)) return false;
        std::unique_ptr<QLocalSocket> socket(listener.nextPendingConnection());
        if (!socket->bytesAvailable() && !socket->waitForReadyRead(1000)) return false;
        if (socket->readAll() != step.first) return false;
        if (socket->write(step.second) != step.second.size()) return false;
        while (socket->bytesToWrite()) if (!socket->waitForBytesWritten(1000)) return false;
        socket->disconnectFromServer();
      }
      return true;
    });
    QVERIFY(ready.tryAcquire(1, 1000));
    QVERIFY2(listenError.isEmpty(), qPrintable(listenError));
    const capture::Topology topology{{"display", {0, 0, 100, 100}, {0, 0, 100, 100}, 1}};
    QElapsedTimer timer; timer.start();
    const auto window = app::nativeWindowDiscovery()->activeWindow(topology, 999);
    QVERIFY(timer.elapsed() < 1000);
    QVERIFY(server.get());
    if (mode == "empty" || mode == "off-screen") { QVERIFY(!window); return; }
    QVERIFY(window); QCOMPARE(window->physical, (capture::PhysicalRect{10, 20, 40, 60}));
  }
};
QTEST_GUILESS_MAIN(NativeWindowDiscoveryTest)
#include "NativeWindowDiscoveryTest.moc"
