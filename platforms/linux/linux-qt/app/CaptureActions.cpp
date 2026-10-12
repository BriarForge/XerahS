#include "CaptureActions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QThread>

namespace xerahs::app {

using namespace xerahs::actions;

namespace {

ActionOutcome failed(const QString &diagnostic) {
  return ActionOutcome{ActionState::Failed, diagnostic, std::nullopt};
}

}  // namespace

const QString CaptureActions::kActionUnavailable = QStringLiteral("action-unavailable");

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)), m_ports(std::move(ports)) {}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  try {
    if (m_ports.actionStarted) m_ports.actionStarted(request.spec.id);
    if (request.spec.id == QStringLiteral("save")) return save();
    if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage(request.cancellation);
    return failed(kActionUnavailable);
  } catch (...) { return failed(diagnostic::actionFailed); }
}

ActionOutcome CaptureActions::save() {
  const auto result = saveCapture(m_media, m_directory, m_pattern, m_ports.save);
  if (!result.file) return failed(result.diagnostic);
  m_savedFile = result.file; m_savedPath = result.file->path;
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

ActionOutcome CaptureActions::copyImage(const CancellationToken &cancellation) {
  if (m_ports.clipboard) return m_ports.clipboard(m_media, cancellation);
  return copyImageToNativeClipboard(m_media);
}

ActionOutcome CaptureActions::copyImageToNativeClipboard(const QImage &media) {
  const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread()) return failed(QStringLiteral("clipboard-unavailable"));
  QClipboard *clipboard = QGuiApplication::clipboard();
  clipboard->setImage(media);
  // CORE-007: report success only when this process actually owns the
  // clipboard afterwards.
  if (!clipboard->ownsClipboard()) return failed(QStringLiteral("clipboard-unavailable"));
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

}  // namespace xerahs::app
