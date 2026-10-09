// EDITOR-SESSION-001 source loading (ES-001, ES-002, ES-016, ES-029) and the
// raster/sidecar save (ES-009, ES-011, ES-012) over Qt Gui. The decisions
// live in xerahs_core; this file only reads and writes files.
#pragma once

#include "image-editor/EditorSession.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <optional>

namespace xerahs::app {

struct LoadedSource {
  std::optional<xerahs::editor::AnnotationDocument> document;
  QImage image;                  // normalized once at load: upright, non-premultiplied ARGB32
  QString diagnostic;            // ES-029 identifier when `document` is empty
  QStringList warnings;          // user-facing: sidecar problems, source mismatch (ES-016)
  QString sidecarPath;           // sidecar the annotations came from, if any
};

// Never modifies `path`. Reopens annotations from the sidecar when one exists.
LoadedSource loadSource(const QString &path);

struct SaveOutcome {
  bool rasterSaved = false;
  bool sidecarSaved = false;
  QString rasterError;           // render or write failure detail
};

// Renders the authoritative export of `document` over `source`, replaces
// `rasterPath` atomically, then writes or removes the sidecar.
SaveOutcome saveEdit(const QString &rasterPath, const QImage &source, const xerahs::editor::AnnotationDocument &document);

}  // namespace xerahs::app
