#include "PreferenceDialog.h"
#include "DrawingDefaultsDialog.h"
#include "NativeEditorPreferences.h"
#include "NativeEditorAppearance.h"
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace xerahs::app {
using namespace xerahs::editor;
bool manageDrawingDefaults(QWidget *parent) {
  const auto original = nativeEditorPreferences();
  const auto revision = nativeEditorPreferenceRevision();
  QPointer<PreferenceDialog> dialog(new PreferenceDialog(parent)); dialog->setObjectName(QStringLiteral("drawingDefaultsDialog"));
  dialog->setWindowTitle(QStringLiteral("Drawing Defaults"));
  new NativeEditorAppearance(*dialog);
  auto *layout = new QVBoxLayout(dialog);
  auto *description = new QLabel(QStringLiteral("These defaults apply to new annotations in all open editors. Existing annotations stay unchanged."), dialog);
  description->setWordWrap(true); layout->addWidget(description);
  auto *form = new QFormLayout; layout->addLayout(form);
  const auto colour = [=](const QString &field, const QString &label, const QString &name) {
    auto *row = new QHBoxLayout;
    auto *input = new QLineEdit(original.values.value(field).toString(), dialog);
    input->setObjectName(name); input->setAccessibleName(label);
    input->setAccessibleDescription(QStringLiteral("Hexadecimal #RRGGBB or #AARRGGBB. The first pair in eight digits is alpha."));
    auto *picker = new QPushButton(QStringLiteral("Choose…"), dialog);
    picker->setObjectName(name + QStringLiteral("Picker")); picker->setAccessibleName(QStringLiteral("Choose %1").arg(label.toLower()));
    row->addWidget(input); row->addWidget(picker); form->addRow(label, row);
    QObject::connect(picker, &QPushButton::clicked, dialog, [=] {
      if (!dialog || !dialog->ready()) return;
      const auto candidate = changeEditorPreferences(original, {{field, input->text()}});
      const auto style = drawingStyleFromPreferences(candidate.preferences.value_or(original));
      const auto current = field == u"BorderColorHex" ? style.strokeColor : style.fillColor;
      const auto chosen = dialog->chooseColor(QColor::fromRgba(current), label, name + "Dialog");
      if (dialog && chosen) input->setText(formatColor(chosen->rgba()));
    });
    return input;
  };
  auto *stroke = colour(QStringLiteral("BorderColorHex"), QStringLiteral("Stroke colour"), QStringLiteral("defaultStrokeColour"));
  auto *fill = colour(QStringLiteral("FillColorHex"), QStringLiteral("Fill colour"), QStringLiteral("defaultFillColour"));
  auto *thickness = new QSpinBox(dialog); thickness->setObjectName(QStringLiteral("defaultStrokeThickness"));
  thickness->setAccessibleName(QStringLiteral("Stroke thickness in pixels")); thickness->setRange(1, 10000);
  thickness->setValue(original.values.value(u"Thickness").toInt()); form->addRow(QStringLiteral("Thickness (pixels)"), thickness);
  auto *format = new QLabel(QStringLiteral("Colours use #RRGGBB or #AARRGGBB. For example, #80FF0000 is half-transparent red."), dialog);
  format->setWordWrap(true); layout->addWidget(format);
  auto *error = new QLabel(dialog); error->setObjectName(QStringLiteral("drawingDefaultsError"));
  error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyDrawingDefaults"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(QStringLiteral("resetDrawingDefaults"));
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const auto defaults = loadEditorPreferences({});
    stroke->setText(defaults.values.value(u"BorderColorHex").toString()); if (!dialog) return;
    fill->setText(defaults.values.value(u"FillColorHex").toString()); if (!dialog) return;
    thickness->setValue(defaults.values.value(u"Thickness").toInt()); if (dialog) error->clear();
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const QJsonObject changes{{QStringLiteral("BorderColorHex"), stroke->text()},
        {QStringLiteral("FillColorHex"), fill->text()}, {QStringLiteral("Thickness"), thickness->value()}};
    dialog->commit([changes, revision] { return applyDrawingDefaults(changes, revision); }, [=](const PreferenceWrite &written) {
      if (written.diagnostic == u"invalid-BorderColorHex" || written.diagnostic == u"invalid-FillColorHex") {
        error->setText(QStringLiteral("Enter stroke and fill colours as #RRGGBB or #AARRGGBB."));
        if (dialog) (written.diagnostic == u"invalid-BorderColorHex" ? stroke : fill)->setFocus();
      } else error->setText(editorPreferenceDiagnosticMessage(written.diagnostic));
    });
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  return PreferenceDialog::run(dialog, parent);
}
}  // namespace xerahs::app
