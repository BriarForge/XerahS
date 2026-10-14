#include "AnnotationColorDialog.h"
#include <QColorDialog>
#include <QLabel>
#include <QLayout>
#include <QPointer>
#include <new>
namespace xerahs::app {
AnnotationColorChoice chooseAnnotationColor(xerahs::editor::EditorSession::ColorTarget target,
    xerahs::editor::Argb initial, const QString &scope, QWidget *parent) {
  using Target = xerahs::editor::EditorSession::ColorTarget;
  if (target != Target::Stroke && target != Target::Fill) return {{}, "annotation-style-invalid"};
  QPointer<QColorDialog> dialog;
  try {
    const QPointer<QWidget> owner(parent); dialog = new QColorDialog(QColor::fromRgba(initial), parent);
    dialog->setObjectName("annotationColorDialog"); dialog->setWindowTitle(target == Target::Stroke ? "Stroke colour" : "Fill colour");
    dialog->setAccessibleName(dialog->windowTitle()); dialog->setAccessibleDescription(scope);
    dialog->setOptions(QColorDialog::ShowAlphaChannel | QColorDialog::DontUseNativeDialog);
    // Changing dialog backends can reset the constructor colour. Set the full
    // RGBA value after selecting the owned widget backend and alpha controls.
    dialog->setCurrentColor(QColor::fromRgba(initial));
    auto *description = new QLabel(scope, dialog); description->setObjectName("annotationColorScope"); description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true); dialog->layout()->addWidget(description);
    const int outcome = dialog->exec(); if (!dialog) return {};
    const QColor chosen = outcome == QDialog::Accepted ? dialog->selectedColor() : QColor();
    delete dialog.data(); if ((parent && !owner) || outcome != QDialog::Accepted) return {};
    return chosen.isValid() ? AnnotationColorChoice{chosen.rgba(), {}} : AnnotationColorChoice{{}, "annotation-style-invalid"};
  } catch (const std::bad_alloc &) { delete dialog.data(); return {{}, "annotation-edit-failed"}; }
}
} // namespace xerahs::app
