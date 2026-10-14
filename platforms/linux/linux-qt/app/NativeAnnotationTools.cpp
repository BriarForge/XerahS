#include "NativeAnnotationTools.h"
#include "EditorCanvas.h"
#include <QAction>
#include <QLabel>
#include <QSignalBlocker>
#include <array>

namespace xerahs::app {
QMap<QString, QAction *> annotationToolActions(EditorCanvas &canvas, QObject *owner, bool includeCrop) {
  using Tool = EditorCanvas::Tool;
  QMap<QString, QAction *> actions;
  for (const auto &entry : std::array<std::pair<QString, Tool>, 3>{{{"Select", Tool::Select}, {"Rectangle", Tool::Rectangle}, {"Crop", Tool::Crop}}}) {
    if (entry.second == Tool::Crop && !includeCrop) continue;
    auto *action = new QAction(entry.first, owner);
    const QString id = entry.first.toLower() + "-tool";
    action->setObjectName(entry.first.toLower() + "Tool");
    action->setCheckable(true); action->setAutoRepeat(false);
    action->setProperty("annotationToolId", entry.first);
    const auto update = [&canvas, action, tool = entry.second] {
      const QSignalBlocker blocker(action);
      action->setChecked(canvas.tool() == tool);
      action->setStatusTip(tool == Tool::Select ? "Select and edit existing objects; blank canvas clears selection."
          : tool == Tool::Rectangle ? "Draw rectangles on blank canvas. The tool stays active after each completed object."
          : "Drag the region to keep. The crop tool stays active after completion.");
      action->setToolTip(action->statusTip());
    };
    QObject::connect(&canvas, &EditorCanvas::toolChanged, action, update); update();
    QObject::connect(action, &QAction::toggled, &canvas, [&canvas, tool = entry.second](bool checked) {
      if (checked) canvas.setTool(tool);
      else if (canvas.tool() == tool) canvas.setTool(Tool::Rectangle);
    });
    actions.insert(id, action);
  }
  return actions;
}
QLabel *annotationToolInstructions(EditorCanvas &canvas, QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setObjectName("annotationToolInstructions"); label->setAccessibleName("Annotation tool instructions");
  label->setTextFormat(Qt::PlainText);
  // Compact instructions must not enlarge a restored editor beyond a small
  // screen. Full pointer/keyboard guidance remains accessible and in the tip.
  label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  label->setMinimumWidth(0);
  const auto update = [&canvas, label] {
    using Tool = EditorCanvas::Tool;
    label->setText(canvas.tool() == Tool::Select ? "Select: click objects; Control-click adds.\nBlank canvas clears selection; Escape cancels."
        : canvas.tool() == Tool::Rectangle ? "Rectangle stays active: drag or use Create Rectangle.\nOutside bounds are clipped; Escape cancels."
        : "Crop stays active: drag the region to keep.\nEscape cancels.");
    label->setAccessibleDescription(canvas.toolInstructions()); label->setToolTip(canvas.toolInstructions());
  };
  QObject::connect(&canvas, &EditorCanvas::toolChanged, label, update); update();
  return label;
}
QString annotationEditMessage(const QString &diagnostic) {
  if (diagnostic == "annotation-edit-stale") return "The document or selection changed during the edit. The rectangle operation was not applied.";
  if (diagnostic == "annotation-style-invalid") return "The rectangle style contains invalid values. The document was kept.";
  if (diagnostic == "annotation-geometry-invalid") return "The edit was rejected because the bounds would be empty, invalid or too large. The document was kept.";
  if (diagnostic == "annotation-selection-protected") return "Hidden or locked objects cannot be edited. The document was kept.";
  if (diagnostic == "annotation-type-unsupported") return "Unsupported annotation types are preserved but cannot be transformed. Select a supported rectangle.";
  return "The annotation edit could not be prepared. The document was kept.";
}
} // namespace xerahs::app
