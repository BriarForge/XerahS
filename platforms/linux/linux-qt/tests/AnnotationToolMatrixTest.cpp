// EDITOR-ANNOTATIONS-001 tool-matrix evidence: every contracted tool is
// checked against the real native actions, canvas and session. Fixtures use
// temporary preferences and in-memory images only.
#include "EditorCanvas.h"
#include "EditorWindow.h"
#include "NativeAnnotationTools.h"
#include "ImageInsertDialog.h"
#include "NativeEditorPreferences.h"
#include "TextContentDialog.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/ShapePainter.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QBuffer>
#include <QPlainTextEdit>
#include <QTransform>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <limits>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
struct ContractTool { QString id, family, documentType; bool hasDocumentType; };

// The shared Product Contract list is the only source of the expected IDs.
QList<ContractTool> contractTools() {
  QFile file(QStringLiteral(XERAHS_ANNOTATION_TOOLS));
  if (!file.open(QIODevice::ReadOnly)) return {};
  const auto tools = QJsonDocument::fromJson(file.readAll()).object().value("tools").toArray();
  QList<ContractTool> result;
  for (const auto &value : tools) {
    const auto o = value.toObject();
    result.append({o.value("id").toString(), o.value("family").toString(), o.value("document_type").toString(), !o.value("document_type").isNull()});
  }
  return result;
}

AnnotationDocument document(qint64 width = 800, qint64 height = 600) {
  AnnotationDocument d;
  d.canvasWidth = width;
  d.canvasHeight = height;
  return d;
}
QImage image(int width = 800, int height = 600) {
  QImage result(width, height, QImage::Format_ARGB32);
  result.fill(Qt::white);
  return result;
}
QString normalized(QString text) { return text.remove(QLatin1Char(' ')).remove(QLatin1Char('&')).toLower(); }

QImage asset() {
  QImage result(6, 4, QImage::Format_ARGB32);
  result.fill(qRgba(10, 140, 60, 200));
  return result;
}
QAction *toolAction(const QMap<QString, QAction *> &actions, const QString &id) {
  for (auto *action : actions) if (action->property("annotationToolId").toString() == id) return action;
  return nullptr;
}
// One pointer drag in document pixels with the canvas's current tool; every
// dialog the tool opens is completed the way a user would, so the call returns
// with the object created (or the canvas idle if the tool creates none).
void drawObject(EditorCanvas &canvas, QPointF from, QPointF to) {
  using Tool = EditorCanvas::Tool;
  const Tool tool = canvas.tool();
  QTimer emojiPicker;
  if (tool == Tool::Emoji) {
    emojiPicker.setInterval(5);
    QObject::connect(&emojiPicker, &QTimer::timeout, &emojiPicker, [&emojiPicker] {
      if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) { emojiPicker.stop(); dialog->accept(); }
    });
    emojiPicker.start();
  }
  const QPoint a = canvas.viewState().toView(from).toPoint(), b = canvas.viewState().toView(to).toPoint();
  QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a);
  QTest::mouseMove(canvas.viewport(), (a + b) / 2);
  QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b);
  emojiPicker.stop();
  if (tool == Tool::Text || tool == Tool::SpeechBalloon) {
    auto *dialog = canvas.findChild<TextContentDialog *>();
    if (dialog) {
      dialog->findChild<QPlainTextEdit *>("balloonText")->setPlainText("Label");
      dialog->accept();
    }
  } else if (tool == Tool::Image) {
    auto *dialog = canvas.findChild<ImageInsertDialog *>();
    if (dialog) { QApplication::clipboard()->setImage(asset()); dialog->useClipboard(); }
  }
  QTRY_VERIFY(!canvas.interactionActive());
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // retire the finished dialog before the next object
}
}  // namespace

class AnnotationToolMatrixTest : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase() {
    QVERIFY(settings.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  }
  void init() { QVERIFY(initializeNativeEditorPreferences(settings.filePath("editor.json")).isEmpty()); }

