// EDITOR-ANNOTATIONS-001 tool-matrix evidence: every contracted tool is
// checked against the real native actions, canvas and session. Fixtures use
// temporary preferences and in-memory images only.
#include "EditorCanvas.h"
#include "EditorWindow.h"
#include "NativeAnnotationTools.h"
#include "NativeEditorPreferences.h"
#include "image-editor/AnnotationRenderer.h"
#include "image-editor/ShapePainter.h"

#include <QAction>
#include <QApplication>
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
};

QTEST_MAIN(AnnotationToolMatrixTest)
#include "AnnotationToolMatrixTest.moc"
