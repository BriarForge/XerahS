#include "WindowDiscovery.h"
#include "capture/WindowSnapTargets.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStringList>
#include <algorithm>
#include <cmath>

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
QStringList socketPaths() {
  const auto signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
  if (signature.isEmpty() || signature.contains('/') || signature == "." || signature == "..") return {};
  QStringList paths;
  const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (!runtime.isEmpty()) paths << runtime + "/hypr/" + signature + "/.socket.sock";
  paths << "/tmp/hypr/" + signature + "/.socket.sock";
  return paths;
}
bool ownProcess(const QJsonValue &pid, qint64 ownPid) {
  return pid.isDouble() && std::isfinite(pid.toDouble()) && pid.toDouble() == double(ownPid);
}
class HyprlandWindowDiscovery final : public WindowDiscovery {
public:
  std::vector<capture::SnapTarget> targets(const capture::Topology &topology, qint64 ownPid) override {
    for (const auto &path : socketPaths()) {
      const auto clients = query(path, "j/clients");
      if (!clients) continue;
      const auto monitors = query(path, "j/monitors");
      if (!monitors) return {};
      return capture::hyprlandSnapTargets(*clients, *monitors, topology, ownPid);
    }
    return {};
  }
  // CORE-014: j/activewindow uses the same client fields and monitor mapping as
  // targets(). A focused window of this process falls back to the top-most
  // other window, which targets() already orders first.
  std::optional<capture::SnapTarget> activeWindow(const capture::Topology &topology, qint64 ownPid) override {
    for (const auto &path : socketPaths()) {
      const auto reply = query(path, "j/activewindow");
      if (!reply) continue;
      const auto document = QJsonDocument::fromJson(*reply);
      if (!document.isObject()) return std::nullopt;
      const auto window = document.object();
      if (window.isEmpty()) return std::nullopt;
      if (ownProcess(window.value(QStringLiteral("pid")), ownPid)) {
        const auto fallback = targets(topology, ownPid);
        if (fallback.empty()) return std::nullopt;
        return fallback.front();
      }
      const auto monitors = query(path, "j/monitors");
      if (!monitors) return std::nullopt;
      QJsonArray clients;
      clients.append(window);
      const auto mapped = capture::hyprlandSnapTargets(QJsonDocument(clients).toJson(QJsonDocument::Compact), *monitors, topology, ownPid);
      if (mapped.size() != 1) return std::nullopt;
      for (const auto &display : topology)
        if (capture::intersection(mapped.front().physical, display.physical)) return mapped.front();
      return std::nullopt;
    }
    return std::nullopt;
  }
};
}
std::unique_ptr<WindowDiscovery> nativeWindowDiscovery() { return std::make_unique<HyprlandWindowDiscovery>(); }
}  // namespace xerahs::app
