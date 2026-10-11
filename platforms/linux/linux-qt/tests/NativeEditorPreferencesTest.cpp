#include "NativeEditorPreferences.h"
#include "RecentImageMenu.h"
#include "DrawingDefaultsDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
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
