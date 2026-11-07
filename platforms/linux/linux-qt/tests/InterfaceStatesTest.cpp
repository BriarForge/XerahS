// IFCAT-003 state matrix for the main native surfaces: the shell window and the
// QR, hash, icon, colour, new-image, URL, recognition, comparison, video-converter
// and watch-folders dialogs. Every state is reached through the real widget and
// must be visible as text (never colour alone) with an accessible cue.
// States that a surface cannot be in are declared "n/a" with the reason.
#include "AccessibleCue.h"
#include "ActionSelectionDialog.h"
#include "AppearanceDialog.h"
#include "AutoCaptureDialog.h"
#include "ApplicationWindow.h"
#include "CaptureHistoryWindow.h"
#include "ColorPickerDialog.h"
#include "CommandRegistry.h"
#include "DrawingDefaultsDialog.h"
#include "EditorSource.h"
#include "EffectsDialog.h"
#include "FileUploadController.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageComparisonDialog.h"
#include "ImageInsertDialog.h"
#include "ImageRecognitionDialog.h"
#include "NativePipelineJournal.h"
#include "NewImageDialog.h"
#include "OnboardingWizard.h"
#include "PipelineHistoryDialog.h"
#include "PresetDialog.h"
#include "QrCodeDialog.h"
#include "RectanglePropertiesDialog.h"
#include "TextContentDialog.h"
#include "UrlOpenDialog.h"
#include "VideoConverterDialog.h"
#include "WatchFolderService.h"
#include "WorkflowExecutionHistoryDialog.h"
#include "WatchFoldersDialog.h"
#include "actions/PipelineJournal.h"
#include "features/ToolsFeature.h"
#include "image-editor/AnnotationDocument.h"
#include "media/VideoConversion.h"
#include "settings/ApplicationSettings.h"
#include "uploads/CredentialStore.h"
#include "watch/WatchFolders.h"
#include <QAccessible>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QToolButton>
#include <atomic>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace xerahs::app;

namespace {
bool visibleText(const QLabel *label) { return label && !label->isHidden() && !label->text().trimmed().isEmpty(); }
QAccessible::Event lastAlert = QAccessible::InvalidEvent;
void record(QAccessibleEvent *event) { if (event->type() == QAccessible::Alert) lastAlert = QAccessible::Alert; }

// The preparation worker blocks here until the test releases it, so the
// loading and cancelled states are reached deterministically.
struct WorkGate { std::atomic<bool> entered{false}, released{false}; };
NewImageHooks gatedHooks(const std::shared_ptr<WorkGate> &gate, bool fail) {
  return {[gate, fail] {
    gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (fail) throw std::runtime_error("private preparation path");
  }};
}

// A network stub: the URL dialog is driven against it, so no request ever
// reaches a real server.
class StubReply final : public QNetworkReply {
public:
  enum class Outcome { Pending, Failed, Success };
  StubReply(const QNetworkRequest &request, Outcome outcome, const QByteArray &payload, QObject *parent)
      : QNetworkReply(parent), m_payload(payload) {
    setRequest(request); setUrl(request.url()); setOperation(QNetworkAccessManager::GetOperation);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
    setAttribute(QNetworkRequest::HttpReasonPhraseAttribute, QStringLiteral("OK"));
    if (!isOpen()) open(ReadOnly | Unbuffered);
    if (outcome == Outcome::Failed) {
      setError(QNetworkReply::ConnectionRefusedError, QStringLiteral("refused"));
      setFinished(true);
      QTimer::singleShot(0, this, [this] { emit finished(); });
    } else if (outcome == Outcome::Success) {
      QTimer::singleShot(0, this, [this] {
        emit readyRead(); emit downloadProgress(m_payload.size(), m_payload.size());
        setFinished(true); emit finished();
      });
    }
  }
  void abort() override {
    if (m_finished) return;
    m_finished = true;
    setError(QNetworkReply::OperationCanceledError, QStringLiteral("cancelled"));
    setFinished(true);
    emit finished();
  }
  qint64 bytesAvailable() const override { return m_payload.size() + QIODevice::bytesAvailable(); }
  bool isSequential() const override { return true; }
  bool atEnd() const override { return m_payload.isEmpty(); }
protected:
  qint64 readData(char *data, qint64 maxSize) override {
    const qint64 count = qMin<qint64>(maxSize, m_payload.size());
    if (count <= 0) return 0;
    std::memcpy(data, m_payload.constData(), size_t(count));
    m_payload.remove(0, int(count));
    return count;
  }
private:
  QByteArray m_payload;
  bool m_finished = false;
};

class StubNetwork final : public QNetworkAccessManager {
public:
  StubReply::Outcome outcome = StubReply::Outcome::Pending;
  QByteArray payload;
  int requests = 0;
protected:
  QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
    ++requests;
    return new StubReply(request, outcome, payload, this);
  }
};

QByteArray pngBytes() {
  QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::blue);
  QBuffer buffer; buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return buffer.data();
}

// The comparison worker blocks here until the test releases it, so the loading
// and cancelled states are reached deterministically. Only the first load waits.
struct ComparisonGate { std::atomic<int> calls{0}; std::atomic<bool> entered{false}, released{false}; };
ComparisonHooks gatedComparisonHooks(const std::shared_ptr<ComparisonGate> &gate) {
  return {[gate] {
    if (++gate->calls != 1) return;
    gate->entered.store(true);
    while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }};
}

// A conversion runner that never reaches GStreamer: the dialog's states are
// driven entirely by the injected runner.
class FakeConversionRunner final : public xerahs::media::ConversionRunner {
public:
  void start(const xerahs::media::Request &request) override { last = request; ++starts; running = true; }
  void cancel() override {
    if (!running) return;
    ++cancels; running = false;
    xerahs::media::ConversionResult result; result.outcome = xerahs::media::Outcome::Cancelled;
    emit finished(result);
  }
  bool busy() const override { return running; }
  void complete(const xerahs::media::ConversionResult &result) { running = false; emit finished(result); }
  xerahs::media::Request last; int starts = 0, cancels = 0; bool running = false;
};

// The onboarding pages name their widgets through accessible names, not object
// names, so the pages are searched by the name the wizard gives them.
QLabel *labelNamed(QWidget *page, const QString &name) {
  for (auto *label : page->findChildren<QLabel *>()) if (label->accessibleName() == name) return label;
  return nullptr;
}
QLineEdit *editNamed(QWidget *page, const QString &name) {
  for (auto *edit : page->findChildren<QLineEdit *>()) if (edit->accessibleName() == name) return edit;
  return nullptr;
}
QPushButton *buttonNamed(QWidget *page, const QString &name) {
  for (auto *button : page->findChildren<QPushButton *>()) if (button->accessibleName() == name) return button;
  return nullptr;
}
// The hotkey list of the wizard page, which carries no object name of its own.
QTableWidget *tableOn(QWidget *page) {
  for (auto *child : page->children()) if (auto *table = qobject_cast<QTableWidget *>(child)) return table;
  return nullptr;
}
// The text a page shows right now: the labels that are on screen, not hidden.
QString visiblePageText(QWidget *page) {
  QStringList parts;
  for (const auto *label : page->findChildren<QLabel *>())
    if (!label->isHidden() && !label->text().trimmed().isEmpty()) parts << label->text();
  return parts.join(QLatin1Char('\n'));
}
}

class InterfaceStatesTest final : public QObject {
  Q_OBJECT
  QTemporaryDir dir;
  // The preference dialogs have no public constructor: their entry point opens the dialog and runs
  // its own modal loop, so a probe reaches the dialog once it is up and drives it from there.
  struct PreferenceStates {
    bool entered = false, cancelled = false;
    QString title, text, error;
    QHash<QString, QString> fields;
  };
  template <typename Open, typename Drive>
  PreferenceStates probePreferenceDialog(Open open, Drive drive) {
    PreferenceStates states;
    int stage = 0;
    QTimer probe; probe.setInterval(5);
    connect(&probe, &QTimer::timeout, &probe, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!dialog || stage != 0) return;
      stage = 1;
      states.entered = true;
      states.title = dialog->windowTitle();
      QStringList labels;
      for (auto *label : dialog->findChildren<QLabel *>()) {
        if (!label->objectName().isEmpty()) states.fields[label->objectName()] = label->text();
        if (!label->text().trimmed().isEmpty()) labels << label->text();
      }
      for (auto *edit : dialog->findChildren<QLineEdit *>()) if (!edit->objectName().isEmpty()) states.fields[edit->objectName()] = edit->text();
      for (auto *button : dialog->findChildren<QPushButton *>()) if (!button->objectName().isEmpty()) states.fields[button->objectName()] = button->text();
      for (auto *combo : dialog->findChildren<QComboBox *>()) if (!combo->objectName().isEmpty()) states.fields[combo->objectName()] = combo->currentText();
      for (auto *box : dialog->findChildren<QCheckBox *>()) if (!box->objectName().isEmpty()) states.fields[box->objectName()] = box->text();
      states.text = labels.join(QStringLiteral(" | "));
      states.error = drive(dialog);
      for (auto *buttons : dialog->findChildren<QDialogButtonBox *>())
        if (auto *cancel = buttons->button(QDialogButtonBox::Cancel)) { cancel->click(); states.cancelled = !dialog->isVisible(); break; }
    });
    probe.start();
    open();
    probe.stop();
    return states;
  }
