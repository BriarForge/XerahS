#include "settings/ConfigurationTransfer.h"
#include "settings/ApplicationSettings.h"
#include "image-editor/EditorPreferenceStore.h"
#include "uploads/DestinationCatalog.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QProcess>
#include <QLockFile>
#include <cstdlib>
#include <QtTest>
using namespace xerahs;
namespace {
QByteArray get(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
settings::ConfigurationPaths paths(const QTemporaryDir &d) { return {d.filePath("app.json"), d.filePath("editor.json"), d.filePath("workflows.json"), d.filePath("destinations.json"), d.path()}; }
QByteArray changed(settings::ConfigurationTransfer &t) {
  auto root = t.exportPlan().candidate; auto stores = root["stores"].toObject(); auto app = stores["application"].toObject(); auto v = app["values"].toObject(); v.insert("showTrayIcon", false); app.insert("values", v); stores.insert("application", app); root.insert("stores", stores); return QJsonDocument(root).toJson();
}
}
class ConfigurationTransferTest : public QObject {
  Q_OBJECT
private slots:
  void compositorCaptureOptInStaysLocalDuringTransfer() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer transfer(p);
    const auto key = QStringLiteral("regionCapture.includeCursor");
    settings::ApplicationSettingsStore local(p.application, d.path());
    auto values = local.current().values; values.insert(key, true); QVERIFY(local.save(values).isEmpty());
    auto exported = transfer.exportPlan(); QVERIFY2(exported.valid(), qPrintable(exported.code));
    QVERIFY(exported.exportReport.omissions.contains("compositor-capture-opt-in"));
    auto stores = exported.candidate["stores"].toObject(); auto app = stores["application"].toObject();
    QVERIFY(!app["values"].toObject().contains(key));
    QTemporaryDir receiverDir; const auto receiverPaths = paths(receiverDir); settings::ConfigurationTransfer receiver(receiverPaths);
    auto plan = receiver.inspect(QJsonDocument(exported.candidate).toJson()); QVERIFY2(plan.valid(), qPrintable(plan.code));
    QVERIFY(receiver.apply(plan, true).isEmpty());
    settings::ApplicationSettingsStore received(receiverPaths.application, receiverDir.path());
    QVERIFY(!settings::regionSessionSettings(received.current().values).includeCursor);
    auto forged = app["values"].toObject(); forged.insert(key, true); app.insert("values", forged);
    stores.insert("application", app); exported.candidate.insert("stores", stores);
    plan = receiver.inspect(QJsonDocument(exported.candidate).toJson());
    QCOMPARE(plan.code, QString("application.regionCapture.includeCursor: configuration-authority-not-portable"));
    auto retained = transfer.inspect(QJsonDocument(transfer.exportPlan().candidate).toJson());
    QVERIFY2(retained.valid(), qPrintable(retained.code)); QVERIFY(transfer.apply(retained, true).isEmpty());
    settings::ApplicationSettingsStore reopened(p.application, d.path());
    QVERIFY(settings::regionSessionSettings(reopened.current().values).includeCursor);
  }

  void recoveryWithoutPendingImportCreatesNoProfileFiles() {
    QTemporaryDir dir; const auto root = dir.filePath("missing-profile");
    settings::ConfigurationTransfer transfer({root + "/app.json", root + "/editor.json", root + "/workflows.json", root + "/destinations.json", root});
    QVERIFY(transfer.recover().isEmpty()); QVERIFY(!QFileInfo::exists(root));
  }

