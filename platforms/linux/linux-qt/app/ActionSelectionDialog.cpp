#include "ActionSelectionDialog.h"
#include "CaptureActions.h"
#include "actions/ActionSelection.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
namespace xerahs::app {
namespace {
QString label(const QString &id) {
  static const QMap<QString, QString> names{
    {"image-effects", "Apply image effects"}, {"annotation", "Annotate image"}, {"save", "Save image"},
    {"save-as", "Save image as…"}, {"save-thumbnail", "Save thumbnail"}, {"image-clipboard", "Copy image to clipboard"},
    {"before-upload-window", "Confirm before upload"}, {"upload", "Upload image"}, {"ocr", "Recognize text (OCR)"},
    {"ocr-clipboard", "Copy recognized text"}, {"qr-scan", "Scan QR code"}, {"pin", "Pin image on screen"},
    {"print", "Print image"}, {"custom-actions", "Run custom actions"}, {"file-clipboard", "Copy image file"},
    {"path-clipboard", "Copy file path"}, {"reveal", "Show saved file in folder"}, {"analyze", "Analyze image"},
    {"delete", "Delete saved file"}};
  return names.value(id, id);
}
}
ActionSelectionDialog::ActionSelectionDialog(const QStringList &configured, const QSize &dimensions, QWidget *parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("captureActionSelection")); setWindowTitle(QStringLiteral("Choose capture actions"));
  setAccessibleName(windowTitle()); setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(QStringLiteral("Captured image: %1 × %2 pixels. Choose actions for this capture only.\n"
      "Cancel or Escape stops these actions and keeps the captured image available in XerahS.\n"
      "Leave all actions unchecked to keep the capture without saving or copying it.").arg(dimensions.width()).arg(dimensions.height()), this);
  intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
  auto *scope = new QLabel(QStringLiteral("Workflow defaults stay unchanged. File/path clipboard and Show saved file need Save image. "
      "Copying a file or path shares its saved location; Show saved file requests your file manager. Unavailable actions are listed below."), this);
  scope->setWordWrap(true); layout->addWidget(scope);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  auto *choices = new QWidget(scroll); auto *rows = new QVBoxLayout(choices);
  for (const auto &choice : xerahs::actions::actionSelectionChoices("after-capture-window", configured, CaptureActions::selectableActions())) {
    auto *box = new QCheckBox(label(choice.action) + (choice.available ? QString{} : QStringLiteral(" — unavailable in this build")), choices);
    box->setObjectName(QStringLiteral("capture-action-") + choice.action); box->setChecked(choice.selected);
    box->setEnabled(choice.available); box->setAccessibleName(box->text());
    if (!choice.available) box->setAccessibleDescription(QStringLiteral("This action is unavailable in this version of XerahS."));
    rows->addWidget(box); m_choices.emplace_back(choice.action, box);
  }
  rows->addStretch(); scroll->setWidget(choices); layout->addWidget(scroll);
  m_error = new QLabel(this); m_error->setObjectName(QStringLiteral("captureSelectionError"));
  m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); layout->addWidget(m_error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto *run = buttons->addButton(QStringLiteral("&Run selected actions"), QDialogButtonBox::AcceptRole);
  run->setObjectName(QStringLiteral("runCaptureActions")); run->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Cancel)->setFocus();
  connect(buttons, &QDialogButtonBox::accepted, this, &ActionSelectionDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(560, 580);
}
void ActionSelectionDialog::accept() {
  QStringList requested;
  for (const auto &[id, box] : m_choices) if (box->isChecked()) requested.append(id);
  const auto validated = xerahs::actions::validateActionSelection("after-capture-window", requested, CaptureActions::selectableActions());
  if (!validated.accepted) {
    m_error->setText(QStringLiteral("The selected actions cannot run (%1). Cancel and try again.").arg(validated.diagnostic));
    return;
  }
  m_selected = validated.selected; QDialog::accept();
}
} // namespace xerahs::app
