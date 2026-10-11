#include "image-editor/RecentImages.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
using namespace xerahs::editor;
class RecentImagesTest : public QObject {
  Q_OBJECT
private slots:
  void defaultOptOutNeverRecordsOrBackfills() {
    RecentImages recent; QVERIFY(!recent.enabled()); QCOMPARE(recent.maximum(), 10);
    QVERIFY(!recent.remember(QStringLiteral("/images/private.png"))); QVERIFY(recent.paths().isEmpty());
    QVERIFY(recent.setPreferences(true, 10)); QVERIFY(recent.paths().isEmpty());
    QVERIFY(recent.remember(QStringLiteral("/images/private.png")));
    QVERIFY(recent.setPreferences(false, 10)); QVERIFY(recent.paths().isEmpty());
    QVERIFY(!recent.remember(QStringLiteral("/images/another.png")));
    QVERIFY(recent.setPreferences(true, 10)); QVERIFY(recent.paths().isEmpty());
  }
  void boundedMostRecentFirstWithPosixCaseAndLexicalRules() {
    RecentImages recent; QVERIFY(recent.setPreferences(true, 3));
    for (const QString &path : {QStringLiteral("/images/one.png"), QStringLiteral("/images/two.png"), QStringLiteral("/images/three.png")}) QVERIFY(recent.remember(path));
    QVERIFY(recent.remember(QStringLiteral("/images/./one.png")));
    QCOMPARE(recent.paths(), QStringList({"/images/one.png", "/images/three.png", "/images/two.png"}));
    QVERIFY(recent.remember(QStringLiteral("/images/ONE.png")));
    QCOMPARE(recent.paths(), QStringList({"/images/ONE.png", "/images/one.png", "/images/three.png"}));
    QVERIFY(recent.setPreferences(true, 1)); QCOMPARE(recent.paths(), QStringList({"/images/ONE.png"}));
    // Paths need not exist: opening validates them, removal always stays available.
    QVERIFY(recent.remember(QStringLiteral("/missing/image.png"))); QCOMPARE(recent.paths(), QStringList({"/missing/image.png"}));
    QVERIFY(recent.remove(QStringLiteral("/missing/./image.png"))); QVERIFY(recent.paths().isEmpty());
  }
  void windowsFixtureAndUncRules() {
    RecentImages recent(RecentPathRules::Windows); QVERIFY(recent.setPreferences(true, 10));
    QVERIFY(recent.remember(QStringLiteral("C:\\Images\\One.png")));
    QVERIFY(recent.remember(QStringLiteral("c:\\images\\ONE.png"))); QCOMPARE(recent.paths().size(), 1);
    QCOMPARE(recent.paths().front(), QStringLiteral("c:/images/ONE.png"));
    QVERIFY(recent.remember(QStringLiteral("\\\\SERVER\\Share\\Folder\\..\\two.png")));
    QVERIFY(recent.remember(QStringLiteral("//server/share/two.png"))); QCOMPARE(recent.paths().size(), 2);
    QCOMPARE(recent.paths().front(), QStringLiteral("//server/share/two.png"));
    QVERIFY(recent.remove(QStringLiteral("C:/IMAGES/one.png"))); QCOMPARE(recent.paths().size(), 1);
    QVERIFY(!recent.remember(QStringLiteral("C:relative.png"))); QVERIFY(!recent.remember(QStringLiteral("//server")));
    QVERIFY(recent.remember(QStringLiteral("C:/../../root.png"))); QCOMPARE(recent.paths().front(), QStringLiteral("C:/root.png"));
    QVERIFY(recent.remember(QStringLiteral("//server/share/../../root.png"))); QCOMPARE(recent.paths().front(), QStringLiteral("//server/share/root.png"));
  }
  void posixSymlinkParentSegmentsKeepTheOpenedFileIdentity() {
    QTemporaryDir dir; QVERIFY(QDir().mkpath(dir.filePath("outside/child")));
    QFile outside(dir.filePath("outside/one.png")), root(dir.filePath("one.png"));
    QVERIFY(outside.open(QIODevice::WriteOnly)); outside.write("outside"); outside.close();
    QVERIFY(root.open(QIODevice::WriteOnly)); root.write("root"); root.close();
    QVERIFY(QFile::link(dir.filePath("outside/child"), dir.filePath("linked")));
    const QString chosen = dir.filePath("linked/../one.png");
    RecentImages recent; QVERIFY(recent.setPreferences(true, 10)); QVERIFY(recent.remember(chosen));
    QFile reopened(recent.paths().front()); QVERIFY(reopened.open(QIODevice::ReadOnly)); QCOMPARE(reopened.readAll(), QByteArray("outside"));
    QVERIFY(recent.remember(root.fileName())); QCOMPARE(recent.paths().size(), 2);
    QCOMPARE(recent.paths().at(1), chosen);
  }
  void invalidPreferencesAndPathsAreAtomic() {
    RecentImages recent; QVERIFY(recent.setPreferences(true, 100)); QVERIFY(recent.remember(QStringLiteral("/one.png")));
    const auto before = recent.paths(); const auto revision = recent.revision();
    for (int maximum : {-1, 0, 101, 2147483647}) {
      QVERIFY(!recent.setPreferences(false, maximum)); QVERIFY(recent.enabled()); QCOMPARE(recent.maximum(), 100);
      QCOMPARE(recent.paths(), before); QCOMPARE(recent.revision(), revision);
    }
    for (const QString &path : {QString(), QStringLiteral("relative.png"), QStringLiteral("file:///one.png"), QStringLiteral("/bad") + QChar(0) + QStringLiteral(".png")}) {
      QVERIFY(!recent.remember(path)); QVERIFY(!recent.remove(path)); QCOMPARE(recent.paths(), before); QCOMPARE(recent.revision(), revision);
    }
    QVERIFY(!recent.remove(QStringLiteral("/absent.png"))); QCOMPARE(recent.revision(), revision);
    QVERIFY(recent.remember(QStringLiteral("/one.png"))); QCOMPARE(recent.revision(), revision);
    recent.clear(); QVERIFY(recent.enabled()); QVERIFY(recent.paths().isEmpty()); QVERIFY(recent.revision() > revision);
    const auto cleared = recent.revision(); recent.clear(); QCOMPARE(recent.revision(), cleared);
  }
};
QTEST_GUILESS_MAIN(RecentImagesTest)
#include "RecentImagesTest.moc"
