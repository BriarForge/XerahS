#include "NativeInstance.h"
#include "workflows/CaptureWorkflows.h"
#include <QJsonArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>
#include <vector>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace xerahs::app;
using namespace xerahs::shell;
namespace {
QString helper() { return QCoreApplication::applicationDirPath() + "/native-instance-fixture"; }
QJsonObject line(const QByteArray &bytes) { return QJsonDocument::fromJson(bytes.trimmed()).object(); }
QByteArray fileBytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
}
class NativeInstanceTest final : public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> temporary;
  std::vector<std::unique_ptr<QProcess>> processes;
  QString directory;
  QProcess *launch(const QString &admission, const ActivationRequest &request = makeActivationRequest(ActivationAction::Activate), const QString &other = {}) {
    auto process = std::make_unique<QProcess>(); auto *result = process.get();
    process->start(helper(), {other.isEmpty() ? directory : other, admission, QString::fromUtf8(serializeActivation(request))});
    processes.push_back(std::move(process)); return result;
  }
  void awaitLine(QProcess &process) { QTRY_VERIFY(process.canReadLine() || process.state() == QProcess::NotRunning); }
  QByteArray exchange(NativeInstance &instance, const QByteArray &request) {
    QLocalSocket socket; socket.connectToServer(instance.endpoint());
    if (!socket.waitForConnected(1000)) return {};
    socket.write(request); socket.flush();
    QByteArray reply; QElapsedTimer deadline; deadline.start();
    while (deadline.elapsed() < 3000 && !reply.contains('\n') && socket.state() != QLocalSocket::UnconnectedState) {
      QCoreApplication::processEvents(); reply += socket.readAll(); QTest::qWait(1);
    }
    reply += socket.readAll(); return reply;
  }
