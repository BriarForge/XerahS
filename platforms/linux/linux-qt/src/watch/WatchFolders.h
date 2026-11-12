// CORE-024, CORE-025: authorized watch rules and durable readiness checkpoints.
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <functional>

namespace xerahs::watch {
inline constexpr int maximumWatchRules = 64;
inline constexpr int maximumWatchFilters = 128;
enum class Action { Upload, Copy, Move };
struct Rule {
  QString path;
  QStringList filters{ "*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp" };
  bool recursive = false;
  bool enabled = true;
  Action action = Action::Upload;
  QUuid id;
};
QString authorizeRoot(const QString &path, const QString &captures, QString *canonical);
bool matches(const Rule &rule, const QString &path);
QString fingerprint(const QString &path);
class WatchFolders {
public:
  explicit WatchFolders(QString directory);
  const QVector<Rule> &rules() const { return m_rules; }
  QString loadError() const { return m_error; }
  QStringList diagnostics() const { return m_diagnostics; }
  QString saveRules(const QVector<Rule> &rules, const QString &captures);
  bool processed(const QString &key) const { return m_processed.contains(key); }
  QString remember(const QString &key);
private:
  QString m_directory, m_error;
  QByteArray m_rulesSnapshot, m_processedSnapshot;
  QVector<Rule> m_rules;
  QJsonArray m_unreadable;
  QStringList m_diagnostics;
  QSet<QString> m_processed;
};
class StabilityTracker {
public:
  using Clock = std::function<qint64()>;
  explicit StabilityTracker(Clock clock = {});
  bool ready(const QString &path);
  bool ready(const QString &path, const QString &observedFingerprint);
  void retain(const QSet<QString> &paths);
private:
  struct Observation { QString key; qint64 since; };
  Clock m_clock;
  QHash<QString, Observation> m_observed;
};
}
