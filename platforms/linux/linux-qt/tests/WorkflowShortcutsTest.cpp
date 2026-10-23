// All desktop traffic uses a private, non-activating fixture bus. No host keys,
// preferences, capture or clipboard are read or changed.
#include "WorkflowShortcutActions.h"
#include "ApplicationWindow.h"
#include "CaptureActions.h"
#include "CaptureController.h"
#include "PortalScreenshot.h"
#include "shell/ActivationDispatcher.h"
#include <QApplication>
#include <QDBusVirtualObject>
#include <QDBusError>
#include <QUrl>
#include <QDBusMetaType>
#include <QImage>
#include <QLabel>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
using namespace xerahs::app;
using namespace xerahs::workflows;
namespace {
const QString root = "/org/freedesktop/portal/desktop", service = "org.freedesktop.portal.Desktop";
const QString globalInterface = "org.freedesktop.portal.GlobalShortcuts", requestInterface = "org.freedesktop.portal.Request";
class FakePortal final : public QDBusVirtualObject {
public:
  explicit FakePortal(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerVirtualObject(root, this, QDBusConnection::SubPath)) qFatal("Fixture registration failed");
  }
  ~FakePortal() override { bus.unregisterObject(root, QDBusConnection::UnregisterTree); }
  QString introspect(const QString &) const override { return {}; }
  QDBusConnection bus;
  QString session, request, imagePath, parentWindow;
  QStringList closed; PortalShortcutList offered, bound;
  int creates = 0, binds = 0, screenshots = 0, configurations = 0;
  uint createCode = 0, bindCode = 0;
  bool early = true, autoRespond = true, delayCreateHandle = false, invalidSession = false, invalidHandle = false, malformedBindings = false, holdBind = false;
  QDBusMessage pending; QVariantMap pendingResults;
  void signal(const QString &interface, const QString &member, const QVariantList &args, const QString &path = {}) {
    auto message = QDBusMessage::createSignal(path.isEmpty() ? root : path, interface, member); message.setArguments(args);
    if (!bus.send(message)) qFatal("Fixture signal failed");
  }
  void respond(uint code, const QVariantMap &results) { signal(requestInterface, "Response", {code, results}, request); }
  void finishHandle() { bus.send(pending.createReply(QVariant::fromValue(QDBusObjectPath(request)))); }
  void press(const QString &id, const QString &sessionOverride = {}) { signal(globalInterface, "Activated", {QVariant::fromValue(QDBusObjectPath(sessionOverride.isEmpty() ? session : sessionOverride)), id, QVariant::fromValue(qulonglong(1)), QVariantMap{}}); }
  void release(const QString &id) { signal(globalInterface, "Deactivated", {QVariant::fromValue(QDBusObjectPath(session)), id, QVariant::fromValue(qulonglong(2)), QVariantMap{}}); }
  void changed(const PortalShortcutList &list) { signal(globalInterface, "ShortcutsChanged", {QVariant::fromValue(QDBusObjectPath(session)), QVariant::fromValue(list)}); }
  bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override {
    if (message.interface() == globalInterface && message.member() == "ConfigureShortcuts") {
      ++configurations; bus.send(message.createReply()); return true;
    }
    if (message.member() == "Close") { closed.append(message.path()); bus.send(message.createReply()); return true; }
    if (message.interface() == globalInterface && (message.member() == "CreateSession" || message.member() == "BindShortcuts")) {
      const bool create = message.member() == "CreateSession";
      const auto options = qdbus_cast<QVariantMap>(message.arguments().at(create ? 0 : 3));
      auto sender = message.service().mid(1); sender.replace('.', '_');
      request = root + "/request/" + sender + '/' + options.value("handle_token").toString();
      uint code;
      if (create) {
        ++creates; session = root + "/session/" + sender + '/' + options.value("session_handle_token").toString();
        pendingResults = {{"session_handle", invalidSession ? root + "/session/other/unsafe" : session}}; code = createCode;
      } else {
        ++binds; offered = qdbus_cast<PortalShortcutList>(message.arguments().at(1)); parentWindow = message.arguments().at(2).toString();
        if (bound.isEmpty()) { bound = offered; for (auto &s : bound) s.properties = {{"trigger_description", "Ctrl+Print"}}; }
        pendingResults = {{"shortcuts", malformedBindings ? QVariant("private invalid payload") : QVariant::fromValue(bound)}}; code = bindCode;
      }
      if (autoRespond && early && (create || !holdBind)) respond(code, pendingResults);
      if (create && delayCreateHandle) { pending = message; return true; }
      bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(invalidHandle ? "/" : request))));
      if (autoRespond && !early && (create || !holdBind)) QTimer::singleShot(0, this, [this, code, results = pendingResults] { respond(code, results); });
      return true;
    }
    if (message.interface() == "org.freedesktop.portal.Screenshot" && message.member() == "Screenshot") {
      ++screenshots; auto sender = message.service().mid(1); sender.replace('.', '_');
      const auto options = qdbus_cast<QVariantMap>(message.arguments().at(1));
      const auto handle = root + "/request/" + sender + '/' + options.value("handle_token").toString();
      signal(requestInterface, "Response", {uint(0), QVariantMap{{"uri", QUrl::fromLocalFile(imagePath).toString()}}}, handle);
      bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(handle)))); return true;
    }
    bus.send(message.createErrorReply(QDBusError::UnknownMethod, "Private fixture error")); return true;
  }
};
bool overlayExists() { for (auto *w : QApplication::topLevelWidgets()) if (w->windowTitle() == "XerahS region capture" && w->isVisible()) return true; return false; }
}
class WorkflowShortcutsTest final : public QObject {
  Q_OBJECT
  QDBusConnection server{QString()}, client{QString()}, attacker{QString()};
  WorkflowShortcutPlan plan() { return workflowShortcutPlan(defaultCaptureWorkflows(), CaptureActions::selectableActions()); }
  WorkflowShortcutPortalConfig config(int response = 1000) { return {service, 1000, response}; }
private slots:
  void init() {
    registerPortalShortcutTypes();
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "shortcut-server");
    client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "shortcut-client");
    attacker = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "shortcut-attacker");
    QVERIFY(server.isConnected() && client.isConnected() && attacker.isConnected()); QVERIFY(server.registerService(service));
  }
  void cleanup() {
    server.unregisterService(service);
    for (const auto *name : {"shortcut-server", "shortcut-client", "shortcut-attacker"}) QDBusConnection::disconnectFromBus(name);
  }
  void domainPlanAndHeldKeyAuthority() {
    auto catalog = defaultCaptureWorkflows(); auto unavailable = catalog.entries[0]; unavailable.id = QUuid::createUuid(); unavailable.actions = {"ocr"}; catalog.entries.append(unavailable);
    const auto offered = workflowShortcutPlan(catalog, CaptureActions::selectableActions()); QCOMPARE(offered.entries.size(), 1); QCOMPARE(offered.unavailable, 1);
    const auto id = offered.entries[0].id; WorkflowShortcutState state(offered);
    QVERIFY(state.press(id).isEmpty()); QVERIFY(state.setBound({id})); QCOMPARE(state.press(id), id); QVERIFY(state.press(id).isEmpty());
    QVERIFY(!state.setBound({id, id})); QVERIFY(state.press(id).isEmpty()); state.release(id); QCOMPARE(state.press(id), id);
    QVERIFY(state.setBound({id})); QVERIFY(state.press(id).isEmpty()); QVERIFY(state.setBound({})); QVERIFY(state.press(id).isEmpty());
    QVERIFY(state.setBound({id})); QCOMPARE(state.press(id), id); state.clear(); QVERIFY(state.press(id).isEmpty());
    catalog.entries[1].id = catalog.entries[0].id; QVERIFY(!workflowShortcutPlan(catalog, CaptureActions::selectableActions()).diagnostic.isEmpty());
  }
  void combinedCaptureAndWorkflowPortalPlan() {
    FakePortal desktop(server);
    const auto offered = captureShortcutPlan(defaultCaptureWorkflows(), CaptureActions::selectableActions(), xerahs::settings::HotkeyBindings::defaults());
    PortalWorkflowShortcuts shortcuts(offered, client, config()); QSignalSpy invoked(&shortcuts, &PortalWorkflowShortcuts::activated);
    shortcuts.start(); QTRY_VERIFY(shortcuts.active()); QCOMPARE(desktop.creates, 1); QCOMPARE(desktop.binds, 1);
    QCOMPARE(desktop.offered.size(), offered.entries.size());
    QMap<QString, QString> expected{{"capture-region", "Print"}, {"capture-full-screen", "CTRL+Print"}, {"capture-monitor", "SHIFT+Print"}, {"capture-region-choose-actions", "ALT+Print"}};
    for (const auto &entry : desktop.offered) if (expected.contains(entry.id)) QCOMPARE(entry.properties.value("preferred_trigger").toString(), expected.value(entry.id));
    desktop.press("capture-region"); QTRY_COMPARE(invoked.size(), 1); QCOMPARE(invoked.first().first().toString(), "capture-region");
    QCOMPARE(shortcuts.bindings().value("capture-region"), "Ctrl+Print");
    desktop.release("capture-region"); desktop.press(offered.entries.last().id); QTRY_COMPARE(invoked.size(), 2);
    QCOMPARE(desktop.binds, 1);
    QSignalSpy configured(&shortcuts, &PortalWorkflowShortcuts::configurationResult);
    shortcuts.configureDesktop(); QTRY_COMPARE(desktop.configurations, 1); QTRY_COMPARE(configured.size(), 1);
    QVERIFY(configured.first().first().toBool()); QCOMPARE(desktop.binds, 1); QCOMPARE(desktop.creates, 1);
  }
  void nativeBindingAndActivationAuthority_data() { QTest::addColumn<bool>("early"); QTest::newRow("early-response") << true; QTest::newRow("after-handle") << false; }
  void nativeBindingAndActivationAuthority() {
    QFETCH(bool, early); FakePortal desktop(server); desktop.early = early;
    const auto offered = plan(); const auto id = offered.entries[0].id; PortalWorkflowShortcuts shortcuts(offered, client, config()); QSignalSpy invoked(&shortcuts, &PortalWorkflowShortcuts::activated);
    shortcuts.start("x11:fixture"); QTRY_VERIFY(shortcuts.active()); QCOMPARE(desktop.creates, 1); QCOMPARE(desktop.binds, 1); QCOMPARE(desktop.parentWindow, QString("x11:fixture"));
    QCOMPARE(desktop.offered.size(), 1); QCOMPARE(desktop.offered[0].id, id); QCOMPARE(desktop.offered[0].properties.keys(), QStringList{"description"}); QVERIFY(shortcuts.summary().contains("Ctrl+Print"));
    desktop.press(id); QTRY_COMPARE(invoked.count(), 1); desktop.press(id); desktop.press("private-name"); desktop.press(id, root + "/session/other/forged");
    auto forged = QDBusMessage::createSignal(root, globalInterface, "Activated"); forged.setArguments({QVariant::fromValue(QDBusObjectPath(desktop.session)), id, QVariant::fromValue(qulonglong(1)), QVariantMap{}}); QVERIFY(attacker.send(forged));
    QTest::qWait(20); QCOMPARE(invoked.count(), 1); desktop.release(id); desktop.press(id); QTRY_COMPARE(invoked.count(), 2);
    desktop.changed(desktop.bound); desktop.press(id); QTest::qWait(20); QCOMPARE(invoked.count(), 2);
    desktop.changed({}); QTRY_VERIFY(shortcuts.summary().contains("No workflow")); desktop.release(id); desktop.press(id); QTest::qWait(20); QCOMPARE(invoked.count(), 2);
    shortcuts.stop(); QTRY_VERIFY(desktop.closed.contains(desktop.session)); desktop.press(id); QTest::qWait(10); QCOMPARE(invoked.count(), 2);
  }
  void refusalsAndTimeoutHaveNoActiveBindings_data() {
    QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("diagnostic");
    QTest::newRow("cancel") << QString("cancel") << QString("shortcut-cancelled");
    QTest::newRow("denied") << QString("denied") << QString("shortcut-denied");
    QTest::newRow("session") << QString("session") << QString("shortcut-invalid");
    QTest::newRow("handle") << QString("handle") << QString("shortcut-invalid");
    QTest::newRow("malformed") << QString("malformed") << QString("shortcut-invalid");
    QTest::newRow("unoffered-id") << QString("unoffered-id") << QString("shortcut-invalid");
    QTest::newRow("timeout") << QString("timeout") << QString("shortcut-timeout");
    QTest::newRow("missing-portal") << QString("missing-portal") << QString("shortcut-unavailable");
    QTest::newRow("duplicate-bindings") << QString("duplicate-bindings") << QString("shortcut-invalid");
  }
  void refusalsAndTimeoutHaveNoActiveBindings() {
    QFETCH(QString, mode); QFETCH(QString, diagnostic); FakePortal desktop(server);
    desktop.bindCode = mode == "cancel" ? 1 : mode == "denied" ? 2 : 0; desktop.invalidSession = mode == "session"; desktop.invalidHandle = mode == "handle";
    desktop.malformedBindings = mode == "malformed"; desktop.autoRespond = mode != "timeout";
    if (mode == "missing-portal") QVERIFY(server.unregisterService(service));
    if (mode == "duplicate-bindings") desktop.bound = {{plan().entries[0].id, {}}, {plan().entries[0].id, {}}};
    if (mode == "unoffered-id") desktop.bound = {{QUuid::createUuid().toString(QUuid::WithoutBraces), {}}};
    PortalWorkflowShortcuts shortcuts(plan(), client, config(25)); QSignalSpy invoked(&shortcuts, &PortalWorkflowShortcuts::activated); shortcuts.start();
    QTRY_COMPARE(shortcuts.diagnostic(), diagnostic); QVERIFY(!shortcuts.active()); QVERIFY(!shortcuts.pending()); desktop.press(plan().entries[0].id, desktop.session.isEmpty() ? root + "/session/fixture/absent" : desktop.session); QTest::qWait(10); QCOMPARE(invoked.count(), 0);
    if (desktop.binds) QTRY_VERIFY(desktop.closed.contains(desktop.session));
    if (mode == "timeout") QTRY_VERIFY(desktop.closed.contains(desktop.request));
  }
  void cancellationDuringEarlyCreateClosesLateHandlesAndSession_data() { QTest::addColumn<bool>("early"); QTest::newRow("before-response") << false; QTest::newRow("after-early-response") << true; }
  void cancellationDuringEarlyCreateClosesLateHandlesAndSession() {
    QFETCH(bool, early); FakePortal desktop(server); desktop.delayCreateHandle = true; desktop.early = early;
    auto *shortcuts = new PortalWorkflowShortcuts(plan(), client, config()); QPointer<PortalWorkflowShortcuts> guard(shortcuts); shortcuts->start(); QTRY_COMPARE(desktop.creates, 1);
    QTest::qWait(10); shortcuts->retire(); QVERIFY(!shortcuts->active()); desktop.finishHandle();
    QTRY_VERIFY(desktop.closed.contains(desktop.request)); QTRY_VERIFY(desktop.closed.contains(desktop.session)); QTRY_VERIFY(!guard); QCOMPARE(desktop.binds, 0);
  }
  void cancelPendingBindAndInvalidChangedBindingsCloseSessions() {
    FakePortal desktop(server); desktop.holdBind = true; PortalWorkflowShortcuts shortcuts(plan(), client, config());
    shortcuts.start(); QTRY_COMPARE(desktop.binds, 1); QVERIFY(shortcuts.pending()); shortcuts.stop();
    QTRY_VERIFY(desktop.closed.contains(desktop.request)); QTRY_VERIFY(desktop.closed.contains(desktop.session));
    desktop.holdBind = false; PortalWorkflowShortcuts next(plan(), client, config()); next.start(); QTRY_VERIFY(next.active());
    auto invalid = desktop.bound; invalid.append(invalid.first()); desktop.changed(invalid); QTRY_COMPARE(next.diagnostic(), QString("shortcut-invalid")); QTRY_VERIFY(desktop.closed.contains(desktop.session));
  }
  void ownerReplacementAndClosedSessionFailClosed_data() { QTest::addColumn<bool>("replace"); QTest::newRow("replacement") << true; QTest::newRow("closed") << false; }
  void ownerReplacementAndClosedSessionFailClosed() {
    QFETCH(bool, replace); FakePortal desktop(server); PortalWorkflowShortcuts shortcuts(plan(), client, config()); QSignalSpy invoked(&shortcuts, &PortalWorkflowShortcuts::activated); shortcuts.start(); QTRY_VERIFY(shortcuts.active());
    if (replace) { QVERIFY(server.unregisterService(service)); QVERIFY(attacker.registerService(service)); }
    else desktop.signal("org.freedesktop.portal.Session", "Closed", {QVariantMap{}}, desktop.session);
    QTRY_VERIFY(!shortcuts.active()); QVERIFY(!shortcuts.diagnostic().isEmpty()); desktop.press(plan().entries[0].id); QTest::qWait(20); QCOMPARE(invoked.count(), 0);
    if (replace) attacker.unregisterService(service);
  }
  void sharedNativeActionsEnableDisableInvalidateAndRespectBusy() {
    FakePortal desktop(server); bool busy = true; WorkflowShortcutActions actions([this] { return plan(); }, [&] { return !busy; }, client, config());
    ApplicationWindow window({{"enable-workflow-shortcuts", actions.enableAction()}, {"disable-workflow-shortcuts", actions.disableAction()}});
    connect(&actions, &WorkflowShortcutActions::notify, &window, [&window](const QString &body, bool ok) { window.showResult("Workflow shortcuts", body, ok); });
    QSignalSpy invoked(&actions, &WorkflowShortcutActions::workflowRequested); window.show(); auto *button = window.findChild<QToolButton *>("enable-workflow-shortcuts"); QVERIFY(button); button->setFocus(); QTest::keyClick(button, Qt::Key_Space); QCOMPARE(desktop.creates, 0); QVERIFY(window.findChild<QLabel *>("operationStatus")->text().contains("busy"));
    busy = false; QTest::keyClick(button, Qt::Key_Space); QTRY_VERIFY(actions.enableAction()->text().startsWith("Configure")); QVERIFY(actions.disableAction()->isEnabled());
    desktop.press(plan().entries[0].id); QTRY_COMPARE(invoked.count(), 1); actions.invalidate(); QVERIFY(!actions.disableAction()->isEnabled()); QVERIFY(actions.enableAction()->isEnabled()); QTRY_VERIFY(desktop.closed.contains(desktop.session));
    actions.enableAction()->trigger(); QTRY_VERIFY(actions.enableAction()->text().startsWith("Configure")); QCOMPARE(desktop.creates, 2); actions.disableAction()->trigger(); QVERIFY(!actions.disableAction()->isEnabled());
    QVERIFY(window.findChild<QLabel *>("operationStatus")->text().contains("disabled"));
  }
  void acceptedHotkeyUsesSavedWorkflowAndRealControllerOutput() {
    using namespace xerahs::shell; QTemporaryDir dir; FakePortal desktop(server);
    desktop.imagePath = dir.filePath("synthetic.png"); QImage image(QApplication::primaryScreen()->geometry().size(), QImage::Format_ARGB32); image.fill(QColor(40, 80, 120)); QVERIFY(image.save(desktop.imagePath));
    xerahs::editor::FilePreferenceStorage storage(dir.filePath("workflows.json")); CaptureWorkflowStore store(storage); auto catalog = *store.current().catalog;
    catalog.entries[0].actions = {"save"}; catalog.entries[0].output.directory = dir.filePath("configured"); catalog.entries[0].output.pattern = "hotkey-%width"; QVERIFY(store.save(catalog, store.revision()).saved);
    const auto id = catalog.entries[0].id.toString(QUuid::WithoutBraces);
    CaptureController capture(dir.filePath("fallback"), nullptr, [&](QObject *parent) { return new PortalScreenshot(client, {}, parent); });
    WorkflowShortcutActions shortcuts([&] { return workflowShortcutPlan(*store.current().catalog, CaptureActions::selectableActions()); }, [&] { return !capture.busy(); }, client, config());
    ActivationOperations operations; operations.captureBusy = [&] { return capture.busy(); }; operations.interactionBlocked = [&] { return shortcuts.pending(); };
    operations.prepareWorkflow = [&](const QString &requested) -> std::function<void()> { const auto choice = store.resolve(QUuid(requested), CaptureActions::selectableActions()); if (!choice.workflow) return {}; return [&, snapshot = *choice.workflow] { capture.captureWorkflow(snapshot); }; };
    ActivationDispatcher dispatcher(operations); int admissions = 0;
    connect(&shortcuts, &WorkflowShortcutActions::workflowRequested, &capture, [&](const QString &workflow) { const auto request = makeActivationRequest(ActivationAction::RunWorkflow, {}, workflow); if (dispatcher.admit(request) == ActivationReplyState::Accepted) { ++admissions; QVERIFY(dispatcher.dispatch(request).isEmpty()); } });
    shortcuts.enableAction()->trigger(); QTRY_VERIFY(shortcuts.enableAction()->text().startsWith("Configure")); desktop.press(id); QTRY_VERIFY(overlayExists());
    desktop.release(id); desktop.press(id); QTest::qWait(10); QCOMPARE(admissions, 1); QCOMPARE(desktop.screenshots, 1);
    capture.pointerDown({10, 10}); capture.pointerUp({40, 35}); QTRY_VERIFY(!capture.busy()); const auto saved = capture.lastPipelineResult(); QVERIFY(saved && saved->file);
    QCOMPARE(saved->file->path, dir.filePath("configured/hotkey-30.png")); QCOMPARE(QImage(saved->file->path).pixelColor(2, 2), QColor(40, 80, 120));
    shortcuts.disableAction()->trigger(); desktop.release(id); desktop.press(id); QTest::qWait(10); QCOMPARE(admissions, 1);
  }
};
QTEST_MAIN(WorkflowShortcutsTest)
#include "WorkflowShortcutsTest.moc"
