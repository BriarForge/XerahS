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
#include <algorithm>
#include <cmath>

namespace xerahs::app {
using namespace xerahs::editor;
RectanglePropertiesDialog::RectanglePropertiesDialog(const RectangleAnnotation &r, QWidget *parent, Purpose purpose)
    : QDialog(parent), m_original{r.start, r.end, r.style}, m_purpose(purpose) {
  const bool creation = purpose == Purpose::Create;
  setObjectName(creation ? "rectangleCreationDialog" : "rectanglePropertiesDialog");
  setWindowTitle(creation ? "Create rectangle" : "Rectangle properties"); setAccessibleName(windowTitle());
  setWindowModality(Qt::WindowModal);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(creation
      ? "Enter bounds in source-image pixels. Bounds may extend outside the image; export clips them. "
        "Create selects one new rectangle and records one undo step. The active tool stays unchanged."
      : "Edit this rectangle in source-image pixels. Bounds may extend outside the image; export clips them. "
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
  if (!creation) {
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
    connect(picker, &QPushButton::clicked, this, [field = QPointer<QLineEdit>(field), value, label] {
      const auto current = parseColor(field->text());
      const QColor chosen = QColorDialog::getColor(QColor::fromRgba(current.value_or(value)), field->window(), "Choose " + label, QColorDialog::ShowAlphaChannel);
      if (field && chosen.isValid()) field->setText(formatColor(chosen.rgba()));
    });
  };
  if (!creation) {
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
  m_status = new QLabel(this); m_status->setObjectName("rectanglePropertiesStatus"); m_status->setAccessibleName("Rectangle validation result");
  m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  if (creation) { buttons->button(QDialogButtonBox::Ok)->setText("Create &rectangle"); buttons->button(QDialogButtonBox::Ok)->setObjectName("commitRectangleCreation"); }
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Ok)->setAutoDefault(false);
  connect(buttons, &QDialogButtonBox::accepted, this, &RectanglePropertiesDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(440, sizeHint().height()); m_fields["rectangleLeft"]->setFocus();
}
void RectanglePropertiesDialog::accept() {
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
