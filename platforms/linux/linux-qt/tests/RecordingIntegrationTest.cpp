#include "recording/Recording.h"
#include <gst/gst.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::recording;
class RecordingIntegrationTest : public QObject {
  Q_OBJECT
private slots:
  void finiteVideo_data() { QTest::addColumn<int>("format"); QTest::newRow("mp4") << int(Format::Mp4); QTest::newRow("webm") << int(Format::WebM); QTest::newRow("gif") << int(Format::Gif); }
  void finiteVideo() {
    QFETCH(int, format); QVERIFY(gst_init_check(nullptr, nullptr, nullptr));
    QSet<QString> available;
    for (const auto &name : {"videoconvert", "videorate", "videoscale", "capsfilter", "filesink", "x264enc", "openh264enc", "vah264enc", "vaapih264enc", "h264parse", "mp4mux", "vp9enc", "webmmux", "gifenc", "videotestsrc"}) {
      auto *factory = gst_element_factory_find(name); if (factory) { available.insert(name); gst_object_unref(factory); }
    }
    const auto encoder = selectEncoder(Format(format), available);
    if (!encoder.diagnostic.isEmpty() || !available.contains("videotestsrc")) QSKIP("Required GStreamer encoder/muxer/test source elements are missing");
    QTemporaryDir folder; Request r; r.format = Format(format); r.folder = folder.path(); r.filename = "test." + extension(r.format);
    Output output; QVERIFY(output.reserve(r).isEmpty());
    const auto description = pipeline(r, encoder, "videotestsrc num-buffers=12 ! video/x-raw,width=160,height=120");
    GError *error = nullptr; auto *p = gst_parse_launch(description.toUtf8().constData(), &error);
    const QString parseError = error ? QString::fromUtf8(error->message) : QString{};
    if (error) g_error_free(error);
    QVERIFY2(p && parseError.isEmpty(), qPrintable(parseError));
    auto *sink = gst_bin_get_by_name(GST_BIN(p), "output"); g_object_set(sink, "location", output.temporary().toUtf8().constData(), nullptr); gst_object_unref(sink);
    auto *bus = gst_element_get_bus(p); const auto started = gst_element_set_state(p, GST_STATE_PLAYING);
    auto *message = gst_bus_timed_pop_filtered(bus, 10 * GST_SECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool eos = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message) gst_message_unref(message); gst_element_set_state(p, GST_STATE_NULL); gst_object_unref(bus); gst_object_unref(p);
    QVERIFY(started != GST_STATE_CHANGE_FAILURE); QVERIFY(eos); QVERIFY(output.publish().isEmpty());
    QFile file(output.destination()); QVERIFY(file.open(QIODevice::ReadOnly)); const auto header = file.read(16);
    if (r.format == Format::Mp4) QVERIFY(header.mid(4).startsWith("ftyp"));
    else if (r.format == Format::WebM) QVERIFY(header.startsWith(QByteArray::fromHex("1a45dfa3")));
    else QVERIFY(header.startsWith("GIF8"));
  }
};
QTEST_GUILESS_MAIN(RecordingIntegrationTest)
#include "RecordingIntegrationTest.moc"
