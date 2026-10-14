#include "EditorPrint.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPrintPreviewWidget>
#include <QPushButton>
#include <QVBoxLayout>
namespace xerahs::app {
EditorPrintChoice chooseEditorPrinter(const EditorPrintSnapshot &snapshot, QWidget *parent) {
  using namespace xerahs::actions;
  EditorPrintChoice cancelled{{ActionState::Cancelled, std::nullopt, std::nullopt}, {}, 100};
  // Preview uses an in-memory Qt preview engine, never a temporary PDF file.
  auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
  printer->setOutputFormat(QPrinter::PdfFormat); printer->setFullPage(false);
  printer->setDocName("XerahS image"); printer->setCreator("XerahS");
  auto result = std::make_shared<EditorPrintChoice>(cancelled);
  const auto owned = std::make_shared<const EditorPrintSnapshot>(snapshot);
  QPointer<QDialog> dialog(new QDialog(parent)); dialog->setObjectName("editorPrintDialog");
  dialog->setWindowTitle("Print Image"); dialog->setAccessibleName("Print image preview and settings"); dialog->resize(760, 700);
  auto *layout = new QVBoxLayout(dialog);
  auto *disclosure = new QLabel("One immutable image page includes the source and visible supported annotations. Transparency is printed on white paper; colours use sRGB. Printing keeps the editable document unsaved.", dialog);
  disclosure->setWordWrap(true); layout->addWidget(disclosure);
  if (!snapshot.disclosures.isEmpty()) {
    auto *colourNotice = new QLabel(snapshot.disclosures.contains("print-colour-converted-to-srgb") ? "Source colours were converted to sRGB for printing." : "The untagged source is interpreted as sRGB for printing.", dialog);
    colourNotice->setWordWrap(true); layout->addWidget(colourNotice);
  }
  auto *form = new QFormLayout; layout->addLayout(form);
  auto *scale = new QDoubleSpinBox(dialog); scale->setObjectName("printScale"); scale->setAccessibleName("Scale as percentage of fit to printable page");
  scale->setRange(1, 100); scale->setDecimals(1); scale->setSuffix(" %"); scale->setValue(100); form->addRow("&Scale (% of fit):", scale);
  auto *orientation = new QComboBox(dialog); orientation->setObjectName("printOrientation"); orientation->setAccessibleName("Page orientation"); orientation->addItems({"Portrait", "Landscape"});
  orientation->setCurrentIndex(printer->pageLayout().orientation() == QPageLayout::Landscape ? 1 : 0); form->addRow("&Orientation:", orientation);
  auto *colour = new QComboBox(dialog); colour->setObjectName("printColour"); colour->setAccessibleName("Print colour mode"); colour->addItems({"Colour", "Grayscale"}); form->addRow("&Colour:", colour);
  auto *page = new QComboBox(dialog); page->setObjectName("printPageSize"); page->setAccessibleName("Preview page size");
  QVector<QPageSize> pages;
  for (const auto id : {QPageSize::A4, QPageSize::Letter, QPageSize::Legal, QPageSize::A5, QPageSize::A3}) { pages.append(QPageSize(id)); page->addItem(pages.last().name(), int(id)); }
  const auto currentPage = printer->pageLayout().pageSize();
  int currentIndex = page->findData(int(currentPage.id()));
  if (currentIndex < 0) { pages.append(currentPage); page->addItem(currentPage.name(), int(currentPage.id())); currentIndex = page->count() - 1; }
  page->setCurrentIndex(currentIndex); form->addRow("&Page size:", page);
  auto *margin = new QDoubleSpinBox(dialog); margin->setObjectName("printMargins"); margin->setAccessibleName("Uniform page margins in millimetres"); margin->setRange(0, 25); margin->setDecimals(1); margin->setSuffix(" mm"); margin->setValue(10);
  printer->setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter); form->addRow("&Margins:", margin);
  auto *preview = new QPrintPreviewWidget(printer.get(), dialog); preview->setObjectName("printPreview"); preview->setAccessibleName("Immutable printed image preview"); layout->addWidget(preview, 1);
  QObject::connect(preview, &QPrintPreviewWidget::paintRequested, dialog, [owned, scale](QPrinter *device) { QPainter painter(device); paintEditorPrintPage(painter, *device, *owned, scale->value()); });
  QObject::connect(scale, &QDoubleSpinBox::valueChanged, preview, [preview] { preview->updatePreview(); });
  QObject::connect(orientation, &QComboBox::currentIndexChanged, preview, [printer, preview](int index) { printer->setPageOrientation(index ? QPageLayout::Landscape : QPageLayout::Portrait); preview->updatePreview(); });
  QObject::connect(colour, &QComboBox::currentIndexChanged, preview, [printer, preview](int index) { printer->setColorMode(index ? QPrinter::GrayScale : QPrinter::Color); preview->updatePreview(); });
  QObject::connect(page, &QComboBox::currentIndexChanged, preview, [printer, pages, preview](int index) { if (index < 0 || index >= pages.size()) return; printer->setPageSize(pages.at(index)); preview->updatePreview(); });
  QObject::connect(margin, &QDoubleSpinBox::valueChanged, preview, [printer, preview](double mm) { printer->setPageMargins(QMarginsF(mm, mm, mm, mm), QPageLayout::Millimeter); preview->updatePreview(); });
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
  auto *print = buttons->addButton("&Print…", QDialogButtonBox::ActionRole); print->setObjectName("confirmPrint"); print->setAutoDefault(false);
  auto *cancel = buttons->button(QDialogButtonBox::Cancel); cancel->setDefault(true); cancel->setFocus();
  layout->addWidget(buttons); QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  const auto available = QPrinterInfo::availablePrinters();
  if (available.isEmpty()) {
    print->setEnabled(false); print->setToolTip("Printing is unavailable: no native printer was found. Add a printer in desktop settings, then open Print Image again.");
    auto *notice = new QLabel(print->toolTip(), dialog); notice->setObjectName("printUnavailable"); notice->setWordWrap(true); layout->insertWidget(1, notice);
  }
  QObject::connect(print, &QPushButton::clicked, dialog, [dialog, printer, result, scale, available] {
    if (!dialog || available.isEmpty()) return;
    auto info = QPrinterInfo::defaultPrinter(); if (info.isNull()) info = available.first();
    auto selected = std::make_shared<QPrinter>(info, QPrinter::HighResolution);
    if (!selected->isValid()) return;
    selected->setFullPage(false); selected->setPageLayout(printer->pageLayout()); selected->setColorMode(printer->colorMode());
    selected->setDocName("XerahS image"); selected->setCreator("XerahS");
    QPointer<QPrintDialog> native(new QPrintDialog(selected.get(), dialog)); native->setObjectName("nativeImagePrintDialog"); native->setWindowTitle("Print Image");
    // File export is a separate action with its own persistence policy. The
    // native printer flow must not introduce an unguarded PDF overwrite path.
    native->setOption(QAbstractPrintDialog::PrintToFile, false);
    native->setOption(QAbstractPrintDialog::PrintSelection, false);
    native->setOption(QAbstractPrintDialog::PrintCurrentPage, false);
    native->setOption(QAbstractPrintDialog::PrintPageRange, true); native->setMinMax(1, 1);
    const auto accepted = native->exec(); delete native.data(); if (!dialog || accepted != QDialog::Accepted) return;
    if (selected->outputFormat() != QPrinter::NativeFormat || !selected->outputFileName().isEmpty() || !selected->isValid()) return;
    result->printer = std::move(selected); result->percentOfFit = scale->value(); result->action = {}; dialog->accept();
  });
  dialog->exec(); const bool alive = bool(dialog); delete dialog.data(); return alive ? *result : cancelled;
}
}
