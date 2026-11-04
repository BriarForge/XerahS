// IFCAT-005: modal dialogs trap focus only while active, identify their purpose,
// expose a safe default and a cancel action, restore focus on close, and never
// hide a destructive consequence behind generic confirmation text.
#include "AppearanceDialog.h"
#include "CanvasCropDialog.h"
#include "CanvasRotationDialog.h"
#include "ColorPickerDialog.h"
#include "DrawingDefaultsDialog.h"
#include "EffectsDialog.h"
#include "FileUploadController.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageComparisonDialog.h"
#include "ImageInsertDialog.h"
#include "ImageRecognitionDialog.h"
#include "NewImageDialog.h"
#include "PresetDialog.h"
#include "RectanglePropertiesDialog.h"
#include "SelectionRotationDialog.h"
#include "TextContentDialog.h"
#include "UrlOpenDialog.h"
#include "QrCodeDialog.h"
#include "UploadDestinationsDialog.h"
#include "VideoConverterDialog.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "image-editor/AnnotationDocument.h"
#include "media/VideoConversion.h"
#include "watch/WatchFolders.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <functional>
#include <memory>

using namespace xerahs::app;

namespace {
bool safeLabel(const QString &text) {
  static const QRegularExpression destructive(R"(\b(delete|remove|clear|discard|forget|reset|overwrite|replace|erase)\b)", QRegularExpression::CaseInsensitiveOption);
  return !destructive.match(text).hasMatch();
}

// A runner that never converts: the modal contract must not depend on an encoder.
class IdleRunner final : public xerahs::media::ConversionRunner {
public:
  void start(const xerahs::media::Request &) override {}
  void cancel() override {}
  bool busy() const override { return false; }
};
}

class ModalDialogsTest final : public QObject {
  Q_OBJECT
  // A host window whose line edit owns focus before a modal opens.
  struct Host {
    QWidget window; QLineEdit *edit = new QLineEdit(&window);
    Host() { auto *layout = new QVBoxLayout(&window); layout->addWidget(edit); window.show(); window.activateWindow(); edit->setFocus(); }
    // Logical focus of the host window: independent of when the platform grants activation under load.
    bool focusOnEdit() const { return window.focusWidget() == edit; }
  };
  // The preference dialogs have no public constructor: their entry point opens the dialog and
  // runs its own modal loop, so the probe reaches the dialog once it is up and checks it there.
  template <typename Open>
  void verifyOwnModalLoop(Open open, Host &host) {
    bool entered = false, trapped = false, titled = false, cancelAction = false, closed = false, rejected = false;
    QString destructiveDefault;
    QTimer probe; probe.setInterval(5);
    connect(&probe, &QTimer::timeout, &probe, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dialog || entered) return;
      entered = true;
      trapped = QApplication::activeModalWidget() == dialog;
      titled = !dialog->windowTitle().trimmed().isEmpty();  // Identifies its purpose.
      for (auto *button : dialog->findChildren<QPushButton *>()) {
        if (!button->isVisibleTo(dialog)) continue;
        const auto label = button->text().remove('&');
        cancelAction |= label.contains("Close", Qt::CaseInsensitive) || label.contains("Cancel", Qt::CaseInsensitive);
        if (button->isDefault() && !safeLabel(label)) destructiveDefault = label;
      }
      for (auto *box : dialog->findChildren<QDialogButtonBox *>()) cancelAction |= box->button(QDialogButtonBox::Cancel) || box->button(QDialogButtonBox::Close);
      QTest::keyClick(dialog, Qt::Key_Escape);
      closed = !dialog->isVisible();
      rejected = dialog->result() == int(QDialog::Rejected);
    });
    probe.start();
    open();
    probe.stop();
    QVERIFY(entered);
    QVERIFY(trapped);
    QVERIFY(titled);
    QVERIFY(cancelAction);
    QVERIFY2(destructiveDefault.isEmpty(), qPrintable(destructiveDefault));
    QVERIFY(closed);
    QVERIFY(rejected);
    QVERIFY(!QApplication::activeModalWidget());  // Trap released on close.
    QTRY_VERIFY(host.focusOnEdit());  // Focus restored.
  }