  // EA-001: the 20 stable IDs of annotation-tools.json are exactly the tools
  // the native canvas, shared action factory and editor window expose.
  void contractToolsAreTheCanvasToolsAndNoneIsExtra() {
    const auto contract = contractTools();
    QCOMPARE(contract.size(), 20);
    QSet<QString> ids, documentTypes;
    for (const auto &tool : contract) { ids.insert(tool.id); if (tool.hasDocumentType) documentTypes.insert(tool.documentType); }
    QCOMPARE(ids.size(), 20);
    // Document types of the contract: the 17 drawable shapes plus the two
    // canvas operations; Select is a mode with no document type.
    QSet<QString> shapes = documentTypes;
    shapes.remove(QStringLiteral("crop")); shapes.remove(QStringLiteral("cut-out"));
    QCOMPARE(shapes.size(), 17);
    QSet<QString> modelTypes;
    for (const QString &type : shapeTypes()) modelTypes.insert(type);
    QCOMPARE(modelTypes, shapes);

    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(420, 340); canvas.show(); QTest::qWait(10); canvas.zoomToFit();
    const auto actions = annotationToolActions(canvas, &canvas);
    QSet<QString> actionIds; QSet<int> distinctTools;
    for (auto *action : actions) actionIds.insert(action->property("annotationToolId").toString());
    QCOMPARE(actions.size(), 20); QCOMPARE(actionIds, ids);
    for (const auto &tool : contract) {
      QAction *action = nullptr;
      for (auto *candidate : actions) if (candidate->property("annotationToolId").toString() == tool.id) action = candidate;
      QVERIFY2(action, qPrintable(tool.id)); QVERIFY2(action->isEnabled(), qPrintable(tool.id)); QVERIFY(action->isCheckable());
      QCOMPARE(normalized(action->text()), tool.id.toLower());  // "Speech Balloon" presents SpeechBalloon
      action->trigger();
      QVERIFY2(action->isChecked(), qPrintable(tool.id));
      distinctTools.insert(int(canvas.tool()));
      // Creation tools create exactly their contracted document type; Select,
      // Crop and CutOut have no drawable type.
      const QString type = EditorCanvas::shapeType(canvas.tool());
      const bool drawable = tool.hasDocumentType && tool.documentType != u"crop" && tool.documentType != u"cut-out";
      QCOMPARE(type, drawable ? tool.documentType : QString());
      for (auto *other : actions) if (other != action) QVERIFY2(!other->isChecked(), qPrintable(tool.id));
    }
    QCOMPARE(distinctTools.size(), 20);  // every contracted ID selects a different canvas tool

    // Presentation differences are not omissions: the capture annotation
    // surface drops only the two canvas operations (Crop and CutOut).
    const auto capture = annotationToolActions(canvas, &canvas, false);
    QSet<QString> captureIds;
    for (auto *action : capture) captureIds.insert(action->property("annotationToolId").toString());
    QSet<QString> expected = ids; expected.remove(QStringLiteral("Crop")); expected.remove(QStringLiteral("CutOut"));
    QCOMPARE(captureIds, expected);
  }

