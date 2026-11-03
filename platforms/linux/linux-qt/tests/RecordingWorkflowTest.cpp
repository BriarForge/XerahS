#include "RecordingController.h"
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
  void nativeControllerValidationUsesRecorderAndStorageRefusalPreventsStart() {
    QTemporaryDir dir; NativeWorkflowExecutions store(dir.filePath("records")); RecordingController controller(nullptr, store.writer());
    QSignalSpy notification(&controller, &RecordingController::notify); auto r = request(dir.path()); r.frameRate = 0;
    controller.start(r, "command-line"); QCOMPARE(controller.state(), State::Failed); QVERIFY(!controller.busy());
    const auto result = store.result(controller.sessionId()); QCOMPARE(result.value("state"), QJsonValue("validation-failure"));
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
