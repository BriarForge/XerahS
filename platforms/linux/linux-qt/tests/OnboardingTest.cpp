// CORE-008: first-run onboarding. Resumable, skippable, accessible; declining
// every optional step leaves local capture on its defaults.
#include "CaptureActions.h"
#include "CapturePipeline.h"
#include "CommandRegistry.h"
#include "OnboardingProgress.h"
#include "OnboardingWizard.h"
#include "settings/ApplicationSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;

namespace {

struct Harness {
  QTemporaryDir dir;
  xerahs::settings::ApplicationSettingsStore settings;
  QList<OnboardingState> persisted;
  QStringList commands;
  int testCaptures = 0, saves = 0;
  OnboardingPorts ports;

  Harness() : settings(dir.filePath("settings.json"), dir.filePath("Pictures")) {
    ports.defaultFolder = dir.filePath("Pictures");
    ports.settings = [this] { return settings.current().values; };
    ports.saveSettings = [this](const QJsonObject &values) { ++saves; return settings.save(values); };
    ports.defaultWorkflow = [this] {
      return OnboardingWorkflowInfo{{"save", "image-clipboard"}, settings.current().values.value("saveFolder").toString(), {}};
    };
    ports.testCapture = [this] { ++testCaptures; };
    ports.runCommand = [this](const QString &id) { commands.append(id); };
    ports.persist = [this](const OnboardingState &state) { persisted.append(state); };
  }
};

QString slurp(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }

}  // namespace

