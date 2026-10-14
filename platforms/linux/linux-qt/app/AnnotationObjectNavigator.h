#pragma once
#include <QListView>
#include <QPointer>
#include <functional>
namespace xerahs::app {
class EditorCanvas;
class AnnotationObjectModel;
// Native selection UI. Model rows own UUIDs and plain text, never document
// pointers or executable unknown payloads. Availability callbacks are pure.
class AnnotationObjectNavigator final : public QListView {
  Q_OBJECT
public:
  explicit AnnotationObjectNavigator(EditorCanvas &, QWidget *parent = nullptr,
      std::function<bool()> ready = {});
  void refresh();
  void focusObjects();
protected:
  void keyPressEvent(QKeyEvent *) override;
  bool event(QEvent *) override;
private:
  void publishSelection();
  QPointer<EditorCanvas> m_canvas;
  AnnotationObjectModel *m_objects;
  std::function<bool()> m_ready;
  bool m_refreshing = false;
};
}
