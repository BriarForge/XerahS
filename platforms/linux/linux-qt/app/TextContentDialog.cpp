#include "TextContentDialog.h"
#include "image-editor/ShapePainter.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
namespace xerahs::app {
TextContentDialog::TextContentDialog(const xerahs::editor::RectangleAnnotation &shape, QWidget *parent)
    : QDialog(parent), m_shape(shape) {
  const bool balloon = shape.type() == xerahs::editor::shape::speechBalloon;
  setObjectName(balloon ? "balloonContentDialog" : "textContentDialog"); setWindowTitle(balloon ? "Speech balloon text and tail" : "Text annotation"); setAccessibleName(windowTitle());
  setAttribute(Qt::WA_DeleteOnClose); setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this); auto *form = new QFormLayout; layout->addLayout(form);
  m_text = new QPlainTextEdit(shape.content.text, this); m_text->setObjectName("balloonText"); m_text->setAccessibleName(balloon ? "Balloon text" : "Annotation text"); form->addRow("&Text", m_text);
  m_family = new QComboBox(this); m_family->setObjectName("annotationFontFamily"); m_family->setAccessibleName("Font family; empty uses the native default");
  m_family->setEditable(true); m_family->setInsertPolicy(QComboBox::NoInsert); m_family->addItem(QString{}); m_family->addItems(QFontDatabase::families());
  m_family->lineEdit()->setMaxLength(xerahs::editor::kMaxTextLength);
  m_family->lineEdit()->setPlaceholderText("Native default (leave empty)"); m_family->setEditText(shape.content.fontFamily); form->addRow("Font &family", m_family);
  auto *familyStatus = new QLabel(this); familyStatus->setObjectName("annotationFontFamilyStatus"); familyStatus->setTextFormat(Qt::PlainText); familyStatus->setWordWrap(true); form->addRow(familyStatus);
  const auto describeFamily = [this, familyStatus] {
    const auto family = m_family->currentText();
    familyStatus->setText(family.isEmpty() ? "Native default font." : QFontDatabase::hasFamily(family) ? "Installed native font." :
      "font-family-unavailable: This font is unavailable. Text uses the native default. Its saved family stays unchanged until you apply a replacement.");
  };
  connect(m_family, &QComboBox::currentTextChanged, this, describeFamily); describeFamily();
  m_size = new QDoubleSpinBox(this); m_size->setObjectName("balloonFontSize"); m_size->setAccessibleName("Font size in pixels"); m_size->setDecimals(17); m_size->setRange(1, 2000);
  m_size->setValue(xerahs::editor::shapeFontSize(shape)); m_initialSize = m_size->value(); form->addRow("Font &size (pixels)", m_size);
  m_size->setToolTip("Enter a fractional size from 1 to 2000 pixels. Unchanged Apply keeps the saved size or native default.");
  if (shape.content.fontSize > 0 && shape.content.fontSize != m_initialSize) {
    auto *sizeStatus = new QLabel(QString("Saved font size: %1 pixels. The control edits 1 to 2000 pixels; unchanged Apply keeps the saved value.").arg(QString::number(shape.content.fontSize, 'g', 17)), this);
    sizeStatus->setObjectName("annotationFontSizeStatus"); sizeStatus->setTextFormat(Qt::PlainText); sizeStatus->setWordWrap(true); form->addRow(sizeStatus);
  }
  m_alignment = new QComboBox(this); m_alignment->setObjectName("annotationTextAlignment"); m_alignment->setAccessibleName("Horizontal text alignment");
  m_alignment->addItem(balloon ? "Default (center)" : "Default (left)", "");
  m_alignment->addItem("Left", "left"); m_alignment->addItem("Center", "center"); m_alignment->addItem("Right", "right");
  m_alignment->setCurrentIndex(m_alignment->findData(shape.content.textAlignment)); form->addRow("Horizontal &alignment", m_alignment);
  const auto styleControl = [this, form](const QString &label, const QString &name, std::optional<bool> value) {
    auto *control = new QCheckBox(label, this); control->setObjectName(name); control->setAccessibleName(QString(label).remove(QLatin1Char('&')));
    control->setTristate(true); control->setCheckState(value.has_value() ? (*value ? Qt::Checked : Qt::Unchecked) : Qt::PartiallyChecked);
    control->setToolTip("Partially checked uses the native font style."); form->addRow(control); return control;
  };
  m_bold = styleControl("&Bold", "annotationTextBold", shape.content.textBold);
  m_italic = styleControl("&Italic", "annotationTextItalic", shape.content.textItalic);
  m_color = new QLineEdit(this); m_color->setObjectName("annotationTextColor");
  m_color->setAccessibleName("Text color in #AARRGGBB format");
  m_color->setPlaceholderText(balloon ? "Automatic contrast (leave empty)" : "Stroke color (leave empty)");
  m_color->setToolTip("Enter #AARRGGBB, including alpha. Leave empty to use the original text-color rule. This changes text only.");
  if (shape.content.textColor.has_value()) m_color->setText(xerahs::editor::formatColor(*shape.content.textColor));
  form->addRow("Text &color (#AARRGGBB)", m_color);
  m_fillColor = new QLineEdit(this); m_fillColor->setObjectName("annotationTextFillColor");
  m_fillColor->setAccessibleName("Text background fill color in #AARRGGBB format");
  m_fillColor->setPlaceholderText("Original fill (leave empty)");
  m_fillColor->setToolTip("Enter #AARRGGBB, including alpha. Zero alpha is transparent. Leave empty to use the original fill rule. This changes the fill only.");
  if (shape.content.textFillColor.has_value()) m_fillColor->setText(xerahs::editor::formatColor(*shape.content.textFillColor));
  form->addRow("Background &fill (#AARRGGBB)", m_fillColor);
  if (balloon) {
    m_borderColor = new QLineEdit(this); m_borderColor->setObjectName("annotationBalloonBorderColor");
    m_borderColor->setAccessibleName("Speech balloon border color in #AARRGGBB format");
    m_borderColor->setPlaceholderText("Original stroke color (leave empty)");
    m_borderColor->setToolTip("Enter #AARRGGBB, including alpha. Zero alpha is transparent. Leave empty to use the original stroke color. This changes the balloon border only.");
    if (shape.content.balloonBorderColor.has_value()) m_borderColor->setText(xerahs::editor::formatColor(*shape.content.balloonBorderColor));
    form->addRow("Balloon &border (#AARRGGBB)", m_borderColor);
    m_tail = new QCheckBox("Show balloon &tail", this); m_tail->setObjectName("balloonTailEnabled"); m_tail->setAccessibleName("Show balloon tail"); m_tail->setChecked(!shape.content.points.empty()); form->addRow(m_tail);
    m_x = new QLineEdit(this); m_y = new QLineEdit(this); m_x->setObjectName("balloonTailX"); m_y->setObjectName("balloonTailY");
    m_x->setAccessibleName("Tail tip X as a fraction of balloon width"); m_y->setAccessibleName("Tail tip Y as a fraction of balloon height");
    for (auto *edit : {m_x, m_y}) edit->setMaxLength(128);
    if (!shape.content.points.empty()) { m_x->setText(QString::number(shape.content.points[0].x, 'g', 17)); m_y->setText(QString::number(shape.content.points[0].y, 'g', 17)); }
    form->addRow("Tip &X (fraction of width)", m_x); form->addRow("Tip &Y (fraction of height)", m_y);
    auto sync = [this] { m_x->setEnabled(m_tail->isChecked()); m_y->setEnabled(m_tail->isChecked()); };
    connect(m_tail, &QCheckBox::toggled, this, sync); sync();
    auto *note = new QLabel("Tip coordinates are relative to the unrotated balloon: (0, 0) is its top-left, (1, 1) its bottom-right. Values outside the box extend the tail. Enter finite numbers using a decimal point. Apply changes this object only; Cancel keeps it unchanged.", this);
    note->setWordWrap(true); layout->addWidget(note);
  }
  m_error = new QLabel(this); m_error->setObjectName("balloonContentError"); m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); layout->addWidget(m_error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this); layout->addWidget(buttons);
  buttons->button(QDialogButtonBox::Apply)->setAutoDefault(false); buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &TextContentDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); resize(560, 430);
}
void TextContentDialog::accept() {
  auto content = m_shape.content; content.text = m_text->toPlainText();
  if (content.text.isEmpty() || content.text.size() > xerahs::editor::kMaxTextLength) {
    m_error->setText("annotation-style-invalid: Enter non-empty text within the supported text limit."); return;
  }
  // Keep the zero/default representation (and fractional sizes from compatible
  // files) unless the user actually changes the displayed size.
  if (m_size->value() != m_initialSize) content.fontSize = m_size->value();
  const auto family = m_family->currentText();
  if (!xerahs::editor::validFontFamily(family) || (!family.isEmpty() && family != m_shape.content.fontFamily && !QFontDatabase::hasFamily(family))) {
    m_error->setText("font-family-unavailable: Choose an installed font family or clear the field to use the native default."); m_family->setFocus(); return;
  }
  content.fontFamily = family;
  content.textAlignment = m_alignment->currentData().toString();
  const auto chosenStyle = [](QCheckBox *control) -> std::optional<bool> {
    return control->checkState() == Qt::PartiallyChecked ? std::nullopt : std::optional<bool>(control->isChecked());
  };
  content.textBold = chosenStyle(m_bold); content.textItalic = chosenStyle(m_italic);
  const auto colorText = m_color->text().trimmed(); content.textColor.reset();
  if (!colorText.isEmpty()) {
    content.textColor = xerahs::editor::parseColor(colorText);
    if (!content.textColor.has_value()) {
      m_error->setText("annotation-text-color-invalid: Enter #AARRGGBB including alpha, or leave the field empty for the default text color."); m_color->setFocus(); return;
    }
  }
  const auto fillText = m_fillColor->text().trimmed(); content.textFillColor.reset();
  if (!fillText.isEmpty()) {
    content.textFillColor = xerahs::editor::parseColor(fillText);
    if (!content.textFillColor.has_value()) {
      m_error->setText("annotation-text-fill-color-invalid: Enter #AARRGGBB including alpha, or leave the field empty for the original fill."); m_fillColor->setFocus(); return;
    }
  }
  if (m_borderColor) {
    const auto borderText = m_borderColor->text().trimmed(); content.balloonBorderColor.reset();
    if (!borderText.isEmpty()) {
      content.balloonBorderColor = xerahs::editor::parseColor(borderText);
      if (!content.balloonBorderColor.has_value()) {
        m_error->setText("annotation-balloon-border-color-invalid: Enter #AARRGGBB including alpha, or leave the field empty for the original stroke color."); m_borderColor->setFocus(); return;
      }
    }
  }
  if (m_tail && m_tail->isChecked()) {
    bool xOk = false, yOk = false; const double x = QLocale::c().toDouble(m_x->text().trimmed(), &xOk), y = QLocale::c().toDouble(m_y->text().trimmed(), &yOk);
    if (!xOk || !yOk) {
      m_error->setText("balloon-tail-invalid: Enter finite tip coordinates that map to a finite image position, or turn off the tail."); return;
    }
    if (content.points.empty()) content.points.push_back({x, y}); else content.points[0] = {x, y};
    if (!xerahs::editor::validBalloonTail(m_shape, content.points)) {
      m_error->setText("balloon-tail-invalid: Enter finite tip coordinates that map to a finite image position, or turn off the tail."); return;
    }
  } else if (m_tail) content.points.clear();
  m_choice = std::move(content); QDialog::accept();
}
}