private slots:
  void everyChoiceConfirmationDefaultsToASafeButtonAndHasAPurposefulTitle() {
    QDirIterator files(XERAHS_APP_DIR, {"*.cpp"}, QDir::Files, QDirIterator::Subdirectories);
    const QRegularExpression start(R"((new QMessageBox\(|QMessageBox\s+\w+\(|QMessageBox::question\())");
    const QRegularExpression choice(R"(QMessageBox::(Cancel|No|Discard|Yes|Reset|Close|Save)\b)");
    const QRegularExpression safeDefault(R"(setDefaultButton\(QMessageBox::(Cancel|No|Save)\))");
    const QRegularExpression staticDefault(R"(QMessageBox::(No|Cancel)\))");
    const QRegularExpression title("(?:QMessageBox::(?:Question|Warning|Information|Critical),\\s*|question\\([^,]+,\\s*)(?:QStringLiteral\\()?\"([^\"]*)\"");
    const QStringList generic{"Confirm", "Confirmation", "Are you sure?", "Warning", "Question", "Message", "Error", "Information"};
    int checked = 0;
    while (files.hasNext()) {
      const auto path = files.next(); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto text = QString::fromUtf8(file.readAll());
      for (auto it = start.globalMatch(text); it.hasNext();) {
        const auto match = it.next(); const int begin = match.capturedStart(), end = text.indexOf(");", begin);
        const auto head = text.mid(begin, end - begin + 2); const int line = text.left(begin).count('\n') + 1;
        if (!choice.match(head).hasMatch()) continue;  // Single-button information box.
        ++checked; const auto where = QString("%1:%2").arg(path).arg(line);
        if (head.startsWith("QMessageBox::question")) QVERIFY2(staticDefault.match(head).hasMatch(), qPrintable(where + " relies on the implicit (Yes) default"));
        else QVERIFY2(safeDefault.match(text.mid(begin, 2500)).hasMatch(), qPrintable(where + " has no safe explicit default"));
        const auto named = title.match(head);
        if (named.hasMatch()) { QVERIFY2(!generic.contains(named.captured(1)) && named.captured(1).size() >= 8, qPrintable(where + " generic title " + named.captured(1))); }
      }
    }
    QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));
  }
  void removingADestinationSaysWhatIsDeletedDefaultsToNoAndRestoresFocus() {
    Host host; QVERIFY(host.focusOnEdit());
    auto ports = nativeUploadDestinationsPorts(); QVERIFY(ports.confirmRemove);
    QString text, defaultText; bool trapped = false, probed = false;
    QTimer probe; probe.setInterval(5);
    connect(&probe, &QTimer::timeout, &probe, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || probed) return;
      probed = true; text = box->text(); defaultText = box->defaultButton() ? box->defaultButton()->text() : QString();
      trapped = QApplication::activeModalWidget() == box;
      QTest::keyClick(box, Qt::Key_Escape);
    });
    probe.start();
    const bool removed = ports.confirmRemove(&host.window, "Example uploader"); probe.stop();
    QVERIFY(probed); QVERIFY(!removed);
    QVERIFY(text.contains("Example uploader")); QVERIFY(text.contains("delete its secrets from the keyring"));  // Consequence stated, not "Are you sure?".
    QVERIFY2(defaultText.contains("No"), qPrintable(defaultText)); QVERIFY(trapped);
    QVERIFY(!QApplication::activeModalWidget()); QTRY_VERIFY(host.focusOnEdit());
  }
  void modalDialogsTrapFocusOnlyWhileActiveIdentifyPurposeEscapeAndRestoreFocus_data() {
    QTest::addColumn<int>("which");
    QTest::newRow("qr-code") << 0; QTest::newRow("color-picker") << 1; QTest::newRow("hash-checker") << 2; QTest::newRow("icon-converter") << 3; QTest::newRow("new-image") << 4; QTest::newRow("url-open") << 5;
    QTest::newRow("image-insert") << 6; QTest::newRow("scan-qr") << 7; QTest::newRow("recognize-text") << 8; QTest::newRow("image-comparison") << 9;
    QTest::newRow("watch-folders") << 10; QTest::newRow("video-converter") << 11; QTest::newRow("effects") << 12; QTest::newRow("presets") << 13;
    QTest::newRow("editor-appearance") << 14; QTest::newRow("drawing-defaults") << 15;
    QTest::newRow("rectangle-properties") << 16; QTest::newRow("text-content") << 17;
    QTest::newRow("canvas-crop") << 18; QTest::newRow("canvas-rotation") << 19; QTest::newRow("selection-rotation") << 20;
  }
  void modalDialogsTrapFocusOnlyWhileActiveIdentifyPurposeEscapeAndRestoreFocus() {
    QFETCH(int, which); Host host; QVERIFY(host.focusOnEdit());
    if (which == 14 || which == 15) {
      // These preference dialogs are opened by their own entry point, which runs the modal loop.
      QVERIFY(!QApplication::activeModalWidget());  // No trap before it opens.
      if (which == 14) verifyOwnModalLoop([&] { manageEditorAppearance(&host.window); }, host);
      else verifyOwnModalLoop([&] { manageDrawingDefaults(&host.window); }, host);
      return;
    }
    QImage comparisonImage(64, 48, QImage::Format_ARGB32); comparisonImage.fill(Qt::white);
    // Rows 10..13 take injected ports. These helpers must outlive the dialog.
    QTemporaryDir dir;
    QImage sourceImage(64, 48, QImage::Format_ARGB32); sourceImage.fill(Qt::white);
    xerahs::editor::AnnotationDocument sourceDocument;
    xerahs::editor::RectangleAnnotation rectangle; rectangle.start = {0, 0}; rectangle.end = {120, 80};
    xerahs::editor::RectangleAnnotation textShape; textShape.content.type = xerahs::editor::shape::text;
    xerahs::editor::RectangleAnnotation rotated; rotated.id = QUuid::createUuid();
    rotated.start = {10, 10}; rotated.end = {60, 40};
    xerahs::editor::AnnotationDocument rotationDocument; rotationDocument.annotations = {rotated};
    xerahs::editor::CropRegion cropRegion{0, 0, 64, 48};
    auto credentials = std::make_shared<xerahs::uploads::MemoryCredentialStore>();
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
    xerahs::watch::WatchFolders watchStore(dir.filePath("watch"));
    FileUploadPorts uploadPorts; uploadPorts.catalogPath = dir.filePath("destinations.json");
    uploadPorts.credentials = credentials; uploadPorts.history = journal;
    FileUploadController uploads(uploadPorts);
    WatchFolderService watcher(watchStore, uploads, [&] { return dir.filePath("captures"); });
    xerahs::media::NoElements availability;
    auto registry = std::make_shared<xerahs::effects::EffectRegistry>(xerahs::effects::builtinEffects());
    QDialog *dialog = which == 0 ? static_cast<QDialog *>(new QrCodeDialog(&host.window)) : which == 1 ? static_cast<QDialog *>(new ColorPickerDialog(&host.window))
        : which == 2 ? static_cast<QDialog *>(new HashCheckerDialog(&host.window)) : which == 3 ? static_cast<QDialog *>(new IconConverterDialog(&host.window))
        : which == 4 ? static_cast<QDialog *>(new NewImageDialog(&host.window)) : which == 5 ? static_cast<QDialog *>(new UrlOpenDialog(&host.window))
        : which == 6 ? static_cast<QDialog *>(new ImageInsertDialog(&host.window)) : which == 7 ? static_cast<QDialog *>(new ImageRecognitionDialog(ImageRecognitionKind::QrCode, &host.window))
        : which == 8 ? static_cast<QDialog *>(new ImageRecognitionDialog(ImageRecognitionKind::Text, &host.window)) : which == 9 ? static_cast<QDialog *>(new ImageComparisonDialog(xerahs::editor::AnnotationDocument{}, comparisonImage, &host.window))
        : which == 10 ? static_cast<QDialog *>(new WatchFoldersDialog(watcher, &host.window))
        : which == 11 ? static_cast<QDialog *>(new VideoConverterDialog(new IdleRunner, &availability, &host.window))
        : which == 12 ? static_cast<QDialog *>(new EffectsDialog(sourceDocument, sourceImage, [](PreparedCanvasEdit) { return false; }, &host.window, dir.filePath("effects.json")))
        : which == 16 ? static_cast<QDialog *>(new RectanglePropertiesDialog(rectangle, &host.window))
        : which == 17 ? static_cast<QDialog *>(new TextContentDialog(textShape, &host.window))
        : which == 18 ? static_cast<QDialog *>(new CanvasCropDialog(xerahs::editor::AnnotationDocument{}, sourceImage, cropRegion, &host.window))
        : which == 19 ? static_cast<QDialog *>(new CanvasRotationDialog(xerahs::editor::AnnotationDocument{}, sourceImage, &host.window))
        : which == 20 ? static_cast<QDialog *>(new SelectionRotationDialog(rotationDocument, QSet<QUuid>{rotated.id}, &host.window))
        : static_cast<QDialog *>(new PresetDialog(sourceDocument, sourceImage, [](PreparedCanvasEdit) { return false; }, registry, &host.window));
    QVERIFY(!QApplication::activeModalWidget());  // No trap before it opens.
    dialog->setModal(true); dialog->open();
    QTRY_VERIFY(dialog->isVisible()); QCOMPARE(QApplication::activeModalWidget(), dialog);
    QVERIFY(!dialog->windowTitle().trimmed().isEmpty());  // Identifies its purpose.
    // A cancel/close action is exposed, and no default button carries a destructive label.
    bool cancelAction = false;
    for (auto *button : dialog->findChildren<QPushButton *>()) {
      if (!button->isVisibleTo(dialog)) continue;
      const auto label = button->text().remove('&');
      cancelAction |= label.contains("Close", Qt::CaseInsensitive) || label.contains("Cancel", Qt::CaseInsensitive);
      if (button->isDefault()) QVERIFY2(safeLabel(label), qPrintable(label));
    }
    for (auto *box : dialog->findChildren<QDialogButtonBox *>()) cancelAction |= box->button(QDialogButtonBox::Cancel) || box->button(QDialogButtonBox::Close);
    QVERIFY(cancelAction);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->isVisible()); QCOMPARE(dialog->result(), int(QDialog::Rejected));
    QVERIFY(!QApplication::activeModalWidget());  // Trap released on close.
    QTRY_VERIFY(host.focusOnEdit());  // Focus restored.
    delete dialog;
  }
};
QTEST_MAIN(ModalDialogsTest)
#include "ModalDialogsTest.moc"
