#include "DestinationConnectionTest.h"
#include "UploadDestinationsDialog.h"
#include <QApplication>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <atomic>
using namespace xerahs::app;
using namespace xerahs::uploads;
namespace {
class CountingCredentials final : public CredentialStore {
public:
  MemoryCredentialStore inner;
  mutable int interactiveReads = 0, probeReads = 0, availabilityChecks = 0;
  CredentialResult store(const QUuid &id,const QString &name,const QString &value) override { return inner.store(id,name,value); }
  CredentialResult lookup(const QUuid &id,const QString &name) const override { ++interactiveReads; return inner.lookup(id,name); }
  CredentialStatus availability(const QUuid &id,const QString &name) const override { ++availabilityChecks; return inner.availability(id,name); }
  CredentialResult lookupWithoutPrompt(const QUuid &id,const QString &name) const override { ++probeReads; return inner.lookup(id,name); }
  CredentialResult remove(const QUuid &id,const QString &name) override { return inner.remove(id,name); }
  CredentialResult removeAll(const QUuid &id) override { return inner.removeAll(id); }
};
struct CredentialGate { std::atomic_bool entered{false}, release{false}; };
class BlockingCredentials final : public CredentialStore {
public:
  explicit BlockingCredentials(std::shared_ptr<CredentialGate> gate) : gate(std::move(gate)) {}
  std::shared_ptr<CredentialGate> gate;
  CredentialResult store(const QUuid &,const QString &,const QString &) override { return {CredentialStatus::Denied,{}}; }
  CredentialResult lookup(const QUuid &,const QString &) const override { return {CredentialStatus::Denied,{}}; }
  CredentialStatus availability(const QUuid &,const QString &) const override { return CredentialStatus::Ok; }
  CredentialResult lookupWithoutPrompt(const QUuid &,const QString &) const override {
    gate->entered.store(true); while (!gate->release.load()) QThread::msleep(1);
    return {CredentialStatus::Ok,"credential-private-value"};
  }
  CredentialResult remove(const QUuid &,const QString &) override { return {CredentialStatus::Denied,{}}; }
  CredentialResult removeAll(const QUuid &) override { return {CredentialStatus::Denied,{}}; }
};
class Reply final : public QNetworkReply {
public:
  Reply(const QNetworkRequest &request, QString mode, int *aborts, QObject *parent) : QNetworkReply(parent), m_aborts(aborts) {
    setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
    if (mode == "hang") return;
    QTimer::singleShot(0, this, [this, mode] {
      if (isFinished()) return;
      setAttribute(QNetworkRequest::HttpStatusCodeAttribute, mode == "redirect" ? 302 : mode == "unsupported" ? 405 : mode == "http" ? 403 : 204);
      if (mode == "network") setError(RemoteHostClosedError, "server echoed credential-private-value and /private/capture.png");
      if (mode == "http") setError(ContentAccessDenied, "server echoed credential-private-value and /private/capture.png");
      setFinished(true); emit finished();
    });
  }
  void abort() override { if (!isFinished()) { ++*m_aborts; setFinished(true); emit finished(); } }
protected:
  qint64 readData(char *, qint64) override { return -1; }
private:
  int *m_aborts;
};
class Network final : public QNetworkAccessManager {
public:
  int calls = 0, aborts = 0; QString mode = "success";
  Operation operation = UnknownOperation; QNetworkRequest last; bool hadBody = false;
protected:
  QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *body) override {
    ++calls; operation = op; last = request; hadBody = body && !body->atEnd();
    return new Reply(request, mode, &aborts, this);
  }
};
UploadDestination destination() {
  UploadDestination d; d.id = QUuid::createUuid(); d.name = "Test destination";
  d.requestUrl = "https://example.invalid/probe"; d.body = BodyType::Binary;
  d.headers = {{"Authorization", "{secret:Authorization}"}};
  d.arguments = {{"ignored", "{secret:body-only}"}};
  d.data = "private body must not be sent"; return d;
}
UploadDestinationsPorts ports(Network &network, int &creations) {
  UploadDestinationsPorts p; p.report = [](QWidget *, const QString &) {};
  p.connectionNetwork = [&](QObject *) { ++creations; return &network; }; return p;
}
QString status(UploadDestinationsDialog &dialog) { return dialog.findChild<QLabel *>("destinationConnectionStatus")->text(); }
}
class DestinationConnectionTestTest : public QObject {
  Q_OBJECT
private slots:
  void viewingAndValidationAreLocalAndClickSendsOnlyScopedHead() {
    QTemporaryDir dir; const auto d = destination();
    DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(d).isEmpty());
    CountingCredentials credentials; QVERIFY(credentials.store(d.id, "Authorization", "credential-private-value").ok());
    Network network; int creations = 0;
    UploadDestinationsDialog dialog(catalog, credentials, ports(network, creations)); dialog.show();
    QTRY_VERIFY(!dialog.busy());
    QCOMPARE(credentials.interactiveReads, 0); QCOMPARE(credentials.probeReads, 0); QVERIFY(credentials.availabilityChecks > 0);
    DestinationCatalog reloaded(dir.filePath("destinations.json")); QVERIFY(validateDestination(*reloaded.find(d.id)).isEmpty());
    const auto prepared = buildConnectionTestRequest(d, credentials); QVERIFY(prepared.request);
    QCOMPARE(network.calls, 0); QCOMPARE(creations, 0); QVERIFY(prepared.request->body.isEmpty());
    dialog.findChild<QPushButton *>("testDestinationConnection")->click();
    QTRY_VERIFY(status(dialog).startsWith("connection-test-succeeded:"));
    QCOMPARE(credentials.interactiveReads, 0); QCOMPARE(credentials.probeReads, 2);
    QCOMPARE(creations, 1); QCOMPARE(network.calls, 1); QCOMPARE(network.operation, QNetworkAccessManager::HeadOperation);
    QVERIFY(!network.hadBody); QCOMPARE(network.last.rawHeader("Authorization"), QByteArray("credential-private-value"));
    QCOMPARE(network.last.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(), int(QNetworkRequest::ManualRedirectPolicy));
    QCOMPARE(network.last.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(), int(QNetworkRequest::Manual));
    QVERIFY(!status(dialog).contains("credential-private-value")); QVERIFY(status(dialog).contains("Upload permission is not verified"));
  }
  void failuresHaveValueFreeDiagnostics_data() {
    QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("code");
    QTest::newRow("network") << QString("network") << QString("connection-test-network-failed");
    QTest::newRow("http") << QString("http") << QString("connection-test-http-failed");
    QTest::newRow("redirect") << QString("redirect") << QString("connection-test-redirect-refused");
    QTest::newRow("unsupported") << QString("unsupported") << QString("connection-test-unsupported");
  }
  void failuresHaveValueFreeDiagnostics() {
    QFETCH(QString, mode); QFETCH(QString, code);
    QTemporaryDir dir; const auto d = destination(); DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(d).isEmpty());
    MemoryCredentialStore credentials; credentials.store(d.id, "Authorization", "credential-private-value");
    Network network; network.mode = mode; int creations = 0;
    UploadDestinationsDialog dialog(catalog, credentials, ports(network, creations)); QTRY_VERIFY(!dialog.busy());
    dialog.testConnection(); QTRY_VERIFY(status(dialog).startsWith(code + ":")); QCOMPARE(network.calls, 1);
    QVERIFY(!status(dialog).contains("credential-private-value")); QVERIFY(!status(dialog).contains("/private"));
    QVERIFY(!status(dialog).contains("example.invalid")); QVERIFY(!dialog.busy());
  }
  void cancelButtonEscapeCloseAndPreparationCancelStopFurtherNetwork() {
    QTemporaryDir dir; const auto d = destination(); DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(d).isEmpty());
    MemoryCredentialStore credentials; credentials.store(d.id, "Authorization", "credential-private-value");
    Network network; network.mode = "hang"; int creations = 0;
    UploadDestinationsDialog dialog(catalog, credentials, ports(network, creations)); QTRY_VERIFY(!dialog.busy());
    dialog.testConnection(); dialog.cancelConnectionTest(); QTest::qWait(30); QCOMPARE(network.calls, 0);
    for (int i = 0; i < 3; ++i) {
      dialog.show(); dialog.testConnection(); QTRY_COMPARE(network.calls, i + 1); QVERIFY(dialog.busy());
      if (i == 0) dialog.findChild<QPushButton *>("cancelDestinationConnection")->click();
      if (i == 1) QTest::keyClick(&dialog, Qt::Key_Escape);
      if (i == 2) dialog.close();
      QCOMPARE(network.aborts, i + 1); QVERIFY(!dialog.busy()); QVERIFY(status(dialog).startsWith("connection-test-cancelled:"));
    }
  }
  void invalidEndpointsInputAndOtherDestinationSecretsNeverReachNetwork() {
    MemoryCredentialStore credentials; auto d = destination();
    credentials.store(QUuid::createUuid(), "Authorization", "other-account-secret");
    QVERIFY(!buildConnectionTestRequest(d, credentials).request);
    d.headers.clear(); d.requestUrl = "file:///private/capture.png"; QVERIFY(!buildConnectionTestRequest(d, credentials).request);
    d.requestUrl = "https://user:password@example.invalid/"; QVERIFY(!buildConnectionTestRequest(d, credentials).request);
    d.requestUrl = "https://example.invalid/{filename}"; QCOMPARE(buildConnectionTestRequest(d, credentials).code, QString("connection-test-input-required"));
    QTemporaryDir dir; DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(d).isEmpty());
    Network network; int creations = 0; UploadDestinationsDialog dialog(catalog, credentials, ports(network, creations));
    QTRY_VERIFY(!dialog.busy()); dialog.testConnection(); QTRY_VERIFY(!dialog.busy());
    QVERIFY(status(dialog).startsWith("connection-test-configuration-failed:")); QCOMPARE(network.calls, 0); QCOMPARE(creations, 0);
  }
  void closedPreparationKeepsCredentialOwnerAliveAndNeverStartsLateNetwork() {
    QTemporaryDir dir; DestinationCatalog catalog(dir.filePath("destinations.json")); QVERIFY(catalog.add(destination()).isEmpty());
    auto gate=std::make_shared<CredentialGate>();
    struct Release { std::shared_ptr<CredentialGate> gate; ~Release() { gate->release.store(true); } } release{gate};
    auto credentials=std::make_shared<BlockingCredentials>(gate); std::weak_ptr<CredentialStore> alive=credentials;
    Network network; int creations=0; auto p=ports(network,creations); p.credentialLifetime=credentials;
    auto *dialog=new UploadDestinationsDialog(catalog,*credentials,std::move(p)); QTRY_VERIFY(!dialog->busy());
    dialog->testConnection(); QTRY_VERIFY(gate->entered.load()); delete dialog; credentials.reset();
    QVERIFY(!alive.expired()); QCOMPARE(network.calls,0); gate->release.store(true);
    QTRY_VERIFY(alive.expired()); QCOMPARE(network.calls,0); QCOMPARE(creations,0);
  }
  void timeoutAndOwnerDestructionAbortExactlyOnce() {
    Network network; network.mode = "hang";
    PreparedUploadRequest request; request.url = QUrl("https://example.invalid/probe");
    DestinationConnectionTest test(&network, nullptr, 10); QSignalSpy done(&test, &DestinationConnectionTest::finished);
    test.start(request); QTRY_COMPARE(done.size(), 1); QCOMPARE(done[0][0].toString(), QString("connection-test-timeout"));
    QCOMPARE(network.aborts, 1); test.cancel(); QCOMPARE(done.size(), 1);
    auto *owned = new DestinationConnectionTest(&network); owned->start(request); delete owned;
    QCOMPARE(network.aborts, 2);
  }
};
QTEST_MAIN(DestinationConnectionTestTest)
#include "DestinationConnectionTestTest.moc"
