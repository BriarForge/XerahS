#include "PreferenceDialog.h"
#include "DrawingDefaultsDialog.h"
#include "NativeEditorPreferences.h"
#include "NativeEditorAppearance.h"
#include "image-editor/ShapePainter.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
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
  // ESET-008: Text, Speech Balloon and Step content defaults.
  struct TextControls { QString familyKey, sizeKey, boldKey, italicKey, alignKey; QComboBox *family; QDoubleSpinBox *size; QCheckBox *bold, *italic; QComboBox *align; double initialSize = 0; double savedSize = 0; };
  const auto textSection = [=](const QString &title, const QString &prefix, const QString &familyKey, const QString &sizeKey,
                               const QString &boldKey, const QString &italicKey, const QString &alignKey) {
    TextControls c{familyKey, sizeKey, boldKey, italicKey, alignKey, nullptr, nullptr, nullptr, nullptr, nullptr};
    auto *heading = new QLabel(title, dialog); heading->setTextFormat(Qt::PlainText); form->addRow(heading);
    c.family = new QComboBox(dialog); c.family->setObjectName(prefix + QStringLiteral("FontFamily"));
    c.family->setAccessibleName(title + QStringLiteral(" font family"));
    c.family->addItem(QStringLiteral("System default"), QString());
    for (const auto &name : QFontDatabase::families()) c.family->addItem(name, name);
    const QString saved = original.values.value(familyKey).toString();
    if (!saved.isEmpty() && c.family->findData(saved) < 0) {
      // The saved family stays selectable so Apply does not rewrite it by accident.
      c.family->insertItem(1, xerahs::editor::nativeFontFamilyAvailable(saved) ? saved : saved + QStringLiteral(" (not installed)"), saved);
    }
    c.family->setCurrentIndex(std::max(0, c.family->findData(saved)));
    form->addRow(QStringLiteral("Font family"), c.family);
    if (!saved.isEmpty() && !xerahs::editor::nativeFontFamilyAvailable(saved)) {
      auto *note = new QLabel(QStringLiteral("The saved font \"%1\" is not installed. New objects use the system default font until you choose a replacement.").arg(saved), dialog);
      note->setObjectName(prefix + QStringLiteral("FontUnavailable")); note->setWordWrap(true); note->setTextFormat(Qt::PlainText); form->addRow(note);
    }
    c.size = new QDoubleSpinBox(dialog); c.size->setObjectName(prefix + QStringLiteral("FontSize")); c.size->setAccessibleName(title + QStringLiteral(" font size in pixels"));
    c.size->setDecimals(4); c.size->setRange(1, 2000); c.savedSize = original.values.value(sizeKey).toDouble(24); c.size->setValue(c.savedSize); c.initialSize = c.size->value();
    form->addRow(QStringLiteral("Font size (pixels)"), c.size);
    c.bold = new QCheckBox(QStringLiteral("Bold"), dialog); c.bold->setObjectName(prefix + QStringLiteral("Bold")); c.bold->setChecked(original.values.value(boldKey).toBool());
    c.italic = new QCheckBox(QStringLiteral("Italic"), dialog); c.italic->setObjectName(prefix + QStringLiteral("Italic")); c.italic->setChecked(original.values.value(italicKey).toBool());
    form->addRow(c.bold); form->addRow(c.italic);
    c.align = new QComboBox(dialog); c.align->setObjectName(prefix + QStringLiteral("Alignment")); c.align->setAccessibleName(title + QStringLiteral(" horizontal alignment"));
    for (const auto *name : {"Left", "Center", "Right"}) c.align->addItem(QLatin1String(name), QLatin1String(name));
    c.align->setCurrentIndex(std::max(0, c.align->findData(original.values.value(alignKey).toString())));
    form->addRow(QStringLiteral("Alignment"), c.align);
    return c;
  };
  const auto textControls = textSection(QStringLiteral("Text annotations"), QStringLiteral("defaultText"), QStringLiteral("TextFontFamily"),
      QStringLiteral("TextFontSize"), QStringLiteral("TextBold"), QStringLiteral("TextItalic"), QStringLiteral("TextHorizontalAlignment"));
  const auto balloonControls = textSection(QStringLiteral("Speech balloons"), QStringLiteral("defaultBalloon"), QStringLiteral("SpeechBalloonFontFamily"),
      QStringLiteral("SpeechBalloonFontSize"), QStringLiteral("SpeechBalloonTextBold"), QStringLiteral("SpeechBalloonTextItalic"), QStringLiteral("SpeechBalloonTextHorizontalAlignment"));
  auto *stepHeading = new QLabel(QStringLiteral("Step numbers"), dialog); form->addRow(stepHeading);
  auto *stepSize = new QDoubleSpinBox(dialog); stepSize->setObjectName(QStringLiteral("defaultStepFontSize"));
  stepSize->setAccessibleName(QStringLiteral("Step number font size in pixels; zero sizes it from the step")); stepSize->setDecimals(4);
  stepSize->setRange(0, 2000); stepSize->setSpecialValueText(QStringLiteral("Automatic")); const double savedStepSize = original.values.value(u"StepFontSize").toDouble(); stepSize->setValue(savedStepSize); const double initialStepSize = stepSize->value();
  form->addRow(QStringLiteral("Font size (pixels)"), stepSize);
  const QString savedStep = original.values.value(u"StepType").toString();
  auto *stepStyle = new QComboBox(dialog); stepStyle->setObjectName(QStringLiteral("defaultStepStyle")); stepStyle->setAccessibleName(QStringLiteral("Step style"));
  // EA-009: names are the ESET-008 preference spellings.
  for (const auto &entry : {std::pair{"Numeric", "1, 2, 3 ..."}, std::pair{"UppercaseLetter", "A, B, C ..."}, std::pair{"LowercaseLetter", "a, b, c ..."},
                            std::pair{"UppercaseRoman", "I, II, III ..."}, std::pair{"LowercaseRoman", "i, ii, iii ..."}})
    stepStyle->addItem(QStringLiteral("%1 (%2)").arg(QLatin1String(entry.first), QLatin1String(entry.second)), QLatin1String(entry.first));
  stepStyle->setCurrentIndex(std::max(0, stepStyle->findData(savedStep)));
  if (!stepStyleImplemented(savedStep)) {
    // Saved by a newer build: kept as-is, new steps stay numeric.
    stepStyle->addItem(savedStep + QStringLiteral(" (not available yet)"), savedStep); stepStyle->setCurrentIndex(stepStyle->count() - 1);
  }
  form->addRow(QStringLiteral("Style"), stepStyle);
  const auto textDraft = [](QJsonObject &changes, const TextControls &c) {
    changes.insert(c.familyKey, c.family->currentData().toString()); changes.insert(c.sizeKey, c.size->value() == c.initialSize ? c.savedSize : c.size->value());
    changes.insert(c.boldKey, c.bold->isChecked()); changes.insert(c.italicKey, c.italic->isChecked());
    changes.insert(c.alignKey, c.align->currentData().toString());
  };
  const auto resetText = [](const TextControls &c, const EditorPreferences &d) {
    c.family->setCurrentIndex(std::max(0, c.family->findData(d.values.value(c.familyKey).toString()))); c.size->setValue(d.values.value(c.sizeKey).toDouble());
    c.bold->setChecked(d.values.value(c.boldKey).toBool()); c.italic->setChecked(d.values.value(c.italicKey).toBool());
    c.align->setCurrentIndex(std::max(0, c.align->findData(d.values.value(c.alignKey).toString())));
  };
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
    thickness->setValue(defaults.values.value(u"Thickness").toInt()); if (!dialog) return;
    resetText(textControls, defaults); resetText(balloonControls, defaults);
    stepSize->setValue(defaults.values.value(u"StepFontSize").toDouble()); stepStyle->setCurrentIndex(0);
    if (dialog) error->clear();
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const QJsonObject base{{QStringLiteral("BorderColorHex"), stroke->text()},
        {QStringLiteral("FillColorHex"), fill->text()}, {QStringLiteral("Thickness"), thickness->value()}};
    QJsonObject changes = base;
    textDraft(changes, textControls); textDraft(changes, balloonControls);
    changes.insert(QStringLiteral("StepFontSize"), stepSize->value() == initialStepSize ? savedStepSize : stepSize->value()); changes.insert(QStringLiteral("StepType"), stepStyle->currentData().toString());
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
