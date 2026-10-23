// EDITOR-EFFECTS-001: EE-002..EE-006, EE-010..EE-014.
#include "EffectsDialog.h"
#include "EditorSource.h"
#include "RasterSource.h"
#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QColorSpace>
#include <QPointer>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::effects;
using namespace xerahs::editor;
namespace {
QStringList knownIds(const QJsonArray &array, const EffectRegistry &registry) {
  QStringList ids;
  for (const auto &entry : array) {
    const auto id = entry.toString();
    if (registry.find(id) && !ids.contains(id)) ids.append(id);
  }
  return ids;
}
PreparedCanvasEdit failure(const QString &code) {
  PreparedCanvasEdit result; result.result.error = code; return result;
}
QString defaultPath() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
      QStringLiteral("/com.xerahs.app/linux-qt/effects-recents.json");
}
}
EffectsStore::EffectsStore(QString path)
    : m_file(std::move(path), {QStringLiteral("com.xerahs.app/effects-recents"), 1, 65536}) {}
EffectsLists EffectsStore::load(const EffectRegistry &registry) const {
  const auto read = m_file.read();
  EffectsLists lists;
  lists.snapshot = read.snapshot;
  if (read.status != storage::StoreStatus::Ok && read.status != storage::StoreStatus::Missing) {
    lists.diagnostic = "effects-store-" + storage::storeStatusName(read.status); return lists;
  }
  if (read.status == storage::StoreStatus::Ok &&
      (!read.document.value("recent").isArray() || !read.document.value("favorites").isArray())) {
    lists.diagnostic = "effects-store-invalid"; return lists;
  }
  lists.recent = knownIds(read.document.value("recent").toArray(), registry);
  lists.favorites = knownIds(read.document.value("favorites").toArray(), registry);
  return lists;
}
EffectsLists EffectsStore::save(EffectsLists lists) const {
  if (!lists.diagnostic.isEmpty()) return lists;
  lists.recent.removeDuplicates(); lists.favorites.removeDuplicates();
  lists.recent.removeAll(QString()); lists.favorites.removeAll(QString());
  const auto status = m_file.write({{"recent", QJsonArray::fromStringList(lists.recent)},
      {"favorites", QJsonArray::fromStringList(lists.favorites)}}, 1, lists.snapshot, &lists.snapshot);
  if (status != storage::StoreStatus::Ok) lists.diagnostic = "effects-store-" + storage::storeStatusName(status);
  return lists;
}

PreparedCanvasEdit prepareEffectEdit(const AnnotationDocument &document, const QImage &source,
    const Effect &effect, const ParamValues &values, const EffectContext &context) {
  try {
    const auto validSize = [](const QImage &image) {
      return !image.isNull() && image.width() <= kMaxDimension && image.height() <= kMaxDimension &&
          qint64(image.width()) * image.height() <= kMaxPixels;
    };
    if (!validSize(source) || document.canvasWidth != source.width() || document.canvasHeight != source.height()) return failure("effect-invalid-input");
    const auto output = runEffect(effect, source, values, context);
    if (!output.ok()) return failure(output.error);
    if (!validSize(output.image)) return failure("effect-resource-limit");
    PreparedCanvasEdit prepared;
    if (output.image.size() != source.size()) {
      // EE-006: dimension-only effects use the same centred canvas geometry
      // and annotation protection rules as Resize Canvas, before replacing pixels.
      CanvasOperation operation; operation.action = CanvasAction::ResizeCanvas;
      operation.width = output.image.width(); operation.height = output.image.height();
      prepared = prepareCanvasEdit(document, source, operation, {context.cancelled, context.progress});
      if (prepared.result.error) return prepared;
    } else prepared.result.document = document;
    if (context.cancelled()) return failure("effect-cancelled");
    QByteArray bytes; QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !output.image.save(&buffer, "PNG")) return failure("effect-encoding-failed");
    if (context.cancelled()) return failure("effect-cancelled");
    if (bytes.size() > kMaxEncodedImageBytes) return failure("effect-resource-limit");
    prepared.result.document->sourceImagePng = bytes;
    prepared.result.changed = true;
    prepared.source = output.image;
    QImage display = output.image;
    if (!display.colorSpace().isValid()) display.setColorSpace(QColorSpace::SRgb);
    prepared.displaySource = display.colorSpace() == QColorSpace(QColorSpace::SRgb) ? display : display.convertedToColorSpace(QColorSpace::SRgb);
    if (prepared.displaySource.isNull()) return failure("effect-resource-limit");
    return prepared;
  } catch (...) { return failure("effect-failed"); }
}

