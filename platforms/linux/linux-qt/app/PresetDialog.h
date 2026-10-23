// COMPAT-SXIE-001, EE-004..EE-008, EE-015..EE-016.
#pragma once
#include "EffectsDialog.h"
#include "image-editor/effects/SxiePreset.h"
class QListWidget;
namespace xerahs::app {
PreparedCanvasEdit preparePresetEdit(const xerahs::editor::AnnotationDocument &, const QImage &,
    const xerahs::effects::EffectPreset &, const xerahs::effects::EffectRegistry &,
    const xerahs::effects::EffectContext & = {});
class PresetDialog final : public QDialog {
  Q_OBJECT
public:
  PresetDialog(xerahs::editor::AnnotationDocument, QImage, EffectsDialog::Commit,
      std::shared_ptr<const xerahs::effects::EffectRegistry>, QWidget *parent = nullptr);
  ~PresetDialog() override;
  void loadFile(const QString &path);
  void setPreset(xerahs::effects::EffectPreset);
  const xerahs::effects::EffectPreset &preset() const { return m_preset; }
  void accept() override;
  void reject() override;
private:
  void exportFile();
  void status(const QString &code, const QString &message);
  xerahs::editor::AnnotationDocument m_document;
  QImage m_source;
  EffectsDialog::Commit m_commit;
  std::shared_ptr<const xerahs::effects::EffectRegistry> m_registry;
  xerahs::effects::EffectPreset m_preset;
  QListWidget *m_list;
  QLabel *m_status;
  QPushButton *m_apply, *m_export;
  OwnedWork m_work;
  bool m_busy = false, m_canApply = false;
  quint64 m_revision = 0;
};
}
