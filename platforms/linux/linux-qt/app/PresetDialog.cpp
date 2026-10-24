// COMPAT-SXIE-001, EE-004..EE-008, EE-015..EE-016, ROOT-SECURITY-001.
#include "PresetDialog.h"
#include "image-editor/effects/CensusCatalog.h"
#include <algorithm>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTemporaryFile>
namespace xerahs::app {
using namespace xerahs::effects;
PreparedCanvasEdit preparePresetEdit(const editor::AnnotationDocument &document, const QImage &source,
    const EffectPreset &preset, const EffectRegistry &registry, const EffectContext &context) {
  PreparedCanvasEdit edit; edit.result.document = document; edit.source = source;
  if (!preset.ok()) { edit.result.error = preset.error; edit.result.document.reset(); return edit; }
  bool any = false;
  for (const auto &step : preset.steps) {
    if (!step.supported || !step.enabled) continue;
    const auto *effect = registry.find(step.id);
    if (!effect) { edit = {}; edit.result.error = "preset-effect-unavailable"; return edit; }
    EffectContext control = context; control.seed = step.seed;
    auto parameters = step.parameters;
    QTemporaryFile assetFile;
    QString assetKey;
    for (const auto &param : effect->spec().parameters) if (param.kind == ParamKind::FilePath) {
      const auto ref = parameters.value(param.id).toString();
      if (step.id != "draw_image" || param.id != "image_location" || !ref.startsWith("asset:") || !preset.assets.contains(ref.mid(6))) {
        edit = {}; edit.result.error = "preset-asset-missing"; return edit;
      }
      assetKey = ref.mid(6); const auto bytes = preset.assets.value(assetKey);
      if (!assetFile.open() || assetFile.write(bytes) != bytes.size() || !assetFile.flush()) {
        edit = {}; edit.result.error = "preset-asset-write-failed"; return edit;
      }
      assetFile.close(); parameters[param.id] = assetFile.fileName();
    }
    edit = prepareEffectEdit(*edit.result.document, edit.source, *effect, parameters, control);
    if (edit.result.error) { const auto error = edit.result.error; edit = {}; edit.result.error = error; return edit; }
    if (!assetKey.isEmpty()) {
      auto operations = edit.result.document->extra.value("effectOperations").toArray();
      auto operation = operations.last().toObject();
      operation["parameters"] = serializeParameters(effect->spec(), step.parameters);
      operations[operations.size() - 1] = operation;
      edit.result.document->extra["effectOperations"] = operations;
      auto assets = edit.result.document->extra.value("effectPresetAssets").toObject();
      assets[assetKey] = QString::fromLatin1(preset.assets.value(assetKey).toBase64());
      edit.result.document->extra["effectPresetAssets"] = assets;
    }
    any = true;
  }
  if (!any || context.cancelled()) { edit = {}; edit.result.error = any ? "effect-cancelled" : "preset-no-supported-steps"; }
  if (!edit.result.error) {
    auto operations = document.extra.value("effectPresetOperations").toArray();
    operations.append(QJsonDocument::fromJson(writeXsiePreset(preset, registry)).object());
    edit.result.document->extra["effectPresetOperations"] = operations;
  }
  return edit;
}
PresetDialog::PresetDialog(editor::AnnotationDocument document, QImage source, EffectsDialog::Commit commit,
    std::shared_ptr<const EffectRegistry> registry, QWidget *parent)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)),
      m_commit(std::move(commit)), m_registry(std::move(registry)) {
  setObjectName("presetDialog"); setWindowTitle(tr("Image effect preset")); resize(640, 400);
  auto *layout = new QVBoxLayout(this);
  m_list = new QListWidget(this); m_list->setObjectName("presetSteps"); m_list->setAccessibleName(tr("Ordered preset steps and support status")); layout->addWidget(m_list);
  m_status = new QLabel(this); m_status->setObjectName("presetStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
  m_apply = buttons->button(QDialogButtonBox::Apply); m_apply->setEnabled(false);
  m_export = buttons->addButton(tr("Export preset…"), QDialogButtonBox::ActionRole); m_export->setEnabled(false); m_export->setObjectName("presetExport");
  connect(m_apply, &QPushButton::clicked, this, &PresetDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &PresetDialog::reject);
  connect(m_export, &QPushButton::clicked, this, &PresetDialog::exportFile); layout->addWidget(buttons);
}
PresetDialog::~PresetDialog() { m_work.cancel(); }
void PresetDialog::status(const QString &code, const QString &message) { m_status->setText(code.isEmpty() ? message : code + ": " + message); }
void PresetDialog::loadFile(const QString &path) {
  m_work.cancel(); const auto revision = ++m_revision;
  m_busy = true; m_apply->setEnabled(false); m_export->setEnabled(false);
  status({}, tr("Reading preset…")); const auto registry = m_registry;
  m_work = OwnedWork::run<EffectPreset>(this, [path, registry](const OwnedWork::Context &work) {
    QFile file(path); EffectPreset preset;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 192 * 1024 * 1024) { preset.error = "preset-read-failed"; return preset; }
    const auto bytes = file.read(192 * 1024 * 1024 + 1);
    if (work.cancelled()) { preset.error = "effect-cancelled"; return preset; }
    return path.endsWith(".sxie", Qt::CaseInsensitive) ? readSxiePreset(bytes, *registry) : readXsiePreset(bytes, *registry);
  }, [this, revision](EffectPreset preset) { if (revision != m_revision) return; m_busy = false; setPreset(std::move(preset)); });
}
void PresetDialog::setPreset(EffectPreset preset) {
  m_preset = std::move(preset); m_list->clear(); bool any = false;
  for (const auto &step : m_preset.steps) {
    const auto name = step.id.isEmpty() ? step.legacy.value("$type").toString() : step.id;
    const bool unavailable = !m_registry->find(step.id) && std::any_of(censusCatalog().cbegin(), censusCatalog().cend(), [&](const CensusEffect &e) { return e.id == step.id; });
    const auto diagnostic = unavailable ? " — effect-unavailable: " + effectUnavailableReason(step.id) : QString();
    m_list->addItem((step.supported ? tr("Supported: ") : tr("Unsupported: ")) + name +
        (!step.enabled ? tr(" (disabled)") : QString()) + (step.diagnostics.isEmpty() ? QString() : " — " + step.diagnostics.join(", ")) + diagnostic);
    auto *item = m_list->item(m_list->count() - 1);
    item->setData(Qt::AccessibleTextRole, item->text());
    if (unavailable) item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    any |= step.supported && step.enabled && !unavailable;
  }
  m_canApply = m_preset.ok() && any; m_apply->setEnabled(m_canApply); m_export->setEnabled(m_preset.ok());
  status(m_preset.error, m_preset.ok() ? tr("Apply runs supported, enabled steps in order as one undo step. Unsupported data is retained for export.") : tr("The preset could not be read. The document is unchanged."));
}
void PresetDialog::accept() {
  if (m_busy || !m_apply->isEnabled()) return;
  m_busy = true; m_apply->setEnabled(false); m_export->setEnabled(false);
  const auto document = m_document; const auto source = m_source; const auto preset = m_preset; const auto registry = m_registry; const auto revision = ++m_revision;
  status({}, tr("Applying preset…"));
  m_work = OwnedWork::run<PreparedCanvasEdit>(this, [document, source, preset, registry](const OwnedWork::Context &work) {
    return preparePresetEdit(document, source, preset, *registry, {[work] { return work.cancelled(); }, [work](int p) { work.progress(p, 100); }, 0});
  }, [this, revision](PreparedCanvasEdit edit) {
    if (revision != m_revision || m_work.cancelled()) return;
    m_busy = false; m_apply->setEnabled(m_canApply); m_export->setEnabled(true);
    if (edit.result.error) { status(*edit.result.error, edit.result.error->startsWith("effect-unavailable") ? tr("A required font or native rendering resource is unavailable. The document is unchanged.") : tr("The preset failed. The document is unchanged.")); return; }
    const QPointer<PresetDialog> owner(this);
    const bool committed = m_commit && m_commit(std::move(edit));
    if (!owner) return;
    if (!committed) { status("preset-commit-stale", tr("The document changed. Reopen the preset to try again.")); return; }
    QDialog::accept();
  });
}
void PresetDialog::reject() { ++m_revision; m_work.cancel(); QDialog::reject(); }
void PresetDialog::exportFile() {
  if (m_busy) return;
  auto *picker = new QFileDialog(this, tr("Export preset")); picker->setAcceptMode(QFileDialog::AcceptSave);
  picker->setNameFilter(tr("XerahS effect presets (*.xsie)")); picker->setDefaultSuffix("xsie"); picker->setAttribute(Qt::WA_DeleteOnClose);
  connect(picker, &QFileDialog::fileSelected, this, [this](const QString &path) {
    if (m_busy) return;
    m_busy = true; m_apply->setEnabled(false); m_export->setEnabled(false);
    const auto preset = m_preset; const auto registry = m_registry; const auto revision = ++m_revision;
    m_work = OwnedWork::run<QString>(this, [path, preset, registry](const OwnedWork::Context &) {
      storage::PrivateJsonFile file(path, {"com.xerahs.app/effect-preset", 1, 192 * 1024 * 1024});
      const auto read = file.read();
      if (read.status != storage::StoreStatus::Ok && read.status != storage::StoreStatus::Missing) return "preset-export-" + storage::storeStatusName(read.status);
      QByteArray snapshot;
      const auto result = file.write(QJsonDocument::fromJson(writeXsiePreset(preset, *registry)).object(), 1, read.snapshot, &snapshot);
      return result == storage::StoreStatus::Ok ? QString() : "preset-export-" + storage::storeStatusName(result);
    }, [this, revision](QString error) {
      if (revision != m_revision) return;
      m_busy = false; m_apply->setEnabled(m_canApply); m_export->setEnabled(true);
      status(error, error.isEmpty() ? tr("Preset exported.") : tr("The preset could not be saved."));
    });
  }); picker->open();
}
}
