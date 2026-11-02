#pragma once
// EO-010 Set as Wallpaper (Linux realization). One owned source+annotation
// snapshot is rendered as sRGB PNG with alpha, the user chooses a portal target
// and confirms, the PNG is kept at a disclosed owner-private path under the
// application data directory, and org.freedesktop.portal.Wallpaper
// SetWallpaperFile applies it. The portal has no per-display selection: the
// desktop applies the image to every display with its own layout.
#include "PortalWallpaper.h"
#include "actions/PostCapturePipeline.h"
#include "image-editor/AnnotationDocument.h"
#include <QImage>
#include <functional>

class QWidget;

namespace xerahs::app {
struct EditorWallpaperSnapshot {
  QImage image;       // Owned sRGB source and visible supported annotations, with alpha.
  QByteArray png;     // The exact bytes that will be written and handed to the portal.
  QString path;       // Where the PNG will be kept if the user confirms; nothing exists there yet.
  QStringList disclosures;
};
struct EditorWallpaperChoice {
  // Cancelled (the default) means no file is written and no portal is called.
  xerahs::actions::ActionOutcome action{xerahs::actions::ActionState::Cancelled, {}, {}};
  PortalWallpaper::Target target = PortalWallpaper::Target::Background;
  bool showPreview = true;
};
// Native confirmation naming the target, the retained file and the all-displays behaviour.
EditorWallpaperChoice chooseEditorWallpaper(const EditorWallpaperSnapshot &, QWidget *);
// Default directory: <XDG data>/com.xerahs.app/linux-qt/wallpapers (empty when unknown).
QString defaultWallpaperDirectory();

struct EditorWallpaperPorts {
  // Worker-only. Captures must own their data and outlive an abandoned render.
  std::function<void()> beforeRender;
  // GUI-only, no external effect. Borrows the snapshot and parent for this call.
  // An empty callback disables Set as Wallpaper. An exception is a failed
  // confirmation. Owner deletion/reentrant actions are allowed; the editor stays busy.
  std::function<EditorWallpaperChoice(const EditorWallpaperSnapshot &, QWidget *)> confirm =
      [](const EditorWallpaperSnapshot &snapshot, QWidget *parent) { return chooseEditorWallpaper(snapshot, parent); };
  // Empty selects defaultWallpaperDirectory().
  QString directory;
  // Session bus transport settings (service name, method-reply timeout).
  PortalWallpaperConfig portal;
};
struct EditorWallpaperOutcome {
  // Succeeded: the desktop accepted the image. Cancelled: the user declined, or
  // the desktop reported cancel; Failed carries a stable diagnostic code.
  xerahs::actions::ActionOutcome action;
  QString filePath;          // the retained PNG; empty when no file remains
  bool fileRetained = false; // true only when filePath exists and the desktop may refer to it
  QString target;            // the portal "set-on" value that was requested; empty before confirmation
  bool portalCalled = false;
};
// Freezes value-owned source/document data before an owned preparation worker.
// Never mutates the document. Cancellation or owner destruction before the
// portal call writes no wallpaper request; destruction during it closes the request.
EditorWallpaperOutcome runEditorWallpaper(const QImage &, const xerahs::editor::AnnotationDocument &,
                                          QWidget *, const EditorWallpaperPorts & = {});
} // namespace xerahs::app
