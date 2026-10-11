// ESET-005: display recovery uses Qt logical pixels, independent of native I/O.
#pragma once
#include <QList>
#include <QMarginsF>
#include <QRectF>
#include <QSizeF>
#include <optional>

namespace xerahs::editor {
struct EditorWindowPlacement {
  QRectF clientBounds;
  QRectF frameBounds;
  int displayIndex = 0;
};
// Preserve a visible position on the most-overlapped usable display. Otherwise
// centre on the preferred display (or first usable display). Decorations and
// minimum size are constrained by the available work area, including tiny areas.
// No usable display yields no placement, allowing the native host to wait.
std::optional<EditorWindowPlacement> recoverEditorWindowPlacement(
    QRectF requestedClient, const QList<QRectF> &availableDisplays, int preferredDisplay = 0,
    QMarginsF frameMargins = {}, QSizeF minimumClientSize = {400, 300});
}  // namespace xerahs::editor
