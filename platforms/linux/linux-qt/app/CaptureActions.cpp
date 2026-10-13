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
QStringList CaptureActions::selectableActions() { return {"annotation", "save", "save-as", "image-clipboard", "file-clipboard", "path-clipboard", "reveal"}; }
QString CaptureActions::actionLabel(const QString &id) {
  static const QMap<QString, QString> names{{"save", "Save image"}, {"save-as", "Save image as new PNG…"}, {"image-clipboard", "Copy image to clipboard"},
      {"file-clipboard", "Copy image file"}, {"path-clipboard", "Copy file path"}, {"reveal", "Show saved file in folder"},
      {"after-capture-window", "Choose capture actions"}};
  if (id == "annotation") return "Annotate capture";
  return names.value(id, id);
}

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports, std::optional<CaptureFile> inheritedFile, std::vector<CaptureFile> inheritedFiles, std::optional<xerahs::editor::AnnotationDocument> inheritedDocument)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)),
      m_savedPath(inheritedFile ? std::optional<QString>(inheritedFile->path) : std::nullopt), m_savedFile(std::move(inheritedFile)), m_ports(std::move(ports)), m_annotationDocument(std::move(inheritedDocument)) {
  if (m_savedFile && m_savedFile->artifact == "save-as.file") { m_savedAsFile = m_savedFile; m_savedFile.reset(); }
  for (const auto &file : inheritedFiles) {
    if (file.artifact == "save.file") m_savedFile = file;
    else if (file.artifact == "save-as.file") m_savedAsFile = file;
  }
  if (m_savedFile) m_savedPath = m_savedFile->path; else if (m_savedAsFile) m_savedPath = m_savedAsFile->path;
}
std::vector<CaptureFile> CaptureActions::savedFiles() const {
  std::vector<CaptureFile> files; if (m_savedFile) files.push_back(*m_savedFile); if (m_savedAsFile) files.push_back(*m_savedAsFile); return files;
}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  try {
    if (m_ports.actionStarted) m_ports.actionStarted(request.spec.id);
    if (request.spec.id == QStringLiteral("after-capture-window") && m_ports.selection) return m_ports.selection(request.cancellation);
    if (request.spec.id == "annotation") return annotate(request.cancellation);
    if (request.spec.id == QStringLiteral("save")) return save();
    if (request.spec.id == QStringLiteral("save-as")) return saveAs(request.cancellation);
    if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage(request.cancellation);
    if (request.spec.id == "file-clipboard" || request.spec.id == "path-clipboard" || request.spec.id == "reveal") return outputFile(request);
    return failed(kActionUnavailable);
  } catch (...) { return failed(diagnostic::actionFailed); }
}

ActionOutcome CaptureActions::annotate(const CancellationToken &cancellation) {
  const xerahs::editor::RenderControl control{[&] { return cancellation.requested(); }, {}};
  auto source = prepareCapturedSource(m_media, control);
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (!source.document) return failed(source.diagnostic);
  if (!m_ports.annotation) return failed(kActionUnavailable);
  const auto choice = m_ports.annotation(source, cancellation);
  if (cancellation.requested() || (!choice.document && choice.diagnostic.isEmpty())) return {ActionState::Cancelled, {}, {}};
  if (!choice.diagnostic.isEmpty()) return failed(choice.diagnostic);
  const auto parsed = xerahs::editor::parseDocument(xerahs::editor::serializeDocument(*choice.document));
  if (!parsed.document || parsed.document->sourceImagePng != source.document->sourceImagePng ||
      parsed.document->canvasWidth != source.image.width() || parsed.document->canvasHeight != source.image.height())
    return failed(xerahs::editor::diagnostic::documentInvalid);
  const auto rendered = renderEditorImage(source.image, *parsed.document, control);
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (rendered.image.isNull()) return failed(rendered.diagnostic);
  m_media = rendered.image; m_annotationDocument = *parsed.document; return {};
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
ActionOutcome CaptureActions::saveAs(const CancellationToken &cancellation) {
  if (!m_ports.chooseSaveAs) return failed(kActionUnavailable);
  const auto choice = m_ports.chooseSaveAs(m_directory, cancellation);
  if (cancellation.requested() || (!choice.path && choice.diagnostic.isEmpty())) return {ActionState::Cancelled, {}, {}};
  if (!choice.diagnostic.isEmpty()) return failed(choice.diagnostic);
  const auto result = saveCaptureAs(m_media, *choice.path, cancellation, m_ports.save);
  if (result.cancelled) return {ActionState::Cancelled, {}, {}};
  if (!result.file) return failed(result.diagnostic);
  m_savedAsFile = result.file; if (!m_savedFile) m_savedPath = result.file->path; return {};
}
ActionOutcome CaptureActions::outputFile(const ActionRequest &request) {
  const auto &file = request.inputArtifact == QStringLiteral("save.file") ? m_savedFile : m_savedAsFile;
  if (!file || request.inputArtifact != file->artifact) return failed("saved-file-unavailable");
  const auto validation = validateCaptureFile(*file); if (!validation.isEmpty()) return failed(validation);
  if (!m_ports.fileOutput) return failed(kActionUnavailable);
  return m_ports.fileOutput(request.spec.id, *file, request.cancellation);
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
