// EO-013 / ESET-014: drive the actual standalone close, picker and save path.
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"
#include "EditorShutdown.h"
#include "NativeEditorPreferences.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QColorSpace>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace xerahs::app;
using namespace xerahs::editor;

namespace {
QImage pixels() {
  QImage image(60, 40, QImage::Format_ARGB32); image.fill(Qt::blue);
  image.setColorSpace(QColorSpace::SRgb); return image;
}
// Latency budgets are not qualified under sanitizers or valgrind.
bool latencyQualifiable(QString *reason) {
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
  *reason = "Latency is not qualified under sanitizers."; return false;
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_UNDEFINED__)
  *reason = "Latency is not qualified under sanitizers."; return false;
#endif
  QFile mappings("/proc/self/maps");
  if (mappings.open(QIODevice::ReadOnly)) {
    const auto runtime = mappings.readAll();
    if (runtime.contains("libasan.") || runtime.contains("libubsan.") || runtime.contains("libtsan.") || runtime.contains("/valgrind/")) { *reason = "Latency is not qualified under valgrind/sanitizers."; return false; }
  }
  if (qEnvironmentVariableIsSet("ASAN_OPTIONS") || qEnvironmentVariableIsSet("TSAN_OPTIONS") || qEnvironmentVariableIsSet("UBSAN_OPTIONS") ||
      qEnvironmentVariableIsSet("VALGRIND_OPTS") || QFileInfo("/proc/self/exe").symLinkTarget().contains("valgrind")) { *reason = "Latency is not qualified under valgrind/sanitizers."; return false; }
  return true;
}
double percentile95(std::vector<double> values) {
  std::sort(values.begin(), values.end()); return values.at(static_cast<size_t>(0.95 * (values.size() - 1) + 0.5));
}
QByteArray bytes(const QString &path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class EditorExitTest : public QObject {
  Q_OBJECT
private slots:
  void fourKRectangleOpensToInteractiveWithin500ms() {
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    QSKIP("Opening latency is not qualified under sanitizers.");
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_UNDEFINED__)
    QSKIP("Opening latency is not qualified under sanitizers.");
#endif
    QFile mappings("/proc/self/maps");
    if (mappings.open(QIODevice::ReadOnly)) {
      const auto runtime = mappings.readAll();
      if (runtime.contains("libasan.") || runtime.contains("libubsan.") || runtime.contains("libtsan.") || runtime.contains("/valgrind/"))
        QSKIP("Opening latency is not qualified under valgrind/sanitizers.");
    }
    if (qEnvironmentVariableIsSet("ASAN_OPTIONS") || qEnvironmentVariableIsSet("TSAN_OPTIONS") ||
        qEnvironmentVariableIsSet("UBSAN_OPTIONS") || qEnvironmentVariableIsSet("VALGRIND_OPTS") ||
        QFileInfo("/proc/self/exe").symLinkTarget().contains("valgrind"))
      QSKIP("Opening latency is not qualified under valgrind/sanitizers.");
    QImage image(3840, 2160, QImage::Format_ARGB32); image.fill(Qt::blue); image.setColorSpace(QColorSpace::SRgb);
    auto source = prepareCapturedSource(image); QVERIFY(source.document);
    EditorSession session(*source.document); QVERIFY(session.createRectangle({10, 10}, {100, 100}));
    QTemporaryDir dir; const auto path = dir.filePath("4k.png");
    QVERIFY(saveEdit(path, image, session.document()).sidecarSaved);
    // Wall-clock under CPU contention from parallel CI is handled by taking the best of three opens; a
    // genuine regression exceeds the 500 ms budget in every attempt.
    qint64 elapsed = std::numeric_limits<qint64>::max();
    for (int attempt = 0; attempt < 3 && elapsed > 500; ++attempt) {
      QElapsedTimer timer; timer.start();
      std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
      window->setAttribute(Qt::WA_DeleteOnClose, false); window->resize(900, 600); window->show();
      QApplication::processEvents();
      auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas && canvas->isEnabled());
      const auto &document = canvas->annotationDocument(); QCOMPARE(document.annotations.size(), size_t(1));
      const auto id = std::get<RectangleAnnotation>(document.annotations.front()).id;
      QVERIFY(canvas->selectAnnotations({id}, canvas->annotationState(), canvas->documentGeneration()));
      const auto state = canvas->annotationState(); canvas->setFocus(); QTest::keyClick(canvas, Qt::Key_Right);
      QVERIFY(canvas->annotationState() != state); // Actual keyboard mutation proves interactive readiness.
      elapsed = std::min(elapsed, timer.elapsed());
      qInfo("4K source with rectangle reached interactive state in %lld ms (attempt %d)", static_cast<long long>(timer.elapsed()), attempt + 1);
    }
    QVERIFY2(elapsed <= 500, qPrintable(QString("Interactive opening took %1 ms (limit 500 ms)").arg(elapsed)));
  }
  // ES-021: 60 Hz manipulation previews on a 3840x2160 source. Each update is a real pointer move delivered
  // to the real canvas followed by the real synchronous viewport repaint (the preview frame). The budget is
  // one 60 Hz frame (16.7 ms) at the 95th percentile of 120 updates per gesture: a median would hide the
  // dropped frames that make a preview feel janky. Wall-clock under CPU contention from parallel CI is
  // handled by taking the best of three attempts per gesture; a genuine regression exceeds the budget
  // in every attempt.
  void fourKRectangleManipulationPreviewsHold60Hz() {
    QString reason; if (!latencyQualifiable(&reason)) QSKIP(qPrintable(reason));
    QImage image(3840, 2160, QImage::Format_ARGB32); image.fill(Qt::blue); image.setColorSpace(QColorSpace::SRgb);
    auto source = prepareCapturedSource(image); QVERIFY(source.document);
    EditorSession session(*source.document); QVERIFY(session.createRectangle({400, 400}, {1400, 1200}));
    QTemporaryDir dir; const auto path = dir.filePath("4k.png"); QVERIFY(saveEdit(path, image, session.document()).sidecarSaved);
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->resize(900, 600); window->show(); QApplication::processEvents();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas && canvas->isEnabled());
    const auto id = std::get<RectangleAnnotation>(canvas->annotationDocument().annotations.front()).id;
    QVERIFY(canvas->selectAnnotations({id}, canvas->annotationState(), canvas->documentGeneration()));
    QWidget *viewport = canvas->viewport(); QCoreApplication::processEvents(); viewport->repaint();
    constexpr int updates = 120; constexpr double budgetMs = 1000.0 / 60.0;
    enum class Gesture { Move, Resize, Create };
    const auto run = [&](Gesture gesture) {
      std::vector<double> best; double bestP95 = 1e9;
      for (int attempt = 0; attempt < 3; ++attempt) {
        const auto toView = [&](QPointF p) { return canvas->viewState().toView(p).toPoint(); };
        QPoint from, delta;
        if (gesture == Gesture::Move) { from = toView({900, 800}); delta = {1, 1}; }
        else if (gesture == Gesture::Resize) { from = toView({1400, 1200}); delta = {1, 1}; }
        else { from = toView({1800, 300}); delta = {1, 1}; }
        QTest::mousePress(viewport, Qt::LeftButton, {}, from);
        const auto before = canvas->annotationState();
        std::vector<double> samples; samples.reserve(updates); QElapsedTimer timer;
        for (int i = 1; i <= updates; ++i) {
          const QPoint to = from + QPoint(delta.x() * i, delta.y() * i); timer.start();
          QTest::mouseMove(viewport, to); viewport->repaint();
          samples.push_back(timer.nsecsElapsed() / 1.0e6);
        }
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, from + QPoint(updates, updates));
        QApplication::processEvents();
        if (canvas->annotationState() == before) { QTest::qFail("gesture did not commit a real edit", __FILE__, __LINE__); return std::make_pair(1e9, 1e9); }
        const auto p95 = percentile95(samples); if (p95 < bestP95) { bestP95 = p95; best = samples; }
        if (p95 <= budgetMs) break;
      }
      return std::make_pair(bestP95, *std::max_element(best.begin(), best.end()));
    };
    const std::pair<const char *, Gesture> gestures[] = {{"move", Gesture::Move}, {"resize", Gesture::Resize}, {"create", Gesture::Create}};
    for (const auto &[name, gesture] : gestures) {
      const auto [p95, worst] = run(gesture);
      qInfo("4K %s preview: p95 %.2f ms, worst %.2f ms (budget %.1f ms)", name, p95, worst, budgetMs);
      QVERIFY2(p95 <= budgetMs, qPrintable(QString("%1 preview p95 %2 ms exceeds the 60 Hz budget %3 ms").arg(name).arg(p95).arg(budgetMs)));
    }
  }
  void sessionManagerRetainsDirtyDocumentWithoutInteraction() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(pixels())); QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    const auto before = serializeDocument(canvas->annotationDocument());
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished);
    QCOMPARE(commitEditorsForSession(false), QString("editor-session-interaction-required"));
    QVERIFY(window->isVisible()); QVERIFY(window->windowTitle().contains(QChar(0x2022)));
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(saved.count(), 0);
    QVERIFY(!QApplication::activeModalWidget());
    QTimer::singleShot(0, [] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Cancel)->click();
    });
    QCOMPARE(commitEditorsForSession(true), QString("editor-close-cancelled"));
    QVERIFY(window->isVisible()); QCOMPARE(saved.count(), 0);
    QTimer::singleShot(0, [] {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Discard)->click();
    });
    QVERIFY(commitEditorsForSession(true).isEmpty()); QVERIFY(!window->isVisible());
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QCOMPARE(saved.count(), 0);
  }
  void init() {
    initializeNativeEditorPreferences();
    QVERIFY(applyConveniencePreferences({{"ShowExitConfirmation", false}}, nativeEditorPreferenceRevision()).saved);
  }
  void destroyingEditorDuringCloseConfirmationDoesNotSaveOrCrash_data() {
    QTest::addColumn<bool>("dirty"); QTest::newRow("dirty") << true; QTest::newRow("clean") << false;
  }
  void destroyingEditorDuringCloseConfirmationDoesNotSaveOrCrash() {
    QFETCH(bool, dirty); QTemporaryDir dir; const auto path = dir.filePath("source.png"); QVERIFY(pixels().save(path));
    const auto original = bytes(path);
    QPointer<EditorWindow> window = dirty ? EditorWindow::fromCapturedImage(pixels()) : EditorWindow::open(path);
    QVERIFY(window); window->show();
    QVERIFY(applyConveniencePreferences({{"ShowExitConfirmation", !dirty}}, nativeEditorPreferenceRevision()).saved);
    QSignalSpy saved(window, &EditorWindow::saveFinished); QPointer<QMessageBox> confirmation;
    bool destroyed = false; QTimer driver;
    connect(&driver, &QTimer::timeout, this, [&] {
      confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!confirmation) return;
      driver.stop(); QCOMPARE(confirmation->windowTitle(), dirty ? QString("Unsaved changes") : QString("Close editor"));
      delete window.data(); destroyed = true;
    });
    driver.start(2); window->close(); driver.stop();
    QVERIFY(destroyed); QVERIFY(window.isNull()); QVERIFY(confirmation.isNull()); QCOMPARE(saved.size(), 0);
    QCOMPARE(bytes(path), original); QVERIFY(!QFile::exists(path + ".xann"));
  }
  void saveAsHostReentryCannotOpenAnotherChooser() {
    QPointer<EditorWindow> window = EditorWindow::fromCapturedImage(pixels()); QVERIFY(window); window->show();
    auto *saveAs = window->findChild<QAction *>("saveAsImage"); QVERIFY(saveAs);
    auto *canvas = window->findChild<EditorCanvas *>(); const auto before = serializeDocument(canvas->annotationDocument());
    QSignalSpy saved(window, &EditorWindow::saveFinished);
    bool entered = false, nestedOpened = false; QTimer driver, nested;
    nested.setSingleShot(true);
    connect(&nested, &QTimer::timeout, this, [&] {
      int visible = 0; for (auto *widget : QApplication::topLevelWidgets()) if (qobject_cast<QFileDialog *>(widget) && widget->isVisible()) ++visible;
      nestedOpened = visible > 1;
      if (auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) picker->reject();
    });
    connect(&driver, &QTimer::timeout, this, [&] {
      auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); if (!picker || entered) return;
      entered = true; nested.start(1); saveAs->trigger(); nested.stop(); picker->reject();
    });
    driver.start(2); saveAs->trigger(); driver.stop();
    QVERIFY(entered); QVERIFY(!nestedOpened); QCOMPARE(saved.size(), 0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QVERIFY(window->isVisible()); delete window.data();
  }
  void throwingSaveCompletionObserverDoesNotEscapeOrRepeatAcknowledgedOutput() {
    QTemporaryDir dir; const auto path = dir.filePath("image.png"); QVERIFY(pixels().save(path));
    std::unique_ptr<EditorWindow> window(EditorWindow::open(path)); QVERIFY(window); window->show();
    QSignalSpy saved(window.get(), &EditorWindow::saveFinished);
    connect(window.get(), &EditorWindow::saveFinished, this, [] { throw std::runtime_error("PRIVATE host payload"); });
    bool escaped = false;
    try { window->findChild<QAction *>("saveImage")->trigger(); } catch (...) { escaped = true; }
    QVERIFY(!escaped); QCOMPARE(saved.size(), 1); QVERIFY(saved.first()[0].toBool() && saved.first()[1].toBool());
    QCOMPARE(QImage(path), pixels()); QVERIFY(!window->windowTitle().contains(QChar(0x2022)));
    auto *retry = window->findChild<QAction *>("retryEditorSidecar"); QVERIFY(retry); QVERIFY(!retry->isEnabled());
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
