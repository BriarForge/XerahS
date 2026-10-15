// EDITOR-SESSION-001 editor surface: canvas, rectangle tool, selection,
// undo/redo, save, and the dirty-close prompt (ES-003 to ES-008, ES-019).
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/CanvasOperations.h"
#include "EditorSave.h"
#include "EditorCopy.h"
#include "EditorPrint.h"
#include "AnnotationClipboard.h"
#include "CanvasCropDialog.h"
#include "EditorHistory.h"
#include "EditorCanvasWorker.h"
#include "ImageComparisonDialog.h"
#include "NewImageDialog.h"
#include "EditorOpen.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <optional>

class QAction;
class QDockWidget;

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
  void setNewImageHooks(NewImageHooks hooks) { m_newImageHooks = std::move(hooks); }
  void setOpenHooks(EditorOpenHooks hooks) { m_openHooks = std::move(hooks); }
  void setCanvasHooks(EditorCanvasHooks hooks) { m_canvasHooks = std::move(hooks); }
  void setComparisonHooks(ComparisonHooks hooks) { m_comparisonHooks = std::move(hooks); }
  void setHistoryHooks(EditorHistoryHooks hooks) { m_historyHooks = std::move(hooks); }
  void setCropHooks(CanvasCropHooks hooks) { m_cropHooks = std::move(hooks); }
  void setSaveHooks(EditorSaveHooks hooks) { m_saveHooks = std::move(hooks); }
  void setCopyPorts(EditorCopyPorts ports);
  void setPrintPorts(EditorPrintPorts ports);
  void setAnnotationClipboardPorts(AnnotationClipboardPorts ports);
signals:
  // GUI-thread acknowledgement after checkpoint/partial receipt recording;
  // emitted while Save is busy. Reentrant Save is ignored; owner deletion is safe.
  void saveFinished(bool rasterSaved, bool sidecarSaved, bool cancelled);
  // GUI acknowledgement while outputs are busy; unknown delivery is explicit.
  void copyFinished(const QString &state, const QString &diagnostic, bool cancellationArrivedDuring);
  // GUI acknowledgement while outputs are busy; a submitted job is distinct
  // from physical delivery. Owner deletion/reentrant calls are guarded.
  // Throwing completion observers are contained without repeating the job.
  void printFinished(const QString &state, const QString &diagnostic, bool cancellationArrivedDuring);
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
                    QImage source, const QImage &display);
  bool save(bool chooseFile);
  bool retrySave();
  bool performSave(const QString &target, const EditorSaveReceiptPtr &repair = {});
  bool withOutputBusy(const std::function<bool()> &operation);
  bool copyImage(const EditorCopyPayloadPtr &retry = {});
  bool retryCopy();
  bool printImage(const EditorPrintSnapshotPtr &retry = {});
  bool retryPrint();
  bool transferAnnotations(const QString &command);
  bool rectangleDialog(bool creation);
  bool rotateSelected();
  bool editColor(xerahs::editor::EditorSession::ColorTarget);
  bool changeStrokeWidth(bool increase);
  bool reorderSelected(xerahs::editor::EditorSession::Order);
  bool deleteSelected();
  void recordRecentImage(const QString &absolutePath);
  void applyCreationDefaults();
  void persistDrawingDefaults(quint64 expectedRevision);
  void refresh();
  void restoreHistory(bool redo);
  void cropImage();
  void cropImage(xerahs::editor::CropRegion region);
  void autoCropImage();
  void rotateImage();
  void clearImage();
  void compareImages();
  void resizeImage(bool canvasOnly);
  void applyCanvas(xerahs::editor::CanvasOperation operation);
  void editCanvas(const std::function<std::optional<xerahs::editor::CanvasOperation>(const QImage &, QWidget *)> &choose);
  bool commitCanvas(PreparedCanvasEdit prepared, quint64 state, const QSet<QUuid> &selection, quint64 generation);

  QString m_path;
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
  QAction *m_createRectangle = nullptr;
  QAction *m_clearAnnotations = nullptr;
  QDockWidget *m_annotationsDock = nullptr;
  AnnotationObjectNavigator *m_annotationObjects = nullptr;
  bool m_canvasOperationPending = false;
  bool m_closePending = false;
  bool m_outputPending = false;
  QAction *m_retrySave = nullptr;
  NewImageHooks m_newImageHooks;
  EditorOpenHooks m_openHooks;
  EditorCanvasHooks m_canvasHooks;
  ComparisonHooks m_comparisonHooks;
  EditorHistoryHooks m_historyHooks;
  CanvasCropHooks m_cropHooks;
  EditorSaveHooks m_saveHooks;
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
  QMap<QString, QAction *> m_annotationActions;
  AnnotationClipboardPorts m_annotationClipboard;
  QString m_annotationMessage;
  std::optional<quint64> m_annotationMessageState;
  QByteArray m_lastAnnotationPaste;
  double m_annotationPasteOffset = 0;
};

}  // namespace xerahs::app
