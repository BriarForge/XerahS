#include "CaptureActions.h"
#include "workflows/CaptureWorkflows.h"
#include "EditorCanvasOperations.h"
#include "SecretServiceCredentialStore.h"
#include "uploads/DestinationCatalog.h"
#include "uploads/UploadExecutor.h"
#include <QBuffer>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QStandardPaths>
#include <QTimer>

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
QStringList CaptureActions::selectableActions() { auto actions = xerahs::workflows::implementedCaptureActions(); actions.append("upload"); return actions; }
QString CaptureActions::actionLabel(const QString &id) {
  static const QMap<QString, QString> names{{"save", "Save image"}, {"save-as", "Save image as new PNG…"}, {"image-clipboard", "Copy image to clipboard"},
      {"file-clipboard", "Copy image file"}, {"path-clipboard", "Copy file path"}, {"reveal", "Show saved file in folder"},
      {"after-capture-window", "Choose capture actions"}};
  if (id == "upload") return "Upload capture";
  if (id == "annotation") return "Annotate capture";
  return names.value(id, id);
}

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports, std::optional<CaptureFile> inheritedFile, std::vector<CaptureFile> inheritedFiles, std::optional<xerahs::editor::AnnotationDocument> inheritedDocument, bool sidecarIntent)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)),
      m_savedPath(inheritedFile ? std::optional<QString>(inheritedFile->path) : std::nullopt), m_savedFile(std::move(inheritedFile)), m_ports(std::move(ports)), m_annotationDocument(std::move(inheritedDocument)), m_sidecarIntent(sidecarIntent) {
  m_sidecars.reserve(2); // reserve acknowledgements before durable publication
  if (m_savedFile && m_savedFile->artifact == "save-as.file") { m_savedAsFile = m_savedFile; m_savedFile.reset(); }
  for (const auto &file : inheritedFiles) {
    if (file.artifact == "save.file") m_savedFile = file;
    else if (file.artifact == "save-as.file") m_savedAsFile = file;
    else if (file.artifact == "save.sidecar" || file.artifact == "save-as.sidecar") m_sidecars.push_back(file);
  }
  if (m_savedFile) m_savedPath = m_savedFile->path; else if (m_savedAsFile) m_savedPath = m_savedAsFile->path;
}
std::vector<CaptureFile> CaptureActions::savedFiles() const {
  std::vector<CaptureFile> files; if (m_savedFile) files.push_back(*m_savedFile); if (m_savedAsFile) files.push_back(*m_savedAsFile);
  files.insert(files.end(), m_sidecars.begin(), m_sidecars.end()); return files;
}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  try {
    if (m_ports.actionStarted) m_ports.actionStarted(request.spec.id);
    if (request.spec.id == QStringLiteral("after-capture-window") && m_ports.selection) return m_ports.selection(request.cancellation);
    if (request.spec.id == "upload") return upload(request);
    if (request.spec.id == "annotation") return annotate(request.cancellation);
    if (request.spec.id == QStringLiteral("save")) return save(request.cancellation);
    if (request.spec.id == QStringLiteral("save-as")) return saveAs(request.cancellation);
    if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage(request.cancellation);
    if (request.spec.id == "file-clipboard" || request.spec.id == "path-clipboard" || request.spec.id == "reveal") return outputFile(request);
    return failed(kActionUnavailable);
  } catch (...) { return failed(diagnostic::actionFailed); }
}

