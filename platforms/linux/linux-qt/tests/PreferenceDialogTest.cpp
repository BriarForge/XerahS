#include "AppearanceDialog.h"
#include "DrawingDefaultsDialog.h"
#include "NativeEditorToolbar.h"
#include "NativeEditorPreferences.h"
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
QString name(const QString &kind) { return kind == "drawing" ? "drawingDefaultsDialog" : kind == "appearance" ? "appearanceDialog" : "toolbarDialog"; }
QString command(const QString &kind) { return kind == "drawing" ? "drawingDefaults" : kind == "appearance" ? "manageAppearance" : "customizeToolbar"; }
bool manage(const QString &kind, QWidget *parent, const ToolbarActionRegistry &actions) {
  if (kind == "drawing") return manageDrawingDefaults(parent);
  if (kind == "appearance") return manageEditorAppearance(parent);
  return manageEditorToolbar(parent, actions);
}
void draft(QDialog &dialog, const QString &kind) {
  if (kind == "drawing") dialog.findChild<QSpinBox *>("defaultStrokeThickness")->setValue(11);
  else if (kind == "appearance") { dialog.findChild<QCheckBox *>("useSystemTheme")->setChecked(false); dialog.findChild<QComboBox *>("manualTheme")->setCurrentIndex(1); }
  else {
    auto *table = dialog.findChild<QTableWidget *>("toolbarItems");
    for (int row = 0; row < table->rowCount(); ++row) if (table->item(row, 0)->data(Qt::UserRole).toJsonObject().value("Id") == "save") table->item(row, 1)->setCheckState(Qt::Unchecked);
  }
}
void apply(QDialog &dialog) { dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click(); }
QMetaObject::Connection observe(const QString &kind, QObject *context, const std::function<void()> &callback) {
  auto &events = nativeEditorPreferenceEvents();
  if (kind == "drawing") return QObject::connect(&events, &NativeEditorPreferenceEvents::drawingDefaultsChanged, context, callback);
  if (kind == "appearance") return QObject::connect(&events, &NativeEditorPreferenceEvents::appearanceChanged, context, callback);
  return QObject::connect(&events, &NativeEditorPreferenceEvents::toolbarChanged, context, callback);
}
struct ActionObserver final : QObject {
  std::function<void()> added;
  bool eventFilter(QObject *, QEvent *event) override { if (event->type() == QEvent::ActionAdded && added) added(); return false; }
};
}
class PreferenceDialogTest final : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase() {
    QVERIFY(settings.isValid()); QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path()); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  }
  void init() { initializeNativeEditorPreferences(); }
  void cleanup() { initializeNativeEditorPreferences(); }
  void ownedDialogCompletionAndCleanup_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("mode");
    for (const auto *kind : {"drawing", "appearance", "toolbar"}) for (const auto *mode : {"parent", "dialog", "cleanup", "direct-accept"}) QTest::newRow(qPrintable(QString("%1-%2").arg(kind, mode))) << QString(kind) << QString(mode);
  }
  void ownedDialogCompletionAndCleanup() {
    QFETCH(QString, kind); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto before = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); QPointer<QWidget> parent(new QWidget); parent->show();
    QAction save("Save"); ToolbarActionRegistry actions{{"save", &save}}; QPointer<QDialog> dialog; bool entered = false;
    const auto cleanup = qScopeGuard([&] { delete parent.data(); });
    QTimer::singleShot(0, this, [&] {
      dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); entered = true; QCOMPARE(dialog->objectName(), name(kind)); draft(*dialog, kind);
      if (mode == "parent") delete parent.data(); else if (mode == "dialog") delete dialog.data();
      else if (mode == "cleanup") { connect(dialog, &QObject::destroyed, this, [&] { delete parent.data(); }); dialog->reject(); }
      else { dialog->accept(); QVERIFY(dialog->isVisible()); dialog->reject(); }
    });
    QVERIFY(!manage(kind, parent, actions)); QVERIFY(entered); QVERIFY(!dialog); QCOMPARE(nativeEditorPreferences().values, before); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(!QFile::exists(path));
  }
  void ownedColourPickerAndParentLifetimes_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("field"); QTest::addColumn<QString>("mode");
    for (const auto *field : {"defaultStrokeColour", "defaultFillColour", "manualAccent"}) for (const auto *mode : {"accept", "cancel", "picker", "parent", "reject-parent", "cleanup"})
      QTest::newRow(qPrintable(QString("%1-%2").arg(field, mode))) << QString(field == QString("manualAccent") ? "appearance" : "drawing") << QString(field) << QString(mode);
  }
  void ownedColourPickerAndParentLifetimes() {
    QFETCH(QString, kind); QFETCH(QString, field); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    const auto before = nativeEditorPreferences().values; const auto revision = nativeEditorPreferenceRevision(); QPointer<QWidget> parent(new QWidget); parent->show();
    QAction save("Save"); ToolbarActionRegistry actions{{"save", &save}}; QPointer<QDialog> dialog; QPointer<QColorDialog> picker; bool picked = false; const auto cleanup = qScopeGuard([&] { delete parent.data(); });
    QTimer::singleShot(0, this, [&] {
      dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const auto close = qScopeGuard([&] { if (dialog) dialog->reject(); });
      if (kind == "appearance") dialog->findChild<QCheckBox *>("useSystemAccent")->setChecked(false);
      auto *input = dialog->findChild<QLineEdit *>(field); const auto original = input->text();
      QTimer::singleShot(0, this, [&] {
        picker = qobject_cast<QColorDialog *>(QApplication::activeModalWidget()); QVERIFY(picker); picked = true; QVERIFY(picker->testOption(QColorDialog::ShowAlphaChannel)); QVERIFY(!picker->accessibleName().isEmpty());
        // Programmatic Apply and Reset must not consume a draft while the picker owns it.
        QVERIFY(QMetaObject::invokeMethod(dialog->findChild<QDialogButtonBox *>(), "accepted", Qt::DirectConnection));
        dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::RestoreDefaults)->click();
        QCOMPARE(input->text(), original);
        QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(dialog->isVisible());
        picker->setCurrentColor(QColor::fromRgba(0x80667788));
        if (mode == "accept" && field == "defaultStrokeColour" && qEnvironmentVariableIsSet("XERAHS_PREFERENCE_PICKER_QA")) QVERIFY(picker->grab().save(qEnvironmentVariable("XERAHS_PREFERENCE_PICKER_QA")));
        if (mode == "accept") picker->accept(); else if (mode == "cancel") QTest::keyClick(picker, Qt::Key_Escape); else if (mode == "picker") delete picker.data();
        else if (mode == "parent") delete parent.data(); else if (mode == "reject-parent") dialog->reject();
        else { connect(picker, &QObject::destroyed, this, [&] { delete parent.data(); }); picker->accept(); }
      });
      dialog->findChild<QPushButton *>(field + "Picker")->click();
      if (!dialog) return;
      QCOMPARE(input->text(), mode == "accept" ? QString("#80667788") : original);
      if (mode == "accept") apply(*dialog);
    });
    QCOMPARE(manage(kind, parent, actions), mode == "accept"); QVERIFY(picked); QVERIFY(!picker); QVERIFY(!dialog);
    if (mode == "accept") { const auto key = field == "defaultStrokeColour" ? "BorderColorHex" : field == "defaultFillColour" ? "FillColorHex" : "AccentColorHex"; QCOMPARE(nativeEditorPreferences().values.value(key), QJsonValue("#80667788")); QVERIFY(QFile::exists(path)); }
    else { QCOMPARE(nativeEditorPreferences().values, before); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(!QFile::exists(path)); }
  }
  void applyPublicationIsSerialAndOwnerSafe_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("mode");
    for (const auto *kind : {"drawing", "appearance", "toolbar"}) for (const auto *mode : {"reenter", "parent", "dialog", "failure-retry"}) QTest::newRow(qPrintable(QString("%1-%2").arg(kind, mode))) << QString(kind) << QString(mode);
  }
  void applyPublicationIsSerialAndOwnerSafe() {
    QFETCH(QString, kind); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); const auto revision = nativeEditorPreferenceRevision();
    QPointer<QWidget> parent(new QWidget); parent->show(); QAction save("Save"); ToolbarActionRegistry actions{{"save", &save}}; QPointer<QDialog> dialog; int publications = 0;
    const auto connection = observe(kind, this, [&] {
      ++publications;
      if (mode == "parent") delete parent.data(); else if (mode == "dialog") delete dialog.data();
      else if (dialog) { QVERIFY(!dialog->isEnabled()); QVERIFY(QMetaObject::invokeMethod(dialog->findChild<QDialogButtonBox *>(), "accepted", Qt::DirectConnection)); }
    });
    const auto cleanup = qScopeGuard([&] { disconnect(connection); delete parent.data(); });
    QTimer::singleShot(0, this, [&] {
      dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const auto close = qScopeGuard([&] { if (dialog) dialog->reject(); }); draft(*dialog, kind);
      if (mode == "failure-retry") {
        QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0)); apply(*dialog); QVERIFY(dialog && dialog->isVisible() && dialog->isEnabled());
        QCOMPARE(publications, 0); QCOMPARE(nativeEditorPreferenceRevision(), revision); QVERIFY(!QFile::exists(path));
        auto *error = dialog->findChild<QLabel *>(kind == "drawing" ? "drawingDefaultsError" : kind == "appearance" ? "appearanceError" : "toolbarError"); QVERIFY(error && !error->text().isEmpty()); lock.unlock();
      }
      apply(*dialog);
    });
    QCOMPARE(manage(kind, parent, actions), mode != "parent" && mode != "dialog"); QCOMPARE(publications, 1); QCOMPARE(nativeEditorPreferenceRevision(), revision + 1); QVERIFY(QFile::exists(path)); QVERIFY(!dialog);
    const auto persisted = bytes(path); const auto values = nativeEditorPreferences().values; QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(nativeEditorPreferences().values, values); QCOMPARE(bytes(path), persisted);
  }
  void editorConfigurationBusyAndTeardown_data() {
    QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("mode");
    for (const auto *kind : {"drawing", "appearance", "toolbar"}) for (const auto *mode : {"entry", "parent", "cleanup", "publication", "apply"}) QTest::newRow(qPrintable(QString("%1-%2").arg(kind, mode))) << QString(kind) << QString(mode);
  }
  void editorConfigurationBusyAndTeardown() {
    QFETCH(QString, kind); QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QImage image(16, 12, QImage::Format_ARGB32); image.fill(Qt::blue); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(image)); QVERIFY(window); window->show();
    const auto cleanup = qScopeGuard([&] { delete window.data(); }); auto *canvas = window->findChild<EditorCanvas *>(); const auto doc = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState(); QPointer<QDialog> dialog;
    QMetaObject::Connection publication;
    if (mode == "publication") publication = observe(kind, this, [&] { delete window.data(); });
    const auto disconnectPublication = qScopeGuard([&] { disconnect(publication); });
    if (mode == "entry") {
      auto *save = window->findChild<QAction *>("saveImage"); connect(save, &QAction::changed, this, [&, save] { if (window && !save->isEnabled()) delete window.data(); });
      window->findChild<QAction *>(command(kind))->trigger(); QVERIFY(!window); QVERIFY(!QFile::exists(path)); return;
    }
    QTimer::singleShot(0, this, [&] {
      dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const auto close = qScopeGuard([&] { if (dialog) dialog->reject(); });
      QVERIFY(!canvas->isEnabled()); QVERIFY(!window->menuBar()->isEnabled()); QVERIFY(!window->findChild<QAction *>("saveImage")->isEnabled()); QVERIFY(!window->openImage(dir.filePath("unused.png")));
      window->findChild<QAction *>(command(kind))->trigger(); QCOMPARE(QApplication::activeModalWidget(), dialog.data());
      if (mode == "parent") delete window.data();
      else if (mode == "cleanup") { connect(dialog, &QObject::destroyed, this, [&] { delete window.data(); }); dialog->reject(); }
      else {
        draft(*dialog, kind);
        if (mode == "apply" && qEnvironmentVariableIsSet("XERAHS_PREFERENCE_DIALOG_QA")) QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_PREFERENCE_DIALOG_QA") + "-" + kind + ".png"));
        apply(*dialog);
      }
    });
    window->findChild<QAction *>(command(kind))->trigger(); QVERIFY(!dialog);
    if (mode != "apply") { QVERIFY(!window); QCOMPARE(QFile::exists(path), mode == "publication"); return; }
    QVERIFY(window); QVERIFY(canvas->isEnabled()); QVERIFY(window->menuBar()->isEnabled()); QCOMPARE(serializeDocument(canvas->annotationDocument()), doc); QCOMPARE(canvas->annotationState(), state); QVERIFY(QFile::exists(path));
    if (kind == "drawing") QCOMPARE(canvas->creationStrokeWidth(), 11.0);
  }
  void reentrantToolbarPublicationEventuallyShowsNewestPreferences() {
    QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QImage image(16, 12, QImage::Format_ARGB32); image.fill(Qt::blue); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(image)); QVERIFY(window); window->show();
    const auto cleanup = qScopeGuard([&] { delete window.data(); }); auto *bar = window->findChild<QToolBar *>("editorToolbar"); QVERIFY(bar);
    const auto revision = nativeEditorPreferenceRevision(); bool changed = false; ActionObserver observer;
    const QJsonArray newest{QJsonObject{{"Id", "undo"}, {"IsVisible", true}, {"BeginGroup", false}, {"Hotkey", ""}}};
    observer.added = [&] { if (changed) return; changed = true; QVERIFY(applyToolbarPreferences(newest, nativeEditorPreferenceRevision()).saved); };
    QTimer::singleShot(0, this, [&] {
      QPointer<QDialog> dialog(qobject_cast<QDialog *>(QApplication::activeModalWidget())); QVERIFY(dialog);
      const auto close = qScopeGuard([&] { if (dialog) dialog->reject(); }); draft(*dialog, "toolbar"); bar->installEventFilter(&observer); apply(*dialog);
    });
    window->findChild<QAction *>("customizeToolbar")->trigger(); QVERIFY(changed); QCOMPARE(nativeEditorPreferenceRevision(), revision + 2);
    QTRY_COMPARE(bar->actions().size(), 1); QCOMPARE(bar->actions().first(), window->findChild<QAction *>("undo"));
    const auto saved = nativeEditorPreferences().values.value("ToolbarItems"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); QCOMPARE(nativeEditorPreferences().values.value("ToolbarItems"), saved);
  }
  void unchangedDrawingApplyRecoversFailedLocalDefaultsWithoutHistory() {
    QTemporaryDir dir; const auto path = dir.filePath("prefs.json"); QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QImage image(16, 12, QImage::Format_ARGB32); image.fill(Qt::blue); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(image)); QVERIFY(window); window->show();
    const auto cleanup = qScopeGuard([&] { delete window.data(); }); auto *canvas = window->findChild<EditorCanvas *>();
    const auto document = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState(); const auto revision = nativeEditorPreferenceRevision();
    QLockFile lock(path + ".lock"); QVERIFY(lock.tryLock(0)); window->findChild<QAction *>("increaseStroke")->trigger();
    QCOMPARE(canvas->creationStrokeWidth(), 5.0); QCOMPARE(nativeEditorPreferenceRevision(), revision); lock.unlock();
    QSignalSpy changes(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::drawingDefaultsChanged);
    QTimer::singleShot(0, this, [&] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); apply(*dialog); });
    window->findChild<QAction *>("drawingDefaults")->trigger(); QCOMPARE(changes.count(), 0); QCOMPARE(canvas->creationStrokeWidth(), 4.0);
    QCOMPARE(nativeEditorPreferenceRevision(), revision + 1); QCOMPARE(serializeDocument(canvas->annotationDocument()), document); QCOMPARE(canvas->annotationState(), state); QVERIFY(QFile::exists(path));
  }
};
QTEST_MAIN(PreferenceDialogTest)
#include "PreferenceDialogTest.moc"
