#include "NativeEditorPreferences.h"
#include "RecentImageMenu.h"
#include "DrawingDefaultsDialog.h"
#include "ConveniencePreferencesDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include "image-editor/ShapePainter.h"
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLockFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QByteArray bytes(const QString &path) {
  QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool writePrivate(const QString &path, const QByteArray &data) {
  QFile file(path); return QDir().mkpath(QFileInfo(path).absolutePath()) && QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner) &&
      file.open(QIODevice::WriteOnly) && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) && file.write(data) == data.size();
}
QByteArray textEnvelope(const QJsonObject &current) {
  return QJsonDocument(QJsonObject{{"format", "com.xerahs.app/linux-qt/editor-settings"}, {"version", 1}, {"current", current}, {"previous", QJsonObject{}}}).toJson(QJsonDocument::Indented);
}
// Opens the Drawing Defaults dialog, lets `edit` change the draft, then Apply or Cancel.
bool runDrawingDialog(const std::function<void(QDialog &)> &edit, bool apply) {
  bool entered = false; QTimer timer; QElapsedTimer deadline; deadline.start();
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog || entered) return;
    entered = true; edit(*dialog);
    dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
  });
  timer.start(5); const bool result = manageDrawingDefaults(); timer.stop(); return entered && result;
}
bool optIn(int maximum = 10) {
  auto draft = recentImageHistory(); const auto revision = draft.revision();
  return draft.setPreferences(true, maximum) && applyRecentImagePreferences(draft, revision).saved;
}
void changeDialog(const QString &operation, bool apply) {
  auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
  QCOMPARE(dialog->objectName(), QStringLiteral("recentImagesDialog"));
  auto *privacy = dialog->findChild<QLabel *>(QStringLiteral("recentImagesPrivacy"));
  QVERIFY(privacy && privacy->text().contains(QStringLiteral("saved privately")));
  if (operation == u"remove") {
    dialog->findChild<QListWidget *>(QStringLiteral("recentImagePaths"))->setCurrentRow(1);
    dialog->findChild<QPushButton *>(QStringLiteral("removeRecentImage"))->click();
  } else if (operation == u"clear") dialog->findChild<QPushButton *>(QStringLiteral("clearRecentImages"))->click();
  else if (operation == u"disable") dialog->findChild<QCheckBox *>(QStringLiteral("rememberRecentImages"))->setChecked(false);
  else if (operation == u"trim") dialog->findChild<QSpinBox *>(QStringLiteral("maximumRecentImages"))->setValue(1);
  else if (operation == u"enable") dialog->findChild<QCheckBox *>(QStringLiteral("rememberRecentImages"))->setChecked(true);
  dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
}
}
class NativeEditorPreferencesTest : public QObject {
  Q_OBJECT
private slots:
  void convenienceApplyPersistsAndRejectsStaleInvalidAndFailedWrites() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    FilePreferenceStorage storage(path); EditorPreferenceStore seed(storage);
    const QJsonObject future{{"mode", "preserved"}};
    auto prepared = changeEditorPreferences(seed.preferences(), {{"FutureConvenience", future}, {"Thickness", 9}});
    QVERIFY(prepared.preferences); QVERIFY(seed.save(*prepared.preferences).saved);
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn());
    QVERIFY(rememberEditorImage("/private/keep.png").isEmpty());
    const auto recentRevision = recentImageHistory().revision(); const auto revision = nativeEditorPreferenceRevision();
    const QJsonObject changes{{"AutoCopyImageToClipboard", true}, {"AutoCloseEditorOnTask", true},
        {"ZoomToFitOnOpen", true}, {"ShowExitConfirmation", true}};
    QVERIFY(applyConveniencePreferences(changes, revision).saved);
    QCOMPARE(recentImageHistory().revision(), recentRevision);
    const auto before = bytes(path); const auto current = nativeEditorPreferenceRevision();
    const auto stale = applyConveniencePreferences({{"AutoCloseEditorOnTask", false}}, revision);
    QVERIFY(!stale.saved); QCOMPARE(stale.diagnostic, QString("settings-changed-in-session"));
    const auto invalid = applyConveniencePreferences({{"ZoomToFitOnOpen", "yes"}}, current);
    QVERIFY(!invalid.saved); QCOMPARE(invalid.diagnostic, QString("invalid-ZoomToFitOnOpen"));
    const auto unknown = applyConveniencePreferences({{"Thickness", 10}}, current);
    QVERIFY(!unknown.saved); QCOMPARE(unknown.diagnostic, QString("invalid-convenience-preferences"));
    QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
    QVERIFY(!applyConveniencePreferences({{"AutoCloseEditorOnTask", false}}, current).saved);
    QCOMPARE(bytes(path), before); QCOMPARE(nativeEditorPreferenceRevision(), current);
    QVERIFY(nativeEditorPreferences().values.value("AutoCloseEditorOnTask").toBool()); lock.unlock();
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    for (auto it = changes.begin(); it != changes.end(); ++it) QCOMPARE(nativeEditorPreferences().values.value(it.key()), it.value());
    QCOMPARE(nativeEditorPreferences().values.value("FutureConvenience"), QJsonValue(future));
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(9));
    QCOMPARE(recentImageHistory().paths(), QStringList{"/private/keep.png"});
  }
  void convenienceDialogApplyCancelAndFailedPersistence_data() {
    QTest::addColumn<QString>("mode");
    QTest::newRow("apply") << QString("apply");
    QTest::newRow("cancel") << QString("cancel");
    QTest::newRow("write-failure") << QString("write-failure");
    QTest::newRow("stale-draft") << QString("stale-draft");
  }
  void convenienceDialogApplyCancelAndFailedPersistence() {
    QFETCH(QString, mode);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyConveniencePreferences({{"AutoCopyImageToClipboard", false}}, nativeEditorPreferenceRevision()).saved);
    const auto before = bytes(path); bool visited = false, failureReported = false;
    QLockFile lock(path + ".lock");
    if (mode == "write-failure") QVERIFY(lock.tryLock(0));
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      QCOMPARE(dialog->objectName(), QString("conveniencePreferencesDialog")); visited = true;
      for (const auto &key : conveniencePreferenceFields()) {
        auto *check = dialog->findChild<QCheckBox *>(key); QVERIFY(check); QVERIFY(!check->accessibleName().isEmpty()); check->setChecked(true);
      }
      auto *exit = dialog->findChild<QCheckBox *>("ShowExitConfirmation");
      QVERIFY(exit->accessibleDescription().contains("Unsaved changes always"));
      if (mode == "stale-draft") QVERIFY(applyDrawingDefaults({{"Thickness", 9}}, nativeEditorPreferenceRevision()).saved);
      auto *buttons = dialog->findChild<QDialogButtonBox *>();
      buttons->button(mode == "cancel" ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)->click();
      if (mode == "write-failure" || mode == "stale-draft") {
        QVERIFY(dialog->isVisible()); failureReported = !dialog->findChild<QLabel *>("conveniencePreferencesError")->text().isEmpty();
        buttons->button(QDialogButtonBox::Cancel)->click();
      }
    });
    QCOMPARE(manageConveniencePreferences(), mode == "apply"); QVERIFY(visited);
    QCOMPARE(failureReported, mode == "write-failure" || mode == "stale-draft");
    for (const auto &key : conveniencePreferenceFields()) QCOMPARE(nativeEditorPreferences().values.value(key).toBool(), mode == "apply");
    if (mode != "apply" && mode != "stale-draft") QCOMPARE(bytes(path), before);
    if (mode == "stale-draft") QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(9));
    lock.unlock(); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    for (const auto &key : conveniencePreferenceFields()) QCOMPARE(nativeEditorPreferences().values.value(key).toBool(), mode == "apply");
  }
  void windowChangesKeepRecentsAndUnrelatedFieldsWhileRejectingInvalidOrStaleWrites() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    FilePreferenceStorage storage(path); EditorPreferenceStore seed(storage);
    const QJsonObject future{{"mode", "preserved"}};
    auto prepared = changeEditorPreferences(seed.preferences(), {{"Theme", "Light"}, {"Thickness", 8}, {"FutureOption", future},
        {"RememberRecentImagePaths", true}, {"MaxRecentImageFiles", 1}, {"RecentImageFiles", QJsonArray{"/private/first.png"}}});
    QVERIFY(prepared.preferences); QVERIFY(seed.save(*prepared.preferences).saved);
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto recentRevision = recentImageHistory().revision(); const auto oldPreferenceRevision = nativeEditorPreferenceRevision();
    QVERIFY(rememberEditorWindowState({{"WindowWidth", 700}, {"WindowHeight", 500}, {"IsWindowMaximized", false}}).isEmpty());
    QCOMPARE(recentImageHistory().revision(), recentRevision); QCOMPARE(recentImageHistory().paths(), QStringList{"/private/first.png"});
    const auto after = bytes(path);
    const auto stale = applyWindowPreferences({{"RememberWindowState", false}}, oldPreferenceRevision);
    QVERIFY(!stale.saved); QCOMPARE(stale.diagnostic, QStringLiteral("settings-changed-in-session"));
    const auto invalid = applyWindowPreferences({{"WindowHeight", 0}, {"WindowWidth", 800}}, nativeEditorPreferenceRevision());
    QVERIFY(!invalid.saved); QCOMPARE(invalid.diagnostic, QStringLiteral("invalid-WindowHeight")); QCOMPARE(bytes(path), after);
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QCOMPARE(windowPreferencesFromPreferences(nativeEditorPreferences()).normalSize, QSizeF(700, 500));
    QCOMPARE(nativeEditorPreferences().values.value("Theme"), QJsonValue("Light"));
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(8));
    QCOMPARE(nativeEditorPreferences().values.value("FutureOption"), QJsonValue(future));
    QCOMPARE(recentImageHistory().paths(), QStringList{"/private/first.png"});
  }
  void windowOptOutStopsAutomaticUpdatesAndSurvivesPreviousRecordRecovery() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(rememberEditorWindowState({{"WindowWidth", 700}, {"WindowHeight", 500}, {"IsWindowMaximized", false}}).isEmpty());
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::windowMemoryChanged);
    QVERIFY(applyWindowPreferences({{"RememberWindowState", false}}, nativeEditorPreferenceRevision()).saved);
    QCOMPARE(events.count(), 1); QCOMPARE(events.at(0).at(0).toBool(), false);
    const auto disabled = bytes(path); const auto revision = nativeEditorPreferenceRevision();
    QVERIFY(rememberEditorWindowState({{"WindowWidth", 900}, {"WindowHeight", 600}, {"IsWindowMaximized", true}}).isEmpty());
    QCOMPARE(bytes(path), disabled); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    auto document = QJsonDocument::fromJson(disabled).object();
    for (const auto *version : {"current", "previous"}) {
      const auto record = document.value(version).toObject(); QVERIFY(!record.value("RememberWindowState").toBool());
      for (const auto *key : {"WindowWidth", "WindowHeight", "IsWindowMaximized"}) QVERIFY(!record.contains(key));
    }
    document.insert("current", "damaged current"); QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    const auto recoveredBytes = QJsonDocument(document).toJson(); QCOMPARE(file.write(recoveredBytes), recoveredBytes.size()); file.close();
    QCOMPARE(initializeNativeEditorPreferences(path), QStringList{"recovered-previous-settings"});
    QVERIFY(!windowPreferencesFromPreferences(nativeEditorPreferences()).remember);
    QVERIFY(rememberEditorWindowState({{"WindowWidth", 900}, {"WindowHeight", 600}, {"IsWindowMaximized", true}}).isEmpty());
    QCOMPARE(bytes(path), recoveredBytes);
  }
  void drawingDraftApplyCancelAndReset_data() {
    QTest::addColumn<bool>("apply"); QTest::addColumn<bool>("reset");
    for (bool apply : {false, true}) for (bool reset : {false, true})
      QTest::newRow(qPrintable(QString("%1-%2").arg(apply ? "apply" : "cancel", reset ? "reset" : "edit"))) << apply << reset;
  }
  void drawingDraftApplyCancelAndReset() {
    QFETCH(bool, apply); QFETCH(bool, reset);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyDrawingDefaults({{"BorderColorHex", "#11223344"}, {"FillColorHex", "#80556677"}, {"Thickness", 7}}, nativeEditorPreferenceRevision()).saved);
    const auto original = nativeEditorPreferences().values; const auto previousBytes = bytes(path);
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool edited = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      if (edited) return; edited = true;
      QCOMPARE(dialog->objectName(), QStringLiteral("drawingDefaultsDialog"));
      dialog->findChild<QLineEdit *>("defaultStrokeColour")->setText("#40667788");
      dialog->findChild<QLineEdit *>("defaultFillColour")->setText("#008899aa");
      dialog->findChild<QSpinBox *>("defaultStrokeThickness")->setValue(10);
      if (reset) dialog->findChild<QPushButton *>("resetDrawingDefaults")->click();
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
      buttons->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    timer.start(5); QCOMPARE(manageDrawingDefaults(), apply); timer.stop(); QVERIFY(edited && !timedOut);
    QCOMPARE(events.count(), apply ? 1 : 0);
    if (!apply) { QCOMPARE(nativeEditorPreferences().values, original); QCOMPARE(bytes(path), previousBytes); }
    else {
      QCOMPARE(nativeEditorPreferences().values.value("BorderColorHex"), QJsonValue(reset ? "#FFFF0000" : "#40667788"));
      QCOMPARE(nativeEditorPreferences().values.value("FillColorHex"), QJsonValue(reset ? "#00000000" : "#008899AA"));
      QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(reset ? 4 : 10));
    }
    const auto expected = nativeEditorPreferences().values;
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(nativeEditorPreferences().values, expected);
  }
  void missingSavedFontFallsBackWithoutRewritingUntilExplicitReplacement() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(writePrivate(path, textEnvelope({{"TextFontFamily", "Gone Sans"}, {"TextFontSize", 27.75}, {"TextHorizontalAlignment", "Right"},
        {"SpeechBalloonFontFamily", "Gone Sans"}, {"StepType", "FutureStyle9"}, {"FutureOption", true}})));
    const auto original = bytes(path); QVERIFY(!original.isEmpty());
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    // Use: resolve creation defaults repeatedly; the stored bytes never change.
    for (int i = 0; i < 3; ++i) {
      const auto defaults = contentDefaultsFromPreferences(nativeEditorPreferences(), nativeFontFamilyAvailable);
      QVERIFY(defaults.text.familyUnavailable); QVERIFY(defaults.text.fontFamily.isEmpty());
      QCOMPARE(defaults.text.requestedFamily, QStringLiteral("Gone Sans")); QCOMPARE(defaults.text.fontSize, 27.75);
      QCOMPARE(defaults.text.alignment, QStringLiteral("right")); QVERIFY(defaults.stepStyleInert);
    }
    QCOMPARE(bytes(path), original);
    // The dialog shows the unavailable family, and Cancel leaves the file alone.
    QVERIFY(!runDrawingDialog([](QDialog &dialog) {
      QVERIFY(dialog.findChild<QLabel *>("defaultTextFontUnavailable")); QVERIFY(dialog.findChild<QLabel *>("defaultBalloonFontUnavailable"));
      auto *family = dialog.findChild<QComboBox *>("defaultTextFontFamily"); QVERIFY(family);
      QCOMPARE(family->currentData().toString(), QStringLiteral("Gone Sans")); QVERIFY(family->currentText().contains("not installed"));
      QCOMPARE(dialog.findChild<QDoubleSpinBox *>("defaultTextFontSize")->value(), 27.75);
      QCOMPARE(dialog.findChild<QComboBox *>("defaultStepStyle")->currentData().toString(), QStringLiteral("FutureStyle9"));
      dialog.findChild<QSpinBox *>("defaultStrokeThickness")->setValue(9);
    }, false));
    QCOMPARE(bytes(path), original);
    // A replacement that is not installed is refused; the stored family stays.
    const auto refused = applyDrawingDefaults({{"TextFontFamily", "Another Missing Font"}}, nativeEditorPreferenceRevision());
    QVERIFY(!refused.saved); QCOMPARE(refused.diagnostic, QStringLiteral("font-family-unavailable")); QCOMPARE(bytes(path), original);
    QVERIFY(editorPreferenceDiagnosticMessage(refused.diagnostic).contains("not installed"));
    // Applying an unrelated change with the family untouched keeps the saved family and step style.
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    QVERIFY(runDrawingDialog([](QDialog &dialog) { dialog.findChild<QSpinBox *>("defaultStrokeThickness")->setValue(9); }, true));
    QCOMPARE(events.count(), 1);
    auto reloaded = QJsonDocument::fromJson(bytes(path)).object().value("current").toObject();
    QCOMPARE(reloaded.value("TextFontFamily"), QJsonValue("Gone Sans")); QCOMPARE(reloaded.value("TextFontSize"), QJsonValue(27.75));
    QCOMPARE(reloaded.value("StepType"), QJsonValue("FutureStyle9")); QCOMPARE(reloaded.value("FutureOption"), QJsonValue(true));
    QCOMPARE(reloaded.value("Thickness"), QJsonValue(9));
    // An explicit replacement through the dialog is the only thing that rewrites the family.
    const auto installed = QFontDatabase::families(); QVERIFY(!installed.isEmpty());
    QVERIFY(runDrawingDialog([&](QDialog &dialog) {
      auto *family = dialog.findChild<QComboBox *>("defaultTextFontFamily"); const int index = family->findData(installed.first()); QVERIFY(index >= 0);
      family->setCurrentIndex(index); dialog.findChild<QDoubleSpinBox *>("defaultTextFontSize")->setValue(40);
      dialog.findChild<QCheckBox *>("defaultTextBold")->setChecked(false); dialog.findChild<QCheckBox *>("defaultTextItalic")->setChecked(true);
      auto *align = dialog.findChild<QComboBox *>("defaultTextAlignment"); align->setCurrentIndex(align->findData("Left"));
    }, true));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto &values = nativeEditorPreferences().values;
    QCOMPARE(values.value("TextFontFamily").toString(), installed.first()); QCOMPARE(values.value("TextFontSize"), QJsonValue(40));
    QCOMPARE(values.value("TextBold"), QJsonValue(false)); QCOMPARE(values.value("TextItalic"), QJsonValue(true));
    QCOMPARE(values.value("TextHorizontalAlignment"), QJsonValue("Left"));
    QCOMPARE(values.value("SpeechBalloonFontFamily"), QJsonValue("Gone Sans"));
    QCOMPARE(values.value("StepType"), QJsonValue("FutureStyle9"));
    const auto resolved = contentDefaultsFromPreferences(nativeEditorPreferences(), nativeFontFamilyAvailable);
    QVERIFY(!resolved.text.familyUnavailable); QCOMPARE(resolved.text.fontFamily, installed.first()); QVERIFY(resolved.balloon.familyUnavailable);
  }
  void textDefaultsPublishOnlyAfterSuccessfulPersistenceAndRejectInvalidDraftsAtomically() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyDrawingDefaults({{"Thickness", 5}}, nativeEditorPreferenceRevision()).saved);
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    const auto original = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); const auto storedBytes = bytes(path);
    {
      QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
      const auto locked = applyDrawingDefaults({{"TextFontSize", 33.5}, {"StepFontSize", 20}}, revision);
      QVERIFY(!locked.saved); QCOMPARE(locked.diagnostic, QStringLiteral("settings-locked"));
      QCOMPARE(events.count(), 0); QCOMPARE(nativeEditorPreferences().values, original); QCOMPARE(bytes(path), storedBytes);
    }
    for (const auto &bad : {QJsonObject{{"TextFontSize", 0.5}}, QJsonObject{{"TextHorizontalAlignment", "Justify"}}, QJsonObject{{"StepType", ""}},
                            QJsonObject{{"SpeechBalloonTextBold", "yes"}}, QJsonObject{{"StepFontSize", 2001}}}) {
      auto change = bad; change.insert("Thickness", 7);
      const auto rejected = applyDrawingDefaults(change, revision);
      QVERIFY(!rejected.saved); QVERIFY(rejected.diagnostic.startsWith("invalid-"));
      QVERIFY(!editorPreferenceDiagnosticMessage(rejected.diagnostic).isEmpty());
      QCOMPARE(nativeEditorPreferences().values, original); QCOMPARE(events.count(), 0); QCOMPARE(bytes(path), storedBytes);
    }
    QVERIFY(!applyDrawingDefaults({{"UnrelatedField", 1}}, revision).saved);
    QVERIFY(applyDrawingDefaults({{"TextFontSize", 1}, {"SpeechBalloonFontSize", 2000}, {"StepFontSize", 0.5}, {"StepType", "LowercaseLetter"},
        {"SpeechBalloonTextHorizontalAlignment", "Right"}, {"TextFontFamily", ""}}, revision).saved);
    QCOMPARE(events.count(), 1);
    QCOMPARE(nativeEditorPreferences().values.value("SpeechBalloonFontSize"), QJsonValue(2000));
    QCOMPARE(contentDefaultsFromPreferences(nativeEditorPreferences(), nativeFontFamilyAvailable).balloon.alignment, QStringLiteral("right"));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QCOMPARE(nativeEditorPreferences().values.value("StepType"), QJsonValue("LowercaseLetter"));
    QCOMPARE(nativeEditorPreferences().values.value("StepFontSize"), QJsonValue(0.5));
    // A stale draft cannot overwrite the newer state.
    const auto stale = applyDrawingDefaults({{"TextFontSize", 50}}, revision);
    QVERIFY(!stale.saved); QCOMPARE(stale.diagnostic, QStringLiteral("settings-changed-in-session"));
  }
  void invalidDrawingDraftRemainsOpenWithoutPublishingAnyField() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto original = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision();
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool tested = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      if (tested) return; tested = true;
      auto *stroke = dialog->findChild<QLineEdit *>("defaultStrokeColour");
      auto *fill = dialog->findChild<QLineEdit *>("defaultFillColour");
      auto *apply = dialog->findChild<QPushButton *>("applyDrawingDefaults");
      stroke->setText("red"); fill->setText("#80010203");
      dialog->findChild<QSpinBox *>("defaultStrokeThickness")->setValue(12); apply->click();
      QVERIFY(dialog->isVisible()); QCOMPARE(nativeEditorPreferences().values, original);
      QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(!QFile::exists(path));
      QVERIFY(dialog->findChild<QLabel *>("drawingDefaultsError")->text().contains("#AARRGGBB"));
      stroke->setText("#ffffffff"); fill->setText("#invalid"); apply->click();
      QVERIFY(dialog->isVisible()); QCOMPARE(nativeEditorPreferences().values, original); QVERIFY(!QFile::exists(path));
      dialog->reject();
    });
    timer.start(5); QVERIFY(!manageDrawingDefaults()); timer.stop(); QVERIFY(tested && !timedOut); QCOMPARE(events.count(), 0);
    const auto invalid = applyDrawingDefaults({{"Thickness", 0}, {"FillColorHex", "#80010203"}}, revision);
    QVERIFY(!invalid.saved); QCOMPARE(invalid.diagnostic, QStringLiteral("invalid-Thickness"));
    QCOMPARE(nativeEditorPreferences().values, original);
  }
  void failedOrStaleDrawingDraftPreservesStoredAndActiveDefaults_data() {
    QTest::addColumn<bool>("stale"); QTest::newRow("locked") << false; QTest::newRow("incoming-image") << true;
  }
  void failedOrStaleDrawingDraftPreservesStoredAndActiveDefaults() {
    QFETCH(bool, stale);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn(1));
    QVERIFY(rememberEditorImage("/private/first.png").isEmpty());
    QLockFile lock(path + ".lock"); if (!stale) QVERIFY(lock.tryLock(0));
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    QByteArray expectedBytes = bytes(path); auto expected = nativeEditorPreferences().values;
    QTimer timer; QElapsedTimer deadline; deadline.start(); bool tested = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (deadline.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      if (tested) return; tested = true;
      dialog->findChild<QLineEdit *>("defaultStrokeColour")->setText("#40667788");
      dialog->findChild<QSpinBox *>("defaultStrokeThickness")->setValue(10);
      if (stale) {
        QVERIFY(rememberEditorImage("/private/incoming.png").isEmpty());
        expectedBytes = bytes(path); expected = nativeEditorPreferences().values;
      }
      dialog->findChild<QPushButton *>("applyDrawingDefaults")->click();
      QVERIFY(dialog->isVisible()); QCOMPARE(nativeEditorPreferences().values, expected); QCOMPARE(bytes(path), expectedBytes);
      const auto error = dialog->findChild<QLabel *>("drawingDefaultsError")->text();
      QVERIFY(error.contains(stale ? "changed" : "Another process")); QVERIFY(!error.contains("/private/"));
      dialog->reject();
    });
    timer.start(5); QVERIFY(!manageDrawingDefaults()); timer.stop(); QVERIFY(tested && !timedOut); QCOMPARE(events.count(), 0);
    lock.unlock(); QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(nativeEditorPreferences().values, expected);
  }
  void styleApplyCannotResetRecentDraftRevisionOrOverwriteAnIncomingImage() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn(1));
    QVERIFY(rememberEditorImage("/private/first.png").isEmpty());
    const auto draft = recentImageHistory(); const auto revision = draft.revision();
    QVERIFY(applyDrawingDefaults({{"Thickness", 8}}, nativeEditorPreferenceRevision()).saved);
    QCOMPARE(recentImageHistory().revision(), revision); QCOMPARE(recentImageHistory().paths(), draft.paths());
    QVERIFY(rememberEditorImage("/private/next.png").isEmpty()); QVERIFY(recentImageHistory().revision() > revision);
    const auto rejected = applyRecentImagePreferences(draft, revision); QVERIFY(!rejected.saved);
    QCOMPARE(recentImageHistory().paths(), QStringList{"/private/next.png"});
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(8));
  }
  void init() { initializeNativeEditorPreferences(); }
  void cleanup() { initializeNativeEditorPreferences(); }
  void missingNativeStoreDoesNotWriteOrBackfill() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(nativeEditorPreferencePersistence());
    QVERIFY(!QFile::exists(path)); QVERIFY(!recentImageHistory().enabled());
    QVERIFY(rememberEditorImage("/private/prior.png").isEmpty()); QVERIFY(!QFile::exists(path));
    QTimer::singleShot(0, [] { changeDialog("enable", false); }); QVERIFY(!manageRecentImages()); QVERIFY(!QFile::exists(path));
    QTimer::singleShot(0, [] { changeDialog("enable", true); }); QVERIFY(manageRecentImages());
    QVERIFY(QFile::exists(path)); QVERIFY(recentImageHistory().enabled()); QVERIFY(recentImageHistory().paths().isEmpty());
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(recentImageHistory().enabled()); QVERIFY(recentImageHistory().paths().isEmpty());
  }
  void successfulRememberSurvivesRestartWithNewestFirstOrder() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn(2));
    for (const auto *name : {"/private/one.png", "/private/two.png", "/private/one.png", "/private/three.png"}) QVERIFY(rememberEditorImage(name).isEmpty());
    const QStringList expected{"/private/three.png", "/private/one.png"};
    QCOMPARE(recentImageHistory().paths(), expected);
    QCOMPARE(nativeEditorPreferences().values.value("RecentImageFiles").toArray(), QJsonArray::fromStringList(expected));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(recentImageHistory().paths(), expected); QCOMPARE(recentImageHistory().maximum(), 2);
  }
  void nativeRecoveryKeepsUnrelatedAndUnknownPreferencesWhenTheListIsSaved() {
    QTemporaryDir temporary; const QString path = temporary.filePath("settings.json");
    const QJsonObject future{{"enabled", true}, {"mode", "future"}};
    const QJsonObject current{{"Theme", "Light"}, {"BlurStrength", "NaN"}, {"Thickness", 8}, {"FutureOption", future}};
    const QJsonObject envelope{{"format", "com.xerahs.app/linux-qt/editor-settings"}, {"version", 1}, {"current", current}, {"previous", QJsonObject{}}};
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    const auto encoded = QJsonDocument(envelope).toJson(); QCOMPARE(file.write(encoded), encoded.size()); file.close();
    const auto diagnostics = initializeNativeEditorPreferences(path);
    QCOMPARE(diagnostics, QStringList{"invalid-BlurStrength"});
    QVERIFY(editorPreferenceDiagnosticMessage(diagnostics.front()).contains("Blur strength"));
    QCOMPARE(nativeEditorPreferences().values.value("Theme"), QJsonValue("Light"));
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(8));
    QCOMPARE(nativeEditorPreferences().values.value("BlurStrength"), QJsonValue(30));
    QVERIFY(optIn()); QVERIFY(rememberEditorImage("/private/remembered.png").isEmpty());
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QCOMPARE(nativeEditorPreferences().values.value("Theme"), QJsonValue("Light"));
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(8));
    QCOMPARE(nativeEditorPreferences().values.value("FutureOption").toObject(), future);
  }
  void nativeApplyAndCancelPersistRemovalAndPrivacy_data() {
    QTest::addColumn<QString>("operation"); QTest::addColumn<bool>("apply");
    for (const QString &operation : {QString("remove"), QString("clear"), QString("disable"), QString("trim")})
      for (bool apply : {false, true}) QTest::newRow(qPrintable(operation + (apply ? "-apply" : "-cancel"))) << operation << apply;
  }
  void nativeApplyAndCancelPersistRemovalAndPrivacy() {
    QFETCH(QString, operation); QFETCH(bool, apply);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn(3));
    for (const auto *name : {"/private/one.png", "/private/two.png", "/private/three.png"}) QVERIFY(rememberEditorImage(name).isEmpty());
    const QByteArray before = bytes(path); const QStringList original = recentImageHistory().paths();
    QTimer::singleShot(0, [=] { changeDialog(operation, apply); }); QCOMPARE(manageRecentImages(), apply);
    if (!apply) { QCOMPARE(bytes(path), before); QCOMPARE(recentImageHistory().paths(), original); }
    else if (operation == u"remove") QCOMPARE(recentImageHistory().paths(), QStringList({"/private/three.png", "/private/one.png"}));
    else if (operation == u"trim") { QCOMPARE(recentImageHistory().paths(), QStringList{"/private/three.png"}); QCOMPARE(recentImageHistory().maximum(), 1); }
    else QVERIFY(recentImageHistory().paths().isEmpty());
    const auto expected = recentImageHistory().paths(); const auto enabled = recentImageHistory().enabled();
    const auto stored = QJsonDocument::fromJson(bytes(path)).object();
    if (apply) for (const auto &entry : stored.value("previous").toObject().value("RecentImageFiles").toArray()) QVERIFY(expected.contains(entry.toString()));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(recentImageHistory().paths(), expected); QCOMPARE(recentImageHistory().enabled(), enabled);
  }
  void failedApplyAndRememberPreserveActiveAndStoredPreferences() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn()); QVERIFY(rememberEditorImage("/private/original.png").isEmpty());
    const auto original = recentImageHistory().paths(); const auto revision = recentImageHistory().revision(); const auto before = bytes(path);
    QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0));
    QVERIFY(!rememberEditorImage("/private/new.png").isEmpty()); QCOMPARE(recentImageHistory().paths(), original); QCOMPARE(recentImageHistory().revision(), revision); QCOMPARE(bytes(path), before);
    bool reported = false;
    QTimer::singleShot(0, [&] {
      QTimer::singleShot(0, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(box);
        reported = box->windowTitle() == u"Preferences not saved"; QVERIFY(!box->text().contains("/private/")); box->accept();
      });
      changeDialog("clear", true);
    });
    QVERIFY(!manageRecentImages()); QVERIFY(reported); QCOMPARE(recentImageHistory().paths(), original); QCOMPARE(recentImageHistory().revision(), revision); QCOMPARE(bytes(path), before);
    lock.unlock(); QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(recentImageHistory().paths(), original);
  }
  void cappedIncomingRememberStillInvalidatesAnOlderModalDraft() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(optIn(1)); QVERIFY(rememberEditorImage("/private/one.png").isEmpty());
    auto draft = recentImageHistory(); const auto revision = draft.revision(); draft.clear();
    QVERIFY(rememberEditorImage("/private/two.png").isEmpty()); QVERIFY(recentImageHistory().revision() > revision);
    const auto stored = bytes(path); const auto result = applyRecentImagePreferences(draft, revision);
    QVERIFY(!result.saved); QCOMPARE(result.diagnostic, QStringLiteral("settings-changed-in-session"));
    QCOMPARE(recentImageHistory().paths(), QStringList{"/private/two.png"}); QCOMPARE(bytes(path), stored);
  }
  // ESET-002: native warning path reports Invalid fields but stays silent for Unknown ones,
  // and the host exposes the Unknown field state. A newer record warns and is read-only.
  void nativeWarningsNameInvalidFieldsButNeverUnknownOnes() {
    QTemporaryDir temporary; const QString path = temporary.filePath("settings.json");
    const QJsonObject future{{"enabled", true}};
    QVERIFY(writePrivate(path, textEnvelope({{"Theme", "Light"}, {"FutureOption", future}})));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());  // Unknown alone: no warning.
    QCOMPARE(nativeEditorPreferences().fields.value("FutureOption"), PreferenceFieldState::Unknown);
    QCOMPARE(nativeEditorPreferences().fields.value("Theme"), PreferenceFieldState::Valid);
    QVERIFY(!nativeEditorPreferences().readOnly);
    QVERIFY(writePrivate(path, textEnvelope({{"FutureOption", future}, {"BlurStrength", "NaN"}})));
    const auto diagnostics = initializeNativeEditorPreferences(path);
    QCOMPARE(diagnostics, QStringList{"invalid-BlurStrength"});
    QCOMPARE(nativeEditorPreferences().fields.value("FutureOption"), PreferenceFieldState::Unknown);
    QCOMPARE(nativeEditorPreferences().fields.value("BlurStrength"), PreferenceFieldState::Invalid);
    QVERIFY(!editorPreferenceDiagnosticMessage(diagnostics.front()).isEmpty());
    QVERIFY(writePrivate(path, textEnvelope({{"schemaVersion", 2}, {"FutureOption", future}})));
    const auto newer = initializeNativeEditorPreferences(path);
    QVERIFY(newer.contains("settings-newer-version"));
    QVERIFY(!editorPreferenceDiagnosticMessage("settings-newer-version").isEmpty());
    QCOMPARE(nativeEditorPreferences().fields.value("schemaVersion"), PreferenceFieldState::NewerVersion);
    QVERIFY(nativeEditorPreferences().readOnly);
  }
  // ESET-012: the application produces no log, telemetry, support or export text of its own
  // for preferences; the only paths shown are the user-owned Recent Images menu and dialog.
  // Diagnostics, user-visible warning text and every Qt log message stay value-free.
  void recentPathsNeverAppearInWarningsDiagnosticsOrLogs() {
    static QStringList logged;
    logged.clear();
    const auto previousHandler = qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) { logged << message; });
    struct Restore { QtMessageHandler handler; ~Restore() { qInstallMessageHandler(handler); } } restore{previousHandler};
    const QString secret = QStringLiteral("/home/private-sentinel/secret-photo.png");
    auto clean = [&](const QString &text) { return !text.contains(QStringLiteral("private-sentinel")) && !text.contains(QStringLiteral("secret-photo")); };
    QStringList texts;
    auto collect = [&](const QStringList &diagnostics) {
      for (const auto &diagnostic : diagnostics) { texts << diagnostic << editorPreferenceDiagnosticMessage(diagnostic); }
    };
    QTemporaryDir temporary; const QString path = temporary.filePath("settings.json");
    const QList<QJsonObject> stored{
        {{"RecentImageFiles", QJsonArray{secret}}},  // Stored without opt-in.
        {{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret, "relative/secret-photo.png"}}},  // Invalid list.
        {{"schemaVersion", 2}, {"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret}}},  // Newer record.
        {{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret}}, {"BlurStrength", "NaN"}}};
    for (const auto &current : stored) { QVERIFY(writePrivate(path, textEnvelope(current))); collect(initializeNativeEditorPreferences(path)); }
    QVERIFY(texts.contains("recent-paths-ignored-without-opt-in")); QVERIFY(texts.contains("invalid-RecentImageFiles"));
    QVERIFY(texts.contains("settings-newer-version"));
    // Failed and refused saves while paths exist, including a read-only (newer) record.
    QVERIFY(writePrivate(path, textEnvelope({{"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{secret}}})));
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    auto draft = recentImageHistory(); const auto revision = draft.revision(); draft.remove(secret);
    collect({applyRecentImagePreferences(draft, revision + 99).diagnostic});  // Stale draft.
    QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner)); QVERIFY(QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    collect({rememberEditorImage(secret + "2")});  // Directory not writable: save fails but the editor survives.
    QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QVERIFY(writePrivate(path, textEnvelope({{"schemaVersion", 2}, {"RecentImageFiles", QJsonArray{secret}}})));
    collect(initializeNativeEditorPreferences(path)); collect({rememberEditorImage(secret)});
    for (const auto &code : {"settings-read-only", "settings-locked", "settings-changed-externally", "settings-changed-in-session", "recovered-previous-settings",
                             "invalid-RecentImageFiles", "invalid-MaxRecentImageFiles", "invalid-schemaVersion", "settings-unsafe-path"})
      collect({code});
    QVERIFY(!texts.isEmpty());
    for (const auto &text : texts) QVERIFY2(clean(text), qPrintable(text));
    for (const auto &message : logged) QVERIFY2(clean(message), qPrintable(message));
  }
  void damagedOrNewerNativeStorageIsVisibleReadOnlyAndUntouched() {
    QTemporaryDir temporary; const QString path = temporary.filePath("settings.json");
    const auto newer = QByteArray("{\"format\":\"com.xerahs.app/linux-qt/editor-settings\",\"version\":2,\"current\":{}}");
    for (const auto &data : {QByteArray("{damaged"), newer}) {
      QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)); QCOMPARE(file.write(data), data.size()); file.close();
      const auto diagnostics = initializeNativeEditorPreferences(path); QVERIFY(!diagnostics.isEmpty());
      QVERIFY(!editorPreferenceDiagnosticMessage(diagnostics.front()).isEmpty()); QVERIFY(nativeEditorPreferences().readOnly);
      QVERIFY(!optIn()); QVERIFY(!recentImageHistory().enabled()); QCOMPARE(bytes(path), data);
    }
  }
};
QTEST_MAIN(NativeEditorPreferencesTest)
#include "NativeEditorPreferencesTest.moc"
