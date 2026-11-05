#include "NativeAnnotationTools.h"
#include "EditorCanvas.h"
#include <QCoreApplication>
#include <QAction>
#include <QLabel>
#include <QSignalBlocker>
#include <QPointer>
#include <array>

namespace xerahs::app {
QAction *annotationDeleteAction(EditorCanvas &canvas, QObject *owner, std::function<void()> dispatch, std::function<bool()> ready) {
  auto *action = new QAction(QCoreApplication::translate("NativeAnnotationTools", "Delete"), owner); action->setObjectName("deleteSelection"); action->setAutoRepeat(false);
  action->setStatusTip(QCoreApplication::translate("NativeAnnotationTools", "Delete selected annotations in one undo step. Hidden or locked objects protect the whole selection; source pixels, assets and creation defaults stay unchanged."));
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
      {"bring-forward", "bringForward", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Bring Forward"), QT_TRANSLATE_NOOP("NativeAnnotationTools", "Page Up"), Order::Forward},
      {"send-backward", "sendBackward", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Send Backward"), QT_TRANSLATE_NOOP("NativeAnnotationTools", "Page Down"), Order::Backward},
      {"bring-to-front", "bringToFront", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Bring to Front"), QT_TRANSLATE_NOOP("NativeAnnotationTools", "Home"), Order::Front},
      {"send-to-back", "sendToBack", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Send to Back"), QT_TRANSLATE_NOOP("NativeAnnotationTools", "End"), Order::Back}}}) {
    auto *action = new QAction(QCoreApplication::translate("NativeAnnotationTools", entry.label), owner);
    action->setObjectName(QString::fromLatin1(entry.name)); action->setAutoRepeat(false);
    action->setStatusTip(QCoreApplication::translate("NativeAnnotationTools", "%1 on the canvas. Reorder the selected group in one undo step, keeping its relative order. Creation defaults stay unchanged.").arg(QCoreApplication::translate("NativeAnnotationTools", entry.key)));
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
  struct ToolLabel { const char *id, *source; Tool tool; };
  for (const auto &entry : std::array<ToolLabel, 20>{{
      {"Select", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Select"), Tool::Select},
      {"Rectangle", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Rectangle"), Tool::Rectangle},
      {"Ellipse", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Ellipse"), Tool::Ellipse},
      {"Line", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Line"), Tool::Line},
      {"Arrow", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Arrow"), Tool::Arrow},
      {"Freehand", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Freehand"), Tool::Freehand},
      {"Text", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Text"), Tool::Text},
      {"Step", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Step"), Tool::Step},
      {"Highlight", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Highlight"), Tool::Highlight},
      {"Blur", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Blur"), Tool::Blur},
      {"Pixelate", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Pixelate"), Tool::Pixelate},
      {"SpeechBalloon", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Speech Balloon"), Tool::SpeechBalloon},
      {"Magnify", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Magnify"), Tool::Magnify},
      {"Spotlight", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Spotlight"), Tool::Spotlight},
      {"SmartEraser", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Smart Eraser"), Tool::SmartEraser},
      {"Image", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Image"), Tool::Image},
      {"Emoji", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Emoji"), Tool::Emoji},
      {"Cursor", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Cursor"), Tool::Cursor},
      {"CutOut", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Cut Out"), Tool::CutOut},
      {"Crop", QT_TRANSLATE_NOOP("NativeAnnotationTools", "Crop"), Tool::Crop}}}) {
    if ((entry.tool == Tool::Crop || entry.tool == Tool::CutOut) && !includeCrop) continue;
    const QString stableId = QString::fromLatin1(entry.id);
    auto *action = new QAction(QCoreApplication::translate("NativeAnnotationTools", entry.source), owner);
    const QString id = stableId.toLower() + "-tool";
    action->setObjectName(stableId.toLower() + "Tool");
    action->setCheckable(true); action->setAutoRepeat(false);
    action->setProperty("annotationToolId", stableId);
    const auto update = [&canvas, action, tool = entry.tool] {
      const QSignalBlocker blocker(action);
      action->setChecked(canvas.tool() == tool);
      action->setStatusTip(tool == Tool::Select ? QCoreApplication::translate("NativeAnnotationTools", "Select and edit existing objects; blank canvas clears selection.")
          : tool == Tool::Crop ? QCoreApplication::translate("NativeAnnotationTools", "Drag the region to keep. The crop tool stays active after completion.")
          : tool == Tool::CutOut ? QCoreApplication::translate("NativeAnnotationTools", "Drag across a strip to remove it and close the gap. The tool stays active after completion.")
          : QCoreApplication::translate("NativeAnnotationTools", "Draw %1 objects on blank canvas. The tool stays active after each completed object.").arg(action->text()));
      action->setStatusTip(action->statusTip() + " " + EditorCanvas::toolOptions(tool));
      action->setToolTip(action->statusTip());
    };
    QObject::connect(&canvas, &EditorCanvas::toolChanged, action, update); update();
    QObject::connect(action, &QAction::toggled, &canvas, [&canvas, tool = entry.tool](bool checked) {
      if (checked) canvas.setTool(tool);
      else if (canvas.tool() == tool) canvas.setTool(Tool::Rectangle);
    });
    actions.insert(id, action);
  }
  return actions;
}
QLabel *annotationToolInstructions(EditorCanvas &canvas, QWidget *parent) {
  auto *label = new QLabel(parent);
  label->setObjectName("annotationToolInstructions"); label->setAccessibleName(QCoreApplication::translate("NativeAnnotationTools", "Annotation tool instructions"));
  label->setTextFormat(Qt::PlainText);
  // Compact instructions must not enlarge a restored editor beyond a small
  // screen. Full pointer/keyboard guidance remains accessible and in the tip.
  label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  label->setMinimumWidth(0);
  const auto update = [&canvas, label] {
    using Tool = EditorCanvas::Tool;
    label->setText(canvas.tool() == Tool::Select ? QCoreApplication::translate("NativeAnnotationTools", "Select: click objects; Control-click adds.\nBlank canvas clears selection; Escape cancels.")
        : canvas.tool() == Tool::Rectangle ? QCoreApplication::translate("NativeAnnotationTools", "Rectangle stays active: drag or use Create Rectangle.\nOutside bounds are clipped; Escape cancels.")
        : canvas.tool() == Tool::Crop ? QCoreApplication::translate("NativeAnnotationTools", "Crop stays active: drag the region to keep.\nEscape cancels.")
        : canvas.toolInstructions().section(QLatin1Char('.'), 0, 0) + QCoreApplication::translate("NativeAnnotationTools", ".\nEscape cancels."));
    label->setAccessibleDescription(canvas.toolInstructions()); label->setToolTip(canvas.toolInstructions());
  };
  QObject::connect(&canvas, &EditorCanvas::toolChanged, label, update); update();
  return label;
}
QString annotationEditMessage(const QString &diagnostic) {
  if (diagnostic == "annotation-order-invalid") return QCoreApplication::translate("NativeAnnotationTools", "The annotation ordering command is invalid. The document was kept.");
  if (diagnostic == "annotation-edit-stale") return QCoreApplication::translate("NativeAnnotationTools", "The document, selection or creation defaults changed during the edit. The annotation operation was not applied.");
  if (diagnostic == "annotation-style-invalid") return QCoreApplication::translate("NativeAnnotationTools", "The rectangle style contains invalid values. The document was kept.");
  if (diagnostic == "annotation-geometry-invalid") return QCoreApplication::translate("NativeAnnotationTools", "The edit was rejected because the bounds would be empty, invalid or too large. The document was kept.");
  if (diagnostic == "annotation-selection-protected") return QCoreApplication::translate("NativeAnnotationTools", "Hidden or locked objects cannot be edited. The document was kept.");
  if (diagnostic == "annotation-asset-invalid") return QCoreApplication::translate("NativeAnnotationTools", "The image is not a valid, safely sized PNG, so it was not inserted. The document was kept.");
  if (diagnostic == "annotation-type-unsupported") return QCoreApplication::translate("NativeAnnotationTools", "Unsupported annotation types are preserved but cannot be transformed. Select a supported rectangle.");
  return QCoreApplication::translate("NativeAnnotationTools", "The annotation edit could not be prepared. The document was kept.");
}
} // namespace xerahs::app
