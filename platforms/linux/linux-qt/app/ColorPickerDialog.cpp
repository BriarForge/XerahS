#include "ColorPickerDialog.h"

#include "tools/ColorFormats.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {
namespace {
const int kRecentLimit = 10;

QString recentLabel(const QColor &color) {
  using xerahs::tools::ColorFormat;
  using xerahs::tools::formatColor;
  return formatColor(color, color.alpha() == 255 ? ColorFormat::Hex : ColorFormat::HexAlpha);
}
}  // namespace

ColorPickerDialog::ColorPickerDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("colorPickerDialog"));
  setWindowTitle(QStringLiteral("Pick colour"));
  setAccessibleName(QStringLiteral("Pick colour"));
  setMinimumWidth(420);

  auto *layout = new QVBoxLayout(this);
  auto *heading = new QHBoxLayout;
  m_swatch = new QLabel(this);
  m_swatch->setObjectName(QStringLiteral("colourSwatch"));
  m_swatch->setFixedSize(48, 48);
  m_swatch->setFrameShape(QFrame::Panel);
  m_swatch->setFrameShadow(QFrame::Sunken);
  m_swatch->setAccessibleName(QStringLiteral("Colour swatch"));
  m_swatch->setFocusPolicy(Qt::NoFocus);
  m_hex = new QLineEdit(this);
  m_hex->setObjectName(QStringLiteral("hexColour"));
  m_hex->setAccessibleName(QStringLiteral("Hex colour"));
  m_hex->setPlaceholderText(QStringLiteral("#RRGGBB or #RRGGBBAA"));
  m_hex->setClearButtonEnabled(true);
  heading->addWidget(m_swatch);
  heading->addWidget(m_hex, 1);
  layout->addLayout(heading);

  m_hexError = new QLabel(this);
  m_hexError->setObjectName(QStringLiteral("hexColourError"));
  m_hexError->setAccessibleName(QStringLiteral("Hex colour error"));
  m_hexError->setWordWrap(true);
  m_hexError->setTextFormat(Qt::PlainText);
  m_hexError->setFocusPolicy(Qt::NoFocus);
  m_hexError->hide();
  layout->addWidget(m_hexError);

  const QStringList blanks = xerahs::tools::formatAll(QColor());
  QWidget *previous = m_hex;
  for (int index = 0; index < 8; ++index) {
    const auto format = static_cast<xerahs::tools::ColorFormat>(index);
    const QString name = xerahs::tools::formatName(format);
    auto *row = new QHBoxLayout;
    m_lines[index] = new QLabel(blanks.value(index), this);
    m_lines[index]->setObjectName(QStringLiteral("colorFormat%1").arg(index));
    // The name is the whole line so a screen reader hears the value, not only "HEX".
    m_lines[index]->setAccessibleName(blanks.value(index));
    m_lines[index]->setTextFormat(Qt::PlainText);
    m_lines[index]->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lines[index]->setFocusPolicy(Qt::NoFocus);
    m_copies[index] = new QPushButton(QStringLiteral("Copy"), this);
    m_copies[index]->setObjectName(QStringLiteral("copyFormat%1").arg(index));
    m_copies[index]->setAccessibleName(QStringLiteral("Copy %1").arg(name));
    m_copies[index]->setAutoDefault(false);
    row->addWidget(m_lines[index], 1);
    row->addWidget(m_copies[index]);
    layout->addLayout(row);
    setTabOrder(previous, m_copies[index]);
    previous = m_copies[index];
    connect(m_copies[index], &QPushButton::clicked, this, [this, index] { copyLine(index); });
  }

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("colourPickerStatus"));
  m_status->setAccessibleName(QStringLiteral("Colour picker status"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  m_status->setFocusPolicy(Qt::NoFocus);
  m_status->hide();
  layout->addWidget(m_status);

  m_pickAgain = new QPushButton(QStringLiteral("Pick again"), this);
  m_pickAgain->setObjectName(QStringLiteral("pickAgain"));
  m_pickAgain->setAccessibleName(QStringLiteral("Pick again"));
  m_pickAgain->setAutoDefault(false);
  layout->addWidget(m_pickAgain);
  setTabOrder(previous, m_pickAgain);

  auto *recentLabel = new QLabel(QStringLiteral("Recent"), this);
  recentLabel->setFocusPolicy(Qt::NoFocus);
  m_recent = new QListWidget(this);
  m_recent->setObjectName(QStringLiteral("recentColours"));
  m_recent->setAccessibleName(QStringLiteral("Recent colours"));
  m_recent->setMinimumHeight(120);
  recentLabel->setBuddy(m_recent);
  layout->addWidget(recentLabel);
  layout->addWidget(m_recent, 1);
  setTabOrder(m_pickAgain, m_recent);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setAccessibleName(QStringLiteral("Close colour picker"));
  if (auto *closeButton = buttons->button(QDialogButtonBox::Close)) {
    closeButton->setAutoDefault(false);
    closeButton->setAccessibleName(QStringLiteral("Close"));
    setTabOrder(m_recent, closeButton);
  }
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_pickAgain, &QPushButton::clicked, this, &ColorPickerDialog::pick);
  connect(m_hex, &QLineEdit::editingFinished, this, &ColorPickerDialog::commitHex);
  connect(m_hex, &QLineEdit::returnPressed, this, &ColorPickerDialog::commitHex);
  connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
    const QColor selected = item->data(Qt::UserRole).value<QColor>();
    if (selected.isValid()) applyColor(selected);
  });
}

