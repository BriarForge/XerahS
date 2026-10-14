#include "RectanglePropertiesDialog.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <array>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;
RectanglePropertiesDialog::RectanglePropertiesDialog(const RectangleAnnotation &r, QWidget *parent)
    : QDialog(parent), m_original{r.start, r.end, r.style} {
  setObjectName("rectanglePropertiesDialog"); setWindowTitle("Rectangle properties"); setAccessibleName(windowTitle());
  setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel("Edit this rectangle in source-image pixels. Bounds may extend outside the image; export clips them. "
      "OK records one undo step. Creation defaults stay unchanged.", this);
  intro->setTextFormat(Qt::PlainText); intro->setWordWrap(true); layout->addWidget(intro);
  auto *form = new QFormLayout; layout->addLayout(form);
  const auto numeric = [&](const QString &key, const QString &label, double value) {
    auto *field = new QLineEdit(QString::number(value, 'g', 17), this); field->setObjectName(key);
    field->setAccessibleName(label); field->setAccessibleDescription("Finite number in source-image coordinates; use a decimal point.");
    form->addRow(label, field); m_fields.insert(key, field);
  };
  numeric("rectangleLeft", "Left", r.left()); numeric("rectangleTop", "Top", r.top());
  numeric("rectangleWidth", "Width", r.right() - r.left()); numeric("rectangleHeight", "Height", r.bottom() - r.top());
  numeric("rectangleStrokeWidth", "Stroke width", r.style.strokeWidth);
  numeric("rectangleRotation", "Clockwise rotation (degrees)", r.style.rotationDegrees);
  numeric("rectangleOpacity", "Opacity (0 to 1)", r.style.opacity);
  const auto color = [&](const QString &key, const QString &label, Argb value) {
    auto *row = new QWidget(this); auto *horizontal = new QHBoxLayout(row); horizontal->setContentsMargins(0, 0, 0, 0);
    auto *field = new QLineEdit(formatColor(value), row); field->setObjectName(key); field->setAccessibleName(label);
    field->setAccessibleDescription("Alpha, red, green and blue hexadecimal bytes: #AARRGGBB."); m_fields.insert(key, field);
    auto *picker = new QPushButton("Choose…", row); picker->setObjectName(key + "Picker"); picker->setAccessibleName("Choose " + label);
    picker->setAutoDefault(false); horizontal->addWidget(field); horizontal->addWidget(picker); form->addRow(label + " (#AARRGGBB)", row);
    connect(picker, &QPushButton::clicked, this, [field = QPointer<QLineEdit>(field), value, label] {
      const auto current = parseColor(field->text());
      const QColor chosen = QColorDialog::getColor(QColor::fromRgba(current.value_or(value)), field->window(), "Choose " + label, QColorDialog::ShowAlphaChannel);
      if (field && chosen.isValid()) field->setText(formatColor(chosen.rgba()));
    });
  };
  color("rectangleStrokeColor", "Stroke color", r.style.strokeColor); color("rectangleFillColor", "Fill color", r.style.fillColor);
  m_status = new QLabel(this); m_status->setObjectName("rectanglePropertiesStatus"); m_status->setAccessibleName("Rectangle validation result");
  m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
  connect(buttons, &QDialogButtonBox::accepted, this, &RectanglePropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(440, sizeHint().height()); m_fields["rectangleLeft"]->setFocus();
}
void RectanglePropertiesDialog::accept() {
  QMap<QString, double> numbers;
  for (const auto &key : {"rectangleLeft", "rectangleTop", "rectangleWidth", "rectangleHeight", "rectangleStrokeWidth", "rectangleRotation", "rectangleOpacity"}) {
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
  next.style.strokeWidth = numbers["rectangleStrokeWidth"]; next.style.rotationDegrees = numbers["rectangleRotation"]; next.style.opacity = numbers["rectangleOpacity"];
  const auto stroke = parseColor(m_fields["rectangleStrokeColor"]->text()), fill = parseColor(m_fields["rectangleFillColor"]->text());
  if (!stroke || !fill) { m_status->setText("Enter stroke and fill as #AARRGGBB colors. The document is unchanged."); return; }
  next.style.strokeColor = *stroke; next.style.fillColor = *fill;
  const auto invalid = rectanglePropertiesDiagnostic(next);
  if (!invalid.isEmpty()) { m_status->setText("Use positive finite width and height, stroke width above 0 and at most 10000, and opacity from 0 to 1. The document is unchanged."); return; }
  m_properties = next; QDialog::accept();
}
std::optional<RectangleProperties> chooseRectangleProperties(const RectangleAnnotation &source, QWidget *parent) {
  QPointer<RectanglePropertiesDialog> dialog = new RectanglePropertiesDialog(source, parent);
  const int result = dialog->exec();
  if (!dialog) return {};
  const auto properties = result == QDialog::Accepted ? dialog->properties() : std::nullopt;
  delete dialog; return properties;
}
} // namespace xerahs::app
