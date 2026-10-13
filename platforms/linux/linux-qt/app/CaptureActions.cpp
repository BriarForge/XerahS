#include "CaptureActions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QThread>
#include <QMap>

namespace xerahs::app {

using namespace xerahs::actions;

namespace {

ActionOutcome failed(const QString &diagnostic) {
  return ActionOutcome{ActionState::Failed, diagnostic, std::nullopt};
}

}  // namespace

const QString CaptureActions::kActionUnavailable = QStringLiteral("action-unavailable");
QStringList CaptureActions::selectableActions() { return {"save", "image-clipboard", "file-clipboard", "path-clipboard", "reveal"}; }
QString CaptureActions::actionLabel(const QString &id) {
  static const QMap<QString, QString> names{{"save", "Save image"}, {"image-clipboard", "Copy image to clipboard"},
      {"file-clipboard", "Copy image file"}, {"path-clipboard", "Copy file path"}, {"reveal", "Show saved file in folder"},
      {"after-capture-window", "Choose capture actions"}};
  return names.value(id, id);
}

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports, std::optional<CaptureFile> inheritedFile)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)),
      m_savedPath(inheritedFile ? std::optional<QString>(inheritedFile->path) : std::nullopt), m_savedFile(std::move(inheritedFile)), m_ports(std::move(ports)) {}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  try {
    if (m_ports.actionStarted) m_ports.actionStarted(request.spec.id);
    if (request.spec.id == QStringLiteral("after-capture-window") && m_ports.selection) return m_ports.selection(request.cancellation);
    if (request.spec.id == QStringLiteral("save")) return save();
    if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage(request.cancellation);
    if (request.spec.id == "file-clipboard" || request.spec.id == "path-clipboard" || request.spec.id == "reveal") return outputFile(request);
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
ActionOutcome CaptureActions::outputFile(const ActionRequest &request) {
  if (!m_savedFile || request.inputArtifact != QStringLiteral("save.file")) return failed("saved-file-unavailable");
  const auto validation = validateCaptureFile(*m_savedFile); if (!validation.isEmpty()) return failed(validation);
  if (!m_ports.fileOutput) return failed(kActionUnavailable);
  return m_ports.fileOutput(request.spec.id, *m_savedFile, request.cancellation);
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
