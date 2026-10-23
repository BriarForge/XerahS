#include "media/VideoConversion.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::media;

class VideoConversionTest : public QObject {
  Q_OBJECT
  static SetAvailability everything() {
    return SetAvailability({"x264enc", "x265enc", "nvh264enc", "vp8enc", "vp9enc", "svtav1enc", "avenc_mpeg4", "gifenc", "mp4mux", "matroskamux",
                            "webmmux", "avimux", "h264parse", "h265parse", "avenc_aac", "opusenc", "lamemp3enc"});
  }
  static QString touch(const QTemporaryDir &dir, const QString &name) {
    const auto path = dir.filePath(name); QFile f(path); (void)f.open(QIODevice::WriteOnly); f.write("x"); return path;
  }
private slots:
  void catalogueMatchesContract() {
    // EU-012: exact list and order from utility-catalog.json
    const QStringList expected{"X264", "X265", "H264Nvenc", "HevcNvenc", "H264Amf", "HevcAmf", "H264Qsv", "HevcQsv", "Vp8", "Vp9", "Av1", "Xvid", "Gif", "Webp", "Apng"};
    QStringList ids; for (const auto &c : codecs()) ids << c.id;
    QCOMPARE(ids, expected);
    QCOMPARE(ids.size(), 15);
    for (const auto &c : codecs()) QCOMPARE(c.hardware, c.id.contains("Nvenc") || c.id.contains("Amf") || c.id.contains("Qsv"));
    const QString path = QStringLiteral(XERAHS_REPO_ROOT_DIR "/product-contract/capabilities/EDITOR-UTILITIES-001/utility-catalog.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) QSKIP("contract catalog not found from the build tree");
    QStringList fromFile;
    for (const auto &u : QJsonDocument::fromJson(f.readAll()).object()["utilities"].toArray())
      if (u.toObject()["id"] == "video-converter") for (const auto &c : u.toObject()["codecs"].toArray()) fromFile << c.toString();
    QCOMPARE(fromFile, expected);
  }
  void everyInvalidPairRejected() {
    QTemporaryDir dir; const auto in = touch(dir, "a.mkv"); auto port = everything();
    int rejected = 0, accepted = 0;
    for (const auto &codec : codecs()) for (auto c : containers()) {
      Request r; r.inputPath = in; r.codecId = codec.id; r.container = c; r.outputPath = dir.filePath("out." + extensionFor(c));
      const auto plan = planConversion(r, {}, port);
      if (!codec.containers.contains(c)) { QCOMPARE(plan.error, ErrorCode::IncompatibleContainer); QCOMPARE(errorCodeId(plan.error), QString("VC_INCOMPATIBLE_CONTAINER")); ++rejected; }
      else { QVERIFY2(plan.error != ErrorCode::IncompatibleContainer, qPrintable(codec.id)); ++accepted; }
    }
    QCOMPARE(rejected + accepted, 15 * 7); QVERIFY(rejected > 50);
  }
  void inputAndRequestErrors() {
    QTemporaryDir dir; const auto in = touch(dir, "a.mp4"); auto port = everything();
    Request r; r.inputPath = in; r.codecId = "X264"; r.container = Container::Mp4; r.outputPath = dir.filePath("o.mp4");
    QVERIFY(planConversion(r, {}, port).ok());
    auto bad = r; bad.codecId = "Nope"; QCOMPARE(planConversion(bad, {}, port).error, ErrorCode::UnknownCodec);
    bad = r; bad.inputPath = ""; QCOMPARE(planConversion(bad, {}, port).error, ErrorCode::EmptyInput);
    bad = r; bad.inputPath = dir.filePath("missing.mp4"); QCOMPARE(planConversion(bad, {}, port).error, ErrorCode::InputMissing);
    bad = r; bad.bitrateKbps = -1; QCOMPARE(planConversion(bad, {}, port).error, ErrorCode::InvalidBitrate);
    InputInfo audioOnly; audioOnly.known = true; audioOnly.hasVideo = false;
    QCOMPARE(planConversion(r, audioOnly, port).error, ErrorCode::NoVideoStream);
  }
  void outputRules() {
    QTemporaryDir dir; const auto in = touch(dir, "a.mp4"); auto port = everything();
    Request r; r.inputPath = in; r.codecId = "X264"; r.container = Container::Mp4; r.outputPath = in;
    QCOMPARE(planConversion(r, {}, port).error, ErrorCode::OutputIsInput);
    r.outputPath = dir.path() + "/./a.mp4"; QCOMPARE(planConversion(r, {}, port).error, ErrorCode::OutputIsInput);
    r.overwrite = true; QCOMPARE(planConversion(r, {}, port).error, ErrorCode::OutputIsInput);   // overwrite never allows the input
    r.overwrite = false; r.outputPath = touch(dir, "b.mp4"); QCOMPARE(planConversion(r, {}, port).error, ErrorCode::OutputExists);
    r.overwrite = true; QVERIFY(planConversion(r, {}, port).ok());
    r.overwrite = false; r.outputPath = dir.filePath("nodir/c.mp4"); QCOMPARE(planConversion(r, {}, port).error, ErrorCode::OutputFolderMissing);
    r.outputPath = ""; QCOMPARE(planConversion(r, {}, port).error, ErrorCode::EmptyOutput);
    QCOMPARE(suggestOutputPath(in, Container::Mkv), dir.filePath("a-converted.mkv"));
    touch(dir, "a-converted.mkv"); QCOMPARE(suggestOutputPath(in, Container::Mkv), dir.filePath("a-converted (2).mkv"));
    const auto partial = partialPathFor(dir.filePath("o.mp4"), "ab12");
    QVERIFY(partial != dir.filePath("o.mp4")); QCOMPARE(QFileInfo(partial).absolutePath(), dir.path()); QVERIFY(QFileInfo(partial).fileName().startsWith('.'));
  }
  void pipelineDescriptions() {
    QTemporaryDir dir; const auto in = touch(dir, "weird \"name\"; rm -rf.mp4"); auto port = everything();
    Request r; r.inputPath = in; r.codecId = "X264"; r.container = Container::Mp4; r.outputPath = dir.filePath("o.mp4"); r.bitrateKbps = 2500;
    InputInfo info; info.known = true; info.hasAudio = true;
    auto plan = planConversion(r, info, port);
    QVERIFY(plan.ok());
    QCOMPARE(plan.description, QString("filesrc name=src ! decodebin name=dec mp4mux name=mux ! filesink name=sink "
        "dec. ! video/x-raw ! queue ! videoconvert ! x264enc bitrate=2500 ! h264parse ! queue ! mux. "
        "dec. ! audio/x-raw ! queue ! audioconvert ! audioresample ! avenc_aac ! queue ! mux."));
    QVERIFY(!plan.description.contains(dir.path()));   // EU-013/015: no paths in the description
    // webm/vp9 uses target-bitrate in bits per second and opus audio
    r.codecId = "Vp9"; r.container = Container::WebM; r.outputPath = dir.filePath("o.webm");
    plan = planConversion(r, info, port); QVERIFY(plan.ok());
    QVERIFY(plan.description.contains("vp9enc target-bitrate=2500000")); QVERIFY(plan.description.contains("opusenc"));
    // GIF: no audio, no muxer, diagnostic reported
    r.codecId = "Gif"; r.container = Container::Gif; r.outputPath = dir.filePath("o.gif");
    plan = planConversion(r, info, port); QVERIFY(plan.ok());
    QVERIFY(plan.description.contains("gifenc ! sink.")); QVERIFY(plan.description.contains("fakesink"));
    QVERIFY(!plan.description.contains("bitrate")); QCOMPARE(plan.diagnostics.size(), 1); QVERIFY(plan.diagnostics[0].startsWith("AUDIO_DROPPED_CONTAINER"));
    // input without audio: no audio branch at all
    r.codecId = "X264"; r.container = Container::Mkv; r.outputPath = dir.filePath("o.mkv"); info.hasAudio = false;
    plan = planConversion(r, info, port); QVERIFY(plan.ok()); QVERIFY(!plan.description.contains("audio")); QVERIFY(plan.diagnostics.isEmpty());
    // audio opt-out and missing audio encoder
    info.hasAudio = true; r.includeAudio = false; plan = planConversion(r, info, port); QVERIFY(!plan.description.contains("opusenc")); QVERIFY(plan.diagnostics.isEmpty());
    r.includeAudio = true; SetAvailability noAudio({"x264enc", "matroskamux"});
    plan = planConversion(r, info, noAudio); QVERIFY(plan.ok()); QVERIFY(plan.diagnostics[0].startsWith("AUDIO_DROPPED_NO_ENCODER"));
    // encoder fallback is deterministic: first usable candidate
    SetAvailability onlyOpenH264({"openh264enc", "matroskamux"});
    QCOMPARE(planConversion(r, info, onlyOpenH264).encoder, QString("openh264enc"));
  }
  void unavailableAndHardware() {
    QTemporaryDir dir; const auto in = touch(dir, "a.mp4");
    SetAvailability software({"x264enc", "mp4mux"});
    Request r; r.inputPath = in; r.container = Container::Mp4; r.outputPath = dir.filePath("o.mp4");
    r.codecId = "H264Nvenc"; auto plan = planConversion(r, {}, software);
    QCOMPARE(plan.error, ErrorCode::CodecUnavailable); QVERIFY(plan.message.contains("hardware")); QVERIFY(plan.encoder.isEmpty());   // never substituted
    r.codecId = "X264"; QVERIFY(planConversion(r, {}, software).ok());
    SetAvailability nvenc({"nvh264enc", "mp4mux"});
    r.codecId = "H264Nvenc"; plan = planConversion(r, {}, nvenc); QVERIFY(plan.ok()); QCOMPARE(plan.encoder, QString("nvh264enc"));
    SetAvailability noMux({"x264enc"});
    r.codecId = "X264"; QCOMPARE(planConversion(r, {}, noMux).error, ErrorCode::MuxerUnavailable);
    const auto avail = codecAvailability(*findCodec("Webp"), Container::WebP, software);
    QVERIFY(!avail.usable); QVERIFY(!avail.reason.isEmpty());
    QVERIFY(!codecAvailability(*findCodec("X264"), Container::WebM, nvenc).usable);
  }
};

QTEST_APPLESS_MAIN(VideoConversionTest)
#include "VideoConversionTest.moc"
