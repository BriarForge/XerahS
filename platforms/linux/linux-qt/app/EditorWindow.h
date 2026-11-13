// EDITOR-SESSION-001 editor surface: canvas, rectangle tool, selection,
// undo/redo, save, and the dirty-close prompt (ES-003 to ES-008, ES-019).
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/EditorRecovery.h"
#include "image-editor/CanvasOperations.h"
#include "EditorSave.h"
#include "EditorCopy.h"
#include "EditorPrint.h"
#include "EditorPin.h"
#include "EditorUpload.h"
#include "EditorWallpaper.h"
#include "AnnotationClipboard.h"
#include "CanvasCropDialog.h"
#include "EditorHistory.h"
#include "EditorCanvasWorker.h"
#include "ImageComparisonDialog.h"
#include "NewImageDialog.h"
#include "EditorOpen.h"
#include "UrlOpenDialog.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <optional>

class QAction;
class QDockWidget;
class QLabel;
class QTimer;

namespace xerahs::app {

class EditorCanvas;
class AnnotationObjectNavigator;
class NativeEditorWindowState;
class NativeEditorToolbar;
struct PreparedCanvasEdit;

class EditorWindow final : public QMainWindow {
  Q_OBJECT
public:
  ~EditorWindow() override;
  // Returns nullptr after telling the user why when the image cannot be opened.
  static EditorWindow *open(const QString &path, QWidget *parent = nullptr);
  static EditorWindow *createNew(QWidget *parent = nullptr);
  static EditorWindow *fromCapturedImage(const QImage &image, QWidget *parent = nullptr);
  static EditorWindow *fromCapturedDocument(const xerahs::editor::AnnotationDocument &document, QWidget *parent = nullptr);
  // Empty path invokes the native picker; recent-file actions pass an explicit path.
  bool openImage(const QString &path = {});
  // EC-003: explicit dialog, HTTPS by default, bounded download held in memory only.
  // Ends exactly where openImage does: validated source, dirty Save/Discard/Cancel,
  // then an untitled session. Returns false when nothing was replaced.
  bool openImageFromUrl();
  void setUrlOpenOptions(UrlOpenOptions options) { m_urlOpenOptions = std::move(options); }
  void setNewImageHooks(NewImageHooks hooks) { m_newImageHooks = std::move(hooks); }
  void setOpenHooks(EditorOpenHooks hooks) { m_openHooks = std::move(hooks); }
  void setCanvasHooks(EditorCanvasHooks hooks) { m_canvasHooks = std::move(hooks); }
  void setComparisonHooks(ComparisonHooks hooks) { m_comparisonHooks = std::move(hooks); }
  void setHistoryHooks(EditorHistoryHooks hooks) { m_historyHooks = std::move(hooks); }
  void setCropHooks(CanvasCropHooks hooks) { m_cropHooks = std::move(hooks); }
  // GUI-thread provider configuration. In-flight outputs keep frozen copies;
  // replacing a provider affects later attempts. Captures must own any data
  // needed after editor destruction; phase-specific contracts live in each
  // port header and EDITOR-HOST-CALLBACKS.md.
  void setSaveHooks(EditorSaveHooks hooks) { m_saveHooks = std::move(hooks); }
  void setCopyPorts(EditorCopyPorts ports);
  void setPrintPorts(EditorPrintPorts ports);
  void setPinPorts(EditorPinPorts ports);
  void setUploadPorts(EditorUploadPorts ports);
  void setWallpaperPorts(EditorWallpaperPorts ports);
  void setAnnotationClipboardPorts(AnnotationClipboardPorts ports);
  // ES-018: restart notice for separate recovery documents. Each is disclosed
  // with its age and source and offered Restore / Discard / Decide later (the
  // default, which keeps it). Restoring opens an unsaved editor and never writes
  // the explicit save. Returns the number of editors restored.
  static int offerRecovery(QWidget *parent = nullptr);
  // Test seam: delay before a dirty change is written to its recovery document.
  static void setRecoveryDelay(int milliseconds);
  // Writes the recovery document now when the session is dirty; returns its diagnostic.
  QString writeRecoveryNow();
signals:
  // GUI-thread acknowledgement after checkpoint/partial receipt recording;
  // emitted while Save is busy. Reentrant Save is ignored; owner deletion is safe.
  // Emitted on the GUI thread, at most once for an output attempt. Cancelling
  // the chooser starts no output and emits nothing. Observers own copied bools;
  // exceptions cannot undo publication or authorize retry. Owner deletion is
  // allowed; the editor then returns without further access.
  void saveFinished(bool rasterSaved, bool sidecarSaved, bool cancelled);
  // GUI acknowledgement while outputs are busy; unknown delivery is explicit.
  void copyFinished(const QString &state, const QString &diagnostic, bool cancellationArrivedDuring);
  // GUI acknowledgement while outputs are busy; a submitted job is distinct
  // from physical delivery. Owner deletion/reentrant calls are guarded.
  // Throwing completion observers are contained without repeating the job.
  void printFinished(const QString &state, const QString &diagnostic, bool cancellationArrivedDuring);
  void pinFinished(const QString &state, const QString &diagnostic);
  void uploadFinished(const QString &state, const QString &diagnostic, const QString &url);
  // GUI acknowledgement while outputs are busy: state is succeeded/cancelled/failed, diagnostic a
  // stable code, path the retained wallpaper PNG (empty when none remains). Owner deletion is safe.
  void wallpaperFinished(const QString &state, const QString &diagnostic, const QString &path);
  void annotationTransferFinished(const QString &command, const QString &state, const QString &diagnostic, bool changed);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  EditorWindow(const QString &path, QImage image, xerahs::editor::AnnotationDocument document,
               xerahs::editor::EditorSession::InitialPersistence persistence = xerahs::editor::EditorSession::InitialPersistence::Saved,
               QImage displaySource = {});
  void newImage();
  int confirmReplacement(const QString &replacement);
  bool replacementCurrent(quint64 state, quint64 generation, const QSet<QUuid> &selection);
  void adoptSession(std::unique_ptr<xerahs::editor::EditorSession> session, const QString &path,
                    QImage source, const QImage &display, const QString &sourceLabel = {});
  bool save(bool chooseFile);
  bool retrySave();
  bool reportSaveFinished(bool rasterSaved, bool sidecarSaved, bool cancelled);
  // Caller owns the complete chooser/publication transaction via withOutputBusy.
  bool performSave(const QString &target, const EditorSaveReceiptPtr &repair = {}, const xerahs::editor::RasterSaveOptions &encoding = {});
  bool withOutputBusy(const std::function<bool()> &operation);
  bool configurePreferences(const std::function<bool()> &operation);
  bool copyImage(const EditorCopyPayloadPtr &retry = {});
  bool retryCopy();
  bool printImage(const EditorPrintSnapshotPtr &retry = {});
  bool retryPrint();
  bool pinImage();
  bool uploadImage();
  bool setWallpaper();
  bool transferAnnotations(const QString &command);
  bool rectangleDialog(bool creation);
  // EA-005/EA-014: guarded dialog for the border, corner and shadow creation defaults.
  bool shapeStyleDefaultsDialog();
  bool rotateSelected();
  bool editColor(xerahs::editor::EditorSession::ColorTarget);
  bool changeStrokeWidth(bool increase);
  bool reorderSelected(xerahs::editor::EditorSession::Order);
  bool deleteSelected();
  void recordRecentImage(const QString &absolutePath);
  void applyCreationDefaults();
  void rememberAnnotationTool();
  void persistDrawingDefaults(quint64 expectedRevision);
  void refresh();
  void updateStatusMessage();
  void scheduleRecovery();
  quint64 submitRecoveryWrite();
  void discardRecovery(bool wait = false);
  void updatePointerStatus();
  void restoreHistory(bool redo);
  void cropImage();
  void cropImage(xerahs::editor::CropRegion region);
  void autoCropImage();
  void rotateImage();
  void effectsImage();
  void clearImage();
  void compareImages();
  void resizeImage(bool canvasOnly);
  void applyCanvas(xerahs::editor::CanvasOperation operation);
  void editCanvas(const std::function<std::optional<xerahs::editor::CanvasOperation>(const QImage &, QWidget *)> &choose);
  bool commitCanvas(PreparedCanvasEdit prepared, quint64 state, const QSet<QUuid> &selection, quint64 generation);

