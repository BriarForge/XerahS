#pragma once
#include "AnnotationDocument.h"
#include <QSet>
namespace xerahs::editor {
// The annotation part (and any asset-free payload) is bounded by this. An
// asset-bearing payload carries its PNG assets beside that part and is bounded
// by the larger whole-payload, per-asset and count limits below.
constexpr qsizetype kMaxAnnotationClipboardBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaxAnnotationAssetClipboardBytes = 16 * 1024 * 1024;
constexpr qsizetype kMaxClipboardAssetPngBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxClipboardAssets = 32;
constexpr std::size_t kMaxAnnotationTransferObjects = 1024;
constexpr std::size_t kMaxTransferDocumentObjects = 100000;
struct AnnotationTransferResult {
  std::vector<Annotation> annotations;
  EmbeddedImages assets;  // clipboard only: the PNG bytes of the referenced imageKeys
  QByteArray bytes;
  QString diagnostic;
  bool ok() const { return diagnostic.isEmpty() && !annotations.empty(); }
};
// Local candidate object transport. Only explicitly supported objects, no source
// pixels, paths or document metadata. An asset-bearing clipboard payload (version
// 2) additionally carries the embedded PNG of exactly the selected Image, Emoji
// and Cursor objects, bounded by the limits above and validated as untrusted
// input on both the offer and the read. Unselected assets stay private;
// asset-free payloads remain version 1. Local duplication keeps the document's
// assets without using the clipboard.
AnnotationTransferResult selectedAnnotationTransfer(const AnnotationDocument &, const QSet<QUuid> &, bool forClipboard);
AnnotationTransferResult parseAnnotationClipboard(const QByteArray &);
QString annotationTransferDiagnostic(const std::vector<Annotation> &);
}
