// CORE-024, CORE-025: authorized watch rules and durable readiness checkpoints.
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <functional>

namespace xerahs::watch {
enum class Action { Upload, Copy, Move };
struct Rule {
  QString path;
  QStringList filters{ "*.png", "*.jpg", "*.jpeg", "*.gif", "*.webp" };
  bool recursive = false;
  bool enabled = true;
  Action action = Action::Upload;
};
QString authorizeRoot(const QString &path, const QString &captures, QString *canonical);
bool matches(const Rule &rule, const QString &path);
QString fingerprint(const QString &path);
QString readStore(const QString &path, QJsonArray *items);
QString writeStore(const QString &path, const QJsonArray &items);
class WatchFolders {
public:
  explicit WatchFolders(QString directory);
  const QVector<Rule> &rules() const { return m_rules; }
  QString loadError() const { return m_error; }
  QString saveRules(const QVector<Rule> &rules, const QString &captures);
  bool processed(const QString &key) const { return m_processed.contains(key); }
  QString remember(const QString &key);
private:
  QString m_directory, m_error;
  QVector<Rule> m_rules;
  QSet<QString> m_processed;
};
class StabilityTracker {
public:
  using Clock = std::function<qint64()>;
  explicit StabilityTracker(Clock clock = {});
  bool ready(const QString &path);
  void retain(const QSet<QString> &paths);
private:
  struct Observation { QString key; qint64 since; };
  Clock m_clock;
  QHash<QString, Observation> m_observed;
};
}
