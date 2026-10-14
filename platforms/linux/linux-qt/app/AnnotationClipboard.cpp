#include "AnnotationClipboard.h"
#include "image-editor/AnnotationTransfer.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QThread>
#include <memory>
namespace xerahs::app {
using namespace xerahs::actions;
namespace {
QClipboard *clipboard() {
  auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
  return app && QThread::currentThread() == app->thread() ? QGuiApplication::clipboard() : nullptr;
}
}
AnnotationClipboardRead readAnnotationClipboard() {
  auto *service = clipboard(); if (!service) return {{}, "annotation-clipboard-unavailable"};
  try {
    const auto *mime = service->mimeData();
    if (!mime || !mime->hasFormat(annotationClipboardMime)) return {{}, "annotation-clipboard-format-unavailable"};
    auto bytes = mime->data(annotationClipboardMime); // Qt owns native retrieval; no streaming API
    if (bytes.size() > xerahs::editor::kMaxAnnotationClipboardBytes) return {{}, "annotation-transfer-too-large"};
    return {std::move(bytes), {}};
  } catch (...) { return {{}, "annotation-clipboard-read-failed"}; }
}
ActionOutcome publishAnnotationClipboard(const QByteArray &bytes) {
  auto *service = clipboard(); if (!service) return {ActionState::Failed, "annotation-clipboard-unavailable", std::nullopt};
  const auto parsed = xerahs::editor::parseAnnotationClipboard(bytes);
  if (!parsed.ok()) return {ActionState::Failed, parsed.diagnostic, std::nullopt};
  bool attempted = false;
  try {
    auto mime = std::make_unique<QMimeData>(); mime->setData(annotationClipboardMime, bytes); const QPointer<QMimeData> offered(mime.get());
    attempted = true; service->setMimeData(mime.release());
    if (offered && service->mimeData() == offered.data() && service->ownsClipboard()) return {};
  } catch (...) { if (!attempted) return {ActionState::Failed, "annotation-clipboard-failed", std::nullopt}; }
  ActionOutcome unknown{ActionState::Failed, "annotation-clipboard-delivery-unknown", std::nullopt}; unknown.outcomeUnknown = true; return unknown;
}
}
