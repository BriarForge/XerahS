// CORE-024, CORE-025: authorized watch rules and durable readiness checkpoints.
#pragma once
#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <functional>
#include <optional>

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

// CORE-025 P1: the disposition of one watch-folder publication attempt.
// The order of these rules is the contract; the first match wins.
//   1. a leftover `sending` record from an earlier process is unconfirmed;
//   2. a publication that succeeded is processed;
//   3. a started publication whose every attempt was answered is released;
//   4. a started publication with any unanswered attempt is unconfirmed;
//   5. a publication that never started is released.
enum class PublicationDisposition { Processed, Released, Unconfirmed };
QString publicationDispositionName(PublicationDisposition disposition);
struct PublicationFacts {
  bool requestStarted = false;         // a request was actually issued
  int attempts = 0;                   // request attempts made
  bool everyAttemptAnswered = false;  // production can prove every attempt got an HTTP response
  bool succeeded = false;
  bool restartWithSending = false;    // a sending record survived from an earlier process
};
PublicationDisposition publicationDisposition(const PublicationFacts &facts);

// CORE-025 P5: the checkpoint entries that survive a checkpoint write.
// `keys` is the full key set before the write; `facts` gives the current file
// state for each key. A key is kept when the parent directory is missing (the
// root may be unmounted or removed) or when the current fingerprint still
// equals the recorded key. A key recorded as sending is never pruned.
struct RetentionFacts {
  QString key;
  bool parentExists = false;
  QString fingerprint;  // the complete recorded key, compared for equality
  bool sending = false;
};
QStringList retainedKeys(const QStringList &keys, const QList<RetentionFacts> &facts);

struct CheckpointWrite { QString path; QSet<QString> processed; QString key; QByteArray expected; };
struct CheckpointResult { QString error; QByteArray snapshot; QSet<QString> processed; };

// CORE-025 P6: the watch checkpoint store. Version 2 adds `publications`;
// version 1 stays readable and is upgraded on the next write.
enum class PublicationState { Sending, Unconfirmed };
QString publicationStateName(PublicationState state);
struct PublicationRecord { QString key; PublicationState state = PublicationState::Sending; QString at; };
struct CheckpointIntent { QString path; QSet<QString> processed; QList<PublicationRecord> publications; QString key; QByteArray expected; };
struct IntentResult { QString error; QByteArray snapshot; QSet<QString> processed; QList<PublicationRecord> publications; };
struct SettleRequest { QString path; QSet<QString> processed; QList<PublicationRecord> publications; QString key; PublicationState state; QByteArray expected; };
struct SettleResult { QString error; QByteArray snapshot; QSet<QString> processed; QList<PublicationRecord> publications; };

class WatchFolders {
public:
  explicit WatchFolders(QString directory);
  const QVector<Rule> &rules() const { return m_rules; }
  QString loadError() const { return m_error; }
  QStringList diagnostics() const { return m_diagnostics; }
  QString saveRules(const QVector<Rule> &rules, const QString &captures);
  bool processed(const QString &key) const { return m_processed.contains(key); }
  // Publications still recorded as sending or unconfirmed, newest last.
  const QList<PublicationRecord> &publications() const { return m_publications; }
  bool publicationPending(const QString &key) const;
  QString remember(const QString &key);
  CheckpointWrite checkpoint(const QString &key) const;
  static CheckpointResult writeCheckpoint(const CheckpointWrite &write);
  QString adoptCheckpoint(const CheckpointWrite &write, CheckpointResult result);
  // CORE-025 P2: the durable send intent, written before any dispatch.
  CheckpointIntent publicationIntent(const QString &key, const QString &at = {}) const;
  static IntentResult writeIntent(const CheckpointIntent &intent);
  QString adoptIntent(const CheckpointIntent &intent, IntentResult result);
  // CORE-025 P1/P2: settle a publication as processed, released or unconfirmed.
  SettleRequest settlePublication(const QString &key, PublicationState state) const;
  static SettleResult writeSettlement(const SettleRequest &request);
  QString adoptSettlement(const SettleRequest &request, SettleResult result);
  // CORE-025 P5: prune processed and unconfirmed entries whose file changed.
  static QStringList retained(const QStringList &keys, const QList<RetentionFacts> &facts);
private:
  QString m_directory, m_error;
  QByteArray m_rulesSnapshot, m_processedSnapshot;
  QVector<Rule> m_rules;
  QJsonArray m_unreadable;
  QStringList m_diagnostics;
  QSet<QString> m_processed;
  QList<PublicationRecord> m_publications;
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
