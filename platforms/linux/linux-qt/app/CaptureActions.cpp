#include "CaptureActions.h"
#include "workflows/CaptureWorkflows.h"
#include "EditorCanvasOperations.h"
#include "EditorPrint.h"
#include "PinWindow.h"
#include "ImageRecognitionDialog.h"
#include "SecretServiceCredentialStore.h"
#include "uploads/CatalogPath.h"
#include "uploads/DestinationCatalog.h"
#include "uploads/UploadExecutor.h"
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QTimer>

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QThread>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>

namespace xerahs::app {

using namespace xerahs::actions;

namespace {

ActionOutcome failed(const QString &diagnostic) {
  return ActionOutcome{ActionState::Failed, diagnostic, std::nullopt};
}

}  // namespace

const QString CaptureActions::kActionUnavailable = QStringLiteral("action-unavailable");
QStringList CaptureActions::selectableActions() { return xerahs::workflows::implementedCaptureActions(); }
QString CaptureActions::actionLabel(const QString &id) {
  static const QMap<QString, QString> names{{"save", "Save image"}, {"save-as", "Save image as new PNG…"}, {"image-clipboard", "Copy image to clipboard"},
      {"file-clipboard", "Copy image file"}, {"path-clipboard", "Copy file path"}, {"reveal", "Show saved file in folder"},
      {"after-capture-window", "Choose capture actions"}, {"quick-task-menu", "Show quick task menu"}};
  if (id == "upload") return "Upload capture";
  if (id == "before-upload-window") return "Confirm before uploading";
  if (id == "pin") return "Pin image on screen";
  if (id == "print") return "Print image";
  if (id == "url-clipboard") return "Copy URL to clipboard";
  if (id == "annotation") return "Annotate capture";
  if (id == "ocr") return "Recognize text (OCR)";
  if (id == "ocr-clipboard") return "Copy recognized text";
  if (id == "qr-scan") return "Scan QR codes";
  if (id == "delete") return "Delete saved capture after successful file actions";
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
    if ((request.spec.id == "after-capture-window" || request.spec.id == "quick-task-menu") && m_ports.selection)
      return m_ports.selection(request.spec.id, request.cancellation);
    if (request.spec.id == "before-upload-window") return beforeUpload(request.cancellation);
    if (request.spec.id == "upload") return upload(request);
    if (request.spec.id == "url-clipboard") return copyUrl(request.cancellation);
    if (request.spec.id == "annotation") return annotate(request.cancellation);
    if (request.spec.id == "ocr") return recognize(ImageRecognitionKind::Text, request.cancellation);
    if (request.spec.id == "qr-scan") return recognize(ImageRecognitionKind::QrCode, request.cancellation);
    if (request.spec.id == "ocr-clipboard") return copyRecognizedText(request.cancellation);
    if (request.spec.id == QStringLiteral("save")) return save(request.cancellation);
    if (request.spec.id == QStringLiteral("save-as")) return saveAs(request.cancellation);
    if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage(request.cancellation);
    if (request.spec.id == "pin") return pin(request.cancellation);
    if (request.spec.id == "print") return print(request.cancellation);
    if (request.spec.id == "delete") return removeFile(request);
    if (request.spec.id == "file-clipboard" || request.spec.id == "path-clipboard" || request.spec.id == "reveal") return outputFile(request);
    return failed(kActionUnavailable);
  } catch (...) { return failed(diagnostic::actionFailed); }
}

ActionOutcome CaptureActions::recognize(ImageRecognitionKind kind, const CancellationToken &cancellation) {
  const xerahs::editor::RenderControl control{[&] { return cancellation.requested(); }, {}};
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    return failed("recognition-worker-required");
  const auto result = kind == ImageRecognitionKind::Text ? recognizeTextImage(m_media, control) : scanQrImage(m_media, control);
  if (cancellation.requested() || result.cancelled) return {ActionState::Cancelled, {}, {}};
  if (!result.diagnostic.isEmpty()) return failed(result.diagnostic);
  const auto shown = m_ports.recognitionResult ? m_ports.recognitionResult(kind, result.values, cancellation)
      : failed("recognition-result-unavailable");
  if (shown.state == ActionState::Succeeded && !shown.outcomeUnknown && kind == ImageRecognitionKind::Text)
    m_recognizedText = result.values.join('\n');
  return shown;
}
ActionOutcome CaptureActions::copyRecognizedText(const CancellationToken &cancellation) {
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (m_recognizedText.isEmpty()) return {ActionState::Succeeded, QStringLiteral("ocr-no-text"), {}};
  return m_ports.textClipboard ? m_ports.textClipboard(m_recognizedText, cancellation) : failed("clipboard-unavailable");
}
ActionOutcome CaptureActions::showNativeRecognition(ImageRecognitionKind kind, const QStringList &values) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread()) return failed("recognition-result-unavailable");
  auto dialog = std::make_unique<ImageRecognitionDialog>(kind);
  dialog->showRecognizedValues(values);
  dialog->show();
  dialog.release();
  return {};
}

