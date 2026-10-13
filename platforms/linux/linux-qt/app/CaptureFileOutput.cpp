#include "CaptureFileOutput.h"
#include <QClipboard>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
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
ActionOutcome copyCaptureFile(const QString &action, const CaptureFile &file) {
  const auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  if (!app || app->thread() != QThread::currentThread()) return failed("clipboard-unavailable");
  const auto validation = validateCaptureFile(file); if (!validation.isEmpty()) return failed(validation);
  auto *clipboard = QGuiApplication::clipboard();
  if (action == "file-clipboard") {
    const auto url = QUrl::fromLocalFile(file.path); auto *data = new QMimeData;
    data->setUrls({url}); data->setData("x-special/gnome-copied-files", "copy\n" + url.toEncoded());
    clipboard->setMimeData(data);
  } else if (action == "path-clipboard") clipboard->setText(file.path);
  else return failed("action-unavailable");
  return clipboard->ownsClipboard() ? ActionOutcome{} : failed("clipboard-unavailable");
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
