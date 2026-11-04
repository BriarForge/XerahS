#include "capture/ScreenTopology.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QSet>
#include <map>
#include <cmath>
namespace xerahs::capture {
QString stableDisplayIdentity(const QString &name, const QString &manufacturer, const QString &model, const QString &serial) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_6_0);
  stream << name << manufacturer << model << serial;
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
Topology snapshotScreenTopology(const std::vector<ScreenDescription> &screens) {
  Topology result; std::map<double, qint64> columns, rows; QSet<QString> ids;
  for (const auto &screen : screens) {
    if (!screen.captureAvailable) continue;
    const auto &g = screen.logical;
    if (!std::isfinite(g.left) || !std::isfinite(g.top) || !std::isfinite(g.right) || !std::isfinite(g.bottom) ||
        !std::isfinite(screen.scale) || screen.scale <= 0 || g.right <= g.left || g.bottom <= g.top) return {};
    const double w = (g.right - g.left) * screen.scale, h = (g.bottom - g.top) * screen.scale;
    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w > kMaximumCaptureDimension || h > kMaximumCaptureDimension) return {};
    const auto width = qint64(std::floor(w + 0.000001)), height = qint64(std::floor(h + 0.000001));
    const auto id = stableDisplayIdentity(screen.name, screen.manufacturer, screen.model, screen.serial);
    if (ids.contains(id)) return {}; // never substitute a list index
    ids.insert(id);
    Display display{id, {0, 0, width, height}, g, screen.scale};
    display.rotation = screen.rotation; display.colorDescription = screen.colorDescription;
    display.captureAvailable = screen.captureAvailable;
    result.push_back(std::move(display));
    columns[g.left] = std::max(columns[g.left], width); rows[g.top] = std::max(rows[g.top], height);
  }
  const auto pack = [](auto &groups) {
    qint64 origin = 0;
    for (auto &group : groups) { const auto extent = group.second; group.second = origin; origin += extent; }
    return origin;
  };
  if (pack(columns) > qint64(kMaximumCaptureDimension) || pack(rows) > qint64(kMaximumCaptureDimension)) return {};
  for (auto &display : result) {
    const auto x = columns.at(display.logical.left), y = rows.at(display.logical.top);
    display.physical = {x, y, x + display.physical.width(), y + display.physical.height()};
  }
  return result;
}
}
