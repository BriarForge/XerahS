#include "ApplicationWindow.h"
#include "CaptureHistoryWindow.h"
#include "CaptureWorkflows.h"
#include "ColorPickerDialog.h"
#include "ScreenColorSampler.h"
#include "EffectsDialog.h"
#include "EditorWindow.h"
#include "FileUploadController.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageComparisonDialog.h"
#include "ImageRecognitionDialog.h"
#include "NativeAccessibility.h"
#include "NativeEditorPreferences.h"
#include "PipelineHistoryDialog.h"
#include "PresetDialog.h"
#include "QrCodeDialog.h"
#include "SettingsDialog.h"
#include "UploadDestinationsDialog.h"
#include "VideoConverterDialog.h"
#include "WatchFoldersDialog.h"
#include "WatchFolderService.h"
#include "WorkflowExecutionHistoryDialog.h"
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QSettings>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QStyleOption>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <array>
using namespace xerahs;
using namespace xerahs::app;
namespace {
constexpr std::array<Qt::UIEffect,6> nativeUiEffects{Qt::UI_AnimateMenu,Qt::UI_FadeMenu,Qt::UI_AnimateCombo,Qt::UI_AnimateTooltip,Qt::UI_FadeTooltip,Qt::UI_AnimateToolBox};
class IdleRunner final : public media::ConversionRunner {
public:
  void start(const media::Request &) override {}
  void cancel() override {}
  bool busy() const override { return false; }
};
}
class NativeSurfaceAccessibilityTest : public QObject {
  Q_OBJECT
  QFont m_font;
  QPalette m_palette;
  std::array<bool,6> m_motionDefaults{};
  QTemporaryDir m_settings;
  void audit(QWidget &window) {
    window.setAttribute(Qt::WA_DeleteOnClose, false); window.show(); window.activateWindow(); QTest::qWait(30);
    auto *accessible = QAccessible::queryAccessibleInterface(&window); QVERIFY(accessible);
    QVERIFY2(!accessible->text(QAccessible::Name).isEmpty(), qPrintable(window.objectName()));
    QVERIFY2(!accessible->text(QAccessible::Description).isEmpty(), qPrintable(window.windowTitle()));
    QVERIFY2(window.findChild<QObject *>("nativeAccessibility"), qPrintable(window.windowTitle()));
    for (const auto effect : nativeUiEffects) QVERIFY(!QApplication::isEffectEnabled(effect));
    const auto workArea = window.screen()->availableGeometry();
    QVERIFY2(workArea.contains(window.frameGeometry()), qPrintable(window.windowTitle() + " frame exceeds work area"));
    const auto checkPage = [&] {
      QList<QWidget *> targets;
      QList<QRadioButton *> radios;
      for (auto *widget : window.findChildren<QWidget *>()) {
        if (!widget->isVisible() || !widget->isEnabled() || !(widget->focusPolicy() & Qt::TabFocus) || widget->focusProxy()) continue;
        // A menu bar's platform keyboard convention is Alt, not the tab chain.
        if (widget->inherits("QMenuBar")) continue;
        if (auto *radio = qobject_cast<QRadioButton *>(widget)) { radios.append(radio); if (radio->isChecked()) targets.append(widget); }
        else targets.append(widget);
        const auto *control = QAccessible::queryAccessibleInterface(widget); QVERIFY(control);
        QVERIFY2(!control->text(QAccessible::Name).isEmpty(), qPrintable(QString(widget->metaObject()->className()) + ": " + widget->objectName() + " lacks accessible name"));
      }
      if (!targets.isEmpty()) {
        targets.front()->setFocus(Qt::TabFocusReason); QSet<QWidget *> visited;
        for (int i = 0; i < targets.size()*4+20; ++i) {
          auto *focused = QApplication::focusWidget(); if (!focused) break;
          visited.insert(focused);
          QStyleOption option; option.initFrom(focused); QVERIFY(option.state.testFlag(QStyle::State_HasFocus));
          QTest::keyClick(focused, Qt::Key_Tab); QCoreApplication::processEvents();
        }
        for (auto *target : targets) {
          QVERIFY2(visited.contains(target), qPrintable(target->objectName() + " / " + target->accessibleName() + " missing from keyboard traversal"));
          target->setFocus(Qt::TabFocusReason); QCoreApplication::processEvents();
          QVERIFY2(!target->visibleRegion().isEmpty(), qPrintable(target->objectName() + " remains outside scrolling viewport"));
        }
      }
      // Native radio groups have one Tab stop; arrow keys visit their choices.
      for (auto *radio : radios) if (!radio->isChecked()) {
        QRadioButton *original = nullptr;
        for (auto *peer : radio->parentWidget()->findChildren<QRadioButton *>()) if (peer->isChecked()) original = peer;
        QVERIFY(original); original->setFocus();
        for (int i=0;i<radios.size()+1 && QApplication::focusWidget()!=radio;++i) QTest::keyClick(QApplication::focusWidget(),Qt::Key_Down);
        QCOMPARE(QApplication::focusWidget(),radio); original->setChecked(true);
      }
      for (auto *label : window.findChildren<QLabel *>()) {
        if (!label->isVisible() || label->text().isEmpty() || label->text().contains('<')) continue;
        if (label->wordWrap() && label->hasHeightForWidth())
          QVERIFY2(label->height()+2 >= label->heightForWidth(label->width()), qPrintable(label->objectName() + " wraps into a clipped label"));
      }
      const auto palette = window.palette();
      QVERIFY(palette.color(QPalette::Text).lightness() != palette.color(QPalette::Base).lightness());
      QVERIFY(palette.color(QPalette::WindowText).lightness() != palette.color(QPalette::Window).lightness());
    };
    if (auto *tabs = window.findChild<QTabWidget *>()) for (int i=0;i<tabs->count();++i) { tabs->setCurrentIndex(i); QCoreApplication::processEvents(); checkPage(); }
    else checkPage();
  }
private slots:
  void initTestCase() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,m_settings.path());
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_settings.path());
  }
  void init() {
    m_font = QApplication::font(); m_palette = QApplication::palette(); initializeNativeEditorPreferences();
    auto font = m_font;
    if (font.pointSizeF()>0) font.setPointSizeF(font.pointSizeF()*1.5); else font.setPixelSize(qRound(font.pixelSize()*1.5));
    QApplication::setFont(font);
    auto palette = m_palette;
    for (auto group : {QPalette::Active,QPalette::Inactive,QPalette::Disabled}) {
      for (auto role : {QPalette::Window,QPalette::Base,QPalette::Button}) palette.setColor(group,role,Qt::black);
      for (auto role : {QPalette::WindowText,QPalette::Text,QPalette::ButtonText}) palette.setColor(group,role,Qt::white);
      palette.setColor(group,QPalette::Highlight,Qt::yellow); palette.setColor(group,QPalette::HighlightedText,Qt::black);
    }
    QApplication::setPalette(palette);
    for (size_t i=0;i<nativeUiEffects.size();++i) { m_motionDefaults[i]=QApplication::isEffectEnabled(nativeUiEffects[i]); QApplication::setEffectEnabled(nativeUiEffects[i],false); }
  }
  void cleanup() { for (size_t i=0;i<nativeUiEffects.size();++i) QApplication::setEffectEnabled(nativeUiEffects[i],m_motionDefaults[i]); QApplication::setFont(m_font); QApplication::setPalette(m_palette); initializeNativeEditorPreferences(); }
  void nativeSurfaces_data() {
    QTest::addColumn<QString>("surface");
    for (const auto *name : {"shell","settings","capture-history","pipeline-history","workflow-history","workflows","destinations","watch-folders",
        "editor","effects","presets","colour-picker","colour-sampler","hash-checker","icon-converter","qr-code","qr-scan","ocr","image-comparison","video-converter"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void nativeSurfaces() {
    QFETCH(QString,surface); QTemporaryDir dir;
    QImage image(40,30,QImage::Format_ARGB32); image.fill(Qt::blue);
    editor::AnnotationDocument document;
    settings::ApplicationSettingsStore settings(dir.filePath("settings.json"),dir.path());
    WorkflowShortcutActions shortcuts([] { return workflows::WorkflowShortcutPlan{}; }, [] { return true; });
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
    auto credentials = std::make_shared<uploads::MemoryCredentialStore>();
    uploads::DestinationCatalog destinations(dir.filePath("destinations.json"));
    watch::WatchFolders watchStore(dir.filePath("watch"));
    FileUploadPorts filePorts; filePorts.catalogPath=dir.filePath("destinations.json"); filePorts.credentials=credentials; filePorts.history=journal;
    FileUploadController uploads(filePorts);
    WatchFolderService watcher(watchStore,uploads,[&] { return dir.filePath("captures"); });
    media::NoElements availability;
    std::unique_ptr<QWidget> window;
    QAction capture("Capture region"), cancel("Cancel capture");
    if (surface=="shell") window.reset(new ApplicationWindow({{"capture-region",&capture},{"cancel-capture",&cancel}}));
    if (surface=="settings") window.reset(new SettingsDialog(settings,dir.path(),{},shortcuts,[]{}));
    if (surface=="capture-history") window.reset(new CaptureHistoryWindow(journal,[]{return false;},{}));
    if (surface=="pipeline-history") window.reset(new PipelineHistoryDialog(journal,[]{return false;}));
    if (surface=="workflow-history") window.reset(new WorkflowExecutionHistoryDialog({[]{return WorkflowExecutionListing{};},{}},[]{return false;}));
    if (surface=="destinations") window.reset(new UploadDestinationsDialog(destinations,*credentials));
    if (surface=="watch-folders") window.reset(new WatchFoldersDialog(watcher));
    if (surface=="editor") window.reset(EditorWindow::fromCapturedImage(image));
    if (surface=="effects") window.reset(new EffectsDialog(document,image,[](PreparedCanvasEdit){return false;},nullptr,dir.filePath("effects.json")));
    if (surface=="presets") window.reset(new PresetDialog(document,image,[](PreparedCanvasEdit){return false;},std::make_shared<effects::EffectRegistry>(effects::builtinEffects())));
    if (surface=="colour-picker") window.reset(new ColorPickerDialog);
    if (surface=="colour-sampler") window.reset(new ScreenColorSampler(image));
    if (surface=="hash-checker") window.reset(new HashCheckerDialog);
    if (surface=="icon-converter") window.reset(new IconConverterDialog);
    if (surface=="qr-code") window.reset(new QrCodeDialog);
    if (surface=="qr-scan") window.reset(new ImageRecognitionDialog(ImageRecognitionKind::QrCode));
    if (surface=="ocr") window.reset(new ImageRecognitionDialog(ImageRecognitionKind::Text));
    if (surface=="image-comparison") window.reset(new ImageComparisonDialog(document,image));
    if (surface=="video-converter") window.reset(new VideoConverterDialog(new IdleRunner,&availability));
    if (surface=="workflows") {
      NativeCaptureWorkflows workflows(dir.filePath("workflows.json"));
      QTimer::singleShot(0,[&] { auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); audit(*dialog); dialog->reject(); });
      manageCaptureWorkflows(workflows,nullptr,[]{return true;}); return;
    }
    QVERIFY(window); audit(*window);
    if (auto *dialog=qobject_cast<QDialog *>(window.get())) { QTest::keyClick(dialog,Qt::Key_Escape); QTRY_VERIFY(!dialog->isVisible()); }
  }
  void busyAndErrorTextUpdatesAccessibleDescriptions() {
    ApplicationWindow window(QMap<QString,QAction *>{}); window.show();
    window.showActivity("Capture","Waiting for permission");
    auto *status=window.findChild<QLabel *>("operationStatus"); QVERIFY(status);
    QTRY_COMPARE(status->accessibleDescription(),status->text());
    window.showResult("Capture","Permission denied",false); QTRY_COMPARE(status->accessibleDescription(),status->text());
    auto *accessible=QAccessible::queryAccessibleInterface(status); QVERIFY(accessible);
    QVERIFY(accessible->text(QAccessible::Description).contains("Permission denied"));
    HashCheckerDialog hash; hash.show(); hash.setFile("/missing/private/test-image.png"); hash.compute();
    QTRY_VERIFY(!hash.busy());
    for (auto *label:hash.findChildren<QLabel *>()) if (label->objectName().contains("Status",Qt::CaseInsensitive)) QTRY_COMPARE(label->accessibleDescription(),label->text());
  }
};
QTEST_MAIN(NativeSurfaceAccessibilityTest)
#include "NativeSurfaceAccessibilityTest.moc"
