#include "RecordingController.h"
#include "RecordingObservation.h"
#include "actions/PipelineResultJson.h"
#include "NativeWorkflowExecutions.h"
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::recording;
using namespace xerahs::workflows;
namespace {
QStringList terminalLogs;
void collectTerminalLog(QtMsgType, const QMessageLogContext &, const QString &message) { terminalLogs.append(message); }
struct LogCapture {
  QtMessageHandler previous;
  LogCapture() { terminalLogs.clear(); previous = qInstallMessageHandler(collectTerminalLog); }
  ~LogCapture() { qInstallMessageHandler(previous); }
};
Request request(const QString &folder) {
  Request r; r.folder = folder; r.filename = "PRIVATE-recording.mp4"; r.frameRate = 24; r.quality = 83; r.cursor = false;
  r.systemAudioDevice = "PRIVATE-monitor"; r.microphoneDevice = "PRIVATE-microphone"; r.region = QRectF(3, 4, 100, 60); return r;
}
QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
void partial(Output &output) { QFile f(output.temporary()); if (!f.open(QIODevice::WriteOnly) || f.write("private-media") != 13) qFatal("Cannot prepare encoder fixture"); }
}
class RecordingWorkflowTest final : public QObject {
  Q_OBJECT
private slots:
  void recordingUsesTheSameDurableExecutionRecord_data() {
    QTest::addColumn<QString>("origin");
    for (const auto *origin : {"native-shell", "global-shortcut", "command-line", "external-activation"}) QTest::newRow(origin) << QString(origin);
  }
  void recordingUsesTheSameDurableExecutionRecord() {
    QFETCH(QString, origin); QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); Session session;
    auto r = request(dir.path()); QVERIFY(session.observe(r, origin, store.writer())); const auto id = session.id();
    QVERIFY(!QUuid(id).isNull()); const auto raw = bytes(dir.filePath("records/" + id + ".json"));
    const auto admitted = parseWorkflowExecution(raw, id); QVERIFY(admitted); QVERIFY(admitted->startedAt.isValid());
    QCOMPARE(admitted->origin, origin); QCOMPARE(admitted->settings.value("frameRate"), QJsonValue(24));
    QCOMPARE(admitted->settings.value("quality"), QJsonValue(83)); QCOMPARE(admitted->settings.value("cursor"), QJsonValue(false));
    QCOMPARE(admitted->settings.value("filename"), QJsonValue(r.filename)); QCOMPARE(admitted->settings.value("directory"), QJsonValue(r.folder));
    QCOMPARE(admitted->settings.value("region"), QJsonValue(QJsonArray{3, 4, 100, 60}));
    QCOMPARE(admitted->settings.value("systemAudioDevice"), QJsonValue(r.systemAudioDevice));
    QCOMPARE(admitted->settings.value("microphoneDevice"), QJsonValue(r.microphoneDevice));
    r.frameRate = 60; r.systemAudioDevice.clear(); // Editing caller settings does not alter the admitted snapshot.
    Output output; QVERIFY(output.reserve(r).isEmpty()); partial(output);
    for (const auto event : {Event::Request, Event::Granted, Event::Started, Event::Pause, Event::Resume}) QVERIFY(session.send(event));
    QVERIFY(session.control(id, Event::Stop).accepted);
    QCOMPARE(store.result(id).value("terminal"), QJsonValue(false)); QVERIFY(!QFile::exists(output.destination()));
    QVERIFY(session.finalize(output)); QVERIFY(QFile::exists(output.destination()));
    const auto query = store.result(id); QCOMPARE(query.value("state"), QJsonValue("succeeded")); QVERIFY(query.value("terminal").toBool());
    const auto record = parseWorkflowExecution(bytes(dir.filePath("records/" + id + ".json")), id); QVERIFY(record);
    QCOMPARE(record->settings, admitted->settings); QCOMPARE(record->startedAt, admitted->startedAt); QVERIFY(record->finishedAt >= record->startedAt);
    QCOMPARE(record->steps, QJsonArray({QJsonObject{{"action", "start-recording"}, {"state", "succeeded"}},
        QJsonObject{{"action", "pause-recording"}, {"state", "succeeded"}}, QJsonObject{{"action", "resume-recording"}, {"state", "succeeded"}},
        QJsonObject{{"action", "stop-recording"}, {"state", "succeeded"}}}));
    const auto terminalBytes = bytes(dir.filePath("records/" + id + ".json"));
    const auto repeated = session.control(id, Event::Stop); QVERIFY(repeated.accepted); QVERIFY(!repeated.changed); QCOMPARE(repeated.output, output.destination());
    QCOMPARE(bytes(dir.filePath("records/" + id + ".json")), terminalBytes);
    QCOMPARE(NativeWorkflowExecutions(dir.filePath("records")).result(id), query);
    QVERIFY(!QJsonDocument(query).toJson().contains("PRIVATE")); QVERIFY(!QJsonDocument(query).toJson().contains(dir.path().toUtf8()));
  }
  void recordingFailuresAndAbortHaveOneRedactedTerminal_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"denied", "cancelled", "abort", "encoder-failed", "request-invalid", "encoder-unavailable"}) QTest::newRow(mode) << QString(mode);
  }
  void recordingFailuresAndAbortHaveOneRedactedTerminal() {
    QFETCH(QString, mode); QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); Session session; auto r = request(dir.path());
    QVERIFY(session.observe(r, "native-shell", store.writer())); QVERIFY(session.send(Event::Request)); Output output;
    QString expected;
    if (mode == "denied") { QVERIFY(session.send(Event::Denied)); expected = "permission-denied"; }
    else if (mode == "cancelled") { QVERIFY(session.send(Event::Cancel)); expected = "cancelled"; }
    else {
      if (mode == "abort" || mode == "encoder-failed") {
        QVERIFY(session.send(Event::Granted)); QVERIFY(session.send(Event::Started)); QVERIFY(output.reserve(r).isEmpty()); partial(output);
      }
      if (mode == "abort") { QVERIFY(session.control(session.id(), Event::Abort).accepted); output.abort(); expected = "cancelled"; }
      else { QVERIFY(session.fail(output, mode)); expected = mode == "request-invalid" ? "validation-failure" : mode == "encoder-unavailable" ? "unavailable-dependency" : "permanent-failure"; }
    }
    const auto result = store.result(session.id()); QCOMPARE(result.value("state"), QJsonValue(expected)); QVERIFY(result.value("terminal").toBool());
    QVERIFY(!QJsonDocument(result).toJson().contains("PRIVATE")); QVERIFY(!QJsonDocument(result).toJson().contains(dir.path().toUtf8()));
    QVERIFY(!session.send(Event::Error, "PRIVATE-diagnostic"));
    QCOMPARE(store.result(session.id()), result);
    if (mode == "encoder-failed") { QVERIFY(!session.result().recoverableOutput.isEmpty()); QVERIFY(QFile::exists(session.result().recoverableOutput)); }
    if (mode == "abort") { QVERIFY(session.result().recoverableOutput.isEmpty()); QVERIFY(!QFile::exists(output.destination())); }
  }
  void recordingAllFourTerminalSurfacesShareStateAndOpaqueOutputs_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"stop", "abort", "partial-encoder-failure", "denied", "unconfirmed-stop"}) QTest::newRow(mode) << QString(mode);
  }
  void recordingAllFourTerminalSurfacesShareStateAndOpaqueOutputs() {
    QFETCH(QString, mode); QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); Session session;
    const auto writer = store.writer();
    QVERIFY(session.observe(request(dir.path()), "native-shell", [writer, mode](const QString &id, const std::optional<QByteArray> &expected, const QByteArray &data) {
      if (mode == "unconfirmed-stop" && !QJsonDocument::fromJson(data).object().value("finishedAt").toString().isEmpty())
        return xerahs::actions::JournalWrite{false, "pipeline-journal-write-failed"};
      return writer(id, expected, data);
    }));
    RecordingObservation presentation(session);
    QSignalSpy progress(&presentation, &RecordingObservation::activityChanged), notification(&presentation, &RecordingObservation::notify);
    QSignalSpy observed(&presentation, &RecordingObservation::terminalObserved);
    Output output; LogCapture logs;
    QVERIFY(session.send(Event::Request));
    if (mode == "denied") QVERIFY(session.send(Event::Denied));
    else {
      QVERIFY(output.reserve(request(dir.path())).isEmpty()); partial(output);
      QVERIFY(session.send(Event::Granted)); QVERIFY(session.send(Event::Started));
      if (mode == "stop" || mode == "unconfirmed-stop") { QVERIFY(session.control(session.id(), Event::Stop).accepted); QVERIFY(session.finalize(output)); }
      else if (mode == "abort") { QVERIFY(session.control(session.id(), Event::Abort).accepted); output.abort(); }
      else QVERIFY(session.fail(output, "encoder-failed"));
    }
    QCOMPARE(progress.size(), 1); QCOMPARE(notification.size(), 1); QCOMPARE(observed.size(), 1);
    const auto observation = observed.first().first().toJsonObject();
    const auto text = xerahs::actions::pipelineObservationText(observation);
    const auto history = NativeWorkflowExecutions(dir.filePath("records")).result(session.id());
    QCOMPARE(observation.value("state"), history.value("state")); QCOMPARE(observation.value("outputs"), history.value("outputs"));
    QCOMPARE(progress.first().first().toString(), text); QCOMPARE(notification.first()[1].toString(), text);
    QVERIFY(terminalLogs.contains("Workflow terminal " + text));
    const bool success = mode == "stop"; QCOMPARE(notification.first()[2].toBool(), success);
    if (!success) { QVERIFY(!notification.first().first().toString().contains("completed")); QVERIFY(!text.contains("succeeded")); }
    const auto outputs = observation.value("outputs").toArray();
    if (mode == "stop" || mode == "partial-encoder-failure") {
      QCOMPARE(outputs.size(), 1); QCOMPARE(outputs.first(), QJsonValue(session.id() + (success ? "/recording.file" : "/recording.partial")));
    } else QVERIFY(outputs.isEmpty());
    if (mode == "unconfirmed-stop") { QCOMPARE(observation.value("state"), QJsonValue("outcome-unconfirmed")); QVERIFY(QFile::exists(session.result().output)); }
    const auto surfaces = QJsonDocument(history).toJson() + QJsonDocument(observation).toJson() + text.toUtf8() + terminalLogs.join('\n').toUtf8();
    QVERIFY(!surfaces.contains("PRIVATE")); QVERIFY(!surfaces.contains("private-media")); QVERIFY(!surfaces.contains(dir.path().toUtf8()));
    // History cannot be coerced into exposing an arbitrary path or a completed file after failure.
    auto bad = QJsonDocument::fromJson(bytes(dir.filePath("records/" + session.id() + ".json"))).object();
    bad.insert("outputs", QJsonArray{dir.filePath("PRIVATE-recording.mp4")});
    QVERIFY(!parseWorkflowExecution(QJsonDocument(bad).toJson(), session.id()));
    QVERIFY(!session.send(Event::Error, "PRIVATE")); QCOMPARE(notification.size(), 1);
  }
  void deletingTerminalPresentationDuringProgressCannotNotifyALaterOwner() {
    Session session; auto presentation = std::make_unique<RecordingObservation>(session); int notifications = 0;
    connect(presentation.get(), &RecordingObservation::notify, this, [&](const auto &, const auto &, bool) { ++notifications; });
    connect(presentation.get(), &RecordingObservation::activityChanged, this, [&](const auto &) { presentation.reset(); });
    QVERIFY(session.send(Event::Request)); QVERIFY(session.send(Event::Denied)); QVERIFY(!presentation); QCOMPARE(notifications, 0);
  }
  void nativeControllerValidationUsesRecorderAndStorageRefusalPreventsStart() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); RecordingController controller(nullptr, store.writer());
    QSignalSpy terminal(&controller, &RecordingController::terminalObserved), progress(&controller, &RecordingController::activityChanged);
    QSignalSpy notification(&controller, &RecordingController::notify); auto r = request(dir.path()); r.frameRate = 0;
    controller.start(r, "command-line"); QCOMPARE(controller.state(), State::Failed); QVERIFY(!controller.busy());
    const auto result = store.result(controller.sessionId()); QCOMPARE(result.value("state"), QJsonValue("validation-failure"));
    QCOMPARE(terminal.size(), 1); QCOMPARE(progress.size(), 1);
    QCOMPARE(notification.first()[1].toString(), progress.first().first().toString());
    QCOMPARE(terminal.first().first().toJsonObject().value("state"), result.value("state"));
    QCOMPARE(result.value("origin"), QJsonValue("command-line")); QVERIFY(result.value("terminal").toBool()); QCOMPARE(notification.size(), 1); QVERIFY(!notification[0][2].toBool());
    int writes = 0; RecordingController refused(nullptr, [&](const QString &, const std::optional<QByteArray> &, const QByteArray &) {
      ++writes; return xerahs::actions::JournalWrite{false, "pipeline-journal-write-failed"};
    });
    QSignalSpy failure(&refused, &RecordingController::notify); refused.start(request(dir.path()));
    QCOMPARE(writes, 1); QCOMPARE(refused.state(), State::Idle); QVERIFY(refused.sessionId().isEmpty()); QCOMPARE(failure.size(), 1); QVERIFY(!failure[0][2].toBool());
    QVERIFY(!QFile::exists(dir.filePath("PRIVATE-recording.mp4")));
  }
};
QTEST_GUILESS_MAIN(RecordingWorkflowTest)
#include "RecordingWorkflowTest.moc"
