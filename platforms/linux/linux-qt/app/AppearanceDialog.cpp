#include "PreferenceDialog.h"
#include "AppearanceDialog.h"
#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include <QAccessible>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {
using namespace xerahs::editor;
bool manageEditorAppearance(QWidget *parent) {
  const auto original = nativeEditorPreferences(); const auto revision = nativeEditorPreferenceRevision();
  QPointer<PreferenceDialog> dialog(new PreferenceDialog(parent)); dialog->setObjectName(QStringLiteral("appearanceDialog")); dialog->setWindowTitle(QStringLiteral("Editor Appearance"));
  new NativeEditorAppearance(*dialog);
  auto *layout = new QVBoxLayout(dialog);
  auto *description = new QLabel(QStringLiteral("Applies to all editor windows. Desktop high contrast and reduced motion remain in effect. Colours may adjust on screen for contrast; the saved accent stays unchanged."), dialog);
  description->setWordWrap(true); layout->addWidget(description);
  auto *systemTheme = new QCheckBox(QStringLiteral("Use desktop theme"), dialog);
  systemTheme->setObjectName(QStringLiteral("useSystemTheme")); layout->addWidget(systemTheme);
  auto *form = new QFormLayout; layout->addLayout(form);
  auto *theme = new QComboBox(dialog); theme->setObjectName(QStringLiteral("manualTheme")); theme->setAccessibleName(QStringLiteral("Manual editor theme"));
  theme->addItem(QStringLiteral("Dark"), QStringLiteral("Dark")); theme->addItem(QStringLiteral("Light"), QStringLiteral("Light"));
  form->addRow(QStringLiteral("Manual theme"), theme);
  auto *systemAccent = new QCheckBox(QStringLiteral("Use desktop accent colour"), dialog);
  systemAccent->setObjectName(QStringLiteral("useSystemAccent")); layout->addWidget(systemAccent);
  auto *colourRow = new QHBoxLayout;
  auto *accent = new QLineEdit(dialog); accent->setObjectName(QStringLiteral("manualAccent")); accent->setAccessibleName(QStringLiteral("Manual accent colour"));
  accent->setAccessibleDescription(QStringLiteral("Hexadecimal #RRGGBB or #AARRGGBB. The first pair in eight digits is alpha."));
  auto *picker = new QPushButton(QStringLiteral("Choose…"), dialog); picker->setObjectName(QStringLiteral("manualAccentPicker"));
  picker->setAccessibleName(QStringLiteral("Choose manual accent colour")); colourRow->addWidget(accent); colourRow->addWidget(picker);
  auto *accentForm = new QFormLayout; accentForm->addRow(QStringLiteral("Manual accent colour"), colourRow); layout->addLayout(accentForm);
  auto *error = new QLabel(dialog); error->setObjectName(QStringLiteral("appearanceError")); error->setWordWrap(true); layout->addWidget(error);
  const auto setDraft = [=](const EditorPreferences &preferences) {
    const auto &v = preferences.values;
    systemTheme->setChecked(v.value(u"UseSystemTheme").toBool() || v.value(u"Theme") == QJsonValue(QStringLiteral("System")));
    if (!dialog) return;
    theme->setCurrentIndex(v.value(u"Theme") == QJsonValue(QStringLiteral("Light")) ? 1 : 0);
    if (!dialog) return;
    theme->setEnabled(!systemTheme->isChecked()); if (!dialog) return;
    systemAccent->setChecked(v.value(u"UseSystemAccentColor").toBool()); if (!dialog) return;
    accent->setText(v.value(u"AccentColorHex").toString()); if (!dialog) return;
    accent->setEnabled(!systemAccent->isChecked()); if (dialog) picker->setEnabled(!systemAccent->isChecked());
  };
  setDraft(original);
  QObject::connect(systemTheme, &QCheckBox::toggled, dialog, [theme](bool use) { theme->setEnabled(!use); });
  QObject::connect(systemAccent, &QCheckBox::toggled, dialog, [dialog, accent, picker](bool use) { accent->setEnabled(!use); if (dialog) picker->setEnabled(!use); });
  QObject::connect(picker, &QPushButton::clicked, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const auto candidate = changeEditorPreferences(original, {{QStringLiteral("AccentColorHex"), accent->text()}});
    const auto color = parseColor(candidate.preferences.value_or(original).values.value(u"AccentColorHex").toString()).value_or(0xFF3E83F2);
    const auto chosen = dialog->chooseColor(QColor::fromRgba(color), QStringLiteral("Accent colour"), "appearanceAccentDialog");
    if (dialog && chosen) accent->setText(formatColor(chosen->rgba()));
  });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply")); buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyAppearance"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(QStringLiteral("resetAppearance")); buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, dialog, [=] { if (!dialog || !dialog->ready()) return; setDraft(loadEditorPreferences({})); if (dialog) error->clear(); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const QJsonObject changes{{QStringLiteral("Theme"), theme->currentData().toString()},
        {QStringLiteral("UseSystemTheme"), systemTheme->isChecked()}, {QStringLiteral("UseSystemAccentColor"), systemAccent->isChecked()},
        {QStringLiteral("AccentColorHex"), accent->text()}};
    dialog->commit([changes, revision] { return applyAppearancePreferences(changes, revision); }, [=](const PreferenceWrite &written) {
      error->setText(written.diagnostic == u"invalid-AccentColorHex" ? QStringLiteral("Enter the accent as #RRGGBB or #AARRGGBB.") : editorPreferenceDiagnosticMessage(written.diagnostic));
      if (!dialog) return;
      if (written.diagnostic == u"invalid-AccentColorHex") accent->setFocus();
      if (!dialog) return;
      QAccessibleEvent alert(error, QAccessible::Alert); QAccessible::updateAccessibility(&alert);
    });
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  return PreferenceDialog::run(dialog, parent);
}
}  // namespace xerahs::app