class OnboardingTest final : public QObject {
  Q_OBJECT
private slots:
  void walkingThroughRecordsEveryPageAndFinishIsFinal() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show();
    QCOMPARE(wizard.currentPageName(), QString("welcome"));
    for (int i = 0; i < onboardingPages().size() - 1; ++i) wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("summary"));
    QVERIFY(!wizard.state().finished);
    QVERIFY(wizard.button(QWizard::FinishButton)->isVisible());
    wizard.button(QWizard::FinishButton)->click();
    QVERIFY(wizard.state().finished);
    QCOMPARE(wizard.state().completed, onboardingPages());
    QVERIFY(wizard.state().skipped.isEmpty());
    QVERIFY(h.persisted.last().finished);
  }

  void onlyOptionalPagesCanBeSkipped() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    for (const auto &page : {"welcome", "telemetry", "summary"}) QVERIFY2(!wizard.skippable(page), page);
    for (const auto &page : {"capture-permission", "save-location", "hotkeys", "uploads"}) QVERIFY2(wizard.skippable(page), page);
    wizard.show();
    QVERIFY(!wizard.button(QWizard::CustomButton1)->isEnabled());  // welcome
    wizard.skipCurrent();
    QCOMPARE(wizard.currentPageName(), QString("welcome"));
    wizard.next();
    QVERIFY(wizard.button(QWizard::CustomButton1)->isEnabled());
    wizard.skipCurrent();
    QCOMPARE(wizard.currentPageName(), QString("save-location"));
    QCOMPARE(wizard.state().skipped, QStringList{"capture-permission"});
  }

  void finishLaterKeepsPlaceAndDoesNotComplete() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); wizard.next(); wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("save-location"));
    QSignalSpy rejected(&wizard, &QDialog::rejected);
    wizard.button(QWizard::CustomButton2)->click();
    QCOMPARE(rejected.count(), 1);
    QVERIFY(!wizard.state().finished);
    QCOMPARE(h.persisted.last().page, QString("save-location"));
    QCOMPARE(h.persisted.last().completed, (QStringList{"welcome", "capture-permission"}));
  }

  void progressRoundTripsAndResumes() {
    QTemporaryDir dir; const auto path = dir.filePath("onboarding.json");
    OnboardingState state; state.page = "hotkeys"; state.completed = {"welcome", "capture-permission"}; state.skipped = {"save-location"};
    {
      OnboardingStore store(path);
      QCOMPARE(store.load().existed, false);
      QVERIFY(store.save(state).isEmpty());
      state.completed.append("hotkeys");
      QVERIFY(store.save(state).isEmpty());  // second write reuses the snapshot
    }
    OnboardingStore restarted(path);
    const auto load = restarted.load();
    QVERIFY(load.existed); QVERIFY(load.writable); QVERIFY(load.diagnostic.isEmpty());
    QCOMPARE(load.state, state);
    const auto envelope = QJsonDocument::fromJson(slurp(path).toUtf8()).object();
    QCOMPARE(envelope.value("format").toString(), QString("com.xerahs.app/onboarding"));
    QCOMPARE(envelope.value("version").toInt(), 1);
    QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther), QFileDevice::Permissions());
    Harness h; OnboardingWizard wizard(h.ports, load.state);
    wizard.show();
    QCOMPARE(wizard.currentPageName(), QString("hotkeys"));
    QCOMPARE(wizard.state().completed, load.state.completed);
  }

  void corruptStoreStartsFreshAndIsNeverOverwritten() {
    QTemporaryDir dir; const auto path = dir.filePath("onboarding.json");
    for (const QByteArray &bytes : {QByteArray("not json"),
         QByteArray(R"({"format":"com.xerahs.app/onboarding","version":1,"page":"nowhere","completed":[],"skipped":[],"finished":false})"),
         QByteArray(R"({"format":"com.xerahs.app/onboarding","version":1,"page":"hotkeys","completed":["welcome","welcome"],"skipped":[],"finished":false})")}) {
      QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); f.write(bytes); f.close(); f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
      OnboardingStore store(path); const auto load = store.load();
      QCOMPARE(load.diagnostic, QString("onboarding-store-invalid"));
      QVERIFY(!load.writable); QCOMPARE(load.state, OnboardingState());
      QVERIFY(!autoOpenOnboarding(true, load));
      QVERIFY(!store.save(load.state).isEmpty());
      QCOMPARE(slurp(path).toUtf8(), bytes);
      QVERIFY(!onboardingDiagnosticMessage(load.diagnostic).isEmpty());
    }
  }

  void newerStoreStartsFreshReportsAndIsPreserved() {
    QTemporaryDir dir; const auto path = dir.filePath("onboarding.json");
    xerahs::storage::StoreOptions options; options.format = "com.xerahs.app/onboarding"; options.maxVersion = 2;
    QCOMPARE(xerahs::storage::PrivateJsonFile(path, options).write({{"page", "uploads"}, {"future", true}}, 2, {}, nullptr), xerahs::storage::StoreStatus::Ok);
    const auto before = slurp(path);
    OnboardingStore store(path); const auto load = store.load();
    QCOMPARE(load.diagnostic, QString("onboarding-store-newer"));
    QVERIFY(!load.writable); QCOMPARE(load.state, OnboardingState());
    QVERIFY(onboardingDiagnosticMessage(load.diagnostic).contains("newer"));
    QVERIFY(!store.save(load.state).isEmpty());
    QCOMPARE(slurp(path), before);
    // The wizard still runs, in memory, and shows why progress is not saved.
    Harness h; OnboardingWizard wizard(h.ports, load.state, onboardingDiagnosticMessage(load.diagnostic));
    wizard.show();
    QVERIFY(wizard.findChild<QLabel *>() != nullptr);
    bool shown = false;
    for (auto *label : wizard.findChildren<QLabel *>()) shown |= label->accessibleName() == "Setup guide progress warning" && label->text().contains("newer");
    QVERIFY(shown);
  }

  void changedFileOnDiskIsNotSilentlyOverwritten() {
    QTemporaryDir dir; const auto path = dir.filePath("onboarding.json");
    OnboardingStore a(path), b(path); a.load(); b.load();
    OnboardingState state; state.page = "hotkeys";
    QVERIFY(a.save(state).isEmpty());
    QCOMPARE(b.save(state), QString("onboarding-store-changed"));
  }

  void completionStopsAutoOpenAndHeadlessNeverOpens() {
    OnboardingLoad fresh;
    QVERIFY(autoOpenOnboarding(true, fresh));
    QVERIFY(!autoOpenOnboarding(false, fresh));  // --run-workflow, --edit, --capture-*, forwarded or headless
    OnboardingLoad partway = fresh; partway.state.page = "uploads"; partway.existed = true;
    QVERIFY(autoOpenOnboarding(true, partway));  // resumes
    OnboardingLoad done = partway; done.state.finished = true;
    QVERIFY(!autoOpenOnboarding(true, done));
    QVERIFY(!autoOpenOnboarding(false, done));
    // Completion survives a restart.
    QTemporaryDir dir; OnboardingStore store(dir.filePath("o.json")); store.load();
    QVERIFY(store.save(done.state).isEmpty());
    OnboardingStore restarted(dir.filePath("o.json"));
    QVERIFY(!autoOpenOnboarding(true, restarted.load()));
  }

  void reopenAfterCompletionStartsAtWelcomeAndStaysFinished() {
    Harness h; OnboardingState done; done.finished = true; done.page = "summary"; done.completed = onboardingPages();
    OnboardingWizard wizard(h.ports, done);
    wizard.show();
    QCOMPARE(wizard.currentPageName(), QString("welcome"));
    wizard.finishLater();
    QVERIFY(h.persisted.last().finished);
  }

  void setupGuideIsRegisteredOnceInPreferences() {
    CommandRegistry registry;
    QVERIFY(registry.add(setupGuideCommand()));
    QVERIFY(!registry.add(setupGuideCommand()));
    const auto *spec = registry.spec("setup-guide");
    QVERIFY(spec); QCOMPARE(int(spec->group), int(CommandGroup::Preferences));
    QCOMPARE(spec->label, QString("Setup guide…"));
    QCOMPARE(registry.ids(CommandSurface::Tray).count("setup-guide"), 1);
    QCOMPARE(registry.ids(CommandSurface::PreferencesMenu).count("setup-guide"), 1);
  }

  void saveLocationReusesSettingsValidationAndStore() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); wizard.next(); wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("save-location"));
    QLineEdit *folder = nullptr, *pattern = nullptr;
    for (auto *edit : wizard.currentPage()->findChildren<QLineEdit *>()) (edit->accessibleName() == "Default save folder" ? folder : pattern) = edit;
    QVERIFY(folder && pattern);
    QCOMPARE(folder->text(), h.dir.filePath("Pictures"));
    pattern->clear();  // an empty pattern is invalid in Settings too
    wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("save-location"));  // blocked
    QCOMPARE(h.saves, 0);
    pattern->setText("shot-%y%mo%d");
    folder->setText(h.dir.filePath("Shots"));
    wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("hotkeys"));
    QCOMPARE(h.saves, 1);
    QCOMPARE(h.settings.current().values.value("filenamePattern").toString(), QString("shot-%y%mo%d"));
    QCOMPARE(h.settings.current().values.value("saveFolder").toString(), h.dir.filePath("Shots"));
    QVERIFY(wizard.state().completed.contains("save-location"));
  }

  void testCaptureReportsDenialHonestlyAndRestoresTheWindow() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("capture-permission"));
    QPushButton *test = nullptr; QLabel *status = nullptr;
    for (auto *b : wizard.currentPage()->findChildren<QPushButton *>()) if (b->accessibleName() == "Test capture now") test = b;
    for (auto *l : wizard.currentPage()->findChildren<QLabel *>()) if (l->accessibleName() == "Test capture result") status = l;
    QVERIFY(test && status);
    test->click();
    QVERIFY(wizard.testCapturePending());
    QVERIFY(!wizard.isVisible());  // hidden so it is not in the screenshot
    QTRY_COMPARE(h.testCaptures, 1);
    wizard.reportCaptureResult(false, "Screenshot permission was denied by the desktop.");
    QVERIFY(!wizard.testCapturePending());
    QVERIFY(wizard.isVisible());
    QVERIFY(status->text().startsWith("Capture did not succeed."));
    QVERIFY(status->text().contains("denied by the desktop"));
    test->click();
    QTRY_COMPARE(h.testCaptures, 2);
    wizard.reportCaptureResult(true, "Saved to /x.png.");
    QVERIFY(status->text().startsWith("Capture worked."));
    wizard.reportCaptureResult(false, "late duplicate");  // ignored when nothing is pending
    QVERIFY(status->text().startsWith("Capture worked."));
  }

  void hotkeysPageListsActionsAndOpensExistingFlows() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); wizard.next(); wizard.next(); wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("hotkeys"));
    auto *table = wizard.currentPage()->findChild<QTableWidget *>();
    QVERIFY(table); QCOMPARE(table->rowCount(), int(xerahs::settings::captureHotkeyActions().size()));
    for (auto *b : wizard.currentPage()->findChildren<QPushButton *>()) if (b->accessibleName() == "Enable workflow shortcuts") b->click();
    QCOMPARE(h.commands, QStringList{"enable-workflow-shortcuts"});
    wizard.next(); wizard.next();  // uploads, telemetry
    QCOMPARE(wizard.currentPageName(), QString("telemetry"));
  }

  void uploadsPageOpensDestinationsAndTelemetryStatesNoTelemetry() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); for (int i = 0; i < 4; ++i) wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("uploads"));
    for (auto *b : wizard.currentPage()->findChildren<QPushButton *>()) b->click();
    QCOMPARE(h.commands, (QStringList{"upload-destinations", "upload-destinations"}));
    wizard.next();
    bool says = false;
    for (auto *l : wizard.currentPage()->findChildren<QLabel *>()) says |= l->text().contains("no telemetry");
    QVERIFY(says);
  }

  void summaryStatesWhatWasDoneAndTheDefaultConsequence() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show(); wizard.next(); wizard.skipCurrent();
    for (int i = 0; i < 5; ++i) wizard.next();
    QCOMPARE(wizard.currentPageName(), QString("summary"));
    QString text;
    for (auto *l : wizard.currentPage()->findChildren<QLabel *>()) if (l->accessibleName() == "Setup summary") text = l->text();
    QVERIFY(text.contains("Screen capture permission: skipped"));
    QVERIFY(text.contains("Welcome: reviewed"));
    QVERIFY(text.contains("Captures are saved to " + h.dir.filePath("Pictures") + " and copied to the clipboard; nothing is uploaded."));
  }

  void describeDefaultWorkflowFollowsTheActions() {
    QCOMPARE(describeDefaultWorkflow({{"save", "image-clipboard"}, "/p", {}}), QString("Captures are saved to /p and copied to the clipboard; nothing is uploaded."));
    QCOMPARE(describeDefaultWorkflow({{"save"}, "/p", {}}), QString("Captures are saved to /p; nothing is uploaded."));
    QVERIFY(describeDefaultWorkflow({{"save", "upload"}, "/p", {}}).endsWith("uploaded to your default destination."));
    QVERIFY(describeDefaultWorkflow({{}, "/p", {}}).contains("nothing is saved"));
  }

  void decliningEverythingLeavesDefaultCaptureWorking() {
    Harness h;
    const auto before = h.settings.current().values;
    OnboardingWizard wizard(h.ports, {});
    wizard.show();
    while (wizard.currentPageName() != "summary") wizard.skippable(wizard.currentPageName()) ? wizard.skipCurrent() : wizard.next();
    wizard.button(QWizard::FinishButton)->click();
    QVERIFY(wizard.state().finished);
    QCOMPARE(wizard.state().skipped, (QStringList{"capture-permission", "save-location", "hotkeys", "uploads"}));
    QCOMPARE(h.settings.current().values, before);
    QCOMPARE(h.saves, 0); QCOMPARE(h.testCaptures, 0); QVERIFY(h.commands.isEmpty());

    // The default workflow still saves and copies, with nothing uploaded.
    auto journal = std::make_shared<NativePipelineJournal>(h.dir.filePath("state"));
    CaptureActionPorts ports; int copied = 0, uploaded = 0;
    ports.clipboard = [&](const QImage &, const xerahs::actions::CancellationToken &) { ++copied; return xerahs::actions::ActionOutcome{}; };
    ports.uploadCatalogPath = h.dir.filePath("none.json");
    QImage image(12, 8, QImage::Format_ARGB32); image.fill(QColor(1, 2, 3));
    const auto info = h.ports.defaultWorkflow();
    CapturePipeline pipeline(image, h.settings.current().values.value("saveFolder").toString(), "after-onboarding", info.actions, nullptr, ports, journal);
    QSignalSpy done(&pipeline, &CapturePipeline::finished); pipeline.start(); QTRY_COMPARE(done.count(), 1);
    const auto receipt = qvariant_cast<CapturePipelineReceipt>(done.first().first());
    QVERIFY(receipt->file); QVERIFY(QFile::exists(receipt->file->path)); QCOMPARE(QImage(receipt->file->path), image);
    QVERIFY(receipt->file->path.startsWith(h.dir.filePath("Pictures")));
    QCOMPARE(copied, 1); QCOMPARE(uploaded, 0);
    QVERIFY(!info.actions.contains("upload"));
  }

  void everyControlHasAnAccessibleName() {
    Harness h; OnboardingWizard wizard(h.ports, {});
    wizard.show();
    QVERIFY(!wizard.accessibleName().isEmpty());
    for (int i = 0; i < onboardingPages().size(); ++i) {
      QVERIFY(!wizard.currentPage()->accessibleName().isEmpty());
      for (auto *w : wizard.currentPage()->findChildren<QWidget *>()) {
        QString name = w->accessibleName();
        if (auto *b = qobject_cast<QPushButton *>(w)) name += b->text();
        if (auto *l = qobject_cast<QLabel *>(w)) name += l->text();
        const bool interactive = qobject_cast<QPushButton *>(w) || qobject_cast<QLineEdit *>(w) || qobject_cast<QTableWidget *>(w);
        if (interactive || qobject_cast<QLabel *>(w))
          QVERIFY2(!name.trimmed().isEmpty() || w->objectName().startsWith("qt_"), qPrintable(currentPageLabel(wizard) + ": " + w->metaObject()->className()));
        if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTableWidget *>(w))
          if (!w->objectName().startsWith("qt_")) QVERIFY2(!w->accessibleName().isEmpty(), qPrintable(w->metaObject()->className()));
      }
      if (i + 1 < onboardingPages().size()) wizard.next();
    }
    for (auto id : {QWizard::NextButton, QWizard::BackButton, QWizard::CustomButton1, QWizard::CustomButton2}) {
      auto *b = wizard.button(id); QVERIFY(!(b->accessibleName() + b->text()).isEmpty());
    }
  }

private:
  static QString currentPageLabel(const OnboardingWizard &w) { return w.currentPageName(); }
};

QTEST_MAIN(OnboardingTest)
#include "OnboardingTest.moc"
