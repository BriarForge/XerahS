#include <QRegularExpression>
#include "ConfigurationTransfer.h"
#include "ApplicationSettings.h"
#include "image-editor/EditorPreferences.h"
#include "storage/JsonBoundary.h"
#include "storage/PrivateJsonFile.h"
#include "uploads/DestinationCatalog.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <array>
#include <memory>

namespace xerahs::settings {
namespace {
using D = compatibility::ImportDisposition;
using S = storage::StoreStatus;
const QStringList ids{"application", "editor", "workflows", "destinations"};
QString format(int i) {
  return QStringLiteral("com.xerahs.app/linux-qt/") + QStringList{"application-settings", "editor-settings", "capture-workflows", "upload-destinations"}.at(i);
}
int version(int i) { return i == 2 ? 2 : 1; }
QStringList paths(const ConfigurationPaths &p) { return {p.application, p.editor, p.workflows, p.destinations}; }
storage::PrivateJsonFile file(const ConfigurationPaths &p, int i) { return {paths(p).at(i), {format(i), version(i), 8 * 1024 * 1024, 0, 30000, true}}; }
QString journalPath(const ConfigurationPaths &p) { return p.application + ".import-recovery"; }
storage::PrivateJsonFile journal(const ConfigurationPaths &p) {
  return {journalPath(p), {"com.xerahs.app/configuration-recovery", 1, 64 * 1024 * 1024}};
}
bool atomic(const QString &path, const QByteArray &bytes) {
  QSaveFile out(path); out.setDirectWriteFallback(false);
  return out.open(QIODevice::WriteOnly) && out.setPermissions(QFile::ReadOwner | QFile::WriteOwner) &&
      out.write(bytes) == bytes.size() && out.commit();
}
bool validSnapshot(int i, const QByteArray &bytes) {
  if (bytes.isEmpty()) return true;
  const auto parsed = compatibility::readJson(bytes, {i == 3 ? 8 * 1024 * 1024 : 1024 * 1024, 64, 100000});
  if (!parsed.ok() || !parsed.document.isObject()) return false;
  const auto doc = parsed.document.object(); const auto v = doc.value("version");
  return (doc.value("format") == format(i) || ((i == 0 || i == 3) && !doc.contains("format"))) &&
      v.isDouble() && (v == QJsonValue(1) || (i == 2 && v == QJsonValue(2)));
}
void entry(ConfigurationPlan &p, const QString &path, D d) {
  p.report.add(d);
  if (d == D::Rejected || d == D::Quarantined) {  // COMPCAT-003: paths are structural (known keys, destination ordinals), never values
    static const QRegularExpression ordinal(QStringLiteral("^destinations\\[(\\d+)\\]$"));
    const auto m = ordinal.match(path);
    p.report.addRecord(m.hasMatch() ? m.captured(1).toLongLong() : -1, QStringLiteral("configuration-field"), path, d,
                       d == D::Rejected ? QStringLiteral("rejected-before-apply") : QStringLiteral("quarantined"));
  }
  p.fields.append(path + ": " + QStringList{"retained", "transformed", "defaulted", "quarantined", "rejected", "ignored"}.at(static_cast<int>(d)));
}
void omit(ConfigurationPlan &p, const QString &code) {
  if (!p.exportReport.omissions.contains(code)) p.exportReport.omissions.append(code);
}
bool secretKey(const QString &key) {
  const auto k = key.toLower();
  return compatibility::unsafeUnknown(key) || k.contains("key") || k.contains("account");
}
// Opaque request bodies and extensions have no reviewed secret schema. They are
// omitted explicitly rather than guessing whether their text is a credential.
QJsonObject portableDestination(uploads::UploadDestination d, ConfigurationPlan &p, int index) {
  const auto prefix = QString("destinations[%1]").arg(index);
  auto scrub = [&](QString &v, const QString &path, bool sensitive) {
    if (sensitive && !v.isEmpty() && !uploads::referencedSecret(v)) {
      v = "{secret:omitted}"; omit(p, "destination-secret-fields");
      p.fields.append(path + ": secret omitted"); p.report.add(D::Ignored);
    }
  };
  QUrl url(d.requestUrl, QUrl::StrictMode);
  if (!url.userInfo().isEmpty()) { url.setUserInfo({}); omit(p, "destination-url-credentials"); }
  QUrlQuery query(url); QList<QPair<QString, QString>> clean;
  for (auto pair : query.queryItems(QUrl::FullyDecoded)) {
    scrub(pair.second, prefix + ".url.query", secretKey(pair.first)); clean.append(pair);
  }
  query.setQueryItems(clean); url.setQuery(query); d.requestUrl = url.toString();
  for (auto *list : {&d.query, &d.headers, &d.arguments})
    for (auto &f : *list) scrub(f.value, prefix + ".request-field", secretKey(f.name));
  if (!d.data.isEmpty()) { d.data.clear(); omit(p, "destination-opaque-body"); }
  if (!d.extra.isEmpty()) { d.extra = {}; omit(p, "destination-unreviewed-extensions"); }
  if (!uploads::DestinationCatalog::secretNames(d).isEmpty()) {
    omit(p, "credential-service-secrets"); p.fields.append(prefix + ": secret references retained; credentials omitted");
  }
  return QJsonDocument::fromJson(uploads::writeDestination(d)).object();
}
QJsonObject defaults(int i, const ConfigurationPaths &p) {
  if (i == 0) return {{"format", format(i)}, {"version", 1}, {"values", defaultSettings(p.defaultFolder)}};
  if (i == 1) return {{"format", format(i)}, {"version", 1}, {"current", editor::serializeEditorPreferences(editor::loadEditorPreferences({}))}};
  if (i == 2) return QJsonDocument::fromJson(workflows::workflowsJson(workflows::defaultCaptureWorkflows())).object();
  return {{"format", format(i)}, {"version", 1}, {"default", ""}, {"destinations", QJsonArray{}}};
}
// Strict acceptance differs from tolerant ordinary loading: a repaired invalid
// field is reported, but the entire import is blocked before publication.
bool validate(QJsonObject &root, int i, const ConfigurationPaths &paths, ConfigurationPlan &plan) {
  if (i == 2 && root.value("version") == QJsonValue(1)) {
    const auto legacy = workflows::parseCaptureWorkflows(QJsonDocument(root).toJson());
    if (!legacy.catalog) return false;
    root = QJsonDocument::fromJson(workflows::workflowsJson(*legacy.catalog)).object();
    entry(plan, "workflows.version", D::Transformed);
  }
  if (root.value("format") != format(i) || root.value("version") != QJsonValue(version(i))) return false;
  auto extra = root;
  for (const auto *key : {"format", "version"}) extra.remove(key);
  static const QVector<QStringList> members{{"values", "previous"}, {"current", "previous"}, {"workflows", "defaultId"}, {"destinations", "default"}};
  for (const auto &key : members.at(i)) extra.remove(key);
  if (compatibility::containsUnsafeUnknown(extra) || compatibility::containsTraversal(extra)) return false;
  if (i == 0) {
    if (!root.value("values").isObject()) return false;
    const auto input = root.value("values").toObject(); const auto r = repairSettings(input, paths.defaultFolder);
    for (auto it = r.fields.cbegin(); it != r.fields.cend(); ++it) {
      const bool known = precedenceOrder(it.key()).size() > 0;
      if (input.contains(it.key())) entry(plan, "application." + (known ? it.key() : QString("extension")),
          it.value() == FieldState::Invalid ? D::Rejected : r.values.value(it.key()) != input.value(it.key()) ? D::Transformed : D::Retained);
    }
    if (!r.writable || !r.diagnostics.isEmpty()) return false;
    root.insert("values", r.values); root.remove("previous");
  } else if (i == 1) {
    if (!root.value("current").isObject()) return false;
    const auto r = editor::loadEditorPreferences(root.value("current").toObject());
    for (auto it = r.fields.cbegin(); it != r.fields.cend(); ++it)
      if (it.value() != editor::PreferenceFieldState::Missing) entry(plan,
          "editor." + (it.value() == editor::PreferenceFieldState::Unknown ? QString("extension") : it.key()),
          it.value() == editor::PreferenceFieldState::Invalid ? D::Rejected : D::Retained);
    if (r.readOnly || r.report.count(D::Rejected)) return false;
    root.insert("current", editor::serializeEditorPreferences(r)); root.remove("previous");
  } else if (i == 2) {
    const auto r = workflows::parseCaptureWorkflows(QJsonDocument(root).toJson());
    if (!r.catalog) return false;
    for (const auto &w : r.catalog->entries) {
      if (!workflows::workflowAvailability(w, workflows::implementedCaptureActions()).isEmpty() ||
          compatibility::containsUnsafeUnknown(w.extra) || compatibility::containsUnsafeUnknown(w.output.extra)) return false;
      entry(plan, "workflows.record", D::Retained);
    }
    if (compatibility::containsUnsafeUnknown(r.catalog->extra)) return false;
  } else {
    if (!root.value("destinations").isArray() || !root.value("default").isString()) return false;
    const auto array = root.value("destinations").toArray(); if (array.size() > uploads::maximumDestinations) return false;
    QSet<QUuid> seen;
    for (int n = 0; n < array.size(); ++n) {
      if (!array[n].isObject()) return false;
      const auto r = uploads::readDestination(QJsonDocument(array[n].toObject()).toJson());
      if (!r.destination || !uploads::validateDestination(*r.destination).isEmpty() || seen.contains(r.destination->id) ||
          compatibility::containsUnsafeUnknown(r.destination->extra)) { entry(plan, QString("destinations[%1]").arg(n), D::Rejected); return false; }
      ConfigurationPlan scrub;
      bool existing = false;
      const auto live = file(paths, i).read();
      for (const auto &old : live.document.value("destinations").toArray()) if (old == array[n]) existing = true;
      if (!existing) {
        if (portableDestination(*r.destination, scrub, n) != array[n].toObject()) {
          plan.fields.append(QString("destinations[%1]: secret or unreviewed content rejected").arg(n)); return false;
        }
        plan.fields.append(scrub.fields);
        for (const auto &code : scrub.exportReport.omissions) omit(plan, code);
      }
      seen.insert(r.destination->id); entry(plan, QString("destinations[%1]").arg(n), D::Retained);
    }
    const auto def = root.value("default").toString(); if (!def.isEmpty() && !seen.contains(QUuid(def))) return false;
  }
  return QJsonDocument(root).toJson().size() <= (i == 3 ? 8 * 1024 * 1024 : 1024 * 1024);
}
}
QString ConfigurationTransfer::target() { return "com.xerahs.app/configuration"; }
QString ConfigurationPlan::preview() const {
  return report.summary() + "\n" + fields.join('\n') + "\nOmissions: " + exportReport.omissions.join(", ") +
      (code.isEmpty() ? QString() : "\n" + code);
}
ConfigurationPlan ConfigurationTransfer::exportPlan() const {
  ConfigurationPlan p; p.exportReport.target = target();
  if (!m_paths.configured()) { p.code = "configuration-paths-unavailable"; return p; }
  QJsonObject stores;
  for (int i = 0; i < 4; ++i) {
    const auto r = file(m_paths, i).read();
    if (r.status != S::Ok && r.status != S::Missing) { p.code = ids[i] + ": configuration-read-failed"; return p; }
    p.expected.insert(ids[i], r.snapshot);
    auto doc = r.status == S::Missing ? defaults(i, m_paths) : r.document;
    if (!doc.contains("format") && (i == 0 || i == 3)) { doc.insert("format", format(i)); entry(p, ids[i] + ".format", D::Transformed); }
    if (i == 0) {
      auto values = doc.value("values").toObject();
      for (const auto *key : {"saveFolder", "systemAudioDevice", "microphoneDevice", "startAtLogin", "regionCapture.includeCursor"}) values.remove(key);
      doc.insert("values", values); doc.remove("previous"); omit(p, "store-recovery-snapshots");
      omit(p, "profile-local-save-folder"); omit(p, "native-audio-devices"); omit(p, "autostart-authority"); omit(p, "compositor-capture-opt-in");
    } else if (i == 1) {
      auto values = doc.value("current").toObject();
      for (const auto *key : {"RecentImageFiles", "WindowState", "WindowWidth", "WindowHeight", "IsWindowMaximized"}) values.remove(key);
      values.remove("RememberRecentImagePaths");
      doc.insert("current", values); doc.remove("previous"); omit(p, "store-recovery-snapshots");
      omit(p, "profile-local-recent-paths"); omit(p, "profile-local-window-state");
    } else if (i == 2) {
      auto r = workflows::parseCaptureWorkflows(QJsonDocument(doc).toJson());
      if (!r.catalog) { p.code = "workflows: configuration-invalid"; return p; }
      for (auto &w : r.catalog->entries) w.output.directory.clear();
      doc = QJsonDocument::fromJson(workflows::workflowsJson(*r.catalog)).object(); omit(p, "workflow-local-folders");
    } else {
      QJsonArray out;
      for (const auto &v : doc.value("destinations").toArray()) {
        const auto r = uploads::readDestination(QJsonDocument(v.toObject()).toJson());
        if (!r.destination) { p.code = "destinations: configuration-invalid"; return p; }
        out.append(portableDestination(*r.destination, p, out.size()));
      }
      doc = {{"format", format(i)}, {"version", 1}, {"default", doc.value("default")}, {"destinations", out}};
      omit(p, "credential-service-secrets");
    }
    if (!validate(doc, i, m_paths, p)) { p.code = ids[i] + ": configuration-invalid"; return p; }
    if (i == 0) {
      auto values = doc.value("values").toObject();
      for (const auto *key : {"saveFolder", "systemAudioDevice", "microphoneDevice", "startAtLogin", "regionCapture.includeCursor"}) values.remove(key);
      doc.insert("values", values);
    } else if (i == 1) {
      auto values = doc.value("current").toObject();
      for (const auto *key : {"RecentImageFiles", "RememberRecentImagePaths", "WindowWidth", "WindowHeight", "IsWindowMaximized"}) values.remove(key);
      doc.insert("current", values);
    }
    stores.insert(ids[i], doc);
  }
  p.candidate = {{"format", target()}, {"version", 1}, {"target", "com.xerahs.app"}, {"stores", stores},
      {"omissions", QJsonArray::fromStringList(p.exportReport.omissions)}};
  return p;
}
QString ConfigurationTransfer::exportFile(const QString &path, compatibility::ExportReport *report) const {
  auto p = exportPlan(); if (!p.valid()) return p.code;
  for (const auto &live : paths(m_paths)) if (QFileInfo(live).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) return "configuration-export-live-path";
  storage::PrivateJsonFile out(path, {target(), 1, 8 * 1024 * 1024});
  const auto old = out.read(); if (old.status != S::Ok && old.status != S::Missing) return "configuration-export-unsafe-target";
  const auto status = out.write(p.candidate, 1, old.snapshot, nullptr);
  if (report) *report = p.exportReport;
  return status == S::Ok ? QString() : "configuration-export-failed";
}
ConfigurationPlan ConfigurationTransfer::inspect(const QByteArray &bytes) const {
  ConfigurationPlan p; p.exportReport.target = target();
  const auto parsed = compatibility::readJson(bytes, {8 * 1024 * 1024, 64, 100000});
  if (!parsed.ok() || !parsed.document.isObject()) { p.code = "configuration-invalid-json"; return p; }
  auto root = parsed.document.object();
  if (root.value("format") != target() || root.value("version") != QJsonValue(1) || root.value("target") != "com.xerahs.app" ||
      !root.value("stores").isObject()) { p.code = "configuration-unsupported-version-or-target"; return p; }
  auto extensions = root;
  for (const auto *key : {"format", "version", "target", "stores", "omissions"}) extensions.remove(key);
  if (compatibility::containsUnsafeUnknown(extensions) || compatibility::containsTraversal(extensions)) { p.code = "configuration-unsafe-extension"; return p; }
  auto stores = root.value("stores").toObject();
  if (stores.size() != 4) { p.code = "configuration-store-set-invalid"; return p; }
  for (int i = 0; i < 4; ++i) {
    if (!stores.value(ids[i]).isObject()) { p.code = ids[i] + ": configuration-invalid"; return p; }
    auto doc = stores.value(ids[i]).toObject();
    const auto live = file(m_paths, i).read();
    if (i == 0 && doc.value("values").isObject()) {
      auto values = doc.value("values").toObject();
      auto local = live.status == S::Ok ? live.document.value("values").toObject() : defaultSettings(m_paths.defaultFolder);
      for (const auto *key : {"saveFolder", "systemAudioDevice", "microphoneDevice", "startAtLogin", "regionCapture.includeCursor"}) {
        if (!values.contains(key)) { values.insert(key, local.value(key)); entry(p, QString("application.") + key, D::Defaulted); }
        else if ((QString(key) == "startAtLogin" || QString(key) == "regionCapture.includeCursor") && values.value(key) != local.value(key)) { p.code = QString("application.") + key + ": configuration-authority-not-portable"; return p; }
      }
      doc.insert("values", values);
    }
    if (i == 1 && doc.value("current").isObject()) {
      auto values = doc.value("current").toObject();
      const auto local = live.status == S::Ok ? live.document.value("current").toObject() : defaults(i, m_paths).value("current").toObject();
      for (const auto *key : {"RecentImageFiles", "RememberRecentImagePaths", "WindowWidth", "WindowHeight", "IsWindowMaximized"})
        if (!values.contains(key) && local.contains(key)) { values.insert(key, local.value(key)); entry(p, QString("editor.") + key, D::Defaulted); }
      doc.insert("current", values);
    }
    if (!validate(doc, i, m_paths, p)) { entry(p, ids[i], D::Rejected); p.code = ids[i] + ": configuration-invalid"; return p; }
    stores.insert(ids[i], doc);
    const auto r = file(m_paths, i).read();
    if (r.status != S::Ok && r.status != S::Missing) { p.code = ids[i] + ": configuration-live-unreadable"; return p; }
    p.expected.insert(ids[i], r.snapshot);
  }
  // Never echo untrusted omission text from input in the preview.
  if (root.value("omissions").isArray() && !root.value("omissions").toArray().isEmpty()) {
    static const QStringList knownOmissions{"profile-local-save-folder", "native-audio-devices", "autostart-authority", "profile-local-recent-paths", "profile-local-window-state", "workflow-local-folders", "credential-service-secrets", "destination-secret-fields", "destination-url-credentials", "destination-opaque-body", "destination-unreviewed-extensions", "store-recovery-snapshots"};
    for (const auto &value : root.value("omissions").toArray()) if (value.isString() && knownOmissions.contains(value.toString())) omit(p, value.toString());
    p.fields.append("source: declared omissions present; unrecognized labels are not displayed");
  }
  p.fields.append("credentials: secret values excluded; history, documents, plugins and automation remain unchanged");
  root.insert("stores", stores); p.candidate = root; return p;
}
namespace {
QString publish(const ConfigurationPaths &p, const QMap<QString, QByteArray> &expected,
    const QMap<QString, QByteArray> &desired, const std::function<bool(const QString &, int)> &fault = {}) {
  std::array<storage::StoreLock, 4> locks;
  std::array<std::unique_ptr<QSaveFile>, 4> staged;
  QJsonObject before, after;
  for (int i = 0; i < 4; ++i) {
    auto f = file(p, i);
    if (f.lock(locks[i]) != S::Ok) return ids[i] + ": configuration-busy-or-unsafe";
    const auto r = f.read();
    if ((r.status != S::Ok && r.status != S::Missing) || r.snapshot != expected.value(ids[i])) return ids[i] + ": configuration-changed";
    before.insert(ids[i], QString::fromLatin1(r.snapshot.toBase64()));
    const auto bytes = desired.value(ids[i]);
    if (bytes.size() > (i == 3 ? 8 * 1024 * 1024 : 1024 * 1024)) return ids[i] + ": configuration-too-large";
    after.insert(ids[i], QString::fromLatin1(bytes.toBase64()));
    if (fault && fault("prepare", i)) return ids[i] + ": configuration-prepare-failed";
    if (bytes == r.snapshot || bytes.isEmpty()) continue;
    staged[i] = std::make_unique<QSaveFile>(paths(p)[i]); staged[i]->setDirectWriteFallback(false);
    if (!staged[i]->open(QIODevice::WriteOnly) ||
        !staged[i]->setPermissions(QFile::ReadOwner | QFile::WriteOwner) || staged[i]->write(bytes) != bytes.size()) return ids[i] + ": configuration-prepare-failed";
  }
  storage::StoreLock recoveryLock; auto j = journal(p);
  if (j.lock(recoveryLock) != S::Ok || j.read().status != S::Missing) return "configuration-recovery-required";
  if (j.write({{"before", before}, {"after", after}}, 1, {}, nullptr, &recoveryLock) != S::Ok) return "configuration-backup-failed";
  for (int i = 0; i < 4; ++i) {
    const bool fail = fault && fault("publish", i);
    if (!fail && desired.value(ids[i]) == expected.value(ids[i])) continue;
    const bool saved = !fail && (desired.value(ids[i]).isEmpty() ? QFile::remove(paths(p)[i]) : staged[i]->commit());
    if (!saved) {
      bool restored = true;
      for (int n = 0; n < i; ++n) {
        const auto bytes = QByteArray::fromBase64(before.value(ids[n]).toString().toLatin1());
        if (desired.value(ids[n]) == expected.value(ids[n])) continue;
        restored &= bytes.isEmpty() ? (!QFile::exists(paths(p)[n]) || QFile::remove(paths(p)[n])) : atomic(paths(p)[n], bytes);
      }
      if (restored) QFile::remove(journalPath(p));
      return ids[i] + (restored ? ": configuration-publish-failed-rolled-back" : ": configuration-recovery-required");
    }
  }
  return QFile::remove(journalPath(p)) ? QString() : QString("configuration-recovery-required");
}
} // namespace
QString ConfigurationTransfer::apply(const ConfigurationPlan &plan, bool confirmed, const std::function<bool(const QString &, int)> &fault) const {
  if (!confirmed) return "configuration-cancelled";
  if (!plan.valid() || !m_paths.configured()) return "configuration-invalid-plan";
  auto checked = inspect(QJsonDocument(plan.candidate).toJson()); if (!checked.valid()) return checked.code;
  QMap<QString, QByteArray> desired;
  for (const auto &id : ids) desired.insert(id, plan.preservedStores.contains(id) ? plan.expected.value(id) :
      QJsonDocument(checked.candidate.value("stores").toObject().value(id).toObject()).toJson());
  return publish(m_paths, plan.expected, desired, fault);
}
QString ConfigurationTransfer::restoreSnapshots(const QMap<QString, QByteArray> &snapshots, bool confirmed) const {
  if (!m_paths.configured()) return "configuration-paths-unavailable";
  if (!confirmed) return "configuration-cancelled";
  if (snapshots.size() != 4) return "configuration-backup-invalid";
  QMap<QString, QByteArray> expected;
  for (int i = 0; i < 4; ++i) {
    if (!snapshots.contains(ids[i])) return "configuration-backup-invalid";
    const auto bytes = snapshots.value(ids[i]);
    if (!validSnapshot(i, bytes)) return "configuration-backup-invalid";
    const auto r = file(m_paths, i).read(); if (r.status != S::Ok && r.status != S::Missing) return "configuration-live-unreadable";
    expected.insert(ids[i], r.snapshot);
  }
  return publish(m_paths, expected, snapshots);
}

QString ConfigurationTransfer::recover() const {
  if (!m_paths.configured()) return "configuration-paths-unavailable";
  auto j = journal(m_paths);
  const auto initial = j.read();
  if (initial.status == S::Missing) return {}; // Ordinary/headless startup must remain read-only.
  if (initial.status != S::Ok) return "configuration-recovery-invalid";
  storage::StoreLock jl;
  if (j.lock(jl) != S::Ok) return "configuration-recovery-busy";
  const auto r = j.read(); if (r.status == S::Missing) return {};
  if (r.status != S::Ok) return "configuration-recovery-invalid";
  const auto before = r.document.value("before").toObject(), after = r.document.value("after").toObject();
  if (before.size() != 4 || after.size() != 4) return "configuration-recovery-invalid";
  std::array<storage::StoreLock, 4> locks;
  for (int i = 0; i < 4; ++i) {
    if (file(m_paths, i).lock(locks[i]) != S::Ok) return "configuration-recovery-busy";
    const auto current = file(m_paths, i).read();
    if (!before.value(ids[i]).isString() || !after.value(ids[i]).isString()) return "configuration-recovery-invalid";
    const auto b = QByteArray::fromBase64Encoding(before.value(ids[i]).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const auto a = QByteArray::fromBase64Encoding(after.value(ids[i]).toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!b || !a || !validSnapshot(i, b.decoded) || !validSnapshot(i, a.decoded)) return "configuration-recovery-invalid";
    if ((current.status != S::Ok && current.status != S::Missing) || (current.snapshot != a.decoded && current.snapshot != b.decoded)) return "configuration-recovery-conflict";
  }
  for (int i = 0; i < 4; ++i) {
    const auto bytes = QByteArray::fromBase64(before.value(ids[i]).toString().toLatin1());
    if (bytes.isEmpty()) { if (QFile::exists(paths(m_paths)[i]) && !QFile::remove(paths(m_paths)[i])) return "configuration-recovery-write-failed"; }
    else if (!atomic(paths(m_paths)[i], bytes)) return "configuration-recovery-write-failed";
  }
  return QFile::remove(journalPath(m_paths)) ? QString() : QString("configuration-recovery-write-failed");
}
}
