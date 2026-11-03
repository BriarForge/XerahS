// EDITOR-ANNOTATIONS-001 tool-matrix evidence: every contracted tool is
// checked against the real native actions, canvas and session. Fixtures use
// temporary preferences and in-memory images only.
#include "CanvasCropDialog.h"
#include "EditorCanvas.h"
#include "EditorWindow.h"
#include "NativeAnnotationTools.h"
#include "ImageInsertDialog.h"
#include "NativeEditorPreferences.h"
#include "TextContentDialog.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/CanvasOperations.h"
#include "image-editor/ShapePainter.h"

#include <QAccessible>
#include <QAction>
#include <QToolBar>
#include <QToolButton>
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPushButton>
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
#include <functional>
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

// Reference model of the documented SmartEraser fill, written independently of
// the painter: the integer mean of every in-canvas pixel on the one-pixel ring
// around the integer box [left,right) x [top,bottom), or opaque white if none.
Argb expectedEraserFill(const ArgbImage &source, int left, int top, int right, int bottom) {
  qint64 sum[4] = {0, 0, 0, 0}, count = 0;
  for (int y = top - 1; y <= bottom; ++y)
    for (int x = left - 1; x <= right; ++x) {
      const bool onRing = x == left - 1 || x == right || y == top - 1 || y == bottom;
      if (!onRing || x < 0 || y < 0 || x >= source.width || y >= source.height) continue;
      const Argb p = source.at(x, y);
      sum[0] += p >> 24; sum[1] += (p >> 16) & 255; sum[2] += (p >> 8) & 255; sum[3] += p & 255; ++count;
    }
  if (!count) return 0xFFFFFFFF;
  return (Argb(sum[0] / count) << 24) | (Argb(sum[1] / count) << 16) | (Argb(sum[2] / count) << 8) | Argb(sum[3] / count);
}
// Every pixel differs by position so a wrong ring shows up as a wrong mean.
ArgbImage gradient(qint64 width, qint64 height) {
  ArgbImage image = solidImage(width, height, 0xFF000000);
  for (qint64 y = 0; y < height; ++y)
    for (qint64 x = 0; x < width; ++x)
      image.pixels[std::size_t(y * width + x)] = 0xFF000000 | (Argb((x * 37 + y * 11) % 256) << 16) | (Argb((x * 5 + y * 29) % 256) << 8) | Argb((x * 13 + y * 3 + 90) % 256);
  return image;
}
RectangleAnnotation eraser(PointF start, PointF end) {
  RectangleAnnotation a; a.id = QUuid::createUuid(); a.start = start; a.end = end; a.content.type = shape::smartEraser;
  a.style.rotationDegrees = 0; return a;
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

  // EA-014 / ESET-006, data-driven over every drawable tool and through the production path: persisted defaults ->
  // native window -> pointer creation. A new object snapshots the defaults saved when it was created, changing the
  // saved defaults afterwards never edits an existing object, and editing a selected object's style is one undo step.
  void everyToolsNewObjectSnapshotsSavedDefaultsAndLaterChangesNeverTouchExistingObjects_data() { drawingToolIds_data(); }
  void everyToolsNewObjectSnapshotsSavedDefaultsAndLaterChangesNeverTouchExistingObjects() {
    QFETCH(QString, id);
    QTemporaryDir dir; QVERIFY(dir.isValid()); const QString path = dir.filePath("canvas.png"), prefs = dir.filePath("native/prefs.json");
    QVERIFY(image(400, 300).save(path)); QVERIFY(initializeNativeEditorPreferences(prefs).isEmpty());
    const QJsonObject first{{"BorderColorHex", "#FF112233"}, {"FillColorHex", "#44556677"}, {"Thickness", 7}, {"CornerRadius", 5}, {"BorderStyle", "Dash"},
        {"ArrowStyle", "Double"}, {"Shadow", true}, {"ShadowColorHex", "#80102030"}, {"ShadowBlurRadius", 6}, {"ShadowOpacity", 0.5}, {"ShadowOffsetX", -2},
        {"ShadowOffsetY", 4}, {"CursorType", "Arrow"}, {"BlurStrength", 15}, {"PixelateStrength", 25}, {"MagnifierStrength", 5}, {"MagnifierEllipse", true},
        {"SpotlightStrength", 40}, {"SpotlightBlur", 9}, {"SpotlightEllipse", true}, {"TextFontSize", 31}, {"SpeechBalloonFontSize", 29}, {"StepFontSize", 14}};
    const QJsonObject second{{"BorderColorHex", "#FF998877"}, {"FillColorHex", "#20ABCDEF"}, {"Thickness", 3}, {"CornerRadius", 0}, {"BorderStyle", "Dot"},
        {"ArrowStyle", "Line"}, {"Shadow", false}, {"ShadowColorHex", "#40AABBCC"}, {"ShadowBlurRadius", 1}, {"ShadowOpacity", 1}, {"ShadowOffsetX", 8},
        {"ShadowOffsetY", 9}, {"CursorType", "Default"}, {"BlurStrength", 60}, {"PixelateStrength", 80}, {"MagnifierStrength", 8}, {"MagnifierEllipse", false},
        {"SpotlightStrength", 70}, {"SpotlightBlur", 2}, {"SpotlightEllipse", false}, {"TextFontSize", 22}, {"SpeechBalloonFontSize", 20}, {"StepFontSize", 10}};
    const auto lowered = [](QString name) { name[0] = name[0].toLower(); return name; };
    const auto expect = [&](const RectangleAnnotation &a, const QJsonObject &d, const char *which) {
      const QString label = id + QStringLiteral(" ") + QLatin1String(which); const auto &style = a.style; const auto &content = a.content; const QString type = a.type();
      QVERIFY2(style.strokeColor == parseColor(d.value("BorderColorHex").toString()), qPrintable(label));
      QVERIFY2(style.fillColor == parseColor(d.value("FillColorHex").toString()), qPrintable(label));
      QVERIFY2(style.strokeWidth == d.value("Thickness").toDouble(), qPrintable(label));
      if (hasShapeDecoration(type)) {
        QVERIFY2(style.borderStyle == parseBorderStyle(lowered(d.value("BorderStyle").toString())), qPrintable(label));
        QCOMPARE(style.shadow.enabled, d.value("Shadow").toBool());
        if (style.shadow.enabled) {
          QVERIFY2(style.shadow.color == parseColor(d.value("ShadowColorHex").toString()), qPrintable(label));
          QCOMPARE(style.shadow.blurRadius, d.value("ShadowBlurRadius").toDouble()); QCOMPARE(style.shadow.opacity, d.value("ShadowOpacity").toDouble());
          QCOMPARE(style.shadow.offsetX, d.value("ShadowOffsetX").toDouble()); QCOMPARE(style.shadow.offsetY, d.value("ShadowOffsetY").toDouble());
        }
      } else { QVERIFY2(style.borderStyle == BorderStyle::Solid && !style.shadow.enabled, qPrintable(label)); }
      QCOMPARE(style.cornerRadius, type == shape::rectangle ? d.value("CornerRadius").toDouble() : 0.0);
      QVERIFY2(style.arrowStyle == (type == shape::arrow ? parseArrowStyle(lowered(d.value("ArrowStyle").toString())).value() : ArrowStyle::Classic), qPrintable(label));
      if (type == shape::blur) QCOMPARE(content.strength, d.value("BlurStrength").toDouble());
      if (type == shape::pixelate) QCOMPARE(content.strength, d.value("PixelateStrength").toDouble());
      if (type == shape::magnify) { QCOMPARE(content.strength, d.value("MagnifierStrength").toDouble()); QCOMPARE(content.isEllipse, d.value("MagnifierEllipse").toBool()); }
      if (type == shape::spotlight) {
        QCOMPARE(content.strength, d.value("SpotlightStrength").toDouble()); QCOMPARE(content.isEllipse, d.value("SpotlightEllipse").toBool());
        QCOMPARE(content.blurAmount, d.value("SpotlightBlur").toDouble());
      }
      if (type == shape::cursor) QCOMPARE(content.cursorType, d.value("CursorType").toString());
      if (type == shape::emoji) { QCOMPARE(content.unicodeSequence, QString("1f600")); QCOMPARE(content.displayName, QString("grinning face")); }
      if (type == shape::text) QCOMPARE(content.fontSize, d.value("TextFontSize").toDouble());
      if (type == shape::speechBalloon) QCOMPARE(content.fontSize, d.value("SpeechBalloonFontSize").toDouble());
      if (type == shape::step) QCOMPARE(content.fontSize, d.value("StepFontSize").toDouble());
    };
    QVERIFY(applyDrawingDefaults(first, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show(); QTest::qWait(10);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    const auto tool = EditorCanvas::toolFromName(id); QVERIFY(tool);
    canvas->setTool(*tool);
    // The last tool is remembered for creation tools only (reference IsRememberableAnnotationTool).
    QCOMPARE(lastAnnotationToolFromPreferences(nativeEditorPreferences()), rememberableAnnotationTool(id) ? id : QStringLiteral("Rectangle"));
    drawObject(*canvas, {40, 40}, {140, 120}); QCOMPARE(canvas->annotationDocument().annotations.size(), std::size_t(1));
    const auto firstObject = std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[0]); expect(firstObject, first, "first");
    const auto beforeChange = serializeAnnotationList(canvas->annotationDocument().annotations);
    // Editing the saved defaults leaves the existing object byte-for-byte unchanged, and its history untouched.
    QVERIFY(applyDrawingDefaults(second, nativeEditorPreferenceRevision()).saved);
    QCOMPARE(serializeAnnotationList(canvas->annotationDocument().annotations), beforeChange);
    QCOMPARE(window->findChild<QAction *>("undo")->isEnabled(), true);
    drawObject(*canvas, {200, 40}, {300, 120}); QCOMPARE(canvas->annotationDocument().annotations.size(), std::size_t(2));
    expect(std::get<RectangleAnnotation>(canvas->annotationDocument().annotations[1]), second, "second");
    QCOMPARE(serializeAnnotationList({canvas->annotationDocument().annotations[0]}), QJsonArray{beforeChange[0]});  // still the first snapshot
    // Editing a selected object's style is one undoable mutation of that object alone.
    QVERIFY(canvas->selectAnnotations({firstObject.id}, canvas->annotationState(), canvas->documentGeneration()));
    const auto both = serializeAnnotationList(canvas->annotationDocument().annotations);
    window->findChild<QAction *>("increaseStroke")->trigger();
    const auto edited = serializeAnnotationList(canvas->annotationDocument().annotations);
    QCOMPARE(edited[0].toObject().value("strokeWidth").toDouble(), second.value("Thickness").toDouble() + 1);  // the toolbar step is taken from the current default
    QCOMPARE(edited[1], both[1]);
    window->findChild<QAction *>("undo")->trigger();
    QCOMPARE(serializeAnnotationList(canvas->annotationDocument().annotations), both);  // a single undo restores everything
    window->findChild<QAction *>("redo")->trigger();
    QCOMPARE(serializeAnnotationList(canvas->annotationDocument().annotations), edited);
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

  // EA-012: the documented fill is the mean colour of the one-pixel ring.
  void smartEraserFillsWithTheMeanOfANonUniformRing() {
    const ArgbImage source = gradient(60, 40); auto doc = document(60, 40); doc.annotations = {eraser({20, 15}, {40, 25})};
    const auto rendered = renderDocument(source, doc); QVERIFY(rendered.image);
    const Argb fill = expectedEraserFill(source, 20, 15, 40, 25);
    QVERIFY(fill != source.at(19, 14) && fill != source.at(40, 25));  // a mean, not any single ring pixel
    for (int y = 15; y < 25; ++y) for (int x = 20; x < 40; ++x) QCOMPARE(rendered.image->at(x, y), fill);
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 60; ++x)
      if (x < 20 || x >= 40 || y < 15 || y >= 25) QCOMPARE(rendered.image->at(x, y), source.at(x, y));  // nothing else changes
    // The interior content, unlike the ring, does not influence the fill.
    ArgbImage altered = source; for (int y = 15; y < 25; ++y) for (int x = 20; x < 40; ++x) altered.pixels[std::size_t(y * 60 + x)] = 0xFF123456;
    const auto again = renderDocument(altered, doc); QVERIFY(again.image); QCOMPARE(again.image->at(30, 20), fill);
  }
  void smartEraserClipsItsRingAtTheCanvasEdges() {
    const ArgbImage source = gradient(30, 30);
    struct Case { int l, t, r, b; };
    for (const Case c : {Case{0, 0, 10, 10}, Case{20, 20, 30, 30}, Case{0, 12, 8, 20}, Case{22, 0, 30, 9}}) {
      auto doc = document(30, 30); doc.annotations = {eraser({double(c.l), double(c.t)}, {double(c.r), double(c.b)})};
      const auto rendered = renderDocument(source, doc); QVERIFY(rendered.image);
      const Argb fill = expectedEraserFill(source, c.l, c.t, c.r, c.b);
      for (int y = c.t; y < c.b; ++y) for (int x = c.l; x < c.r; ++x) QCOMPARE(rendered.image->at(x, y), fill);
    }
    // No ring pixel on the canvas at all: documented opaque white.
    auto whole = document(30, 30); whole.annotations = {eraser({0, 0}, {30, 30})};
    const auto all = renderDocument(source, whole); QVERIFY(all.image);
    for (const Argb pixel : all.image->pixels) QCOMPARE(pixel, Argb(0xFFFFFFFF));
  }
  void smartEraserIsDeterministicAndReadsOnlyTheSource() {
    const ArgbImage source = gradient(60, 40); auto doc = document(60, 40); doc.annotations = {eraser({20, 15}, {40, 25})};
    const auto first = renderDocument(source, doc), second = renderDocument(source, doc);
    QVERIFY(first.image && second.image); QVERIFY(first.image->pixels == second.image->pixels);
    // An earlier annotation covering the ring does not change the fill: the ring is read from the immutable source.
    auto covered = doc; RectangleAnnotation under; under.id = QUuid::createUuid(); under.start = {10, 5}; under.end = {50, 35};
    under.style.fillColor = 0xFFFF0000; under.style.strokeColor = 0xFFFF0000; covered.annotations = {under, doc.annotations[0]};
    const auto rendered = renderDocument(source, covered); QVERIFY(rendered.image);
    QCOMPARE(rendered.image->at(30, 20), first.image->at(30, 20));
    QCOMPARE(rendered.image->at(30, 20), expectedEraserFill(source, 20, 15, 40, 25));
  }
  void smartEraserResultIsPreservedByHistoryAndNeverEditsTheSource() {
    const ArgbImage source = gradient(60, 40); EditorSession session(document(60, 40));
    const auto before = renderDocument(source, session.document()); QVERIFY(before.image);
    ShapeContent content; content.type = shape::smartEraser;
    const auto id = session.createShape({{20, 15}, {40, 25}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    const auto erased = renderDocument(source, session.document()); QVERIFY(erased.image);
    QCOMPARE(erased.image->at(30, 20), expectedEraserFill(source, 20, 15, 40, 25));
    const auto sourcePng = session.document().sourceImagePng;
    QVERIFY(session.undo()); QVERIFY(!session.dirty());
    QVERIFY(renderDocument(source, session.document()).image->pixels == before.image->pixels);   // content restored
    QVERIFY(session.redo()); QCOMPARE(session.undoCount(), 1);
    const auto redone = renderDocument(source, session.document()); QVERIFY(redone.image);
    QVERIFY(redone.image->pixels == erased.image->pixels);  // the committed result survives undo and redo
    QCOMPARE(session.document().sourceImagePng, sourcePng); // erasing is an annotation; the source pixels are never rewritten
    // The tool and its text never claim content-aware removal.
    const QImage display = image(60, 40); EditorCanvas canvas(session, display); canvas.setTool(EditorCanvas::Tool::SmartEraser);
    const QString description = canvas.toolInstructions();
    QVERIFY(description.contains("not content-aware"));
  }

  // EA-013: CutOut removes the declared span on either axis and moves pixels
  // and annotations by one deterministic rule (the reference XerahS rule).
  void cutOutMovesPixelsAndAnnotationsByTheDocumentedRule_data() {
    QTest::addColumn<bool>("columns");
    QTest::newRow("columns") << true;
    QTest::newRow("rows") << false;
  }
  void cutOutMovesPixelsAndAnnotationsByTheDocumentedRule() {
    QFETCH(bool, columns);
    // The strip is 40..60 along the cut axis; the other axis is 0..30 for every object.
    const auto at = [&](double along, double across) { return columns ? PointF{along, across} : PointF{across, along}; };
    const auto box = [&](const QString &type, double from, double to, std::vector<PointF> points = {}) {
      RectangleAnnotation a; a.id = QUuid::createUuid(); a.content.type = type; a.content.points = std::move(points);
      a.start = at(from, 0); a.end = at(to, 30); a.style.strokeWidth = 2; return a;
    };
    const qint64 length = 100, other = 30;
    ArgbImage source = solidImage(columns ? length : other, columns ? other : length, 0xFF000000);
    for (qint64 y = 0; y < source.height; ++y) for (qint64 x = 0; x < source.width; ++x)
      source.pixels[std::size_t(y * source.width + x)] = 0xFF000000 | Argb((columns ? x : y) * 2) << 8 | Argb(columns ? y : x);
    const auto before = box(shape::rectangle, 10, 30), inside = box(shape::ellipse, 45, 55), after = box(shape::rectangle, 70, 90),
        spanning = box(shape::rectangle, 20, 80), startsInside = box(shape::rectangle, 50, 80), endsAtStrip = box(shape::rectangle, 10, 50),
        line = box(shape::line, 30, 90, {{0, 0}, {0.5, 0.5}, {1, 1}});
    auto rotatedInside = box(shape::rectangle, 20, 80); rotatedInside.start = at(20, 10); rotatedInside.end = at(80, 20);  // 60 x 10 box
    // Rotated a quarter turn about its centre (50) it spans 45..55 along the cut axis, wholly inside the strip.
    rotatedInside.style.rotationDegrees = columns ? 90 : 0;
    if (!columns) { rotatedInside.start = at(45, 0); rotatedInside.end = at(55, 30); rotatedInside.style.rotationDegrees = 0; }
    auto rotatedAfter = box(shape::rectangle, 70, 90); rotatedAfter.style.rotationDegrees = 90;  // 20 x 30 turned: 75..95 or so, wholly after
    rotatedAfter.start = at(75, 5); rotatedAfter.end = at(85, 25); rotatedAfter.style.rotationDegrees = 90;
    auto doc = document(source.width, source.height);
    doc.annotations = {before, inside, after, spanning, startsInside, endsAtStrip, line, rotatedInside, rotatedAfter};
    CanvasOperation op; op.action = CanvasAction::CutOut; op.cutColumns = columns;
    op.start = at(60, 5); op.end = at(40, 25);  // reverse drag; only the cut axis matters
    const auto result = applyCanvasOperation(doc, source, op);
    QVERIFY(result.document && result.image && !result.error);
    QCOMPARE(result.image->width, columns ? qint64(80) : other); QCOMPARE(result.image->height, columns ? other : qint64(80));
    QCOMPARE(result.document->canvasWidth, result.image->width); QCOMPARE(result.document->canvasHeight, result.image->height);
    // Pixels: those before the strip stay, those after move back by 20; the strip is gone.
    for (qint64 y = 0; y < result.image->height; ++y) for (qint64 x = 0; x < result.image->width; ++x) {
      const qint64 along = columns ? x : y; const qint64 sx = columns && along >= 40 ? x + 20 : x, sy = !columns && along >= 40 ? y + 20 : y;
      QCOMPARE(result.image->at(x, y), source.at(sx, sy));
    }
    const auto &out = result.document->annotations;
    const auto get = [&](const RectangleAnnotation *original) -> const RectangleAnnotation * {
      for (const auto &a : out) if (annotationId(a) == original->id) return &std::get<RectangleAnnotation>(a);
      return nullptr;
    };
    const auto lo = [&](const RectangleAnnotation *r) { return columns ? r->start.x : r->start.y; };
    const auto hi = [&](const RectangleAnnotation *r) { return columns ? r->end.x : r->end.y; };
    const auto crossLo = [&](const RectangleAnnotation *r) { return columns ? r->start.y : r->start.x; };
    const auto crossHi = [&](const RectangleAnnotation *r) { return columns ? r->end.y : r->end.x; };
    QVERIFY(get(&before)); QCOMPARE(lo(get(&before)), 10.0); QCOMPARE(hi(get(&before)), 30.0);               // before: unchanged
    QVERIFY(!get(&inside));                                                                                    // wholly inside: removed
    QVERIFY(get(&after)); QCOMPARE(lo(get(&after)), 50.0); QCOMPARE(hi(get(&after)), 70.0);                    // after: moves back by 20
    QVERIFY(get(&spanning)); QCOMPARE(lo(get(&spanning)), 20.0); QCOMPARE(hi(get(&spanning)), 60.0);          // spans the end: shortened
    QVERIFY(get(&startsInside)); QCOMPARE(lo(get(&startsInside)), 40.0); QCOMPARE(hi(get(&startsInside)), 60.0);  // starts inside: starts at the cut
    QVERIFY(get(&endsAtStrip)); QCOMPARE(lo(get(&endsAtStrip)), 10.0); QCOMPARE(hi(get(&endsAtStrip)), 50.0); // ends inside the strip: kept
    QCOMPARE(crossLo(get(&spanning)), 0.0); QCOMPARE(crossHi(get(&spanning)), 30.0);                          // the other axis is never touched
    // A path keeps its points at their document positions: 30 stays, 60 (past the strip) becomes 40, 90 becomes 70.
    const auto *path = get(&line); QVERIFY(path);
    QCOMPARE(lo(path), 30.0); QCOMPARE(hi(path), 70.0);
    const double span = hi(path) - lo(path), middle = columns ? path->content.points[1].x : path->content.points[1].y;
    QCOMPARE(lo(path) + middle * span, 40.0);
    QVERIFY(!get(&rotatedInside));                                                                              // rotated extent inside the strip: removed
    QVERIFY(get(&rotatedAfter)); QCOMPARE(lo(get(&rotatedAfter)), 55.0); QCOMPARE(hi(get(&rotatedAfter)), 65.0);  // rotated, after: moves back
    // Stable IDs and document order for everything kept.
    QList<QUuid> order; for (const auto &a : out) order << annotationId(a);
    QList<QUuid> expected; for (const QUuid &id : {before.id, after.id, spanning.id, startsInside.id, endsAtStrip.id, line.id, rotatedAfter.id}) expected << id;
    QCOMPARE(order.size(), expected.size());
    for (const auto &id : expected) QVERIFY(order.contains(id));
    // Degenerate spans are refused without a partial result.
    for (const auto &bad : {std::make_pair(at(0, 0), at(100, 30)), std::make_pair(at(50, 0), at(50, 30)),
                            std::make_pair(PointF{std::numeric_limits<double>::quiet_NaN(), 0}, at(60, 30))}) {
      CanvasOperation invalid = op; invalid.start = bad.first; invalid.end = bad.second;
      const auto refused = applyCanvasOperation(doc, source, invalid);
      QVERIFY(refused.error); QVERIFY(!refused.document); QVERIFY(!refused.image);
    }
    auto unsupported = doc; UnsupportedAnnotation future; future.id = QUuid::createUuid(); future.raw = QJsonObject{{"type", "future"}}; unsupported.annotations.push_back(future);
    const auto refused = applyCanvasOperation(unsupported, source, op); QVERIFY(refused.error); QVERIFY(!refused.document);
  }

  // EA-013/EA-017 native: Crop (pointer + dialog) and CutOut (pointer, both
  // axes) are single history operations that mark the document dirty, undo to
  // the clean checkpoint, redo, and save as clean.
  void cropAndCutOutAreOneNativeHistoryOperationWithDirtyState() {
    QTemporaryDir dir; const QString path = dir.filePath("canvas.png");
    QImage pixels(100, 60, QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y) for (int x = 0; x < 100; ++x) pixels.setPixel(x, y, qRgb(x * 2, y * 4, 90));
    QVERIFY(pixels.save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->resize(900, 600); window->show(); QTest::qWait(10);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom(); canvas->setFocus();
    const auto dirty = [&] { return window->windowTitle().contains(" •"); };
    const auto act = [&](const char *name) { auto *a = window->findChild<QAction *>(name); Q_ASSERT(a); a->trigger(); };
    const auto size = [&] { return QSize(int(canvas->annotationDocument().canvasWidth), int(canvas->annotationDocument().canvasHeight)); };
    const auto drag = [&](QPointF from, QPointF to) {
      QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(from).toPoint());
      QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView(to).toPoint());
    };
    QVERIFY(!dirty()); QVERIFY(!window->findChild<QAction *>("undo")->isEnabled()); QCOMPARE(size(), QSize(100, 60));

    // Crop: the pointer proposes the region, the native dialog applies it.
    QTimer applyCrop; applyCrop.setInterval(5);
    connect(&applyCrop, &QTimer::timeout, &applyCrop, [&] {
      if (auto *dialog = qobject_cast<CanvasCropDialog *>(QApplication::activeModalWidget())) {
        applyCrop.stop(); auto *apply = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
        QTRY_VERIFY(apply->isEnabled()); apply->click();
      }
    });
    act("cropTool"); applyCrop.start(); drag({10, 10}, {70, 50}); applyCrop.stop();
    const QSize cropped = size(); QVERIFY(cropped.width() >= 60 && cropped.width() <= 61 && cropped.height() >= 40 && cropped.height() <= 41);  // pointer edges round outwards
    QVERIFY(dirty()); QVERIFY(window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(!window->findChild<QAction *>("redo")->isEnabled());
    act("undo"); QCOMPARE(size(), QSize(100, 60)); QVERIFY(!dirty()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());  // back at the clean checkpoint
    act("redo"); QCOMPARE(size(), cropped); QVERIFY(dirty());
    act("undo"); QVERIFY(!dirty());

    // CutOut columns: a 20 px strip at x = 30..50.
    act("cutoutTool"); canvas->resetZoom();
    drag({30, 20}, {50, 25});
    QTRY_COMPARE(size(), QSize(80, 60)); QVERIFY(dirty()); QVERIFY(window->findChild<QAction *>("undo")->isEnabled()); QVERIFY(!window->findChild<QAction *>("redo")->isEnabled());
    QCOMPARE(canvas->tool(), EditorCanvas::Tool::CutOut);
    act("undo"); QCOMPARE(size(), QSize(100, 60)); QVERIFY(!dirty()); QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    act("redo"); QCOMPARE(size(), QSize(80, 60)); QVERIFY(dirty());
    // CutOut rows on the already cut canvas: a 10 px strip at y = 20..30 (taller than wide drag).
    drag({40, 20}, {44, 30});
    QTRY_COMPARE(size(), QSize(80, 50)); QVERIFY(dirty());
    act("undo"); QCOMPARE(size(), QSize(80, 60)); QVERIFY(dirty());      // still dirty: the first cut is not the saved state
    act("undo"); QCOMPARE(size(), QSize(100, 60)); QVERIFY(!dirty());
    act("redo"); act("redo"); QCOMPARE(size(), QSize(80, 50)); QVERIFY(dirty());
    // Saving makes the cut canvas the clean state, and undo is dirty again.
    act("saveImage"); QVERIFY(!dirty()); QCOMPARE(QImage(path).size(), QSize(80, 50));
    QCOMPARE(QImage(path).pixel(29, 0), pixels.pixel(29, 0)); QCOMPARE(QImage(path).pixel(30, 0), pixels.pixel(50, 0));
    act("undo"); QVERIFY(dirty()); act("redo"); QVERIFY(!dirty());
  }

  // EA-017: every listed mutation (create, edit, delete, paste, duplicate,
  // order, style, crop, cut-out) is exactly one history operation that makes
  // the saved document dirty, undoes back to the clean checkpoint, redoes to
  // dirty, and saves clean. Selection and creation defaults are not mutations.
  void everyListedMutationIsOneHistoryOperationWithExactDirtyState() {
    const auto png = [](const ArgbImage &pixels) {
      QImage out(int(pixels.width), int(pixels.height), QImage::Format_ARGB32);
      for (qint64 y = 0; y < pixels.height; ++y) for (qint64 x = 0; x < pixels.width; ++x) out.setPixel(int(x), int(y), pixels.at(x, y));
      QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); out.save(&buffer, "PNG"); return bytes;
    };
    const ArgbImage pixels = gradient(100, 60);
    auto base = document(100, 60); base.sourceImagePng = png(pixels); base.createdAt = base.modifiedAt = "2026-11-03T00:00:00Z";
    EditorSession session(base);
    const auto first = session.createRectangle({10, 10}, {40, 30}); const auto second = session.createRectangle({50, 10}, {90, 40}); QVERIFY(first && second);
    ShapeContent text; text.type = shape::text; text.text = "Note";
    const auto note = session.createShape({{10, 40}, {60, 55}, {}}, text, session.stateId(), session.selection()); QVERIFY(note);
    session.markSaved(); QVERIFY(!session.dirty());

    // Non-mutations leave the saved state clean.
    session.select({*first}); session.select({*first, *second}); session.select({});
    QVERIFY(session.setCreationStyle([] { RectangleStyle style; style.strokeColor = 0xFF00AA00; style.strokeWidth = 9; return style; }()));
    QVERIFY(!session.dirty());
    const int undoBefore = session.undoCount();
    QCOMPARE(undoBefore, 3);
    session.select({});

    const auto run = [&](const char *name, const std::function<bool()> &mutation) {
      QVERIFY2(!session.dirty(), name);
      const int before = session.undoCount();
      QVERIFY2(mutation(), name);
      QVERIFY2(session.dirty(), name);
      QVERIFY2(session.undoCount() == before + 1, name);  // exactly one history operation
      QVERIFY2(session.undo(), name);
      QVERIFY2(!session.dirty(), name);                    // the clean checkpoint is reachable again
      QVERIFY2(session.redo(), name);
      QVERIFY2(session.dirty(), name);
      session.markSaved(); QVERIFY2(!session.dirty(), name);  // saving makes the result clean
      QVERIFY2(session.undo(), name); QVERIFY2(session.dirty(), name);  // and undo past the save is dirty
      session.markSaved();                                  // continue from the restored state
    };
    run("create", [&] { return session.createShape({{70, 45}, {95, 58}, {}}, ShapeContent{}, session.stateId(), session.selection()).has_value(); });
    run("edit text", [&] { session.select({*note}); auto content = session.selectedRectangle()->content; content.text = "Changed"; return session.editShapeContent(*note, content, session.stateId()); });
    run("edit properties", [&] { session.select({*first}); auto edit = RectangleProperties{session.selectedRectangle()->start, session.selectedRectangle()->end, session.selectedRectangle()->style};
                                 edit.style.strokeWidth = 8; return session.editRectangleProperties(*first, edit, session.stateId()); });
    run("move", [&] { session.select({*first}); return session.moveSelection(4, 3); });
    run("resize", [&] { session.select({*first}); return session.resizeSelection(6, 5); });
    run("rotate", [&] { session.select({*first}); return session.rotateSelection(30); });
    run("style stroke colour", [&] { session.select({*first}); return session.setStrokeColor(0xFF0000FF); });
    run("style fill colour", [&] { session.select({*first}); return session.setFillColor(0x8000FF00); });
    run("style thickness", [&] { session.select({*first}); return session.setStrokeWidth(11); });
    run("style opacity", [&] { session.select({*first}); return session.setOpacity(0.5); });
    run("order forward", [&] { session.select({*first}); return session.reorderSelection(EditorSession::Order::Front); });
    run("order backward", [&] { session.select({*second}); return session.reorderSelection(EditorSession::Order::Back); });
    run("delete", [&] { session.select({*first}); return session.deleteSelection(); });
    run("duplicate", [&] { session.select({*first}); return session.duplicateSelection().changed; });
    run("paste", [&] { session.select({}); return session.insertAnnotations({session.document().annotations.front()}, session.stateId()).changed; });
    run("cut", [&] { session.select({*second}); return session.cutAnnotations(session.stateId(), session.selection()).changed; });

    // Canvas edits commit a replacement source and annotations atomically.
    const auto canvasEdit = [&](CanvasOperation op) {
      return [&session, &pixels, op, png] {
        auto result = applyCanvasOperation(session.document(), pixels, op);
        if (!result.document || !result.image) return false;
        result.document->sourceImagePng = png(*result.image);
        return session.commitCanvas(*result.document, session.stateId(), session.selection());
      };
    };
    // The canvas edit keeps `pixels` of the original size, so reset it first when the canvas shrinks.
    CanvasOperation crop; crop.action = CanvasAction::Crop; crop.start = {5, 5}; crop.end = {75, 45};
    run("crop", canvasEdit(crop));
    CanvasOperation columns; columns.action = CanvasAction::CutOut; columns.cutColumns = true; columns.start = {30, 0}; columns.end = {50, 60};
    run("cut-out columns", canvasEdit(columns));
    CanvasOperation rows; rows.action = CanvasAction::CutOut; rows.cutColumns = false; rows.start = {0, 20}; rows.end = {100, 30};
    run("cut-out rows", canvasEdit(rows));

    // A rejected mutation changes nothing: no history, no dirt, redo kept.
    session.select({*note}); QVERIFY(session.undo()); session.markSaved(); const int redo = session.redoCount(); QVERIFY(redo > 0);
    QVERIFY(!session.createShape({{5, 5}, {5, 5}, {}}, ShapeContent{}, session.stateId(), session.selection()));
    QVERIFY(!session.dirty()); QCOMPARE(session.redoCount(), redo);
  }

  // EA-018: selection outline, resize handles and focus must be visible
  // without colour. Each frame is converted to grayscale and the cues are
  // measured as luminance contrast against the same frame without them.
  void selectionAndHandleCuesSurviveGrayscale_data() {
    QTest::addColumn<QString>("type");
    for (const char *type : {"rectangle", "ellipse", "line", "arrow", "freehand", "text", "step", "image", "blur", "pixelate",
                             "smart-eraser", "highlight", "magnify", "spotlight"}) QTest::newRow(type) << QString::fromLatin1(type);
  }
  void selectionAndHandleCuesSurviveGrayscale() {
    QFETCH(QString, type);
    QImage source(300, 200, QImage::Format_ARGB32); source.fill(qRgb(128, 128, 128));  // mid gray: both white and black cues must stand out
    EditorSession session(document(300, 200));
    ShapeContent content; content.type = type;
    if (isPathShape(type)) content.points = type == shape::freehand ? std::vector<PointF>{{0, 0}, {0.5, 1}, {1, 0}} : std::vector<PointF>{{0, 0}, {1, 1}};
    if (type == shape::text) content.text = "Label";
    if (type == shape::step) content.stepValue = 1;
    const auto id = session.createShape({{100, 80}, {200, 140}, {}}, content, session.stateId(), session.selection()); QVERIFY(id);
    EditorCanvas canvas(session, source); canvas.resize(520, 440); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    session.select({}); canvas.viewport()->update(); const QImage plain = canvas.viewport()->grab().toImage().convertToFormat(QImage::Format_Grayscale8);
    session.select({*id}); canvas.viewport()->update(); const QImage marked = canvas.viewport()->grab().toImage().convertToFormat(QImage::Format_Grayscale8);
    QCOMPARE(marked.size(), plain.size());
    const auto diffAt = [&](int x, int y) { return std::abs(qGray(marked.pixel(x, y)) - qGray(plain.pixel(x, y))); };

    // Outline: a band around the bounds that differs from the unselected frame by luminance alone,
    // as a dash pattern rather than a solid line (a pattern cue, not only a hue).
    const double gap = 2 + 3 / canvas.viewState().zoom();
    const QPointF topLeft = canvas.viewState().toView({100 - gap, 80 - gap}), topRight = canvas.viewState().toView({200 + gap, 80 - gap});
    int transitions = 0; bool wasStrong = false; int strongColumns = 0;
    for (int x = int(topLeft.x()) + 12; x < int(topRight.x()) - 12; ++x) {
      int strongest = 0; for (int y = int(topLeft.y()) - 2; y <= int(topLeft.y()) + 1; ++y) strongest = std::max(strongest, diffAt(x, y));
      const bool strong = strongest >= 60; strongColumns += strong;
      if (x > int(topLeft.x()) + 12 && strong != wasStrong) ++transitions;
      wasStrong = strong;
    }
    QVERIFY2(strongColumns >= 20, qPrintable(QString("%1 outline columns differ in luminance").arg(strongColumns)));
    QVERIFY2(transitions >= 6, qPrintable(QString("%1 dash transitions").arg(transitions)));

    // Handles: all eight, each a white square with a dark outline over the mid-gray image.
    const auto handleCentres = std::vector<QPointF>{{100 - gap, 80 - gap}, {150, 80 - gap}, {200 + gap, 80 - gap}, {200 + gap, 110},
                                                    {200 + gap, 140 + gap}, {150, 140 + gap}, {100 - gap, 140 + gap}, {100 - gap, 110}};
    for (const QPointF &centre : handleCentres) {
      const QPoint c = canvas.viewState().toView(centre).toPoint();
      int light = 0, dark = 0, changed = 0;
      for (int y = c.y() - 5; y <= c.y() + 5; ++y) for (int x = c.x() - 5; x <= c.x() + 5; ++x) {
        const int g = qGray(marked.pixel(x, y)); light += g >= 230; dark += g <= 90; changed += diffAt(x, y) >= 60;
      }
      QVERIFY2(light >= 20 && dark >= 8 && changed >= 40, qPrintable(QString("handle at %1,%2: light %3 dark %4 changed %5").arg(c.x()).arg(c.y()).arg(light).arg(dark).arg(changed)));
    }
  }

  // The focus ring is also a luminance cue: focus changes the frame's gray edge.
  void canvasFocusCueSurvivesGrayscale() {
    const QImage source = image(300, 200); EditorSession session(document(300, 200)); EditorCanvas canvas(session, source);
    canvas.resize(520, 440); canvas.show(); canvas.activateWindow(); QTest::qWait(10);
    canvas.clearFocus(); canvas.viewport()->update(); const QImage unfocused = canvas.viewport()->grab().toImage().convertToFormat(QImage::Format_Grayscale8);
    canvas.setFocus(); QTRY_VERIFY(canvas.hasFocus()); canvas.viewport()->update(); const QImage focused = canvas.viewport()->grab().toImage().convertToFormat(QImage::Format_Grayscale8);
    int strong = 0, edge = 0;
    for (int x = 0; x < focused.width(); ++x) for (int y : {1, 2, focused.height() - 2, focused.height() - 3}) { ++edge; strong += std::abs(qGray(focused.pixel(x, y)) - qGray(unfocused.pixel(x, y))) >= 40; }
    QVERIFY2(strong * 2 >= edge / 2, qPrintable(QString("%1 of %2 focus-edge samples differ").arg(strong).arg(edge)));
  }

  // Every tool is a named, checkable native control, and a keyboard user can
  // cancel a live gesture of every tool with Escape.
  void everyToolHasAnAccessibleControlAndEscapeCancelsItsLiveGesture_data() {
    QTest::addColumn<QString>("id");
    for (const auto &tool : contractTools()) QTest::newRow(qPrintable(tool.id)) << tool.id;
  }
  void everyToolHasAnAccessibleControlAndEscapeCancelsItsLiveGesture() {
    QFETCH(QString, id);
    const QImage source = image(); EditorSession session(document()); EditorCanvas canvas(session, source);
    canvas.resize(820, 640); canvas.show(); QTest::qWait(10); canvas.resetZoom();
    const auto actions = annotationToolActions(canvas, &canvas); QAction *action = toolAction(actions, id); QVERIFY(action);
    QToolBar bar; bar.addAction(action); bar.show();
    auto *button = qobject_cast<QToolButton *>(bar.widgetForAction(action)); QVERIFY(button);
    auto *accessible = QAccessible::queryAccessibleInterface(button); QVERIFY(accessible);
    QCOMPARE(normalized(accessible->text(QAccessible::Name)), id.toLower());
    QTest::mouseClick(button, Qt::LeftButton); QVERIFY(action->isChecked()); QVERIFY(accessible->state().checked);
    QVERIFY(!accessible->text(QAccessible::Description).isEmpty() || !action->toolTip().isEmpty());
    if (id == u"Select") return;  // nothing to draw: selection gestures are covered by the move and resize tests
    QSignalSpy cropped(&canvas, &EditorCanvas::cropRequested), cut(&canvas, &EditorCanvas::cutOutRequested);
    const QPoint a = canvas.viewState().toView({100, 100}).toPoint(), b = canvas.viewState().toView({300, 250}).toPoint();
    canvas.setFocus(); QTest::mousePress(canvas.viewport(), Qt::LeftButton, {}, a); QTest::mouseMove(canvas.viewport(), (a + b) / 2);
    QVERIFY(canvas.interactionActive());
    QTest::keyClick(&canvas, Qt::Key_Escape); QVERIFY(!canvas.interactionActive());
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, {}, b);
    QVERIFY(session.document().annotations.empty()); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
    QCOMPARE(cropped.count() + cut.count(), 0); QVERIFY(!canvas.findChild<TextContentDialog *>()); QVERIFY(!canvas.findChild<ImageInsertDialog *>());
    QVERIFY(action->isChecked());  // cancelling a gesture leaves the tool selected
  }
};

QTEST_MAIN(AnnotationToolMatrixTest)
#include "AnnotationToolMatrixTest.moc"
