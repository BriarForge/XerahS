// EO-013 / ESET-014: drive the actual standalone close, picker and save path.
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"
#include "NativeEditorPreferences.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QColorSpace>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
QImage pixels() {
  QImage image(60, 40, QImage::Format_ARGB32); image.fill(Qt::blue);
  image.setColorSpace(QColorSpace::SRgb); return image;
}
QByteArray bytes(const QString &path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class EditorExitTest : public QObject {
  Q_OBJECT
private slots:
  void init() {
    initializeNativeEditorPreferences();
    QVERIFY(applyConveniencePreferences({{"ShowExitConfirmation", false}}, nativeEditorPreferenceRevision()).saved);
  }
  void dirtyExitMatrix_data() {
    QTest::addColumn<QString>("mode");
    for (const auto *mode : {"save-success", "save-encode-failure", "save-empty-callback-success", "save-wrong-content-success", "discard", "cancel"})
      QTest::newRow(mode) << QString::fromLatin1(mode);
  }
  void dirtyExitMatrix() {
    QFETCH(QString, mode);
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(pixels().save(path));
    const auto original = bytes(path);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->resize(900, 600); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas); canvas->resetZoom();
    auto *rectangle = window->findChild<QAction *>("rectangleTool"); QVERIFY(rectangle); rectangle->trigger();
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({8, 8}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, {}, canvas->viewState().toView({22, 16}).toPoint());
    QCOMPARE(canvas->annotationDocument().annotations.size(), size_t(1));
    QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    const auto document = serializeDocument(canvas->annotationDocument());
    if (mode == "save-encode-failure" || mode == "save-empty-callback-success" || mode == "save-wrong-content-success") {
      EditorSaveHooks hooks;
      hooks.encode = [mode](const QImage &image, QIODevice *device) {
        if (mode == "save-wrong-content-success") { QImage wrong(image.size(), image.format()); wrong.fill(Qt::green); return wrong.save(device, "PNG"); }
        return mode == "save-empty-callback-success";
      };
      window->setSaveHooks(std::move(hooks));
    }
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished);
    int dirtyPrompts = 0, failurePrompts = 0; bool unexpectedPrompt = false;
    const auto choice = mode == "discard" ? QMessageBox::Discard : mode == "cancel" ? QMessageBox::Cancel : QMessageBox::Save;
    QTimer driver;
    connect(&driver, &QTimer::timeout, window.get(), [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      if (box->windowTitle() == "Unsaved changes") {
        ++dirtyPrompts;
        if (box->standardButtons() != (QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel)) unexpectedPrompt = true;
        box->button(choice)->click();
      } else if (box->windowTitle() == "Save incomplete") {
        ++failurePrompts; box->accept();
      } else { unexpectedPrompt = true; box->reject(); }
    });
    driver.start(5); const bool closed = window->close(); driver.stop();
    const bool completeSave = mode == "save-success";
    const bool failedSave = mode.startsWith("save-") && !completeSave;
    QCOMPARE(closed, completeSave || mode == "discard"); QCOMPARE(window->isVisible(), !closed);
    QCOMPARE(dirtyPrompts, 1); QCOMPARE(failurePrompts, failedSave ? 1 : 0); QVERIFY(!unexpectedPrompt);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), document);
    if (completeSave) {
      QCOMPARE(saved.count(), 1); QVERIFY(saved.at(0).at(0).toBool()); QVERIFY(saved.at(0).at(1).toBool());
      QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
      const auto reopened = loadSource(path); QVERIFY(reopened.document);
      QCOMPARE(reopened.document->annotations.size(), size_t(1)); QVERIFY(QFile::exists(path + ".xann"));
      QVERIFY(QImage(path) != pixels());
    } else {
      QCOMPARE(bytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
      QVERIFY(window->windowTitle().contains(QChar(0x2022)));
      QCOMPARE(saved.count(), failedSave ? 1 : 0);
      if (failedSave) {
        QVERIFY(!saved.at(0).at(0).toBool()); QVERIFY(!saved.at(0).at(1).toBool());
        // Turning optional confirmation off must still protect the dirty session
        // after a failed callback. A second close offers all three choices again.
        bool promptedAgain = false;
        QTimer::singleShot(0, [&] {
          auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
          if (!box) return;
          promptedAgain = box->windowTitle() == "Unsaved changes" && box->button(QMessageBox::Save) && box->button(QMessageBox::Discard);
          box->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!window->close()); QVERIFY(promptedAgain); QVERIFY(window->isVisible());
      }
    }
  }
  void cancelledSavePickerKeepsUntitledEditorOpen() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(pixels())); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    const auto before = serializeDocument(canvas->annotationDocument());
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished);
    bool dirtyPrompt = false, pickerCancelled = false; QTimer driver;
    connect(&driver, &QTimer::timeout, window.get(), [&] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        dirtyPrompt = box->windowTitle() == "Unsaved changes";
        box->button(QMessageBox::Save)->click();
      } else if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
        pickerCancelled = true; picker->reject();
      }
    });
    driver.start(5); QVERIFY(!window->close()); driver.stop();
    QVERIFY(dirtyPrompt); QVERIFY(pickerCancelled); QVERIFY(window->isVisible()); QCOMPARE(saved.count(), 0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before);
    QVERIFY(window->windowTitle().contains(QChar(0x2022)));
  }
  void applicationQuitUsesStandaloneDirtyClosePolicy() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(pixels())); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->show();
    bool discard = false; int prompts = 0; QTimer driver;
    connect(&driver, &QTimer::timeout, [&] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        ++prompts; box->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
      }
    });
    driver.start(5); QEvent quit(QEvent::Quit); QApplication::sendEvent(qApp, &quit);
    QVERIFY(!quit.isAccepted()); QVERIFY(window->isVisible());
    discard = true; QObject sink; QEvent accepted(QEvent::Quit); QApplication::sendEvent(&sink, &accepted); driver.stop();
    QCOMPARE(prompts, 2); QVERIFY(!window->isVisible());
  }
  void optionalCleanExitConfirmation_data() {
    QTest::addColumn<bool>("confirm"); QTest::addColumn<bool>("close");
    QTest::newRow("disabled") << false << true;
    QTest::newRow("enabled-cancel") << true << false;
    QTest::newRow("enabled-close") << true << true;
  }
  void optionalCleanExitConfirmation() {
    QFETCH(bool, confirm); QFETCH(bool, close);
    QVERIFY(applyConveniencePreferences({{"ShowExitConfirmation", confirm}}, nativeEditorPreferenceRevision()).saved);
    QTemporaryDir dir; const auto path = dir.filePath("clean.png"); QVERIFY(pixels().save(path));
    const auto original = bytes(path);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->show();
    QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    bool prompted = false; QTimer driver;
    connect(&driver, &QTimer::timeout, window.get(), [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      prompted = box->windowTitle() == "Close editor";
      box->button(close ? QMessageBox::Close : QMessageBox::Cancel)->click();
    });
    driver.start(5); QCOMPARE(window->close(), close); driver.stop(); QCOMPARE(prompted, confirm);
    QCOMPARE(window->isVisible(), !close); QCOMPARE(bytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
  }
};
QTEST_MAIN(EditorExitTest)
#include "EditorExitTest.moc"
