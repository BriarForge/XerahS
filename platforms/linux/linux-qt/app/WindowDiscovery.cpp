#include "WindowDiscovery.h"
#include "capture/WindowSnapTargets.h"

#include <QElapsedTimer>
#include <QLocalSocket>
#include <QStringList>
#include <algorithm>

namespace xerahs::app {
namespace {
constexpr int kTimeoutMs = 150;
constexpr qsizetype kMaxReply = 8 * 1024 * 1024;
std::optional<QByteArray> query(const QString &path, const QByteArray &command) {
  QElapsedTimer timer; timer.start();
  const auto remaining = [&] { return std::max(0, kTimeoutMs - int(timer.elapsed())); };
  QLocalSocket socket;
  socket.connectToServer(path);
  if (!socket.waitForConnected(remaining())) return std::nullopt;
  if (socket.write(command) != command.size()) return std::nullopt;
  while (socket.bytesToWrite())
    if (!remaining() || !socket.waitForBytesWritten(remaining())) return std::nullopt;
  QByteArray reply;
  // Hyprland closes the request socket after the entire reply, which can span reads.
  while (true) {
    reply += socket.readAll();
    if (reply.size() > kMaxReply) return std::nullopt;
    if (socket.state() == QLocalSocket::UnconnectedState) break;
    if (!remaining()) return std::nullopt;
    if (!socket.waitForReadyRead(remaining()) && socket.state() != QLocalSocket::UnconnectedState)
      return std::nullopt;
  }
  if (socket.error() != QLocalSocket::PeerClosedError && socket.error() != QLocalSocket::UnknownSocketError)
    return std::nullopt;
  return reply;
}
class HyprlandWindowDiscovery final : public WindowDiscovery {
public:
  std::vector<capture::SnapTarget> targets(const capture::Topology &topology, qint64 ownPid) override {
    const auto signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    if (signature.isEmpty() || signature.contains('/') || signature == "." || signature == "..") return {};
    QStringList paths;
    const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty()) paths << runtime + "/hypr/" + signature + "/.socket.sock";
    paths << "/tmp/hypr/" + signature + "/.socket.sock";
    for (const auto &path : paths) {
      const auto clients = query(path, "j/clients");
      if (!clients) continue;
      const auto monitors = query(path, "j/monitors");
      if (!monitors) return {};
      return capture::hyprlandSnapTargets(*clients, *monitors, topology, ownPid);
    }
    return {};
  }
};
}
std::unique_ptr<WindowDiscovery> nativeWindowDiscovery() { return std::make_unique<HyprlandWindowDiscovery>(); }
}  // namespace xerahs::app
