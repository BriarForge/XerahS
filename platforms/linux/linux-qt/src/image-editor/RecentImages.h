// EC-002 / ESET-012: opt-in private recent paths, independent of the UI/store.
#pragma once
#include <QStringList>
#include <optional>

namespace xerahs::editor {
enum class RecentPathRules { Posix, Windows };
class RecentImages {
public:
  static constexpr int defaultMaximum = 10;
  static constexpr int minimumMaximum = 1;
  static constexpr int maximumMaximum = 100;
  explicit RecentImages(RecentPathRules rules = RecentPathRules::Posix) : m_rules(rules) {}
  bool enabled() const { return m_enabled; }
  int maximum() const { return m_maximum; }
  const QStringList &paths() const { return m_paths; }
  quint64 revision() const { return m_revision; }
  // Invalid maximum is atomic. Opting out clears paths, without backfilling on opt-in.
  bool setPreferences(bool enabled, int maximum);
  bool remember(const QString &absolutePath);
  bool remove(const QString &absolutePath);
  void clear();
private:
  std::optional<QString> normalize(const QString &path) const;
  bool samePath(const QString &first, const QString &second) const;
  RecentPathRules m_rules;
  bool m_enabled = false;
  int m_maximum = defaultMaximum;
  QStringList m_paths;
  quint64 m_revision = 0;
};
}  // namespace xerahs::editor
