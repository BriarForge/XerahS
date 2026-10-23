// Optional integration: converts a generated clip with whatever encoder this machine has; skips cleanly otherwise.
#include "VideoConverter.h"

#include <gst/gst.h>

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::media;
using xerahs::app::GstConversionRunner;
using xerahs::app::GstElementAvailability;

class VideoConverterIntegrationTest : public QObject {
  Q_OBJECT
  QTemporaryDir dir;
  QString input;
  struct Pick { QString codec; Container container; } pick;
private slots:
  void initTestCase() {
    QVERIFY(gst_init_check(nullptr, nullptr, nullptr));
    GstElementAvailability port;
    bool found = false;
    for (const auto &c : {QString("X264"), QString("Vp8"), QString("Vp9"), QString("Gif")}) {
      const auto *codec = findCodec(c);
      for (auto container : codec->containers) if (codecAvailability(*codec, container, port).usable && (muxerFor(container).isEmpty() || port.hasElement(muxerFor(container)))) { pick = {c, container}; found = true; break; }
      if (found) break;
    }
    if (!found) QSKIP("No usable encoder/muxer pair is installed");
    for (const auto &mux : {"matroskamux", "avimux", "mp4mux"}) {
      if (!port.hasElement(mux) || !port.hasElement("videotestsrc")) continue;
      input = dir.filePath(QStringLiteral("in.%1").arg(QString(mux) == "avimux" ? "avi" : "mkv"));
      const auto desc = QStringLiteral("videotestsrc num-buffers=15 ! video/x-raw,width=160,height=120,framerate=15/1 ! %1 ! filesink location=\"%2\"").arg(mux, input);
      GError *error = nullptr; auto *p = gst_parse_launch(desc.toUtf8().constData(), &error);
      if (!p || error) { if (error) g_error_free(error); input.clear(); continue; }
      gst_element_set_state(p, GST_STATE_PLAYING);
      auto *bus = gst_element_get_bus(p); auto *m = gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
      const bool ok = m && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
      if (m) gst_message_unref(m);
      gst_object_unref(bus); gst_element_set_state(p, GST_STATE_NULL); gst_object_unref(p);
      if (ok && QFileInfo(input).size() > 0) break;
      input.clear();
    }
    if (input.isEmpty()) QSKIP("Cannot generate a test clip with the installed muxers");
  }
  Request request(const QString &name) {
    Request r; r.inputPath = input; r.codecId = pick.codec; r.container = pick.container; r.outputPath = dir.filePath(name + '.' + extensionFor(pick.container)); return r;
  }
  QStringList stray() { return QDir(dir.path()).entryList(QDir::Files | QDir::Hidden, QDir::Name).filter(".partial"); }
  void convertsAndPublishesAtomically() {
    GstConversionRunner runner; QSignalSpy done(&runner, &ConversionRunner::finished);
    const auto r = request("ok"); runner.start(r);
    QVERIFY(done.wait(30000));
    const auto result = done.first().first().value<ConversionResult>();
    QVERIFY2(result.outcome == Outcome::Success, qPrintable(errorCodeId(result.error) + result.message));
    QVERIFY(QFileInfo(r.outputPath).size() > 0); QVERIFY(stray().isEmpty());
  }
  void cancelRemovesPartial() {
    GstConversionRunner runner; QSignalSpy done(&runner, &ConversionRunner::finished);
    const auto r = request("cancelled"); runner.start(r); runner.cancel();
    QVERIFY(done.count() >= 1);
    QCOMPARE(done.first().first().value<ConversionResult>().outcome, Outcome::Cancelled);
    QTest::qWait(300);
    QVERIFY(!QFileInfo::exists(r.outputPath)); QVERIFY(stray().isEmpty()); QVERIFY(!runner.busy());
  }
  void refusesExistingOutput() {
    GstConversionRunner runner; QSignalSpy done(&runner, &ConversionRunner::finished);
    auto r = request("ok"); runner.start(r);   // exists from the first test
    QVERIFY(done.wait(30000));
    QCOMPARE(done.first().first().value<ConversionResult>().error, ErrorCode::OutputExists);
  }
};

QTEST_MAIN(VideoConverterIntegrationTest)
#include "VideoConverterIntegrationTest.moc"
