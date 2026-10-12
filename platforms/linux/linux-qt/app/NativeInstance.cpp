#include "NativeInstance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xerahs::app {
using namespace xerahs::shell;
namespace {
InstanceStart failure(const char *diagnostic) { return {InstanceStart::Kind::Failed, QString::fromLatin1(diagnostic)}; }
bool privateDirectory(int fd) {
  struct stat status{};
  return fd >= 0 && fstat(fd, &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == geteuid() && !(status.st_mode & 0077);
}
std::optional<quint64> kernelPeer(qintptr descriptor) {
  struct ucred credential{}; socklen_t size = sizeof(credential);
  if (descriptor < 0 || getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &credential, &size) != 0 || size != sizeof(credential)) return {};
  return quint64(credential.uid);
}
}
NativeInstance::NativeInstance(QString directory, NativeInstanceOptions options, QObject *parent)
    : QObject(parent), m_directory(QDir::cleanPath(directory)), m_options(std::move(options)) {
  m_options.timeoutMs = std::clamp(m_options.timeoutMs, 1, 30000);
  m_options.maximumClients = std::clamp(m_options.maximumClients, 1, 64);
  m_server.setSocketOptions(QLocalServer::UserAccessOption);
  m_server.setMaxPendingConnections(m_options.maximumClients);
  qRegisterMetaType<ActivationRequest>();
  connect(&m_server, &QLocalServer::newConnection, this, &NativeInstance::acceptConnections);
}
NativeInstance::~NativeInstance() {
  for (auto *socket : m_clients) { disconnect(socket, nullptr, this, nullptr); socket->abort(); }
  m_server.close();
  if (m_lockFd >= 0) close(m_lockFd);
  if (m_directoryFd >= 0) close(m_directoryFd);
}
QString NativeInstance::runtimeDirectory() {
  const auto root = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  return root.isEmpty() ? QString{} : QDir(root).filePath(QStringLiteral("com.xerahs.app"));
}
QString NativeInstance::endpoint() const { return QDir(m_directory).filePath(QStringLiteral("instance.sock")); }
bool NativeInstance::openDirectory() {
  if (!QDir::isAbsolutePath(m_directory) || m_directory.contains(QChar(0))) return false;
  const QFileInfo info(m_directory); const auto parent = QFile::encodeName(info.dir().absolutePath());
  const int parentFd = open(parent.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (!privateDirectory(parentFd)) { if (parentFd >= 0) close(parentFd); return false; }
  const auto leaf = QFile::encodeName(info.fileName());
  if (leaf.isEmpty() || leaf == "." || leaf == "..") { close(parentFd); return false; }
  if (mkdirat(parentFd, leaf.constData(), 0700) != 0 && errno != EEXIST) { close(parentFd); return false; }
  m_directoryFd = openat(parentFd, leaf.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  close(parentFd); return privateDirectory(m_directoryFd);
}
bool NativeInstance::authenticate(qintptr descriptor) const {
  return authorizedActivationPeer(quint64(geteuid()), m_options.peerUser ? m_options.peerUser(descriptor) : kernelPeer(descriptor));
}
InstanceStart NativeInstance::start(const ActivationRequest &request) {
  if (m_started) return failure("instance-already-started");
  m_started = true;
  const auto validated = parseActivation(serializeActivation(request));
  if (!validated.request) return {InstanceStart::Kind::Failed, validated.diagnostic};
  if (!openDirectory()) return failure("instance-storage-denied");
  m_lockFd = openat(m_directoryFd, "instance.lock", O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
  struct stat lockStatus{};
  if (m_lockFd < 0 || fstat(m_lockFd, &lockStatus) != 0 || !S_ISREG(lockStatus.st_mode) ||
      lockStatus.st_uid != geteuid() || lockStatus.st_nlink != 1 || (lockStatus.st_mode & 0077) || lockStatus.st_size != 0)
    return failure("instance-storage-denied");
  if (flock(m_lockFd, LOCK_EX | LOCK_NB) != 0) {
    if (errno != EWOULDBLOCK && errno != EAGAIN) return failure("instance-lock-failed");
    close(m_lockFd); m_lockFd = -1;
    return forward(*validated.request);
  }
  // Only the lifetime-lock owner can remove a genuinely stale socket. A live
  // uncooperative endpoint, a regular file or a symlink is never overwritten.
  struct stat node{};
  if (fstatat(m_directoryFd, "instance.sock", &node, AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISSOCK(node.st_mode) || node.st_uid != geteuid()) return failure("instance-endpoint-conflict");
    QLocalSocket probe; probe.connectToServer(endpoint());
    if (probe.waitForConnected(std::min(500, m_options.timeoutMs))) { probe.abort(); return failure("instance-endpoint-conflict"); }
    if (probe.error() != QLocalSocket::ConnectionRefusedError && probe.error() != QLocalSocket::ServerNotFoundError)
      return failure("instance-endpoint-conflict");
    if (unlinkat(m_directoryFd, "instance.sock", 0) != 0) return failure("instance-endpoint-conflict");
  } else if (errno != ENOENT) return failure("instance-endpoint-conflict");
  if (!m_server.listen(endpoint())) return failure("instance-listen-failed");
  return {InstanceStart::Kind::Primary, {}};
}
InstanceStart NativeInstance::forward(const ActivationRequest &request) {
  QElapsedTimer deadline; deadline.start(); QLocalSocket socket; socket.setReadBufferSize(maximumActivationReplyBytes + 1);
  socket.connectToServer(endpoint());
  if (!socket.waitForConnected(m_options.timeoutMs)) return failure("instance-unavailable");
  if (!authenticate(socket.socketDescriptor())) { socket.abort(); return failure("activation-peer-denied"); }
  const auto frame = serializeActivation(request) + '\n';
  if (socket.write(frame) != frame.size()) return failure("activation-delivery-unknown");
  socket.flush(); QByteArray response;
  while (deadline.elapsed() < m_options.timeoutMs) {
    response += socket.readAll();
    const auto newline = response.indexOf('\n');
    if (response.size() > maximumActivationReplyBytes + 1) return failure("activation-delivery-unknown");
    if (newline >= 0) {
      if (newline != response.size() - 1) return failure("activation-delivery-unknown");
      const auto parsed = parseActivationReply(response.left(newline), request.id);
      if (!parsed) return failure("activation-delivery-unknown");
      if (parsed->state == ActivationReplyState::Accepted) return {InstanceStart::Kind::Forwarded, {}};
      return {parsed->state == ActivationReplyState::Busy ? InstanceStart::Kind::Busy : InstanceStart::Kind::Failed, parsed->diagnostic};
    }
    if (!socket.waitForReadyRead(std::max(1, m_options.timeoutMs - int(deadline.elapsed())))) break;
  }
  // No resend or election after bytes were sent: the owner may have received
  // them even if its acknowledgement was lost. The caller gets an honest error.
  return failure("activation-delivery-unknown");
}
void NativeInstance::acceptConnections() {
  while (auto *socket = m_server.nextPendingConnection()) {
    if (m_clients.size() >= m_options.maximumClients || !authenticate(socket->socketDescriptor())) { socket->abort(); socket->deleteLater(); continue; }
    m_clients.insert(socket); socket->setReadBufferSize(maximumActivationBytes + 1);
    connect(socket, &QLocalSocket::disconnected, this, [this, socket] { m_clients.remove(socket); socket->deleteLater(); });
    auto *timer = new QTimer(socket); timer->setObjectName(QStringLiteral("activationDeadline")); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, socket] {
      if (!socket->property("handled").toBool()) reject(socket, QStringLiteral("activation-invalid"));
      else socket->abort();
    });
    timer->start(m_options.timeoutMs);
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] { consume(socket); });
    consume(socket);
  }
}
void NativeInstance::consume(QLocalSocket *socket) {
  if (socket->property("handled").toBool()) return;
  if (socket->bytesAvailable() > maximumActivationBytes + 1) { reject(socket, QStringLiteral("activation-invalid")); return; }
  if (!socket->canReadLine()) {
    if (socket->bytesAvailable() >= maximumActivationBytes + 1) reject(socket, QStringLiteral("activation-invalid"));
    return;
  }
  const auto frame = socket->readLine(maximumActivationBytes + 2);
  if (!frame.endsWith('\n') || socket->bytesAvailable()) { reject(socket, QStringLiteral("activation-invalid")); return; }
  const auto parsed = parseActivation(frame.left(frame.size() - 1));
  if (!parsed.request) { reject(socket, parsed.diagnostic); return; }
  const auto &request = *parsed.request;
  const auto fingerprint = QCryptographicHash::hash(serializeActivation(request), QCryptographicHash::Sha256);
  const auto existing = m_receipts.constFind(request.id);
  if (existing != m_receipts.cend()) {
    if (existing->fingerprint != fingerprint) reject(socket, QStringLiteral("activation-invalid"), request.id);
    else sendReply(socket, existing->reply);
    return;
  }
  ActivationReply reply{request.id, ActivationReplyState::Busy, QStringLiteral("activation-busy")};
  try {
    if (m_admission) {
      reply.state = m_admission(request);
      reply.diagnostic = reply.state == ActivationReplyState::Accepted ? QString{} :
                         reply.state == ActivationReplyState::Busy ? QStringLiteral("activation-busy") : QStringLiteral("activation-unavailable");
    }
  } catch (const std::exception &) { reply.state = ActivationReplyState::Rejected; reply.diagnostic = QStringLiteral("activation-handler-failed"); }
  if (reply.state != ActivationReplyState::Busy) {
    while (m_receiptOrder.size() >= 256) m_receipts.remove(m_receiptOrder.dequeue());
    m_receiptOrder.enqueue(request.id); m_receipts.insert(request.id, {fingerprint, reply});
  }
  sendReply(socket, reply);
  if (reply.state == ActivationReplyState::Accepted)
    QTimer::singleShot(0, this, [this, request] { emit activationRequested(request); });
}
void NativeInstance::reject(QLocalSocket *socket, const QString &diagnostic, const QString &id) {
  sendReply(socket, {id, ActivationReplyState::Rejected, diagnostic});
}
void NativeInstance::sendReply(QLocalSocket *socket, const ActivationReply &reply) {
  socket->setProperty("handled", true);
  socket->write(serializeActivationReply(reply) + '\n'); socket->flush(); socket->disconnectFromServer();
}
} // namespace xerahs::app
