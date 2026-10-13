// EDITOR-SESSION-001 source loading (ES-001, ES-002, ES-016, ES-029) and the
// raster/sidecar save (ES-009, ES-011, ES-012) over Qt Gui. The decisions
// live in xerahs_core; this file only reads and writes files.
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/SourceBinding.h"
#include "image-editor/AnnotationRenderer.h"
#include "EditorSave.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <optional>

namespace xerahs::app {

struct PendingSourceChoice {
  xerahs::editor::AnnotationDocument document;
  QImage currentRaster, embeddedSource;
  QImage currentDisplay, embeddedDisplay;
  QByteArray currentRasterPng;
  QString rasterPath, rasterHash, sidecarPath;
};

struct LoadedSource {
  std::optional<xerahs::editor::AnnotationDocument> document;
  QImage image;                  // normalized once at load: upright, non-premultiplied ARGB32
  QImage displayImage;           // private sRGB copy, prepared before publishing a session
  QString diagnostic;            // ES-029 identifier when `document` is empty
  QStringList warnings;          // user-facing: sidecar problems, source mismatch (ES-016)
  QString sidecarPath;           // sidecar the annotations came from, if any
  std::optional<PendingSourceChoice> pendingChoice;  // no usable document until explicitly resolved
};

// Never modifies `path`. Reopens annotations from the sidecar when one exists.
LoadedSource loadSource(const QString &path, const xerahs::editor::RenderControl &control = {});
// Captured pixels become an owned, untitled source. No file/sidecar is read or
// written, and no durable path is inferred from an earlier pipeline save.
LoadedSource prepareCapturedSource(const QImage &image, const xerahs::editor::RenderControl &control = {});
// Reopens an owned in-memory document; no saved raster or sidecar path is read.
LoadedSource prepareCapturedDocument(const xerahs::editor::AnnotationDocument &document,
                                    const xerahs::editor::RenderControl &control = {});
struct RenderedEditorImage { QImage image; QString diagnostic; };
RenderedEditorImage renderEditorImage(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                                     const xerahs::editor::RenderControl &control = {});
// Uses the validated snapshots shown by the chooser, without rereading files.
LoadedSource resolveSourceChoice(const LoadedSource &pending, xerahs::editor::SourceChoice choice,
                                const xerahs::editor::RenderControl &control = {});

}  // namespace xerahs::app
