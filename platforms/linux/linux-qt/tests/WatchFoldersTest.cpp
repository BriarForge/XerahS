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
      QTest::newRow(qPrintable(fileName + "-newer")) << fileName << QByteArray(R"({"format":"com.xerahs.app/watch-folders","version":2,"items":[]})");
      const auto items = fileName == "watch-folders.json" ? QJsonArray{QJsonObject{{"path", 3}}} : QJsonArray{42};
      QTest::newRow(qPrintable(fileName + "-invalid-item")) << fileName << QJsonDocument(QJsonObject{
        {"format", "com.xerahs.app/watch-folders"}, {"version", 1}, {"items", items}}).toJson();
    }
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
};

QTEST_GUILESS_MAIN(WatchFoldersTest)
#include "WatchFoldersTest.moc"