private slots:
  // Qt drops accessibility updates while no assistive client is active (always, on Qt 6.4 offscreen), so observe
  // the cue the production code raises as well as anything Qt delivers.
  void initTestCase() {
    QVERIFY(dir.isValid()); QAccessible::installUpdateHandler(record);
    accessibleCueObserver() = [](QObject *, QAccessible::Event type) { if (type == QAccessible::Alert) lastAlert = QAccessible::Alert; };
  }

  // Auto capture: initial, populated, validation, running, error.
  // empty: n/a because the dialog always opens with its default source, interval and count.
  // loading: n/a because the dialog has no background work of its own.
  // cancelled: n/a because cancelling closes the dialog without a message.
  // denied: n/a because the dialog asks for no permission.
  // unavailable: n/a because the dialog has no optional dependency.
  void autoCaptureDialogStates() {
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    {
      // Without a preferences port the dialog has no bound yet, and says so.
      AutoCaptureDialog dialog([now] { return now; }, false);
      dialog.show();
      auto *status = dialog.findChild<QLabel *>("autoCaptureStatus");
      auto *source = dialog.findChild<QComboBox *>("autoCaptureSource");
      auto *interval = dialog.findChild<QSpinBox *>("autoCaptureInterval");
      auto *start = dialog.findChild<QPushButton *>("autoCaptureStart");
      auto *countBound = dialog.findChild<QRadioButton *>("autoCaptureCountBound");
      auto *timeBound = dialog.findChild<QRadioButton *>("autoCaptureTimeBound");
      auto *end = dialog.findChild<QDateTimeEdit *>("autoCaptureEnd");
      QVERIFY(status && source && interval && start && countBound && timeBound && end);
      QVERIFY2(!dialog.windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                              // initial
      QVERIFY2(source->currentText() == "Full screen", qPrintable(source->currentText()));                             // initial: the default source is shown
      QCOMPARE(interval->value(), 5);                                                                                // initial
      QVERIFY2(status->text().contains("Choose a bound"), qPrintable(status->text()));                               // validation: the missing bound is named
      QVERIFY(!start->isEnabled());
      countBound->setChecked(true);
      QVERIFY2(status->text().contains("Ready"), qPrintable(status->text()));                                         // populated: a complete request is ready
      QVERIFY(start->isEnabled());
      timeBound->setChecked(true);
      end->setDateTime(QDateTime::fromMSecsSinceEpoch(now - 3600000));
      QVERIFY2(status->text().contains("End time must be after"), qPrintable(status->text()));                        // validation: the end time in the past is refused
      QVERIFY(!start->isEnabled());
    }
    {
      // A store that refuses the write reports it and still lets the capture start.
      xerahs::settings::CapturePreferencesPort preferences;
      preferences.values = [] { return QJsonObject{}; };
      preferences.save = [](const QJsonObject &) { return QStringLiteral("settings-write-failed"); };
      AutoCaptureDialog dialog([now] { return now; }, false, preferences);
      dialog.show();
      auto *status = dialog.findChild<QLabel *>("autoCaptureStatus");
      QVERIFY(status);
      QVERIFY2(status->text().contains("Ready"), qPrintable(status->text()));                                         // populated
      dialog.findChild<QPushButton *>("autoCaptureStart")->click();
      QVERIFY2(status->text().contains("Could not remember this choice"), qPrintable(status->text()));                 // error: the refused write is named
      QVERIFY2(status->text().contains("settings-write-failed"), qPrintable(status->text()));
    }
    {
      AutoCaptureDialog running([now] { return now; }, true);
      running.show();
      auto *status = running.findChild<QLabel *>("autoCaptureStatus");
      auto *source = running.findChild<QComboBox *>("autoCaptureSource");
      QVERIFY(status && source);
      QVERIFY2(status->text().contains("Automatic capture is running"), qPrintable(status->text()));                    // running
      QVERIFY(!source->isEnabled());
    }
  }

  // Action selection: initial, populated, unavailable, validation.
  // empty: n/a because the dialog always lists the whole catalogue of actions.
  // loading: n/a because the dialog has no background work of its own.
  // error: n/a because the only refusal it can produce is the selection validation, which it reports in the same label.
  // cancelled: n/a because cancelling closes the dialog without a message.
  // denied: n/a because the dialog asks for no permission.
  void actionSelectionDialogStates() {
    // "image-effects" is a catalogue action this build does not implement, so it is listed but cannot run.
    ActionSelectionDialog dialog({"save", "image-effects"}, QSize(640, 480));
    dialog.show();
    auto *error = dialog.findChild<QLabel *>("captureSelectionError");
    auto *save = dialog.findChild<QCheckBox *>("capture-action-save");
    auto *effects = dialog.findChild<QCheckBox *>("capture-action-image-effects");
    QVERIFY(error && save && effects);
    QVERIFY2(!dialog.windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                                // initial
    bool introVisible = false;
    for (const auto *label : dialog.findChildren<QLabel *>())
      if (!label->isHidden() && !label->text().trimmed().isEmpty()) { introVisible = true; break; }
    QVERIFY2(introVisible, "the dialog opens with visible prose");                                                    // initial
    QVERIFY2(save->isChecked(), "the configured action is already chosen");                                           // populated
    QVERIFY2(effects->text().contains("unavailable"), qPrintable(effects->text()));                                    // unavailable: the row says so
    QVERIFY2(!effects->isEnabled(), "an unavailable action cannot be chosen");                                         // unavailable
    QVERIFY2(effects->accessibleDescription().contains("unavailable"), "the accessible cue names the unavailable action");  // unavailable
    dialog.findChild<QPushButton *>("runCaptureActions")->click();
    QVERIFY2(error->text().contains("cannot run"), qPrintable(error->text()));                                         // validation: the selection is refused in words
    QVERIFY2(error->text().contains("selection-action-unavailable"), qPrintable(error->text()));
    QVERIFY2(dialog.isVisible(), "the dialog stays open so the choice can be corrected");                              // validation
  }

  // Onboarding wizard: first page, initial, loading, populated, validation, error,
  // denied, unavailable, finish-later, decline-everything.
  // empty: n/a because the guide always opens on the welcome page with its explanation.
  // cancelled: n/a because the guide hides the cancel button; stopping early is the
  // Finish later state below.
  void onboardingWizardStates() {
    OnboardingPorts ports;
    ports.settings = [folder = dir.path()] {
      return QJsonObject{{"saveFolder", folder}, {"filenamePattern", QStringLiteral("%y%mo%dT%h%mi_%ra{10}")}};
    };
    ports.saveSettings = [](const QJsonObject &) { return QString(); };
    // Declining everything leaves no workflow at all: nothing is saved, copied or uploaded.
    ports.defaultWorkflow = [] { return OnboardingWorkflowInfo{}; };

    // The first page: the guide opens on the welcome page and explains itself.
    {
      OnboardingWizard wizard(ports, OnboardingState{});
      wizard.show();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("welcome"));                                           // first page
      QVERIFY2(wizard.windowTitle().contains("XerahS"), qPrintable(wizard.windowTitle()));                     // initial
      QVERIFY2(wizard.accessibleDescription().contains("finish later"), qPrintable(wizard.accessibleDescription()));
      auto *welcome = wizard.page(0);
      QVERIFY(welcome);
      QCOMPARE(welcome->title(), QStringLiteral("Welcome to XerahS"));                                       // first page
      const auto prose = visiblePageText(welcome);
      QVERIFY2(prose.contains("XerahS already works as it is"), qPrintable(prose));                             // first page
    }

    // The permission page: with no capture port the test capture is unavailable; with a
    // stub port the pending capture and a denial are both named.
    {
      OnboardingWizard wizard(ports, OnboardingState{});
      wizard.show();
      wizard.next();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("capture-permission"));
      auto *permission = wizard.page(1);
      auto *status = labelNamed(permission, "Test capture result");
      auto *test = buttonNamed(permission, "Test capture now");
      QVERIFY(status && test);
      QCOMPARE(status->text(), QStringLiteral("No test capture has been run."));                              // initial
      test->click();
      QVERIFY2(status->text().contains("not available in this session"), qPrintable(status->text()));         // unavailable
    }
    {
      OnboardingPorts denied = ports;
      denied.testCapture = [] {};  // a stub: no capture is ever taken
      OnboardingWizard wizard(denied, OnboardingState{});
      wizard.show();
      wizard.next();
      auto *permission = wizard.page(1);
      auto *status = labelNamed(permission, "Test capture result");
      auto *test = buttonNamed(permission, "Test capture now");
      QVERIFY(status && test);
      test->click();
      QVERIFY2(status->text().contains("Capturing the full screen"), qPrintable(status->text()));              // loading
      wizard.reportCaptureResult(false, "The desktop denied the screen capture.");
      QVERIFY2(status->text().contains("Capture did not succeed"), qPrintable(status->text()));                // error
      QVERIFY2(status->text().contains("The desktop denied the screen capture"), qPrintable(status->text()));  // denied
    }

    // The save folder page: a malformed filename pattern is refused in words and the
    // page stays open.
    {
      OnboardingWizard wizard(ports, OnboardingState{});
      wizard.show();
      wizard.next();
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("save-location"));
      auto *page = wizard.page(2);
      auto *folder = editNamed(page, "Default save folder");
      auto *pattern = editNamed(page, "Filename pattern");
      auto *problem = labelNamed(page, "Save location problem");
      auto *preview = labelNamed(page, "Filename preview");
      QVERIFY(folder && pattern && problem && preview);
      QCOMPARE(folder->text(), dir.path());                                                                 // populated
      pattern->setText("%ra{10");  // an unterminated token
      QVERIFY2(preview->text().contains("Pattern problem"), qPrintable(preview->text()));                       // validation
      wizard.next();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("save-location"));                                   // validation: the page stays
      QVERIFY2(problem->text().contains("The filename pattern is not valid"), qPrintable(problem->text()));   // validation
    }

    // The same page when the store refuses the write: the refusal is shown and the
    // page stays open.
    {
      OnboardingPorts refused = ports;
      refused.saveSettings = [](const QJsonObject &) { return QStringLiteral("settings-write-failed"); };
      OnboardingWizard wizard(refused, OnboardingState{});
      wizard.show();
      wizard.next();
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("save-location"));
      auto *page = wizard.page(2);
      auto *pattern = editNamed(page, "Filename pattern");
      auto *problem = labelNamed(page, "Save location problem");
      QVERIFY(pattern && problem);
      pattern->setText("%y%mo%d");  // valid, but a change the store has to save
      wizard.next();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("save-location"));                                   // error: the page stays
      QVERIFY2(problem->text().contains("settings-write-failed"), qPrintable(problem->text()));               // error
    }

    // Finish later: the guide closes and keeps the place.
    {
      int persists = 0;
      OnboardingState last;
      OnboardingPorts later = ports;
      later.persist = [&](const OnboardingState &state) { ++persists; last = state; };
      OnboardingWizard wizard(later, OnboardingState{});
      wizard.show();
      auto *finish = wizard.button(QWizard::CustomButton2);
      QVERIFY(finish);
      QVERIFY2(finish->text().contains("Finish"), qPrintable(finish->text()));                               // finish-later
      QCOMPARE(finish->accessibleName(), QStringLiteral("Finish later"));                                    // finish-later
      QVERIFY2(finish->accessibleDescription().contains("keeps your place"), qPrintable(finish->accessibleDescription()));
      finish->click();
      QVERIFY(!wizard.isVisible());                                                                         // finish-later
      QCOMPARE(wizard.result(), int(QDialog::Rejected));                                                     // finish-later
      QVERIFY(persists > 0);                                                                                // finish-later: the place is kept
      QCOMPARE(last.page, QStringLiteral("welcome"));
    }

    // Declining everything: every optional step is skipped and the summary says so.
    {
      OnboardingWizard wizard(ports, OnboardingState{});
      wizard.show();
      wizard.next();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("capture-permission"));
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("save-location"));
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("hotkeys"));
      auto *table = tableOn(wizard.page(3));
      QVERIFY(table);
      QVERIFY(table->rowCount() > 0);                                                                       // populated
      QVERIFY(!table->item(0, 0)->text().trimmed().isEmpty());                                              // populated
      QVERIFY(!table->item(0, 1)->text().trimmed().isEmpty());                                              // populated
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("uploads"));
      wizard.skipCurrent();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("telemetry"));
      wizard.next();
      QCOMPARE(wizard.currentPageName(), QStringLiteral("summary"));
      auto *report = wizard.page(6)->findChild<QLabel *>();
      QVERIFY(report);
      const auto summary = report->text();
      QVERIFY2(summary.contains("Screen capture permission: skipped"), qPrintable(summary));                 // decline-everything
      QVERIFY2(summary.contains("Save folder and filename: skipped"), qPrintable(summary));                 // decline-everything
      QVERIFY2(summary.contains("Hotkeys: skipped"), qPrintable(summary));                                 // decline-everything
      QVERIFY2(summary.contains("Uploads: skipped"), qPrintable(summary));                                 // decline-everything
      QVERIFY2(summary.contains("Welcome: reviewed"), qPrintable(summary));                                // decline-everything
      QVERIFY2(summary.contains("Telemetry and privacy: reviewed"), qPrintable(summary));                  // decline-everything
      QVERIFY2(summary.contains("nothing is saved, copied or uploaded"), qPrintable(summary));              // decline-everything
      QVERIFY(wizard.state().skipped.contains("capture-permission"));                                       // decline-everything
      QVERIFY(wizard.state().skipped.contains("uploads"));                                                  // decline-everything
    }
  }

  // Rectangle properties: initial, populated, validation, error, cancelled.
  // empty: n/a because the dialog always opens with the object's own bounds and values.
  // loading: n/a because the dialog does no background work of its own.
  // denied: n/a because the dialog asks for no permission.
  // unavailable: n/a because the only optional part is the colour picker, and declining it
  // keeps the draft without any message to show.
  void rectanglePropertiesDialogStates() {
    xerahs::editor::RectangleAnnotation rectangle;
    rectangle.start = {0, 0}; rectangle.end = {120, 80};
    RectanglePropertiesDialog dialog(rectangle, nullptr);
    dialog.show();
    auto *status = dialog.findChild<QLabel *>("rectanglePropertiesStatus");
    auto *left = dialog.findChild<QLineEdit *>("rectangleLeft");
    auto *top = dialog.findChild<QLineEdit *>("rectangleTop");
    auto *width = dialog.findChild<QLineEdit *>("rectangleWidth");
    auto *height = dialog.findChild<QLineEdit *>("rectangleHeight");
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(status && left && top && width && height && buttons);
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    QVERIFY(ok && cancel);
    QVERIFY2(!dialog.windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                     // initial
    QVERIFY2(status->text().trimmed().isEmpty(), qPrintable(status->text()));                                // initial: nothing is complained about yet
    QCOMPARE(left->text(), QStringLiteral("0"));                                                            // populated
    QCOMPARE(top->text(), QStringLiteral("0"));                                                             // populated
    QCOMPARE(width->text(), QStringLiteral("120"));                                                          // populated
    QCOMPARE(height->text(), QStringLiteral("80"));                                                          // populated
    QVERIFY2(cancel->text().contains("Cancel"), qPrintable(cancel->text()));                                 // cancelled: the way out is named
    width->setText("not a number");
    ok->click();
    QVERIFY2(status->text().contains("Enter finite numbers using a decimal point"), qPrintable(status->text()));  // validation
    QVERIFY(dialog.isVisible());                                                                             // validation: the dialog stays open
    QVERIFY(!dialog.properties());                                                                           // validation: nothing is committed
    width->setText("-5");
    ok->click();
    QVERIFY2(status->text().contains("Use positive finite width and height"), qPrintable(status->text()));    // error
    QVERIFY(dialog.isVisible());                                                                             // error: the dialog stays open
    QVERIFY(!dialog.properties());                                                                           // error: nothing is committed
    cancel->click();
    QVERIFY(!dialog.isVisible());                                                                           // cancelled
    QVERIFY(!dialog.properties());                                                                           // cancelled: the document stays unchanged
  }

  // Text content: initial, empty, populated, validation, error, unavailable, cancelled.
  // loading: n/a because the dialog does no background work of its own.
  // denied: n/a because the dialog asks for no permission.
  void textContentDialogStates() {
    xerahs::editor::RectangleAnnotation shape;
    shape.content.type = xerahs::editor::shape::text;
    shape.content.text = QStringLiteral("Hello");
    // The dialog releases itself when it closes, so it is watched through a guard.
    QPointer<TextContentDialog> dialog = new TextContentDialog(shape, nullptr);
    dialog->show();
    auto *error = dialog->findChild<QLabel *>("balloonContentError");
    auto *familyStatus = dialog->findChild<QLabel *>("annotationFontFamilyStatus");
    auto *text = dialog->findChild<QPlainTextEdit *>("balloonText");
    auto *family = dialog->findChild<QComboBox *>("annotationFontFamily");
    auto *color = dialog->findChild<QLineEdit *>("annotationTextColor");
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    QVERIFY(error && familyStatus && text && family && color && buttons);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    auto *cancel = buttons->button(QDialogButtonBox::Cancel);
    QVERIFY(apply && cancel);
    QVERIFY2(!dialog->windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                     // initial
    QVERIFY2(error->text().trimmed().isEmpty(), qPrintable(error->text()));                                  // initial: nothing is complained about yet
    QCOMPARE(text->toPlainText(), QStringLiteral("Hello"));                                                  // populated
    QVERIFY2(familyStatus->text().contains("Native default font"), qPrintable(familyStatus->text()));         // populated: an empty family means the native default
    family->setEditText("No Such Installed Font");
    QVERIFY2(familyStatus->text().contains("font-family-unavailable"), qPrintable(familyStatus->text()));     // unavailable: the code is named
    QVERIFY2(familyStatus->text().contains("This font is unavailable"), qPrintable(familyStatus->text()));     // unavailable: the sentence is named
    family->setEditText(QString());
    color->setText("not-a-colour");
    apply->click();
    QVERIFY2(error->text().contains("annotation-text-color-invalid"), qPrintable(error->text()));            // error: the code is named
    QVERIFY2(error->text().contains("Enter #AARRGGBB"), qPrintable(error->text()));                           // error: the sentence is named
    QVERIFY(dialog->isVisible());                                                                            // error: the dialog stays open
    QVERIFY(!dialog->choice());                                                                              // error: nothing is committed
    color->clear();
    text->clear();
    QVERIFY2(text->toPlainText().isEmpty(), "the annotation's own text was the only text there");             // empty
    apply->click();
    QVERIFY2(error->text().contains("annotation-style-invalid"), qPrintable(error->text()));                 // validation: the code is named
    QVERIFY2(error->text().contains("Enter non-empty text"), qPrintable(error->text()));                      // validation: the sentence is named
    QVERIFY(dialog->isVisible());                                                                            // validation: the dialog stays open
    QVERIFY(!dialog->choice());                                                                              // validation: nothing is committed
    cancel->click();
    QVERIFY(!dialog->isVisible());                                                                           // cancelled
    QVERIFY(!dialog->choice());                                                                              // cancelled: the annotation stays unchanged
    QTRY_VERIFY(!dialog);                                                                                    // cancelled: the dialog is released
  }

  // Pipeline history: initial, loading, empty, populated, unavailable, error.
  // validation: n/a because the dialog takes no typed input; it only lists records.
  // cancelled: n/a because the record read is the only background work and it cannot be cancelled.
  // denied: n/a because the dialog asks for no permission.
  void pipelineHistoryDialogStates() {
    const auto directory = dir.filePath("pipeline-history-states");
    auto storage = std::make_shared<NativePipelineJournal>(directory);
    PipelineHistoryDialog dialog(storage, [] { return false; });
    dialog.show();
    auto *status = dialog.findChild<QLabel *>("captureHistoryStatus");
    auto *table = dialog.findChild<QTreeWidget *>("captureHistoryRecords");
    QVERIFY(status && table);
    QVERIFY2(!dialog.windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                              // initial
    QVERIFY2(status->text().contains("Reading capture results"), qPrintable(status->text()));                         // loading
    QTRY_VERIFY(status->text().contains("0 result records"));                                                        // empty
    QCOMPARE(table->topLevelItemCount(), 0);
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    xerahs::actions::PipelineJournalSnapshot snapshot;
    snapshot.result.taskId = id; snapshot.selected = {"save"}; snapshot.terminal = true;
    snapshot.createdAt = QDateTime::currentDateTimeUtc();
    xerahs::actions::ActionResult saved; saved.action = "save"; saved.state = xerahs::actions::ActionState::Succeeded;
    snapshot.result.orderedResults = {saved};
    QVERIFY(storage->replace(id, {}, xerahs::actions::serializePipelineJournal(snapshot)).stored);
    dialog.findChild<QPushButton *>("refreshCaptureHistory")->click();
    QTRY_COMPARE(table->topLevelItemCount(), 1);
    QVERIFY2(status->text().contains("1 result records"), qPrintable(status->text()));                                // populated
    const auto broken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QFile unreadable(QDir(directory).filePath(broken + ".json"));
    QVERIFY(unreadable.open(QIODevice::WriteOnly)); unreadable.write("not a journal record"); unreadable.close();
    dialog.findChild<QPushButton *>("refreshCaptureHistory")->click();
    QTRY_COMPARE(table->topLevelItemCount(), 2);
    bool unavailable = false;
    for (int row = 0; row < table->topLevelItemCount(); ++row) unavailable |= table->topLevelItem(row)->text(1).contains("Unavailable");
    QVERIFY2(unavailable, "a record that cannot be read is listed as unavailable");                                  // unavailable
    QFile stray(QDir(directory).filePath("stray.json"));
    QVERIFY(stray.open(QIODevice::WriteOnly)); stray.write("{}"); stray.close();
    dialog.findChild<QPushButton *>("refreshCaptureHistory")->click();
    QTRY_VERIFY(status->text().contains("could not be read"));                                                      // error
  }

  // Workflow execution history: initial, loading, empty, populated, unavailable, error.
  // validation: n/a because the dialog takes no typed input; it only lists records.
  // cancelled: n/a because the record read is the only background work and it cannot be cancelled.
  // denied: n/a because the dialog asks for no permission.
  void workflowExecutionHistoryDialogStates() {
    {
      // The listing port blocks, so the reading state stays on screen until the test releases it.
      const auto gate = std::make_shared<WorkGate>();
      WorkflowHistoryPorts ports;
      ports.list = [gate] {
        gate->entered.store(true);
        while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return WorkflowExecutionListing{};
      };
      WorkflowExecutionHistoryDialog dialog(ports, [] { return false; });
      dialog.show();
      auto *status = dialog.findChild<QLabel *>("workflowExecutionHistoryStatus");
      auto *table = dialog.findChild<QTreeWidget *>("workflowExecutionRecords");
      QVERIFY(status && table);
      QVERIFY2(!dialog.windowTitle().trimmed().isEmpty(), "the dialog names its purpose");                            // initial
      QTRY_VERIFY(gate->entered.load());
      QVERIFY2(status->text().contains("Reading workflow execution metadata"), qPrintable(status->text()));           // loading
      gate->released.store(true);
      QTRY_VERIFY(status->text().contains("0 execution records"));                                                  // empty
      QCOMPARE(table->topLevelItemCount(), 0);
    }
    {
      WorkflowHistoryPorts ports;
      ports.list = [] {
        WorkflowExecutionListing listing;
        WorkflowExecutionEntry entry; entry.id = "fixture-execution";
        entry.result = QJsonObject{{"startedAt", "2026-01-01T00:00:00.000Z"}, {"state", "completed"}, {"origin", "native-shell"}};
        listing.entries.push_back(entry);
        WorkflowExecutionEntry unreadable; unreadable.id = "fixture-unreadable"; unreadable.diagnostic = "workflow-record-unreadable";
        listing.entries.push_back(unreadable);
        return listing;
      };
      WorkflowExecutionHistoryDialog dialog(ports, [] { return false; });
      dialog.show();
      auto *status = dialog.findChild<QLabel *>("workflowExecutionHistoryStatus");
      auto *table = dialog.findChild<QTreeWidget *>("workflowExecutionRecords");
      QVERIFY(status && table);
      QTRY_COMPARE(table->topLevelItemCount(), 2);
      QVERIFY2(status->text().contains("2 execution records"), qPrintable(status->text()));                            // populated
      bool unavailable = false;
      for (int row = 0; row < table->topLevelItemCount(); ++row) unavailable |= table->topLevelItem(row)->text(1).contains("Unavailable");
      QVERIFY2(unavailable, "a record that cannot be read is listed as unavailable");                                  // unavailable
    }
    {
      WorkflowHistoryPorts ports;
      ports.list = [] { WorkflowExecutionListing listing; listing.diagnostic = "workflow-storage-unavailable"; return listing; };
      WorkflowExecutionHistoryDialog dialog(ports, [] { return false; });
      dialog.show();
      auto *status = dialog.findChild<QLabel *>("workflowExecutionHistoryStatus");
      QVERIFY(status);
      QTRY_VERIFY(status->text().contains("could not be read"));                                                      // error
    }
  }

  // Capture history: initial, loading, empty, populated, validation, error.
  // cancelled: n/a because the history read is the only background work and it cannot be cancelled.
  // denied: n/a because the window asks for no permission.
  // unavailable: n/a because the window has no optional dependency.
  void captureHistoryWindowStates() {
    auto storage = std::make_shared<NativePipelineJournal>(dir.filePath("history-states"));
    CaptureHistoryWindow window(storage, [] { return false; }, [](const QString &, QWidget *) {});
    window.show();
    auto *status = window.findChild<QLabel *>("captureHistoryWindowStatus");
    auto *list = window.findChild<QListWidget *>("captureHistoryItems");
    auto *search = window.findChild<QLineEdit *>("captureHistorySearch");
    QVERIFY(status && list && search);
    QVERIFY2(!window.windowTitle().trimmed().isEmpty(), "the window names its purpose");                                // initial
    QVERIFY2(!search->placeholderText().trimmed().isEmpty(), "the search explains itself in words");                   // initial
    QVERIFY2(status->text().contains("Reading capture history"), qPrintable(status->text()));                           // loading
    QTRY_VERIFY(status->text().contains("No saved captures"));                                                         // empty
    QCOMPARE(list->count(), 0);
    QVERIFY2(!window.findChild<QPushButton *>("openHistoryCapture")->isEnabled(), "with no capture there is nothing to open");  // empty
    FileUploadHistoryRecord upload;
    upload.taskId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    upload.path = dir.filePath("gone.png");
    upload.origin = "file-upload";
    upload.destinationName = "Destination fixture";
    upload.createdAt = QDateTime::currentDateTimeUtc();
    upload.result = {"https://example.test/upload", "https://example.test/thumb", "https://example.test/delete", {}};
    QVERIFY(storage->recordFileUpload(upload).stored);
    window.refresh();
    QTRY_COMPARE(list->count(), 1);
    QVERIFY2(status->text().contains("history items"), qPrintable(status->text()));                                    // populated
    QTRY_VERIFY(list->count() > 0 && list->item(0)->text().contains("File missing"));                                // error: the gone file stays visible
    search->setText("no item carries this text");
    QTRY_VERIFY(status->text().contains("No history items match"));                                                   // validation
  }

  // Appearance: initial, populated, validation.
  // empty: n/a because the dialog opens with the saved preferences already loaded.
  // loading: n/a because the dialog applies its changes synchronously.
  // error: n/a because the only refusal the dialog can produce is the invalid value, which it reports as the validation message.
  // cancelled: n/a because cancelling closes the dialog without a message.
  // denied: n/a because the dialog asks for no permission.
  // unavailable: n/a because the dialog has no optional dependency.
  void appearanceDialogStates() {
    const auto states = probePreferenceDialog([&] { manageEditorAppearance(nullptr); }, [](QDialog *dialog) {
      auto *accent = dialog->findChild<QLineEdit *>("manualAccent");
      auto *apply = dialog->findChild<QPushButton *>("applyAppearance");
      auto *error = dialog->findChild<QLabel *>("appearanceError");
      if (!accent || !apply || !error) return QString();
      accent->setText("not-a-colour");
      apply->click();
      return error->text();
    });
    QVERIFY(states.entered);
    QVERIFY2(!states.title.trimmed().isEmpty(), "the dialog names its purpose");                                 // initial
    QVERIFY2(!states.text.trimmed().isEmpty(), "the dialog explains what it does in words");                      // initial
    const auto theme = states.fields.value("manualTheme");
    QVERIFY2(theme == "Dark" || theme == "Light", qPrintable(theme));                                           // populated: the saved theme is shown
    QVERIFY2(states.error.contains("Enter the accent"), qPrintable(states.error));                                // validation: the bad colour is refused in words
    QVERIFY2(states.cancelled, "cancelling closes the dialog");                                                 // cancelled
  }

  // Drawing defaults: initial, populated, validation.
  // empty: n/a because the dialog opens with the saved defaults already loaded.
  // loading: n/a because the dialog applies its changes synchronously.
  // error: n/a because the only refusal the dialog can produce is the invalid value, which it reports as the validation message.
  // cancelled: n/a because cancelling closes the dialog without a message.
  // denied: n/a because the dialog asks for no permission.
  // unavailable: n/a because the dialog has no optional dependency.
  void drawingDefaultsDialogStates() {
    const auto states = probePreferenceDialog([&] { manageDrawingDefaults(nullptr); }, [](QDialog *dialog) {
      auto *stroke = dialog->findChild<QLineEdit *>("defaultStrokeColour");
      auto *apply = dialog->findChild<QPushButton *>("applyDrawingDefaults");
      auto *error = dialog->findChild<QLabel *>("drawingDefaultsError");
      if (!stroke || !apply || !error) return QString();
      stroke->setText("not-a-colour");
      apply->click();
      return error->text();
    });
    QVERIFY(states.entered);
    QVERIFY2(!states.title.trimmed().isEmpty(), "the dialog names its purpose");                                 // initial
    QVERIFY2(!states.text.trimmed().isEmpty(), "the dialog explains what it does in words");                      // initial
    QVERIFY2(states.fields.value("defaultStrokeColour").startsWith("#"), qPrintable(states.fields.value("defaultStrokeColour")));  // populated: the saved stroke colour is shown
    QVERIFY2(states.error.contains("Enter stroke, fill and shadow colours"), qPrintable(states.error));           // validation: the bad colour is refused in words
    QVERIFY2(states.cancelled, "cancelling closes the dialog");                                                 // cancelled
  }

  // Shell window: initial, loading (determinate/indeterminate), populated, terminal failure,
  // permission denied, dependency unavailable. empty/validation/partial: n/a (it only reports results;
  // partial outcomes are shown by the pipeline result views, validation by each dialog).
  void shellWindowStatesAreVisibleTextWithAnAccessibleCue() {
    CommandRegistry commands; installToolDialogs(commands, {});
    ApplicationWindow window(commands); window.activate();
    auto *status = window.findChild<QLabel *>("operationStatus"); auto *progress = window.findChild<QProgressBar *>("operationProgress");
    QVERIFY(status); QVERIFY(progress);
    QCOMPARE(status->text(), QString("Ready")); QVERIFY(progress->isHidden());                                   // initial
    window.showProgress("Region capture", 40); QVERIFY(!progress->isHidden()); QCOMPARE(progress->maximum(), 100);    // loading, knowable
    QVERIFY2(status->text().contains("40%") && status->text().contains("Cancel"), qPrintable(status->text()));
    window.showActivity("Capture actions", "Saving capture"); QCOMPARE(progress->maximum(), 0); QVERIFY(status->text().contains("Saving"));  // loading, indeterminate
    lastAlert = QAccessible::InvalidEvent;
    window.showResult("Capture", "Saved to the captures folder", true); QVERIFY(progress->isHidden());            // populated / success
    QVERIFY(status->text().contains("Saved")); QCOMPARE(lastAlert, QAccessible::InvalidEvent);
    window.showResult("Capture", "Save failed: the folder is not writable", false);                               // terminal failure
    QVERIFY(status->text().contains("failed")); QCOMPARE(lastAlert, QAccessible::Alert);
    QCOMPARE(status->textFormat(), Qt::PlainText); QVERIFY(!status->accessibleDescription().isEmpty());
    window.showResult("Activation", "XerahS could not secure its runtime files. Check the ownership and permissions of XDG_RUNTIME_DIR.", false);  // permission denied
    QVERIFY(status->text().contains("permissions"));
    auto *video = commands.action("video-converter");                                                              // dependency unavailable
    QVERIFY(video);
    if (!video->isEnabled()) QVERIFY(video->toolTip().contains("GStreamer") && video->text().contains("requires"));
    QVERIFY2(window.findChild<QToolButton *>("video-converter"), "an unavailable command stays visible with its reason, or is explicitly absent");
  }
  void qrDialogStates() {
    QrCodeDialog qr;                                                                                              // initial + empty + validation
    QCOMPARE(qr.errorCode(), QString("qr-empty-content")); QVERIFY(qr.statusText().contains("qr-empty-content"));
    for (auto name : {"qr-copy", "qr-save", "qr-upload"}) QVERIFY(!qr.findChild<QPushButton *>(name)->isEnabled());
    qr.setText("hello");                                                                                          // populated
    QVERIFY(qr.errorCode().isEmpty()); QVERIFY(!qr.image().isNull());
    QVERIFY2(!qr.findChild<QPushButton *>("qr-upload")->isEnabled(), "no upload destination wired: dependency unavailable, not inert");  // dependency unavailable
    qr.setText(QString(3000, 'a')); QCOMPARE(qr.errorCode(), QString("qr-content-too-long")); QVERIFY(qr.image().isNull());  // validation error
    QVERIFY(!qr.statusText().isEmpty());
  }
  void hashCheckerStates() {
    HashCheckerDialog hash;
    auto *status = hash.findChild<QLabel *>("hashStatus"); auto *progress = hash.findChild<QProgressBar *>("hashProgress"); auto *cancel = hash.findChild<QPushButton *>("cancelHash");
    QVERIFY(hash.digest().isEmpty()); QVERIFY(!hash.busy());                                                      // initial / empty
    hash.setFile("/dev/zero"); hash.compute();                                                                   // loading: an endless read stays busy
    QTRY_VERIFY(hash.busy()); QVERIFY(cancel->isEnabled()); QVERIFY(!hash.statusText().isEmpty()); QVERIFY(status && progress);
    hash.cancel(); QTRY_VERIFY(!hash.busy()); QVERIFY(hash.digest().isEmpty()); QVERIFY(!hash.statusText().isEmpty());   // cancelled terminal state
    QFile file(dir.filePath("abc")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("abc"); file.close();
    hash.setFile(file.fileName()); hash.compute(); QTRY_VERIFY(!hash.digest().isEmpty());                          // populated
    hash.findChild<QLineEdit *>("hashCompare")->setText("deadbeef"); QCOMPARE(hash.comparison(), QString("No match"));    // validation, in words
    hash.setFile(dir.filePath("missing")); hash.compute(); QTRY_VERIFY(hash.digest().isEmpty() && !hash.busy());    // error
    QVERIFY2(hash.statusText().contains("not", Qt::CaseInsensitive) || hash.statusText().contains("fail", Qt::CaseInsensitive) || hash.statusText().contains("could", Qt::CaseInsensitive), qPrintable(hash.statusText()));
  }
  void iconConverterStates() {
    IconConverterDialog icon; auto *save = icon.findChild<QPushButton *>("saveIcon");
    QVERIFY(icon.imagePath().isEmpty()); QVERIFY(icon.errorText().isEmpty());                                       // initial / empty
    save->click(); QCOMPARE(icon.errorText(), QString("Choose an image before saving."));                           // validation: Save explains instead of doing nothing
    icon.setImage(dir.filePath("missing.png")); QVERIFY(!icon.errorText().isEmpty());                                // error
    QImage image(64, 64, QImage::Format_ARGB32); image.fill(Qt::blue); QVERIFY(image.save(dir.filePath("ok.png")));
    icon.setImage(dir.filePath("ok.png")); QVERIFY(icon.errorText().isEmpty());                                     // populated
    for (auto *box : icon.sizeCheckBoxes()) box->setChecked(false);
    QVERIFY2(!save->isEnabled(), "no size selected: validation keeps Save disabled");                               // validation
    icon.sizeCheckBoxes().first()->setChecked(true);
  }
  void colourPickerStates() {
    ColorPickerDialog picker; auto *status = picker.findChild<QLabel *>("colourPickerStatus"); auto *hex = picker.findChild<QLineEdit *>("hexColour");
    QVERIFY(!picker.findChild<QPushButton *>("copyAllColorFormats")->isEnabled());                                  // initial / empty
    picker.showColor(QColor("#336699")); QVERIFY(picker.findChild<QPushButton *>("copyAllColorFormats")->isEnabled());  // populated
    hex->setText("zzzzzz"); hex->editingFinished(); QVERIFY(!picker.findChild<QLabel *>("hexColourError")->text().isEmpty()); // validation
    picker.notePortalResult(PortalColorPicker::Outcome::Denied, {});                                                 // permission denied
    QVERIFY2(status->text().contains("refused"), qPrintable(status->text()));
    picker.notePortalResult(PortalColorPicker::Outcome::Failed, {});                                                 // dependency unavailable
    QVERIFY2(status->text().contains("unavailable"), qPrintable(status->text()));
    QVERIFY(picker.findChild<QPushButton *>("pickAgain")->isEnabled());
  }
  // New image: initial, empty, populated, validation, loading, cancelled, error, failure.
  // denied: n/a because the dialog asks for no permission; a refused input is reported as an error.
  // unavailable: n/a because the dialog has no optional dependency; its decoder is always built in.
  void newImageDialogStates() {
    NewImageDialog dialog; dialog.show();
    auto *status = dialog.findChild<QLabel *>("newImageStatus");
    auto *initial = dialog.findChild<QLabel *>("newImageInitial");
    auto *width = dialog.findChild<QSpinBox *>("newImageWidth");
    auto *height = dialog.findChild<QSpinBox *>("newImageHeight");
    auto *background = dialog.findChild<QComboBox *>("newImageBackground");
    auto *colour = dialog.findChild<QLineEdit *>("newImageColour");
    auto *progress = dialog.findChild<QProgressBar *>("newImageProgress");
    auto *cancel = dialog.findChild<QPushButton *>("cancelNewImageWork");
    auto *create = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    QVERIFY(status && initial && width && height && background && colour && progress && cancel && create);
    QVERIFY2(status->text().contains("Create prepares"), qPrintable(status->text()));                                     // initial
    QVERIFY(create->isEnabled());
    QCOMPARE(initial->text(), QString("No initial image"));                                                              // empty: nothing is selected
    QImage source(3, 2, QImage::Format_ARGB32); source.fill(Qt::blue);
    QVERIFY(dialog.setInitialImage(source, "Fixture"));
    QVERIFY2(initial->text().contains("Fixture") && initial->text().contains("pixels"), qPrintable(initial->text()));  // populated
    width->setValue(100000); height->setValue(100000);
    QVERIFY(!create->isEnabled());
    QVERIFY2(status->text().contains("limits"), qPrintable(status->text()));                                             // validation: the size is over the limit
    width->setValue(6); height->setValue(4);
    background->setCurrentIndex(1); colour->setText("invalid");
    QVERIFY(!create->isEnabled());
    QVERIFY2(status->text().contains("#AARRGGBB"), qPrintable(status->text()));                                         // validation: the colour is not #AARRGGBB
    colour->setText("#FF0000FF");
    QVERIFY(create->isEnabled());                                                                                        // populated: a valid input enables Create
    const auto gate = std::make_shared<WorkGate>();
    NewImageDialog working(nullptr, gatedHooks(gate, false)); working.show();
    auto *workStatus = working.findChild<QLabel *>("newImageStatus");
    auto *workProgress = working.findChild<QProgressBar *>("newImageProgress");
    auto *workCancel = working.findChild<QPushButton *>("cancelNewImageWork");
    QVERIFY(working.loadInitialFile(dir.filePath("initial.png")));
    QVERIFY2(workStatus->text().contains("Loading"), qPrintable(workStatus->text()));                                    // loading
    QVERIFY(workProgress->isVisible());
    QTRY_VERIFY(gate->entered.load());
    workCancel->click();
    QVERIFY2(workStatus->text().contains("cancelled"), qPrintable(workStatus->text()));                                 // cancelled
    QVERIFY(!workProgress->isVisible());
    gate->released.store(true);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    NewImageDialog failing; failing.show();
    auto *failStatus = failing.findChild<QLabel *>("newImageStatus");
    QFile bad(dir.filePath("bad.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
    QVERIFY(failing.loadInitialFile(dir.filePath("bad.png")));
    QTRY_VERIFY_WITH_TIMEOUT(failStatus->text().contains("not a supported image"), 5000);                                // error: the file is not an image
    QVERIFY(failing.loadInitialFile(dir.filePath("missing.png")));
    QTRY_VERIFY_WITH_TIMEOUT(failStatus->text().contains("not a supported image"), 5000);                                // error: the file is missing
    const auto failingGate = std::make_shared<WorkGate>();
    NewImageDialog broken(nullptr, gatedHooks(failingGate, true)); broken.show();
    broken.findChild<QSpinBox *>("newImageWidth")->setValue(4);
    broken.findChild<QSpinBox *>("newImageHeight")->setValue(3);
    broken.accept();
    QTRY_VERIFY(failingGate->entered.load());
    QVERIFY2(broken.findChild<QLabel *>("newImageStatus")->text().contains("Preparing"), "the preparation announces itself"); // loading
    failingGate->released.store(true);
    QTRY_VERIFY_WITH_TIMEOUT(broken.findChild<QLabel *>("newImageStatus")->text().contains("preparation failed"), 5000);  // failure
    QVERIFY(!broken.findChild<QLabel *>("newImageStatus")->text().contains("private"));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
  }
  // URL: initial, empty, validation, denied, populated, loading, cancelled, failure.
  // unavailable: n/a because the dialog has no optional dependency; every refusal is a validation or a failure.
  // The download is driven against a stub network, so no request reaches a real server.
  void urlOpenDialogStates() {
    StubNetwork network;
    UrlOpenOptions options; options.manager = &network;
    UrlOpenDialog dialog(nullptr, options); dialog.show();
    auto *input = dialog.findChild<QLineEdit *>("openImageUrlInput");
    auto *allowHttp = dialog.findChild<QCheckBox *>("openImageUrlAllowHttp");
    auto *privacy = dialog.findChild<QLabel *>("openImageUrlPrivacy");
    auto *status = dialog.findChild<QLabel *>("openImageUrlStatus");
    auto *progress = dialog.findChild<QProgressBar *>("openImageUrlProgress");
    auto *download = dialog.findChild<QPushButton *>("openImageUrlDownload");
    auto *cancel = dialog.findChild<QPushButton *>("openImageUrlCancel");
    QVERIFY(input && allowHttp && privacy && status && progress && download && cancel);
    QVERIFY2(privacy->text().contains("Nothing is downloaded until you press Download"), qPrintable(privacy->text()));   // initial
    QVERIFY(input->text().isEmpty());
    QVERIFY(download->isEnabled());
    QVERIFY(status->text().isEmpty());                                                                                     // initial: nothing has been requested yet
    download->click();
    QVERIFY2(status->text().contains("https://"), qPrintable(status->text()));                                          // empty
    QCOMPARE(network.requests, 0);
    input->setText("ftp://example.com/image.png");
    download->click();
    QVERIFY2(status->text().contains("Only https://"), qPrintable(status->text()));                                      // validation: the scheme is refused
    QCOMPARE(network.requests, 0);
    input->setText("http://example.com/image.png");
    download->click();
    QVERIFY2(status->text().contains("Unencrypted http://"), qPrintable(status->text()));                                // denied: refused without the opt-in
    QCOMPARE(network.requests, 0);
    allowHttp->setChecked(true);
    input->setText("https://example.com/image.png");
    download->click();
    QCOMPARE(network.requests, 1);                                                                                      // populated: the valid address is requested
    QVERIFY2(status->text().contains("Contacting"), qPrintable(status->text()));                                         // loading
    QVERIFY(progress->isVisible());
    cancel->click();
    QVERIFY2(status->text().contains("cancelled"), qPrintable(status->text()));                                         // cancelled
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    StubNetwork served;
    served.outcome = StubReply::Outcome::Success;
    served.payload = pngBytes();
    UrlOpenOptions servedOptions; servedOptions.manager = &served;
    UrlOpenDialog done(nullptr, servedOptions); done.show();
    done.findChild<QLineEdit *>("openImageUrlInput")->setText("https://example.com/image.png");
    done.findChild<QPushButton *>("openImageUrlDownload")->click();
    QTRY_COMPARE(done.result(), int(QDialog::Accepted));
    const auto result = done.takeResult();
    QVERIFY(result);
    QCOMPARE(result->bytes, served.payload);                                                                            // populated: the image is handed over
    QVERIFY2(!done.findChild<QLabel *>("openImageUrlStatus")->text().isEmpty(), "the download reports itself in words");
    StubNetwork refused;
    refused.outcome = StubReply::Outcome::Failed;
    UrlOpenOptions refusedOptions; refusedOptions.manager = &refused;
    UrlOpenDialog broken(nullptr, refusedOptions); broken.show();
    broken.findChild<QLineEdit *>("openImageUrlInput")->setText("https://example.com/image.png");
    broken.findChild<QPushButton *>("openImageUrlDownload")->click();
    QTRY_VERIFY_WITH_TIMEOUT(broken.findChild<QLabel *>("openImageUrlStatus")->text().contains("failed"), 5000);          // failure
  }
  // Recognition: initial, empty, populated, loading, cancelled, failure, denied, unavailable.
  // validation: n/a because the dialog takes no typed input; the values it shows are produced locally.
  // The capture and recognition ports are injected, so no portal or file dialog is ever opened.
  void imageRecognitionDialogStates() {
    ImageRecognitionPorts ports;
    std::function<void(RecognitionImageChoice)> pending;
    ports.capture = [&](auto complete) { pending = std::move(complete); };
    ports.recognize = [](auto, const QImage &, const auto &) { return ImageRecognitionResult{{"fixture QR"}}; };
    ImageRecognitionDialog dialog(ImageRecognitionKind::QrCode, nullptr, ports); dialog.show();
    auto *status = dialog.findChild<QLabel *>("recognitionStatus");
    auto *results = dialog.findChild<QPlainTextEdit *>("recognitionResults");
    auto *progress = dialog.findChild<QProgressBar *>("recognitionProgress");
    auto *copy = dialog.findChild<QPushButton *>("copyRecognitionResults");
    auto *cancel = dialog.findChild<QPushButton *>("cancelRecognition");
    QVERIFY(status && results && progress && copy && cancel);
    QVERIFY2(status->text().contains("Choose an image file"), qPrintable(status->text()));                                // initial
    QVERIFY(results->toPlainText().isEmpty());                                                                            // empty
    QVERIFY(!copy->isEnabled()); QVERIFY(!cancel->isEnabled());
    dialog.scanScreen();
    QTRY_VERIFY(dialog.busy());
    QVERIFY2(status->text().contains("permission"), qPrintable(status->text()));                                         // loading: it asks the desktop for permission
    QVERIFY(cancel->isEnabled());
    dialog.cancel();
    QTRY_VERIFY(!dialog.busy());
    QVERIFY2(status->text().contains("Cancelled"), qPrintable(status->text()));                                          // cancelled
    dialog.showRecognizedValues({"fixture QR"});
    QCOMPARE(dialog.values(), QStringList{"fixture QR"});
    QVERIFY2(results->toPlainText().contains("fixture QR"), qPrintable(results->toPlainText()));                         // populated
    QVERIFY2(status->text().contains("QR code"), qPrintable(status->text()));
    QVERIFY(copy->isEnabled());
    ImageRecognitionPorts emptyPorts;
    emptyPorts.capture = [](auto complete) { complete({QImage(2, 2, QImage::Format_ARGB32)}); };
    emptyPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{}; };
    ImageRecognitionDialog emptyDialog(ImageRecognitionKind::QrCode, nullptr, emptyPorts);
    emptyDialog.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(emptyDialog.findChild<QLabel *>("recognitionStatus")->text().contains("No QR codes found"), 5000); // empty: the scan found nothing
    ImageRecognitionPorts deniedPorts;
    deniedPorts.capture = [](auto complete) { complete({{}, "permission-denied"}); };
    deniedPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{}; };
    ImageRecognitionDialog denied(ImageRecognitionKind::QrCode, nullptr, deniedPorts);
    denied.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(denied.findChild<QLabel *>("recognitionStatus")->text().contains("Recognition failed"), 5000);   // denied / failure
    QVERIFY2(denied.findChild<QLabel *>("recognitionStatus")->text().contains("permission-denied"), "the refusal is named");
    ImageRecognitionPorts missingPorts;
    missingPorts.capture = [](auto complete) { complete({QImage(2, 2, QImage::Format_ARGB32)}); };
    missingPorts.recognize = [](auto, const auto &, const auto &) { return ImageRecognitionResult{{}, "qr-scanner-unavailable"}; };
    ImageRecognitionDialog missing(ImageRecognitionKind::QrCode, nullptr, missingPorts);
    missing.scanScreen();
    QTRY_VERIFY_WITH_TIMEOUT(missing.findChild<QLabel *>("recognitionStatus")->text().contains("unavailable"), 5000);       // dependency unavailable
    QVERIFY2(missing.findChild<QLabel *>("recognitionStatus")->text().contains("zxing"), "the missing dependency is named");
    if (!qrScanningAvailable()) {
      ImageRecognitionDialog plain(ImageRecognitionKind::QrCode);                                                        // dependency unavailable
      QVERIFY2(plain.findChild<QLabel *>("recognitionStatus")->text().contains("unavailable"), "the missing scanner is named");
      QVERIFY(!plain.findChild<QPushButton *>("recognitionFile")->isEnabled());
    }
  }
  // Comparison: initial, empty, loading, cancelled, populated, validation, failure.
  // denied: n/a because the dialog asks for no permission; a refused file is a validation error.
  // unavailable: n/a because the dialog has no optional dependency; its decoder is always built in.
  // The file pickers are never opened: every load goes through the public loadFile() entry point.
  void imageComparisonDialogStates() {
    QImage source(64, 48, QImage::Format_ARGB32); source.fill(Qt::white);
    const auto gate = std::make_shared<ComparisonGate>();
    ImageComparisonDialog dialog({}, source, nullptr, gatedComparisonHooks(gate));
    auto *first = dialog.findChild<QLabel *>("comparisonInput0");
    auto *second = dialog.findChild<QLabel *>("comparisonInput1");
    auto *geometry = dialog.findChild<QLabel *>("comparisonGeometry");
    auto *status = dialog.findChild<QLabel *>("comparisonStatus");
    auto *progress = dialog.findChild<QProgressBar *>("comparisonProgress");
    auto *cancel = dialog.findChild<QPushButton *>("cancelComparisonLoad");
    auto *slider = dialog.findChild<QSlider *>("comparisonDivider");
    auto *view = dialog.findChild<ComparisonView *>("comparisonView");
    QVERIFY(first && second && geometry && status && progress && cancel && slider && view);
    QCOMPARE(view->accessibleDescription(), QString("Choose two images to compare."));                       // empty: the hint is exposed, not only painted
    QCOMPARE(first->text(), QString("No image selected"));                                                   // initial / empty
    QCOMPARE(second->text(), QString("No image selected"));
    QVERIFY2(geometry->text().contains("must be validated"), qPrintable(geometry->text()));                // empty: it explains itself
    QVERIFY(status->text().isEmpty());                                                                      // initial: nothing is loading yet
    dialog.show();
    QTRY_VERIFY(gate->entered.load());
    QVERIFY2(status->text().contains("Preparing"), qPrintable(status->text()));                             // loading
    QVERIFY(progress->isVisible());
    QVERIFY(cancel->isEnabled());
    cancel->click();
    QVERIFY2(status->text().contains("Load cancelled"), qPrintable(status->text()));                         // cancelled
    QVERIFY(!progress->isVisible());
    QCOMPARE(first->text(), QString("No image selected"));
    gate->released.store(true);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    const auto firstPath = dir.filePath("first.png"), secondPath = dir.filePath("second.png");
    QImage red(4, 2, QImage::Format_ARGB32); red.fill(Qt::red); QVERIFY(red.save(firstPath));
    QImage blue(2, 4, QImage::Format_ARGB32); blue.fill(Qt::blue); QVERIFY(blue.save(secondPath));
    QVERIFY(dialog.loadFile(0, firstPath));
    QTRY_VERIFY(first->text().contains("first.png"));
    QVERIFY(dialog.loadFile(1, secondPath));
    QTRY_VERIFY(slider->isEnabled());                                                                        // populated
    QTRY_COMPARE(view->accessibleDescription(), QString("First image left of the divider; second image right. Drag to move the divider, or use arrow keys and Home/End."));          // populated: the view explains the divider
    QVERIFY2(first->text().contains("4 × 2"), qPrintable(first->text()));
    QVERIFY2(second->text().contains("2 × 4"), qPrintable(second->text()));
    QVERIFY2(status->text().contains("Move the divider"), qPrintable(status->text()));
    QVERIFY2(geometry->text().contains("Comparison canvas"), qPrintable(geometry->text()));
    const auto validatedFirst = first->text(), validatedSecond = second->text();
    const auto badPath = dir.filePath("bad.png");
    QFile bad(badPath); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
    QVERIFY(dialog.loadFile(0, badPath));
    QTRY_VERIFY(status->text().contains("not a supported image"));                                           // validation
    QCOMPARE(first->text(), validatedFirst);
    QCOMPARE(second->text(), validatedSecond);
    ImageComparisonDialog broken({}, source, nullptr,
                                ComparisonHooks{[] { throw std::runtime_error("private comparison detail"); }});
    broken.show();
    auto *brokenStatus = broken.findChild<QLabel *>("comparisonStatus");
    QVERIFY(brokenStatus);
    QTRY_VERIFY_WITH_TIMEOUT(brokenStatus->text().contains("could not be prepared"), 5000);                  // failure
    QVERIFY(!brokenStatus->text().contains("private"));
    QCOMPARE(brokenStatus->textFormat(), Qt::PlainText);
    QTRY_VERIFY_WITH_TIMEOUT(brokenStatus->accessibleDescription().contains("could not be prepared"), 5000);  // failure: the accessible cue carries it
  }
  // Video converter: initial, empty, validation, populated, loading, cancelled, failure, success.
  // denied: n/a because the dialog asks for no permission.
  // The unavailable state is reached with media::NoElements, where every codec is refused with
  // its reason; the conversion states use a fake element set, so no GStreamer element is used.
  void videoConverterDialogStates() {
    const auto clip = dir.filePath("clip.mkv");
    QFile file(clip); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); file.close();
    xerahs::media::NoElements noElements;
    VideoConverterDialog unavailable(new FakeConversionRunner, &noElements); unavailable.show();
    auto *unavailableStatus = unavailable.findChild<QLabel *>("vcStatus");
    auto *unavailableCodec = unavailable.findChild<QComboBox *>("vcCodec");
    auto *unavailableModel = qobject_cast<QStandardItemModel *>(unavailableCodec ? unavailableCodec->model() : nullptr);
    QVERIFY(unavailableStatus && unavailableCodec && unavailableModel);
    QVERIFY2(unavailableStatus->text().contains("Choose or drop a video"), qPrintable(unavailableStatus->text()));  // initial
    QVERIFY(unavailable.input().isEmpty());
    QVERIFY(unavailable.findChild<QLineEdit *>("vcOutput")->text().isEmpty());
    QVERIFY(!unavailableModel->item(0)->isEnabled());                                                        // dependency unavailable: the codec stays listed but disabled
    QVERIFY2(unavailableCodec->itemData(0, Qt::ToolTipRole).toString().contains("GStreamer"),
             qPrintable(unavailableCodec->itemData(0, Qt::ToolTipRole).toString()));
    unavailable.setInput(clip);
    unavailable.findChild<QPushButton *>("vcConvert")->click();
    QVERIFY2(unavailableStatus->text().contains("VC_CODEC_UNAVAILABLE"), qPrintable(unavailableStatus->text()));  // dependency unavailable
    QVERIFY2(unavailableStatus->text().contains("GStreamer"), qPrintable(unavailableStatus->text()));
    xerahs::media::SetAvailability availability({"x264enc", "mp4mux"});
    auto *runner = new FakeConversionRunner;
    VideoConverterDialog dialog(runner, &availability); dialog.show();
    auto *status = dialog.findChild<QLabel *>("vcStatus");
    auto *output = dialog.findChild<QLineEdit *>("vcOutput");
    auto *convert = dialog.findChild<QPushButton *>("vcConvert");
    auto *cancel = dialog.findChild<QPushButton *>("vcCancel");
    auto *show = dialog.findChild<QPushButton *>("vcShow");
    auto *progress = dialog.findChild<QProgressBar *>("vcProgress");
    QVERIFY(status && output && convert && cancel && show && progress);
    QVERIFY2(status->text().contains("Choose or drop a video"), qPrintable(status->text()));                  // initial
    QVERIFY(dialog.input().isEmpty());
    QVERIFY(output->text().isEmpty());
    convert->click();
    QVERIFY2(status->text().contains("VC_EMPTY_INPUT"), qPrintable(status->text()));                         // empty / validation: no input is named
    QCOMPARE(runner->starts, 0);
    dialog.setInput(clip);
    QVERIFY2(status->text().contains("Ready to convert"), qPrintable(status->text()));                       // populated
    QVERIFY2(output->text().endsWith("clip-converted.mp4"), qPrintable(output->text()));
    output->setText(clip);
    convert->click();
    QVERIFY2(status->text().contains("VC_OUTPUT_IS_INPUT"), qPrintable(status->text()));                      // validation
    QCOMPARE(runner->starts, 0);
    output->setText(dir.filePath("out.mp4"));
    convert->click();
    QCOMPARE(runner->starts, 1);
    QVERIFY(dialog.busy());
    QVERIFY2(status->text().contains("Converting"), qPrintable(status->text()));                              // loading
    QVERIFY(progress->isVisible());
    QVERIFY(!convert->isEnabled());
    QVERIFY(cancel->isEnabled());
    emit runner->progress(37);
    QTRY_COMPARE(progress->value(), 37);
    cancel->click();
    QVERIFY2(status->text().contains("Cancelled"), qPrintable(status->text()));                               // cancelled
    QVERIFY(!dialog.busy());
    convert->click();
    xerahs::media::ConversionResult failed; failed.outcome = xerahs::media::Outcome::Failed;
    failed.error = xerahs::media::ErrorCode::PipelineFailed; failed.message = "boom";
    runner->complete(failed);
    QVERIFY2(status->text().contains("VC_PIPELINE_FAILED"), qPrintable(status->text()));                      // failure
    QVERIFY2(status->text().contains("boom"), qPrintable(status->text()));
    QTRY_VERIFY(status->accessibleDescription().contains("VC_PIPELINE_FAILED"));                               // failure: the accessible cue carries it
    convert->click();
    xerahs::media::ConversionResult served; served.outcome = xerahs::media::Outcome::Success;
    served.outputPath = output->text();
    runner->complete(served);
    QVERIFY2(status->text().contains("Saved"), qPrintable(status->text()));                                   // populated: the result is named
    QCOMPARE(progress->value(), 100);
    QVERIFY(show->isVisibleTo(&dialog));
    dialog.setFolderOpener([](const QString &) { return false; });
    show->click();
    QVERIFY2(status->text().contains("No file manager"), qPrintable(status->text()));                          // failure: the folder cannot be shown
  }
  // Watch folders: initial, empty, validation, error, denied, populated.
  // loading: n/a because the dialog applies rules synchronously and reports the outcome in the same label.
  // cancelled: n/a because the dialog has no long-running operation to cancel.
  // unavailable: n/a because the dialog has no optional dependency; a broken store is reported as an error.
  // The folder picker is never opened: every rule is typed into the path field.
  void watchFoldersDialogStates() {
    const auto captures = dir.filePath("captures");
    QVERIFY(QDir().mkpath(captures));
    xerahs::watch::WatchFolders store(dir.filePath("watch-store"));
    QVERIFY(store.loadError().isEmpty());
    auto credentials = std::make_shared<xerahs::uploads::MemoryCredentialStore>();
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
    FileUploadPorts ports; ports.catalogPath = dir.filePath("destinations.json");
    ports.credentials = credentials; ports.history = journal;
    FileUploadController uploads(ports);
    WatchFolderService service(store, uploads, [&] { return captures; });
    WatchFoldersDialog dialog(service); dialog.show();
    auto *list = dialog.findChild<QListWidget *>("watchFolders");
    auto *error = dialog.findChild<QLabel *>("watchError");
    auto *path = dialog.findChild<QLineEdit *>("watchPath");
    auto *filters = dialog.findChild<QLineEdit *>("watchFilters");
    auto *add = dialog.findChild<QPushButton *>("addWatchFolder");
    auto *remove = dialog.findChild<QPushButton *>("removeWatchFolder");
    QVERIFY(list && error && path && filters && add && remove);
    QString description;
    for (auto *label : dialog.findChildren<QLabel *>())
      if (label->text().startsWith("New matching files are processed")) description = label->text();
    QVERIFY2(!description.isEmpty(), "the dialog explains what watching does in words");                       // initial
    QCOMPARE(list->count(), 0);                                                                              // empty
    QVERIFY(error->text().isEmpty());
    QVERIFY2(!remove->isEnabled(), "with no watch folder there is nothing to remove");                         // empty: Remove stays disabled
    remove->click();                                                                                         // empty: a disabled Remove does nothing at all
    QCOMPARE(list->count(), 0);
    QVERIFY(error->text().isEmpty());
    const auto watched = dir.filePath("watched");
    QVERIFY(QDir().mkpath(watched));
    path->setText(watched);
    filters->setText("");
    lastAlert = QAccessible::InvalidEvent;
    add->click();
    QCOMPARE(error->text(), QString("Enter at least one file pattern, for example *.png. (watch-filter-required)")); // validation: a filter is required
    QTRY_COMPARE(lastAlert, QAccessible::Alert);                                                            // validation: the error raises an accessible alert
    filters->setText("sub/*.png");
    add->click();
    QCOMPARE(error->text(), QString("One of the file patterns is not valid. (watch-filter-invalid)"));          // validation: a filter may not cross folders
    filters->setText("*.png");
    path->setText(dir.filePath("missing"));
    add->click();
    QCOMPARE(error->text(), QString("Choose a folder to watch. (watch-directory-required)"));                    // error: the folder does not exist
    path->setText(captures);
    add->click();
    QCOMPARE(error->text(), QString("That folder contains the capture folder, which would upload captures in a loop. (watch-capture-loop)")); // error: the captures folder may not be watched
    const auto denied = dir.filePath("denied");
    QVERIFY(QDir().mkpath(denied));
    QVERIFY(QFile::setPermissions(denied, QFile::ExeOwner));
    if (geteuid() == 0) {
      // denied: n/a because the process is root and bypasses the directory permission bits
    } else {
      path->setText(denied);
      add->click();
      QCOMPARE(error->text(), QString("XerahS cannot read that folder. (watch-directory-denied)"));              // denied: the folder may not be read
    }
    QFile::setPermissions(denied, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    path->setText(watched);
    add->click();
    QVERIFY(error->text().isEmpty());
    QCOMPARE(list->count(), 1);                                                                             // populated
    list->setCurrentRow(0);
    QVERIFY2(remove->isEnabled(), "a selected watch folder can be removed");                                // populated: Remove is armed
    QVERIFY2(list->item(0)->text().contains("watched"), qPrintable(list->item(0)->text()));
    QVERIFY2(list->item(0)->text().contains("Upload"), qPrintable(list->item(0)->text()));
    const auto second = dir.filePath("watched-two");
    QVERIFY(QDir().mkpath(second));
    QVERIFY(QDir(watched).removeRecursively());
    path->setText(second);
    add->click();
    QVERIFY(error->text().isEmpty());
    QCOMPARE(list->count(), 2);
    QVERIFY2(list->item(0)->text().contains("folder missing"), qPrintable(list->item(0)->text()));            // error: a missing folder stays visible
  }
  // Effects: initial, empty, populated, loading, validation, failure, unavailable.
  // denied: n/a because the dialog asks for no permission; it edits an image already held in memory.
  // cancelled: n/a because the only cancel action closes the dialog, and a cancelled preview is
  // discarded without a message (the worker reports effect-cancelled, the dialog drops that result).
  // The recent/favorite store lives in the temporary directory, so no user setting is touched.
  void effectsDialogStates() {
    QImage pixels(64, 48, QImage::Format_ARGB32); pixels.fill(Qt::white);
    const auto loaded = prepareCapturedSource(pixels);
    QVERIFY(loaded.document);
    EffectsDialog dialog(*loaded.document, loaded.image, [](PreparedCanvasEdit) { return false; },
                          nullptr, dir.filePath("effects-store.json"));
    dialog.show();
    auto *status = dialog.findChild<QLabel *>("effectsStatus");
    auto *preview = dialog.findChild<QLabel *>("effectsPreview");
    auto *list = dialog.findChild<QListWidget *>("effectsList");
    auto *notice = dialog.findChild<QLabel *>("effectsUnavailableNotice");
    auto *unavailable = dialog.findChild<QListWidget *>("effectsUnavailableList");
    auto *progress = dialog.findChild<QProgressBar *>();
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    auto *search = dialog.findChild<QLineEdit *>("effectsSearch");
    QVERIFY(status && preview && list && notice && unavailable && progress && buttons && search);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply);
    QVERIFY2(preview->text() == QStringLiteral("Select an effect to preview"), qPrintable(preview->text()));   // initial: the hint stays while there is no preview
    QVERIFY2(status->text().isEmpty(), qPrintable(status->text()));                                        // initial: nothing has been reported yet
    QVERIFY2(!apply->isEnabled(), "the recent and favorite store is still loading");                        // initial
    QTRY_VERIFY(apply->isEnabled());                                                                      // populated: a valid effect is selected once the store has loaded
    QVERIFY(!dialog.selectedId().isEmpty());
    QTRY_VERIFY(!dialog.previewImage().isNull());                                                         // populated: the selected effect is previewed
    QVERIFY2(status->text().contains("Preview only"), qPrintable(status->text()));                          // populated
    QVERIFY2(notice->text().contains("unavailable"), qPrintable(notice->text()));                            // unavailable
    bool reasonNamed = false, stillListed = false;
    for (int row = 0; row < unavailable->count(); ++row) reasonNamed |= unavailable->item(row)->text().contains("effect-unavailable");
    for (int row = 0; row < list->count(); ++row) stillListed |= list->item(row)->text().contains("effect-unavailable");
    QVERIFY2(reasonNamed, "every unavailable effect names the reason it is unavailable");                    // unavailable
    QVERIFY2(stillListed, "an unavailable effect stays listed, disabled, with its reason");                   // unavailable
    search->setText("no effect in the catalog answers to this");
    QTRY_VERIFY(list->count() == 0);                                                                      // empty: nothing matches the search
    QVERIFY2(!apply->isEnabled(), "with no effect to apply, Apply stays disabled");                          // empty
    QVERIFY2(status->text() == QStringLiteral("No effect matches the search. Change the search or choose a category."), qPrintable(status->text()));  // empty: an empty search is guidance, not an effect failure
    QVERIFY2(preview->text() == QStringLiteral("Select an effect to preview"), qPrintable(preview->text()));   // empty: the hint returns when the preview is cleared
    search->setText("auto_contrast");
    QTRY_VERIFY(dialog.selectedId() == QString("auto_contrast"));                                          // populated
    QTRY_VERIFY(!dialog.previewImage().isNull());                                                         // populated
    auto *clip = dialog.findChild<QLineEdit *>("param:clip_percent");
    auto *clipError = dialog.findChild<QLabel *>("error:clip_percent");
    QVERIFY(clip && clipError);
    clip->setText("999");
    QVERIFY2(clipError->text().contains("Enter a valid value"), qPrintable(clipError->text()));               // validation
    QVERIFY2(status->text().contains("effect-invalid-parameter"), qPrintable(status->text()));                // validation
    QVERIFY2(!apply->isEnabled(), "an out-of-range parameter keeps Apply disabled");                          // validation
    clip->setText("0.5");
    QTRY_VERIFY(apply->isEnabled());                                                                      // populated: a valid parameter enables Apply again
    apply->click();
    QVERIFY2(status->text().contains("Applying"), qPrintable(status->text()));                               // loading
    QVERIFY(progress->isVisible());                                                                       // loading
    QTRY_VERIFY(status->text().contains("effect-commit-stale"));                                           // failure
    QVERIFY2(status->accessibleDescription().contains("effect-commit-stale"), "the failure carries an accessible cue");  // failure
  }
  // Presets: initial, empty, loading, populated, validation, unavailable, error, failure.
  // denied: n/a because the dialog asks for no permission; it reads a preset the user chose.
  // cancelled: n/a because the only cancel action closes the dialog and abandons the run without a message.
  void presetDialogStates() {
    QImage pixels(64, 48, QImage::Format_ARGB32); pixels.fill(Qt::white);
    const auto loaded = prepareCapturedSource(pixels);
    QVERIFY(loaded.document);
    auto registry = std::make_shared<xerahs::effects::EffectRegistry>(xerahs::effects::builtinEffects());
    PresetDialog dialog(*loaded.document, loaded.image, [](PreparedCanvasEdit) { return false; }, registry);
    dialog.show();
    auto *status = dialog.findChild<QLabel *>("presetStatus");
    auto *steps = dialog.findChild<QListWidget *>("presetSteps");
    auto *exportButton = dialog.findChild<QPushButton *>("presetExport");
    auto *buttons = dialog.findChild<QDialogButtonBox *>();
    QVERIFY(status && steps && exportButton && buttons);
    auto *apply = buttons->button(QDialogButtonBox::Apply);
    QVERIFY(apply);
    QVERIFY2(status->text().isEmpty(), qPrintable(status->text()));                                        // initial: no preset has been read yet
    QVERIFY2(!apply->isEnabled(), "there is nothing to apply until a preset is read");                        // initial
    QVERIFY2(!exportButton->isEnabled(), "there is nothing to export until a preset is read");                 // initial
    QCOMPARE(steps->count(), 0);                                                                           // empty
    xerahs::effects::EffectPreset nothing;
    dialog.setPreset(nothing);
    QCOMPARE(steps->count(), 0);                                                                           // empty: the preset carries no steps
    QVERIFY2(!apply->isEnabled(), "a preset without steps cannot be applied");                               // empty
    QVERIFY2(status->text().contains("This preset has no steps, so there is nothing to apply."), qPrintable(status->text()));  // empty: an empty preset says there is nothing to apply
    dialog.loadFile(dir.filePath("missing-preset.xsie"));
    QVERIFY2(status->text().contains("Reading preset"), qPrintable(status->text()));                          // loading
    QVERIFY(!apply->isEnabled());
    QTRY_VERIFY(status->text().contains("preset-read-failed"));                                             // error
    QVERIFY2(status->text().contains("could not be read"), qPrintable(status->text()));                        // error
    xerahs::effects::EffectPreset preset;
    preset.name = "Fixture preset";
    xerahs::effects::PresetStep step;
    step.id = "brightness"; step.supported = true; step.enabled = true;
    step.parameters.insert("amount", QVariant(0.0));
    preset.steps.append(step);
    dialog.setPreset(preset);
    QCOMPARE(steps->count(), 1);                                                                           // populated
    QVERIFY2(steps->item(0)->text().contains("Supported"), qPrintable(steps->item(0)->text()));               // populated
    QVERIFY2(status->text().contains("Apply runs supported"), qPrintable(status->text()));                    // populated
    QVERIFY(apply->isEnabled());                                                                           // populated
    QVERIFY(exportButton->isEnabled());                                                                    // populated
    apply->click();
    QVERIFY2(status->text().contains("Applying preset"), qPrintable(status->text()));                         // loading
    QTRY_VERIFY(status->text().contains("preset-commit-stale"));                                            // failure
    QVERIFY2(status->text().contains("document changed"), qPrintable(status->text()));                         // failure
    xerahs::effects::EffectPreset unsupported;
    xerahs::effects::PresetStep unknown;
    unknown.id = "legacy_unknown_step"; unknown.supported = false; unknown.enabled = true;
    unsupported.steps.append(unknown);
    dialog.setPreset(unsupported);
    QCOMPARE(steps->count(), 1);
    QVERIFY2(steps->item(0)->text().contains("Unsupported"), qPrintable(steps->item(0)->text()));              // validation
    QVERIFY2(!apply->isEnabled(), "a preset with no supported step cannot be applied");                        // validation
    xerahs::effects::EffectPreset absent;
    xerahs::effects::PresetStep missing;
    missing.id = "remove_background"; missing.supported = false; missing.enabled = true;
    absent.steps.append(missing);
    dialog.setPreset(absent);
    QVERIFY2(steps->item(0)->text().contains("effect-unavailable"), qPrintable(steps->item(0)->text()));       // unavailable
    QVERIFY2(steps->item(0)->text().contains("remove_background"), qPrintable(steps->item(0)->text()));         // unavailable
    QVERIFY2(!apply->isEnabled(), "an unavailable effect keeps Apply disabled");                               // unavailable
  }
  // Insert image: initial, empty, loading, validation, error, failure, populated.
  // denied: n/a because the dialog asks for no permission; it reads a file or the clipboard the user offered.
  // unavailable: n/a because the dialog has no optional dependency; its decoder is always built in.
  // cancelled: n/a because the only cancel action closes the dialog and abandons the insertion.
  // The file picker is never opened: every load goes through the public loadFile() entry point.
  void imageInsertDialogStates() {
    QImage asset(4, 2, QImage::Format_ARGB32); asset.fill(Qt::green);
    QVERIFY(asset.save(dir.filePath("insert.png")));
    {
      QPointer<ImageInsertDialog> dialog(new ImageInsertDialog);
      dialog->show();
      auto *status = dialog->findChild<QLabel *>("insertImageStatus");
      auto *file = dialog->findChild<QPushButton *>("insertImageFile");
      auto *clipboardButton = dialog->findChild<QPushButton *>("insertImageClipboard");
      auto *progress = dialog->findChild<QProgressBar *>("insertImageProgress");
      QVERIFY(status && file && clipboardButton && progress);
      QVERIFY2(status->text().contains("Choose a source"), qPrintable(status->text()));                        // initial
      QVERIFY(file->isEnabled() && clipboardButton->isEnabled());
      QApplication::clipboard()->setText("no image on the clipboard");
      dialog->useClipboard();
      QVERIFY2(status->text().contains("no image"), qPrintable(status->text()));                              // empty
      QVERIFY(file->isEnabled());
      QImage oversized(16385, 1, QImage::Format_ARGB32); oversized.fill(Qt::red);
      QApplication::clipboard()->setImage(oversized);
      dialog->useClipboard();
      QVERIFY2(status->text().contains("exceeds"), qPrintable(status->text()));                               // validation
      QVERIFY(file->isEnabled());
      if (dialog) delete dialog.data();
    }
    {
      QPointer<ImageInsertDialog> dialog(new ImageInsertDialog);
      dialog->show();
      QFile bad(dir.filePath("broken.png")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.write("not an image"); bad.close();
      QVERIFY(dialog->loadFile(bad.fileName()));
      QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("insertImageStatus")->text().contains("source-unsupported"), 5000);  // error
      QVERIFY(!dialog->busy());
      if (dialog) delete dialog.data();
    }
    {
      QPointer<ImageInsertDialog> dialog(new ImageInsertDialog(nullptr, {[] { throw std::runtime_error("private insertion detail"); }}));
      dialog->show();
      QVERIFY(dialog->loadFile(dir.filePath("insert.png")));
      QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QLabel *>("insertImageStatus")->text().contains("image-insert-failed"), 5000);  // failure
      QVERIFY(!dialog->findChild<QLabel *>("insertImageStatus")->text().contains("private"));                  // failure: the internal detail stays private
      if (dialog) delete dialog.data();
    }
    {
      const auto gate = std::make_shared<WorkGate>();
      QPointer<ImageInsertDialog> dialog(new ImageInsertDialog(nullptr, {[gate] {
        gate->entered.store(true);
        while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }}));
      dialog->show();
      QVERIFY(dialog->loadFile(dir.filePath("insert.png")));
      auto *status = dialog->findChild<QLabel *>("insertImageStatus");
      QVERIFY2(status->text().contains("Validating"), qPrintable(status->text()));                             // loading
      QVERIFY(!dialog->findChild<QPushButton *>("insertImageFile")->isEnabled());                              // loading: the sources are locked while the image is prepared
      QTRY_VERIFY(gate->entered.load());
      gate->released.store(true);
      QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    }
    {
      QPointer<ImageInsertDialog> dialog(new ImageInsertDialog);
      dialog->show();
      bool accepted = false, prepared = false; QString inserted;
      QObject::connect(dialog.data(), &QDialog::accepted, dialog.data(), [&] { accepted = true; prepared = dialog->choice().valid(); inserted = dialog->findChild<QLabel *>("insertImageStatus")->text(); });
      QVERIFY(dialog->loadFile(dir.filePath("insert.png")));
      QTRY_VERIFY(accepted);
      QVERIFY2(inserted == QStringLiteral("Image inserted."), qPrintable(inserted));                           // populated: the insertion is reported in words
      QVERIFY2(prepared, "a valid image is prepared and handed over");                                        // populated
    }
  }
};
QTEST_MAIN(InterfaceStatesTest)
#include "InterfaceStatesTest.moc"
