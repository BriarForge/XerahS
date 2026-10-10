#include "image-editor/SourceBinding.h"

#include <new>

namespace xerahs::editor {
SourceBindingResult bindSource(const AnnotationDocument &document, const QByteArray &currentRasterPng,
                               const QString &rasterPath, const QString &rasterHash, SourceChoice choice) {
  try {
    const auto valid = parseDocument(serializeDocument(document));
    if (valid.error) return {std::nullopt, valid.error, false};
    if (choice != SourceChoice::Unspecified && choice != SourceChoice::CurrentRaster && choice != SourceChoice::EmbeddedSource)
      return {std::nullopt, diagnostic::documentInvalid, false};
    const bool mismatch = !document.imageHash.isEmpty() && document.imageHash != rasterHash;
    if (mismatch && choice == SourceChoice::Unspecified) return {std::nullopt, std::nullopt, true};
    AnnotationDocument bound = document;
    if (choice == SourceChoice::CurrentRaster) bound.sourceImagePng = currentRasterPng;
    bound.imagePath = rasterPath;
    bound.imageHash = rasterHash;
    // Validate the selected header before reading it; never allocate pixels here.
    const auto selected = parseDocument(serializeDocument(bound));
    if (selected.error) return {std::nullopt, selected.error, false};
    const auto dimension = [&](int offset) {
      const auto *p = reinterpret_cast<const unsigned char *>(bound.sourceImagePng.constData() + offset);
      return (qint64(p[0]) << 24) | (qint64(p[1]) << 16) | (qint64(p[2]) << 8) | qint64(p[3]);
    };
    if (choice != SourceChoice::CurrentRaster &&
        (dimension(16) != document.canvasWidth || dimension(20) != document.canvasHeight))
      return {std::nullopt, diagnostic::documentInvalid, false};
    bound.canvasWidth = dimension(16); bound.canvasHeight = dimension(20);
    return {std::move(bound), std::nullopt, false};
  } catch (const std::bad_alloc &) { return {std::nullopt, diagnostic::documentTooLarge, false}; }
}
}  // namespace xerahs::editor
