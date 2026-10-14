#include "SelectionRotationDialog.h"
#include "NativeAnnotationTools.h"
#include "image-editor/RectangleGeometry.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <cmath>
#include <new>
namespace xerahs::app {
using namespace xerahs::editor;
SelectionRotationDialog::SelectionRotationDialog(AnnotationDocument document, QSet<QUuid> selection, QWidget *parent)
    : QDialog(parent), m_document(std::move(document)), m_selection(std::move(selection)) {
  setObjectName("selectionRotationDialog"); setWindowTitle("Rotate selection"); setAccessibleName(windowTitle());
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel("Add clockwise degrees to each selected rectangle about its own bounds centre. Negative angles turn counter-clockwise. "
      "The document changes only when you choose Rotate; source pixels, bounds and other styles stay unchanged.", this);
  intro->setTextFormat(Qt::PlainText); intro->setWordWrap(true); layout->addWidget(intro);
  auto *form = new QFormLayout; layout->addLayout(form);
  m_angle = new QLineEdit("0", this); m_angle->setObjectName("selectionRotationDegrees"); m_angle->setMaxLength(128);
  m_angle->setAccessibleName("Clockwise degrees to add"); m_angle->setAccessibleDescription("Enter a finite number using a decimal point. Negative turns counter-clockwise. The resulting angles are normalized to 0 through less than 360 degrees.");
  form->addRow("&Angle in degrees:", m_angle);
  m_status = new QLabel(this); m_status->setObjectName("selectionRotationStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  m_preview = new QPlainTextEdit(this); m_preview->setObjectName("selectionRotationPreview"); m_preview->setReadOnly(true);
  m_preview->setAccessibleName("Selected object rotation preview"); m_preview->setAccessibleDescription("Each selected object UUID, its original angle, and its resulting angle. Unsupported objects remain unchanged."); layout->addWidget(m_preview, 1);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this); m_rotate = buttons->addButton("&Rotate", QDialogButtonBox::AcceptRole);
  m_rotate->setObjectName("applySelectionRotation"); m_rotate->setAutoDefault(false); buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  connect(m_angle, &QLineEdit::textChanged, this, &SelectionRotationDialog::refreshPreview);
  connect(buttons, &QDialogButtonBox::accepted, this, &SelectionRotationDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  resize(580, 420); refreshPreview(); m_angle->selectAll(); m_angle->setFocus();
}
void SelectionRotationDialog::refreshPreview() {
  const QPointer<SelectionRotationDialog> owner(this); const QString input = m_angle->text(); m_previewDegrees.reset();
  QString diagnostic, preview; int rectangles = 0, unsupported = 0; bool angleInvalid = false;
  try {
    bool parsed = false; const double degrees = QLocale::c().toDouble(input.trimmed(), &parsed);
    if (!parsed || !std::isfinite(degrees)) { diagnostic = "annotation-geometry-invalid"; angleInvalid = true; }
    QStringList rows;
    if (diagnostic.isEmpty()) for (const auto &a : m_document.annotations) if (m_selection.contains(annotationId(a))) {
      if (!annotationEditable(a)) { diagnostic = "annotation-selection-protected"; break; }
      const auto id = annotationId(a).toString(QUuid::WithoutBraces);
      if (const auto *r = std::get_if<RectangleAnnotation>(&a)) {
        const auto prepared = rotateRectangleGeometry({r->start, r->end, r->style}, degrees);
        if (!prepared.properties) { diagnostic = prepared.diagnostic; break; }
        ++rectangles; rows << QString("Rectangle %1: %2° → %3°").arg(id, QString::number(r->style.rotationDegrees, 'g', 17), QString::number(prepared.properties->style.rotationDegrees, 'g', 17));
      } else { ++unsupported; rows << QString("Unsupported object %1: unchanged").arg(id); }
    }
    if (diagnostic.isEmpty() && !rectangles) diagnostic = "annotation-type-unsupported";
    if (diagnostic.isEmpty()) { preview = rows.join('\n'); m_previewDegrees = degrees; }
  } catch (const std::bad_alloc &) { diagnostic = "annotation-edit-failed"; }
  m_rotate->setEnabled(diagnostic.isEmpty()); if (!owner || m_angle->text() != input) return;
  m_status->setText(diagnostic.isEmpty() ? QString("%1 rectangle(s) will rotate; %2 unsupported object(s) stay unchanged. One undo step after Rotate.").arg(rectangles).arg(unsupported)
      : (angleInvalid ? QStringLiteral("Enter a finite clockwise angle using a decimal point. No objects will rotate.") : annotationEditMessage(diagnostic))); if (!owner || m_angle->text() != input) return;
  m_preview->setPlainText(diagnostic.isEmpty() ? preview : "No rotation will be applied. Correct the angle or cancel.");
}
void SelectionRotationDialog::accept() {
  const QPointer<SelectionRotationDialog> owner(this); const QString input = m_angle->text(); refreshPreview();
  if (!owner || input != m_angle->text() || !m_previewDegrees) return;
  m_degrees = m_previewDegrees; QDialog::accept();
}
SelectionRotationChoice chooseSelectionRotation(const AnnotationDocument &document, const QSet<QUuid> &selection, QWidget *parent) {
  try {
    const QPointer<QWidget> owner(parent); QPointer<SelectionRotationDialog> dialog = new SelectionRotationDialog(document, selection, parent);
    const int outcome = dialog->exec(); if (!dialog) return {};
    const auto degrees = outcome == QDialog::Accepted ? dialog->degrees() : std::nullopt;
    delete dialog.data(); if (parent && !owner) return {}; return {degrees, {}};
  } catch (const std::bad_alloc &) { return {{}, "annotation-edit-failed"}; }
}
} // namespace xerahs::app
