#pragma once
#include "AnnotationDocument.h"
#include <QSet>
namespace xerahs::editor {
constexpr qsizetype kMaxAnnotationClipboardBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxAnnotationTransferObjects = 1024;
constexpr std::size_t kMaxTransferDocumentObjects = 100000;
struct AnnotationTransferResult {
  std::vector<Annotation> annotations;
  QByteArray bytes;
  QString diagnostic;
  bool ok() const { return diagnostic.isEmpty() && !annotations.empty(); }
};
// Local candidate object transport. Only explicitly supported objects, no source
// pixels, paths or document metadata. Clipboard offers and reads refuse selected
// asset-bearing objects because version 1 carries no pixels. Unselected assets
// stay private and do not block asset-free selections; local duplication keeps
// the document's assets without using this clipboard restriction.
AnnotationTransferResult selectedAnnotationTransfer(const AnnotationDocument &, const QSet<QUuid> &, bool forClipboard);
AnnotationTransferResult parseAnnotationClipboard(const QByteArray &);
QString annotationTransferDiagnostic(const std::vector<Annotation> &);
}
