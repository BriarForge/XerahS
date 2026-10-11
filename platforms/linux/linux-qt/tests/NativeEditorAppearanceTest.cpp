#include "AppearanceDialog.h"
#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QJsonObject manual(const QString &theme = QStringLiteral("Dark")) {
  return {{"Theme", theme}, {"UseSystemTheme", false}, {"UseSystemAccentColor", false}, {"AccentColorHex", "#FF006000"}};
}
void changeDraft(QDialog &dialog) {
  dialog.findChild<QCheckBox *>("useSystemTheme")->setChecked(false);
  dialog.findChild<QComboBox *>("manualTheme")->setCurrentIndex(1);
  dialog.findChild<QCheckBox *>("useSystemAccent")->setChecked(false);
  dialog.findChild<QLineEdit *>("manualAccent")->setText("#1234abcd");
}
}
class NativeEditorAppearanceTest : public QObject {
  Q_OBJECT
  QPalette m_native;
private slots:
  void init() { m_native = QApplication::palette(); initializeNativeEditorPreferences(); }
  void cleanup() { initializeNativeEditorPreferences(); QApplication::setPalette(m_native); QApplication::processEvents(); }
  void applyCancelAndResetAreAtomicAcrossNativeWindowsAndRestart_data() {
    QTest::addColumn<bool>("apply"); QTest::addColumn<bool>("reset");
    for (bool apply : {false, true}) for (bool reset : {false, true}) QTest::newRow(qPrintable(QString("apply-%1-reset-%2").arg(apply).arg(reset))) << apply << reset;
  }
  void applyCancelAndResetAreAtomicAcrossNativeWindowsAndRestart() {
    QFETCH(bool, apply); QFETCH(bool, reset);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(applyAppearancePreferences(manual(), nativeEditorPreferenceRevision()).saved);
    NativeDesktopAppearance source; QWidget one, two; NativeEditorAppearance a(one, source), b(two, source);
    one.show(); two.show(); const auto before = bytes(path); const auto palette = one.palette();
    QSignalSpy events(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::appearanceChanged);
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      changeDraft(*dialog);
      if (reset) dialog->findChild<QPushButton *>("resetAppearance")->click();
      QCOMPARE(one.palette(), palette); QCOMPARE(two.palette(), palette); QCOMPARE(bytes(path), before);
      auto *check = dialog->findChild<QCheckBox *>("useSystemTheme");
      auto *accessible = QAccessible::queryAccessibleInterface(check); QVERIFY(accessible);
      QCOMPARE(accessible->role(), QAccessible::CheckBox); QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Use desktop theme"));
      dialog->findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    QCOMPARE(manageEditorAppearance(&one), apply); QCOMPARE(events.count(), apply ? 1 : 0);
    if (!apply) { QCOMPARE(bytes(path), before); QCOMPARE(one.palette(), palette); QCOMPARE(two.palette(), palette); return; }
    QCOMPARE(one.palette(), two.palette()); QCOMPARE(QApplication::palette(), m_native);
    if (reset) {
      QVERIFY(nativeEditorPreferences().values.value("UseSystemTheme").toBool());
      QCOMPARE(nativeEditorPreferences().values.value("AccentColorHex"), QJsonValue("#FF3E83F2"));
    } else {
      QCOMPARE(nativeEditorPreferences().values.value("Theme"), QJsonValue("Light"));
      QCOMPARE(nativeEditorPreferences().values.value("AccentColorHex"), QJsonValue("#1234ABCD"));
      QCOMPARE(one.palette().color(QPalette::Window), QColor(240, 240, 240));
    }
    const auto expected = nativeEditorPreferences().values; QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QCOMPARE(nativeEditorPreferences().values, expected);
    QWidget restarted; NativeEditorAppearance again(restarted, source); QCOMPARE(restarted.palette(), one.palette());
  }
  void invalidLockedAndStaleDraftsKeepPublishedPaletteAndFile_data() {
    QTest::addColumn<QString>("failure"); for (const auto *mode : {"invalid", "locked", "stale"}) QTest::newRow(mode) << QString::fromLatin1(mode);
  }
  void invalidLockedAndStaleDraftsKeepPublishedPaletteAndFile() {
    QFETCH(QString, failure);
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(applyAppearancePreferences(manual(), nativeEditorPreferenceRevision()).saved);
    QWidget parent; NativeEditorAppearance appearance(parent); const auto before = bytes(path); const auto palette = parent.palette();
    QLockFile lock(path + ".lock"); if (failure == u"locked") QVERIFY(lock.tryLock(0));
    QTimer timer; QElapsedTimer elapsed; elapsed.start(); bool checked = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      if (elapsed.elapsed() > 5000) { timedOut = true; dialog->reject(); return; }
      changeDraft(*dialog);
      if (failure == u"invalid") dialog->findChild<QLineEdit *>("manualAccent")->setText("invalid");
      if (failure == u"stale") QVERIFY(applyDrawingDefaults({{"Thickness", 8}}, nativeEditorPreferenceRevision()).saved);
      const auto saved = bytes(path); const auto revision = nativeEditorPreferenceRevision();
      dialog->findChild<QPushButton *>("applyAppearance")->click();
      QVERIFY(dialog->isVisible()); QVERIFY(!dialog->findChild<QLabel *>("appearanceError")->text().isEmpty());
      QCOMPARE(bytes(path), saved); QCOMPARE(nativeEditorPreferenceRevision(), revision); QCOMPARE(parent.palette(), palette);
      QCOMPARE(nativeEditorPreferences().values.value("Theme"), QJsonValue("Dark")); checked = true; dialog->reject();
    });
    timer.start(5); QVERIFY(!manageEditorAppearance(&parent)); timer.stop(); QVERIFY(checked && !timedOut);
    if (failure != u"stale") QCOMPARE(bytes(path), before);
  }
  void nativePaletteChangesUpdateSystemWindowsWithoutPreferenceWritesOrFocusChanges() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(applyAppearancePreferences(manual(), nativeEditorPreferenceRevision()).saved);
    NativeDesktopAppearance source; QWidget window; QLineEdit input(&window); input.setAccessibleName("Example input");
    NativeEditorAppearance appearance(window, source); window.show(); input.setFocus(); QTRY_VERIFY(input.hasFocus());
    const auto font = input.font(); const auto manualPalette = window.palette();
    QPalette system = m_native; system.setColor(QPalette::Window, QColor(12, 13, 14)); system.setColor(QPalette::WindowText, Qt::white);
    system.setColor(QPalette::Base, QColor(10, 10, 10)); system.setColor(QPalette::Text, Qt::white);
    system.setColor(QPalette::Button, QColor(12, 13, 14)); system.setColor(QPalette::ButtonText, Qt::white);
    system.setColor(QPalette::Highlight, QColor(255, 136, 0)); QApplication::setPalette(system); QApplication::processEvents();
    QCOMPARE(window.palette().color(QPalette::Window), manualPalette.color(QPalette::Window));
    QVERIFY(applyAppearancePreferences({{"UseSystemTheme", true}, {"UseSystemAccentColor", true}}, nativeEditorPreferenceRevision()).saved);
    QCOMPARE(window.palette().color(QPalette::Window), QColor(12, 13, 14)); QCOMPARE(window.palette().color(QPalette::Highlight), QColor(255, 136, 0));
    const auto before = bytes(path); const auto revision = nativeEditorPreferenceRevision();
    system.setColor(QPalette::Window, QColor(240, 240, 240)); system.setColor(QPalette::WindowText, Qt::black);
    system.setColor(QPalette::Base, Qt::white); system.setColor(QPalette::Text, Qt::black);
    system.setColor(QPalette::Button, QColor(240, 240, 240)); system.setColor(QPalette::ButtonText, Qt::black);
    system.setColor(QPalette::Highlight, QColor(0, 96, 0)); QApplication::setPalette(system);
    QTRY_COMPARE(window.palette().color(QPalette::Window), QColor(240, 240, 240));
    QCOMPARE(bytes(path), before); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(input.hasFocus()); QCOMPARE(input.font(), font);
    QCOMPARE(QAccessible::queryAccessibleInterface(&input)->text(QAccessible::Name), QStringLiteral("Example input"));
  }
  void appearanceWritesPreserveUnknownAndRecentAuthorityAndRejectOtherDomains() {
    QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json"); FilePreferenceStorage storage(path); EditorPreferenceStore seed(storage);
    const auto prepared = changeEditorPreferences(seed.preferences(), {{"FutureTheme", QJsonObject{{"mode", "unknown"}}},
        {"RememberRecentImagePaths", true}, {"RecentImageFiles", QJsonArray{"/private/image.png"}}, {"Thickness", 9}});
    QVERIFY(prepared.preferences); QVERIFY(seed.save(*prepared.preferences).saved); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto recentRevision = recentImageHistory().revision();
    QVERIFY(applyAppearancePreferences(manual("Light"), nativeEditorPreferenceRevision()).saved);
    QCOMPARE(recentImageHistory().revision(), recentRevision); QCOMPARE(recentImageHistory().paths(), QStringList{"/private/image.png"});
    QCOMPARE(nativeEditorPreferences().values.value("Thickness"), QJsonValue(9)); QVERIFY(nativeEditorPreferences().values.contains("FutureTheme"));
    const auto before = bytes(path);
    const auto rejected = applyAppearancePreferences({{"Theme", "Dark"}, {"Thickness", 4}}, nativeEditorPreferenceRevision());
    QVERIFY(!rejected.saved); QCOMPARE(rejected.diagnostic, QStringLiteral("invalid-appearance-preferences")); QCOMPARE(bytes(path), before);
  }
  void alphaAwareAccentPickerOnlyChangesTheDraftUntilApply_data() {
    QTest::addColumn<bool>("accept"); QTest::newRow("cancel-picker") << false; QTest::newRow("accept-picker") << true;
  }
  void alphaAwareAccentPickerOnlyChangesTheDraftUntilApply() {
    QFETCH(bool, accept); QTemporaryDir temporary; const QString path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QVERIFY(applyAppearancePreferences(manual(), nativeEditorPreferenceRevision()).saved);
    const auto before = bytes(path); QTimer timer; QElapsedTimer elapsed; elapsed.start(); bool chosen = false, timedOut = false;
    connect(&timer, &QTimer::timeout, [&] {
      auto *picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); if (!picker) return;
      if (elapsed.elapsed() > 5000) { timedOut = true; picker->reject(); return; }
      QVERIFY(picker->testOption(QColorDialog::ShowAlphaChannel)); picker->setCurrentColor(QColor::fromRgba(0x1234ABCD));
      chosen = true; if (accept) picker->accept(); else picker->reject();
    });
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      dialog->findChild<QPushButton *>("manualAccentPicker")->click();
      QCOMPARE(bytes(path), before); QCOMPARE(dialog->findChild<QLineEdit *>("manualAccent")->text(), accept ? QString("#1234ABCD") : QString("#FF006000"));
      dialog->findChild<QPushButton *>("applyAppearance")->click();
    });
    timer.start(5); QVERIFY(manageEditorAppearance()); timer.stop(); QVERIFY(chosen && !timedOut);
    QCOMPARE(nativeEditorPreferences().values.value("AccentColorHex"), QJsonValue(accept ? "#1234ABCD" : "#FF006000"));
  }
};
QTEST_MAIN(NativeEditorAppearanceTest)
#include "NativeEditorAppearanceTest.moc"
