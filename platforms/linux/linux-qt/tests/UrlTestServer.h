// Loopback HTTP fixture for EC-003 tests. Plain http is exercised only through
// the per-request opt-in; production defaults are not relaxed.
#pragma once
#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

class UrlTestServer : public QObject {
public:
  explicit UrlTestServer(QObject *parent = nullptr) : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
      while (auto *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
    m_server.listen(QHostAddress::LocalHost, 0);
  }
  bool ok() const { return m_server.isListening(); }
  QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
  QByteArray png() const {
    QImage image(7, 5, QImage::Format_ARGB32); image.fill(qRgba(10, 200, 30, 255));
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
  }
  int requests = 0;                 // total requests seen
  QStringList paths;                // request targets in order (including query)
  qint64 streamed = 0;              // bytes written by /big
  bool streamClosed = false;        // client dropped /big or /slow
  bool slowOpened = false;
  const qint64 plannedBig = 200 * 1024 * 1024;

private:
  void respond(QTcpSocket *s, int status, const QByteArray &type, const QByteArray &body, const QByteArray &extra = {}) {
    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
             "\r\nConnection: close\r\n" + extra + "\r\n" + body);
    s->disconnectFromHost();
  }
  void handle(QTcpSocket *s) {
    const auto request = s->readAll(); if (request.isEmpty() || !request.contains("\r\n\r\n")) return;
    const auto line = request.left(request.indexOf("\r\n")).split(' ');
    const QString target = line.value(1); const QString path = target.section('?', 0, 0);
    ++requests; paths << target;
    if (path == "/ok.png") return respond(s, 200, "image/png", png());
    if (path == "/fake.png") return respond(s, 200, "image/png", QByteArray("<html>not an image at all</html>").repeated(20));
    if (path == "/empty") return respond(s, 200, "image/png", {});
    if (path == "/missing") return respond(s, 404, "text/plain", "gone");
    if (path == "/loop") return respond(s, 302, "text/plain", {}, "Location: /loop\r\n");
    if (path == "/to-ftp") return respond(s, 302, "text/plain", {}, "Location: ftp://127.0.0.1/x.png\r\n");
    if (path == "/to-userinfo") return respond(s, 302, "text/plain", {}, "Location: http://user:pw@127.0.0.1:" + QByteArray::number(m_server.serverPort()) + "/ok.png\r\n");
    if (path == "/to-https") return respond(s, 302, "text/plain", {}, "Location: https://127.0.0.1:1/ok.png\r\n");
    if (path.startsWith("/r/")) {
      const int n = path.mid(3).toInt();
      return respond(s, 302, "text/plain", {}, "Location: " + (n > 0 ? QByteArray("/r/") + QByteArray::number(n - 1) : QByteArray("/ok.png")) + "\r\n");
    }
    if (path == "/slow") {  // header and a few bytes, then silence
      slowOpened = true; s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 100000\r\nConnection: close\r\n\r\nabc"); s->flush();
      connect(s, &QTcpSocket::disconnected, this, [this] { streamClosed = true; }); return;
    }
    if (path == "/big") {
      s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\n\r\n");
      auto *timer = new QTimer(s); QPointer<QTcpSocket> socket(s);
      connect(s, &QTcpSocket::disconnected, this, [this] { streamClosed = true; });
      connect(timer, &QTimer::timeout, this, [this, socket, timer] {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState || streamed >= plannedBig) { timer->stop(); if (socket) socket->disconnectFromHost(); return; }
        const QByteArray chunk(256 * 1024, 'z'); socket->write(chunk); streamed += chunk.size();
      });
      timer->start(1); return;
    }
    respond(s, 404, "text/plain", "unknown");
  }
  QTcpServer m_server;
};
