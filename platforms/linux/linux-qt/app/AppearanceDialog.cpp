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
  QDialog dialog(parent); dialog.setObjectName(QStringLiteral("appearanceDialog")); dialog.setWindowTitle(QStringLiteral("Editor Appearance"));
  NativeEditorAppearance appearance(dialog);
  auto *layout = new QVBoxLayout(&dialog);
  auto *description = new QLabel(QStringLiteral("Applies to all editor windows. Desktop high contrast and reduced motion remain in effect. Colours may adjust on screen for contrast; the saved accent stays unchanged."), &dialog);
  description->setWordWrap(true); layout->addWidget(description);
  auto *systemTheme = new QCheckBox(QStringLiteral("Use desktop theme"), &dialog);
  systemTheme->setObjectName(QStringLiteral("useSystemTheme")); layout->addWidget(systemTheme);
  auto *form = new QFormLayout; layout->addLayout(form);
  auto *theme = new QComboBox(&dialog); theme->setObjectName(QStringLiteral("manualTheme")); theme->setAccessibleName(QStringLiteral("Manual editor theme"));
  theme->addItem(QStringLiteral("Dark"), QStringLiteral("Dark")); theme->addItem(QStringLiteral("Light"), QStringLiteral("Light"));
  form->addRow(QStringLiteral("Manual theme"), theme);
  auto *systemAccent = new QCheckBox(QStringLiteral("Use desktop accent colour"), &dialog);
  systemAccent->setObjectName(QStringLiteral("useSystemAccent")); layout->addWidget(systemAccent);
  auto *colourRow = new QHBoxLayout;
  auto *accent = new QLineEdit(&dialog); accent->setObjectName(QStringLiteral("manualAccent")); accent->setAccessibleName(QStringLiteral("Manual accent colour"));
  accent->setAccessibleDescription(QStringLiteral("Hexadecimal #RRGGBB or #AARRGGBB. The first pair in eight digits is alpha."));
  auto *picker = new QPushButton(QStringLiteral("Choose…"), &dialog); picker->setObjectName(QStringLiteral("manualAccentPicker"));
  picker->setAccessibleName(QStringLiteral("Choose manual accent colour")); colourRow->addWidget(accent); colourRow->addWidget(picker);
  auto *accentForm = new QFormLayout; accentForm->addRow(QStringLiteral("Manual accent colour"), colourRow); layout->addLayout(accentForm);
  auto *error = new QLabel(&dialog); error->setObjectName(QStringLiteral("appearanceError")); error->setWordWrap(true); layout->addWidget(error);
  const auto setDraft = [&](const EditorPreferences &preferences) {
    const auto &v = preferences.values;
    systemTheme->setChecked(v.value(u"UseSystemTheme").toBool() || v.value(u"Theme") == QJsonValue(QStringLiteral("System")));
    theme->setCurrentIndex(v.value(u"Theme") == QJsonValue(QStringLiteral("Light")) ? 1 : 0);
    theme->setEnabled(!systemTheme->isChecked()); systemAccent->setChecked(v.value(u"UseSystemAccentColor").toBool());
    accent->setText(v.value(u"AccentColorHex").toString()); accent->setEnabled(!systemAccent->isChecked()); picker->setEnabled(!systemAccent->isChecked());
  };
  setDraft(original);
  QObject::connect(systemTheme, &QCheckBox::toggled, &dialog, [theme](bool use) { theme->setEnabled(!use); });
  QObject::connect(systemAccent, &QCheckBox::toggled, &dialog, [accent, picker](bool use) { accent->setEnabled(!use); picker->setEnabled(!use); });
  QObject::connect(picker, &QPushButton::clicked, &dialog, [&] {
    const auto candidate = changeEditorPreferences(original, {{QStringLiteral("AccentColorHex"), accent->text()}});
    const auto color = parseColor(candidate.preferences.value_or(original).values.value(u"AccentColorHex").toString()).value_or(0xFF3E83F2);
    const auto chosen = QColorDialog::getColor(QColor::fromRgba(color), &dialog, QStringLiteral("Accent colour"), QColorDialog::ShowAlphaChannel);
    if (chosen.isValid()) accent->setText(formatColor(chosen.rgba()));
  });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply")); buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyAppearance"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(QStringLiteral("resetAppearance")); buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] { setDraft(loadEditorPreferences({})); error->clear(); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    const auto written = applyAppearancePreferences({{QStringLiteral("Theme"), theme->currentData().toString()},
        {QStringLiteral("UseSystemTheme"), systemTheme->isChecked()}, {QStringLiteral("UseSystemAccentColor"), systemAccent->isChecked()},
        {QStringLiteral("AccentColorHex"), accent->text()}}, revision);
    if (written.saved) { dialog.accept(); return; }
    error->setText(editorPreferenceDiagnosticMessage(written.diagnostic));
    if (written.diagnostic == u"invalid-AccentColorHex") { error->setText(QStringLiteral("Enter the accent as #RRGGBB or #AARRGGBB.")); accent->setFocus(); }
    QAccessibleEvent alert(error, QAccessible::Alert); QAccessible::updateAccessibility(&alert);
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  return dialog.exec() == QDialog::Accepted;
}
}  // namespace xerahs::app
