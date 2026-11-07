#include "EditorUpload.h"
#include "ComparisonImages.h"
#include "EditorPreparation.h"
#include "SecretServiceCredentialStore.h"
#include "uploads/CatalogPath.h"
#include "uploads/DestinationCatalog.h"
#include "uploads/UploadExecutor.h"
#include <QApplication>
#include <QAbstractButton>
#include <QBuffer>
#include <QEventLoop>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <atomic>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, {}}; }
ActionOutcome cancelled() { return {ActionState::Cancelled, {}, {}}; }
ActionOutcome unknown() { auto out = failed("upload-delivery-unknown"); out.outcomeUnknown = true; return out; }
bool stopping(const xerahs::editor::RenderControl &control) { return control.cancelled && control.cancelled(); }
QString safeDiagnostic(const std::optional<QString> &diagnostic) {
  static const QSet<QString> codes{"upload-unavailable", "upload-destination-missing", "upload-destination-unreadable",
      "upload-confirmation-failed", "upload-worker-required", "missing-secret", "credential-unavailable",
      "input-too-large", "input-unreadable", "invalid-header", "invalid-content-type", "empty-id", "empty-name",
      "unsupported-method", "empty-url", "invalid-url", "non-http-scheme", "url-userinfo", "unsupported-body",
      "missing-file-form-name", "http-error", "parse-failed", "invalid-result-url", "response-too-large"};
  return diagnostic && codes.contains(*diagnostic) ? *diagnostic : QStringLiteral("upload-failed");
}
bool webUrl(const QString &text) {
  const QUrl url(text, QUrl::StrictMode);
  return url.isValid() && (url.scheme() == "http" || url.scheme() == "https") && !url.host().isEmpty() && url.userInfo().isEmpty();
}
}
EditorUploadChoice chooseEditorUpload(const EditorUploadSnapshot &, QWidget *parent, const QString &catalogPath) {
  struct Loaded { std::optional<uploads::UploadDestination> destination; QString error; };
  const auto loaded = std::make_shared<Loaded>();
  const auto ready = runEditorPreparation(parent,
      {"uploadDestinationProgress", QCoreApplication::translate("EditorUpload", "Upload Image"), QCoreApplication::translate("EditorUpload", "Reading the configured upload destination…"), QCoreApplication::translate("EditorUpload", "Upload destination progress")},
      [loaded, catalogPath](const auto &control) {
        if (stopping(control)) return;
        uploads::DestinationCatalog catalog(uploads::defaultCatalogPath(catalogPath));
        if (!catalog.loadCode().isEmpty()) loaded->error = "upload-destination-unreadable";
        else if (!(loaded->destination = catalog.defaultDestination())) loaded->error = "upload-destination-missing";
      });
  if (ready == EditorPreparationResult::Cancelled) return {};
  if (ready != EditorPreparationResult::Ready) return {failed("upload-destination-unreadable"), {}};
  if (!loaded->destination) return {failed(loaded->error), {}};
  const auto destination = *loaded->destination;
  const QUrl target(destination.requestUrl);
  const QString host = target.host().isEmpty() ? QCoreApplication::translate("EditorUpload", "configured HTTP destination")
      : target.adjusted(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment).toDisplayString();
  QPointer<QMessageBox> dialog(new QMessageBox(QMessageBox::Question, QCoreApplication::translate("EditorUpload", "Upload Image?"),
      QCoreApplication::translate("EditorUpload", "Upload the rendered image to %1 (%2)?\n\nThe PNG includes source pixels and visible supported annotations, converted to sRGB with alpha. The host may make the image public and retain it even if you cancel after transmission. The editable document stays open and unchanged.").arg(destination.name, host),
      QMessageBox::Ok | QMessageBox::Cancel, parent));
  if (target.scheme() == "http") dialog->setInformativeText(QCoreApplication::translate("EditorUpload", "This destination uses unencrypted HTTP."));
  dialog->setObjectName("confirmEditorUpload"); dialog->setTextFormat(Qt::PlainText);
  dialog->setDefaultButton(QMessageBox::Cancel); dialog->setEscapeButton(QMessageBox::Cancel);
  dialog->button(QMessageBox::Ok)->setText(QCoreApplication::translate("EditorUpload", "Upload"));
  const auto answer = dialog->exec(); delete dialog.data();
  return answer == QMessageBox::Ok ? EditorUploadChoice{ActionOutcome{}, destination} : EditorUploadChoice{};
}
ActionOutcome submitEditorUpload(const EditorUploadSnapshot &snapshot, const uploads::UploadDestination &destination,
    const xerahs::editor::RenderControl &control, const std::shared_ptr<uploads::CredentialStore> &credentials) {
  if (stopping(control)) return cancelled();
  if (QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread()) return failed("upload-worker-required");
  const auto store = credentials ? credentials : std::make_shared<SecretServiceCredentialStore>();
  uploads::UploadInput input; input.fileName = "image.png"; input.mimeType = "image/png"; input.fileBytes = snapshot.png;
  const auto request = uploads::buildUploadRequest(destination, input, *store);
  if (stopping(control)) return cancelled();
  if (!request.request) return failed(request.code);
  QNetworkAccessManager manager; uploads::UploadExecutor executor(&manager); QEventLoop loop; QTimer timer;
  uploads::UploadOutcome response; bool completed = false;
  QObject::connect(&executor, &uploads::UploadExecutor::finished, &loop, [&](const auto &value) { response = value; completed = true; loop.quit(); });
  QObject::connect(&executor, &uploads::UploadExecutor::progress, &loop, [&](qint64 sent, qint64 total) {
    if (control.progress && total > 0) control.progress(int(qMin<qint64>(100, sent * 100 / total)));
  });
  QObject::connect(&timer, &QTimer::timeout, &executor, [&] { if (stopping(control)) executor.cancel(); });
  timer.start(10); executor.start(*request.request); if (!completed) loop.exec();
  if (!response.code.isEmpty()) {
    auto out = (response.code == "cancelled" || response.code == "timeout" || response.code == "network-error") ? unknown() : failed(response.code);
    out.upload = UploadResult{{}, {}, {}, {}, response.attempts}; return out;
  }
  ActionOutcome outcome; outcome.upload = UploadResult{response.url, response.thumbnailUrl, response.deletionUrl, {}, response.attempts}; return outcome;
}
EditorUploadOutcome runEditorUpload(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                                   QWidget *parent, const EditorUploadPorts &ports) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || !ports.confirm || !ports.submit) return {failed("upload-unavailable")};
  const QPointer<QWidget> owner(parent);
  struct Prepared { EditorUploadSnapshot snapshot; QString diagnostic; };
  const auto prepared = std::make_shared<Prepared>();
  const auto ready = runEditorPreparation(parent,
      {"uploadImageProgress", QCoreApplication::translate("EditorUpload", "Upload Image"), QCoreApplication::translate("EditorUpload", "Rendering source and visible annotations as sRGB with alpha…"), QCoreApplication::translate("EditorUpload", "Upload image progress")},
      [source, document, prepared, beforeRender = ports.beforeRender](const auto &control) {
        if (beforeRender) beforeRender();
        if (stopping(control)) return;
        if (!xerahs::editor::parseDocument(xerahs::editor::serializeDocument(document)).document ||
            source.width() != document.canvasWidth || source.height() != document.canvasHeight) { prepared->diagnostic = "upload-render-failed"; return; }
        const auto rendered = prepareComparisonImage(source, document.annotations, control);
        if (rendered.image.isNull()) { prepared->diagnostic = rendered.diagnostic == "comparison-annotation-unsupported" ? "upload-annotation-unsupported" : "upload-render-failed"; return; }
        prepared->snapshot.image = rendered.image;
        QBuffer png(&prepared->snapshot.png);
        if (!png.open(QIODevice::WriteOnly) || !rendered.image.save(&png, "PNG")) prepared->diagnostic = "upload-encode-failed";
      });
  if (ready == EditorPreparationResult::Cancelled || (parent && !owner)) return {cancelled()};
  if (ready != EditorPreparationResult::Ready || !prepared->diagnostic.isEmpty() || prepared->snapshot.png.isEmpty())
    return {failed(prepared->diagnostic.isEmpty() ? QStringLiteral("upload-render-failed") : prepared->diagnostic)};
  EditorUploadChoice choice;
  try { choice = ports.confirm(prepared->snapshot, parent); } catch (...) { return {failed("upload-confirmation-failed")}; }
  if (parent && !owner) return {cancelled()};
  if (choice.action.outcomeUnknown || choice.action.replacementSelection || !choice.action.artifacts.empty()) return {failed("upload-confirmation-failed")};
  if (choice.action.state != ActionState::Succeeded) return {choice.action.state == ActionState::Cancelled ? cancelled() : failed(safeDiagnostic(choice.action.diagnostic))};
  if (!choice.destination) return {failed("upload-unavailable")};
  struct Submitted { ActionOutcome action; std::atomic<bool> entered{false}; };
  const auto submitted = std::make_shared<Submitted>();
  const auto transfer = runEditorPreparation(parent,
      {"uploadTransferProgress", QCoreApplication::translate("EditorUpload", "Upload Image"), QCoreApplication::translate("EditorUpload", "Uploading the confirmed image. Cancellation cannot remove an image already received by the host…"), QCoreApplication::translate("EditorUpload", "Upload transfer progress")},
      [submitted, snapshot = prepared->snapshot, destination = *choice.destination, submit = ports.submit](const auto &control) {
        if (stopping(control)) { submitted->action = cancelled(); return; }
        submitted->entered.store(true);
        try { submitted->action = submit(snapshot, destination, control); } catch (...) { submitted->action = unknown(); }
      });
  if (transfer == EditorPreparationResult::Cancelled || (parent && !owner)) return {submitted->entered.load() ? unknown() : cancelled()};
  if (transfer != EditorPreparationResult::Ready) return {unknown()};
  auto action = submitted->action;
  const auto metadata = [attempts = action.upload ? action.upload->attempts : 0](ActionOutcome result) {
    if (attempts > 0) result.upload = UploadResult{{}, {}, {}, {}, attempts};
    return result;
  };
  if (action.outcomeUnknown || action.state == ActionState::Skipped || action.replacementSelection || !action.artifacts.empty()) return {metadata(unknown())};
  if (action.state == ActionState::Succeeded) {
    if (!action.upload || !webUrl(action.upload->url) ||
        (!action.upload->thumbnailUrl.isEmpty() && !webUrl(action.upload->thumbnailUrl)) ||
        (!action.upload->deletionUrl.isEmpty() && !webUrl(action.upload->deletionUrl))) return {metadata(unknown())};
    action.diagnostic.reset(); action.upload->errorMessage.clear(); return {action};
  }
  return {metadata(action.state == ActionState::Cancelled ? cancelled() : failed(safeDiagnostic(action.diagnostic)))};
}
}
