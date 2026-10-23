#include "recording/Recording.h"
#include "shell/ActivationProtocol.h"
#include "shell/ActivationDispatcher.h"
#include "settings/HotkeyBindings.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::recording;
namespace {
Request request(const QString &folder = "/tmp") { Request r; r.folder = folder; r.filename = "recording.mp4"; return r; }
QSet<QString> elements() { return {"videoconvert", "videorate", "videoscale", "capsfilter", "filesink", "x264enc", "openh264enc", "vah264enc", "vaapih264enc", "h264parse", "mp4mux", "vp9enc", "webmmux", "gifenc"}; }
// An encoder port only receives the reserved path, never the public destination.
struct FakeEncoderPort {
  QString path;
  bool begin(const QString &temporary) { path = temporary; QFile f(path); return f.open(QIODevice::WriteOnly) && f.write("partial") > 0; }
  bool finalize() { QFile f(path); return f.open(QIODevice::Append) && f.write("-finalized") > 0; }
};
}
class RecordingTest : public QObject {
  Q_OBJECT
private slots:
  void cropMapping() {
    QCOMPARE(mapCrop({110, 220, 101, 51}, {100, 200, 400, 300}, {800, 600}).rectangle, QRect(20, 40, 202, 102));
    QCOMPARE(mapCrop({-1800, 10, 101, 51}, {-1920, 0, 1920, 1080}, {1920, 1080}).rectangle, QRect(120, 10, 100, 50));
    QCOMPARE(mapCrop({0, 0, 1920, 1080}, {0, 0, 1920, 1080}, {1280, 720}).rectangle, QRect(0, 0, 1280, 720));
    QCOMPARE(mapCrop({0, 0, 101, 51}, {0, 0, 1920, 1080}, {1920, 1080}).rectangle, QRect(0, 0, 100, 50));
    for (const QRectF region : {QRectF(-1, 0, 10, 10), QRectF(0, 0, 401, 10), QRectF(0, 299, 10, 2)})
      QCOMPARE(mapCrop(region, {0, 0, 400, 300}, {800, 600}).diagnostic, QString("region-outside-stream"));
    QCOMPARE(mapCrop({0, 0, 1, 1}, {0, 0, 400, 300}, {}).diagnostic, QString("region-mapping-unavailable"));
    auto r = request(); r.crop = QRect(20, 40, 202, 102); r.streamSize = {800, 600};
    const auto description = pipeline(r, selectEncoder(r.format, elements()), "videotestsrc");
    QVERIFY(description.contains("videocrop name=regioncrop left=20 top=40 right=578 bottom=458"));
    r.crop = QRect(799, 0, 2, 2); QVERIFY(pipeline(r, selectEncoder(r.format, elements()), "videotestsrc").isEmpty());
  }
  void explicitAudioSelection() {
    const auto system = audioDevice({{"device.name", "alsa_output.card.monitor"}}, "Speakers");
    const auto mic = audioDevice({{"node.name", "alsa_input.card"}}, "Microphone");
    const auto pulse = audioDevice({{"internal-name", "alsa_output.card.monitor"}, {"device.name", "ambiguous label"}}, "Speakers");
    QCOMPARE(pulse.id, QString("internal-name:alsa_output.card.monitor")); QVERIFY(pulse.monitor);
    QVERIFY(system.monitor); QVERIFY(!mic.monitor); QVERIFY(audioDevice({}, "Unnamed").id.isEmpty());
    QCOMPARE(system.id, QString("device.name:alsa_output.card.monitor"));
    int calls = 0; QVector<AudioDevice> devices{system, mic};
    DeviceMonitor monitor = [&] { ++calls; return devices; };
    auto r = request(); QVERIFY(selectAudio(r, monitor).devices.isEmpty()); QCOMPARE(calls, 0);
    r.systemAudioDevice = system.id; r.microphoneDevice = mic.id;
    auto selected = selectAudio(r, monitor); QCOMPARE(selected.devices.size(), 2); QVERIFY(selected.diagnostics.isEmpty());
    devices.removeLast(); selected = selectAudio(r, monitor); QVERIFY(selected.devices.isEmpty()); QCOMPARE(selected.diagnostics.size(), 1);
    QVERIFY(!pipeline(r, selectEncoder(r.format, elements()), "videotestsrc", selected.devices.size(), "voaacenc").contains("audiomixer"));
    QVERIFY(selected.diagnostics.first().contains("microphone")); QVERIFY(selected.diagnostics.first().contains(mic.id));
    r.microphoneDevice.clear(); QCOMPARE(selectAudio(r, monitor).devices.size(), 1);
    r.microphoneDevice = r.systemAudioDevice; QCOMPARE(selectAudio(r, monitor).devices.size(), 1);
    r.format = Format::Gif; calls = 0; QVERIFY(selectAudio(r, monitor).devices.isEmpty()); QCOMPARE(calls, 0);
    for (auto format : {Format::Mp4, Format::WebM, Format::Gif}) {
      r.format = format; r.filename = "audio." + extension(format);
      const auto description = pipeline(r, selectEncoder(format, elements()), "videotestsrc", 2, format == Format::WebM ? "opusenc" : "voaacenc");
      QCOMPARE(description.contains("audiomixer"), format != Format::Gif);
      QCOMPARE(description.contains("audioqueue1"), format != Format::Gif);
    }
  }
  void transitionMatrix() {
    const QList<QList<Event>> paths{{}, {Event::Request}, {Event::Request, Event::Granted},
      {Event::Request, Event::Granted, Event::Started}, {Event::Request, Event::Granted, Event::Started, Event::Pause},
      {Event::Request, Event::Granted, Event::Started, Event::Stop},
      {Event::Request, Event::Granted, Event::Started, Event::Stop, Event::Finalized}, {Event::Abort}, {Event::Request, Event::Error}, {Event::Request, Event::Denied}};
    for (int state = 0; state < paths.size(); ++state) for (int event = 0; event <= int(Event::Cancel); ++event) {
      Session session; for (auto e : paths[state]) QVERIFY(session.send(e));
      QCOMPARE(session.state(), State(state)); QSignalSpy changed(&session, &Session::changed), finished(&session, &Session::finished);
      const auto e = Event(event); State expected = State(state);
      if (state < 6) {
        if (e == Event::Abort || e == Event::Cancel) expected = State::Cancelled;
        else if (e == Event::Error && state != 0) expected = State::Failed;
        else if (state == 0 && e == Event::Request) expected = State::RequestingPermission;
        else if (state == 1 && e == Event::Granted) expected = State::Starting;
        else if (state == 1 && e == Event::Denied) expected = State::PermissionDenied;
        else if (state == 2 && e == Event::Started) expected = State::Recording;
        else if (state == 3 && e == Event::Pause) expected = State::Paused;
        else if (state == 4 && e == Event::Resume) expected = State::Recording;
        else if ((state == 3 || state == 4) && e == Event::Stop) expected = State::Stopping;
        else if (state == 5 && e == Event::Finalized) expected = State::Completed;
      }
      QCOMPARE(session.send(e), expected != State(state)); QCOMPARE(session.state(), expected);
      QCOMPARE(changed.size(), expected == State(state) ? 0 : 1);
      QCOMPARE(finished.size(), expected != State(state) && terminal(expected) ? 1 : 0);
      if (terminal(expected)) {
        for (int again = 0; again <= int(Event::Cancel); ++again) QVERIFY(!session.send(Event(again)));
        QVERIFY(finished.size() <= 1);
      }
    }
  }
  void resumeAndSingleTerminalResult() {
    Session s; QSignalSpy result(&s, &Session::finished);
    for (auto e : {Event::Request, Event::Granted, Event::Started, Event::Pause, Event::Resume, Event::Pause, Event::Stop, Event::Finalized}) QVERIFY(s.send(e));
    QCOMPARE(s.state(), State::Completed); QCOMPARE(result.size(), 1);
    Session failed; QSignalSpy error(&failed, &Session::finished); failed.send(Event::Request); failed.send(Event::Error, "/private/error");
    QCOMPARE(error.first().at(1).toString(), QString("encoder-failed"));
  }
  void validationAndNaming() {
    auto r = request(); QVERIFY(validate(r).isEmpty()); QCOMPARE(r.frameRate, 30);
    for (auto value : {0, -1, 121}) { r.frameRate = value; QVERIFY(!validate(r).isEmpty()); }
    for (auto value : {1, 30, 120}) { r.frameRate = value; QVERIFY(validate(r).isEmpty()); }
    r.quality = 101; QVERIFY(!validate(r).isEmpty()); r.quality = 75;
    for (const auto &name : {"../escape.mp4", "x/webm.mp4", "", "video.gif", "x\\file.mp4"}) { r.filename = name; QVERIFY(!validate(r).isEmpty()); }
    r = request(); r.folder = "relative"; QVERIFY(!validate(r).isEmpty());
    r = request(); r.crop = QRect(0, 0, 100, 200); QVERIFY(validate(r).isEmpty());
    r.target = Target::Window; QVERIFY(!validate(r).isEmpty()); r.target = Target::Monitor;
    r.crop = QRect(-1, 0, 10, 10); QVERIFY(!validate(r).isEmpty());
    for (auto f : {Format::Mp4, Format::WebM, Format::Gif}) { r = request(); r.format = f; r.filename = generatedFilename(f); QVERIFY2(validate(r).isEmpty(), qPrintable(r.filename)); }
  }
  void deterministicEncodersAndDescriptions() {
    auto available = elements();
    QCOMPARE(selectEncoder(Format::Mp4, available).element, QString("x264enc")); available.remove("x264enc");
    QCOMPARE(selectEncoder(Format::Mp4, available).element, QString("openh264enc")); available.remove("openh264enc");
    QCOMPARE(selectEncoder(Format::Mp4, available).element, QString("vah264enc")); available.remove("vah264enc");
    QCOMPARE(selectEncoder(Format::Mp4, available).element, QString("vaapih264enc")); available.remove("mp4mux");
    QVERIFY(!selectEncoder(Format::Mp4, available).diagnostic.isEmpty());
    for (auto format : {Format::Mp4, Format::WebM, Format::Gif}) {
      auto r = request(); r.format = format; r.filename = "test." + extension(format);
      auto e = selectEncoder(format, elements()); const auto description = pipeline(r, e, "videotestsrc num-buffers=5");
      QVERIFY(description.startsWith("videotestsrc num-buffers=5 ! videoconvert")); QVERIFY(description.contains("framerate=30/1"));
      QVERIFY(description.contains(e.element)); QVERIFY(description.endsWith("filesink name=output")); QVERIFY(!description.contains("audio"));
      if (format == Format::Mp4) QVERIFY(description.contains("h264parse ! mp4mux"));
      if (format == Format::WebM) QVERIFY(description.contains("webmmux"));
      for (const auto &element : e.required) { auto missing = elements(); missing.remove(element); if (element != e.element) QVERIFY(!selectEncoder(format, missing).diagnostic.isEmpty()); }
      r.crop = QRect(0, 0, 100, 100); QVERIFY(pipeline(r, e, "videotestsrc").isEmpty());
    }
  }
  void outputFinalizationAbortAndCollision() {
    QTemporaryDir folder; QVERIFY(folder.isValid()); auto r = request(folder.path()); Output output; FakeEncoderPort encoder;
    QVERIFY(output.reserve(r).isEmpty()); const auto temporary = output.temporary(); QVERIFY(QFile::exists(temporary));
    QVERIFY(!QFile::exists(output.destination())); QVERIFY(encoder.begin(temporary)); QVERIFY(encoder.finalize());
    QVERIFY(output.publish().isEmpty()); QVERIFY(!QFile::exists(temporary));
    QFile file(output.destination()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("partial-finalized")); file.close();
    QVERIFY(!output.reserve(r).isEmpty()); QVERIFY(file.exists());
    r.filename = "abort.mp4"; QVERIFY(output.reserve(r).isEmpty()); const auto aborted = output.temporary(); QVERIFY(encoder.begin(aborted)); output.abort();
    QVERIFY(!QFile::exists(aborted)); QVERIFY(!QFile::exists(output.destination()));
    r.filename = "empty.mp4"; QVERIFY(output.reserve(r).isEmpty()); QVERIFY(!output.publish().isEmpty()); output.abort();
    r.filename = "collision.mp4"; QVERIFY(output.reserve(r).isEmpty()); QVERIFY(encoder.begin(output.temporary()));
    QFile collision(output.destination()); QVERIFY(collision.open(QIODevice::WriteOnly)); collision.write("other"); collision.close();
    QVERIFY(!output.publish().isEmpty()); output.abort(); QVERIFY(collision.open(QIODevice::ReadOnly)); QCOMPARE(collision.readAll(), QByteArray("other"));
    QString scoped; { Output o; r.filename = "destructor.mp4"; QVERIFY(o.reserve(r).isEmpty()); scoped = o.temporary(); QVERIFY(encoder.begin(scoped)); } QVERIFY(!QFile::exists(scoped));
  }
  void activationAndHotkeys() {
    using namespace xerahs::shell;
    for (auto action : {ActivationAction::StartRecording, ActivationAction::StopRecording, ActivationAction::StartRegionRecording}) {
      auto request = makeActivationRequest(action); const auto parsed = parseActivation(serializeActivation(request)); QVERIFY(parsed.request); QCOMPARE(parsed.request->action, action);
    }
    int starts = 0, stops = 0; bool busy = false;
    ActivationOperations ops; ops.captureBusy = [&] { return busy; }; ops.startRecording = [&] { ++starts; }; ops.stopRecording = [&] { ++stops; };
    ActivationDispatcher dispatcher(ops); auto start = makeActivationRequest(ActivationAction::StartRecording), stop = makeActivationRequest(ActivationAction::StopRecording);
    QCOMPARE(dispatcher.admit(start), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(start).isEmpty()); QCOMPARE(starts, 1);
    auto region = makeActivationRequest(ActivationAction::StartRegionRecording);
    ops.startRegionRecording = [&] { ++starts; }; ActivationDispatcher regionDispatcher(ops);
    QCOMPARE(regionDispatcher.admit(region), ActivationReplyState::Accepted); QVERIFY(regionDispatcher.dispatch(region).isEmpty()); QCOMPARE(starts, 2);
    busy = true; QCOMPARE(regionDispatcher.admit(region), ActivationReplyState::Busy);
    busy = true; QCOMPARE(dispatcher.admit(start), ActivationReplyState::Busy);
    QCOMPARE(dispatcher.admit(stop), ActivationReplyState::Accepted); QVERIFY(dispatcher.dispatch(stop).isEmpty()); QCOMPARE(stops, 1);
    const auto defaults = xerahs::settings::HotkeyBindings::defaults();
    for (const auto &id : {"start-region-recording", "start-recording", "start-gif-recording", "stop-recording"}) { QVERIFY(defaults.entries.contains(id)); QVERIFY(defaults.entries.value(id).isEmpty()); }
  }
};
QTEST_GUILESS_MAIN(RecordingTest)
#include "RecordingTest.moc"