bool CaptureActions::createTemporaryUploadFile() {
  // PCA-019: this native upload input is an in-memory PNG, never a persisted file.
  m_uploadBytes.clear(); QBuffer buffer(&m_uploadBytes);
  return buffer.open(QIODevice::WriteOnly) && m_media.save(&buffer, "PNG");
}
ActionOutcome CaptureActions::upload(const ActionRequest &request) {
  using namespace xerahs::uploads;
  if (request.cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  // Secret Service uses blocking D-Bus calls, so this action belongs on the worker.
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    return failed("upload-worker-required");
  const auto path = m_ports.uploadCatalogPath.isEmpty()
      ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("upload-destinations.json")
      : m_ports.uploadCatalogPath;
  DestinationCatalog catalog(path);
  const auto destination = catalog.defaultDestination();
  const auto failure = [](const QString &code, const QString &message) {
    auto out = failed(code); out.upload = UploadResult{{}, {}, {}, message}; return out;
  };
  if (!catalog.loadCode().isEmpty()) return failure(catalog.loadCode(), "Could not read Upload destinations.");
  if (!destination) return failure("upload-destination-missing", "No upload destination. Add one in Upload destinations.");
  UploadInput input; input.mimeType = "image/png";
  if (request.inputArtifact == "save.file" || request.inputArtifact == "save-as.file") {
    const auto &file = request.inputArtifact == "save.file" ? m_savedFile : m_savedAsFile;
    if (!file) return failed("saved-file-unavailable");
    const auto validation = validateCaptureFile(*file); if (!validation.isEmpty()) return failed(validation);
    input.filePath = file->path; input.fileName = QFileInfo(file->path).fileName();
  } else {
    if (m_uploadBytes.isEmpty() && !createTemporaryUploadFile()) return failed(diagnostic::temporaryFileUnavailable);
    input.fileBytes = m_uploadBytes; input.fileName = "capture.png";
  }
  const auto credentials = m_ports.uploadCredentials ? m_ports.uploadCredentials : std::make_shared<SecretServiceCredentialStore>();
  const auto built = buildUploadRequest(*destination, input, *credentials);
  if (request.cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (!built.request) return failure(built.code, built.errorMessage);
  QNetworkAccessManager manager; UploadExecutor executor(&manager); QEventLoop loop; QTimer cancellation;
  cancellation.setInterval(10);
  UploadOutcome response;
  QObject::connect(&executor, &UploadExecutor::finished, &loop, [&](const UploadOutcome &out) { response = out; loop.quit(); });
  QObject::connect(&cancellation, &QTimer::timeout, &executor, [&] { if (request.cancellation.requested()) executor.cancel(); });
  cancellation.start(); executor.start(*built.request); loop.exec();
  if (response.code == "cancelled") return {ActionState::Cancelled, {}, {}};
  if (!response.code.isEmpty()) return failure(response.code, response.errorMessage);
  ActionOutcome out; out.upload = UploadResult{response.url, response.thumbnailUrl, response.deletionUrl, {}}; return out;
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
  if (!parsed.document) return failed(xerahs::editor::diagnostic::documentInvalid);
  QImage acceptedSource = source.image;
  if (choice.sourceRegion) {
    const auto r = *choice.sourceRegion;
    if (!xerahs::editor::validCropRegion(r, source.image.width(), source.image.height()) ||
        parsed.document->canvasWidth != r.width || parsed.document->canvasHeight != r.height)
      return failed(xerahs::editor::diagnostic::documentInvalid);
    xerahs::editor::CanvasOperation op; op.action = xerahs::editor::CanvasAction::Crop;
    op.start = {double(r.left), double(r.top)}; op.end = {double(r.left + r.width), double(r.top + r.height)};
    const auto verified = prepareCanvasEdit(*source.document, source.image, op, {control.cancelled, {}});
    if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
    if (verified.result.error || !verified.result.document || verified.result.document->sourceImagePng != parsed.document->sourceImagePng)
      return failed(xerahs::editor::diagnostic::documentInvalid);
    acceptedSource = verified.source;
  } else if (parsed.document->sourceImagePng != source.document->sourceImagePng ||
      parsed.document->canvasWidth != source.image.width() || parsed.document->canvasHeight != source.image.height())
    return failed(xerahs::editor::diagnostic::documentInvalid);
  const auto rendered = renderEditorImage(acceptedSource, *parsed.document, control);
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (rendered.image.isNull()) return failed(rendered.diagnostic);
  m_media = rendered.image; m_annotationDocument = *parsed.document; m_sidecarIntent = choice.sidecarIntent; return {};
}
ActionOutcome CaptureActions::failedSave(const QString &action, const QString &code) const {
  auto outcome = failed(code);
  if (m_sidecarIntent) outcome.artifacts = {{action + ".file", ActionState::Failed, code}, {action + ".sidecar", ActionState::Skipped, {}}};
  return outcome;
}
ActionOutcome CaptureActions::finishSave(const QString &action, const CaptureFile &raster, const CancellationToken &cancellation) {
  if (!m_sidecarIntent) return {};
  CaptureSidecarResult companion{false, {}, xerahs::editor::diagnostic::sidecarSaveFailed};
  try {
    // A durable acknowledgement of the PNG must precede the companion intent.
    // An observer/journal failure preserves the PNG and blocks the next write.
    if (m_ports.rasterPublished) m_ports.rasterPublished(savedFiles());
    if (m_annotationDocument) companion = saveCaptureSidecar(raster, *m_annotationDocument, cancellation, m_ports.save);
  } catch (...) {}
  if (companion.file) m_sidecars.push_back(*companion.file);
  ActionOutcome outcome;
  outcome.artifacts = {{action + ".file", ActionState::Succeeded, {}}, {action + ".sidecar", companion.succeeded ? ActionState::Succeeded : ActionState::Failed, {}}};
  if (companion.succeeded) outcome.artifacts[1].absent = !companion.file;
  else {
    outcome.state = ActionState::Failed; outcome.diagnostic = companion.diagnostic;
    outcome.artifacts[1].diagnostic = companion.diagnostic;
  }
  return outcome;
}
ActionOutcome CaptureActions::save(const CancellationToken &cancellation) {
  // Only a linked, acknowledged partial save reaches this branch. It repairs
  // the original path's companion; it must never encode or publish another PNG.
  if (m_savedFile) return m_sidecarIntent ? finishSave("save", *m_savedFile, cancellation) : failed("retry-invalid-source");
  const auto result = saveCapture(m_media, m_directory, m_pattern, m_ports.save);
  if (!result.file) return failedSave("save", result.diagnostic);
  m_savedFile = result.file; m_savedPath = result.file->path;
  return finishSave("save", *m_savedFile, cancellation);
}

ActionOutcome CaptureActions::copyImage(const CancellationToken &cancellation) {
  if (m_ports.clipboard) return m_ports.clipboard(m_media, cancellation);
  return copyImageToNativeClipboard(m_media);
}
ActionOutcome CaptureActions::saveAs(const CancellationToken &cancellation) {
  if (m_savedAsFile) return m_sidecarIntent ? finishSave("save-as", *m_savedAsFile, cancellation) : failed("retry-invalid-source");
  if (!m_ports.chooseSaveAs) return failed(kActionUnavailable);
  const auto choice = m_ports.chooseSaveAs(m_directory, cancellation);
  if (cancellation.requested() || (!choice.path && choice.diagnostic.isEmpty())) return {ActionState::Cancelled, {}, {}};
  if (!choice.diagnostic.isEmpty()) return failed(choice.diagnostic);
  const auto result = saveCaptureAs(m_media, *choice.path, cancellation, m_ports.save);
  if (result.cancelled) return {ActionState::Cancelled, {}, {}};
  if (!result.file) return failedSave("save-as", result.diagnostic);
  m_savedAsFile = result.file; if (!m_savedFile) m_savedPath = result.file->path;
  return finishSave("save-as", *m_savedAsFile, cancellation);
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
