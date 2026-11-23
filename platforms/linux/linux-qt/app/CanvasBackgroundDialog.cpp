#include "CanvasBackgroundDialog.h"

#include "image-editor/CanvasOperations.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {

namespace {

QString colorName(const QColor &color) {
  return QStringLiteral("#%1").arg(color.rgba(), 8, 16, QChar(u'0')).toUpper();
}

}  // namespace

CanvasBackgroundDialog::CanvasBackgroundDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("canvasBackgroundDialog"));
  setWindowTitle(tr("Background"));

  m_kind = new QComboBox(this);
  m_kind->setObjectName(QStringLiteral("backgroundKind"));
  m_kind->setAccessibleName(tr("Background kind"));
  m_kind->addItem(tr("Transparent"));
  m_kind->addItem(tr("Solid colour"));
  m_kind->addItem(tr("Gradient"));

  m_solid = new QPushButton(this);
  m_solid->setObjectName(QStringLiteral("backgroundSolid"));
  m_solid->setAccessibleName(tr("Solid background colour"));
  m_gradientFrom = new QPushButton(this);
  m_gradientFrom->setObjectName(QStringLiteral("backgroundGradientFrom"));
  m_gradientFrom->setAccessibleName(tr("Gradient top colour"));
  m_gradientTo = new QPushButton(this);
  m_gradientTo->setObjectName(QStringLiteral("backgroundGradientTo"));
  m_gradientTo->setAccessibleName(tr("Gradient bottom colour"));

  const auto number = [&](const QString &name, const QString &label, double value, double maximum) {
    auto *spin = new QDoubleSpinBox(this);
    spin->setObjectName(name);
    spin->setAccessibleName(label);
    spin->setRange(0, maximum);
    spin->setDecimals(0);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral(" px"));
    return spin;
  };
  m_margin = number(QStringLiteral("backgroundMargin"), tr("Margin"), 80, 1000);
  m_padding = number(QStringLiteral("backgroundPadding"), tr("Padding"), 40, 1000);
  m_corner = number(QStringLiteral("backgroundCorner"), tr("Corner radius"), 20, 255);
  m_shadow = number(QStringLiteral("backgroundShadow"), tr("Shadow radius"), 30, 100);

  m_smart = new QCheckBox(tr("Smart padding"), this);
  m_smart->setObjectName(QStringLiteral("backgroundSmart"));
  m_smart->setAccessibleName(tr("Smart padding"));
  m_smart->setChecked(true);

  auto *solidRow = new QHBoxLayout;
  solidRow->addWidget(m_solid);
  auto *gradientRow = new QHBoxLayout;
  gradientRow->addWidget(m_gradientFrom);
  gradientRow->addWidget(m_gradientTo);

  auto *form = new QFormLayout;
  form->addRow(tr("&Kind"), m_kind);
  form->addRow(tr("Solid &colour"), solidRow);
  form->addRow(tr("&Gradient"), gradientRow);
  form->addRow(tr("&Margin"), m_margin);
  form->addRow(tr("&Padding"), m_padding);
  form->addRow(tr("Corner &radius"), m_corner);
  form->addRow(tr("Shadow &radius"), m_shadow);
  form->addRow(QString(), m_smart);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("backgroundStatus"));
  m_status->setWordWrap(true);

  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { refreshKinds(); });
  connect(m_solid, &QPushButton::clicked, this, [this] { pickColor(m_solid, m_solidColor); });
  connect(m_gradientFrom, &QPushButton::clicked, this, [this] { pickColor(m_gradientFrom, m_gradientFromColor); });
  connect(m_gradientTo, &QPushButton::clicked, this, [this] { pickColor(m_gradientTo, m_gradientToColor); });

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(m_status);
  layout->addWidget(m_buttons);
  refreshKinds();
}

CanvasBackgroundDialog::~CanvasBackgroundDialog() = default;

void CanvasBackgroundDialog::pickColor(QPushButton *button, QColor &color) {
  const QColor picked = QColorDialog::getColor(color, this, tr("Select colour"), QColorDialog::ShowAlphaChannel);
  if (!picked.isValid()) return;
  color = picked;
  button->setText(colorName(color));
}

void CanvasBackgroundDialog::refreshKinds() {
  const int kind = m_kind->currentIndex();
  m_solid->setEnabled(kind == 1);
  m_gradientFrom->setEnabled(kind == 2);
  m_gradientTo->setEnabled(kind == 2);
  m_solid->setText(colorName(m_solidColor));
  m_gradientFrom->setText(colorName(m_gradientFromColor));
  m_gradientTo->setText(colorName(m_gradientToColor));
}

xerahs::editor::CanvasOperation CanvasBackgroundDialog::operation() const {
  xerahs::editor::CanvasOperation op;
  op.action = xerahs::editor::CanvasAction::Background;
  switch (m_kind->currentIndex()) {
    case 1: op.background.kind = xerahs::editor::BackgroundPolicy::Kind::Solid; break;
    case 2: op.background.kind = xerahs::editor::BackgroundPolicy::Kind::Gradient; break;
    default: op.background.kind = xerahs::editor::BackgroundPolicy::Kind::Transparent; break;
  }
  op.background.solid = m_solidColor.rgba();
  op.background.gradientFrom = m_gradientFromColor.rgba();
  op.background.gradientTo = m_gradientToColor.rgba();
  op.background.margin = m_margin->value();
  op.background.padding = m_padding->value();
  op.background.cornerRadius = m_corner->value();
  op.background.smartPadding = m_smart->isChecked();
  op.background.shadowRadius = m_shadow->value();
  return op;
}

void CanvasBackgroundDialog::accept() {
  // The seam owns validation; a typed failure from the commit path reaches
  // the window's status surface, so this dialog has no second opinion.
  m_status->clear();
  QDialog::accept();
}

}  // namespace xerahs::app
