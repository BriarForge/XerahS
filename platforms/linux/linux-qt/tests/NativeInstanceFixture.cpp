// Process harness for production election/intake. No GUI, capture, uploads or
// preference storage is linked or invoked; all paths are test runtime fixtures.
#include "NativeInstance.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QTimer>
#include <cstdio>
#include <stdexcept>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
using namespace xerahs::app;
using namespace xerahs::shell;
void print(QJsonObject value) {
  const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact); std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout); std::fputc('\n', stdout); std::fflush(stdout);
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv); const auto args = app.arguments();
  if (args.size() < 3) return 1;
  const auto directory = args.at(1), admission = args.at(2);
  auto request = makeActivationRequest(ActivationAction::Activate);
  if (args.size() >= 4) {
    const auto parsed = parseActivation(args.at(3).toUtf8()); if (!parsed.request) return 1; request = *parsed.request;
  }
  // A deliberately faulty real peer owns the kernel lease. The production
  // secondary must not re-elect itself after lost/invalid acknowledgements.
  if (admission.startsWith(u"ack-") || admission == u"no-listen") {
    if (!QDir().mkdir(directory) || !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) return 1;
    const int lease = open(QFile::encodeName(directory + "/instance.lock").constData(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lease < 0 || flock(lease, LOCK_EX | LOCK_NB) != 0) return 1;
    QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption);
    if (admission != u"no-listen" && !server.listen(directory + "/instance.sock")) return 1;
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
      while (auto *socket = server.nextPendingConnection()) {
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QLocalSocket::readyRead, [socket, admission] {
          if (!socket->canReadLine() || socket->property("received").toBool()) return;
          socket->setProperty("received", true);
          const auto parsed = parseActivation(socket->readLine().trimmed()); if (!parsed.request) { socket->abort(); return; }
          print({{"event", "received"}, {"id", parsed.request->id}});
          if (admission == u"ack-timeout") return;
          if (admission == u"ack-drop") { socket->abort(); return; }
          QByteArray reply = serializeActivationReply({parsed.request->id, ActivationReplyState::Accepted, {}}) + '\n';
          if (admission == u"ack-mismatch") reply = serializeActivationReply({makeActivationRequest(ActivationAction::Activate).id, ActivationReplyState::Accepted, {}}) + '\n';
          if (admission == u"ack-malformed") reply = "private invalid fixture\n";
          if (admission == u"ack-partial") reply = "{";
          if (admission == u"ack-oversized") reply = QByteArray(maximumActivationReplyBytes + 2, 'x') + '\n';
          if (admission == u"ack-two-frames") reply += reply;
          socket->write(reply); socket->flush(); socket->disconnectFromServer();
        });
      }
    });
    print({{"event", "primary"}}); QTimer::singleShot(30000, &app, &QCoreApplication::quit);
    const int result = app.exec(); server.close(); close(lease); return result;
  }
  NativeInstance instance(directory);
  if (admission != u"not-ready") instance.setAdmission([&](const ActivationRequest &) {
    if (admission == u"throw") throw std::runtime_error("Private fixture details must not escape");
    return admission == u"busy" ? ActivationReplyState::Busy : admission == u"reject" ? ActivationReplyState::Rejected : ActivationReplyState::Accepted;
  });
  QObject::connect(&instance, &NativeInstance::activationRequested, [&](const ActivationRequest &received) {
    print({{"event", "activation"}, {"request", QJsonDocument::fromJson(serializeActivation(received)).object()}});
  });
  const auto started = instance.start(request);
  const QString kind = started.kind == InstanceStart::Kind::Primary ? "primary" : started.kind == InstanceStart::Kind::Forwarded ? "forwarded" : started.kind == InstanceStart::Kind::Busy ? "busy" : "failed";
  print({{"event", kind}, {"error", started.diagnostic}});
  if (started.kind != InstanceStart::Kind::Primary) return started.kind == InstanceStart::Kind::Forwarded ? 0 : 2;
  QTimer::singleShot(30000, &app, &QCoreApplication::quit); return app.exec();
}
