#pragma once
#include "actions/PostCapturePipeline.h"
#include <QByteArray>
#include <functional>
namespace xerahs::app {
inline const QString annotationClipboardMime = QStringLiteral("application/x-com-xerahs-linux-qt-annotations+json");
struct AnnotationClipboardRead { QByteArray bytes; QString diagnostic; };
AnnotationClipboardRead readAnnotationClipboard();
xerahs::actions::ActionOutcome publishAnnotationClipboard(const QByteArray &);
struct AnnotationClipboardPorts {
  // GUI-only borrowed bytes, owned copies may be retained. Failed/Cancelled
  // promise no effect; exceptions/uncertain delivery never authorize Cut.
  // Owner deletion is allowed. Reentrant document/output actions stay busy.
  std::function<xerahs::actions::ActionOutcome(const QByteArray &)> publish = publishAnnotationClipboard;
  // GUI-only, returns owned bytes. Read exceptions cannot mutate the document.
  // Empty callbacks disable the affected commands, with no fallback.
  std::function<AnnotationClipboardRead()> read = readAnnotationClipboard;
};
}
