#pragma once
#include "CaptureSave.h"
#include <QDBusConnection>
class QObject;
namespace xerahs::app {
using FileOutputCompletion = std::function<void(xerahs::actions::ActionOutcome)>;
xerahs::actions::ActionOutcome copyCaptureFile(const QString &action, const CaptureFile &file);
// Completion is bounded and exactly once, including owner destruction after
// dispatch. A missing reply is unknown, never permission to repeat the request.
void revealCaptureFile(const CaptureFile &file, QObject *owner, FileOutputCompletion completion,
                       const QDBusConnection &bus = QDBusConnection::sessionBus(), int timeoutMs = 10000);
} // namespace xerahs::app
