#include "NativeAnnotationTools.h"
#include "EditorCanvas.h"
#include <QAction>
#include <QLabel>
#include <QSignalBlocker>
#include <QPointer>
#include <array>

namespace xerahs::app {
QAction *annotationDeleteAction(EditorCanvas &canvas, QObject *owner, std::function<void()> dispatch, std::function<bool()> ready) {
  auto *action = new QAction("Delete", owner); action->setObjectName("deleteSelection"); action->setAutoRepeat(false);
  action->setStatusTip("Delete selected annotations in one undo step. Hidden or locked objects protect the whole selection; source pixels, assets and creation defaults stay unchanged.");
  action->setToolTip(action->statusTip()); const QPointer<EditorCanvas> guarded(&canvas);
  const auto update = [guarded, action, ready] {
    action->setEnabled(guarded && guarded->isEnabled() && !guarded->interactionActive() && guarded->canDeleteSelection() && (!ready || ready()));
  };
  QObject::connect(&canvas, &EditorCanvas::changed, action, update);
  QObject::connect(&canvas, &EditorCanvas::toolChanged, action, update);
  QObject::connect(&canvas, &EditorCanvas::viewportChanged, action, update); update();
  QObject::connect(action, &QAction::triggered, &canvas, [guarded, dispatch, ready] {
    if (!guarded || !guarded->isEnabled() || guarded->interactionActive() || (ready && !ready())) return;
    if (dispatch) dispatch(); else guarded->deleteSelection();
  });
  return action;
}
QMap<QString, QAction *> annotationOrderActions(EditorCanvas &canvas, QObject *owner,
    std::function<void(xerahs::editor::EditorSession::Order)> dispatch, std::function<bool()> ready) {
  using Order = xerahs::editor::EditorSession::Order;
  struct Command { const char *id, *name, *label, *key; Order order; };
  const QPointer<EditorCanvas> guarded(&canvas); QMap<QString, QAction *> actions;
  for (const auto &entry : std::array<Command, 4>{{
      {"bring-forward", "bringForward", "Bring Forward", "Page Up", Order::Forward},
      {"send-backward", "sendBackward", "Send Backward", "Page Down", Order::Backward},
      {"bring-to-front", "bringToFront", "Bring to Front", "Home", Order::Front},
      {"send-to-back", "sendToBack", "Send to Back", "End", Order::Back}}}) {
    auto *action = new QAction(QString::fromLatin1(entry.label), owner);
    action->setObjectName(QString::fromLatin1(entry.name)); action->setAutoRepeat(false);
    action->setStatusTip(QString("%1 on the canvas. Reorder the selected group in one undo step, keeping its relative order. Creation defaults stay unchanged.").arg(QString::fromLatin1(entry.key)));
    action->setToolTip(action->statusTip());
    // Keep default Home/End/Page navigation local to the canvas, rather than
    // installing window shortcuts that steal native list or text navigation.
    const auto update = [guarded, action, ready] {
      action->setEnabled(guarded && guarded->isEnabled() && !guarded->interactionActive() && guarded->canOrderSelection() && (!ready || ready()));
    };
    QObject::connect(&canvas, &EditorCanvas::changed, action, update);
    QObject::connect(&canvas, &EditorCanvas::toolChanged, action, update);
    QObject::connect(&canvas, &EditorCanvas::viewportChanged, action, update); update();
    QObject::connect(action, &QAction::triggered, &canvas, [guarded, dispatch, ready, order = entry.order] {
      if (!guarded || !guarded->isEnabled() || guarded->interactionActive() || (ready && !ready())) return;
      if (dispatch) dispatch(order); else guarded->reorderSelection(order);
    });
    actions.insert(QString::fromLatin1(entry.id), action);
  }
  return actions;
}
QMap<QString, QAction *> annotationToolActions(EditorCanvas &canvas, QObject *owner, bool includeCrop) {
  using Tool = EditorCanvas::Tool;
  QMap<QString, QAction *> actions;
  for (const auto &entry : std::array<std::pair<QString, Tool>, 19>{{{"Select", Tool::Select}, {"Rectangle", Tool::Rectangle},
           {"Ellipse", Tool::Ellipse}, {"Line", Tool::Line}, {"Arrow", Tool::Arrow}, {"Freehand", Tool::Freehand},
           {"Text", Tool::Text}, {"Step", Tool::Step}, {"Highlight", Tool::Highlight}, {"Blur", Tool::Blur},
           {"Pixelate", Tool::Pixelate}, {"SpeechBalloon", Tool::SpeechBalloon}, {"Magnify", Tool::Magnify},
           {"Spotlight", Tool::Spotlight}, {"SmartEraser", Tool::SmartEraser}, {"Image", Tool::Image}, {"Emoji", Tool::Emoji},
           {"Cursor", Tool::Cursor}, {"Crop", Tool::Crop}}}) {
    if (entry.second == Tool::Crop && !includeCrop) continue;
    const QString label = entry.first == u"SpeechBalloon" ? QStringLiteral("Speech Balloon")
        : entry.first == u"SmartEraser" ? QStringLiteral("Smart Eraser") : entry.first;
    auto *action = new QAction(label, owner);
    const QString id = entry.first.toLower() + "-tool";
    action->setObjectName(entry.first.toLower() + "Tool");
    action->setCheckable(true); action->setAutoRepeat(false);
    action->setProperty("annotationToolId", entry.first);
    const auto update = [&canvas, action, tool = entry.second] {
      const QSignalBlocker blocker(action);
      action->setChecked(canvas.tool() == tool);
      action->setStatusTip(tool == Tool::Select ? "Select and edit existing objects; blank canvas clears selection."
          : tool == Tool::Crop ? "Drag the region to keep. The crop tool stays active after completion."
          : "Draw " + EditorCanvas::shapeType(tool) + " objects on blank canvas. The tool stays active after each completed object.");
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
        : canvas.tool() == Tool::Crop ? "Crop stays active: drag the region to keep.\nEscape cancels."
        : canvas.toolInstructions().section(QLatin1Char('.'), 0, 0) + ".\nEscape cancels.");
    label->setAccessibleDescription(canvas.toolInstructions()); label->setToolTip(canvas.toolInstructions());
  };
  QObject::connect(&canvas, &EditorCanvas::toolChanged, label, update); update();
  return label;
}
QString annotationEditMessage(const QString &diagnostic) {
  if (diagnostic == "annotation-order-invalid") return "The annotation ordering command is invalid. The document was kept.";
  if (diagnostic == "annotation-edit-stale") return "The document, selection or creation defaults changed during the edit. The annotation operation was not applied.";
  if (diagnostic == "annotation-style-invalid") return "The rectangle style contains invalid values. The document was kept.";
  if (diagnostic == "annotation-geometry-invalid") return "The edit was rejected because the bounds would be empty, invalid or too large. The document was kept.";
  if (diagnostic == "annotation-selection-protected") return "Hidden or locked objects cannot be edited. The document was kept.";
  if (diagnostic == "annotation-type-unsupported") return "Unsupported annotation types are preserved but cannot be transformed. Select a supported rectangle.";
  return "The annotation edit could not be prepared. The document was kept.";
}
} // namespace xerahs::app
