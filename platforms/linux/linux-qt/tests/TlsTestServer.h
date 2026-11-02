// Loopback HTTPS fixture (QSslServer) with the checked-in TEST-ONLY certificates
// from tests/tls (see tests/tls/generate.sh). Trust is injected into the
// downloader through its test seam; production trust is never altered.
#pragma once
#include <QFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QImage>
#include <QBuffer>
#include <QDir>

class TlsTestServer : public QObject {
public:
  static QString dir() { return QStringLiteral(QT_TESTCASE_SOURCEDIR "/tests/tls"); }
  static QList<QSslCertificate> ca() { return QSslCertificate::fromPath(dir() + "/ca.pem"); }
  // `name` is "server", "mismatch" or "selfsigned".
  explicit TlsTestServer(const QString &name, QObject *parent = nullptr) : QObject(parent) {
    QFile key(dir() + "/" + name + ".key"); key.open(QIODevice::ReadOnly);
    auto config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(QSslCertificate::fromPath(dir() + "/" + name + ".pem").value(0));
    config.setPrivateKey(QSslKey(&key, QSsl::Rsa));
    m_server.setSslConfiguration(config);
    connect(&m_server, &QSslServer::pendingConnectionAvailable, this, [this] {
      while (auto *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handle(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      }
    });
    m_server.listen(QHostAddress::LocalHost, 0);
  }
  bool ok() const { return m_server.isListening(); }
  QString base() const { return QStringLiteral("https://127.0.0.1:%1").arg(m_server.serverPort()); }
  QString hostBase() const { return QStringLiteral("https://localhost:%1").arg(m_server.serverPort()); }
  QByteArray png() const {
    QImage image(7, 5, QImage::Format_ARGB32); image.fill(qRgba(10, 200, 30, 255));
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
  }
  int requests = 0;  // completed HTTPS requests (a refused certificate never gets here)
private:
  void handle(QTcpSocket *s) {
    const auto request = s->readAll(); if (!request.contains("\r\n\r\n")) return;
    ++requests;
    const QByteArray path = request.split(' ').value(1);
    if (path == "/to-http") {
      s->write("HTTP/1.1 302 X\r\nLocation: http://127.0.0.1:1/ok.png\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    } else {
      const auto body = png();
      s->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    }
    s->disconnectFromHost();
  }
  QSslServer m_server;
};
