#include "NativeTranslations.h"
#include "ActionSelectionDialog.h"
#include "CaptureActions.h"
#include "CaptureActionLabels.h"
#include "actions/ActionSelection.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
namespace xerahs::app {
namespace {
QStringList availableActions() {
  auto available = CaptureActions::selectableActions();
  available.append("after-capture-window");
  return available;
}
}
ActionSelectionDialog::ActionSelectionDialog(const QStringList &configured, const QSize &dimensions, QWidget *parent, QString selector)
    : QDialog(parent), m_selector(std::move(selector)) {
  setObjectName(QStringLiteral("captureActionSelection"));
  const auto title = m_selector == "quick-task-menu" ? nativeText<ActionSelectionDialog>(QT_TR_NOOP("Quick task menu")) : nativeText<ActionSelectionDialog>(QT_TR_NOOP("Choose capture actions"));
  bindNativeText(*this, "windowTitle", title); bindNativeText(*this, "accessibleName", title);
  setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  auto *intro = translatedWidget(new QLabel(QString{}, this), "text", nativeText<ActionSelectionDialog>(QT_TR_NOOP("Captured image: %1 × %2 pixels. Choose actions for this capture only.\n"
      "Cancel or Escape stops these actions and keeps the captured image available in XerahS.\n"
      "Leave all actions unchecked to keep the capture without saving or copying it.")).arg(dimensions.width()).arg(dimensions.height()));
  intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
  auto *scope = translatedWidget(new QLabel(QString{}, this), "text", nativeText<ActionSelectionDialog>(QT_TR_NOOP("Workflow defaults stay unchanged. File/path clipboard and Show saved file need Save image or Save As. "
      "Copying a file or path shares its saved location; Show saved file requests your file manager. Unavailable actions are listed below.")));
  scope->setWordWrap(true); layout->addWidget(scope);
  auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
  auto *choices = new QWidget(scroll); auto *rows = new QVBoxLayout(choices);
  for (const auto &choice : xerahs::actions::actionSelectionChoices(m_selector, configured, availableActions())) {
    const auto *source = captureActionLabelSource(choice.action);
    auto label = source ? nativeText("CaptureActions", source) : NativeText::literal(choice.action);
    if (!choice.available) label = label.append(nativeText<ActionSelectionDialog>(QT_TR_NOOP(" — unavailable in this build")));
    auto *box = translatedWidget(new QCheckBox(choices), "text", label);
    box->setObjectName(QStringLiteral("capture-action-") + choice.action); box->setChecked(choice.selected);
    box->setEnabled(choice.available); bindNativeText(*box, "accessibleName", label);
    if (!choice.available) bindNativeText(*box, "accessibleDescription", nativeText<ActionSelectionDialog>(QT_TR_NOOP("This action is unavailable in this version of XerahS.")));
    rows->addWidget(box); m_choices.emplace_back(choice.action, box);
  }
  rows->addStretch(); scroll->setWidget(choices); layout->addWidget(scroll);
  m_error = new QLabel(this); m_error->setObjectName(QStringLiteral("captureSelectionError"));
  m_error->setTextFormat(Qt::PlainText); m_error->setWordWrap(true); layout->addWidget(m_error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
  auto *run = addTranslatedButton(*buttons, nativeText<ActionSelectionDialog>(QT_TR_NOOP("&Run selected actions")), QDialogButtonBox::AcceptRole);
  run->setObjectName(QStringLiteral("runCaptureActions")); run->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); buttons->button(QDialogButtonBox::Cancel)->setFocus();
  connect(buttons, &QDialogButtonBox::accepted, this, &ActionSelectionDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
  resize(560, 580);
}
void ActionSelectionDialog::accept() {
  QStringList requested;
  for (const auto &[id, box] : m_choices) if (box->isChecked()) requested.append(id);
  const auto validated = xerahs::actions::validateActionSelection(m_selector, requested, availableActions());
  if (!validated.accepted) {
    bindNativeText(*m_error, "text", nativeText<ActionSelectionDialog>(QT_TR_NOOP("The selected actions cannot run (%1). Cancel and try again.")).arg(validated.diagnostic));
    return;
  }
  m_selected = validated.selected; QDialog::accept();
}
} // namespace xerahs::app
