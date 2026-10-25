#pragma once
#include "image-editor/AnnotationDocument.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/RasterSaveOptions.h"
#include <QImage>
#include <functional>
#include <memory>
class QIODevice;
class QWidget;
namespace xerahs::app {
// Live native observations, never reconstructed from a path or persisted
// report. Directory identity retains the kernel's interpretation of user paths.
struct EditorFileIdentity {
  quint64 device = 0, inode = 0;
  qint64 length = 0, modifiedSeconds = 0, modifiedNanos = 0, changedSeconds = 0, changedNanos = 0;
  quint32 owner = 0, mode = 0;
  bool operator==(const EditorFileIdentity &other) const;
};
enum class EditorDestinationKind { Missing, Regular, Other };
struct EditorDestination {
  EditorDestinationKind kind = EditorDestinationKind::Other;
  std::optional<EditorFileIdentity> identity;
};
struct EditorSaveReceipt {
  QString path;
  quint64 directoryDevice = 0, directoryInode = 0;
  EditorFileIdentity raster;
  QString rasterHash;
  EditorDestination previousSidecar;
  xerahs::editor::AnnotationDocument document;
  xerahs::editor::RasterSaveOptions encoding;
};
using EditorSaveReceiptPtr = std::shared_ptr<const EditorSaveReceipt>;
enum class EditorSaveArtifact { Raster, Sidecar };
enum class EditorSavePhase { Rendering, Encoding, PublishingRaster, SavingAnnotations, Complete };
struct EditorSaveHooks {
  // Synchronous worker callbacks; arguments are borrowed for this call only.
  // Returning false/throwing fails before publication; never use GUI APIs here.
  // Captured state must outlive the worker, including after parent destruction
  // releases runEditorSave. Copying a callable does not extend reference captures.
  std::function<bool(const QImage &, QIODevice *)> encode;
  std::function<void(EditorSaveArtifact, const QString &)> beforePublish;
};
struct EditorSaveControl {
  std::function<bool()> cancelled;
  std::function<void(EditorSavePhase, int)> progress;
};
struct SaveOutcome {
  bool rasterSaved = false;
  bool sidecarSaved = false;
  QString rasterError;
  QString sidecarError;
  bool cancelled = false;
  bool rasterReused = false;
  std::optional<QString> sidecarPath;
  EditorSaveReceiptPtr receipt;
};
SaveOutcome saveEdit(const QString &path, const QImage &source, const xerahs::editor::AnnotationDocument &document,
                    const EditorSaveControl &control = {}, const EditorSaveHooks &hooks = {},
                    const xerahs::editor::RasterSaveOptions &encoding = {});
SaveOutcome retryEditorSidecar(const EditorSaveReceiptPtr &receipt, const EditorSaveControl &control = {}, const EditorSaveHooks &hooks = {});
// GUI modal progress bridge over an owned worker. `source` must own its pixels;
// Qt copy-on-write retains that image and a value-owned document for this job.
// Cancel waits for a safe terminal boundary; parent destruction cancels and releases the bridge without
// joining or giving the worker references to any GUI/stack/session object.
SaveOutcome runEditorSave(const QString &path, const QImage &source, const xerahs::editor::AnnotationDocument &document,
                         QWidget *parent, const EditorSaveHooks &hooks = {}, const EditorSaveReceiptPtr &repair = {},
                         const xerahs::editor::RasterSaveOptions &encoding = {});
} // namespace xerahs::app