  void exportLockFailureKeepsPreviousArtifactAndUnknownInputIsBounded() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p); const auto path = d.filePath("export.json");
    QVERIFY(t.exportFile(path).isEmpty()); const auto before = get(path); QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock());
    QVERIFY(!t.exportFile(path).isEmpty()); QCOMPARE(get(path), before);
    auto bytes = QByteArray("{\"format\":\"com.xerahs.app/configuration\",\"version\":1,\"version\":1}");
    QVERIFY(!t.inspect(bytes).valid()); QVERIFY(!t.inspect(QByteArray(65, '[') + "0" + QByteArray(65, ']')).valid());
    QVERIFY(!t.inspect(QByteArray(8 * 1024 * 1024 + 1, ' ')).valid());
  }

  void stableSemanticIdsAndExplicitWorkflowUpgrade() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    auto root = t.exportPlan().candidate; auto stores = root["stores"].toObject(); auto workflows = stores["workflows"].toObject();
    const auto id = workflows["defaultId"]; auto array = workflows["workflows"].toArray(); auto workflow = array[0].toObject();
    QCOMPARE(workflow["capture"], QJsonValue("region")); QVERIFY(workflow["actions"].toArray().contains("save"));
    workflow.remove("output"); array[0] = workflow; workflows.insert("workflows", array); workflows.insert("version", 1); stores.insert("workflows", workflows); root.insert("stores", stores);
    auto plan = t.inspect(QJsonDocument(root).toJson()); QVERIFY(plan.valid()); QVERIFY(plan.report.count(compatibility::ImportDisposition::Transformed));
    QVERIFY(t.apply(plan, true).isEmpty());
    const auto saved = QJsonDocument::fromJson(get(p.workflows)).object(); QCOMPARE(saved["version"], QJsonValue(2)); QCOMPARE(saved["defaultId"], id);
    workflow.insert("output", QJsonObject{{"directory", ""}, {"pattern", "example"}}); array[0] = workflow; workflows.insert("workflows", array);
    stores.insert("workflows", workflows); root.insert("stores", stores); QVERIFY(!t.inspect(QJsonDocument(root).toJson()).valid()); // v1 extension must not gain authority
  }
  void destinationValueReferenceAndCollectionPolicy_data() {
    QTest::addColumn<QString>("kind");
    for (const auto *k : {"runtime-method", "runtime-body", "duplicate-id", "missing-default", "invalid-fields", "too-many", "newer-version"}) QTest::newRow(k) << QString(k);
  }
  void destinationValueReferenceAndCollectionPolicy() {
    QFETCH(QString, kind); QTemporaryDir d; auto p = paths(d); uploads::DestinationCatalog catalog(p.destinations);
    uploads::UploadDestination u; u.id = QUuid::createUuid(); u.name = "Destination"; u.requestUrl = "https://example.com/upload"; QVERIFY(catalog.add(u).isEmpty());
    settings::ConfigurationTransfer t(p); auto root = t.exportPlan().candidate; auto stores = root["stores"].toObject(); auto dest = stores["destinations"].toObject(); auto entries = dest["destinations"].toArray(); auto entry = entries[0].toObject();
    if (kind == "runtime-method") entry.insert("method", "XerahS.Uploaders.Post");
    if (kind == "runtime-body") entry.insert("body", "System.Net.MultipartFormData");
    if (kind == "invalid-fields") entry.insert("headers", QJsonObject{{"unexpected", true}});
    if (kind == "newer-version") entry.insert("schemaVersion", 2);
    entries[0] = entry;
    if (kind == "duplicate-id") entries.append(entry);
    if (kind == "too-many") for (int n = 0; n < 256; ++n) entries.append(entry);
    dest.insert("destinations", entries); if (kind == "missing-default") dest.insert("default", QUuid::createUuid().toString(QUuid::WithoutBraces));
    stores.insert("destinations", dest); root.insert("stores", stores); const auto before = get(p.destinations);
    auto plan = t.inspect(QJsonDocument(root).toJson()); QVERIFY(!plan.valid()); QVERIFY(!t.apply(plan, true).isEmpty()); QCOMPARE(get(p.destinations), before); QVERIFY(!QFile::exists(p.application));
  }

  void interruptedPublicationRecoversExactOriginalFiles() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    QVERIFY(t.apply(t.inspect(QJsonDocument(t.exportPlan().candidate).toJson()), true).isEmpty());
    const QList<QByteArray> before{get(p.application), get(p.editor), get(p.workflows), get(p.destinations)};
    QProcess child; child.start(QCoreApplication::applicationFilePath(), {"--interrupt-import", d.path()}); QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitCode(), 71);
    QVERIFY(QFile::exists(p.application + ".import-recovery")); QVERIFY(get(p.application) != before[0]);
    QVERIFY(t.recover().isEmpty());
    QCOMPARE((QList<QByteArray>{get(p.application), get(p.editor), get(p.workflows), get(p.destinations)}), before);
    QVERIFY(t.recover().isEmpty());
  }
  void safeExtensionTransformsAndVersionPolicy() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    auto root = t.exportPlan().candidate; auto stores = root["stores"].toObject(); auto app = stores["application"].toObject(); auto values = app["values"].toObject();
    auto hotkeys = values["hotkeys"].toObject(); hotkeys.insert("capture-region", "Shift+Ctrl+F1"); values.insert("hotkeys", hotkeys);
    values.insert("FutureLabel", QJsonObject{{"note", "safe-extension"}}); app.insert("values", values); stores.insert("application", app); root.insert("stores", stores);
    auto plan = t.inspect(QJsonDocument(root).toJson()); QVERIFY2(plan.valid(), qPrintable(plan.code)); QVERIFY(plan.report.count(compatibility::ImportDisposition::Transformed) > 0);
    QVERIFY(t.apply(plan, true).isEmpty()); const auto exported = t.exportPlan(); QVERIFY(QJsonDocument(exported.candidate).toJson().contains("safe-extension"));
    root.insert("version", 2); QVERIFY(!t.inspect(QJsonDocument(root).toJson()).valid()); root.insert("version", 0); QVERIFY(!t.inspect(QJsonDocument(root).toJson()).valid());
  }

  void explicitVersionTargetAtomicExportAndOmissions() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    auto plan = t.exportPlan(); QVERIFY2(plan.valid(), qPrintable(plan.code));
    QCOMPARE(plan.candidate["version"], QJsonValue(1)); QCOMPARE(plan.candidate["target"], QJsonValue("com.xerahs.app"));
    QVERIFY(plan.exportReport.omissions.contains("credential-service-secrets"));
    compatibility::ExportReport report; QVERIFY(t.exportFile(d.filePath("export.json"), &report).isEmpty());
    QVERIFY(t.inspect(get(d.filePath("export.json"))).valid()); QVERIFY(!QFile::exists(p.application));
    QVERIFY(!t.exportFile(p.application).isEmpty()); QVERIFY(!t.exportFile(d.path()).isEmpty());
  }
  void previewCancelConfirmAndRestartUseProductionStores() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    auto plan = t.inspect(changed(t)); QVERIFY2(plan.valid(), qPrintable(plan.code));
    QVERIFY(plan.preview().contains("retained")); QVERIFY(plan.preview().contains("secret"));
    QCOMPARE(t.apply(plan, false), QString("configuration-cancelled")); QVERIFY(!QFile::exists(p.application));
    const auto result = t.apply(plan, true); QVERIFY2(result.isEmpty(), qPrintable(result));
    settings::ApplicationSettingsStore app(p.application, d.path()); QVERIFY(!app.current().values["showTrayIcon"].toBool());
    editor::FilePreferenceStorage es(p.editor), ws(p.workflows); editor::EditorPreferenceStore editor(es); workflows::CaptureWorkflowStore workflow(ws);
    QVERIFY(!editor.preferences().readOnly); QVERIFY(workflow.current().catalog); uploads::DestinationCatalog dest(p.destinations); QVERIFY(dest.writable());
  }
  void secondStoreFailureLeavesEveryOriginalByteUnchanged_data() {
    QTest::addColumn<QString>("phase"); QTest::addColumn<bool>("existing");
    for (auto phase : {QString("prepare"), QString("publish")}) for (bool existing : {false, true})
      QTest::newRow(qPrintable(phase + (existing ? "-existing" : "-missing"))) << phase << existing;
  }
  void secondStoreFailureLeavesEveryOriginalByteUnchanged() {
    QFETCH(QString, phase); QFETCH(bool, existing); QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    if (existing) QVERIFY(t.apply(t.inspect(QJsonDocument(t.exportPlan().candidate).toJson()), true).isEmpty());
    const auto before = QList<QByteArray>{get(p.application), get(p.editor), get(p.workflows), get(p.destinations)};
    const auto plan = t.inspect(changed(t)); QVERIFY(plan.valid());
    const auto code = t.apply(plan, true, [&](const QString &at, int i) { return at == phase && i == 1; });
    QVERIFY2(code.startsWith("editor:"), qPrintable(code));
    QCOMPARE((QList<QByteArray>{get(p.application), get(p.editor), get(p.workflows), get(p.destinations)}), before);
    QVERIFY(!QFile::exists(p.application + ".import-recovery"));
  }
  void stalePreviewDoesNotOverwriteLiveSettings() {
    QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p); auto plan = t.inspect(changed(t));
    settings::ApplicationSettingsStore app(p.application, d.path()); auto v = app.current().values; v.insert("playCaptureSound", true); QVERIFY(app.save(v).isEmpty());
    const auto before = get(p.application); QVERIFY(t.apply(plan, true).contains("configuration-changed")); QCOMPARE(get(p.application), before);
  }
  void rejectsMalformedFamiliesBeforeMutation_data() {
    QTest::addColumn<QString>("store"); QTest::addColumn<QString>("key"); QTest::addColumn<QJsonValue>("value");
    QTest::newRow("enum") << QString("application") << QString("delayedCapture.source") << QJsonValue("System.Runtime.Type");
    QTest::newRow("path") << QString("application") << QString("saveFolder") << QJsonValue("/tmp/../private");
    QTest::newRow("colour") << QString("editor") << QString("BorderColorHex") << QJsonValue("private-invalid");
    QTest::newRow("geometry") << QString("editor") << QString("WindowWidth") << QJsonValue(-1);
    QTest::newRow("collection") << QString("workflows") << QString("workflows") << QJsonValue(QJsonArray{});
    QTest::newRow("reference") << QString("workflows") << QString("defaultId") << QJsonValue("00000000-0000-4000-8000-000000000001");
    QTest::newRow("secret") << QString("application") << QString("secretToken") << QJsonValue("private-secret");
  }
  void rejectsMalformedFamiliesBeforeMutation() {
    QFETCH(QString, store); QFETCH(QString, key); QFETCH(QJsonValue, value); QTemporaryDir d; auto p = paths(d); settings::ConfigurationTransfer t(p);
    auto root = t.exportPlan().candidate; auto stores = root["stores"].toObject(); auto doc = stores[store].toObject();
    if (store == "workflows") doc.insert(key, value);
    else { const auto member = store == "editor" ? "current" : "values"; auto obj = doc[member].toObject(); obj.insert(key, value); doc.insert(member, obj); }
    stores.insert(store, doc); root.insert("stores", stores); auto plan = t.inspect(QJsonDocument(root).toJson());
    QVERIFY(!plan.valid()); QVERIFY(!plan.preview().contains("private-secret")); QVERIFY(!t.apply(plan, true).isEmpty()); QVERIFY(!QFile::exists(p.application));
  }
  void destinationSecretsAreOmittedAndListed() {
    QTemporaryDir d; auto p = paths(d); uploads::DestinationCatalog dest(p.destinations);
    uploads::UploadDestination u; u.id = QUuid::createUuid(); u.name = "Upload"; u.requestUrl = "https://example.com/upload?apiKey=private-secret"; u.headers = {{"Authorization", "private-secret"}};
    QVERIFY(dest.add(u).isEmpty()); settings::ConfigurationTransfer t(p); auto plan = t.exportPlan();
    QVERIFY2(plan.valid(), qPrintable(plan.code)); const auto bytes = QJsonDocument(plan.candidate).toJson();
    QVERIFY(!bytes.contains("private-secret")); QVERIFY(plan.preview().contains("secret omitted")); QVERIFY(plan.exportReport.omissions.contains("destination-secret-fields"));
    const auto inspected = t.inspect(bytes); QVERIFY(inspected.valid());
    QVERIFY(inspected.preview().contains("destinations[0]: secret references retained"));
    QVERIFY(inspected.exportReport.omissions.contains("destination-secret-fields"));
  }
};
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() == 3 && app.arguments()[1] == "--interrupt-import") {
    const QDir d(app.arguments()[2]);
    settings::ConfigurationTransfer t({d.filePath("app.json"), d.filePath("editor.json"), d.filePath("workflows.json"), d.filePath("destinations.json"), d.path()});
    t.apply(t.inspect(changed(t)), true, [](const QString &phase, int index) { if (phase == "publish" && index == 1) std::_Exit(71); return false; });
    return 72;
  }
  ConfigurationTransferTest test; return QTest::qExec(&test, argc, argv);
}
#include "ConfigurationTransferTest.moc"