  QString m_path;
  QTimer *m_recoveryTimer = nullptr;
  QString m_recoveryId;
  std::shared_ptr<xerahs::editor::EditorRecoveryStore> m_recoveryStore;
  quint64 m_recoveryGeneration = 0, m_recoveryJob = 0;
  std::optional<quint64> m_recoveryRequestedState;
  QString m_recoverySource;  // origin of a restored session while it has no path of its own
  QString m_recoveryDiagnostic;
  std::optional<quint64> m_recoveredState;
  QString m_preferenceDiagnostic;
  QString m_drawingPreferenceDiagnostic;
  QImage m_image;
  QByteArray m_sourceBytes;
  std::unique_ptr<xerahs::editor::EditorSession> m_session;
  EditorCanvas *m_canvas = nullptr;
  NativeEditorWindowState *m_windowState = nullptr;
  NativeEditorToolbar *m_toolbarController = nullptr;
  QMap<QString, QAction *> m_toolbarActions;
  QMap<QString, QAction *> m_orderActions;
  QAction *m_undo = nullptr;
  QAction *m_redo = nullptr;
  QAction *m_delete = nullptr;
  QAction *m_rotate = nullptr;
  QAction *m_rectangleProperties = nullptr;
  QAction *m_shapeStyleDefaults = nullptr;
  QAction *m_createRectangle = nullptr;
  QAction *m_clearAnnotations = nullptr;
  QDockWidget *m_annotationsDock = nullptr;
  AnnotationObjectNavigator *m_annotationObjects = nullptr;
  QLabel *m_pointerStatus = nullptr;
  std::optional<QPoint> m_pointerPixel;
  bool m_canvasOperationPending = false;
  bool m_closePending = false;
  bool m_outputPending = false;
  QAction *m_retrySave = nullptr;
  NewImageHooks m_newImageHooks;
  EditorOpenHooks m_openHooks;
  UrlOpenOptions m_urlOpenOptions;
  QString m_sourceLabel;  // remote host of an opened URL image, empty otherwise
  EditorCanvasHooks m_canvasHooks;
  ComparisonHooks m_comparisonHooks;
  EditorHistoryHooks m_historyHooks;
  CanvasCropHooks m_cropHooks;
  EditorSaveHooks m_saveHooks;
  xerahs::editor::RasterSaveOptions m_saveEncoding;
  EditorSaveReceiptPtr m_partialSave;
  std::optional<quint64> m_partialSaveState;
  QString m_saveMessage;
  std::optional<quint64> m_saveMessageState;
  QAction *m_copyAction = nullptr;
  QAction *m_retryCopy = nullptr;
  EditorCopyPorts m_copyPorts;
  EditorCopyPayloadPtr m_failedCopy;
  std::optional<quint64> m_failedCopyState;
  QString m_copyMessage;
  std::optional<quint64> m_copyMessageState;
  QAction *m_printAction = nullptr;
  QAction *m_retryPrint = nullptr;
  EditorPrintPorts m_printPorts;
  EditorPrintSnapshotPtr m_failedPrint;
  std::optional<quint64> m_failedPrintState;
  QString m_printMessage;
  std::optional<quint64> m_printMessageState;
  QAction *m_pinAction = nullptr;
  EditorPinPorts m_pinPorts;
  QString m_pinMessage;
  QAction *m_uploadAction = nullptr;
  EditorUploadPorts m_uploadPorts;
  QString m_uploadMessage;
  QAction *m_wallpaperAction = nullptr;
  EditorWallpaperPorts m_wallpaperPorts;
  QString m_wallpaperMessage;
  QMap<QString, QAction *> m_annotationActions;
  AnnotationClipboardPorts m_annotationClipboard;
  QString m_annotationMessage;
  QString m_importMessage;
  std::optional<quint64> m_annotationMessageState;
  QByteArray m_lastAnnotationPaste;
  double m_annotationPasteOffset = 0;
};

}  // namespace xerahs::app