EffectsDialog::EffectsDialog(AnnotationDocument document, QImage source, Commit commit,
    QWidget *parent, QString storePath, EffectRegistry registry)
    : QDialog(parent), m_document(std::move(document)), m_source(std::move(source)), m_commit(std::move(commit)),
      m_registry(std::make_shared<EffectRegistry>(std::move(registry))),
      m_store(std::make_shared<EffectsStore>(storePath.isEmpty() ? defaultPath() : storePath)) {
  setObjectName("effectsDialog"); setWindowTitle(tr("Effects")); setAccessibleName(windowTitle()); resize(840, 640);
  auto *layout = new QVBoxLayout(this);
  auto *discovery = new QHBoxLayout;
  m_category = new QComboBox(this); m_category->setObjectName("effectsCategory"); m_category->setAccessibleName(tr("Effect category"));
  m_category->addItems({tr("All effects"), tr("Adjustments"), tr("Drawings"), tr("Filters"), tr("Manipulations"), tr("Recent"), tr("Favorites")});
  m_search = new QLineEdit(this); m_search->setObjectName("effectsSearch"); m_search->setAccessibleName(tr("Search effects by name or ID")); m_search->setPlaceholderText(tr("Search effects"));
  discovery->addWidget(m_category); discovery->addWidget(m_search); layout->addLayout(discovery);
  auto *body = new QHBoxLayout;
  m_list = new QListWidget(this); m_list->setObjectName("effectsList"); m_list->setAccessibleName(tr("Effects")); body->addWidget(m_list, 1);
  auto *right = new QVBoxLayout;
  m_parameters = new QWidget(this); m_form = new QFormLayout(m_parameters);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setWidget(m_parameters); right->addWidget(scroll, 1);
  m_preview = new QLabel(tr("Select an effect to preview"), this); m_preview->setObjectName("effectsPreview"); m_preview->setAlignment(Qt::AlignCenter); m_preview->setMinimumSize(240, 180);
  m_preview->setAccessibleName(tr("Non-authoritative effect preview")); right->addWidget(m_preview, 1);
  body->addLayout(right, 2); layout->addLayout(body, 1);
  auto *actions = new QHBoxLayout;
  m_favorite = new QPushButton(tr("Favorite"), this); m_favorite->setObjectName("effectsFavorite"); m_favorite->setCheckable(true);
  m_clearRecent = new QPushButton(tr("Clear Recent"), this); m_clearRecent->setObjectName("effectsClearRecent");
  m_reset = new QPushButton(tr("Reset to defaults"), this); m_reset->setObjectName("effectsReset");
  for (auto *button : {m_favorite, m_clearRecent, m_reset}) { button->setAutoDefault(false); actions->addWidget(button); }
  layout->addLayout(actions);
  m_status = new QLabel(this); m_status->setObjectName("effectsStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  m_progress = new QProgressBar(this); m_progress->setAccessibleName(tr("Effect progress")); m_progress->setRange(0, 100); m_progress->hide(); layout->addWidget(m_progress);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this); layout->addWidget(m_buttons);
  connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &EffectsDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &EffectsDialog::reject);
  connect(m_search, &QLineEdit::textChanged, this, &EffectsDialog::rebuildList);
  connect(m_category, &QComboBox::currentIndexChanged, this, &EffectsDialog::rebuildList);
  connect(m_list, &QListWidget::currentRowChanged, this, &EffectsDialog::selectEffect);
  connect(m_reset, &QPushButton::clicked, this, [this] { buildParameters(); changed(); });
  connect(m_favorite, &QPushButton::clicked, this, [this](bool checked) {
    const auto id = selectedId(); if (id.isEmpty()) return;
    m_lists.favorites.removeAll(id); if (checked) m_lists.favorites.append(id);
    persist(); rebuildList();
  });
  connect(m_clearRecent, &QPushButton::clicked, this, [this] { m_lists.recent.clear(); persist(); rebuildList(); });
  m_debounce.setSingleShot(true); m_debounce.setInterval(60);
  connect(&m_debounce, &QTimer::timeout, this, [this] { run(false); });
  rebuildList(); updateFavorite();
  const auto store = m_store; const auto catalog = m_registry;
  OwnedWork::run<EffectsLists>(this, [store, catalog](const OwnedWork::Context &) { return store->load(*catalog); },
      [this](EffectsLists lists) {
        m_lists = std::move(lists); m_loading = false;
        if (!m_lists.diagnostic.isEmpty()) showError(m_lists.diagnostic);
        rebuildList(); updateFavorite();
      });
}
EffectsDialog::~EffectsDialog() { m_work.cancel(); }
QString EffectsDialog::selectedId() const { return m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole).toString() : QString(); }
void EffectsDialog::showError(const QString &code) {
  m_status->setText(code + (code.startsWith("effects-store-")
      ? tr(": Recent and favorite effects could not be saved or loaded.")
      : tr(": The effect could not be completed. The document has been kept unchanged.")));
  m_status->setAccessibleDescription(m_status->text());
}
void EffectsDialog::updateFavorite() {
  const bool writable = !m_loading && m_lists.diagnostic.isEmpty() && !m_applying && !m_closeAfterSave;
  m_favorite->setEnabled(writable && !selectedId().isEmpty()); m_clearRecent->setEnabled(writable);
  m_favorite->setChecked(m_lists.favorites.contains(selectedId()));
  m_buttons->button(QDialogButtonBox::Apply)->setEnabled(!m_loading && !m_applying && !m_closeAfterSave && !selectedId().isEmpty());
}
void EffectsDialog::rebuildList() {
  const auto previous = selectedId();
  {
    QSignalBlocker block(m_list); m_list->clear();
    const auto category = m_category->currentIndex(); const auto query = m_search->text();
    auto specs = m_registry->specs();
    if (category == 5 || category == 6) {
      specs.clear();
      for (const auto &id : category == 5 ? m_lists.recent : m_lists.favorites)
        if (const auto *effect = m_registry->find(id)) specs.append(&effect->spec());
    }
    for (const auto *spec : specs) {
      if (category >= 1 && category <= 4 && int(spec->category) != category - 1) continue;
      if (!spec->displayName.contains(query, Qt::CaseInsensitive) && !spec->id.contains(query, Qt::CaseInsensitive)) continue;
      auto *item = new QListWidgetItem(spec->displayName + " (" + spec->id + ")", m_list);
      item->setData(Qt::UserRole, spec->id); item->setData(Qt::AccessibleTextRole, item->text());
      if (spec->id == previous) m_list->setCurrentItem(item);
    }
  }
  if (!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
  else if (selectedId() != previous) selectEffect();
  updateFavorite();
}
void EffectsDialog::selectEffect() {
  buildParameters(); updateFavorite(); changed();
}
void EffectsDialog::buildParameters() {
  while (m_form->rowCount()) m_form->removeRow(0);
  m_values.clear();
  const auto *effect = m_registry->find(selectedId()); if (!effect) return;
  for (const auto &param : effect->spec().parameters) {
    m_values.insert(param.id, param.defaultValue);
    QWidget *control = nullptr;
    const auto set = [this, id = param.id](QVariant value) { m_values[id] = std::move(value); changed(); };
    if (param.kind == ParamKind::Float || param.kind == ParamKind::Int) {
      auto *row = new QWidget(m_parameters); auto *box = new QHBoxLayout(row); box->setContentsMargins(0, 0, 0, 0);
      auto *slider = new QSlider(Qt::Horizontal, row); slider->setObjectName("slider:" + param.id); slider->setAccessibleName(param.label);
      if (param.kind == ParamKind::Int) {
        auto *spin = new QSpinBox(row); spin->setRange(int(param.minimum), int(param.maximum)); spin->setValue(param.defaultValue.toInt());
        slider->setRange(spin->minimum(), spin->maximum()); slider->setValue(spin->value());
        connect(spin, &QSpinBox::valueChanged, this, [set, slider](int value) { QSignalBlocker block(slider); slider->setValue(value); set(value); });
        connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
        control = spin; box->addWidget(slider); box->addWidget(spin);
      } else {
        auto *spin = new QDoubleSpinBox(row); spin->setDecimals(6); spin->setRange(param.minimum, param.maximum); spin->setValue(param.defaultValue.toDouble());
        slider->setRange(0, 10000);
        const auto position = [param](double value) { return param.maximum == param.minimum ? 0 : int(std::round((value - param.minimum) * 10000 / (param.maximum - param.minimum))); };
        slider->setValue(position(spin->value()));
        connect(spin, &QDoubleSpinBox::valueChanged, this, [set, slider, position](double value) { QSignalBlocker block(slider); slider->setValue(position(value)); set(value); });
        connect(slider, &QSlider::valueChanged, spin, [spin, param](int value) { spin->setValue(param.minimum + (param.maximum - param.minimum) * value / 10000); });
        control = spin; box->addWidget(slider); box->addWidget(spin);
      }
      control->setObjectName("param:" + param.id); control->setAccessibleName(param.label);
      control->setAccessibleDescription(tr("Range %1 to %2").arg(param.minimum).arg(param.maximum));
      row->setFocusProxy(control); m_form->addRow(param.label, row); continue;
    } else if (param.kind == ParamKind::Bool) {
      auto *check = new QCheckBox(m_parameters); check->setChecked(param.defaultValue.toBool());
      connect(check, &QCheckBox::toggled, this, set); control = check;
    } else if (param.kind == ParamKind::Enum) {
      auto *combo = new QComboBox(m_parameters); combo->addItems(param.choices); combo->setCurrentText(param.defaultValue.toString());
      connect(combo, &QComboBox::currentTextChanged, this, set); control = combo;
    } else if (param.kind == ParamKind::Color) {
      auto *button = new QPushButton(param.defaultValue.value<QColor>().name(QColor::HexArgb), m_parameters); button->setAutoDefault(false);
      connect(button, &QPushButton::clicked, this, [this, button, param, set] {
        auto *picker = new QColorDialog(m_values.value(param.id).value<QColor>(), button);
        picker->setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
        picker->setWindowTitle(param.label); picker->setAttribute(Qt::WA_DeleteOnClose);
        connect(picker, &QColorDialog::colorSelected, button, [button, set](const QColor &color) { button->setText(color.name(QColor::HexArgb)); set(color); });
        picker->open();
      }); control = button;
    } else {
      auto *edit = new QLineEdit(param.defaultValue.toString(), m_parameters); control = edit;
      connect(edit, &QLineEdit::textChanged, this, set);
      if (param.kind == ParamKind::FilePath) {
        auto *row = new QWidget(m_parameters); auto *box = new QHBoxLayout(row); box->setContentsMargins(0, 0, 0, 0);
        auto *browse = new QPushButton(tr("Browse…"), row); browse->setAutoDefault(false); browse->setAccessibleName(tr("Choose %1").arg(param.label));
        connect(browse, &QPushButton::clicked, this, [edit, browse, param] {
          auto *picker = new QFileDialog(browse, param.label); picker->setFileMode(QFileDialog::ExistingFile); picker->setAttribute(Qt::WA_DeleteOnClose);
          connect(picker, &QFileDialog::fileSelected, edit, &QLineEdit::setText); picker->open();
        });
        box->addWidget(edit); box->addWidget(browse);
        edit->setObjectName("param:" + param.id); edit->setAccessibleName(param.label); row->setFocusProxy(edit); m_form->addRow(param.label, row); continue;
      }
    }
    control->setObjectName("param:" + param.id); control->setAccessibleName(param.label); m_form->addRow(param.label, control);
  }
}
void EffectsDialog::changed() {
  m_work.cancel(); ++m_revision; m_debounce.stop(); m_previewImage = {}; m_preview->clear();
  m_buttons->button(QDialogButtonBox::Apply)->setEnabled(!selectedId().isEmpty() && !m_applying && !m_loading && !m_closeAfterSave);
  m_progress->hide();
  const auto *effect = m_registry->find(selectedId());
  if (effect && effect->spec().mode == Mode::Previewable) m_debounce.start();
}
void EffectsDialog::run(bool apply) {
  const auto id = selectedId(); const auto *effect = m_registry->find(id); if (!effect || m_closing) return;
  m_work.cancel(); const auto revision = ++m_revision;
  m_applying = apply; m_parameters->setEnabled(!apply); m_category->setEnabled(!apply); m_search->setEnabled(!apply); m_list->setEnabled(!apply); m_reset->setEnabled(!apply);
  m_buttons->button(QDialogButtonBox::Apply)->setEnabled(!apply); updateFavorite();
  m_status->setText(apply ? tr("Applying at full resolution…") : tr("Previewing at full resolution…"));
  m_progress->setValue(0); m_progress->show();
  const auto registry = m_registry; const auto document = m_document; const auto source = m_source; const auto values = m_values;
  const auto previewSize = m_preview->size();
  struct Result { PreparedCanvasEdit edit; QImage preview, thumbnail; };
  m_work = OwnedWork::run<Result>(this, [registry, id, document, source, values, apply, previewSize](const OwnedWork::Context &work) {
    const EffectContext context{[work] { return work.cancelled(); }, [work](int p) { work.progress(p, 100); }, 0};
    Result result;
    result.edit = prepareEffectEdit(document, source, *registry->find(id), values, context);
    if (!apply && !result.edit.result.error) {
      auto rendered = renderEditorImage(result.edit.source, *result.edit.result.document, {context.cancelled, context.progress});
      if (!rendered.diagnostic.isEmpty()) result.edit.result.error = rendered.diagnostic;
      else {
        result.preview = std::move(rendered.image);
        result.thumbnail = result.preview.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      }
    }
    return result;
  }, [this, revision, apply, id](Result result) {
    if (m_closing || revision != m_revision || m_work.cancelled()) return;
    m_progress->hide(); m_applying = false;
    m_parameters->setEnabled(true); m_category->setEnabled(true); m_search->setEnabled(true); m_list->setEnabled(true); m_reset->setEnabled(true);
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(true); updateFavorite();
    if (result.edit.result.error) { showError(*result.edit.result.error); return; }
    if (apply) {
      const QPointer<EffectsDialog> owner(this);
      const bool committed = m_commit && m_commit(std::move(result.edit));
      if (!owner) return;
      if (!committed) { showError("effect-commit-stale"); return; }
      m_parameters->setEnabled(false); m_category->setEnabled(false); m_search->setEnabled(false); m_list->setEnabled(false); m_reset->setEnabled(false);
      m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
      m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
      m_lists.recent.removeAll(id); m_lists.recent.prepend(id);
      while (m_lists.recent.size() > 20) m_lists.recent.removeLast();
      m_closeAfterSave = true; updateFavorite();
      emit effectCommitted(id); if (owner) persist(true);
    } else {
      m_previewImage = std::move(result.preview);
      m_preview->setPixmap(QPixmap::fromImage(result.thumbnail));
      m_status->setText(tr("Preview only. Apply commits one undo step."));
      if (!m_lists.diagnostic.isEmpty()) showError(m_lists.diagnostic);
      emit previewChanged();
    }
  }, [this, revision](qint64 done, qint64) { if (!m_closing && revision == m_revision) m_progress->setValue(int(done)); });
}
void EffectsDialog::persist(bool closeAfter) {
  m_closeAfterSave = m_closeAfterSave || closeAfter;
  if (m_loading || !m_lists.diagnostic.isEmpty()) {
    if (closeAfter) { showError(m_lists.diagnostic.isEmpty() ? "effects-store-not-ready" : m_lists.diagnostic); m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false); }
    return;
  }
  if (m_saving) { m_saveAgain = true; return; }
  m_saving = true; const auto lists = m_lists; const auto store = m_store;
  OwnedWork::run<EffectsLists>(this, [store, lists](const OwnedWork::Context &) { return store->save(lists); },
      [this](EffectsLists saved) {
        m_saving = false; m_lists.snapshot = saved.snapshot;
        if (!saved.diagnostic.isEmpty()) {
          m_lists.diagnostic = saved.diagnostic; showError(saved.diagnostic); updateFavorite();
          if (m_closeAfterSave) m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
          return;
        }
        if (m_saveAgain) { m_saveAgain = false; persist(); }
        else if (m_closeAfterSave) { m_closing = true; QDialog::accept(); }
      });
}
void EffectsDialog::accept() {
  if (m_applying || m_closing || m_closeAfterSave || m_loading) return;
  m_debounce.stop(); run(true);
}
void EffectsDialog::reject() {
  m_closing = true; ++m_revision; m_debounce.stop(); m_work.cancel(); QDialog::reject();
}
} // namespace xerahs::app
