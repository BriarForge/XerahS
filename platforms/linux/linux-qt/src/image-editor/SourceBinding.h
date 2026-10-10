// ES-015..ES-017: bind a validated source only after an explicit mismatch choice.
#pragma once

#include "image-editor/AnnotationDocument.h"

namespace xerahs::editor {
enum class SourceChoice { Unspecified, CurrentRaster, EmbeddedSource };
struct SourceBindingResult {
  std::optional<AnnotationDocument> document;
  std::optional<QString> error;
  bool choiceRequired = false;
};

// Native codecs validate full image decoding; this model validates persisted
// structure and PNG header limits before selecting immutable source bytes.
SourceBindingResult bindSource(const AnnotationDocument &document, const QByteArray &currentRasterPng,
                               const QString &rasterPath, const QString &rasterHash,
                               SourceChoice choice = SourceChoice::Unspecified);
}  // namespace xerahs::editor
