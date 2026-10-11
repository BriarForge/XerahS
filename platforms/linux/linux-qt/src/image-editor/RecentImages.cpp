#include "RecentImages.h"

namespace xerahs::editor {
std::optional<QString> RecentImages::normalize(const QString &path) const {
  if (path.isEmpty() || path.contains(QChar(0))) return std::nullopt;
  if (m_rules == RecentPathRules::Posix) {
    if (!path.startsWith(QLatin1Char('/'))) return std::nullopt;
    QStringList parts;
    for (const auto &part : path.split(QLatin1Char('/'), Qt::SkipEmptyParts)) if (part != u".") parts.append(part);
    // POSIX resolves '..' after following symlinks. Lexically removing it can
    // change which file a recorded path opens; leave that resolution to the OS.
    return QStringLiteral("/") + parts.join(QLatin1Char('/'));
  }
  QString native = path; native.replace(QLatin1Char('\\'), QLatin1Char('/'));
  const bool unc = native.startsWith(u"//");
  const ushort letter = native.isEmpty() ? 0 : native.at(0).unicode();
  const bool drive = native.size() >= 3 && ((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z')) &&
      native.at(1) == QLatin1Char(':') && native.at(2) == QLatin1Char('/');
  if (!unc && !drive) return std::nullopt;
  QString root; QStringList parts;
  if (unc) {
    parts = native.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.at(0) == u"." || parts.at(0) == u".." || parts.at(1) == u"." || parts.at(1) == u"..") return std::nullopt;
    root = QStringLiteral("//") + parts.takeFirst(); root += QLatin1Char('/') + parts.takeFirst();
  } else { root = native.left(3); parts = native.mid(3).split(QLatin1Char('/'), Qt::SkipEmptyParts); }
  QStringList normalized;
  for (const auto &part : parts) {
    if (part == u".") continue;
    if (part == u"..") { if (!normalized.isEmpty()) normalized.removeLast(); }
    else normalized.append(part);
  }
  if (normalized.isEmpty()) return root;
  return root + (root.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + normalized.join(QLatin1Char('/'));
}
bool RecentImages::samePath(const QString &first, const QString &second) const {
  return first.compare(second, m_rules == RecentPathRules::Windows ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0;
}
bool RecentImages::setPreferences(bool enabled, int maximum) {
  if (maximum < minimumMaximum || maximum > maximumMaximum) return false;
  if (m_enabled == enabled && m_maximum == maximum) return true;
  m_enabled = enabled; m_maximum = maximum;
  if (!enabled) m_paths.clear();
  while (m_paths.size() > maximum) m_paths.removeLast();
  ++m_revision; return true;
}
bool RecentImages::remember(const QString &absolutePath) {
  if (!m_enabled) return false;
  const auto normalized = normalize(absolutePath); if (!normalized) return false;
  if (!m_paths.isEmpty() && samePath(m_paths.front(), *normalized) && m_paths.front() == *normalized) return true;
  for (int i = m_paths.size() - 1; i >= 0; --i) if (samePath(m_paths.at(i), *normalized)) m_paths.removeAt(i);
  m_paths.prepend(*normalized);
  while (m_paths.size() > m_maximum) m_paths.removeLast();
  ++m_revision; return true;
}
bool RecentImages::remove(const QString &absolutePath) {
  const auto normalized = normalize(absolutePath); if (!normalized) return false;
  for (int i = 0; i < m_paths.size(); ++i) if (samePath(m_paths.at(i), *normalized)) {
    m_paths.removeAt(i); ++m_revision; return true;
  }
  return false;
}
void RecentImages::clear() { if (!m_paths.isEmpty()) { m_paths.clear(); ++m_revision; } }
}  // namespace xerahs::editor
