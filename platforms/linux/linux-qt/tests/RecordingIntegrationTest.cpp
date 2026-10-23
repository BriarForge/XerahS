#include "recording/Recording.h"
#include <gst/gst.h>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::recording;
namespace {
struct Tracks { GstElement *pipeline = nullptr; int video = 0, audio = 0, width = 0, height = 0; };
void inspectTrack(GstElement *, GstPad *pad, gpointer data) {
  auto &tracks = *static_cast<Tracks *>(data);
  auto *caps = gst_pad_get_current_caps(pad);
  if (caps) {
    const auto *structure = gst_caps_get_structure(caps, 0);
    const QString type = QString::fromUtf8(gst_structure_get_name(structure));
    if (type.startsWith("video/")) {
      ++tracks.video; gst_structure_get_int(structure, "width", &tracks.width); gst_structure_get_int(structure, "height", &tracks.height);
    } else if (type.startsWith("audio/")) ++tracks.audio;
    gst_caps_unref(caps);
  }
  auto *sink = gst_element_factory_make("fakesink", nullptr);
  g_object_set(sink, "sync", false, "async", false, nullptr);
  gst_bin_add(GST_BIN(tracks.pipeline), sink); gst_element_sync_state_with_parent(sink);
  auto *input = gst_element_get_static_pad(sink, "sink"); gst_pad_link(pad, input); gst_object_unref(input);
}
}

class RecordingIntegrationTest : public QObject {
  Q_OBJECT
private slots:
  void finiteVideo_data() {
    QTest::addColumn<int>("format"); QTest::addColumn<bool>("crop"); QTest::addColumn<int>("audio");
    QTest::newRow("mp4") << int(Format::Mp4) << false << 0;
    QTest::newRow("webm") << int(Format::WebM) << false << 0;
    QTest::newRow("gif") << int(Format::Gif) << false << 0;
    QTest::newRow("cropped-mp4-audio-mix") << int(Format::Mp4) << true << 2;
    QTest::newRow("cropped-webm-audio-mix") << int(Format::WebM) << true << 2;
    QTest::newRow("cropped-webm-video-only") << int(Format::WebM) << true << 0;
  }
  void finiteVideo() {
    QFETCH(bool, crop); QFETCH(int, audio);
    QFETCH(int, format); QVERIFY(gst_init_check(nullptr, nullptr, nullptr));
    QSet<QString> available;
    for (const auto &name : {"videoconvert", "videorate", "videoscale", "capsfilter", "filesink", "x264enc", "openh264enc", "vah264enc", "vaapih264enc", "h264parse", "mp4mux", "vp9enc", "webmmux", "gifenc", "videotestsrc", "videocrop", "voaacenc", "avenc_aac", "opusenc", "audiotestsrc", "audiomixer", "audioconvert", "audioresample", "queue"}) {
      auto *factory = gst_element_factory_find(name); if (factory) { available.insert(name); gst_object_unref(factory); }
    }
    const auto encoder = selectEncoder(Format(format), available);
    if (!encoder.diagnostic.isEmpty() || !available.contains("videotestsrc")) QSKIP("Required GStreamer encoder/muxer/test source elements are missing");
    QTemporaryDir folder; Request r; r.format = Format(format); r.folder = folder.path(); r.filename = "test." + extension(r.format);
    QString audioEncoder = r.format == Format::WebM ? "opusenc" : available.contains("voaacenc") ? "voaacenc" : "avenc_aac";
    if (audio) for (const auto &element : QStringList{audioEncoder, "audiotestsrc", "audiomixer", "audioconvert", "audioresample", "queue"})
      if (!available.contains(element)) QSKIP("Required optional audio integration elements are missing");
    if (crop) {
      if (!available.contains("videocrop")) QSKIP("Required optional crop element is missing");
      r.crop = mapCrop({10, 10, 101, 51}, {0, 0, 160, 120}, {160, 120}).rectangle; r.streamSize = {160, 120};
    }
    Output output; QVERIFY(output.reserve(r).isEmpty());
    auto description = pipeline(r, encoder, "videotestsrc num-buffers=12 ! video/x-raw,width=160,height=120", audio, audioEncoder);
    for (int i = 0; i < audio; ++i) description += QStringLiteral(" audiotestsrc num-buffers=20 ! audioqueue%1.").arg(i);
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
    if (r.format != Format::Gif) {
      Tracks tracks; tracks.pipeline = gst_pipeline_new(nullptr);
      auto *source = gst_element_factory_make("filesrc", nullptr);
      auto *demux = gst_element_factory_make(r.format == Format::Mp4 ? "qtdemux" : "matroskademux", nullptr);
      QVERIFY(source && demux);
      g_object_set(source, "location", output.destination().toUtf8().constData(), nullptr);
      gst_bin_add_many(GST_BIN(tracks.pipeline), source, demux, nullptr); QVERIFY(gst_element_link(source, demux));
      g_signal_connect(demux, "pad-added", G_CALLBACK(inspectTrack), &tracks);
      auto *inspectionBus = gst_element_get_bus(tracks.pipeline);
      gst_element_set_state(tracks.pipeline, GST_STATE_PLAYING);
      auto *done = gst_bus_timed_pop_filtered(inspectionBus, 10 * GST_SECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
      const bool complete = done && GST_MESSAGE_TYPE(done) == GST_MESSAGE_EOS;
      if (done) gst_message_unref(done);
      gst_element_set_state(tracks.pipeline, GST_STATE_NULL); gst_object_unref(inspectionBus); gst_object_unref(tracks.pipeline);
      QVERIFY(complete); QCOMPARE(tracks.video, 1); QCOMPARE(tracks.audio, audio ? 1 : 0);
      QCOMPARE(tracks.width, crop ? 100 : 160); QCOMPARE(tracks.height, crop ? 50 : 120);
    }
  }
};
QTEST_GUILESS_MAIN(RecordingIntegrationTest)
#include "RecordingIntegrationTest.moc"
