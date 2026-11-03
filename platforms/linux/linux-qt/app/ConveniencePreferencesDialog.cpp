#include "ConveniencePreferencesDialog.h"
#include "PreferenceDialog.h"
#include "NativeEditorPreferences.h"
#include "NativeEditorAppearance.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace xerahs::app {
bool manageConveniencePreferences(QWidget *parent) {
  const auto original = nativeEditorPreferences();
  const auto revision = nativeEditorPreferenceRevision();
  QPointer<PreferenceDialog> dialog(new PreferenceDialog(parent));
  dialog->setObjectName(QStringLiteral("conveniencePreferencesDialog"));
  dialog->setWindowTitle(QStringLiteral("Editor Convenience"));
  new NativeEditorAppearance(*dialog);
  auto *layout = new QVBoxLayout(dialog);
  auto *description = new QLabel(QStringLiteral("These preferences apply when an editor opens or Continue completes. Required consent, unsaved-change confirmation, validation and error reports always remain enabled."), dialog);
  description->setWordWrap(true); layout->addWidget(description);
  QMap<QString, QCheckBox *> controls;
  for (const auto &entry : {std::pair{"AutoCopyImageToClipboard", "Copy the rendered image to the clipboard on Continue"},
                          std::pair{"AutoCloseEditorOnTask", "Close after Continue completes successfully"},
                          std::pair{"ZoomToFitOnOpen", "Zoom to fit on open"},
                          std::pair{"ShowExitConfirmation", "Confirm exit even when there are no unsaved changes"}}) {
    auto *check = new QCheckBox(QString::fromLatin1(entry.second), dialog);
    const auto key = QString::fromLatin1(entry.first);
    check->setObjectName(key); check->setAccessibleName(check->text());
    check->setChecked(original.values.value(key).toBool());
    controls.insert(key, check); layout->addWidget(check);
  }
  controls.value(QStringLiteral("AutoCloseEditorOnTask"))->setAccessibleDescription(QStringLiteral("A clipboard failure keeps the editor open and reports the partial failure."));
  controls.value(QStringLiteral("ShowExitConfirmation"))->setAccessibleDescription(QStringLiteral("Disabling this only skips optional clean exit confirmation. Unsaved changes always offer Save, Discard and Cancel."));
  auto *error = new QLabel(dialog); error->setObjectName(QStringLiteral("conveniencePreferencesError"));
  error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyConveniencePreferences"));
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
    if (!dialog || !dialog->ready()) return;
    QJsonObject changes;
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) changes.insert(it.key(), it.value()->isChecked());
    dialog->commit([changes, revision] { return applyConveniencePreferences(changes, revision); },
        [=](const xerahs::editor::PreferenceWrite &written) { error->setText(editorPreferenceDiagnosticMessage(written.diagnostic)); });
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  return PreferenceDialog::run(dialog, parent);
}
}
