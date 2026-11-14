// CORE-025 P7: the real process SIGKILL proof.
//
// The proof runs the watch upload in a separate process against a local HTTP
// server that stalls after receiving the first request. The parent kills the
// child with SIGKILL, then starts it again against the same store. The
// restarted process must not send a second request, and the startup notice
// must report the publication as unconfirmed.
//
// The store lives on a tmpfs mount, so the proof exercises the real durable
// path rather than an in-memory stand-in.
#include "watch/WatchFolders.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <csignal>
#include <sys/mount.h>
#include <thread>
#include <vector>

namespace {
// A local HTTP server that accepts one request and then stalls, so the child
// is still inside the request when it is killed.
class StallingServer final : public QObject {
  Q_OBJECT
public:
  QTcpServer server;
  int received = 0;
  int responseStatus = 0;
  bool listen() {
    connect(&server, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, wire = QByteArray(), counted = false]() mutable {
          wire += socket->readAll();
          if (counted) return;
          const auto split = wire.indexOf("\r\n\r\n");
          if (split < 0) return;
          const auto length = QRegularExpression("Content-Length: (\\d+)", QRegularExpression::CaseInsensitiveOption)
              .match(QString::fromLatin1(wire.left(split)));
          if (!length.hasMatch() || wire.size() < split + 4 + length.captured(1).toLongLong()) return;
          counted = true; ++received;
          if (responseStatus) {
            socket->write("HTTP/1.1 " + QByteArray::number(responseStatus) + " Reply\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
          }
        });
      }
    });
    return server.listen(QHostAddress::LocalHost);
  }
};
bool writeFile(const QString &path, const QByteArray &bytes = "watch kill fixture pixels") {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
// The tmpfs mount for the durable store. It is a private mount in the test's
// own temporary directory and is removed with it.
class TmpfsMount final {
public:
  bool mount(const QString &target) {
    m_target = target;
    QDir().mkpath(target);
    m_mounted = ::mount("tmpfs", target.toLocal8Bit().constData(), "tmpfs", 0, "size=16m") == 0;
    return m_mounted;
  }
  ~TmpfsMount() { unmount(); }
  void unmount() {
    if (m_mounted) { ::umount2(m_target.toLocal8Bit().constData(), 0); m_mounted = false; }
  }
  bool mounted() const { return m_mounted; }
private:
  QString m_target;
  bool m_mounted = false;
};
QString fixturePath() {
  return QCoreApplication::applicationDirPath() + QStringLiteral("/watch-kill-fixture");
}
}

class WatchKillProofTest final : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    // The proof needs a private tmpfs for the durable store. An unprivileged
    // mount needs its own user and mount namespace, so the test re-execs
    // itself under one. When that is not possible the proof is skipped rather
    // than faked.
    if (qEnvironmentVariableIsSet("XERAHS_WATCH_KILL_NS")) return;
    const auto self = QCoreApplication::applicationFilePath();
    // The unshare helper is in /usr/sbin or /usr/bin depending on the root.
    QString unshare = QStringLiteral("/usr/sbin/unshare");
    if (!QFileInfo::exists(unshare)) unshare = QStringLiteral("/usr/bin/unshare");
    if (!QFileInfo::exists(unshare)) { m_namespaceUnavailable = true; return; }
    QProcess child;
    child.start(unshare,
                {QStringLiteral("--user"), QStringLiteral("--map-root-user"),
                 QStringLiteral("--mount"), self, QStringLiteral("--ns-child")});
    if (!child.waitForStarted(10000)) { m_namespaceUnavailable = true; return; }
    child.waitForFinished(-1);
    m_namespaceExit = child.exitCode();
    m_namespaceOutput = QString::fromLocal8Bit(child.readAllStandardOutput())
        + QString::fromLocal8Bit(child.readAllStandardError());
    // The namespace started when the child produced test output. A failure
    // inside the namespace is a real failure, not a missing environment.
    m_namespaceUnavailable = m_namespaceExit != 0 && !m_namespaceOutput.contains(QStringLiteral("WatchKillProofTest"));
  }

  // The proof runs inside the namespace that owns the tmpfs.
  void aKilledProcessNeverUploadsTheFileAgain() {
    if (qEnvironmentVariableIsSet("XERAHS_WATCH_KILL_NS")) { proof(); return; }
    if (m_namespaceUnavailable) QSKIP("a private mount namespace is not available here");
    if (m_namespaceExit != 0) {
      qWarning("the proof inside the private mount namespace failed:\n%s", qPrintable(m_namespaceOutput));
      QFAIL("the watch kill proof failed inside the private mount namespace");
    }
  }

