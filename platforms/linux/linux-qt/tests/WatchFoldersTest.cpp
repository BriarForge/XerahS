// CORE-024, CORE-025: root consent, readiness and durable duplicate policy.
#include "watch/WatchFolders.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::watch;

namespace {
bool writeFile(const QString &path, const QByteArray &bytes = "pixels") {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

class WatchFoldersTest final : public QObject {
  Q_OBJECT
private slots:
  void checkpointAdoptsTheWorkerSetAndRefusesStaleSnapshots() {
    QTemporaryDir dir; QVERIFY(dir.isValid()); WatchFolders store(dir.path());
    const auto first = store.checkpoint("first");
    auto result = WatchFolders::writeCheckpoint(first);
    QVERIFY(result.error.isEmpty()); QVERIFY(result.processed.contains("first"));
    result.processed.insert("worker-only");
    QCOMPARE(store.adoptCheckpoint(first, std::move(result)), QString());
    QVERIFY(store.processed("first")); QVERIFY(store.processed("worker-only"));
    const auto stale = store.checkpoint("stale");
    QCOMPARE(store.remember("second"), QString());
    CheckpointResult staleResult{{}, {}, {"stale"}};
    QCOMPARE(store.adoptCheckpoint(stale, std::move(staleResult)), QString("watch-store-write-failed"));
    QVERIFY(!store.processed("stale")); QVERIFY(store.processed("first")); QVERIFY(store.processed("second"));
    WatchFolders reloaded(dir.path()); QVERIFY(reloaded.processed("first")); QVERIFY(reloaded.processed("second"));
  }

  void canonicalizesRootsAndRefusesLoops() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input/child"));
    const auto input = dir.filePath("input");
    const auto captures = dir.filePath("captures");
    QString root;
    QCOMPARE(authorizeRoot(input + "/child/..", captures, &root), QString());
    QCOMPARE(root, QFileInfo(input).canonicalFilePath());
    QVERIFY(QFile::link(input, dir.filePath("alias")));
    QCOMPARE(authorizeRoot(dir.filePath("alias"), captures, &root), QString());
    QCOMPARE(root, QFileInfo(input).canonicalFilePath());
    QCOMPARE(authorizeRoot(dir.filePath("missing"), captures, &root), QString("watch-directory-required"));
    QVERIFY(writeFile(dir.filePath("file.png")));
    QCOMPARE(authorizeRoot(dir.filePath("file.png"), captures, &root), QString("watch-directory-required"));
    QVERIFY(QFile::link(dir.filePath("loop-b"), dir.filePath("loop-a")));
    QVERIFY(QFile::link(dir.filePath("loop-a"), dir.filePath("loop-b")));
    QCOMPARE(authorizeRoot(dir.filePath("loop-a"), captures, &root), QString("watch-directory-required"));
    QVERIFY(QDir().mkpath(captures + "/child"));
    QCOMPARE(authorizeRoot(captures, captures, &root), QString("watch-capture-loop"));
    QCOMPARE(authorizeRoot(captures + "/child", captures, &root), QString("watch-capture-loop"));
    QVERIFY(QFile::link(captures, dir.filePath("captures-alias")));
    QCOMPARE(authorizeRoot(dir.filePath("captures-alias"), captures, &root), QString("watch-capture-loop"));
    QCOMPARE(authorizeRoot(captures, dir.filePath("captures-alias"), &root), QString("watch-capture-loop"));
    // A similarly named sibling is not part of the captures tree.
    QVERIFY(QDir().mkpath(captures + "-other"));
    QCOMPARE(authorizeRoot(captures + "-other", captures, &root), QString());
  }

  void refusesSystemRoots() {
    QString canonical;
    for (const auto &path : QStringList{"/", "/tmp", "/etc", "/usr", "/proc", "/sys", "/dev", "/home", "/var", "/usr/share"}) {
      QVERIFY2(!authorizeRoot(path, "/unused-captures", &canonical).isEmpty(), qPrintable(path));
    }
  }

  void filtersAndIgnoresHiddenTemporaryAndSymlinkFiles_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("expected");
    for (const auto &name : QStringList{"image.png", "IMAGE.PNG", "photo.jpg", "scan-01.dat"})
      QTest::newRow(qPrintable(name)) << name << true;
    for (const auto &name : QStringList{"image.txt", "scan.dat", "scan-012.dat", ".image.png", "~image.png", "image.png~",
                                      "image.tmp", "image.TEMP", "image.part", "image.partial", "image.crdownload", "image.swp", ".hidden/image.png"})
      QTest::newRow(qPrintable(name)) << name << false;
  }
  void filtersAndIgnoresHiddenTemporaryAndSymlinkFiles() {
    QFETCH(QString, name);
    QFETCH(bool, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(name);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QVERIFY(writeFile(path));
    Rule rule;
    rule.filters = {"*.png", "*.jpg", "scan-??.dat"};
    QCOMPARE(matches(rule, path), expected);
    // Even a catch-all glob cannot admit hidden or temporary files.
    if (name.startsWith('.') || name.startsWith('~') || name.endsWith('~') ||
        QStringList{"tmp", "TEMP", "part", "partial", "crdownload", "swp"}.contains(QFileInfo(path).suffix())) {
      rule.filters = {"*"};
      QVERIFY(!matches(rule, path));
    }
    QVERIFY(QFile::link(path, dir.filePath("link.png")));
    QVERIFY(!matches(rule, dir.filePath("link.png")));
    QVERIFY(fingerprint(dir.filePath("link.png")).isEmpty());
    QVERIFY(!matches(rule, dir.path()));
  }

  void stabilityUsesInjectedClockAndRestartsAfterChanges() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("image.png");
    QVERIFY(writeFile(path));
    qint64 now = 100;
    StabilityTracker tracker([&] { return now; });
    QVERIFY(!tracker.ready(path));
    now = 2099;
    QVERIFY(!tracker.ready(path));
    now = 2100;
    QVERIFY(tracker.ready(path));
    const auto previous = fingerprint(path);
    QVERIFY(writeFile(path, "more pixels"));
    QVERIFY(fingerprint(path) != previous);
    QVERIFY(!tracker.ready(path));
    now += 1999;
    QVERIFY(!tracker.ready(path));
    ++now;
    QVERIFY(tracker.ready(path));
    tracker.retain({path});
    QVERIFY(tracker.ready(path));
    tracker.retain({});
    QVERIFY(!tracker.ready(path));
    now += 2000;
    QVERIFY(tracker.ready(path));
    QVERIFY(QFile::remove(path));
    QVERIFY(!tracker.ready(path));
    QVERIFY(writeFile(path));
    QVERIFY(!tracker.ready(path));
  }

  void rulesAndProcessedSetSurviveRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input/child"));
    Rule rule;
    rule.path = dir.filePath("input/child/..");
    rule.filters = {"*.dat"};
    rule.recursive = true;
    rule.enabled = false;
    rule.action = Action::Move;
    const auto storePath = dir.filePath("store");
    const auto captures = dir.filePath("captures");
    const auto file = dir.filePath("input/test.dat");
    QVERIFY(writeFile(file));
    const auto key = fingerprint(file);
    QVERIFY(!key.isEmpty());
    {
      WatchFolders store(storePath);
      QVERIFY(store.loadError().isEmpty());
      QVERIFY(!store.processed(key));
      QCOMPARE(store.saveRules({rule}, captures), QString());
      QCOMPARE(store.remember(key), QString());
      QCOMPARE(store.remember(key), QString());
    }
    WatchFolders restarted(storePath);
    QVERIFY(restarted.loadError().isEmpty());
    QVERIFY(restarted.processed(key));
    QCOMPARE(restarted.rules().size(), 1);
    const auto saved = restarted.rules().first();
    QCOMPARE(saved.path, QFileInfo(rule.path).canonicalFilePath());
    QCOMPARE(saved.filters, rule.filters);
    QCOMPARE(saved.recursive, true);
    QCOMPARE(saved.enabled, false);
    QCOMPARE(saved.action, Action::Move);
    QVERIFY(writeFile(file, "changed content"));
    QVERIFY(!restarted.processed(fingerprint(file)));
    const auto permissions = QFileInfo(storePath + "/watch-processed.json").permissions();
    QVERIFY(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)));
  }

  void rejectsInvalidAndOverlappingRulesWithoutReplacingSavedRules() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("input/child"));
    WatchFolders store(dir.filePath("store"));
    Rule rule;
    rule.path = dir.filePath("input");
    const auto captures = dir.filePath("captures");
    QCOMPARE(store.saveRules({rule}, captures), QString());
    const auto path = dir.filePath("store/watch-folders.json");
    const auto before = readFile(path);
    auto child = rule;
    child.path += "/child";
    QCOMPARE(store.saveRules({rule, child}, captures), QString("watch-overlapping-roots"));
    QCOMPARE(store.saveRules({child, rule}, captures), QString("watch-overlapping-roots"));
    auto bad = rule;
    bad.filters.clear();
    QCOMPARE(store.saveRules({bad}, captures), QString("watch-filter-required"));
    bad.filters = {"child/*.png"};
    QCOMPARE(store.saveRules({bad}, captures), QString("watch-filter-invalid"));
    bad.filters = {"   "};
    QCOMPARE(store.saveRules({bad}, captures), QString("watch-filter-invalid"));
    bad = rule;
    bad.action = Action(99);
    QCOMPARE(store.saveRules({bad}, captures), QString("watch-action-invalid"));
    QCOMPARE(readFile(path), before);
    QCOMPARE(store.rules().size(), 1);
  }

  void corruptAndNewerStoresAreRefusedWithoutWrites_data() {
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QByteArray>("bytes");
    for (const auto &fileName : QStringList{"watch-folders.json", "watch-processed.json"}) {
      QTest::newRow(qPrintable(fileName + "-corrupt")) << fileName << QByteArray("{broken");
      const auto items = fileName == "watch-folders.json" ? QJsonArray{QJsonObject{{"path", 3}}} : QJsonArray{42};
      QTest::newRow(qPrintable(fileName + "-invalid-item")) << fileName << QJsonDocument(QJsonObject{
        {"format", "com.xerahs.app/watch-folders"}, {"version", 1}, {"items", items}}).toJson();
    }
    // CORE-025 P6: the rules store keeps version 1, so a version 2 rules
    // store is still refused. The processed store is now version 2, so its
    // newer version is the one that is refused.
    QTest::newRow("watch-folders-newer") << "watch-folders.json"
        << QByteArray(R"({"format":"com.xerahs.app/watch-folders","version":2,"items":[]})");
    QTest::newRow("watch-processed-newer") << "watch-processed.json"
        << QByteArray(R"({"format":"com.xerahs.app/watch-folders","version":3,"items":[]})");
  }
  void corruptAndNewerStoresAreRefusedWithoutWrites() {
    QFETCH(QString, fileName);
    QFETCH(QByteArray, bytes);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath(fileName);
    QVERIFY(writeFile(path, bytes));
    WatchFolders store(dir.path());
    QVERIFY(!store.loadError().isEmpty());
    QCOMPARE(store.saveRules({}, dir.filePath("captures")), store.loadError());
    QCOMPARE(store.remember("new checkpoint"), store.loadError());
    QCOMPARE(readFile(path), bytes);
    const auto other = fileName == "watch-folders.json" ? "watch-processed.json" : "watch-folders.json";
    QVERIFY(!QFileInfo::exists(dir.filePath(other)));
  }
  // CORE-025 P1: the disposition of one publication attempt. The order of
  // the rules is the contract; the first match wins.
  void publicationDispositionFollowsTheContractOrder_data() {
    QTest::addColumn<bool>("restartWithSending");
    QTest::addColumn<bool>("succeeded");
    QTest::addColumn<bool>("requestStarted");
    QTest::addColumn<bool>("everyAttemptAnswered");
    QTest::addColumn<int>("attempts");
    QTest::addColumn<PublicationDisposition>("expected");
    QTest::newRow("success") << false << true << true << true << 1 << PublicationDisposition::Processed;
    QTest::newRow("client-error") << false << false << true << true << 1 << PublicationDisposition::Released;
    QTest::newRow("server-errors-then-success") << false << true << true << true << 3 << PublicationDisposition::Processed;
    QTest::newRow("timeout") << false << false << true << false << 1 << PublicationDisposition::Unconfirmed;
    QTest::newRow("network-error") << false << false << true << false << 1 << PublicationDisposition::Unconfirmed;
    QTest::newRow("stopped-before-start") << false << false << false << true << 0 << PublicationDisposition::Released;
    QTest::newRow("stopped-mid-request") << false << false << true << false << 1 << PublicationDisposition::Unconfirmed;
    QTest::newRow("crash-with-sending") << true << false << true << false << 1 << PublicationDisposition::Unconfirmed;
    QTest::newRow("crash-with-sending-empty-runtime") << true << false << false << false << 0 << PublicationDisposition::Unconfirmed;
    QTest::newRow("every-retry-answered") << false << false << true << true << 3 << PublicationDisposition::Released;
    QTest::newRow("lost-reply-then-server-error") << false << false << true << false << 2 << PublicationDisposition::Unconfirmed;
    QTest::newRow("lost-reply-then-success") << false << true << true << false << 2 << PublicationDisposition::Processed;
    QTest::newRow("answered-retries-but-crash-sending") << true << false << true << true << 2 << PublicationDisposition::Unconfirmed;
  }
  void publicationDispositionFollowsTheContractOrder() {
    QFETCH(bool, restartWithSending);
    QFETCH(bool, succeeded);
    QFETCH(bool, requestStarted);
    QFETCH(bool, everyAttemptAnswered);
    QFETCH(int, attempts);
    QFETCH(PublicationDisposition, expected);
    PublicationFacts facts;
    facts.restartWithSending = restartWithSending;
    facts.succeeded = succeeded;
    facts.requestStarted = requestStarted;
    facts.everyAttemptAnswered = everyAttemptAnswered;
    facts.attempts = attempts;
    QCOMPARE(publicationDisposition(facts), expected);
  }

  // CORE-025 P5: retention keeps an entry whose parent is missing or whose
  // fingerprint still equals the key, and never prunes a sending entry.
  void retentionKeepsChangedAndSendingEntries_data() {
    QTest::addColumn<QStringList>("keys");
    QTest::addColumn<QList<RetentionFacts>>("facts");
    QTest::addColumn<QStringList>("expected");
    const RetentionFacts live{"key-a", true, "key-a", false};
    const RetentionFacts deleted{"key-b", true, "", false};
    const RetentionFacts changed{"key-c", true, "key-c2", false};
    const RetentionFacts missingParent{"key-d", false, "", false};
    const RetentionFacts sending{"key-e", true, "", true};
    QTest::newRow("live") << QStringList{"key-a"} << QList<RetentionFacts>{live} << QStringList{"key-a"};
    QTest::newRow("deleted") << QStringList{"key-b"} << QList<RetentionFacts>{deleted} << QStringList{};
    QTest::newRow("changed") << QStringList{"key-c"} << QList<RetentionFacts>{changed} << QStringList{};
    QTest::newRow("missing-parent") << QStringList{"key-d"} << QList<RetentionFacts>{missingParent} << QStringList{"key-d"};
    QTest::newRow("sending-never-pruned") << QStringList{"key-e"} << QList<RetentionFacts>{sending} << QStringList{"key-e"};
    QTest::newRow("mixed") << QStringList{"key-a", "key-b", "key-c", "key-d", "key-e"}
                           << QList<RetentionFacts>{live, deleted, changed, missingParent, sending}
                           << QStringList{"key-a", "key-d", "key-e"};
  }
  void retentionKeepsChangedAndSendingEntries() {
    QFETCH(QStringList, keys);
    QFETCH(QList<RetentionFacts>, facts);
    QFETCH(QStringList, expected);
    QCOMPARE(retainedKeys(keys, facts), expected);
  }

  // CORE-025 P6: a version 1 store stays readable and is upgraded on the next
  // write; a version 2 store is refused by a reader that only knows version 1.
  void versionOneStoreIsReadAndUpgradedOnTheNextWrite() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("watch-processed.json");
    QVERIFY(writeFile(path, R"({"format":"com.xerahs.app/watch-folders","version":1,"items":["old"]})"));
    WatchFolders store(dir.path());
    QCOMPARE(store.loadError(), QString());
    QVERIFY(store.processed("old"));
    QCOMPARE(store.remember("new"), QString());
    const auto document = QJsonDocument::fromJson(readFile(path)).object();
    QCOMPARE(document.value("version").toInt(), 2);
    QVERIFY(document.value("items").isArray());
    QVERIFY(document.value("publications").isArray());
    const auto items = document.value("items").toArray();
    QVERIFY(items.contains("old")); QVERIFY(items.contains("new"));
  }

  void versionTwoStoreIsRefusedByAVersionOneReader() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("watch-processed.json");
    const auto bytes = R"({"format":"com.xerahs.app/watch-folders","version":2,"items":[],"publications":[]})";
    QVERIFY(writeFile(path, bytes));
    // A reader that only understands version 1 refuses the store and fails
    // closed without republishing anything.
    const auto read = [&] {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) return QString("watch-store-read-failed");
      const auto document = QJsonDocument::fromJson(file.readAll()).object();
      if (document.value("version").toInt() != 1) return QString("watch-store-invalid-version-or-data");
      return QString();
    };
    QCOMPARE(read(), QString("watch-store-invalid-version-or-data"));
    QCOMPARE(readFile(path), bytes);
  }

  // CORE-025 P2: the intent is written before any dispatch and is visible to
  // the next process as a sending publication.
  void intentIsDurableAndSurvivesARestart() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    WatchFolders store(dir.path());
    QCOMPARE(store.loadError(), QString());
    const auto intent = store.publicationIntent("key-a", "2026-11-14T00:00:00Z");
    const auto result = WatchFolders::writeIntent(intent);
    QCOMPARE(result.error, QString());
    QCOMPARE(store.adoptIntent(intent, result), QString());
    QVERIFY(store.publicationPending("key-a"));

    WatchFolders restarted(dir.path());
    QCOMPARE(restarted.loadError(), QString());
    QVERIFY(restarted.publicationPending("key-a"));
    QCOMPARE(restarted.publications().size(), 1);
    QCOMPARE(restarted.publications().first().key, QString("key-a"));
    QCOMPARE(restarted.publications().first().state, PublicationState::Sending);
    QCOMPARE(restarted.publications().first().at, QString("2026-11-14T00:00:00Z"));
  }

  // CORE-025 P2/P5: a settlement removes or marks the publication, and a
  // sending entry is never pruned by retention.
  void settlementMarksUnconfirmedAndRemovesSending() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    WatchFolders store(dir.path());
    const auto intent = store.publicationIntent("key-a");
    auto intentResult = WatchFolders::writeIntent(intent);
    QCOMPARE(intentResult.error, QString());
    QCOMPARE(store.adoptIntent(intent, std::move(intentResult)), QString());
    QVERIFY(store.publicationPending("key-a"));

    const auto settle = store.settlePublication("key-a", PublicationState::Unconfirmed);
    auto settleResult = WatchFolders::writeSettlement(settle);
    QCOMPARE(settleResult.error, QString());
    QCOMPARE(store.adoptSettlement(settle, std::move(settleResult)), QString());
    QVERIFY(store.publicationPending("key-a"));
    QCOMPARE(store.publications().first().state, PublicationState::Unconfirmed);

    const auto release = store.settlePublication("key-a", PublicationState::Sending);
    auto releaseResult = WatchFolders::writeSettlement(release);
    QCOMPARE(releaseResult.error, QString());
    QCOMPARE(store.adoptSettlement(release, std::move(releaseResult)), QString());
    QVERIFY(!store.publicationPending("key-a"));
  }

  // CORE-025 P5: the retention rule compares the recorded key with the
  // current fingerprint of the file, so a deleted or changed file is pruned
  // and an unchanged file is kept.
  void retentionUsesTheActualFingerprint() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath("watch"));
    const auto path = dir.filePath("watch/file.png");
    QVERIFY(writeFile(path));
    const auto key = fingerprint(path);
    QVERIFY(!key.isEmpty());
    QList<RetentionFacts> facts;
    facts.append(RetentionFacts{key, true, fingerprint(path), false});
    QCOMPARE(retainedKeys({key}, facts), QStringList{key});
    // The file changed: the fingerprint no longer equals the key.
    facts[0].fingerprint = fingerprint(path) + "2";
    QCOMPARE(retainedKeys({key}, facts), QStringList{});
    // The parent is gone: the entry is kept, because the root may be unmounted.
    facts[0].parentExists = false;
    facts[0].fingerprint = {};
    QCOMPARE(retainedKeys({key}, facts), QStringList{key});
  }
};

QTEST_GUILESS_MAIN(WatchFoldersTest)
#include "WatchFoldersTest.moc"
