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
#include <QPlainTextEdit>
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
};

QTEST_MAIN(AnnotationToolMatrixTest)
#include "AnnotationToolMatrixTest.moc"
