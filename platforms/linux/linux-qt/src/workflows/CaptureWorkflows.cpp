#include "CaptureWorkflows.h"
#include "actions/PostCapturePipeline.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
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
  return object;
}
QJsonObject workflowSnapshot(const CaptureWorkflow &w) { auto result = workflowJson(w, false); result.insert("definitionVersion", 1); return result; }
QByteArray workflowsJson(const CaptureWorkflows &c) {
  auto root = c.extra; QJsonArray entries;
  for (const auto &w : c.entries) entries.append(workflowJson(w));
  root.insert("format", format); root.insert("version", 1); root.insert("defaultId", c.defaultId.toString(QUuid::WithoutBraces)); root.insert("workflows", entries);
  return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
WorkflowRead parseCaptureWorkflows(const QByteArray &bytes) {
  if (bytes.size() > maximumPreferenceBytes) return invalid("workflow-too-large");
  QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
  auto root = document.object();
  if (root.value("version").toDouble() > 1) return invalid("workflow-newer-version");
  if (root.value("format") != format || root.value("version") != QJsonValue(1) || !root.value("workflows").isArray() || !root.value("defaultId").isString()) return invalid();
  CaptureWorkflows catalog; catalog.defaultId = QUuid(root.value("defaultId").toString());
  const auto entries = root.value("workflows").toArray(); if (entries.isEmpty() || entries.size() > 64) return invalid();
  QSet<QUuid> ids;
  for (const auto &entry : entries) {
    if (!entry.isObject()) return invalid();
    auto raw = entry.toObject(); CaptureWorkflow w;
    if (!raw.value("id").isString() || !raw.value("name").isString() || !raw.value("capture").isString() || !raw.value("actions").isArray() || !raw.value("chooseActions").isBool()) return invalid();
    w.id = QUuid(raw.value("id").toString()); w.name = raw.value("name").toString(); w.capture = raw.value("capture").toString(); w.chooseActions = raw.value("chooseActions").toBool();
    if (w.id.isNull() || ids.contains(w.id) || !label(w.name, 128) || !label(w.capture, 64)) return invalid();
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
  if (w.capture != "region") return "workflow-capture-unavailable";
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
  const auto result = m_storage.replace(m_snapshot, bytes); if (!result.saved) return result;
  m_snapshot = {PreferenceRead::Kind::Readable, bytes, {}}; m_current = verified; ++m_revision; return {true, {}};
}
QString CaptureWorkflowStore::unchanged() const {
  if (!m_current.catalog) return m_current.diagnostic.isEmpty() ? QStringLiteral("workflow-read-only") : m_current.diagnostic;
  const auto actual = m_storage.read();
  if (actual.kind == PreferenceRead::Kind::Failed) return actual.diagnostic;
  return actual.kind == m_snapshot.kind && actual.bytes == m_snapshot.bytes ? QString{} : QStringLiteral("workflow-changed");
}
} // namespace xerahs::workflows
