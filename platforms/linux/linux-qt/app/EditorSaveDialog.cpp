#include "EditorSaveDialog.h"
#include <QColor>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
namespace xerahs::app {
using namespace xerahs::editor;
namespace {
const QString png = "PNG image (*.png)", jpeg = "JPEG image (*.jpg *.jpeg)", webp = "WebP image (*.webp)";
}
EditorSaveDialog::EditorSaveDialog(const QString &path, QWidget *parent) : QFileDialog(parent, "Save image", path) {
  setObjectName("editorSaveAsDialog"); setAccessibleName("Save image destination and encoding");
  setOption(QFileDialog::DontUseNativeDialog); setAcceptMode(QFileDialog::AcceptSave); setFileMode(QFileDialog::AnyFile);
  setNameFilters({png, jpeg, webp});
  const auto format = rasterFormatFromSuffix(QFileInfo(path).suffix()).value_or(RasterFormat::Png);
  selectNameFilter(format == RasterFormat::Jpeg ? jpeg : format == RasterFormat::WebP ? webp : png);
  auto *panel = new QWidget(this); auto *form = new QFormLayout(panel);
  m_policy = new QLabel(panel); m_policy->setObjectName("editorSaveAlphaPolicy"); m_policy->setTextFormat(Qt::PlainText); m_policy->setWordWrap(true); form->addRow(m_policy);
  m_quality = new QLineEdit(panel); m_quality->setObjectName("editorSaveQuality"); m_quality->setAccessibleName("Encoding quality, 0 to 100, required for JPEG and WebP"); m_quality->setValidator(new QIntValidator(0, 100, m_quality)); m_quality->setPlaceholderText("Enter 0–100");
  m_qualityLabel = new QLabel("Quality (0–100, required)", panel); m_qualityLabel->setBuddy(m_quality); form->addRow(m_qualityLabel, m_quality);
  m_matte = new QLineEdit(panel); m_matte->setObjectName("editorSaveJpegMatte"); m_matte->setAccessibleName("Opaque JPEG background colour in #RRGGBB format"); m_matte->setPlaceholderText("#RRGGBB");
  m_matteLabel = new QLabel("JPEG background (#RRGGBB, required)", panel); m_matteLabel->setBuddy(m_matte); form->addRow(m_matteLabel, m_matte);
  m_error = new QLabel(panel); m_error->setObjectName("editorSaveOptionsError"); m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); form->addRow(m_error);
  if (auto *grid = qobject_cast<QGridLayout *>(layout())) grid->addWidget(panel, grid->rowCount(), 0, 1, grid->columnCount());
  connect(this, &QFileDialog::filterSelected, this, [this] { updateOptions(); }); updateOptions();
}
RasterFormat EditorSaveDialog::selectedFormat() const {
  return selectedNameFilter() == jpeg ? RasterFormat::Jpeg : selectedNameFilter() == webp ? RasterFormat::WebP : RasterFormat::Png;
}
void EditorSaveDialog::updateOptions() {
  const auto format = selectedFormat(); setDefaultSuffix(rasterFormatSuffix(format));
  m_quality->setVisible(format != RasterFormat::Png); m_qualityLabel->setVisible(format != RasterFormat::Png);
  m_matte->setVisible(format == RasterFormat::Jpeg); m_matteLabel->setVisible(format == RasterFormat::Jpeg);
  m_policy->setText(format == RasterFormat::Jpeg ? "JPEG has no alpha. Transparent pixels will be composited over the opaque background you enter. Editable pixels in this session are kept. Companions are saved only when annotations exist." :
      "Alpha is preserved. Companions are saved only when annotations exist; they retain the editable source.");
  m_error->clear();
}
RasterSaveOptions EditorSaveDialog::encodingOptions() const {
  RasterSaveOptions options; options.format = selectedFormat();
  if (options.format != RasterFormat::Png) { bool ok = false; options.quality = m_quality->text().toInt(&ok); if (!ok) options.quality = -1; }
  const auto matte = m_matte->text().trimmed();
  if (options.format == RasterFormat::Jpeg && QRegularExpression("^#[0-9a-fA-F]{6}$").match(matte).hasMatch()) options.jpegMatte = QColor(matte).rgba();
  return options;
}
void EditorSaveDialog::accept() {
  updateOptions();
  const auto options = encodingOptions();
  if (!validRasterSaveOptions(options)) { m_error->setText("export-options-invalid: Enter quality from 0 to 100, and an opaque #RRGGBB background for JPEG."); return; }
  const auto path = selectedFiles().value(0); const auto suffix = QFileInfo(path).suffix();
  if (!suffix.isEmpty() && rasterFormatFromSuffix(suffix) != std::optional<RasterFormat>(options.format)) {
    m_error->setText("export-extension-mismatch: Choose a file extension matching the selected image format."); return;
  }
  QFileDialog::accept(); // Existing overwrite confirmation remains before publication.
}
}
