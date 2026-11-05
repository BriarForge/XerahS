#include "recording/Recording.h"
#include "shell/ActivationProtocol.h"
#include "shell/ActivationDispatcher.h"
#include "settings/HotkeyBindings.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
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
    auto unconsented = selectAudio(r, monitor); QVERIFY(unconsented.devices.isEmpty());
    QCOMPARE(unconsented.diagnostics, QStringList{"audio-consent-required"}); QCOMPARE(calls, 0);
    r.consentedAudioDevices = {system.id, mic.id};
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
  void consentCannotAuthorizeAChangedInputAndGifNeverOpensAudio() {
    auto r = request(); r.systemAudioDevice = "SYNTHETIC-monitor"; r.microphoneDevice = "SYNTHETIC-mic";
    int calls = 0; const DeviceMonitor monitor = [&] { ++calls; return QVector<AudioDevice>{{r.systemAudioDevice, "System", true}, {r.microphoneDevice, "Mic", false}}; };
    r.consentedAudioDevices = {r.systemAudioDevice};
    QCOMPARE(audioConsentDiagnostic(r), QString("audio-consent-required")); QVERIFY(selectAudio(r, monitor).devices.isEmpty()); QCOMPARE(calls, 0);
    r.consentedAudioDevices << r.microphoneDevice; QCOMPARE(selectAudio(r, monitor).devices.size(), 2); QCOMPARE(calls, 1);
    r.microphoneDevice = "SYNTHETIC-other";
    QCOMPARE(audioConsentDiagnostic(r), QString("audio-consent-required")); QVERIFY(selectAudio(r, monitor).devices.isEmpty()); QCOMPARE(calls, 1);
    r.format = Format::Gif; QVERIFY(audioConsentDiagnostic(r).isEmpty()); QVERIFY(selectAudio(r, monitor).devices.isEmpty()); QCOMPARE(calls, 1);
  }
  // CORE-017: cursor, target, frame timing and quality reach the portal/encoder boundary deterministically.
  void cursorTargetTimingAndQuality() {
    QCOMPARE(portalSourceType(Target::Monitor), 1u); QCOMPARE(portalSourceType(Target::Window), 2u);
    QCOMPARE(portalCursorMode(true), 2u); QCOMPARE(portalCursorMode(false), 1u);
    QVERIFY(Request{}.cursor);
    for (int fps : {1, 24, 60, 120}) {
      auto r = request(); r.frameRate = fps;
      QVERIFY(pipeline(r, selectEncoder(r.format, elements()), "videotestsrc").contains(QString("framerate=%1/1").arg(fps)));
    }
    const auto with = [&](Format format, int quality, const QString &element) {
      auto r = request(); r.format = format; r.filename = "q." + extension(format); r.quality = quality;
      auto available = elements();
      for (const auto &other : QStringList{"x264enc", "openh264enc", "vah264enc", "vaapih264enc"}) if (other != element) available.remove(other);
      return pipeline(r, selectEncoder(format, available), "videotestsrc");
    };
    QVERIFY(with(Format::Mp4, 0, "x264enc").contains("quantizer=50")); QVERIFY(with(Format::Mp4, 100, "x264enc").contains("quantizer=0"));
    QVERIFY(with(Format::Mp4, 0, "openh264enc").contains("bitrate=500000")); QVERIFY(with(Format::Mp4, 100, "openh264enc").contains("bitrate=10000000"));
    QVERIFY(with(Format::Mp4, 100, "vah264enc").contains("bitrate=10000"));
    QVERIFY(with(Format::WebM, 0, "vp9enc").contains("cq-level=63")); QVERIFY(with(Format::WebM, 100, "vp9enc").contains("cq-level=0"));
    QVERIFY(with(Format::Mp4, 25, "x264enc") != with(Format::Mp4, 75, "x264enc"));
    // Container and codec follow the format; the three containers are distinct.
    QVERIFY(with(Format::Mp4, 75, "x264enc").contains("mp4mux")); QVERIFY(with(Format::WebM, 75, "vp9enc").contains("webmmux"));
    QVERIFY(!with(Format::Gif, 75, "gifenc").contains("mux"));
  }
  // CORE-018: the always-visible indicator states whether audio is captured and follows the recording state.
  void captureIndicator() {
    QCOMPARE(indicatorText(State::Recording, 65000, "x264enc", true), QString("XerahS · recording · 1:05 · x264enc · Audio recording"));
    QCOMPARE(indicatorText(State::Paused, 9000, "vp9enc", false), QString("XerahS · paused · 0:09 · vp9enc · No audio"));
    QVERIFY(indicatorText(State::Recording, 0, "x264enc", true) != indicatorText(State::Recording, 0, "x264enc", false));
  }
  // CORE-017: an encoder fault mid-recording ends in one Failed result with a stable code, removes the partial output,
  // leaves any existing file alone and lets the next recording start cleanly.
  void recoverableFailureLeavesNoPartial() {
    QTemporaryDir folder; QVERIFY(folder.isValid()); auto r = request(folder.path()); r.filename = "fault.mp4";
    Output output; FakeEncoderPort encoder; Session session; QSignalSpy finished(&session, &Session::finished);
    QVERIFY(output.reserve(r).isEmpty());
    for (auto e : {Event::Request, Event::Granted, Event::Started}) QVERIFY(session.send(e));
    const auto temporary = output.temporary(); QVERIFY(encoder.begin(temporary)); QVERIFY(QFile::exists(temporary));
    QVERIFY(session.send(Event::Error, "/home/user/private/path: gst error"));
    QCOMPARE(finished.size(), 1); QCOMPARE(finished.first().at(0).value<State>(), State::Failed); QCOMPARE(finished.first().at(1).toString(), QString("encoder-failed"));
    output.abort(); QVERIFY(!QFile::exists(temporary)); QVERIFY(!QFile::exists(output.destination()));
    QVERIFY(QDir(folder.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    Session next; QVERIFY(next.send(Event::Request)); QVERIFY(output.reserve(r).isEmpty());
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
  void targetedControlsAreIdempotentAndKeepThePublishedOutput() {
    QTemporaryDir folder; Session session, stale;
    QVERIFY(!QUuid(session.id()).isNull()); QVERIFY(session.id() != stale.id());
    for (auto e : {Event::Request, Event::Granted, Event::Started}) QVERIFY(session.send(e));
    const auto wrong = session.control(stale.id(), Event::Stop);
    QVERIFY(!wrong.accepted); QCOMPARE(wrong.diagnostic, QString("stale-recording-session")); QCOMPARE(session.state(), State::Recording);
    QCOMPARE(session.control(session.id(), Event::Resume).changed, false);
    QVERIFY(session.control(session.id(), Event::Pause).changed);
    QVERIFY(session.control(session.id(), Event::Pause).accepted);
    QVERIFY(!session.control(session.id(), Event::Pause).changed);
    QSignalSpy finished(&session, &Session::finished), changed(&session, &Session::changed);
    const auto stopped = session.control(session.id(), Event::Stop);
    QVERIFY(stopped.accepted); QVERIFY(stopped.changed); QCOMPARE(stopped.state, State::Stopping);
    const auto repeated = session.control(session.id(), Event::Stop);
    QVERIFY(repeated.accepted); QVERIFY(!repeated.changed); QCOMPARE(changed.size(), 1); QCOMPARE(finished.size(), 0);
    Output output; auto r = request(folder.path()); QVERIFY(output.reserve(r).isEmpty());
    FakeEncoderPort encoder; QVERIFY(encoder.begin(output.temporary())); QVERIFY(encoder.finalize());
    QVERIFY(session.finalize(output));
    const auto result = session.control(session.id(), Event::Stop);
    const auto again = session.control(session.id(), Event::Stop);
    QVERIFY(result.accepted); QVERIFY(!result.changed); QCOMPARE(result.output, output.destination());
    QCOMPARE(again.output, result.output); QCOMPARE(again.sessionId, result.sessionId); QCOMPARE(finished.size(), 1);
    QVERIFY(!session.control(session.id(), Event::Pause).accepted);
    QVERIFY(!session.control(stale.id(), Event::Stop).accepted);
  }
  void encoderFailureRetainsPrivatePartialMediaButAbortDiscards() {
    QTemporaryDir folder; QVERIFY(folder.isValid()); QString retained;
    {
      Output output; Session session; auto r = request(folder.path()); QVERIFY(output.reserve(r).isEmpty());
      FakeEncoderPort encoder; QVERIFY(encoder.begin(output.temporary()));
      for (auto e : {Event::Request, Event::Granted, Event::Started}) QVERIFY(session.send(e));
      QVERIFY(session.fail(output, "encoder-failed")); retained = session.result().recoverableOutput; QVERIFY(!retained.isEmpty());
      const auto result = session.result(); QCOMPARE(result.state, State::Failed); QVERIFY(result.output.isEmpty());
      QCOMPARE(result.recoverableOutput, retained); QVERIFY(result.cleanupRequired); QCOMPARE(result.diagnostic, QString("encoder-failed"));
      QVERIFY(!QFile::exists(output.destination())); QVERIFY(output.temporary().isEmpty());
      QFile file(retained); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("partial"));
      QCOMPARE(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther), QFileDevice::Permissions{});
      r.filename = "next.mp4"; QVERIFY(output.reserve(r).isEmpty()); output.abort();
    }
    QVERIFY(QFile::exists(retained)); // Neither a new session nor destruction deletes recovery data.
    {
      Output output; auto r = request(folder.path()); r.filename = "abort.mp4"; QVERIFY(output.reserve(r).isEmpty());
      const auto partial = output.temporary(); FakeEncoderPort encoder; QVERIFY(encoder.begin(partial));
      Session session; for (auto e : {Event::Request, Event::Granted, Event::Started}) QVERIFY(session.send(e));
      QSignalSpy finished(&session, &Session::finished);
      QVERIFY(session.control(session.id(), Event::Abort).accepted); output.abort();
      QVERIFY(session.control(session.id(), Event::Abort).accepted); QCOMPARE(finished.size(), 1);
      QVERIFY(!QFile::exists(partial)); QVERIFY(!QFile::exists(output.destination()));
      QVERIFY(session.result().recoverableOutput.isEmpty()); QVERIFY(!session.result().cleanupRequired);
    }
    Output empty; auto r = request(folder.path()); r.filename = "empty.mp4"; QVERIFY(empty.reserve(r).isEmpty());
    const auto temporary = empty.temporary(); QVERIFY(empty.retainFailure().isEmpty()); QVERIFY(!QFile::exists(temporary));
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
