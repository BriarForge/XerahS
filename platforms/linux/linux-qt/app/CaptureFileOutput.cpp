#include "CaptureFileOutput.h"
#include <QClipboard>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QDir>
#include <QMimeData>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <memory>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, {}}; }
ActionOutcome unknown() { return {ActionState::Failed, QStringLiteral("reveal-outcome-unknown"), {}, true}; }
struct ReplyCompletion {
  FileOutputCompletion callback; bool completed = false;
  void finish(ActionOutcome outcome) { if (completed) return; completed = true; callback(std::move(outcome)); }
};
}
ActionOutcome publishWorkflowClipboard(std::unique_ptr<QMimeData> prepared, WorkflowClipboardFormat format, ClipboardPrivacy privacy) {
  bool supported = false;
  if (prepared) {
    switch (format) {
      case WorkflowClipboardFormat::Image:
        supported = prepared->hasImage() && !qvariant_cast<QImage>(prepared->imageData()).isNull(); break;
      case WorkflowClipboardFormat::File:
        supported = prepared->hasUrls() && !prepared->urls().isEmpty();
        for (const auto &url : prepared->urls()) supported &= url.isLocalFile() && QDir::isAbsolutePath(url.toLocalFile());
        break;
      case WorkflowClipboardFormat::Path:
        supported = prepared->hasText() && QDir::isAbsolutePath(prepared->text()); break;
      case WorkflowClipboardFormat::Url: {
        const QUrl url(prepared->text(), QUrl::StrictMode);
        supported = prepared->hasText() && url.isValid() && !url.host().isEmpty() &&
            (url.scheme() == "https" || url.scheme() == "http"); break;
      }
      case WorkflowClipboardFormat::Text: supported = prepared->hasText(); break;
    }
  }
  if (!supported) return failed("unsupported-content");
  const auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!app || app->thread() != QThread::currentThread()) return failed("clipboard-unavailable");
  auto *clipboard = QGuiApplication::clipboard(); if (!clipboard) return failed("clipboard-unavailable");
  bool attempted = false;
  try {
    prepared->setData(clipboardPrivacyMime, privacy == ClipboardPrivacy::Private ? "private" : "public");
    const QPointer<QMimeData> offered(prepared.get());
    attempted = true; clipboard->setMimeData(prepared.release());
    if (offered && clipboard->mimeData() == offered.data() && clipboard->ownsClipboard()) return {};
  } catch (...) { if (!attempted) return failed("clipboard-failed"); }
  auto result = failed("clipboard-delivery-unknown"); result.outcomeUnknown = true; return result;
}
ActionOutcome copyCaptureFile(const QString &action, const CaptureFile &file) {
  const auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!app || app->thread() != QThread::currentThread()) return failed("clipboard-unavailable");
  const auto validation = validateCaptureFile(file); if (!validation.isEmpty()) return failed(validation);
  auto data = std::make_unique<QMimeData>();
  if (action == "file-clipboard") {
    const auto url = QUrl::fromLocalFile(file.path);
    data->setUrls({url}); data->setData("x-special/gnome-copied-files", "copy\n" + url.toEncoded());
  } else if (action == "path-clipboard") data->setText(file.path);
  else return failed("action-unavailable");
  return publishWorkflowClipboard(std::move(data), action == "file-clipboard" ? WorkflowClipboardFormat::File : WorkflowClipboardFormat::Path);
}
void revealCaptureFile(const CaptureFile &file, QObject *owner, FileOutputCompletion completion,
                       const QDBusConnection &bus, int timeoutMs) {
  const auto validation = validateCaptureFile(file);
  if (!validation.isEmpty()) { completion(failed(validation)); return; }
  if (!owner || owner->thread() != QThread::currentThread() || !bus.isConnected()) { completion(failed("reveal-unavailable")); return; }
  auto message = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
      "org.freedesktop.FileManager1", "ShowItems");
  message << QStringList{QUrl::fromLocalFile(file.path).toString(QUrl::FullyEncoded)} << QString{};
  const auto state = std::make_shared<ReplyCompletion>(); state->callback = std::move(completion);
  auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, qBound(1, timeoutMs, 10000)), owner);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [state](QDBusPendingCallWatcher *call) {
    const QPointer<QDBusPendingCallWatcher> guard(call); const auto ownedState = state;
    const auto reply = call->reply();
    if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().isEmpty()) state->finish({});
    else {
      const QDBusError error(reply);
      const auto type = error.type();
      const bool uncertain = reply.type() != QDBusMessage::ErrorMessage || type == QDBusError::NoReply ||
          type == QDBusError::Timeout || type == QDBusError::TimedOut || type == QDBusError::Disconnected;
      ownedState->finish(uncertain ? unknown() : failed("reveal-unavailable"));
    }
    if (guard) guard->deleteLater();
  });
  // No QObject receiver: destruction must release a worker even as its owning
  // pipeline is destroyed. Only the independent typed state survives here.
  QObject::connect(watcher, &QObject::destroyed, [state] { state->finish(unknown()); });
}
} // namespace xerahs::app
