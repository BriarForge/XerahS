#include "CaptureWorkflows.h"
#include "actions/PostCapturePipeline.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QDir>
namespace xerahs::workflows {
using namespace xerahs::editor;
namespace {
const QString format = QStringLiteral("com.xerahs.app/linux-qt/capture-workflows");
bool label(const QString &value, int limit) {
  if (value.isEmpty() || value != value.trimmed() || value.size() > limit) return false;
  for (auto c : value) if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) return false;
  return true;
}
WorkflowRead invalid(const char *code = "workflow-invalid") { return {{}, QString::fromLatin1(code)}; }
bool validText(const QString &text) {
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text[i].isLowSurrogate()) return false;
    if (text[i].isHighSurrogate() && (++i >= text.size() || !text[i].isLowSurrogate())) return false;
  }
  return true;
}
QJsonObject outputJson(const CaptureOutput &output, bool retainUnknown) {
  auto object = retainUnknown ? output.extra : QJsonObject{};
  object.insert("directory", output.directory); object.insert("pattern", output.pattern); return object;
}
}
QString captureDirectoryDiagnostic(const QString &directory) {
  if (directory.isEmpty()) return {};
  if (directory.size() > 32768 || !QDir::isAbsolutePath(directory) || !validText(directory)) return "workflow-directory-invalid";
  for (const auto c : directory) if (c.unicode() < 32 || c.unicode() == 127) return "workflow-directory-invalid";
  for (const auto &part : directory.split('/', Qt::SkipEmptyParts)) if (part == "." || part == "..") return "workflow-directory-invalid";
  return {};
}
xerahs::naming::ExpansionContext captureOutputExample() {
  xerahs::naming::ExpansionContext context;
  context.date = QDate(2026, 1, 2); context.time = QTime(15, 4, 5); context.unixTime = 1767366245;
  context.locale = QLocale("en_US"); context.width = 1920; context.height = 1080; return context;
}
CaptureOutputPreview previewCaptureOutput(const CaptureOutput &output, xerahs::naming::ExpansionContext context) {
  const auto directory = captureDirectoryDiagnostic(output.directory); if (!directory.isEmpty()) return {{}, directory, {}};
  if (output.pattern.size() > 4096 || !validText(output.pattern)) return {{}, "workflow-pattern-invalid", {}};
  // Never borrow file authority from a caller while previewing a definition.
  context.files = nullptr;
  const auto expanded = xerahs::naming::preview({output.pattern, xerahs::naming::ParseMode::Filename, QStringLiteral("png"), {}}, context);
  if (!expanded.ok()) return {{}, "filename-" + xerahs::naming::errorCodeName(expanded.error().code), expanded.error()};
  return {expanded.value(), {}, {}};
}
QStringList implementedCaptureSources() { return {"region", "full-screen", "monitor", "active-window"}; }
QString captureSourceLabel(const QString &source) {
  if (source == "region") return QStringLiteral("Region");
  if (source == "full-screen") return QStringLiteral("Full screen");
  if (source == "monitor") return QStringLiteral("Monitor under pointer");
  if (source == "scrolling") return QStringLiteral("Scrolling capture");
  if (source == "active-window") return QStringLiteral("Active window");
  return {};
}
QStringList implementedCaptureActions() { return {"annotation", "save", "save-as", "image-clipboard", "file-clipboard", "path-clipboard", "reveal"}; }
QJsonObject workflowDiscoveryError(const QString &diagnostic) {
  static const QSet<QString> errors{"usage-invalid", "workflow-invalid", "workflow-too-large", "workflow-newer-version",
      "workflow-upgrade-conflict", "workflow-changed", "settings-unsafe-path", "settings-private-storage-required",
      "settings-too-large", "settings-read-failed", "workflow-storage-unavailable"};
  return {{"schemaVersion", 1}, {"ok", false}, {"error", QJsonObject{{"code", errors.contains(diagnostic) ? diagnostic : QStringLiteral("workflow-storage-unavailable")}}}};
}
CaptureWorkflows defaultCaptureWorkflows() {
  CaptureWorkflow initial; initial.id = QUuid("8df729a2-7d23-49c0-8566-4e4f6d282729");
  initial.name = "Save and copy image"; initial.actions = {"save", "image-clipboard"};
  return {{initial}, initial.id, {}};
}
QJsonObject workflowJson(const CaptureWorkflow &w, bool retainUnknown) {
  auto object = retainUnknown ? w.extra : QJsonObject{};
  object.insert("id", w.id.toString(QUuid::WithoutBraces)); object.insert("name", w.name);
  object.insert("capture", w.capture); object.insert("actions", QJsonArray::fromStringList(w.actions)); object.insert("chooseActions", w.chooseActions);
  object.insert("output", outputJson(w.output, retainUnknown));
  return object;
}
QJsonObject workflowSnapshot(const CaptureWorkflow &w) { auto result = workflowJson(w, false); result.insert("definitionVersion", 2); return result; }
QByteArray workflowsJson(const CaptureWorkflows &c) {
  auto root = c.extra; QJsonArray entries;
  for (const auto &w : c.entries) entries.append(workflowJson(w));
  root.insert("format", format); root.insert("version", 2); root.insert("defaultId", c.defaultId.toString(QUuid::WithoutBraces)); root.insert("workflows", entries);
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
WorkflowRead parseCaptureWorkflows(const QByteArray &bytes) {
  if (bytes.size() > maximumPreferenceBytes) return invalid("workflow-too-large");
  QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
  auto root = document.object();
  if (root.value("version").toDouble() > 2) return invalid("workflow-newer-version");
  if (root.value("format") != format || (root.value("version") != QJsonValue(1) && root.value("version") != QJsonValue(2)) || !root.value("workflows").isArray() || !root.value("defaultId").isString()) return invalid();
  const bool legacy = root.value("version") == QJsonValue(1);
  CaptureWorkflows catalog; catalog.defaultId = QUuid(root.value("defaultId").toString());
  const auto entries = root.value("workflows").toArray(); if (entries.isEmpty() || entries.size() > 64) return invalid();
  QSet<QUuid> ids;
  for (const auto &entry : entries) {
    if (!entry.isObject()) return invalid();
    auto raw = entry.toObject(); CaptureWorkflow w;
    if (!raw.value("id").isString() || !raw.value("name").isString() || !raw.value("capture").isString() || !raw.value("actions").isArray() || !raw.value("chooseActions").isBool()) return invalid();
    w.id = QUuid(raw.value("id").toString()); w.name = raw.value("name").toString(); w.capture = raw.value("capture").toString(); w.chooseActions = raw.value("chooseActions").toBool();
    if (w.id.isNull() || ids.contains(w.id) || !label(w.name, 128) || !label(w.capture, 64)) return invalid();
    // Do not silently give a formerly unknown v1 field new execution authority.
    if (legacy && raw.contains("output")) return invalid("workflow-upgrade-conflict");
    if (!legacy) {
      if (!raw.value("output").isObject()) return invalid();
      auto output = raw.value("output").toObject();
      if (!output.value("directory").isString() || !output.value("pattern").isString()) return invalid();
      w.output.directory = output.value("directory").toString(); w.output.pattern = output.value("pattern").toString();
      if (!captureDirectoryDiagnostic(w.output.directory).isEmpty() || w.output.pattern.size() > 4096 || !validText(w.output.pattern)) return invalid();
      output.remove("directory"); output.remove("pattern"); w.output.extra = output; raw.remove("output");
    }
    ids.insert(w.id); const auto actions = raw.value("actions").toArray(); if (actions.size() > 128) return invalid();
    QSet<QString> unique; static const QRegularExpression actionId("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$");
    for (const auto &action : actions) {
      const auto id = action.toString();
      if (!action.isString() || id.size() > 128 || !actionId.match(id).hasMatch() || unique.contains(id)) return invalid();
      unique.insert(id); w.actions.append(id);
    }
    for (const auto *key : {"id", "name", "capture", "actions", "chooseActions"}) raw.remove(QLatin1String(key));
    w.extra = raw; catalog.entries.append(std::move(w));
  }
  if (!ids.contains(catalog.defaultId)) return invalid();
  for (const auto *key : {"format", "version", "defaultId", "workflows"}) root.remove(QLatin1String(key));
  catalog.extra = root; return {catalog, {}};
}
QString workflowAvailability(const CaptureWorkflow &w, const QStringList &available) {
  CaptureWorkflows one{{w}, w.id, {}};
  if (!parseCaptureWorkflows(workflowsJson(one)).catalog) return "workflow-invalid";
  if (!implementedCaptureSources().contains(w.capture)) return "workflow-capture-unavailable";
  const auto preview = previewCaptureOutput(w.output); if (!preview.diagnostic.isEmpty()) return preview.diagnostic;
  for (const auto &action : w.actions) if (!xerahs::actions::catalogueIndex(action) || !available.contains(action)) return "workflow-action-unavailable";
  return {};
}
QStringList workflowActions(const CaptureWorkflow &w) {
  auto selected = w.actions; if (w.chooseActions) selected.prepend("after-capture-window"); return selected;
}
void CaptureWorkflowStore::reload() {
  m_snapshot = m_storage.read(); ++m_revision;
  if (m_snapshot.kind == PreferenceRead::Kind::Missing) m_current = {defaultCaptureWorkflows(), {}};
  else if (m_snapshot.kind == PreferenceRead::Kind::Failed) m_current = {{}, m_snapshot.diagnostic};
  else m_current = parseCaptureWorkflows(m_snapshot.bytes);
}
PreferenceWrite CaptureWorkflowStore::save(const CaptureWorkflows &draft, quint64 expectedRevision) {
  if (!m_current.catalog) return {false, "workflow-read-only"};
  if (m_revision != expectedRevision) return {false, "workflow-changed"};
  const auto bytes = workflowsJson(draft); const auto verified = parseCaptureWorkflows(bytes);
  if (!verified.catalog) return {false, verified.diagnostic};
  for (const auto &workflow : draft.entries) {
    const auto preview = previewCaptureOutput(workflow.output); if (!preview.diagnostic.isEmpty()) return {false, preview.diagnostic};
  }
  const auto result = m_storage.replace(m_snapshot, bytes); if (!result.saved) return result;
  m_snapshot = {PreferenceRead::Kind::Readable, bytes, {}}; m_current = verified; ++m_revision; return {true, {}};
}
QString CaptureWorkflowStore::unchanged() const {
  if (!m_current.catalog) return m_current.diagnostic.isEmpty() ? QStringLiteral("workflow-read-only") : m_current.diagnostic;
  const auto actual = m_storage.read();
  if (actual.kind == PreferenceRead::Kind::Failed) return actual.diagnostic;
  return actual.kind == m_snapshot.kind && actual.bytes == m_snapshot.bytes ? QString{} : QStringLiteral("workflow-changed");
}
QJsonObject CaptureWorkflowStore::discovery(const QStringList &available) const {
  const auto diagnostic = unchanged(); if (!diagnostic.isEmpty()) return workflowDiscoveryError(diagnostic);
  QJsonArray entries;
  for (const auto &workflow : m_current.catalog->entries) {
    const auto unavailable = workflowAvailability(workflow, available);
    QJsonObject entry{{"id", workflow.id.toString(QUuid::WithoutBraces)}, {"name", workflow.name},
        {"capture", implementedCaptureSources().contains(workflow.capture) ? workflow.capture : QStringLiteral("unsupported")},
        {"definitionAvailable", unavailable.isEmpty()}};
    if (!unavailable.isEmpty()) entry.insert("diagnosticCode", unavailable);
    entries.append(entry);
  }
  return {{"schemaVersion", 1}, {"ok", true}, {"scope", "saved-definitions"},
      {"source", m_snapshot.kind == PreferenceRead::Kind::Missing ? "initial-defaults" : "saved-storage"},
      {"runtimeStatus", "not-queried"}, {"defaultWorkflowId", m_current.catalog->defaultId.toString(QUuid::WithoutBraces)}, {"workflows", entries}};
}
WorkflowChoice CaptureWorkflowStore::resolve(const QUuid &requested, const QStringList &available) const {
  const auto diagnostic = unchanged(); if (!diagnostic.isEmpty()) return {{}, diagnostic};
  const auto &catalog = *m_current.catalog; const auto id = requested.isNull() ? catalog.defaultId : requested;
  for (const auto &workflow : catalog.entries) if (workflow.id == id) {
    const auto unavailable = workflowAvailability(workflow, available);
    return unavailable.isEmpty() ? WorkflowChoice{workflow, {}} : WorkflowChoice{{}, unavailable};
  }
  return {{}, "workflow-not-found"};
}
} // namespace xerahs::workflows
