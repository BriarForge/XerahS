#pragma once
#include "image-editor/EditorPreferenceStore.h"
#include <QColorDialog>
#include <QPointer>
#include <QScopeGuard>
#include <functional>

namespace xerahs::app {
// Native lifetime/admission boundary only. Validation, revision checks and atomic
// storage remain in the existing preference host and QtCore production store.
class PreferenceDialog final : public QDialog {
public:
  explicit PreferenceDialog(QWidget *parent) : QDialog(parent) {}
  bool ready() const { return !m_finished && !m_committing && !m_picking; }
  void commit(const std::function<xerahs::editor::PreferenceWrite()> &write,
              const std::function<void(const xerahs::editor::PreferenceWrite &)> &report) {
    if (!ready()) return;
    const QPointer<PreferenceDialog> owner(this); m_committing = true;
    const auto restore = qScopeGuard([owner] {
      if (owner && !owner->m_finished) { owner->setEnabled(true); if (owner) owner->m_committing = false; }
    });
    setEnabled(false); if (!owner || m_finished) return;
    const auto result = write();
    if (!owner || m_finished) return;
    if (result.saved) { m_saved = true; done(Accepted); }
    else { setEnabled(true); if (owner && !m_finished) report(result); }
  }
  std::optional<QColor> chooseColor(QColor initial, const QString &title, const QString &name) {
    if (!ready()) return {};
    const QPointer<PreferenceDialog> owner(this); m_picking = true;
    const auto restore = qScopeGuard([owner] { if (owner) owner->m_picking = false; });
    QPointer<QColorDialog> picker(new QColorDialog(initial, this)); m_picker = picker;
    picker->setObjectName(name); picker->setAccessibleName(title); picker->setWindowTitle(title);
    picker->setOption(QColorDialog::ShowAlphaChannel);
    const int answer = picker->exec();
    const auto color = picker && answer == Accepted ? picker->selectedColor() : QColor{};
    if (owner) m_picker.clear();
    destroyDialog(picker);
    if (!owner || m_finished || !color.isValid()) return {};
    return color;
  }
  void done(int result) override {
    if (m_finished || (result == Accepted && !m_saved)) return;
    m_finished = true; const QPointer<PreferenceDialog> owner(this);
    if (m_picker) m_picker->reject();
    if (owner) QDialog::done(result);
  }
  static bool run(QPointer<PreferenceDialog> dialog, QWidget *parent) {
    const QPointer<QWidget> owner(parent); const bool hadParent = parent;
    if (!dialog) return false;
    dialog->exec();
    const bool saved = dialog && dialog->m_saved;
    destroyDialog(dialog);
    return saved && (!hadParent || owner);
  }
private:
  template<class T> static void destroyDialog(QPointer<T> dialog) {
    if (dialog) dialog->hide();
    // Destruction observers may delete the parent. Detach before deletion.
    if (dialog) dialog->setParent(nullptr);
    delete dialog.data();
  }
  QPointer<QColorDialog> m_picker;
  bool m_committing = false, m_picking = false, m_finished = false, m_saved = false;
};
} // namespace xerahs::app
