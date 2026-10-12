#include "CaptureActions.h"

#include <QClipboard>
#include <QGuiApplication>

namespace xerahs::app {

using namespace xerahs::actions;

namespace {

ActionOutcome failed(const QString &diagnostic) {
  return ActionOutcome{ActionState::Failed, diagnostic, std::nullopt};
}

}  // namespace

const QString CaptureActions::kActionUnavailable = QStringLiteral("action-unavailable");

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)) {}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  if (request.spec.id == QStringLiteral("save")) return save();
  if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage();
  return failed(kActionUnavailable);
}

ActionOutcome CaptureActions::save() {
  const auto result = saveCapture(m_media, m_directory, m_pattern);
  if (!result.file) return failed(result.diagnostic);
  m_savedFile = result.file; m_savedPath = result.file->path;
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

ActionOutcome CaptureActions::copyImage() {
  QClipboard *clipboard = QGuiApplication::clipboard();
  clipboard->setImage(m_media);
  // CORE-007: report success only when this process actually owns the
  // clipboard afterwards.
  if (!clipboard->ownsClipboard()) return failed(QStringLiteral("clipboard-unavailable"));
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

}  // namespace xerahs::app
