#pragma once
#include "CaptureSave.h"
#include <QDBusConnection>
#include <QMimeData>
#include <memory>
class QObject;
namespace xerahs::app {
enum class WorkflowClipboardFormat { Image, File, Path, Url, Text };
enum class ClipboardPrivacy { Private, Public };
inline const QString clipboardPrivacyMime = QStringLiteral("application/x-com-xerahs-clipboard-privacy");
// A fully prepared, owned offer is checked before the native clipboard changes.
// Missing/mismatched representations return unsupported-content without an effect.
// Contents never enter diagnostics; private is the conservative default.
xerahs::actions::ActionOutcome publishWorkflowClipboard(std::unique_ptr<QMimeData> prepared,
    WorkflowClipboardFormat format, ClipboardPrivacy privacy = ClipboardPrivacy::Private);
using FileOutputCompletion = std::function<void(xerahs::actions::ActionOutcome)>;
xerahs::actions::ActionOutcome copyCaptureFile(const QString &action, const CaptureFile &file);
// Completion is bounded and exactly once, including owner destruction after
// dispatch. A missing reply is unknown, never permission to repeat the request.
void revealCaptureFile(const CaptureFile &file, QObject *owner, FileOutputCompletion completion,
                       const QDBusConnection &bus = QDBusConnection::sessionBus(), int timeoutMs = 10000);
} // namespace xerahs::app
