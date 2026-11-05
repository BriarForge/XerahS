#include "NativeAccessibility.h"
#include "ColorPickerDialog.h"
#include "ScreenColorSampler.h"
#include <QTimer>

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

ColorPickerDialog::ColorPickerDialog(QWidget *parent, ColorPickerPorts ports) : QDialog(parent), m_ports(std::move(ports)) {
  setObjectName(QStringLiteral("colorPickerDialog"));
  new NativeAccessibility(*this, tr("Choose or sample a screen colour. Review canonical formats and explicitly Copy; Escape closes without copying."), true);
  setWindowTitle(tr("Pick colour"));
  setAccessibleName(tr("Pick colour"));
  setMinimumWidth(420);

  auto *layout = new QVBoxLayout(this);
  auto *heading = new QHBoxLayout;
  m_swatch = new QLabel(this);
  m_swatch->setObjectName(QStringLiteral("colourSwatch"));
  m_swatch->setFixedSize(48, 48);
  m_swatch->setFrameShape(QFrame::Panel);
  m_swatch->setFrameShadow(QFrame::Sunken);
  m_swatch->setAccessibleName(tr("Colour swatch"));
  m_swatch->setFocusPolicy(Qt::NoFocus);
  m_hex = new QLineEdit(this);
  m_hex->setObjectName(QStringLiteral("hexColour"));
  m_hex->setAccessibleName(tr("Hex colour"));
  m_hex->setPlaceholderText(tr("#RRGGBB or #RRGGBBAA"));
  m_hex->setClearButtonEnabled(true);
  heading->addWidget(m_swatch);
  heading->addWidget(m_hex, 1);
  layout->addLayout(heading);
  m_coordinates = new QLabel(this); m_coordinates->setObjectName("pickedScreenCoordinates");
  m_coordinates->setAccessibleName(tr("Selected screenshot pixel coordinates")); m_coordinates->setTextFormat(Qt::PlainText);
  m_coordinates->setWordWrap(true); layout->addWidget(m_coordinates);

  m_hexError = new QLabel(this);
  m_hexError->setObjectName(QStringLiteral("hexColourError"));
  m_hexError->setAccessibleName(tr("Hex colour error"));
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
    m_copies[index] = new QPushButton(tr("Copy"), this);
    m_copies[index]->setObjectName(QStringLiteral("copyFormat%1").arg(index));
    m_copies[index]->setAccessibleName(tr("Copy %1").arg(name));
    m_copies[index]->setAutoDefault(false);
    row->addWidget(m_lines[index], 1);
    row->addWidget(m_copies[index]);
    layout->addLayout(row);
    setTabOrder(previous, m_copies[index]);
    previous = m_copies[index];
    connect(m_copies[index], &QPushButton::clicked, this, [this, index] { copyLine(index); });
  }

  m_copyAll = new QPushButton(tr("Copy all"), this);
  m_copyAll->setObjectName(QStringLiteral("copyAllColorFormats"));
  m_copyAll->setAccessibleName(tr("Copy all formats"));
  m_copyAll->setAutoDefault(false);
  layout->addWidget(m_copyAll);
  connect(m_copyAll, &QPushButton::clicked, this, &ColorPickerDialog::copyAll);
  setTabOrder(previous, m_copyAll);
  previous = m_copyAll;
  // The blank lines hold no colour yet, so there is nothing to copy.
  setCopyEnabled(false);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("colourPickerStatus"));
  m_status->setAccessibleName(tr("Colour picker status"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  m_status->setFocusPolicy(Qt::NoFocus);
  m_status->hide();
  layout->addWidget(m_status);

  m_pickAgain = new QPushButton(tr("Pick again"), this);
  m_pickAgain->setObjectName(QStringLiteral("pickAgain"));
  m_pickAgain->setAccessibleName(tr("Pick again"));
  m_pickAgain->setAutoDefault(false);
  layout->addWidget(m_pickAgain);
  setTabOrder(previous, m_pickAgain);

  auto *recentLabel = new QLabel(tr("Recent"), this);
  recentLabel->setFocusPolicy(Qt::NoFocus);
  m_recent = new QListWidget(this);
  m_recent->setObjectName(QStringLiteral("recentColours"));
  m_recent->setAccessibleName(tr("Recent colours"));
  m_recent->setMinimumHeight(120);
  recentLabel->setBuddy(m_recent);
  layout->addWidget(recentLabel);
  layout->addWidget(m_recent, 1);
  setTabOrder(m_pickAgain, m_recent);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->setAccessibleName(tr("Close colour picker"));
  if (auto *closeButton = buttons->button(QDialogButtonBox::Close)) {
    closeButton->setAutoDefault(false);
    closeButton->setAccessibleName(tr("Close"));
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
  showStatus(QString());
  applyColor(color);
  remember(color);
}

ColorPickerDialog::~ColorPickerDialog() {
  // Child focus-out can emit editingFinished while QWidget destroys its children.
  // Disconnect before the derived object ceases to exist.
  disconnect(m_hex, nullptr, this, nullptr);
  ++m_captureGeneration;
  if (m_capture) { auto *capture = m_capture.data(); m_capture.clear(); capture->disconnect(this); capture->cancel(); capture->retire(); }
}
void ColorPickerDialog::reject() {
  ++m_captureGeneration;
  if (m_capture) { auto *capture = m_capture.data(); m_capture.clear(); capture->cancel(); capture->retire(); }
  if (m_sampler) { m_sampler->reject(); m_sampler.clear(); }
  m_pickAgain->setEnabled(true); QDialog::reject();
}
void ColorPickerDialog::pick() {
  if (m_capture || m_sampler) return;
  const auto generation = ++m_captureGeneration;
  m_pickAgain->setEnabled(false);
  showStatus(tr("Waiting for screenshot permission. The desktop controls capture consent; Escape in the desktop prompt cancels."));
  showMinimized();
  QTimer::singleShot(0, this, [this, generation] {
    if (generation != m_captureGeneration) return;
    const QPointer<ColorPickerDialog> owner(this);
    const auto factory = m_ports.screenshot; // Keep the callback alive if it destroys the dialog.
    PortalScreenshot *capture = nullptr;
    try { capture = factory ? factory(this) : new PortalScreenshot(this); }
    catch (...) { /* Host exceptions never expose private payloads or escape the event loop. */ }
    if (!owner) { if (capture) capture->retire(); return; }
    if (generation != m_captureGeneration) { if (capture) capture->retire(); return; }
    if (!capture) { m_pickAgain->setEnabled(true); showNormal(); showStatus(QStringLiteral("capture-unavailable: ") + tr("Screen capture is unavailable. The previous colour was kept. Try again.")); return; }
    m_capture = capture;
    connect(capture, &PortalScreenshot::finished, this,
        [this, capture, generation](PortalScreenshot::Outcome outcome, const QImage &image, const QString &) {
      if (m_capture == capture) m_capture.clear();
      capture->retire();
      if (generation != m_captureGeneration) return;
      showNormal(); raise(); m_pickAgain->setEnabled(true);
      if (outcome == PortalScreenshot::Outcome::Captured && !image.isNull()) { showCapturedSnapshot(image); return; }
      if (outcome == PortalScreenshot::Outcome::Cancelled) { showStatus(tr("Screen colour sampling cancelled. The previous colour was kept.")); return; }
      showStatus(outcome == PortalScreenshot::Outcome::Denied ? tr("The desktop refused screenshot permission. The previous colour was kept.") : tr("Screenshot capture failed. The previous colour was kept."));
    });
    capture->request();
  });
}
void ColorPickerDialog::showCapturedSnapshot(const QImage &image) {
  if (image.isNull() || m_sampler) return;
  auto *sampler = new ScreenColorSampler(image, this); m_sampler = sampler;
  sampler->setAttribute(Qt::WA_DeleteOnClose); sampler->setWindowModality(Qt::WindowModal);
  const auto generation = m_captureGeneration;
  connect(sampler, &ScreenColorSampler::sampled, this, [this, sampler, generation](const QColor &color, const QPoint &pixel) {
    if (m_sampler != sampler || generation != m_captureGeneration) return;
    showColor(color);
    m_coordinates->setText(tr("Snapshot pixel X: %1, Y: %2 (zero-based screenshot pixels)").arg(pixel.x()).arg(pixel.y()));
    m_coordinates->setAccessibleDescription(m_coordinates->text());
  });
  connect(sampler, &QDialog::finished, this, [this, sampler] { if (m_sampler == sampler) m_sampler.clear(); });
  sampler->open();
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
  showStatus(outcome == PortalColorPicker::Outcome::Denied
                 ? tr("The desktop refused colour picking.")
                 : tr("The screen colour picker is unavailable."));
}

void ColorPickerDialog::applyColor(const QColor &color) {
  m_coordinates->clear(); m_coordinates->setAccessibleDescription({});
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
  setCopyEnabled(true);
  // A shown colour replaces any earlier message, including a copy confirmation.
  showStatus(QString());
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
    m_hexError->setText(tr("Enter a hex colour: #RGB, #RRGGBB or #RRGGBBAA."));
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

void ColorPickerDialog::copyAll() {
  if (!m_color.isValid()) return;
  // The same eight lines the dialog shows, one per line, in format order.
  QApplication::clipboard()->setText(xerahs::tools::formatAll(m_color).join(QLatin1Char('\n')));
  showStatus(tr("All formats copied."));
}

void ColorPickerDialog::setCopyEnabled(bool enabled) {
  for (int index = 0; index < 8; ++index) {
    if (m_copies[index]) m_copies[index]->setEnabled(enabled);
  }
  if (m_copyAll) m_copyAll->setEnabled(enabled);
}

void ColorPickerDialog::showStatus(const QString &message) {
  m_status->setText(message);
  m_status->setAccessibleDescription(message);
  m_status->setVisible(!message.isEmpty());
}

}  // namespace xerahs::app
