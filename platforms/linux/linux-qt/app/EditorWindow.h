// EDITOR-SESSION-001 editor surface: canvas, rectangle tool, selection,
// undo/redo, save, and the dirty-close prompt (ES-003 to ES-008, ES-019).
#pragma once

#include "image-editor/EditorSession.h"
#include "image-editor/CanvasOperations.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <optional>

class QAction;
class QDockWidget;
class QListWidget;

namespace xerahs::app {

class EditorCanvas;
struct PreparedCanvasEdit;

class EditorWindow final : public QMainWindow {
  Q_OBJECT
public:
  ~EditorWindow() override;
  // Returns nullptr after telling the user why when the image cannot be opened.
  static EditorWindow *open(const QString &path, QWidget *parent = nullptr);
  static EditorWindow *createNew(QWidget *parent = nullptr);
  // Empty path invokes the native picker; recent-file actions pass an explicit path.
  bool openImage(const QString &path = {});

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  EditorWindow(const QString &path, QImage image, xerahs::editor::AnnotationDocument document,
               xerahs::editor::EditorSession::InitialPersistence persistence = xerahs::editor::EditorSession::InitialPersistence::Saved,
               QImage displaySource = {});
  void newImage();
  int confirmReplacement(const QString &replacement);
  void adoptSession(std::unique_ptr<xerahs::editor::EditorSession> session, const QString &path,
                    QImage source, const QImage &display);
  bool save(bool chooseFile);
  void refresh();
  void cropImage();
  void autoCropImage();
  void rotateImage();
  void clearImage();
  void compareImages();
  void resizeImage(bool canvasOnly);
  void applyCanvas(xerahs::editor::CanvasOperation operation);
  void commitCanvas(PreparedCanvasEdit prepared, quint64 state);

  QString m_path;
  QImage m_image;
  QByteArray m_sourceBytes;
  std::unique_ptr<xerahs::editor::EditorSession> m_session;
  EditorCanvas *m_canvas = nullptr;
  QAction *m_undo = nullptr;
  QAction *m_redo = nullptr;
  QAction *m_delete = nullptr;
  QAction *m_rotate = nullptr;
  QAction *m_clearAnnotations = nullptr;
  QDockWidget *m_unsupportedDock = nullptr;
  QListWidget *m_unsupportedObjects = nullptr;
  std::optional<quint64> m_placeholderState;
  bool m_canvasOperationPending = false;
};

}  // namespace xerahs::app