private:
private slots:
  void aCleanQuitReleasesKnownUnsentOrAnsweredWork_data() {
    QTest::addColumn<QString>("stage");
    QTest::newRow("preparation") << QString("clean-preparation");
    QTest::newRow("answered-journal") << QString("clean-answered");
  }
  void aCleanQuitReleasesKnownUnsentOrAnsweredWork() {
    if (!qEnvironmentVariableIsSet("XERAHS_WATCH_KILL_NS")) {
      if (m_namespaceUnavailable) QSKIP("a private mount namespace is not available here");
      QCOMPARE(m_namespaceExit, 0); return;
    }
    QFETCH(QString, stage);
    QTemporaryDir dir; QVERIFY(dir.isValid()); TmpfsMount mount;
    QVERIFY(mount.mount(dir.filePath("store"))); QVERIFY(QDir().mkpath(dir.filePath("input")));
    const auto source = dir.filePath("input/new.png"); QVERIFY(writeFile(source));
    StallingServer http; http.responseStatus = stage == "clean-answered" ? 503 : 0;
    QVERIFY(http.listen());
    const auto url = QString("http://127.0.0.1:%1/upload").arg(http.server.serverPort());
    QProcess child; child.start(fixturePath(), {dir.filePath("input"), dir.filePath("store"), url, stage});
    QVERIFY(child.waitForStarted());
    QTRY_COMPARE_WITH_TIMEOUT(child.state(), QProcess::NotRunning, 12000);
    QCOMPARE(child.exitStatus(), QProcess::NormalExit); QCOMPARE(child.exitCode(), 0);
    QVERIFY(child.readAllStandardOutput().contains("clean-loop-returned"));
    QCOMPARE(http.received, stage == "clean-answered" ? 1 : 0);
    xerahs::watch::WatchFolders restarted(dir.filePath("store"));
    QCOMPARE(restarted.loadError(), QString());
    QVERIFY(!restarted.publicationPending(xerahs::watch::fingerprint(source)));
    QVERIFY(!restarted.processed(xerahs::watch::fingerprint(source)));
  }

private:
  void proof() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TmpfsMount mount;
    QVERIFY(mount.mount(dir.filePath("store")));
    const auto store = dir.filePath("store");
    const auto input = dir.filePath("input");
    QVERIFY(QDir().mkpath(input));
    const auto source = input + QStringLiteral("/new.png");
    QVERIFY(writeFile(source));

    StallingServer http;
    QVERIFY(http.listen());
    const auto url = QStringLiteral("http://127.0.0.1:%1/upload").arg(http.server.serverPort());

    // The first process sends the request and is killed while it is in flight.
    QProcess child;
    child.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    child.start(fixturePath(), {input, store, url});
    QVERIFY(child.waitForStarted(10000));
    QTRY_COMPARE_WITH_TIMEOUT(http.received, 1, 20000);
    // SIGKILL: the process cannot clean up, so only the durable intent can
    // tell the next process that the publication was in flight.
    child.kill();
    QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitStatus(), QProcess::CrashExit);

    // The durable store survived the kill and records the sending publication.
    const auto storeFile = store + QStringLiteral("/watch-processed.json");
    QVERIFY(QFileInfo::exists(storeFile));
    QFile file(storeFile);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(file.readAll());
    file.close();
    QVERIFY(document.isObject());
    const auto publications = document.object().value("publications").toArray();
    QCOMPARE(publications.size(), 1);
    QCOMPARE(publications.first().toObject().value("key").toString(), xerahs::watch::fingerprint(source));
    QCOMPARE(publications.first().toObject().value("state").toString(), QStringLiteral("sending"));

    // The second process finds the leftover sending record. It must not send
    // the request again, and it must report the publication as unconfirmed.
    QProcess restarted;
    restarted.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    restarted.start(fixturePath(), {input, store, url, "observe"});
    QVERIFY(restarted.waitForStarted(10000));
    // Pump the parent event loop while the restarted watcher scans, so this
    // server can actually observe a forbidden second request.
    QTRY_COMPARE_WITH_TIMEOUT(restarted.state(), QProcess::NotRunning, 12000);
    QCOMPARE(restarted.exitCode(), 0);
    const auto output = QString::fromLocal8Bit(restarted.readAllStandardOutput());
    QVERIFY(output.contains("restart-observation-complete"));
    QVERIFY(output.contains(QStringLiteral("watch-publication-unconfirmed")));
    QVERIFY(output.contains(QStringLiteral("new.png")));
    // No path and no URL are disclosed in the notice.
    QVERIFY(!output.contains(dir.path()));
    QVERIFY(!output.contains(url));
    // The server never saw a second request.
    QCOMPARE(http.received, 1);

  }

  bool m_namespaceUnavailable = false;
  int m_namespaceExit = 0;
  QString m_namespaceOutput;
};

int main(int argc, char *argv[]) {
  // The proof needs a private tmpfs. An unprivileged mount requires its own
  // user and mount namespace, so the test re-execs itself under one. The
  // child marks itself with an environment variable and runs the proof.
  bool child = false;
  int childArgc = 1;
  std::vector<char *> childArgv(size_t(argc) + 1);
  childArgv[0] = argv[0];
  for (int i = 1; i < argc; ++i) {
    if (QByteArray(argv[i]) == "--ns-child") { child = true; continue; }
    childArgv[childArgc++] = argv[i];
  }
  if (child) qputenv("XERAHS_WATCH_KILL_NS", "1");
  QApplication app(childArgc, childArgv.data());
  WatchKillProofTest test;
  if (child) {
    // Run only the proof inside the namespace. The exit code carries the
    // result to the parent, so a failed proof fails the whole test.
    const auto result = QTest::qExec(&test, childArgc, childArgv.data());
    return result == 0 ? 0 : 1;
  }
  return QTest::qExec(&test, argc, argv);
}
#include "WatchKillProofTest.moc"
