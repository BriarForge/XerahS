// EU-017: destroying a runner mid-conversion abandons it silently; it must not
// call back into an owner that is already being destroyed.
#include "VideoConverter.h"

#include <gst/gst.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;

class GstConversionRunnerLifetimeTest : public QObject {
  Q_OBJECT
private slots:
  void destroyingWhileProbingEmitsNothing() {
    QVERIFY(gst_init_check(nullptr, nullptr, nullptr));
    QTemporaryDir dir;
    int finished = 0;
    auto *runner = new GstConversionRunner;
    connect(runner, &xerahs::media::ConversionRunner::finished, this, [&] { ++finished; });
    xerahs::media::Request request;
    request.inputPath = dir.filePath("missing.mkv"); request.outputPath = dir.filePath("out.mkv"); request.codecId = "Vp8";
    runner->start(request);
    QVERIFY(runner->busy());
    delete runner;
    QTest::qWait(200);  // the abandoned probe finishes and must be dropped too
    QCOMPARE(finished, 0);
  }
};

QTEST_GUILESS_MAIN(GstConversionRunnerLifetimeTest)
#include "GstConversionRunnerLifetimeTest.moc"