private slots:
  void init() { temporary = std::make_unique<QTemporaryDir>(); QVERIFY(temporary->isValid()); directory = temporary->filePath("profile"); }
  void cleanup() {
    for (auto &process : processes) if (process->state() != QProcess::NotRunning) { process->kill(); QVERIFY(process->waitForFinished(3000)); }
    processes.clear(); temporary.reset();
  }
  void forwardsToOneRealProcessWithoutReplacingItsSocket() {
    auto *owner = launch("accept"); awaitLine(*owner); QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    struct stat before{}; const auto path = QFile::encodeName(directory + "/instance.sock"); QVERIFY(lstat(path.constData(), &before) == 0);
    const auto request = makeActivationRequest(ActivationAction::EditImage, temporary->filePath(QString::fromUtf8(" λ\n image .png ")));
    NativeInstance client(directory); const auto started = client.start(request);
    QCOMPARE(started.kind, InstanceStart::Kind::Forwarded); QVERIFY(!client.primary());
    awaitLine(*owner); const auto event = line(owner->readLine()); QCOMPARE(event.value("event"), QJsonValue("activation"));
    QCOMPARE(event.value("request").toObject(), QJsonDocument::fromJson(serializeActivation(request)).object());
    struct stat after{}; QVERIFY(lstat(path.constData(), &after) == 0); QCOMPARE(after.st_ino, before.st_ino);
  }
  void busyRejectionAndNotReadyNeverBecomeAnotherPrimary_data() {
    QTest::addColumn<QString>("admission"); QTest::addColumn<QString>("expected");
    QTest::newRow("busy") << QString("busy") << QString("activation-busy");
    QTest::newRow("startup") << QString("not-ready") << QString("activation-busy");
    QTest::newRow("rejected") << QString("reject") << QString("activation-unavailable");
    QTest::newRow("exception") << QString("throw") << QString("activation-handler-failed");
  }
  void busyRejectionAndNotReadyNeverBecomeAnotherPrimary() {
    QFETCH(QString, admission); QFETCH(QString, expected); auto *owner = launch(admission); awaitLine(*owner);
    QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    NativeInstance client(directory); const auto started = client.start(makeActivationRequest(ActivationAction::CaptureRegion));
    QCOMPARE(started.diagnostic, expected); QCOMPARE(started.kind, expected == u"activation-busy" ? InstanceStart::Kind::Busy : InstanceStart::Kind::Failed);
    QVERIFY(!client.primary()); QCOMPARE(owner->state(), QProcess::Running); QTest::qWait(20); QVERIFY(!owner->canReadLine());
  }
  void simultaneousProcessesElectOnlyOneOwner() {
    auto *one = launch("accept"), *two = launch("accept"); awaitLine(*one); awaitLine(*two);
    const auto a = line(one->readLine()).value("event").toString(), b = line(two->readLine()).value("event").toString();
    QCOMPARE(int(a == u"primary") + int(b == u"primary"), 1);
    const auto other = a == u"primary" ? b : a; QVERIFY(other == u"forwarded" || other == u"failed");
    auto *secondary = a == u"primary" ? two : one; QTRY_COMPARE(secondary->state(), QProcess::NotRunning);
  }
  void missingOrUntrustedAcknowledgementsNeverElectOrResend_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"ack-accepted", "ack-drop", "ack-timeout", "ack-mismatch", "ack-malformed", "ack-partial", "ack-oversized", "ack-two-frames", "no-listen"}) QTest::newRow(mode) << QString(mode);
  }
  void missingOrUntrustedAcknowledgementsNeverElectOrResend() {
    QFETCH(QString, mode); auto *owner = launch(mode); awaitLine(*owner); QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    struct stat before{}; const auto lock = QFile::encodeName(directory + "/instance.lock"); QVERIFY(lstat(lock.constData(), &before) == 0);
    const auto request = makeActivationRequest(ActivationAction::EditImage, temporary->filePath("private fixture.png"));
    NativeInstance client(directory, {100, 16, {}}); QElapsedTimer deadline; deadline.start(); const auto result = client.start(request);
    QVERIFY(deadline.elapsed() < 1000); QVERIFY(!client.primary()); QCOMPARE(owner->state(), QProcess::Running);
    QCOMPARE(result.kind, mode == u"ack-accepted" ? InstanceStart::Kind::Forwarded : InstanceStart::Kind::Failed);
    if (mode != u"ack-accepted") QCOMPARE(result.diagnostic, mode == u"no-listen" ? QString("instance-unavailable") : QString("activation-delivery-unknown"));
    if (mode != u"no-listen") { awaitLine(*owner); const auto received = line(owner->readLine()); QCOMPARE(received.value("event"), QJsonValue("received")); QCOMPARE(received.value("id"), QJsonValue(request.id)); }
    QTest::qWait(20); QVERIFY(!owner->canReadLine());
    struct stat after{}; QVERIFY(lstat(lock.constData(), &after) == 0); QCOMPARE(after.st_ino, before.st_ino);
  }
  void intakeDeadlineFreesCapacityWithoutAcceptingIncompleteWork() {
    NativeInstance instance(directory, {100, 1, {}});
    QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    instance.setAdmission([](const ActivationRequest &) { return ActivationReplyState::Accepted; });
    QSignalSpy activated(&instance, &NativeInstance::activationRequested);
    QLocalSocket stalled; stalled.connectToServer(instance.endpoint()); QVERIFY(stalled.waitForConnected(1000));
    stalled.write("{"); stalled.flush(); QCoreApplication::processEvents();
    QLocalSocket excess; excess.connectToServer(instance.endpoint()); QVERIFY(excess.waitForConnected(1000));
    const auto request = makeActivationRequest(ActivationAction::Activate);
    excess.write(serializeActivation(request) + '\n'); excess.flush();
    QTRY_COMPARE(excess.state(), QLocalSocket::UnconnectedState); QCOMPARE(activated.count(), 0);
    QTRY_COMPARE(stalled.state(), QLocalSocket::UnconnectedState); QCOMPARE(activated.count(), 0);
    const auto reply = parseActivationReply(exchange(instance, serializeActivation(request) + '\n').trimmed(), request.id);
    QVERIFY(reply); QCOMPARE(reply->state, ActivationReplyState::Accepted); QTRY_COMPARE(activated.count(), 1);
  }
  void aCrashedOwnerReleasesItsLifetimeLockAndStaleSocket() {
    auto *owner = launch("accept"); awaitLine(*owner); QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    owner->kill(); QVERIFY(owner->waitForFinished(3000)); QVERIFY(QFileInfo::exists(directory + "/instance.sock"));
    NativeInstance replacement(directory); QCOMPARE(replacement.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    QVERIFY(replacement.primary()); QCOMPARE(QFileInfo(directory + "/instance.lock").size(), qint64(0));
  }
  void partialMalformedOversizedAndMultipleFramesNeverDispatch_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("incomplete") << QByteArray("{"); QTest::newRow("invalid") << QByteArray("{private fixture}\n");
    QTest::newRow("oversized") << QByteArray(maximumActivationBytes + 1, 'x');
    const auto request = serializeActivation(makeActivationRequest(ActivationAction::Activate)) + '\n';
    QTest::newRow("two-frames") << request + request;
    QTest::newRow("claimed-authority") << QByteArray("{\"version\":1,\"id\":\"01234567-89ab-4cde-8123-456789abcdef\",\"action\":\"activate\",\"approved\":true}\n");
  }
  void partialMalformedOversizedAndMultipleFramesNeverDispatch() {
    QFETCH(QByteArray, bytes); NativeInstance instance(directory, {100, 16, {}});
    QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    int admissions = 0; instance.setAdmission([&](const ActivationRequest &) { ++admissions; return ActivationReplyState::Accepted; });
    QSignalSpy activated(&instance, &NativeInstance::activationRequested); const auto response = exchange(instance, bytes);
    QCOMPARE(line(response).value("state"), QJsonValue("rejected")); QCOMPARE(line(response).value("error"), QJsonValue("activation-invalid"));
    QCOMPARE(admissions, 0); QCOMPARE(activated.count(), 0); QVERIFY(instance.primary());
  }
  void maximumRequestBoundaryAndDuplicateReceiptsUseProductionValidation() {
    NativeInstance instance(directory); QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    int admissions = 0; instance.setAdmission([&](const ActivationRequest &) { ++admissions; return ActivationReplyState::Accepted; });
    QSignalSpy activated(&instance, &NativeInstance::activationRequested); auto request = makeActivationRequest(ActivationAction::Activate);
    auto body = serializeActivation(request); body.append(QByteArray(maximumActivationBytes - body.size(), ' '));
    QCOMPARE(parseActivationReply(exchange(instance, body + '\n').trimmed(), request.id)->state, ActivationReplyState::Accepted);
    QTRY_COMPARE(activated.count(), 1); QCOMPARE(admissions, 1);
    QCOMPARE(parseActivationReply(exchange(instance, serializeActivation(request) + '\n').trimmed(), request.id)->state, ActivationReplyState::Accepted);
    QTest::qWait(20); QCOMPARE(activated.count(), 1); QCOMPARE(admissions, 1);
    request.action = ActivationAction::CaptureRegion;
    const auto changed = parseActivationReply(exchange(instance, serializeActivation(request) + '\n').trimmed(), request.id);
    QVERIFY(changed); QCOMPARE(changed->state, ActivationReplyState::Rejected); QCOMPARE(changed->diagnostic, QString("activation-invalid"));
    QCOMPARE(activated.count(), 1); QCOMPARE(admissions, 1);
  }
  void busyRetryDoesNotCacheAnOperationThatNeverStarted() {
    NativeInstance instance(directory); QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    const auto request = makeActivationRequest(ActivationAction::Activate);
    QCOMPARE(parseActivationReply(exchange(instance, serializeActivation(request) + '\n').trimmed(), request.id)->state, ActivationReplyState::Busy);
    instance.setAdmission([](const ActivationRequest &) { return ActivationReplyState::Accepted; });
    QSignalSpy activated(&instance, &NativeInstance::activationRequested);
    QCOMPARE(parseActivationReply(exchange(instance, serializeActivation(request) + '\n').trimmed(), request.id)->state, ActivationReplyState::Accepted);
    QTRY_COMPARE(activated.count(), 1);
  }
  void unsafeDirectoriesAndEndpointFilesArePreserved_data() {
    QTest::addColumn<QString>("kind");
    for (const auto *kind : {"directory-symlink", "directory-world", "socket-file", "socket-symlink", "lock-symlink", "lock-data", "lock-hardlink"}) QTest::newRow(kind) << QString(kind);
  }
  void unsafeDirectoriesAndEndpointFilesArePreserved() {
    QFETCH(QString, kind); const auto target = temporary->filePath("unrelated"); QFile file(target); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("keep this fixture") > 0); file.close();
    const auto original = fileBytes(target);
    if (kind == u"directory-symlink") QVERIFY(QFile::link(temporary->path(), directory));
    else { QVERIFY(QDir().mkdir(directory)); QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)); }
    if (kind == u"directory-world") QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther | QFile::ExeOther));
    if (kind == u"socket-file") QVERIFY(QFile::copy(target, directory + "/instance.sock"));
    if (kind == u"socket-symlink") QVERIFY(QFile::link(target, directory + "/instance.sock"));
    if (kind == u"lock-symlink") QVERIFY(QFile::link(target, directory + "/instance.lock"));
    if (kind == u"lock-data") { QVERIFY(QFile::copy(target, directory + "/instance.lock")); QVERIFY(QFile::setPermissions(directory + "/instance.lock", QFile::ReadOwner | QFile::WriteOwner)); }
    if (kind == u"lock-hardlink") QVERIFY(::link(QFile::encodeName(target).constData(), QFile::encodeName(directory + "/instance.lock").constData()) == 0);
    NativeInstance instance(directory); QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Failed);
    QVERIFY(!instance.primary()); QCOMPARE(fileBytes(target), original);
    if (kind == u"socket-file") QCOMPARE(fileBytes(directory + "/instance.sock"), original);
  }
  void aLiveEndpointWithoutTheLeaseIsNotUnlinked() {
    QVERIFY(QDir().mkdir(directory)); QVERIFY(QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QLocalServer foreign; foreign.setSocketOptions(QLocalServer::UserAccessOption);
    const auto socket = directory + "/instance.sock"; QVERIFY(foreign.listen(socket)); struct stat before{};
    QVERIFY(lstat(QFile::encodeName(socket).constData(), &before) == 0);
    NativeInstance instance(directory); const auto start = instance.start(makeActivationRequest(ActivationAction::Activate));
    QCOMPARE(start.kind, InstanceStart::Kind::Failed); QCOMPARE(start.diagnostic, QString("instance-endpoint-conflict"));
    struct stat after{}; QVERIFY(lstat(QFile::encodeName(socket).constData(), &after) == 0); QCOMPARE(before.st_ino, after.st_ino); QVERIFY(foreign.isListening());
  }
  void peerAuthenticationPrecedesSendingPrivatePayload() {
    auto *owner = launch("accept"); awaitLine(*owner); QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    NativeInstance client(directory, {1000, 16, [](qintptr) { return std::optional<quint64>(quint64(geteuid()) + 1); }});
    const auto start = client.start(makeActivationRequest(ActivationAction::EditImage, temporary->filePath("private.png")));
    QCOMPARE(start.kind, InstanceStart::Kind::Failed); QCOMPARE(start.diagnostic, QString("activation-peer-denied"));
    QTest::qWait(20); QVERIFY(!owner->canReadLine());
  }
  void actualApplicationForwardsBeforeOpeningPreferencesOrDesktopServices_data() {
    QTest::addColumn<QString>("admission"); QTest::addColumn<int>("exitCode");
    QTest::newRow("accepted") << QString("accept") << 0;
    QTest::newRow("busy") << QString("busy") << 2;
    QTest::newRow("missing-ack") << QString("ack-drop") << 1;
  }
  void actualApplicationForwardsBeforeOpeningPreferencesOrDesktopServices() {
    QFETCH(QString, admission); QFETCH(int, exitCode);
    directory = temporary->filePath("com.xerahs.app"); auto *owner = launch(admission); awaitLine(*owner);
    QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_RUNTIME_DIR", temporary->path());
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    const auto config = temporary->filePath("XDG_CONFIG_HOME");
    const auto native = config + "/com.xerahs.app/linux-qt/editor-settings.json";
    const auto reference = config + "/xerahs/ApplicationConfig.json";
    const QByteArray untouched("{\"unsupported-fixture-version\":999,\"keep\":true}");
    for (const auto &path : {native, reference}) {
      QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
      QFile fixture(path); QVERIFY(fixture.open(QIODevice::WriteOnly)); QCOMPARE(fixture.write(untouched), untouched.size()); fixture.close();
    }
    environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + temporary->filePath("no-host-bus"));
    environment.insert("QT_QPA_PLATFORMTHEME", "");
    QProcess application; application.setProcessEnvironment(environment);
    const auto image = temporary->filePath(QString::fromUtf8(" λ\n private .png "));
    application.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--edit", image});
    QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), exitCode);
    QVERIFY(application.readAllStandardOutput().isEmpty()); const auto errors = application.readAllStandardError();
    QVERIFY(!errors.contains(image.toUtf8())); QVERIFY(!errors.contains("private .png"));
    if (exitCode != 0) { QVERIFY(errors.contains(admission == u"busy" ? "activation-busy" : "activation-delivery-unknown")); QVERIFY(errors.contains("Try again") || errors.contains("before retrying")); }
    if (admission == u"accept") { awaitLine(*owner); const auto event = line(owner->readLine()); QCOMPARE(event.value("request").toObject().value("path"), QJsonValue(image)); }
    // Qt may create framework cache directories during QApplication startup.
    // Assert the product's actual persistence boundary instead: even invalid
    // native/reference settings must remain untouched with no recovery files.
    QCOMPARE(fileBytes(native), untouched); QCOMPARE(fileBytes(reference), untouched);
    QCOMPARE(QDir(QFileInfo(native).absolutePath()).entryList(QDir::Files), QStringList{"editor-settings.json"});
    QCOMPARE(QDir(QFileInfo(reference).absolutePath()).entryList(QDir::Files), QStringList{"ApplicationConfig.json"});
    QVERIFY(!QFileInfo::exists(image));
  }
  void deniedIncomingCredentialsCannotReachAdmission() {
    NativeInstance instance(directory, {200, 16, [](qintptr) { return std::optional<quint64>{}; }});
    QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    int admissions = 0; instance.setAdmission([&](const ActivationRequest &) { ++admissions; return ActivationReplyState::Accepted; });
    const auto reply = exchange(instance, serializeActivation(makeActivationRequest(ActivationAction::Activate)) + '\n');
    QVERIFY(reply.isEmpty()); QCOMPARE(admissions, 0);
  }
  void workflowDiscoveryIsHeadlessReadOnlyAndDoesNotActivatePrimary_data() {
    QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("error");
    for (const auto *mode : {"missing", "saved", "legacy", "unavailable"}) QTest::newRow(mode) << QString(mode) << QString{};
    QTest::newRow("corrupt") << QString("corrupt") << QString("workflow-invalid");
    QTest::newRow("newer") << QString("newer") << QString("workflow-newer-version");
    QTest::newRow("unsafe") << QString("unsafe") << QString("settings-private-storage-required");
    QTest::newRow("symlink") << QString("symlink") << QString("settings-unsafe-path");
  }
  void workflowDiscoveryIsHeadlessReadOnlyAndDoesNotActivatePrimary() {
    using namespace xerahs::workflows; QFETCH(QString, mode); QFETCH(QString, error);
    const auto config = temporary->filePath("config"); const auto parent = config + "/com.xerahs.app/linux-qt"; const auto path = parent + "/capture-workflows.json";
    auto catalog = defaultCaptureWorkflows(); auto &profile = catalog.entries[0]; profile.name = QString::fromUtf8("CLI \"λ\" profile"); profile.output.directory = "/private/destination"; profile.output.pattern = "private-pattern"; profile.extra = {{"secret", "private-token"}};
    if (mode == "unavailable") profile.actions = QStringList{"future-action"};
    auto object = QJsonDocument::fromJson(workflowsJson(catalog)).object();
    if (mode == "newer") object.insert("version", 3);
    if (mode == "legacy") { object.insert("version", 1); auto entries = object.value("workflows").toArray(); auto entry = entries[0].toObject(); entry.remove("output"); entries[0] = entry; object.insert("workflows", entries); }
    const QByteArray original = mode == "corrupt" ? QByteArray("private-token malformed") : QJsonDocument(object).toJson();
    if (mode != "missing") {
      QVERIFY(QDir().mkpath(parent)); QVERIFY(QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
      QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(original), original.size()); file.close(); QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
      if (mode == "unsafe") QVERIFY(file.setPermissions(QFile::permissions(path) | QFile::ReadGroup));
      if (mode == "symlink") { QVERIFY(QFile::rename(path, path + ".target")); QVERIFY(QFile::link(path + ".target", path)); }
    }
    int activations = 0; std::unique_ptr<NativeInstance> owner;
    if (mode == "saved") {
      owner = std::make_unique<NativeInstance>(temporary->filePath("com.xerahs.app")); QCOMPARE(owner->start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
      owner->setAdmission([&](const ActivationRequest &) { ++activations; return ActivationReplyState::Busy; });
    }
    auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("XDG_RUNTIME_DIR", temporary->path()); environment.insert("XDG_CONFIG_HOME", config);
    for (const auto *key : {"XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + temporary->filePath("no-host-bus")); environment.insert("QT_QPA_PLATFORM", "xcb"); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    QProcess application; application.setProcessEnvironment(environment); QStringList arguments{"--list-workflows"};
    if (mode != "saved") arguments.append({"--non-interactive", "--json"});
    application.start(QCoreApplication::applicationDirPath() + "/xerahs", arguments); QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), error.isEmpty() ? 0 : 1);
    const auto bytes = application.readAllStandardOutput(); QCOMPARE(bytes.count('\n'), 1); const auto result = line(bytes); QCOMPARE(result.value("schemaVersion"), QJsonValue(1)); QCOMPARE(result.value("ok").toBool(), error.isEmpty());
    QVERIFY(!bytes.contains("private-token")); QVERIFY(!bytes.contains("private-pattern")); QVERIFY(!bytes.contains("/private")); QVERIFY(!bytes.contains(temporary->path().toUtf8())); QVERIFY(!result.contains("requestId"));
    QVERIFY(application.readAllStandardError().isEmpty());
    if (error.isEmpty()) {
      QCOMPARE(result.value("source"), QJsonValue(mode == "missing" ? "initial-defaults" : "saved-storage")); QCOMPARE(result.value("runtimeStatus"), QJsonValue("not-queried"));
      const auto workflows = result.value("workflows").toArray(); QCOMPARE(workflows.size(), 1); const auto workflow = workflows[0].toObject(); QCOMPARE(workflow.value("id"), QJsonValue(profile.id.toString(QUuid::WithoutBraces))); QCOMPARE(workflow.value("definitionAvailable").toBool(), mode != "unavailable");
      if (mode != "missing") QCOMPARE(workflow.value("name"), QJsonValue(profile.name));
    } else { QCOMPARE(result.value("error").toObject().value("code"), QJsonValue(error)); QVERIFY(!result.contains("workflows")); }
    if (mode == "missing") QVERIFY(!QFileInfo::exists(config)); else QCOMPARE(fileBytes(path), original);
    QCoreApplication::processEvents(); QCOMPARE(activations, 0); if (!owner) QVERIFY(!QFileInfo::exists(temporary->filePath("com.xerahs.app")));
    QVERIFY(!QFileInfo::exists(path + ".lock")); QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_STATE_HOME"))); QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_DATA_HOME"))); QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_CACHE_HOME")));
  }
  void workflowHelpDoesNotRequireADisplayOrReadPreferences_data() {
    QTest::addColumn<QStringList>("arguments"); QTest::newRow("plain-help") << QStringList{"--help"}; QTest::newRow("query-help") << QStringList{"--list-workflows", "--non-interactive", "--help"};
  }
  void workflowHelpDoesNotRequireADisplayOrReadPreferences() {
    QFETCH(QStringList, arguments); auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    environment.insert("QT_QPA_PLATFORM", "xcb"); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    QProcess application; application.setProcessEnvironment(environment); application.start(QCoreApplication::applicationDirPath() + "/xerahs", arguments); QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), 0);
    const auto output = application.readAllStandardOutput(); QVERIFY(output.contains("--list-workflows")); QVERIFY(output.contains("--run-workflow")); QVERIFY(application.readAllStandardError().isEmpty());
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) QVERIFY(!QFileInfo::exists(temporary->filePath(key)));
  }
  void workflowDiscoveryRejectsConflictsAndInvalidArgumentsBeforePresentation_data() {
    QTest::addColumn<QStringList>("arguments");
    QTest::newRow("capture") << QStringList{"--list-workflows", "--capture-region"};
    QTest::newRow("edit") << QStringList{"--list-workflows", "--edit", "/private/input"};
    QTest::newRow("workflow") << QStringList{"--list-workflows", "--run-workflow", "private-name"};
    QTest::newRow("unknown") << QStringList{"--list-workflows", "--private-secret", "--json"};
    QTest::newRow("value") << QStringList{"--list-workflows=private-secret", "--non-interactive"};
    QTest::newRow("positional") << QStringList{"--list-workflows", "private-secret"};
  }
  void workflowDiscoveryRejectsConflictsAndInvalidArgumentsBeforePresentation() {
    QFETCH(QStringList, arguments); auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    environment.insert("QT_QPA_PLATFORM", "xcb"); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    QProcess application; application.setProcessEnvironment(environment); application.start(QCoreApplication::applicationDirPath() + "/xerahs", arguments); QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), 1);
    const auto bytes = application.readAllStandardOutput(); QCOMPARE(bytes.count('\n'), 1); const auto result = line(bytes); QCOMPARE(result.value("error").toObject().value("code"), QJsonValue("usage-invalid")); QVERIFY(!result.value("ok").toBool()); QVERIFY(!result.contains("requestId"));
    QVERIFY(!bytes.contains("private")); const auto errors = application.readAllStandardError(); QVERIFY(!errors.contains("private")); QVERIFY(!errors.contains("could not connect to display"));
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) QVERIFY(!QFileInfo::exists(temporary->filePath(key)));
  }
  void actualApplicationForwardsWorkflowIdsAndReportsOnlyDelivery_data() {
    QTest::addColumn<QString>("admission"); QTest::addColumn<int>("exitCode");
    QTest::newRow("accepted") << QString("accept") << 0; QTest::newRow("busy") << QString("busy") << 2;
    QTest::newRow("rejected") << QString("reject") << 1; QTest::newRow("unknown-delivery") << QString("ack-drop") << 1;
  }
  void actualApplicationForwardsWorkflowIdsAndReportsOnlyDelivery() {
    QFETCH(QString, admission); QFETCH(int, exitCode); directory = temporary->filePath("com.xerahs.app"); auto *owner = launch(admission); awaitLine(*owner); QCOMPARE(line(owner->readLine()).value("event"), QJsonValue("primary"));
    auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("XDG_RUNTIME_DIR", temporary->path()); environment.insert("QT_QPA_PLATFORM", "offscreen");
    environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + temporary->filePath("no-host-bus"));
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    const QString workflow = "01234567-89ab-4cde-8123-456789abcdef";
    environment.insert("QT_QPA_PLATFORMTHEME", "");
    QProcess application; application.setProcessEnvironment(environment); application.start(QCoreApplication::applicationDirPath() + "/xerahs", {"--run-workflow", workflow.toUpper(), "--json"});
    QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), exitCode);
    const auto output = application.readAllStandardOutput(); QCOMPARE(output.count('\n'), 1); const auto result = line(output);
    QCOMPARE(result.value("schemaVersion"), QJsonValue(1)); QCOMPARE(result.value("workflowId"), QJsonValue(workflow)); QCOMPARE(result.value("ok").toBool(), exitCode == 0);
    QVERIFY(!result.contains("outputs")); QVERIFY(!result.contains("path")); QVERIFY(!result.contains("name"));
    if (exitCode == 0) { QCOMPARE(result.value("delivery"), QJsonValue("running-instance")); QCOMPARE(result.value("state"), QJsonValue("accepted")); }
    else QCOMPARE(result.value("error").toObject().value("code"), QJsonValue(admission == "busy" ? "activation-busy" : admission == "reject" ? "activation-unavailable" : "activation-delivery-unknown"));
    if (admission == "accept") { awaitLine(*owner); const auto request = line(owner->readLine()).value("request").toObject(); QCOMPARE(request.value("action"), QJsonValue("run-workflow")); QCOMPARE(request.value("workflow_id"), QJsonValue(workflow)); }
    QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_CONFIG_HOME"))); QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_STATE_HOME")));
  }
  void workflowCliRefusesMalformedHeadlessAndMissingProfilesWithoutSideEffects_data() {
    QTest::addColumn<QStringList>("arguments"); QTest::addColumn<QString>("error"); QTest::addColumn<bool>("headless");
    const QString id = "01234567-89ab-4cde-8123-456789abcdef";
    QTest::newRow("invalid-id") << QStringList{"--run-workflow", "private-name", "--json"} << QString("workflow-id-invalid") << false;
    QTest::newRow("conflicting-actions") << QStringList{"--run-workflow", id, "--capture-region", "--json"} << QString("usage-invalid") << false;
    QTest::newRow("unknown-private-argument") << QStringList{"--private-secret", "--json"} << QString("usage-invalid") << false;
    QTest::newRow("non-interactive") << QStringList{"--run-workflow", id, "--non-interactive", "--json"} << QString("presentation-required") << true;
    QTest::newRow("non-interactive-invalid") << QStringList{"--private-secret", "--non-interactive", "--json"} << QString("usage-invalid") << true;
    QTest::newRow("missing-profile") << QStringList{"--run-workflow", id, "--json"} << QString("activation-unavailable") << false;
  }
  void duplicateWorkflowFramesReplayReceiptWithoutDispatchingAgain() {
    NativeInstance instance(directory); QCOMPARE(instance.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    int admitted = 0; instance.setAdmission([&](const ActivationRequest &) { ++admitted; return ActivationReplyState::Accepted; }); QSignalSpy invoked(&instance, &NativeInstance::activationRequested);
    const auto request = makeActivationRequest(ActivationAction::RunWorkflow, {}, "01234567-89ab-4cde-8123-456789abcdef"); const auto frame = serializeActivation(request) + '\n';
    const auto first = exchange(instance, frame); QVERIFY(parseActivationReply(first.trimmed(), request.id)); QTRY_COMPARE(invoked.count(), 1);
    QCOMPARE(exchange(instance, frame), first); QCOMPARE(admitted, 1); QCOMPARE(invoked.count(), 1);
    auto changed = request; changed.workflowId = makeActivationRequest(ActivationAction::Activate).id;
    const auto refused = parseActivationReply(exchange(instance, serializeActivation(changed) + '\n').trimmed(), request.id); QVERIFY(refused); QCOMPARE(refused->state, ActivationReplyState::Rejected); QCOMPARE(admitted, 1); QCOMPARE(invoked.count(), 1);
  }
  void workflowCliRefusesMalformedHeadlessAndMissingProfilesWithoutSideEffects() {
    QFETCH(QStringList, arguments); QFETCH(QString, error); QFETCH(bool, headless);
    auto environment = QProcessEnvironment::systemEnvironment(); environment.insert("XDG_RUNTIME_DIR", temporary->path());
    for (const auto *key : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"}) environment.insert(key, temporary->filePath(key));
    environment.insert("DBUS_SESSION_BUS_ADDRESS", "unix:path=" + temporary->filePath("no-host-bus"));
    environment.insert("QT_QPA_PLATFORM", headless ? "xcb" : "offscreen"); environment.remove("DISPLAY"); environment.remove("WAYLAND_DISPLAY");
    environment.insert("QT_QPA_PLATFORMTHEME", "");
    QProcess application; application.setProcessEnvironment(environment); application.start(QCoreApplication::applicationDirPath() + "/xerahs", arguments);
    QVERIFY(application.waitForFinished(5000)); QCOMPARE(application.exitStatus(), QProcess::NormalExit); QCOMPARE(application.exitCode(), 1);
    const auto output = application.readAllStandardOutput(); QCOMPARE(output.count('\n'), 1); const auto result = line(output); QCOMPARE(result.value("error").toObject().value("code"), QJsonValue(error)); QVERIFY(!result.value("ok").toBool());
    const auto errors = application.readAllStandardError(); QVERIFY(!errors.contains("private-secret")); QVERIFY(!errors.contains("private-name")); QVERIFY(!errors.contains("could not connect to display"));
    QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_CONFIG_HOME"))); QVERIFY(!QFileInfo::exists(temporary->filePath("XDG_STATE_HOME")));
    if (error != "activation-unavailable") QVERIFY(!QFileInfo::exists(temporary->filePath("com.xerahs.app")));
  }
  void runtimeFilesHaveOwnerOnlyAccessAndProfilesAreIsolated() {
    NativeInstance one(directory), two(temporary->filePath("other-profile"));
    QCOMPARE(one.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    QCOMPARE(two.start(makeActivationRequest(ActivationAction::Activate)).kind, InstanceStart::Kind::Primary);
    for (const auto &path : {directory, directory + "/instance.lock", directory + "/instance.sock"}) {
      struct stat node{}; QVERIFY(lstat(QFile::encodeName(path).constData(), &node) == 0); QCOMPARE(node.st_uid, geteuid()); QCOMPARE(node.st_mode & 0077, mode_t(0));
    }
    QVERIFY(one.endpoint() != two.endpoint());
  }
};
QTEST_GUILESS_MAIN(NativeInstanceTest)
#include "NativeInstanceTest.moc"
