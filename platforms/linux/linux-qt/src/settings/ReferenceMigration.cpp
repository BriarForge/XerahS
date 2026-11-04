#include "ReferenceMigration.h"
#include "ApplicationSettings.h"
#include "storage/PrivateJsonFile.h"
#include "storage/JsonBoundary.h"
#include "uploads/SxcuImport.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSet>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace xerahs::settings {
namespace {
using S = storage::StoreStatus;
using D = compatibility::ImportDisposition;
const QStringList ids{"application", "editor", "workflows", "destinations"};
const QStringList names{"ApplicationConfig.json", "WorkflowsConfig.json", "UploadersConfig.json", "uploader-instances.json"};
const QUuid migrationNamespace("60610e2c-c689-5f6a-a107-9e426965227a");
storage::PrivateJsonFile record(const ConfigurationPaths &p) {
  return {p.application + ".reference-migration", {"com.xerahs.app/reference-migration", 1, 64 * 1024 * 1024}};
}
QJsonArray array(const QJsonValue &v) {
  // Newtonsoft TypeNameHandling.Auto may wrap a list in $values. No type name
  // is ever resolved, loaded or used as a semantic ID.
  return v.isArray() ? v.toArray() : v.toObject().value("$values").toArray();
}
void report(ConfigurationPlan &p, const QString &field, D d) {
  p.report.add(d); p.fields.append(field + ": " + QStringList{"retained", "transformed", "defaulted", "quarantined", "rejected", "ignored"}.at(static_cast<int>(d)));
}
QString trigger(const QJsonObject &info) {
  auto key = info.value("Key").toString();
  if (key == "PrintScreen") key = "Print";
  if (key.isEmpty() || key == "None") return {};
  const auto mods = info.value("Modifiers").toString(); QString prefix;
  if (mods != "None" && !mods.isEmpty()) for (const auto &m : mods.split(',', Qt::SkipEmptyParts)) {
    const auto name = m.trimmed();
    if (name == "Control") prefix += "Ctrl+";
    else if (name == "Alt" || name == "Shift" || name == "Meta") prefix += name + "+";
    else return "unsupported";
  }
  return normalizeTrigger(prefix + key);
}
QJsonObject encoded(const QMap<QString, QByteArray> &snapshots) {
  QJsonObject out; for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) out.insert(it.key(), QString::fromLatin1(it.value().toBase64())); return out;
}
QMap<QString, QByteArray> decoded(const QJsonObject &snapshots) {
  QMap<QString, QByteArray> out;
  for (const auto &id : ids) {
    if (!snapshots.value(id).isString()) return {};
    const auto result = QByteArray::fromBase64Encoding(snapshots.value(id).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result) return {};
    out.insert(id, result.decoded);
  }
  return out;
}
}
QString ReferenceMigration::defaultSourceFolder() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath("xerahs");
}
ReferenceMigrationPlan ReferenceMigration::inspectFolder(const QString &folder) const {
  ReferenceMigrationPlan result;
  ConfigurationTransfer transfer(m_paths); auto exported = transfer.exportPlan();
  if (!exported.valid()) { result.configuration = exported; return result; }
  result.configuration = transfer.inspect(QJsonDocument(exported.candidate).toJson());
  auto &p = result.configuration; if (!p.valid()) return result;
  const auto fail = [&](const QString &code) { p.code = code; };
  // Read source data only from explicitly selected regular files. Do not follow
  // custom-path settings, symlinks, SecretsStore, history databases or plugins.
  QCryptographicHash hash(QCryptographicHash::Sha256); QMap<QString, QJsonObject> documents;
  bool found = false;
  for (const auto &name : names) {
    const auto path = QDir(folder).filePath(name); const QFileInfo info(path);
    hash.addData(name.toUtf8()); hash.addData(QByteArray(1, '\0'));
    if (!info.exists() && !info.isSymLink()) { hash.addData("missing"); continue; }
    found = true;
    if (info.isSymLink() || !info.isFile() || info.size() > 8 * 1024 * 1024) { fail(name + ": migration-unsafe-or-too-large"); return result; }
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    QFile file;
    if (fd < 0) { fail(name + ": migration-read-failed"); return result; }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); fail(name + ": migration-read-failed"); return result; }
    struct stat opened {};
    if (::fstat(fd, &opened) != 0 || !S_ISREG(opened.st_mode) || opened.st_size > 8 * 1024 * 1024) { fail(name + ": migration-unsafe-or-too-large"); return result; }
    const auto bytes = file.read(8 * 1024 * 1024 + 1);
    if (file.error() != QFile::NoError) { fail(name + ": migration-read-failed"); return result; } hash.addData(QByteArray::number(bytes.size())); hash.addData(bytes);
    const auto parsed = compatibility::readJson(bytes, {8 * 1024 * 1024, 64, 100000});
    if (!parsed.ok() || !parsed.document.isObject()) { fail(name + ": migration-invalid-json"); return result; }
    auto doc = parsed.document.object(); documents.insert(name, doc);
    // Only record a version token with a bounded numeric grammar, never arbitrary source text.
    const auto v = doc.value("ApplicationVersion").toString();
    result.versions.insert(name, QRegularExpression("^[0-9]+(?:\\.[0-9]+){1,3}$").match(v).hasMatch() && v.size() < 32 ? v : "unversioned-reference-json");
  }
  if (!found) { fail("migration-source-not-found"); return result; }
  result.fingerprint = QString::fromLatin1(hash.result().toHex());
  const auto receipt = record(m_paths).read();
  if (receipt.status != S::Ok && receipt.status != S::Missing) { fail("migration-record-unreadable"); return result; }
  if (receipt.status == S::Ok && receipt.document.value("fingerprint") == result.fingerprint && receipt.document.value("state") == "completed" && receipt.document.value("converterVersion") == QJsonValue(1)) {
    result.completed = true; p.fields.append("migration: prior completion detected; no changes or duplicates"); return result;
  }
  if (receipt.status == S::Ok && receipt.document.value("fingerprint") != result.fingerprint && receipt.document.contains("backup")) {
    fail("migration-restore-or-dismiss-existing-backup-first"); return result;
  }
  auto stores = p.candidate.value("stores").toObject();
  // The transfer export is portable; migration instead merges into exact native
  // snapshots so existing folders, destinations and unrelated records survive.
  for (const auto &id : ids) if (!p.expected.value(id).isEmpty()) {
    auto native = QJsonDocument::fromJson(p.expected.value(id)).object(); native.remove("previous"); stores.insert(id, native);
  }
  auto application = stores.value("application").toObject(); auto values = application.value("values").toObject();
  const auto app = documents.value(names[0]);
  for (const auto &mapping : QList<QPair<QString, QString>>{{"ShowTray", "showTrayIcon"}, {"DisableToastNotification", "showNotification"}}) {
    if (!app.contains(mapping.first)) continue;
    if (!app.value(mapping.first).isBool()) { report(p, "ApplicationConfig." + mapping.first, D::Rejected); continue; }
    values.insert(mapping.second, mapping.first == "DisableToastNotification" ? !app.value(mapping.first).toBool() : app.value(mapping.first).toBool());
    report(p, "ApplicationConfig." + mapping.first, D::Transformed);
  }
  auto catalog = workflows::parseCaptureWorkflows(QJsonDocument(stores.value("workflows").toObject()).toJson()).catalog;
  if (!catalog) { fail("migration-native-workflows-invalid"); return result; }
  const auto wf = documents.value(names[1]); auto sourceWorkflows = array(wf.value("Hotkeys"));
  if (sourceWorkflows.size() > 64) { fail("WorkflowsConfig.Hotkeys: migration-too-many"); return result; }
  if (wf.contains("Hotkeys") && !wf.value("Hotkeys").isArray() && !wf.value("Hotkeys").toObject().value("$values").isArray()) { fail("WorkflowsConfig.Hotkeys: migration-invalid-collection"); return result; }
  QSet<QString> sourceIds;
  const QMap<QString, QString> capture{{"RectangleRegion", "region"}, {"PrintScreen", "full-screen"}, {"ActiveWindow", "active-window"}};
  const QMap<QString, QString> actions{{"None", ""}, {"CopyImageToClipboard", "image-clipboard"}, {"SaveImageToFile", "save"},
      {"SaveImageToFileWithDialog", "save-as"}, {"AnnotateMedia", "annotation"}, {"AnnotateImage", "annotation"},
      {"CopyFileToClipboard", "file-clipboard"}, {"CopyFilePathToClipboard", "path-clipboard"}, {"ShowInExplorer", "reveal"}};
  auto bindings = hotkeys(values);
  bool changedWorkflows = false;
  for (int n = 0; n < sourceWorkflows.size(); ++n) {
    const auto row = QString("WorkflowsConfig.Hotkeys[%1]").arg(n); const auto item = sourceWorkflows[n].toObject(); const auto task = item.value("TaskSettings").toObject();
    const auto sourceId = item.value("Id").toString(); const auto job = task.value("Job").toString();
    if (sourceId.isEmpty() || sourceId.size() > 256 || sourceIds.contains(sourceId) || !capture.contains(job) ||
        (item.contains("Enabled") && item.value("Enabled") != QJsonValue(true)) ||
        (task.contains("OverrideScreenshotsFolder") && !task.value("OverrideScreenshotsFolder").isBool()) ||
        task.value("DestinationInstanceId").isString()) { report(p, row, D::Rejected); continue; }
    sourceIds.insert(sourceId); workflows::CaptureWorkflow w; w.id = QUuid::createUuidV5(migrationNamespace, "workflow:" + sourceId);
    w.name = task.value("Description").toString(); if (w.name.isEmpty()) w.name = workflows::captureSourceLabel(capture.value(job));
    w.capture = capture.value(job); bool supported = task.value("AfterCaptureJob").isString();
    for (const auto &raw : task.value("AfterCaptureJob").toString().split(',', Qt::SkipEmptyParts)) {
      const auto flag = raw.trimmed(); if (!actions.contains(flag)) supported = false;
      else if (!actions.value(flag).isEmpty() && !w.actions.contains(actions.value(flag))) w.actions.append(actions.value(flag));
    }
    // Preserve only demonstrably equivalent output preferences. No external programs,
    // upload routing, permissions or post-upload actions are imported.
    if (task.value("OverrideScreenshotsFolder") == QJsonValue(true)) w.output.directory = task.value("ScreenshotsFolder").toString();
    const auto upload = task.value("UploadSettings").toObject(); if (upload.value("NameFormatPattern").isString()) w.output.pattern = upload.value("NameFormatPattern").toString();
    if (!supported || !workflows::workflowAvailability(w, workflows::implementedCaptureActions()).isEmpty()) { report(p, row, D::Rejected); continue; }
    bool replaced = false; for (auto &old : catalog->entries) if (old.id == w.id) { old = w; replaced = true; }
    if (!replaced) catalog->entries.append(w);
    const auto preferred = trigger(item.value("HotkeyInfo").toObject());
    if (!preferred.isEmpty()) {
      const auto assigned = bindings.assign(w.id.toString(QUuid::WithoutBraces), preferred);
      report(p, row + ".HotkeyInfo", assigned.ok ? D::Transformed : D::Rejected);
    }
    changedWorkflows = true; report(p, row, D::Transformed);
  }
  const auto general = wf.value("DefaultTaskSettings").toObject().value("GeneralSettings").toObject();
  if (general.value("PlaySoundAfterCapture").isBool()) { values.insert("playCaptureSound", general.value("PlaySoundAfterCapture")); report(p, "WorkflowsConfig.DefaultTaskSettings.GeneralSettings.PlaySoundAfterCapture", D::Transformed); }
  QJsonObject hotkeyValues; for (auto it = bindings.entries.cbegin(); it != bindings.entries.cend(); ++it) hotkeyValues.insert(it.key(), it.value()); values.insert("hotkeys", hotkeyValues);
  application.insert("values", values); stores.insert("application", application);
  stores.insert("workflows", QJsonDocument::fromJson(workflows::workflowsJson(*catalog)).object());
  // Legacy custom-uploader records use the existing reviewed SXCU parser. Modern
  // provider instances and opaque plugin SettingsJson remain unsupported data.
  const auto uploaders = documents.value(names[2]); auto custom = array(uploaders.value("CustomUploaders"));
  for (const auto *key : {"CustomUploaders", "CustomUploadersList"})
    if (uploaders.contains(key) && !uploaders.value(key).isArray() && !uploaders.value(key).toObject().value("$values").isArray()) { fail(QString("UploadersConfig.") + key + ": migration-invalid-collection"); return result; }
  bool legacyList = false; if (custom.isEmpty()) { custom = array(uploaders.value("CustomUploadersList")); legacyList = true; }
  if (custom.size() > 256) { fail("UploadersConfig.CustomUploaders: migration-too-many"); return result; }
  auto destinations = stores.value("destinations").toObject(); auto entries = destinations.value("destinations").toArray();
  bool changedDestinations = false; QSet<QString> destinationIds;
  for (int n = 0; n < custom.size(); ++n) {
    const auto row = QString("UploadersConfig.CustomUploaders[%1]").arg(n); const auto wrapper = custom[n].toObject();
    auto item = legacyList ? wrapper : wrapper.value("Item").toObject();
    if (item.contains("$type")) { item.remove("$type"); report(p, row + ".runtime-type-metadata", D::Ignored); }
    auto imported = uploads::importSxcu(QJsonDocument(item).toJson());
    if (!imported.ok || imported.acceptanceBlocked) { report(p, row, D::Rejected); continue; }
    auto destination = imported.destination;
    const auto stable = legacyList ? "legacy:" + destination.name + ":" + destination.requestUrl : wrapper.value("Id").toString();
    if (stable.isEmpty() || destinationIds.contains(stable)) { report(p, row, D::Rejected); continue; }
    destinationIds.insert(stable);
    destination.id = QUuid::createUuidV5(migrationNamespace, "destination:" + stable);
    // Opaque bodies/extensions cannot be certified free of secrets by this converter.
    if (!destination.data.isEmpty() || !destination.extra.isEmpty()) { report(p, row, D::Rejected); continue; }
    for (int k = 0; k < imported.secrets.size(); ++k) p.fields.append(row + ": secret omitted");
    p.report.add(D::Ignored, imported.secrets.size());
    if (!uploads::validateDestination(destination).isEmpty()) { report(p, row, D::Rejected); continue; }
    const auto json = QJsonDocument::fromJson(uploads::writeDestination(destination)).object();
    bool replaced = false; for (int k = 0; k < entries.size(); ++k) if (entries[k].toObject().value("id") == json.value("id")) { entries[k] = json; replaced = true; }
    if (!replaced) entries.append(json);
    changedDestinations = true; report(p, row, D::Transformed);
  }
  destinations.insert("destinations", entries); stores.insert("destinations", destinations);
  if (!documents.value(names[3]).isEmpty()) {
    const auto providers = array(documents.value(names[3]).value("Instances"));
    if (providers.size() > 256) { fail("uploader-instances.Instances: migration-too-many"); return result; }
    for (int n = 0; n < providers.size(); ++n) report(p, QString("uploader-instances.Instances[%1]: provider settings and credentials unsupported; source kept").arg(n), D::Ignored);
  }
  report(p, "reference history (History.db/JSON/XML): unsupported; source and native history kept", D::Ignored);
  report(p, "reference editor settings, unmapped fields and external custom paths: unsupported; source kept", D::Ignored);
  report(p, "SecretsStore: not opened; secret material excluded", D::Ignored);
  p.candidate.insert("stores", stores);
  const auto details = p.fields; const auto counts = p.report;
  auto checked = transfer.inspect(QJsonDocument(p.candidate).toJson());
  if (!checked.valid()) { p.code = checked.code; return result; }
  checked.fields.append(details); for (size_t k = 0; k < counts.counts.size(); ++k) checked.report.counts[k] += counts.counts[k];
  checked.preservedStores = {"editor"}; if (!changedWorkflows) checked.preservedStores.append("workflows");
  if (!changedDestinations) checked.preservedStores.append("destinations");
  p = checked; return result;
}
QString ReferenceMigration::apply(const QString &folder, const ReferenceMigrationPlan &plan, bool confirmed,
    const std::function<bool(const QString &, int)> &fault) const {
  if (!confirmed) return "migration-cancelled";
  if (!plan.valid()) return "migration-invalid-plan";
  auto fresh = inspectFolder(folder); if (!fresh.valid()) return fresh.configuration.code;
  if (fresh.fingerprint != plan.fingerprint) return "migration-source-changed";
  if (fresh.completed) return "migration-already-completed";
  if (fresh.configuration.expected != plan.configuration.expected) return "migration-native-settings-changed";
  if (!m_paths.configured()) return "configuration-paths-unavailable";
  auto f = record(m_paths); storage::StoreLock lock; if (f.lock(lock) != S::Ok) return "migration-busy";
  auto old = f.read(); if (old.status != S::Ok && old.status != S::Missing) return "migration-record-unreadable";
  auto doc = old.document;
  if (!doc.contains("backup")) doc.insert("backup", encoded(plan.configuration.expected));
  doc.insert("fingerprint", plan.fingerprint); doc.insert("sourceVersions", plan.versions);
  doc.insert("converterVersion", 1); doc.insert("targetVersion", 1); doc.insert("state", "prepared");
  QByteArray snapshot;
  if (f.write(doc, 1, old.snapshot, &snapshot, &lock) != S::Ok) return "migration-backup-failed";
  const auto code = ConfigurationTransfer(m_paths).apply(plan.configuration, true, fault);
  if (!code.isEmpty()) return code;
  doc.insert("state", "completed"); doc.insert("outcome", QJsonObject{{"report", plan.configuration.report.summary()}, {"secrets", "omitted"}});
  if (f.write(doc, 1, snapshot, nullptr, &lock) != S::Ok) {
    const auto rollback = ConfigurationTransfer(m_paths).restoreSnapshots(decoded(doc.value("backup").toObject()), true);
    return rollback.isEmpty() ? QString("migration-record-write-failed-rolled-back") : "migration-recovery-required";
  }
  return {};
}
bool ReferenceMigration::backupAvailable() const {
  if (!m_paths.configured()) return false;
  const auto r = record(m_paths).read(); return r.status == S::Ok && decoded(r.document.value("backup").toObject()).size() == 4;
}
QString ReferenceMigration::backupRevision() const {
  if (!m_paths.configured()) return {};
  const auto r = record(m_paths).read();
  return r.status == S::Ok ? QString::fromLatin1(QCryptographicHash::hash(r.snapshot, QCryptographicHash::Sha256).toHex()) : QString();
}
QString ReferenceMigration::restore(bool confirmed, const QString &expectedRevision) const {
  if (!confirmed) return "migration-cancelled";
  if (!m_paths.configured()) return "configuration-paths-unavailable";
  auto f = record(m_paths); storage::StoreLock lock; if (f.lock(lock) != S::Ok) return "migration-busy";
  const auto r = f.read(); if (r.status != S::Ok) return "migration-backup-unavailable";
  if (!expectedRevision.isEmpty() && expectedRevision != QString::fromLatin1(QCryptographicHash::hash(r.snapshot, QCryptographicHash::Sha256).toHex())) return "migration-backup-changed";
  const auto backup = decoded(r.document.value("backup").toObject());
  const auto code = ConfigurationTransfer(m_paths).restoreSnapshots(backup, true); if (!code.isEmpty()) return code;
  auto doc = r.document; doc.insert("state", "restored");
  return f.write(doc, 1, r.snapshot, nullptr, &lock) == S::Ok ? QString() : QString("migration-record-write-failed");
}
QString ReferenceMigration::dismissBackup(bool confirmed, const QString &expectedRevision) const {
  if (!confirmed) return "migration-cancelled";
  if (!m_paths.configured()) return "configuration-paths-unavailable";
  auto f = record(m_paths); const auto r = f.read(); if (r.status != S::Ok) return "migration-backup-unavailable";
  if (!expectedRevision.isEmpty() && expectedRevision != QString::fromLatin1(QCryptographicHash::hash(r.snapshot, QCryptographicHash::Sha256).toHex())) return "migration-backup-changed";
  auto doc = r.document; doc.remove("backup");
  return f.write(doc, 1, r.snapshot, nullptr) == S::Ok ? QString() : QString("migration-record-write-failed");
}
}