  // The same 20 appear, once each and with a name, in the editor's
  // Image > Annotation Tools menu, which is the user's route to them.
  void editorWindowMenuListsEveryContractedToolOnce() {
    const auto contract = contractTools(); QSet<QString> ids;
    for (const auto &tool : contract) ids.insert(tool.id);
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(image(80, 60))); QVERIFY(window); window->show();
    QMenu *menu = nullptr;
    for (auto *candidate : window->menuBar()->findChildren<QMenu *>()) if (candidate->title() == u"Annotation Tools") menu = candidate;
    QVERIFY(menu);
    QList<QString> listed;
    for (auto *action : menu->actions()) {
      QVERIFY(!action->isSeparator()); QVERIFY(!action->text().isEmpty());
      listed.append(action->property("annotationToolId").toString());
    }
    QCOMPARE(listed.size(), 20); QCOMPARE(QSet<QString>(listed.begin(), listed.end()), ids);
    // No tool exists in the window outside the contract.
    QSet<QString> windowIds;
    for (auto *action : window->findChildren<QAction *>()) if (action->property("annotationToolId").isValid()) windowIds.insert(action->property("annotationToolId").toString());
    QCOMPARE(windowIds, ids);
    // Triggering a menu entry selects that tool on the window's canvas.
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    for (auto *action : menu->actions()) {
      action->trigger(); QVERIFY(action->isChecked());
      QCOMPARE(normalized(action->text()), action->property("annotationToolId").toString().toLower());
    }
    QCOMPARE(canvas->tool(), EditorCanvas::Tool::Crop);  // the last entry
  }

  // EA-002: for every one of the 20 tools, selecting it shows its checked
  // state, its applicable options, pointer and keyboard instructions and
  // whether the next completed object keeps the tool active or returns to
  // Select. The reference XerahS defaults (SwitchToSelectionToolAfterDrawing =
  // false) keep a creation tool active, which the behaviour assertion proves.
  void everyToolShowsStateOptionsInstructionsAndKeepsItselfActive_data() {
    QTest::addColumn<QString>("id"); QTest::addColumn<QString>("optionText"); QTest::addColumn<QString>("pointerText");
    QTest::newRow("Select") << "Select" << "selected objects" << "click";
    QTest::newRow("Rectangle") << "Rectangle" << "corner radius" << "drag";
    QTest::newRow("Ellipse") << "Ellipse" << "border style and drop shadow" << "drag";
    QTest::newRow("Line") << "Line" << "optional curve" << "drag";
    QTest::newRow("Arrow") << "Arrow" << "arrow style" << "drag";
    QTest::newRow("Freehand") << "Freehand" << "simplified once" << "drag";
    QTest::newRow("Text") << "Text" << "font family and size" << "click or drag";
    QTest::newRow("SpeechBalloon") << "SpeechBalloon" << "border colour and tail" << "click or drag";
    QTest::newRow("Step") << "Step" << "step number" << "click";
    QTest::newRow("Image") << "Image" << "clipboard image" << "click or drag";
    QTest::newRow("Emoji") << "Emoji" << "the emoji" << "click or drag";
    QTest::newRow("Cursor") << "Cursor" << "mouse pointer image" << "click";
    QTest::newRow("Highlight") << "Highlight" << "tint (fill) colour" << "drag";
    QTest::newRow("SmartEraser") << "SmartEraser" << "mean colour" << "drag";
    QTest::newRow("Blur") << "Blur" << "blur radius" << "drag";
    QTest::newRow("Pixelate") << "Pixelate" << "pixel cell size" << "drag";
    QTest::newRow("Magnify") << "Magnify" << "zoom factor" << "drag";
    QTest::newRow("Spotlight") << "Spotlight" << "dim percentage" << "drag";
    QTest::newRow("Crop") << "Crop" << "numeric bounds" << "drag";
    QTest::newRow("CutOut") << "CutOut" << "removes columns" << "drag";
  }
  void everyToolShowsStateOptionsInstructionsAndKeepsItselfActive() {
    QFETCH(QString, id); QFETCH(QString, optionText); QFETCH(QString, pointerText);
    bool contracted = false;
    for (const auto &tool : contractTools()) contracted = contracted || tool.id == id;
    QVERIFY2(contracted, "the data row names a tool outside annotation-tools.json");
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    const auto actions = annotationToolActions(canvas, &canvas);
    std::unique_ptr<QLabel> label(annotationToolInstructions(canvas, nullptr));
    QAction *action = toolAction(actions, id); QVERIFY(action);
    action->trigger();

    // Active state, on the action and exclusively.
    QVERIFY(action->isChecked());
    for (auto *other : actions) if (other != action) QVERIFY(!other->isChecked());
    // Applicable options and pointer/keyboard guidance, in every place a user or assistive technology reads them.
    const QString full = canvas.toolInstructions();
    QVERIFY2(full.contains("Options:"), qPrintable(full));
    QVERIFY2(full.contains(optionText, Qt::CaseInsensitive), qPrintable(full));
    QVERIFY2(full.contains(pointerText, Qt::CaseInsensitive), qPrintable(full));
    QVERIFY2(full.contains("Escape cancels"), qPrintable(full));            // keyboard: cancel a live gesture
    QVERIFY2(full.contains("Arrows move one image pixel"), qPrintable(full)); // keyboard: transform keys
    QCOMPARE(canvas.accessibleDescription(), full);
    QCOMPARE(label->accessibleDescription(), full); QCOMPARE(label->toolTip(), full);
    QVERIFY(!label->text().isEmpty()); QVERIFY(label->text().contains("Escape cancels"));
    QVERIFY2(action->statusTip().contains(EditorCanvas::toolOptions(canvas.tool())), qPrintable(action->statusTip()));
    QCOMPARE(action->toolTip(), action->statusTip());
    QVERIFY2(!EditorCanvas::toolOptions(canvas.tool()).isEmpty(), qPrintable(id));
    // Keep-active or return-to-Select is stated for every tool that completes an object.
    if (id != u"Select") QVERIFY2(full.contains("stays active"), qPrintable(full));
    QVERIFY2(!full.contains("returns to Select"), qPrintable(full));

    // Behaviour matches the statement: after a completed object the same tool
    // is still checked and active, and the next object is created without
    // re-selecting it.
    const auto tool = canvas.tool();
    if (id == u"Select") {
      drawObject(canvas, {100, 100}, {300, 250});
      QCOMPARE(session.undoCount(), 0); QVERIFY(session.document().annotations.empty());  // Select never creates
    } else if (id == u"Crop" || id == u"CutOut") {
      QSignalSpy crop(&canvas, &EditorCanvas::cropRequested), cut(&canvas, &EditorCanvas::cutOutRequested);
      drawObject(canvas, {100, 100}, {300, 250});
      QCOMPARE(crop.count() + cut.count(), 1); QCOMPARE(cut.count(), id == u"CutOut" ? 1 : 0);
      drawObject(canvas, {400, 300}, {600, 450});
      QCOMPARE(crop.count() + cut.count(), 2);
    } else {
      drawObject(canvas, {100, 100}, {300, 250});
      QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(session.undoCount(), 1);
      QCOMPARE(EditorCanvas::shapeType(tool), std::get<RectangleAnnotation>(session.document().annotations[0]).type());
      QCOMPARE(canvas.tool(), tool); QVERIFY(action->isChecked());
      drawObject(canvas, {400, 300}, {600, 450});
      QCOMPARE(session.document().annotations.size(), std::size_t(2)); QCOMPARE(session.undoCount(), 2);
      QCOMPARE(canvas.tool(), tool); QVERIFY(action->isChecked());
    }
    QCOMPARE(canvas.tool(), tool); QVERIFY(action->isChecked());
    QVERIFY(label->text().contains("Escape cancels"));
  }

  // EA-003 for the directional, freehand and asset objects: Select hits the
  // drawn path (not the bounding box) of Line, Arrow and Freehand and the whole
  // box of Image, Emoji and Cursor, then selects, moves, resizes and rotates
  // them in one undo step each and refuses hidden or locked objects.
  void lineArrowFreehandAndAssetsSelectMoveResizeRotateAndProtect_data() {
    QTest::addColumn<QString>("type");
    for (const char *type : {"line", "arrow", "freehand", "image", "emoji", "cursor"}) QTest::newRow(type) << QString::fromLatin1(type);
  }
  void lineArrowFreehandAndAssetsSelectMoveResizeRotateAndProtect() {
    QFETCH(QString, type);
    const bool stroke = type == shape::line || type == shape::arrow || type == shape::freehand;
    QByteArray png; { QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(asset().save(&buffer, "PNG")); }
    const auto build = [&](bool locked, bool hidden) {
      auto session = std::make_unique<EditorSession>(document());
      ShapeContent content; content.type = type; EmbeddedImages assets;
      if (stroke) content.points = type == shape::freehand ? std::vector<PointF>{{0, 0}, {0.5, 0.5}, {1, 1}} : std::vector<PointF>{{0, 0}, {1, 1}};
      else { content.imageKey = "asset-1"; assets = {{"asset-1", png}}; }
      const auto id = session->createShape({{100, 100}, {300, 300}, {}}, content, session->stateId(), session->selection(), assets);
      Q_ASSERT(id);
      if (locked || hidden) {
        auto doc = session->document(); auto &a = std::get<RectangleAnnotation>(doc.annotations[0]);
        if (locked) a.extra = {{"locked", true}};
        if (hidden) a.visible = false;
        session = std::make_unique<EditorSession>(doc);
      }
      return std::make_pair(std::move(session), *id);
    };
    const QImage source = image();
    const auto prepare = [&](EditorCanvas &canvas) {
      canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom(); canvas.setTool(EditorCanvas::Tool::Select);
    };
    const auto click = [&](EditorCanvas &canvas, QPointF at) { QTest::mouseClick(canvas.viewport(), Qt::LeftButton, {}, canvas.viewState().toView(at).toPoint()); };
    const auto drag = [&](EditorCanvas &canvas, QPointF from, QPointF to) {
      const QPoint a = canvas.viewState().toView(from).toPoint(), b = canvas.viewState().toView(to).toPoint();
      QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a); QTest::mouseMove(canvas.viewport(), b); QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b);
    };

    auto [session, id] = build(false, false);
    EditorCanvas canvas(*session, source); prepare(canvas); session->select({}); const int base = session->undoCount();
    // Hit testing: inside the box but far from a diagonal stroke is empty space.
    const QPointF miss = stroke ? QPointF(290, 110) : QPointF(500, 450);
    click(canvas, miss); QVERIFY(session->selection().isEmpty()); QCOMPARE(session->undoCount(), base);
    click(canvas, {200, 200}); QCOMPARE(session->selection(), QSet<QUuid>{id}); QCOMPARE(session->undoCount(), base);
    if (stroke) {  // a few document pixels from the path still hits, far from it does not
      session->select({}); click(canvas, {203, 197}); QCOMPARE(session->selection(), QSet<QUuid>{id});
      session->select({}); click(canvas, {240, 160}); QVERIFY(session->selection().isEmpty());
      click(canvas, {200, 200});
    }
    // Move: one undo step, bounds translate, content is untouched.
    const auto content = session->selectedRectangle()->content;
    drag(canvas, {200, 200}, {260, 200});
    QCOMPARE(session->undoCount(), base + 1); QCOMPARE(session->selectedRectangle()->left(), 160.0); QCOMPARE(session->selectedRectangle()->right(), 360.0);
    QVERIFY(session->selectedRectangle()->content.points == content.points); QCOMPARE(session->selectedRectangle()->content.imageKey, content.imageKey);
    // Resize by the bottom-right handle: opposite corner fixed, one undo step.
    const double gap = 2 + 3 / canvas.viewState().zoom();
    drag(canvas, {360 + gap, 300 + gap}, {380 + gap, 310 + gap});
    QCOMPARE(session->undoCount(), base + 2);
    QCOMPARE(session->selectedRectangle()->left(), 160.0); QCOMPARE(session->selectedRectangle()->top(), 100.0);
    QCOMPARE(session->selectedRectangle()->right(), 380.0); QCOMPARE(session->selectedRectangle()->bottom(), 310.0);
    // Rotate: one undo step, and hit testing then follows the displayed shape.
    QVERIFY(canvas.canRotateSelection()); QVERIFY(session->rotateSelection(90)); QCOMPARE(session->undoCount(), base + 3);
    const auto *rotated = session->selectedRectangle(); QCOMPARE(rotated->style.rotationDegrees, 90.0);
    const QPointF centre((rotated->left() + rotated->right()) / 2, (rotated->top() + rotated->bottom()) / 2);
    const QPointF before(rotated->left() + 0.15 * (rotated->right() - rotated->left()), rotated->top() + 0.15 * (rotated->bottom() - rotated->top()));
    QTransform turn; turn.translate(centre.x(), centre.y()); turn.rotate(90); turn.translate(-centre.x(), -centre.y());
    session->select({}); click(canvas, turn.map(before)); QCOMPARE(session->selection(), QSet<QUuid>{id});
    if (stroke) { session->select({}); click(canvas, before); QVERIFY(session->selection().isEmpty()); }
    QVERIFY(session->undo()); QVERIFY(session->undo()); QVERIFY(session->undo()); QCOMPARE(session->undoCount(), base);

    // Locked: selectable but never mutated; rotation is unavailable.
    {
      auto [locked, lockedId] = build(true, false); EditorCanvas protectedCanvas(*locked, source); prepare(protectedCanvas);
      const auto original = serializeDocument(locked->document());
      click(protectedCanvas, {200, 200}); QCOMPARE(locked->selection(), QSet<QUuid>{lockedId});
      drag(protectedCanvas, {200, 200}, {260, 200});
      QCOMPARE(serializeDocument(locked->document()), original); QCOMPARE(locked->undoCount(), 0);
      QVERIFY(!protectedCanvas.canRotateSelection()); QVERIFY(!locked->rotateSelection(90)); QVERIFY(!locked->moveSelection(5, 5));
      QVERIFY(!protectedCanvas.deleteSelection()); QCOMPARE(serializeDocument(locked->document()), original);
    }
    // Hidden: cannot be hit, selected or changed.
    {
      auto [hidden, hiddenId] = build(false, true); Q_UNUSED(hiddenId); EditorCanvas hiddenCanvas(*hidden, source); prepare(hiddenCanvas);
      const auto original = serializeDocument(hidden->document());
      click(hiddenCanvas, {200, 200}); QVERIFY(hidden->selection().isEmpty());
      drag(hiddenCanvas, {200, 200}, {260, 200}); QCOMPARE(serializeDocument(hidden->document()), original); QCOMPARE(hidden->undoCount(), 0);
      hidden->select({hiddenId}); QVERIFY(!hidden->moveSelection(5, 5)); QCOMPARE(serializeDocument(hidden->document()), original);
    }
  }

  // EA-004. Policy under test: out-of-canvas geometry is allowed for every
  // drawing tool (bounds are stored unclipped, normalized) and the exporter
  // clips it; zero-area gestures are rejected with a stable diagnostic and no
  // history; click gestures follow each tool's own rule.
  void drawingToolIds_data() {
    QTest::addColumn<QString>("id"); QTest::addColumn<bool>("verticalDragCreates"); QTest::addColumn<bool>("clickCreates");
    for (const char *id : {"Rectangle", "Ellipse", "Highlight", "Blur", "Pixelate", "Magnify", "Spotlight", "SmartEraser"})
      QTest::newRow(id) << QString::fromLatin1(id) << false << false;          // box tools need positive width and height
    for (const char *id : {"Line", "Arrow", "Freehand"})
      QTest::newRow(id) << QString::fromLatin1(id) << true << false;           // straight strokes are padded; a point is not a stroke
    for (const char *id : {"Text", "SpeechBalloon", "Step"})
      QTest::newRow(id) << QString::fromLatin1(id) << false << true;           // a click places a default-size object
    for (const char *id : {"Image", "Emoji", "Cursor"})
      QTest::newRow(id) << QString::fromLatin1(id) << true << true;            // thin or click gestures use the asset's natural size
  }
  void creationAllowsOutOfCanvasAndNormalizesReverseDrags_data() { drawingToolIds_data(); }
  void creationAllowsOutOfCanvasAndNormalizesReverseDrags() {
    QFETCH(QString, id);
    const QImage source = image(200, 150); EditorSession session(document(200, 150)); EditorCanvas canvas(session, source);
    canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    const auto actions = annotationToolActions(canvas, &canvas); toolAction(actions, id)->trigger();
    QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
    drawObject(canvas, {350, 300}, {150, 100});  // reverse, and extending 150 and 150 pixels beyond the canvas
    QCOMPARE(rejected.count(), 0); QCOMPARE(session.undoCount(), 1); QCOMPARE(session.document().annotations.size(), std::size_t(1));
    const auto &created = std::get<RectangleAnnotation>(session.document().annotations[0]);
    QVERIFY(created.left() < created.right()); QVERIFY(created.top() < created.bottom());  // normalized
    QVERIFY2(created.left() >= 150 && created.top() >= 100, qPrintable(id));
    QVERIFY2(created.right() > 200 && created.bottom() > 150, qPrintable(id));            // not clipped to the 200x150 canvas
    QVERIFY(created.right() <= 350 && created.bottom() <= 300);
    // Export clips: the canvas size is unchanged and rendering succeeds.
    const auto rendered = renderDocument(solidImage(200, 150, 0xFFFFFFFF), session.document());
    QVERIFY(rendered.image && !rendered.error); QCOMPARE(rendered.image->width, qint64(200)); QCOMPARE(rendered.image->height, qint64(150));
    QVERIFY(session.undo()); QVERIFY(session.document().annotations.empty());
  }
  void exportClipsAnObjectEntirelyOutsideTheCanvas_data() { drawingToolIds_data(); }
  void exportClipsAnObjectEntirelyOutsideTheCanvas() {
    QFETCH(QString, id);
    const QImage source = image(200, 150); EditorSession session(document(200, 150)); EditorCanvas canvas(session, source);
    canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    const auto actions = annotationToolActions(canvas, &canvas); toolAction(actions, id)->trigger();
    drawObject(canvas, {260, 200}, {340, 260});
    QCOMPARE(session.document().annotations.size(), std::size_t(1)); QCOMPARE(session.undoCount(), 1);
    const ArgbImage white = solidImage(200, 150, 0xFFFFFFFF);
    const auto rendered = renderDocument(white, session.document());
    QVERIFY(rendered.image && !rendered.error);
    if (id == u"Spotlight") QVERIFY(rendered.image->pixels != white.pixels);  // dims everything outside its region, so the whole canvas
    else QVERIFY2(rendered.image->pixels == white.pixels, qPrintable(id));    // otherwise nothing reaches the canvas
  }
  void zeroAreaAndClickGesturesFollowEachToolsRule_data() { drawingToolIds_data(); }
  void zeroAreaAndClickGesturesFollowEachToolsRule() {
    QFETCH(QString, id); QFETCH(bool, verticalDragCreates); QFETCH(bool, clickCreates);
    for (const bool click : {false, true}) {
      const QImage source = image(200, 150); EditorSession session(document(200, 150)); EditorCanvas canvas(session, source);
      canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom();
      const auto actions = annotationToolActions(canvas, &canvas); toolAction(actions, id)->trigger();
      QSignalSpy rejected(&canvas, &EditorCanvas::editRejected);
      if (click) drawObject(canvas, {100, 60}, {100, 60}); else drawObject(canvas, {100, 60}, {100, 120});  // zero width
      const bool creates = click ? clickCreates : verticalDragCreates;
      if (creates) {
        QCOMPARE(session.undoCount(), 1); QCOMPARE(rejected.count(), 0);
        const auto &created = std::get<RectangleAnnotation>(session.document().annotations.at(0));
        QVERIFY2(created.right() > created.left() && created.bottom() > created.top(), qPrintable(id + (click ? " click" : " vertical")));
      } else {
        QVERIFY2(session.document().annotations.empty(), qPrintable(id + (click ? " click" : " vertical")));
        QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
        QCOMPARE(rejected.count(), 1); QCOMPARE(rejected[0][0].toString(), QString("annotation-geometry-invalid"));
      }
          }
  }
  // Non-finite, single-point and empty gestures never reach the document.
  void malformedGesturesAreRejectedForEveryShapeType() {
    const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
    const double huge = std::numeric_limits<double>::max();
    for (const QString &type : shapeTypes()) {
      const auto name = qPrintable(type);
      QVERIFY2(!shapeGeometry(type, {}), name);
      QVERIFY2(!shapeGeometry(type, {{nan, 10}, {50, 60}}), name);
      QVERIFY2(!shapeGeometry(type, {{10, 10}, {50, nan}}), name);
      QVERIFY2(!shapeGeometry(type, {{10, 10}, {inf, 60}}), name);
      QVERIFY2(!shapeGeometry(type, {{10, 10}, {nan, nan}}), name);
      if (isPathShape(type)) {
        QVERIFY2(!shapeGeometry(type, {{10, 10}}), name);               // a single point is not a path
        QVERIFY2(!shapeGeometry(type, {{10, 10}, {10, 10}, {10, 10}}), name);
        QVERIFY2(!shapeGeometry(type, {{10, 10}, {20, 30}, {nan, 40}}), name);  // one bad sample spoils the whole stroke
      }
      // The owned creation path rejects the same inputs with a stable diagnostic and no history.
      EditorSession session(document(200, 150)); ShapeContent content; content.type = type;
      if (isPathShape(type)) content.points = {{0, 0}, {1, 1}};
      const auto state = session.stateId();
      std::vector<RectangleProperties> drafts;
      const std::vector<std::pair<PointF, PointF>> spans{{{nan, 10}, {50, 60}}, {{10, 10}, {50, nan}}, {{10, 10}, {inf, 60}},
          {{-huge, 10}, {huge, 60}}, {{10, 10}, {10, 60}}, {{10, 10}, {50, 10}}, {{10, 10}, {10, 10}}};
      for (const auto &span : spans) { RectangleProperties draft; draft.start = span.first; draft.end = span.second; drafts.push_back(draft); }
      for (const auto &draft : drafts) {
        QVERIFY2(!session.createShape(draft, content, state, session.selection()), name);
        QCOMPARE(session.editDiagnostic(), QString("annotation-geometry-invalid"));
        QCOMPARE(session.undoCount(), 0); QVERIFY(session.document().annotations.empty()); QVERIFY(!session.dirty());
      }
    }
  }
};

QTEST_MAIN(AnnotationToolMatrixTest)
#include "AnnotationToolMatrixTest.moc"
