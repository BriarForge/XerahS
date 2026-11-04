#pragma once
#include "EditorSource.h"
#include "CanvasCropDialog.h"
#include "EditorHistory.h"
#include "EditorCopy.h"
#include "OwnedWork.h"
#include "image-editor/EditorPreferences.h"
#include <QDialog>
#include <QPointer>
#include <QTimer>
class QLabel;
class QCheckBox;
class QProgressDialog;
namespace xerahs::app {
class EditorCanvas;
class AnnotationObjectNavigator;
class CaptureAnnotationDialog final : public QDialog {
  Q_OBJECT
public:
  CaptureAnnotationDialog(const LoadedSource &source, QWidget *parent = nullptr, const xerahs::editor::RectangleStyle &style = {});
  ~CaptureAnnotationDialog() override;
  std::optional<xerahs::editor::AnnotationDocument> document() const { return m_accepted; }
  bool sidecarIntent() const { return m_sidecarIntent; }
  std::optional<xerahs::editor::CropRegion> sourceRegion() const { return m_session.retainedSourceRegion(); }
  void setHistoryHooks(EditorHistoryHooks hooks) { m_historyHooks = std::move(hooks); }
  void setCropHooks(CanvasCropHooks hooks) { m_cropHooks = std::move(hooks); }
  // GUI-thread, value-owned completion, exactly once. Publication is GUI-only;
  // reentrant completion/close is fenced. Destruction abandons owned rendering.
  void enableTaskMode(const xerahs::editor::EditorConveniencePreferences &preferences, EditorCopyPorts copy = {});
  QImage renderedResult() const { return m_taskImage; }
  void abandonTask(); // explicit upstream cancellation, no automatic outputs
signals:
  // GUI-thread completion, at most once. The host must connect before presenting
  // task mode and copy document()/renderedResult() by value during Continue.
  // Cancel carries no result. Upstream cancellation abandons delivery; auto-copy
  // exceptions/late cancellation are distinct diagnostics on a ready Continue.
  // Reentrant Continue/Cancel cannot deliver twice. Host destruction disconnects;
  // a missing receiver is explained without output. Throwing observers are
  // contained and never retried; delivery is uncertain after an exception.
  void taskFinished(const QString &outcome, const QString &diagnostic);
public slots:
  void accept() override;
  void reject() override;
  void done(int result) override;
protected:
  void closeEvent(QCloseEvent *event) override;
private:
  void continueTask();
  void cancelTask();
  enum class TaskState { Inactive, Editing, Rendering, Completing, Completed };
  TaskState m_taskState = TaskState::Inactive;
  xerahs::editor::EditorConveniencePreferences m_taskPreferences;
  EditorCopyPorts m_taskCopy;
  OwnedWork m_taskWork;
  QImage m_taskImage;
  quint64 m_taskGeneration = 0;
  bool m_taskConfirming = false;
  bool m_taskCancelledDuringCopy = false;
  bool editingReady() const;
  void cropImage(xerahs::editor::CropRegion);
  void restoreHistory(bool redo);
  struct HistoryWork;
  void pollHistory();
  void cancelHistory(const std::shared_ptr<HistoryWork> &work);
  void closeHistoryDialog(bool defer);
  void finishOperation();
  std::shared_ptr<HistoryWork> m_historyWork;
  QPointer<QProgressDialog> m_historyDialog;
  QPointer<CanvasCropDialog> m_cropDialog;
  CanvasCropHooks m_cropHooks;
  EditorHistoryHooks m_historyHooks;
  QTimer m_historyPoll;
  bool m_historyTransition = false;
  bool m_operationPending = false;
  bool m_closing = false;
  QImage m_image;
  xerahs::editor::EditorSession m_session;
  EditorCanvas *m_canvas = nullptr;
  AnnotationObjectNavigator *m_annotationObjects = nullptr;
  QLabel *m_status = nullptr;
  std::optional<xerahs::editor::AnnotationDocument> m_accepted;
  QCheckBox *m_saveSidecar = nullptr;
  bool m_sidecarIntent = false;
};
} // namespace xerahs::app
