// EDITOR-SESSION-001 ES-018: owner-fenced continuation API of the serialized
// recovery queue. The queue's nested wait()/waitIdle() loops remain for the
// post-main-loop drain; interactive callers use whenFinished()/whenIdle().
#include "EditorRecoveryQueue.h"
#include "image-editor/EditorRecovery.h"
#include <QApplication>
#include <QBuffer>
#include <QDateTime>
#include <QImage>
#include <QDir>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QDebug>
#include <QTest>
#include <atomic>
#include <memory>
using namespace xerahs::app;
using xerahs::editor::AnnotationDocument;
using xerahs::editor::EditorRecoveryStore;

namespace {
struct Fixture {
  QTemporaryDir dir;
  std::shared_ptr<EditorRecoveryStore> store;
  Fixture() {
    const auto recovery = dir.filePath(QStringLiteral("recovery"));
    QDir().mkpath(recovery);  // the collection lock file requires the directory
    store = std::make_shared<EditorRecoveryStore>(recovery);
  }
};
// Callback state is heap-shared: a failed assertion must never leave a queue
// callback holding a dead stack slot from an abandoned test function.
struct Result { std::shared_ptr<QString> diagnostic = std::make_shared<QString>(QStringLiteral("unset")); };
// parseDocument rejects empty created/modified timestamps, zero canvas
// dimensions and a missing source PNG, so a recovery payload needs all three
// to survive the store's own corruption check, like a real editor session.
AnnotationDocument sampleDocument() {
  AnnotationDocument document;
  document.createdAt = document.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  document.canvasWidth = document.canvasHeight = 1;
  QByteArray png;
  QBuffer buffer(&png);
  QImage(1, 1, QImage::Format_ARGB32).save(&buffer, "PNG");
  document.sourceImagePng = png;
  return document;
}
}

class EditorRecoveryQueueTest : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { QVERIFY(QCoreApplication::instance() != nullptr); }
  void cleanup() {
    // No job outlives its test function: drain before the next one starts.
    EditorRecoveryQueue::instance().waitIdle();
    EditorRecoveryQueue::setWorkHookForTest({});
  }

  void whenIdleRunsImmediatelyOnAnIdleQueue() {
    bool ran = false;
    EditorRecoveryQueue::instance().whenIdle([&ran] { ran = true; });
    QVERIFY(ran);  // synchronous: no job was ever submitted
  }

  void whenFinishedReportsUnknownOrRetiredJobsImmediately() {
    QString diagnostic = QStringLiteral("unset");
    EditorRecoveryQueue::instance().whenFinished(0, [&diagnostic](QString result) { diagnostic = result; });
    QCOMPARE(diagnostic, QStringLiteral("recovery-job-unavailable"));
  }

  void whenFinishedDeliversTheJobDiagnostic() {
    Fixture fixture;
    EditorRecoveryQueue &queue = EditorRecoveryQueue::instance();
    Result done, continuation;
    const auto job = queue.write(fixture.store, QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"), {},
                                 sampleDocument(), QCoreApplication::applicationPid(),
                                 [done](QString result) { *done.diagnostic = result; });
    queue.whenFinished(job, [continuation](QString result) { *continuation.diagnostic = result; });
    QCOMPARE(*continuation.diagnostic, QStringLiteral("unset"));  // queued: the job is still running
    QTRY_COMPARE_WITH_TIMEOUT(*continuation.diagnostic, QString(), 5000);
    QCOMPARE(*done.diagnostic, QString());
    QCOMPARE(fixture.store->list().records.size(), size_t(1));
  }

  void whenFinishedFiresForSupersededWrites() {
    Fixture fixture;
    EditorRecoveryQueue &queue = EditorRecoveryQueue::instance();
    const QString id = QStringLiteral("11111111-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    const auto first = queue.write(fixture.store, id, {}, sampleDocument(), 0, {});
    const auto second = queue.write(fixture.store, id, {}, sampleDocument(), 0, {});
    QVERIFY(first != second);
    Result superseded;
    queue.whenFinished(first, [superseded](QString result) { *superseded.diagnostic = result; });
    QTRY_COMPARE_WITH_TIMEOUT(*superseded.diagnostic, QStringLiteral("recovery-write-superseded"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.store->list().records.size(), size_t(1), 5000);
    Q_UNUSED(second);
  }

  void continuationsChainNewWorkAndIdleFollowsIt() {
    Fixture fixture;
    EditorRecoveryQueue &queue = EditorRecoveryQueue::instance();
    const QString id = QStringLiteral("22222222-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    const auto job = queue.write(fixture.store, id, {}, sampleDocument(), 0, {});
    const auto order = std::make_shared<int>(0);
    const auto listed = std::make_shared<int>(-1);
    const auto idle = std::make_shared<int>(-1);
    // The continuation submits list work from inside the notification, the
    // pattern offerRecovery uses to chain records without a nested loop.
    queue.whenFinished(job, [&, order, listed](QString) {
      *order = 1;
      queue.list(fixture.store, [order, listed](xerahs::editor::RecoveryListing listing) {
        *order = 2; *listed = int(listing.records.size());
      });
    });
    queue.whenIdle([order, idle] { *idle = *order; });
    QTRY_COMPARE_WITH_TIMEOUT(*order, 2, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(*idle, 2, 5000);
    QCOMPARE(*listed, 1);
    QCOMPARE(fixture.store->list().records.size(), size_t(1));
  }

  void whenIdleWaitsForBlockedWork() {
    Fixture fixture;
    struct Probe { QSemaphore gate; std::atomic_bool entered{false}; };
    const auto probe = std::make_shared<Probe>();
    EditorRecoveryQueue::setWorkHookForTest([probe] { if (!probe->entered.exchange(true)) probe->gate.acquire(); });
    EditorRecoveryQueue &queue = EditorRecoveryQueue::instance();
    const auto job = queue.write(fixture.store, QStringLiteral("33333333-bbbb-4ccc-8ddd-eeeeeeeeeeee"), {},
                                 sampleDocument(), 0, {});
    QTRY_VERIFY_WITH_TIMEOUT(probe->entered.load(), 5000);  // the worker holds the queue busy
    const auto idle = std::make_shared<bool>(false);
    queue.whenIdle([idle] { *idle = true; });
    QVERIFY(!*idle);
    probe->gate.release();
    QTRY_VERIFY_WITH_TIMEOUT(*idle, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(fixture.store->list().records.size(), size_t(1), 5000);
    Q_UNUSED(job);
  }
};
QTEST_MAIN(EditorRecoveryQueueTest)
#include "EditorRecoveryQueueTest.moc"
