#include "PreferenceDialog.h"
#include "DrawingDefaultsDialog.h"
#include "NativeEditorPreferences.h"
#include "NativeEditorAppearance.h"
#include "image-editor/ShapePainter.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
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
#include <memory>
#include <vector>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
// CORE-009: every user-visible drawing default string goes through the translator under one context.
// Preference keys and the stored values they hold stay literal.
QString text(const char *source) { return QCoreApplication::translate("DrawingDefaultsDialog", source); }
}  // namespace
bool manageDrawingDefaults(QWidget *parent) {
  const auto original = nativeEditorPreferences();
  const auto revision = nativeEditorPreferenceRevision();
  QPointer<PreferenceDialog> dialog(new PreferenceDialog(parent)); dialog->setObjectName(QStringLiteral("drawingDefaultsDialog"));
  dialog->setWindowTitle(text("Drawing Defaults"));
  new NativeEditorAppearance(*dialog);
  auto *layout = new QVBoxLayout(dialog);
  auto *description = new QLabel(text("These defaults apply to new annotations in all open editors. Existing annotations stay unchanged."), dialog);
  description->setWordWrap(true); layout->addWidget(description);
  auto *form = new QFormLayout; layout->addLayout(form);
  const auto colour = [=](const QString &field, const QString &label, const QString &name) {
    auto *row = new QHBoxLayout;
    auto *input = new QLineEdit(original.values.value(field).toString(), dialog);
    input->setObjectName(name); input->setAccessibleName(label);
    input->setAccessibleDescription(text("Hexadecimal #RRGGBB or #AARRGGBB. The first pair in eight digits is alpha."));
    auto *picker = new QPushButton(text("Choose…"), dialog);
    picker->setObjectName(name + QStringLiteral("Picker")); picker->setAccessibleName(text("Choose %1").arg(label.toLower()));
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
  auto *stroke = colour(QStringLiteral("BorderColorHex"), text("Stroke colour"), QStringLiteral("defaultStrokeColour"));
  auto *fill = colour(QStringLiteral("FillColorHex"), text("Fill colour"), QStringLiteral("defaultFillColour"));
  auto *thickness = new QSpinBox(dialog); thickness->setObjectName(QStringLiteral("defaultStrokeThickness"));
  thickness->setAccessibleName(text("Stroke thickness in pixels")); thickness->setRange(1, 10000);
  thickness->setValue(original.values.value(u"Thickness").toInt()); form->addRow(text("Thickness (pixels)"), thickness);
  // ESET-006: shape decoration, shadow, cursor and emphasis/redaction creation defaults. Every number uses
  // the shared inclusive range (ESET-009) so the control, config load and automation agree.
  struct NumberControl { QString key; QDoubleSpinBox *spin; double saved, initial; };
  struct FlagControl { QString key; QCheckBox *box; };
  struct ChoiceControl { QString key; QComboBox *combo; };
  const auto numbers = std::make_shared<std::vector<NumberControl>>();
  const auto flags = std::make_shared<std::vector<FlagControl>>();
  const auto choices = std::make_shared<std::vector<ChoiceControl>>();
  const auto number = [=](const QString &key, const QString &label, const QString &name, const QString &unit) {
    const auto range = preferenceNumericRange(key);
    auto *spin = new QDoubleSpinBox(dialog); spin->setObjectName(name); spin->setAccessibleName(label + unit);
    spin->setDecimals(range && range->integer ? 0 : 4); if (range) spin->setRange(range->minimum, range->maximum);
    const double saved = original.values.value(key).toDouble(); spin->setValue(saved);
    numbers->push_back({key, spin, saved, spin->value()}); form->addRow(label + unit, spin);
  };
  const auto flag = [=](const QString &key, const QString &label, const QString &name) {
    auto *box = new QCheckBox(label, dialog); box->setObjectName(name); box->setChecked(original.values.value(key).toBool());
    flags->push_back({key, box}); form->addRow(box);
  };
  const auto choice = [=](const QString &key, const QString &label, const QString &name, const QStringList &entries) {
    auto *combo = new QComboBox(dialog); combo->setObjectName(name); combo->setAccessibleName(label);
    // The entries are the stored preference values, so they stay literal.
    for (const auto &entry : entries) combo->addItem(entry, entry);
    const QString saved = original.values.value(key).toString();
    if (combo->findData(saved) < 0) combo->addItem(saved + text(" (not available yet)"), saved);
    combo->setCurrentIndex(std::max(0, combo->findData(saved)));
    choices->push_back({key, combo}); form->addRow(label, combo);
  };
  auto *shapesHeading = new QLabel(text("Shapes, arrows and shadows"), dialog); form->addRow(shapesHeading);
  number(QStringLiteral("CornerRadius"), text("Rectangle corner radius"), QStringLiteral("defaultCornerRadius"), QStringLiteral(" (pixels)"));
  choice(QStringLiteral("BorderStyle"), text("Border style"), QStringLiteral("defaultBorderStyle"), {"Solid", "Dash", "Dot", "DashDot", "DashDotDot"});
  choice(QStringLiteral("ArrowStyle"), text("Arrow style"), QStringLiteral("defaultArrowStyle"), {"Classic", "Modern", "Double", "Basic", "Line"});
  flag(QStringLiteral("Shadow"), text("Drop shadow"), QStringLiteral("defaultShadow"));
  auto *shadowColourRow = new QHBoxLayout;
  auto *shadowColour = new QLineEdit(original.values.value(u"ShadowColorHex").toString(), dialog);
  shadowColour->setObjectName(QStringLiteral("defaultShadowColour")); shadowColour->setAccessibleName(text("Shadow colour"));
  shadowColour->setAccessibleDescription(text("Hexadecimal #RRGGBB or #AARRGGBB. The first pair in eight digits is alpha."));
  shadowColourRow->addWidget(shadowColour); form->addRow(text("Shadow colour"), shadowColourRow);
  number(QStringLiteral("ShadowBlurRadius"), text("Shadow blur"), QStringLiteral("defaultShadowBlur"), QStringLiteral(" (pixels)"));
  number(QStringLiteral("ShadowOpacity"), text("Shadow opacity"), QStringLiteral("defaultShadowOpacity"), QStringLiteral(" (0 to 1)"));
  number(QStringLiteral("ShadowOffsetX"), text("Shadow horizontal offset"), QStringLiteral("defaultShadowOffsetX"), QStringLiteral(" (pixels)"));
  number(QStringLiteral("ShadowOffsetY"), text("Shadow vertical offset"), QStringLiteral("defaultShadowOffsetY"), QStringLiteral(" (pixels)"));
  auto *cursorHeading = new QLabel(text("Cursor"), dialog); form->addRow(cursorHeading);
  choice(QStringLiteral("CursorType"), text("Cursor type"), QStringLiteral("defaultCursorType"), {"Default", "Arrow"});
  auto *emphasisHeading = new QLabel(text("Emphasis and redaction (0 keeps the tool's built-in strength)"), dialog); form->addRow(emphasisHeading);
  number(QStringLiteral("BlurStrength"), text("Blur strength"), QStringLiteral("defaultBlurStrength"), QStringLiteral(" (pixels)"));
  number(QStringLiteral("PixelateStrength"), text("Pixelate cell size"), QStringLiteral("defaultPixelateStrength"), QStringLiteral(" (pixels)"));
  number(QStringLiteral("MagnifierStrength"), text("Magnifier zoom"), QStringLiteral("defaultMagnifierStrength"), QStringLiteral(" (factor)"));
  flag(QStringLiteral("MagnifierEllipse"), text("Magnifier uses an elliptical region"), QStringLiteral("defaultMagnifierEllipse"));
  number(QStringLiteral("SpotlightStrength"), text("Spotlight dimming"), QStringLiteral("defaultSpotlightStrength"), QStringLiteral(" (percent)"));
  number(QStringLiteral("SpotlightBlur"), text("Spotlight blur of the dimmed area"), QStringLiteral("defaultSpotlightBlur"), QStringLiteral(" (pixels)"));
  flag(QStringLiteral("SpotlightEllipse"), text("Spotlight uses an elliptical region"), QStringLiteral("defaultSpotlightEllipse"));
  // ESET-008: Text, Speech Balloon and Step content defaults.
  struct TextControls { QString familyKey, sizeKey, boldKey, italicKey, alignKey; QComboBox *family; QDoubleSpinBox *size; QCheckBox *bold, *italic; QComboBox *align; double initialSize = 0; double savedSize = 0; };
  const auto textSection = [=](const QString &title, const QString &prefix, const QString &familyKey, const QString &sizeKey,
                               const QString &boldKey, const QString &italicKey, const QString &alignKey) {
    TextControls c{familyKey, sizeKey, boldKey, italicKey, alignKey, nullptr, nullptr, nullptr, nullptr, nullptr};
    auto *heading = new QLabel(title, dialog); heading->setTextFormat(Qt::PlainText); form->addRow(heading);
    c.family = new QComboBox(dialog); c.family->setObjectName(prefix + QStringLiteral("FontFamily"));
    c.family->setAccessibleName(title + text(" font family"));
    c.family->addItem(text("System default"), QString());
    for (const auto &name : QFontDatabase::families()) c.family->addItem(name, name);
    const QString saved = original.values.value(familyKey).toString();
    if (!saved.isEmpty() && c.family->findData(saved) < 0) {
      // The saved family stays selectable so Apply does not rewrite it by accident.
      c.family->insertItem(1, xerahs::editor::nativeFontFamilyAvailable(saved) ? saved : saved + text(" (not installed)"), saved);
    }
    c.family->setCurrentIndex(std::max(0, c.family->findData(saved)));
    form->addRow(text("Font family"), c.family);
    if (!saved.isEmpty() && !xerahs::editor::nativeFontFamilyAvailable(saved)) {
      auto *note = new QLabel(text("The saved font \"%1\" is not installed. New objects use the system default font until you choose a replacement.").arg(saved), dialog);
      note->setObjectName(prefix + QStringLiteral("FontUnavailable")); note->setWordWrap(true); note->setTextFormat(Qt::PlainText); form->addRow(note);
    }
    c.size = new QDoubleSpinBox(dialog); c.size->setObjectName(prefix + QStringLiteral("FontSize")); c.size->setAccessibleName(title + text(" font size in pixels"));
    c.size->setDecimals(4); c.size->setRange(1, 2000); c.savedSize = original.values.value(sizeKey).toDouble(24); c.size->setValue(c.savedSize); c.initialSize = c.size->value();
    form->addRow(text("Font size (pixels)"), c.size);
    c.bold = new QCheckBox(text("Bold"), dialog); c.bold->setObjectName(prefix + QStringLiteral("Bold")); c.bold->setChecked(original.values.value(boldKey).toBool());
    c.italic = new QCheckBox(text("Italic"), dialog); c.italic->setObjectName(prefix + QStringLiteral("Italic")); c.italic->setChecked(original.values.value(italicKey).toBool());
    form->addRow(c.bold); form->addRow(c.italic);
    c.align = new QComboBox(dialog); c.align->setObjectName(prefix + QStringLiteral("Alignment")); c.align->setAccessibleName(title + text(" horizontal alignment"));
    // The alignment names are the stored preference values, so the data stays literal.
    for (const auto *name : {"Left", "Center", "Right"}) c.align->addItem(text(name), QLatin1String(name));
    c.align->setCurrentIndex(std::max(0, c.align->findData(original.values.value(alignKey).toString())));
    form->addRow(text("Alignment"), c.align);
    return c;
  };
  const auto textControls = textSection(text("Text annotations"), QStringLiteral("defaultText"), QStringLiteral("TextFontFamily"),
      QStringLiteral("TextFontSize"), QStringLiteral("TextBold"), QStringLiteral("TextItalic"), QStringLiteral("TextHorizontalAlignment"));
  const auto balloonControls = textSection(text("Speech balloons"), QStringLiteral("defaultBalloon"), QStringLiteral("SpeechBalloonFontFamily"),
      QStringLiteral("SpeechBalloonFontSize"), QStringLiteral("SpeechBalloonTextBold"), QStringLiteral("SpeechBalloonTextItalic"), QStringLiteral("SpeechBalloonTextHorizontalAlignment"));
  auto *stepHeading = new QLabel(text("Step numbers"), dialog); form->addRow(stepHeading);
  auto *stepSize = new QDoubleSpinBox(dialog); stepSize->setObjectName(QStringLiteral("defaultStepFontSize"));
  stepSize->setAccessibleName(text("Step number font size in pixels; zero sizes it from the step")); stepSize->setDecimals(4);
  stepSize->setRange(0, 2000); stepSize->setSpecialValueText(text("Automatic")); const double savedStepSize = original.values.value(u"StepFontSize").toDouble(); stepSize->setValue(savedStepSize); const double initialStepSize = stepSize->value();
  form->addRow(text("Font size (pixels)"), stepSize);
  const QString savedStep = original.values.value(u"StepType").toString();
  auto *stepStyle = new QComboBox(dialog); stepStyle->setObjectName(QStringLiteral("defaultStepStyle")); stepStyle->setAccessibleName(text("Step style"));
  // EA-009: names are the ESET-008 preference spellings.
  for (const auto &entry : {std::pair{"Numeric", "1, 2, 3 ..."}, std::pair{"UppercaseLetter", "A, B, C ..."}, std::pair{"LowercaseLetter", "a, b, c ..."},
                            std::pair{"UppercaseRoman", "I, II, III ..."}, std::pair{"LowercaseRoman", "i, ii, iii ..."}})
    stepStyle->addItem(text("%1 (%2)").arg(QLatin1String(entry.first), QLatin1String(entry.second)), QLatin1String(entry.first));
  stepStyle->setCurrentIndex(std::max(0, stepStyle->findData(savedStep)));
  if (!stepStyleImplemented(savedStep)) {
    // Saved by a newer build: kept as-is, new steps stay numeric.
    stepStyle->addItem(savedStep + text(" (not available yet)"), savedStep); stepStyle->setCurrentIndex(stepStyle->count() - 1);
  }
  form->addRow(text("Style"), stepStyle);
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
  auto *format = new QLabel(text("Colours use #RRGGBB or #AARRGGBB. For example, #80FF0000 is half-transparent red."), dialog);
  format->setWordWrap(true); layout->addWidget(format);
  auto *error = new QLabel(dialog); error->setObjectName(QStringLiteral("drawingDefaultsError"));
  error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(text("Apply"));
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
    for (const auto &control : *numbers) control.spin->setValue(defaults.values.value(control.key).toDouble());
    for (const auto &control : *flags) control.box->setChecked(defaults.values.value(control.key).toBool());
    for (const auto &control : *choices) control.combo->setCurrentIndex(std::max(0, control.combo->findData(defaults.values.value(control.key).toString())));
    shadowColour->setText(defaults.values.value(u"ShadowColorHex").toString());
    stepSize->setValue(defaults.values.value(u"StepFontSize").toDouble()); stepStyle->setCurrentIndex(0);
    if (dialog) error->clear();
  });
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    const QJsonObject base{{QStringLiteral("BorderColorHex"), stroke->text()},
        {QStringLiteral("FillColorHex"), fill->text()}, {QStringLiteral("Thickness"), thickness->value()}};
    QJsonObject changes = base;
    textDraft(changes, textControls); textDraft(changes, balloonControls);
    for (const auto &control : *numbers) changes.insert(control.key, control.spin->value() == control.initial ? control.saved : control.spin->value());
    for (const auto &control : *flags) changes.insert(control.key, control.box->isChecked());
    for (const auto &control : *choices) changes.insert(control.key, control.combo->currentData().toString());
    changes.insert(QStringLiteral("ShadowColorHex"), shadowColour->text());
    changes.insert(QStringLiteral("StepFontSize"), stepSize->value() == initialStepSize ? savedStepSize : stepSize->value()); changes.insert(QStringLiteral("StepType"), stepStyle->currentData().toString());
    dialog->commit([changes, revision] { return applyDrawingDefaults(changes, revision); }, [=](const PreferenceWrite &written) {
      if (written.diagnostic == u"invalid-BorderColorHex" || written.diagnostic == u"invalid-FillColorHex" || written.diagnostic == u"invalid-ShadowColorHex") {
        error->setText(text("Enter stroke, fill and shadow colours as #RRGGBB or #AARRGGBB."));
        if (dialog) (written.diagnostic == u"invalid-BorderColorHex" ? stroke : written.diagnostic == u"invalid-FillColorHex" ? fill : shadowColour)->setFocus();
      } else error->setText(editorPreferenceDiagnosticMessage(written.diagnostic));
    });
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  return PreferenceDialog::run(dialog, parent);
}
}  // namespace xerahs::app
