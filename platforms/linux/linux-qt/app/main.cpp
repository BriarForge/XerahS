// XerahS for Linux (linux-qt): tray application with region capture.
// Identity com.xerahs.app / "XerahS" (CORE-002, ROOT-IDENTITY-001).

#include "CaptureController.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QUrl>

#include <cstdio>

using xerahs::app::CaptureController;

namespace {

const QString kInstanceName = QStringLiteral("com.xerahs.app");
const QByteArray kCaptureRegion = QByteArrayLiteral("capture-region");
const QByteArray kActivate = QByteArrayLiteral("activate");

// Monochrome capture-frame mark, drawn so no asset pipeline is needed yet.
QIcon appIcon() {
  QIcon icon;
  for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = size;
    QPainterPath body;
    body.addRoundedRect(QRectF(s * 0.06, s * 0.06, s * 0.88, s * 0.88), s * 0.2, s * 0.2);
    p.fillPath(body, QColor(24, 24, 27));
    QPen pen(QColor(245, 245, 245), std::max(1.5, s * 0.085), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(pen);
    const double a = s * 0.27;
    const double b = s * 0.73;
    const double l = s * 0.15;
    for (const auto &[x, y, sx, sy] : {std::tuple{a, a, 1.0, 1.0}, std::tuple{b, a, -1.0, 1.0},
                                       std::tuple{a, b, 1.0, -1.0}, std::tuple{b, b, -1.0, -1.0}}) {
      p.drawLine(QPointF(x, y), QPointF(x + sx * l, y));
      p.drawLine(QPointF(x, y), QPointF(x, y + sy * l));
    }
    p.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

// CORE-006: deliver the request to a running instance. Returns true when one
// accepted it.
bool forwardToRunningInstance(const QByteArray &request) {
  QLocalSocket socket;
  socket.connectToServer(kInstanceName);
  if (!socket.waitForConnected(500)) return false;
  socket.write(request + '\n');
  socket.flush();
  if (!socket.waitForReadyRead(2000)) return false;
  return socket.readLine().trimmed() == "ok";
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("XerahS"));
  QApplication::setApplicationDisplayName(QStringLiteral("XerahS"));
  QApplication::setDesktopFileName(QStringLiteral("com.xerahs.app"));
  QApplication::setOrganizationDomain(QStringLiteral("xerahs.com"));
  QApplication::setQuitOnLastWindowClosed(false);
  QApplication::setWindowIcon(appIcon());

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("XerahS screen capture"));
  parser.addHelpOption();
  const QCommandLineOption captureRegion(QStringLiteral("capture-region"),
                                         QStringLiteral("Start a region capture (in the running instance if any)."));
  parser.addOption(captureRegion);
  parser.process(app);
  const QByteArray request = parser.isSet(captureRegion) ? kCaptureRegion : kActivate;

  if (forwardToRunningInstance(request)) return 0;

  QLocalServer server;
  QLocalServer::removeServer(kInstanceName);  // stale socket from a crash
  if (!server.listen(kInstanceName)) {
    std::fprintf(stderr, "xerahs: cannot claim the instance socket: %s\n", qPrintable(server.errorString()));
    return 1;
  }

  // Pending clarification (ROOT-ESCALATE-001): no settings store exists yet;
  // captures go to the XDG pictures directory, confirmed by the product owner.
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  CaptureController capture(QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("XerahS")));

  QSystemTrayIcon tray(appIcon());
  tray.setToolTip(QStringLiteral("XerahS"));
  QMenu menu;
  QAction *region = menu.addAction(QStringLiteral("Capture region"));
  QAction *folder = menu.addAction(QStringLiteral("Open captures folder"));
  menu.addSeparator();
  QAction *quit = menu.addAction(QStringLiteral("Quit XerahS"));
  tray.setContextMenu(&menu);

  QObject::connect(region, &QAction::triggered, &capture, &CaptureController::captureRegion);
  QObject::connect(folder, &QAction::triggered, [&capture] {
    QDir().mkpath(capture.saveDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(capture.saveDirectory()));
  });
  QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);
  QObject::connect(&tray, &QSystemTrayIcon::activated, [&capture](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) capture.captureRegion();
  });
  // CORE-007: availability is shown, not faked.
  QObject::connect(&capture, &CaptureController::busyChanged, region, [region](bool busy) {
    region->setEnabled(!busy);
    region->setText(busy ? QStringLiteral("Capture region (in progress)") : QStringLiteral("Capture region"));
  });
  QObject::connect(&capture, &CaptureController::notify, &tray,
                   [&tray](const QString &title, const QString &body, bool success) {
                     tray.showMessage(title, body, success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
                                      5000);
                   });

  QObject::connect(&server, &QLocalServer::newConnection, [&server, &capture] {
    while (QLocalSocket *socket = server.nextPendingConnection()) {
      QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, &capture] {
        if (!socket->canReadLine()) return;
        const QByteArray line = socket->readLine().trimmed();
        if (line == kCaptureRegion) {
          socket->write(capture.busy() ? "busy\n" : "ok\n");
          capture.captureRegion();
        } else if (line == kActivate) {
          socket->write("ok\n");
        } else {
          socket->write("unknown\n");
        }
        socket->flush();
        socket->disconnectFromServer();
      });
    }
  });

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    std::fprintf(stderr, "xerahs: no system tray host; use --capture-region to capture\n");
  }
  tray.show();
  if (request == kCaptureRegion) capture.captureRegion();
  return app.exec();
}