void ColorPickerDialog::showColor(const QColor &color) {
  if (!color.isValid()) return;
  showPortalMessage(QString());
  applyColor(color);
  remember(color);
}

void ColorPickerDialog::pick() {
  if (m_picker) return;
  m_pickAgain->setEnabled(false);
  auto *picker = new PortalColorPicker(this);
  m_picker = picker;
  connect(picker, &PortalColorPicker::finished, this,
          [this, picker](PortalColorPicker::Outcome outcome, const QColor &color, const QString &) {
            if (m_picker == picker) m_picker = nullptr;
            picker->deleteLater();
            notePortalResult(outcome, color);
          });
  picker->request();
}

void ColorPickerDialog::notePortalResult(PortalColorPicker::Outcome outcome, const QColor &color) {
  // Re-enabling the button is the only change on cancel. The colour, the
  // formats, the recent list and any message stay as they were. No popup.
  m_pickAgain->setEnabled(true);
  if (outcome == PortalColorPicker::Outcome::Cancelled) return;
  if (outcome == PortalColorPicker::Outcome::Picked && color.isValid()) {
    showColor(color);
    return;
  }
  showPortalMessage(outcome == PortalColorPicker::Outcome::Denied
                        ? QStringLiteral("The desktop refused colour picking.")
                        : QStringLiteral("The screen colour picker is unavailable."));
}

void ColorPickerDialog::applyColor(const QColor &color) {
  if (!color.isValid()) return;
  m_applying = true;
  m_color = color;
  const QString hex = recentLabel(color);
  m_hex->setText(hex);
  m_hex->setAccessibleDescription(QString());
  m_hexError->clear();
  m_hexError->hide();
  const QStringList lines = xerahs::tools::formatAll(color);
  for (int index = 0; index < 8; ++index) {
    m_lines[index]->setText(lines.value(index));
    m_lines[index]->setAccessibleName(lines.value(index));
  }
  QPixmap swatch(48, 48);
  swatch.fill(color);
  m_swatch->setPixmap(swatch);
  m_swatch->setAccessibleDescription(hex);
  m_applying = false;
}

void ColorPickerDialog::remember(const QColor &color) {
  m_recentColors.removeAll(color);
  m_recentColors.prepend(color);
  while (m_recentColors.size() > kRecentLimit) m_recentColors.removeLast();
  refreshRecent();
}

void ColorPickerDialog::refreshRecent() {
  m_recent->clear();
  for (const QColor &color : m_recentColors) {
    auto *item = new QListWidgetItem(recentLabel(color), m_recent);
    item->setData(Qt::UserRole, color);
    QPixmap chip(16, 16);
    chip.fill(color);
    item->setIcon(QIcon(chip));
  }
}

void ColorPickerDialog::commitHex() {
  if (m_applying) return;
  const auto parsed = xerahs::tools::parseHexColor(m_hex->text());
  if (!parsed || !parsed->isValid()) {
    m_hexError->setText(QStringLiteral("Enter a hex colour: #RGB, #RRGGBB or #RRGGBBAA."));
    m_hexError->show();
    m_hex->setAccessibleDescription(m_hexError->text());
    return;
  }
  applyColor(*parsed);
}

void ColorPickerDialog::copyLine(int index) {
  if (index < 0 || index >= 8 || !m_lines[index]) return;
  QApplication::clipboard()->setText(m_lines[index]->text());
}

void ColorPickerDialog::showPortalMessage(const QString &message) {
  m_status->setText(message);
  m_status->setVisible(!message.isEmpty());
}

}  // namespace xerahs::app
