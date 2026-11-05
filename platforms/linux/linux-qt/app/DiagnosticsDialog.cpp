#include "DiagnosticsDialog.h"
#include "NativeAccessibility.h"
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {
DiagnosticsDialog::DiagnosticsDialog(diagnostics::DiagnosticBundle bundle, QWidget *parent)
    : QDialog(parent), m_bundle(std::move(bundle)) {
  setObjectName("diagnosticsDialog"); setWindowTitle(tr("Export diagnostics")); setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  auto *intro = new QLabel(tr("Review this frozen JSON before exporting it to a new private file. It includes software versions, declared settings-field states and the latest recognized errors from this session. Captures, clipboard contents, recognized text, paths and credentials are excluded. Existing files are preserved."), this);
  intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
  auto *preview = new QPlainTextEdit(this); preview->setObjectName("diagnosticsPreview");
  preview->setAccessibleName(tr("Redacted diagnostic bundle")); preview->setReadOnly(true);
  preview->setPlainText(QString::fromUtf8(m_bundle.bytes())); layout->addWidget(preview);
  m_status = new QLabel(tr("Review the preview, then choose Export new file."), this);
  m_status->setObjectName("diagnosticsExportStatus"); m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true); layout->addWidget(m_status);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  m_export = buttons->addButton(tr("Export new file…"), QDialogButtonBox::ActionRole);
  m_export->setObjectName("exportDiagnosticsFile"); m_export->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Close)->setDefault(true);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_export, &QPushButton::clicked, this, &DiagnosticsDialog::chooseFile); layout->addWidget(buttons);
  new NativeAccessibility(*this, tr("Review redacted diagnostics and explicitly export a new local file. Close preserves application data."), false);
  resize(720, 540);
}
DiagnosticsDialog::~DiagnosticsDialog() { m_closing = true; m_work.cancel(); }
void DiagnosticsDialog::done(int result) {
  m_closing = true; m_work.cancel();
  const QPointer<DiagnosticsDialog> owner(this);
  if (m_picker) m_picker->reject();
  if (owner) QDialog::done(result);
}
void DiagnosticsDialog::chooseFile() {
  if (m_closing || m_writing || m_picker) return;
  const QPointer<DiagnosticsDialog> owner(this);
  QPointer<QFileDialog> picker(new QFileDialog(this, tr("Export diagnostics to a new file")));
  m_picker = picker;
  picker->setObjectName("diagnosticsExportFile"); picker->setAcceptMode(QFileDialog::AcceptSave);
  picker->setFileMode(QFileDialog::AnyFile); picker->setDefaultSuffix("json");
  picker->setNameFilter(tr("JSON files (*.json)")); picker->setOption(QFileDialog::DontConfirmOverwrite);
  picker->selectFile("XerahS-diagnostics.json"); picker->setAttribute(Qt::WA_DeleteOnClose);
  const auto handled = std::make_shared<bool>(false);
  connect(picker, &QDialog::finished, this, [this, picker, handled](int result) {
    if (*handled) return;
    *handled = true; m_picker.clear();
    const auto path = picker && result == QDialog::Accepted ? picker->selectedFiles().value(0) : QString{};
    if (picker) picker->deleteLater();
    if (m_closing) return;
    if (!path.isEmpty()) { write(path); return; }
    const QPointer<DiagnosticsDialog> owner(this);
    m_export->setEnabled(true);
    if (owner) m_status->setText(tr("Export cancelled. The preview is unchanged."));
  });
  connect(picker, &QObject::destroyed, this, [this, handled] {
    if (*handled || m_closing) return;
    *handled = true; m_picker.clear();
    const QPointer<DiagnosticsDialog> owner(this);
    m_export->setEnabled(true);
    if (owner) m_status->setText(tr("Export cancelled. The preview is unchanged."));
  });
  m_export->setEnabled(false);
  if (owner && picker) picker->open();
}
void DiagnosticsDialog::write(const QString &path) {
  if (m_closing || m_writing) return;
  m_writing = true;
  const QPointer<DiagnosticsDialog> owner(this);
  m_export->setEnabled(false); if (!owner) return;
  m_status->setText(tr("Writing the reviewed diagnostic bundle…")); if (!owner) return;
  struct Result { bool cancelled = false; storage::StoreStatus status = storage::StoreStatus::WriteFailed; };
  const auto work = OwnedWork::run<Result>(this, [bundle = m_bundle, path](const OwnedWork::Context &context) {
    if (context.cancelled()) return Result{true};
    try { return Result{false, bundle.save(path)}; } catch (...) { return Result{}; }
  }, [this](Result result) {
    if (m_closing) return;
    m_writing = false;
    const QPointer<DiagnosticsDialog> owner(this);
    m_export->setEnabled(true); if (!owner) return;
    if (result.cancelled) m_status->setText(tr("Export cancelled. The preview is unchanged."));
    else if (result.status == storage::StoreStatus::Ok) m_status->setText(tr("Diagnostics exported. The file contains exactly the reviewed JSON."));
    else if (result.status == storage::StoreStatus::Changed || result.status == storage::StoreStatus::Invalid || result.status == storage::StoreStatus::NewerVersion)
      m_status->setText(QStringLiteral("diagnostics-export-existing-target: ") + tr("Choose a new file name. Existing content was kept."));
    else m_status->setText(QStringLiteral("diagnostics-export-write-failed: ") + tr("Export did not complete. Check the selected location before retrying or choose a different new file."));
  });
  if (owner) m_work = work; else work.cancel();
}
}