bool CaptureActions::createTemporaryUploadFile() {
  removeTemporaryUploadFile();
  const QDir directory(m_ports.uploadTemporaryDirectory.isEmpty() ? QDir::tempPath() : m_ports.uploadTemporaryDirectory);
  auto file = std::make_unique<QTemporaryFile>(directory.filePath(QStringLiteral("xerahs-upload-XXXXXX.png")));
  if (!file->open() || !file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
      !m_media.save(file.get(), "PNG") || !file->flush() || file->fileName().isEmpty()) return false;
  m_uploadFile = std::move(file);
  return true;
}
ActionOutcome CaptureActions::beforeUpload(const CancellationToken &cancellation) {
  using namespace xerahs::uploads;
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    return failed("upload-worker-required");
  DestinationCatalog catalog(defaultCatalogPath(m_ports.uploadCatalogPath));
  if (!catalog.loadCode().isEmpty()) return failed(catalog.loadCode());
  const auto destination = catalog.defaultDestination();
  if (!destination) return failed("upload-destination-missing");
  const auto problems = validateDestination(*destination);
  if (!problems.isEmpty()) return failed(problems.first());
  // Only an origin crosses the GUI boundary. Request paths, templates and
  // credentials remain on the worker, and the approved destination is frozen.
  const QUrl requestUrl(destination->requestUrl);
  QUrl origin; origin.setScheme(requestUrl.scheme()); origin.setHost(requestUrl.host()); origin.setPort(requestUrl.port());
  CaptureUploadReview review{m_media.scaled(QSize(640, 480), Qt::KeepAspectRatio, Qt::SmoothTransformation).copy(),
      m_media.size(), destination->name, origin};
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (review.preview.isNull()) return failed("upload-preview-unavailable");
  auto result = m_ports.beforeUpload ? m_ports.beforeUpload(review, cancellation) : failed("upload-confirmation-unavailable");
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (result.state == ActionState::Succeeded && !result.outcomeUnknown) m_confirmedUploadDestination = *destination;
  return result;
}
ActionOutcome CaptureActions::confirmNativeUpload(const CaptureUploadReview &review, const CancellationToken &cancellation) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread()) return failed("upload-confirmation-unavailable");
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  QMessageBox dialog(QMessageBox::Question, "Upload capture?", {}, QMessageBox::Yes | QMessageBox::Cancel);
  dialog.setObjectName("captureBeforeUploadDialog");
  dialog.setTextFormat(Qt::PlainText);
  dialog.setText(QString("Upload this %1 × %2 image to %3?\n%4\n\nThe image will leave this device and be sent to this destination.")
      .arg(review.originalDimensions.width()).arg(review.originalDimensions.height()).arg(review.destinationName, review.origin.toString()));
  if (review.origin.scheme() == "http") dialog.setInformativeText("Warning: HTTP sends the image without transport encryption.");
  dialog.setIconPixmap(QPixmap::fromImage(review.preview));
  dialog.button(QMessageBox::Yes)->setText("Upload");
  dialog.setDefaultButton(QMessageBox::Cancel);
  dialog.setEscapeButton(QMessageBox::Cancel);
  QTimer cancellationPoll;
  QObject::connect(&cancellationPoll, &QTimer::timeout, &dialog, [&] { if (cancellation.requested()) dialog.reject(); });
  cancellationPoll.start(10);
  const auto choice = dialog.exec();
  return {(!cancellation.requested() && choice == QMessageBox::Yes) ? ActionState::Succeeded : ActionState::Cancelled, {}, {}};
}
ActionOutcome CaptureActions::upload(const ActionRequest &request) {
  using namespace xerahs::uploads;
  if (request.cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  // Secret Service uses blocking D-Bus calls, so this action belongs on the worker.
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    return failed("upload-worker-required");
  const auto path = defaultCatalogPath(m_ports.uploadCatalogPath);
  DestinationCatalog catalog(path);
  const auto destination = m_confirmedUploadDestination ? m_confirmedUploadDestination : catalog.defaultDestination();
  const auto failure = [](const QString &code, const QString &message) {
    auto out = failed(code); out.upload = UploadResult{{}, {}, {}, message}; return out;
  };
  if (!m_confirmedUploadDestination && !catalog.loadCode().isEmpty()) return failure(catalog.loadCode(), "Could not read Upload destinations.");
  if (!destination) return failure("upload-destination-missing", "No upload destination. Add one in Upload destinations.");
  UploadInput input; input.mimeType = "image/png";
  if (request.inputArtifact == "save.file" || request.inputArtifact == "save-as.file") {
    const auto &file = request.inputArtifact == "save.file" ? m_savedFile : m_savedAsFile;
    if (!file) return failed("saved-file-unavailable");
    const auto validation = validateCaptureFile(*file); if (!validation.isEmpty()) return failed(validation);
    input.filePath = file->path; input.fileName = QFileInfo(file->path).fileName();
  } else {
    if (!m_uploadFile && !createTemporaryUploadFile()) return failed(diagnostic::temporaryFileUnavailable);
    input.filePath = m_uploadFile->fileName(); input.fileName = "capture.png";
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
  m_uploadUrl = response.url;
  ActionOutcome out; out.upload = UploadResult{response.url, response.thumbnailUrl, response.deletionUrl, {}}; return out;
}

// PCA-024: only the upload URL, as plain text; a clipboard failure fails this action alone.
ActionOutcome CaptureActions::copyUrl(const CancellationToken &cancellation) {
  if (m_uploadUrl.isEmpty()) return failed("upload-url-unavailable");
  if (m_ports.textClipboard) return m_ports.textClipboard(m_uploadUrl, cancellation);
  return copyTextToNativeClipboard(m_uploadUrl);
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
  if (!choice.rendered.isNull() && choice.rendered != rendered.image) return failed("editor-task-result-invalid");
  m_media = choice.rendered.isNull() ? rendered.image : choice.rendered; m_annotationDocument = *parsed.document; m_sidecarIntent = choice.sidecarIntent;
  ActionOutcome outcome;
  if (!choice.partialDiagnostic.isEmpty()) outcome.artifacts = {{"editor.task", ActionState::Succeeded, {}}, {"editor.auto-copy", ActionState::Failed, choice.partialDiagnostic}};
  return outcome;
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
  m_currentPublications.append("save.file");
  // FN-016/PCA-007: retain the acknowledged PNG, but stop the save here if
  // its counter could not be persisted. Never invent a sidecar acknowledgement.
  if (!result.diagnostic.isEmpty()) return failed(result.diagnostic);
  return finishSave("save", *m_savedFile, cancellation);
}

ActionOutcome CaptureActions::copyImage(const CancellationToken &cancellation) {
  if (m_ports.clipboard) return m_ports.clipboard(m_media, cancellation);
  return copyImageToNativeClipboard(m_media);
}
ActionOutcome CaptureActions::pin(const CancellationToken &cancellation) {
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (m_media.isNull()) return failed("pin-unavailable");
  if (m_ports.pin) return m_ports.pin(m_media, cancellation);
  return pinToNativeScreen(m_media);
}
ActionOutcome CaptureActions::print(const CancellationToken &cancellation) {
  const xerahs::editor::RenderControl control{[&] { return cancellation.requested(); }, {}};
  // The document that print renders is built here, off the GUI thread.
  const auto source = prepareCapturedSource(m_media, control);
  if (cancellation.requested()) return {ActionState::Cancelled, {}, {}};
  if (!source.document) return failed("print-render-failed");
  if (m_ports.print) return m_ports.print(source, cancellation);
  return printNativeImage(source);
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
  m_currentPublications.append("save-as.file");
  return finishSave("save-as", *m_savedAsFile, cancellation);
}
ActionOutcome CaptureActions::outputFile(const ActionRequest &request) {
  const auto &file = request.inputArtifact == QStringLiteral("save.file") ? m_savedFile : m_savedAsFile;
  if (!file || request.inputArtifact != file->artifact) return failed("saved-file-unavailable");
  const auto validation = validateCaptureFile(*file); if (!validation.isEmpty()) return failed(validation);
  if (!m_ports.fileOutput) return failed(kActionUnavailable);
  return m_ports.fileOutput(request.spec.id, *file, request.cancellation);
}
ActionOutcome CaptureActions::removeFile(const ActionRequest &request) {
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread())
    return failed("delete-worker-required");
  auto &file = request.inputArtifact == QStringLiteral("save.file") ? m_savedFile : m_savedAsFile;
  if (!file || request.inputArtifact != file->artifact || !m_currentPublications.contains(file->artifact)) return failed("delete-guard");
  return deleteCaptureFile(*file, request.cancellation, m_ports.deletion);
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

ActionOutcome CaptureActions::pinToNativeScreen(const QImage &media) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread()) return failed(QStringLiteral("pin-unavailable"));
  auto *window = new PinWindow(media);
  window->show(); window->raise(); window->activateWindow();
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

// Reuses the editor's print path (preview, native dialog, worker submission).
// Its Cancelled/Failed outcomes already carry the action's stable codes.
ActionOutcome CaptureActions::printNativeImage(const LoadedSource &source) {
  if (!source.document) return failed(QStringLiteral("print-render-failed"));
  const auto printed = runEditorPrint(source.image, *source.document, nullptr);
  return printed.action;
}

ActionOutcome CaptureActions::copyTextToNativeClipboard(const QString &text) {
  const auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread()) return failed(QStringLiteral("clipboard-unavailable"));
  QClipboard *clipboard = QGuiApplication::clipboard();
  clipboard->setText(text);
  if (!clipboard->ownsClipboard()) return failed(QStringLiteral("clipboard-unavailable"));
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

}  // namespace xerahs::app
