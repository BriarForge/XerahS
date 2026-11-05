#include "RecordingAudioConsent.h"
#include "RecordingActions.h"
#include "RecordingErrors.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
using namespace xerahs::app;
using namespace xerahs::recording;
class RecordingAudioTest final : public QObject {
  Q_OBJECT
private slots:
  void nativeChoicesRequireFreshExplicitConsentForExactFrozenInputs() {
    RecordingAudioConsent consent("SYNTHETIC-system", "SYNTHETIC-mic"); consent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&consent)); consent.activateWindow();
    auto *system = consent.findChild<QCheckBox *>("recordSystemAudioConsent");
    auto *mic = consent.findChild<QCheckBox *>("recordMicrophoneConsent"); QVERIFY(system && mic);
    QVERIFY(!system->isChecked()); QVERIFY(!mic->isChecked());
    Request r; r.systemAudioDevice = "SYNTHETIC-stale"; r.consentedAudioDevices << r.systemAudioDevice;
    consent.apply(r); QVERIFY(r.systemAudioDevice.isEmpty()); QVERIFY(r.microphoneDevice.isEmpty()); QVERIFY(r.consentedAudioDevices.isEmpty());
    system->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), system);
    QTest::keyClick(system, Qt::Key_Space); consent.apply(r);
    QCOMPARE(r.systemAudioDevice, QString("SYNTHETIC-system")); QVERIFY(r.microphoneDevice.isEmpty());
    QCOMPARE(r.consentedAudioDevices, QStringList{"SYNTHETIC-system"}); QVERIFY(audioConsentDiagnostic(r).isEmpty());
    QTest::keyClick(system, Qt::Key_Tab); QCOMPARE(QApplication::focusWidget(), mic);
    QTest::keyClick(mic, Qt::Key_Space); consent.apply(r); QCOMPARE(r.consentedAudioDevices.size(), 2);
    QVERIFY(audioConsentDiagnostic(r).isEmpty()); QCOMPARE(r.microphoneDevice, QString("SYNTHETIC-mic"));
    consent.setFormat(Format::Gif); QVERIFY(!system->isEnabled()); QVERIFY(!mic->isEnabled()); consent.apply(r);
    QVERIFY(r.systemAudioDevice.isEmpty()); QVERIFY(r.microphoneDevice.isEmpty()); QVERIFY(r.consentedAudioDevices.isEmpty());
    consent.setFormat(Format::Mp4); QVERIFY(system->isEnabled()); QVERIFY(!system->isChecked()); QVERIFY(!mic->isChecked());
    RecordingAudioConsent next("SYNTHETIC-system", "SYNTHETIC-mic"); next.apply(r); QVERIFY(r.consentedAudioDevices.isEmpty());
    RecordingAudioConsent missing({}, {}); QVERIFY(!missing.findChild<QCheckBox *>("recordSystemAudioConsent")->isEnabled());
    QVERIFY(!missing.findChild<QCheckBox *>("recordMicrophoneConsent")->isEnabled());
  }
#ifdef XERAHS_GSTREAMER
  void nativeAudioErrorKeepsPrivatePayloadOutOfFailureAndRetainsPartial_data() {
    QTest::addColumn<bool>("paused"); QTest::newRow("recording") << false; QTest::newRow("paused") << true;
  }
  void nativeAudioErrorKeepsPrivatePayloadOutOfFailureAndRetainsPartial() {
    QFETCH(bool, paused); QVERIFY(gst_init_check(nullptr, nullptr, nullptr));
    auto *pipeline = gst_pipeline_new("recording"); auto *input = gst_bin_new("audioinput0"); auto *backend = gst_bin_new("backend");
    QVERIFY(gst_bin_add(GST_BIN(input), backend)); QVERIFY(gst_bin_add(GST_BIN(pipeline), input));
    auto *bus = gst_element_get_bus(pipeline); QVERIFY(bus);
    GError *error = g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ, "SYNTHETIC_PRIVATE_DEVICE_ERROR");
    QVERIFY(gst_bus_post(bus, gst_message_new_error(GST_OBJECT(backend), error, "SYNTHETIC_PRIVATE_DEBUG"))); g_error_free(error);
    auto *message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR); QVERIFY(message);
    const auto code = recordingErrorCode(message); QCOMPARE(code, QString("audio-input-unavailable"));
    gst_message_unref(message); gst_object_unref(bus); gst_object_unref(pipeline);
    QTemporaryDir dir; Request r; r.folder = dir.path(); r.filename = "recording.mp4"; Output output; Session session;
    QVERIFY(output.reserve(r).isEmpty()); { QFile file(output.temporary()); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("SYNTHETIC-PARTIAL"), 17); }
    for (const auto event : {Event::Request, Event::Granted, Event::Started}) QVERIFY(session.send(event));
    if (paused) QVERIFY(session.send(Event::Pause));
    QSignalSpy finished(&session, &Session::finished); QVERIFY(session.fail(output, code));
    const auto result = session.result(); QCOMPARE(result.state, State::Failed); QCOMPARE(result.diagnostic, code);
    QVERIFY(result.output.isEmpty()); QVERIFY(result.cleanupRequired); QVERIFY(QFile::exists(result.recoverableOutput));
    QVERIFY(!QFile::exists(output.destination())); QCOMPARE(finished.size(), 1); QVERIFY(!session.send(Event::Started));
    auto *dialog = makeRecordingResultDialog(result); auto *explanation = dialog->findChild<QLabel *>("recordingAudioFailure");
    QVERIFY(explanation); QVERIFY(explanation->text().contains("recording stopped")); QVERIFY(explanation->text().contains("platform permission"));
    QVERIFY(!explanation->text().contains("SYNTHETIC_PRIVATE")); delete dialog;
    QFile retained(result.recoverableOutput); QVERIFY(retained.open(QIODevice::ReadOnly)); QCOMPARE(retained.readAll(), QByteArray("SYNTHETIC-PARTIAL"));
  }
  void nonAudioBackendErrorsRemainEncoderFailures() {
    QVERIFY(gst_init_check(nullptr, nullptr, nullptr)); auto *encoder = gst_bin_new("encoder");
    GError *error = g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_READ, "SYNTHETIC_PRIVATE_ERROR");
    auto *message = gst_message_new_error(GST_OBJECT(encoder), error, "SYNTHETIC_PRIVATE_DEBUG"); g_error_free(error);
    QCOMPARE(recordingErrorCode(message), QString("encoder-failed")); gst_message_unref(message); gst_object_unref(encoder);
  }
#endif
};
int main(int argc, char **argv) {
  QTemporaryDir profile; qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", "");
  for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) qputenv(key, profile.filePath(key).toUtf8());
  qputenv("DBUS_SESSION_BUS_ADDRESS", ("unix:path=" + profile.filePath("no-bus")).toUtf8());
  QApplication app(argc, argv); RecordingAudioTest test; return QTest::qExec(&test, argc, argv);
}
#include "RecordingAudioTest.moc"
