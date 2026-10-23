// EDITOR-EFFECTS-001: EE-002..EE-006, EE-010..EE-014.
#pragma once

#include "EditorCanvasOperations.h"
#include "OwnedWork.h"
#include "image-editor/effects/Effect.h"
#include "storage/PrivateJsonFile.h"
#include <QDialog>
#include <QTimer>

class QComboBox;
class QLineEdit;
class QListWidget;
class QFormLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QDialogButtonBox;

namespace xerahs::app {
struct EffectsLists {
  QStringList recent, favorites;
  QByteArray snapshot;
  QString diagnostic;
};
class EffectsStore {
public:
  explicit EffectsStore(QString path);
  EffectsLists load(const xerahs::effects::EffectRegistry &) const;
  EffectsLists save(EffectsLists lists) const;
private:
  xerahs::storage::PrivateJsonFile m_file;
};

PreparedCanvasEdit prepareEffectEdit(const xerahs::editor::AnnotationDocument &, const QImage &,
    const xerahs::effects::Effect &, const xerahs::effects::ParamValues &,
    const xerahs::effects::EffectContext &);

class EffectsDialog final : public QDialog {
  Q_OBJECT
public:
  using Commit = std::function<bool(PreparedCanvasEdit)>;
  EffectsDialog(xerahs::editor::AnnotationDocument document, QImage source, Commit commit,
      QWidget *parent = nullptr, QString storePath = {},
      xerahs::effects::EffectRegistry registry = xerahs::effects::builtinEffects());
  ~EffectsDialog() override;
  void reject() override;
  void accept() override;
  QImage previewImage() const { return m_previewImage; }
  QString selectedId() const;
  xerahs::effects::ParamValues parameters() const { return m_values; }
signals:
  void previewChanged();
  void effectCommitted(const QString &id);
private:
  void rebuildList();
  void selectEffect();
  void buildParameters();
  void changed();
  void run(bool apply);
  void persist(bool closeAfter = false);
  void showError(const QString &code);
  void updateFavorite();
  const xerahs::editor::AnnotationDocument m_document;
  const QImage m_source;
  const Commit m_commit;
  const std::shared_ptr<const xerahs::effects::EffectRegistry> m_registry;
  const std::shared_ptr<EffectsStore> m_store;
  EffectsLists m_lists;
  xerahs::effects::ParamValues m_values;
  QComboBox *m_category;
  QLineEdit *m_search;
  QListWidget *m_list;
  QWidget *m_parameters;
  QFormLayout *m_form;
  QLabel *m_preview, *m_status;
  QProgressBar *m_progress;
  QPushButton *m_favorite, *m_reset, *m_clearRecent;
  QDialogButtonBox *m_buttons;
  QTimer m_debounce;
  OwnedWork m_work;
  QImage m_previewImage;
  quint64 m_revision = 0;
  bool m_loading = true, m_saving = false, m_saveAgain = false;
  bool m_applying = false, m_closing = false, m_closeAfterSave = false;
};
} // namespace xerahs::app
