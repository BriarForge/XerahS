#include "CaptureSaveAsDialog.h"
#include "actions/SaveAsTarget.h"
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
namespace xerahs::app {
CaptureSaveAsDialog::CaptureSaveAsDialog(const QString &directory, QWidget *parent)
    : QFileDialog(parent, QStringLiteral("Save capture as new PNG"), directory, QStringLiteral("PNG image (*.png)")) {
  // A Qt Widgets picker keeps validation and cancellation asynchronous and
  // reachable on the supported Qt 6.4 baseline, with no nested dialog loop.
  setOption(QFileDialog::DontUseNativeDialog); setOption(QFileDialog::DontConfirmOverwrite);
  setAcceptMode(QFileDialog::AcceptSave); setFileMode(QFileDialog::AnyFile);
  setWindowModality(Qt::ApplicationModal); setAttribute(Qt::WA_DeleteOnClose);
  setObjectName("captureSaveAsDialog"); setAccessibleName(windowTitle()); selectFile("capture.png");
  m_error = new QLabel(QStringLiteral("Choose a new PNG path. Existing files are kept. Cancel skips this save; other selected actions may continue."), this);
  m_error->setObjectName("captureSaveAsStatus"); m_error->setWordWrap(true); m_error->setTextFormat(Qt::PlainText);
  m_error->setAccessibleName("Save As status"); layout()->addWidget(m_error);
}
void CaptureSaveAsDialog::accept() {
  const auto requested = selectedFiles().value(0);
  // Apply the PNG suffix only after recognizing a directory. QFileDialog's
  // defaultSuffix also changes selectedFiles(), turning "folder" into
  // "folder.png" before our validation can distinguish navigation from saving.
  if (QFileInfo(requested).isDir()) { QFileDialog::accept(); return; }
  const auto target = xerahs::actions::captureSaveAsTarget(requested);
  const QFileInfo info(target.path);
  const QString diagnostic = !target.valid() ? target.diagnostic :
      (info.exists() || info.isSymLink()) ? QStringLiteral("save-as-destination-exists") : QString{};
  if (!diagnostic.isEmpty()) {
    m_error->setText(QStringLiteral("Choose another PNG path (%1). Existing files are kept.").arg(diagnostic)); return;
  }
  m_target = target.path; QDialog::accept();
}
} // namespace xerahs::app
