#include "RectanglePropertiesDialog.h"
#include "AnnotationColorDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <array>
#include <algorithm>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;
RectanglePropertiesDialog::RectanglePropertiesDialog(const RectangleAnnotation &r, QWidget *parent, Purpose purpose)
    : QDialog(parent), m_source(r), m_original{r.start, r.end, r.style}, m_purpose(purpose) {
  const bool creation = purpose == Purpose::Create, defaults = purpose == Purpose::Defaults;
  setObjectName(defaults ? "shapeStyleDefaultsDialog" : creation ? "rectangleCreationDialog" : "rectanglePropertiesDialog");
  setWindowTitle(defaults ? "Shape style defaults" : creation ? "Create rectangle" : r.type() == shape::ellipse ? "Ellipse properties" : r.type() == shape::line ? "Line properties" : r.type() == shape::arrow ? "Arrow properties" : "Rectangle properties");
  setAccessibleName(windowTitle());
  setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(defaults
      ? "Border style, corner radius, arrow style and shadow for new rectangles, ellipses, lines and arrows. Existing objects and history stay unchanged. Apply is not an undo step."
      : creation
      ? "Enter bounds in source-image pixels. Bounds may extend outside the image; export clips them. "
        "Create selects one new rectangle and records one undo step. The active tool stays unchanged."
      : "Edit this object in source-image pixels. Bounds may extend outside the image; export clips them. "
        "OK records one undo step. Creation defaults stay unchanged.", this);
  intro->setTextFormat(Qt::PlainText); intro->setWordWrap(true); layout->addWidget(intro);
  auto *form = new QFormLayout; layout->addLayout(form);
  const auto numeric = [&](const QString &key, const QString &label, double value) {
    auto *field = new QLineEdit(QString::number(value, 'g', 17), this); field->setObjectName(key);
    field->setAccessibleName(label); field->setAccessibleDescription("Finite number in source-image coordinates; use a decimal point.");
    form->addRow(label, field); m_fields.insert(key, field);
  };
  if (!defaults) {
    numeric("rectangleLeft", "Left", r.left()); numeric("rectangleTop", "Top", r.top());
    numeric("rectangleWidth", "Width", r.right() - r.left()); numeric("rectangleHeight", "Height", r.bottom() - r.top());
  }
  if (!creation && !defaults) {
    numeric("rectangleStrokeWidth", "Stroke width", r.style.strokeWidth);
    numeric("rectangleRotation", "Clockwise rotation (degrees)", r.style.rotationDegrees);
    numeric("rectangleOpacity", "Opacity (0 to 1)", r.style.opacity);
  }
  const auto color = [&](const QString &key, const QString &label, Argb value) {
    auto *row = new QWidget(this); auto *horizontal = new QHBoxLayout(row); horizontal->setContentsMargins(0, 0, 0, 0);
    auto *field = new QLineEdit(formatColor(value), row); field->setObjectName(key); field->setAccessibleName(label);
    field->setAccessibleDescription("Alpha, red, green and blue hexadecimal bytes: #AARRGGBB."); m_fields.insert(key, field);
    auto *picker = new QPushButton("Choose…", row); picker->setObjectName(key + "Picker"); picker->setAccessibleName("Choose " + label);
    picker->setAutoDefault(false); horizontal->addWidget(field); horizontal->addWidget(picker); form->addRow(label + " (#AARRGGBB)", row);
    connect(picker, &QPushButton::clicked, this, [owner = QPointer<RectanglePropertiesDialog>(this), field = QPointer<QLineEdit>(field), value, key] {
      if (!owner || !field) return;
      const auto current = parseColor(field->text());
      const auto target = key == "rectangleStrokeColor" ? EditorSession::ColorTarget::Stroke : EditorSession::ColorTarget::Fill;
      const auto chosen = chooseAnnotationColor(target, current.value_or(value),
          "This colour changes only this rectangle properties draft. Choose OK in Rectangle properties to apply it. Creation defaults stay unchanged.", owner);
      if (!owner || !field) return;
      if (chosen.color) field->setText(formatColor(*chosen.color));
      if (!chosen.diagnostic.isEmpty()) owner->m_status->setText("The colour could not be selected. The properties draft was kept.");
    });
  };
  if (defaults) {
    // decoration rows only
  } else if (!creation) {
    color("rectangleStrokeColor", "Stroke color", r.style.strokeColor); color("rectangleFillColor", "Fill color", r.style.fillColor);
  } else {
    auto *style = new QLabel(QString("Creation style captured when this dialog opened: stroke %1, fill %2, stroke width %3, "
        "clockwise rotation %4°, opacity %5. To change it, cancel and edit Drawing Defaults first.")
        .arg(formatColor(r.style.strokeColor), formatColor(r.style.fillColor), QString::number(r.style.strokeWidth, 'g', 17),
             QString::number(r.style.rotationDegrees, 'g', 17), QString::number(r.style.opacity, 'g', 17)), this);
    style->setObjectName("rectangleCreationStyle"); style->setAccessibleName("Captured creation style");
    style->setAccessibleDescription(style->text());
    style->setTextFormat(Qt::PlainText); style->setWordWrap(true); layout->addWidget(style);
  }
  if (defaults || (!creation && hasShapeDecoration(r.type()))) addDecorationRows(form, r);
  m_status = new QLabel(this); m_status->setObjectName("rectanglePropertiesStatus"); m_status->setAccessibleName("Rectangle validation result");
  m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  if (creation) { buttons->button(QDialogButtonBox::Ok)->setText("Create &rectangle"); buttons->button(QDialogButtonBox::Ok)->setObjectName("commitRectangleCreation"); }
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
  connect(buttons, &QDialogButtonBox::accepted, this, &RectanglePropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  if (defaults) buttons->button(QDialogButtonBox::Ok)->setText("&Apply");
  resize(440, sizeHint().height());
  if (defaults) m_borderStyle->setFocus(); else m_fields["rectangleLeft"]->setFocus();
}
void RectanglePropertiesDialog::addDecorationRows(QFormLayout *form, const RectangleAnnotation &r) {
  const auto &st = r.style;
  const auto numeric = [&](const QString &key, const QString &label, double value, const QString &hint) {
    auto *field = new QLineEdit(QString::number(value, 'g', 17), this); field->setObjectName(key);
    field->setAccessibleName(label); field->setAccessibleDescription(hint); field->setToolTip(hint);
    form->addRow(label, field); m_fields.insert(key, field);
  };
  m_borderStyle = new QComboBox(this); m_borderStyle->setObjectName("shapeBorderStyle"); m_borderStyle->setAccessibleName("Border style");
  for (const auto style : {BorderStyle::Solid, BorderStyle::Dash, BorderStyle::Dot, BorderStyle::DashDot, BorderStyle::DashDotDot})
    m_borderStyle->addItem(borderStyleName(style), int(style));
  m_borderStyle->setCurrentIndex(m_borderStyle->findData(int(st.borderStyle)));
  form->addRow("Border style", m_borderStyle);
  if (r.type() == shape::arrow || m_purpose == Purpose::Defaults) {
    m_arrowStyle = new QComboBox(this); m_arrowStyle->setObjectName("shapeArrowStyle"); m_arrowStyle->setAccessibleName("Arrow style");
    for (const auto arrow : {ArrowStyle::Classic, ArrowStyle::Modern, ArrowStyle::Double, ArrowStyle::Basic, ArrowStyle::Line})
      m_arrowStyle->addItem(arrowStyleName(arrow), int(arrow));
    m_arrowStyle->setCurrentIndex(m_arrowStyle->findData(int(st.arrowStyle)));
    form->addRow("Arrow style", m_arrowStyle);
  }
  if ((r.type() == shape::line || r.type() == shape::arrow) && m_purpose == Purpose::Edit && r.content.points.size() >= 2 && r.content.points.size() <= 3) {
    // EA-006: the curve passes through this point at its middle. Modern arrows stay straight.
    const PointF s = lineStart(r), e = lineEnd(r);
    const auto anchor = lineAnchor(r).value_or(PointF{(s.x + e.x) / 2 - (e.y - s.y) * 0.2, (s.y + e.y) / 2 + (e.x - s.x) * 0.2});
    m_curve = new QCheckBox("Curved (Modern arrows stay straight)", this); m_curve->setObjectName("shapeCurve"); m_curve->setAccessibleName("Curved");
    m_curve->setChecked(lineAnchor(r).has_value()); form->addRow(m_curve);
    numeric("shapeCurveX", "Curve passes through X (pixels)", anchor.x, "Document X of the point the curve passes through at its middle.");
    numeric("shapeCurveY", "Curve passes through Y (pixels)", anchor.y, "Document Y of the point the curve passes through at its middle.");
  }
  m_hasCorner = r.type() == shape::rectangle || m_purpose == Purpose::Defaults;
  if (m_hasCorner) numeric("shapeCornerRadius", "Corner radius (pixels)", st.cornerRadius, "Rectangle corner radius from 0 to 10000 pixels; clamped to half the shorter side when drawn. Ellipses ignore it.");
  m_shadow = new QCheckBox("Drop shadow", this); m_shadow->setObjectName("shapeShadow"); m_shadow->setAccessibleName("Drop shadow");
  m_shadow->setChecked(st.shadow.enabled); form->addRow(m_shadow);
  numeric("shapeShadowBlur", "Shadow blur radius (pixels)", st.shadow.blurRadius, "0 to 1000 pixels.");
  numeric("shapeShadowOffsetX", "Shadow offset X (pixels)", st.shadow.offsetX, "-10000 to 10000 pixels; positive is right.");
  numeric("shapeShadowOffsetY", "Shadow offset Y (pixels)", st.shadow.offsetY, "-10000 to 10000 pixels; positive is down.");
  numeric("shapeShadowOpacity", "Shadow opacity (0 to 1)", st.shadow.opacity, "0 to 1, multiplied with the shadow colour alpha.");
  auto *c = new QLineEdit(formatColor(st.shadow.color), this); c->setObjectName("shapeShadowColor"); c->setAccessibleName("Shadow color in #AARRGGBB format");
  form->addRow("Shadow color (#AARRGGBB)", c); m_fields.insert("shapeShadowColor", c);
}
bool RectanglePropertiesDialog::readDecoration(RectangleStyle &style) {
  const auto number = [&](const char *key, double &out) {
    bool ok = false; const double v = m_fields[key]->text().toDouble(&ok);
    if (!ok || !std::isfinite(v)) { m_fields[key]->setFocus(); return false; }
    out = v; return true;
  };
  style.borderStyle = BorderStyle(m_borderStyle->currentData().toInt());
  if (m_arrowStyle) style.arrowStyle = ArrowStyle(m_arrowStyle->currentData().toInt());
  if ((m_hasCorner && !number("shapeCornerRadius", style.cornerRadius)) || !number("shapeShadowBlur", style.shadow.blurRadius) ||
      !number("shapeShadowOffsetX", style.shadow.offsetX) || !number("shapeShadowOffsetY", style.shadow.offsetY) ||
      !number("shapeShadowOpacity", style.shadow.opacity)) {
    m_status->setText("Enter finite numbers using a decimal point for corner radius and shadow. The document is unchanged."); return false;
  }
  const auto color = parseColor(m_fields["shapeShadowColor"]->text());
  if (!color) { m_status->setText("Enter the shadow colour as #AARRGGBB. The document is unchanged."); m_fields["shapeShadowColor"]->setFocus(); return false; }
  style.shadow.color = *color; style.shadow.enabled = m_shadow->isChecked();
  if (!validDecoration(shape::rectangle, style)) {
    m_status->setText("Use corner radius 0 to 10000, blur 0 to 1000, offsets within 10000 and shadow opacity 0 to 1. The document is unchanged."); return false;
  }
  return true;
}
void RectanglePropertiesDialog::accept() {
  if (m_purpose == Purpose::Defaults) {
    RectangleStyle next = m_original.style;
    if (!readDecoration(next)) return;
    m_original.style = next; m_properties = m_original; QDialog::accept(); return;
  }
  QMap<QString, double> numbers;
  const QStringList keys = m_purpose == Purpose::Create
      ? QStringList{"rectangleLeft", "rectangleTop", "rectangleWidth", "rectangleHeight"}
      : QStringList{"rectangleLeft", "rectangleTop", "rectangleWidth", "rectangleHeight", "rectangleStrokeWidth", "rectangleRotation", "rectangleOpacity"};
  for (const auto &key : keys) {
    bool ok = false; const double value = m_fields[key]->text().toDouble(&ok);
    if (!ok || !std::isfinite(value)) { m_status->setText("Enter finite numbers using a decimal point. The document is unchanged."); m_fields[key]->setFocus(); return; }
    numbers.insert(key, value);
  }
  RectangleProperties next = m_original;
  next.start = {numbers["rectangleLeft"], numbers["rectangleTop"]};
  // Preserve the exact imported endpoints when their position and extent were
  // left unchanged; subtracting and re-adding an extent may round differently.
  next.end.x = next.start.x == m_original.start.x && numbers["rectangleWidth"] == m_original.end.x - m_original.start.x
      ? m_original.end.x : next.start.x + numbers["rectangleWidth"];
  next.end.y = next.start.y == m_original.start.y && numbers["rectangleHeight"] == m_original.end.y - m_original.start.y
      ? m_original.end.y : next.start.y + numbers["rectangleHeight"];
  if (m_purpose == Purpose::Edit) {
    next.style.strokeWidth = numbers["rectangleStrokeWidth"]; next.style.rotationDegrees = numbers["rectangleRotation"]; next.style.opacity = numbers["rectangleOpacity"];
    const auto stroke = parseColor(m_fields["rectangleStrokeColor"]->text()), fill = parseColor(m_fields["rectangleFillColor"]->text());
    if (!stroke || !fill) { m_status->setText("Enter stroke and fill as #AARRGGBB colors. The document is unchanged."); return; }
    next.style.strokeColor = *stroke; next.style.fillColor = *fill;
    if (m_borderStyle && !readDecoration(next.style)) return;
    if (m_curve) {
      bool okX = false, okY = false;
      const double x = m_fields["shapeCurveX"]->text().toDouble(&okX), y = m_fields["shapeCurveY"]->text().toDouble(&okY);
      if (!okX || !okY || !std::isfinite(x) || !std::isfinite(y)) { m_status->setText("Enter finite curve coordinates using a decimal point. The document is unchanged."); return; }
      const bool modern = m_source.type() == shape::arrow && next.style.arrowStyle == ArrowStyle::Modern;
      const bool wantCurve = m_curve->isChecked() && !modern;
      const auto before = lineAnchor(m_source);
      if (wantCurve != before.has_value() || (wantCurve && !(*before == PointF{x, y}))) {
        RectangleAnnotation draft = m_source; draft.start = next.start; draft.end = next.end; draft.style = next.style;
        if (!setLineCurve(draft, wantCurve ? std::optional<PointF>(PointF{x, y}) : std::nullopt)) {
          m_status->setText("The curve point is out of range. The document is unchanged."); return;
        }
        next.start = draft.start; next.end = draft.end; next.points = draft.content.points;
      }
    }
  }
  const auto invalid = rectanglePropertiesDiagnostic(next);
  if (!invalid.isEmpty()) {
    m_status->setText(m_purpose == Purpose::Create ? "Use positive width and height with finite bounds. The document is unchanged."
        : "Use positive finite width and height, stroke width above 0 and at most 10000, and opacity from 0 to 1. The document is unchanged."); return;
  }
  m_properties = next; QDialog::accept();
}
namespace {
std::optional<RectangleProperties> chooseRectangle(const RectangleAnnotation &source, QWidget *parent, RectanglePropertiesDialog::Purpose purpose) {
  QPointer<RectanglePropertiesDialog> dialog = new RectanglePropertiesDialog(source, parent, purpose);
  const int result = dialog->exec();
  if (!dialog) return {};
  const auto properties = result == QDialog::Accepted ? dialog->properties() : std::nullopt;
  delete dialog; return properties;
}
}
std::optional<RectangleProperties> chooseRectangleProperties(const RectangleAnnotation &source, QWidget *parent) {
  return chooseRectangle(source, parent, RectanglePropertiesDialog::Purpose::Edit);
}
std::optional<RectangleProperties> chooseRectangleCreation(const RectangleStyle &style, QSize imageSize, QWidget *parent) {
  RectangleAnnotation draft; draft.start = {0, 0};
  draft.end = {double(std::min(100, imageSize.width())), double(std::min(100, imageSize.height()))}; draft.style = style;
  return chooseRectangle(draft, parent, RectanglePropertiesDialog::Purpose::Create);
}
} // namespace xerahs::app
namespace xerahs::app {
std::optional<RectangleStyle> chooseDecorationDefaults(const RectangleStyle &style, QWidget *parent) {
  RectangleAnnotation draft; draft.start = {0, 0}; draft.end = {1, 1}; draft.style = style;
  const auto properties = chooseRectangle(draft, parent, RectanglePropertiesDialog::Purpose::Defaults);
  if (!properties) return std::nullopt;
  return properties->style;
}
} // namespace xerahs::app
