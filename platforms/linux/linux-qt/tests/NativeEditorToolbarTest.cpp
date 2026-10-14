#include "NativeEditorToolbar.h"
#include "NativeEditorPreferences.h"
#include "image-editor/ToolbarPreferences.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QScopeGuard>
#include <QShortcut>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <memory>
using namespace xerahs::app;
using namespace xerahs::editor;
namespace {
QJsonObject item(QString id, QString key = {}, bool visible = true, bool group = false) {
  return {{"Id", id}, {"IsVisible", visible}, {"BeginGroup", group}, {"Hotkey", key}};
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
struct Harness {
  QMainWindow window;
  ToolbarActionRegistry actions;
  QToolBar *bar;
  QWidget *canvas;
  QLineEdit *line;
  bool ready = true;
  std::unique_ptr<NativeEditorToolbar> controller;
  Harness() {
    auto *central = new QWidget(&window); auto *layout = new QVBoxLayout(central);
    line = new QLineEdit(central); canvas = new QWidget(central); canvas->setFocusPolicy(Qt::StrongFocus); canvas->setMinimumSize(200, 100);
    layout->addWidget(line); layout->addWidget(canvas); window.setCentralWidget(central);
    bar = window.addToolBar("Commands"); auto *menu = window.menuBar()->addMenu("Commands");
    for (const auto &descriptor : toolbarActions()) {
      auto *action = new QAction(descriptor.id, &window); action->setData(descriptor.id); actions.insert(descriptor.id, action);
      menu->addAction(action); window.addAction(action);
    }
    actions["save"]->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    actions["undo"]->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    controller = std::make_unique<NativeEditorToolbar>(window, *bar, actions, [this] { return ready; });
    window.resize(500, 300); window.show();
  }
  void focusCanvas() {
    window.activateWindow(); canvas->setFocus();
    QTRY_VERIFY(window.isActiveWindow()); QTRY_COMPARE(QApplication::focusWidget(), canvas);
  }
  QStringList layoutIds() const {
    QStringList result; for (auto *action : bar->actions()) result.append(action->isSeparator() ? "|" : action->data().toString()); return result;
  }
};
int rowFor(QTableWidget &table, const QString &id) {
  for (int row = 0; row < table.rowCount(); ++row)
    if (table.item(row, 0)->data(Qt::UserRole).value<QJsonObject>().value("Id") == QJsonValue(id)) return row;
  return -1;
}
struct DialogResult { bool accepted = false, acted = false, timedOut = false; };
DialogResult runDialog(Harness &harness, const std::function<void(QDialog &)> &edit) {
  DialogResult result; QTimer timer; QElapsedTimer deadline; deadline.start();
  QObject::connect(&timer, &QTimer::timeout, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
    if (deadline.elapsed() > 5000) { result.timedOut = true; dialog->reject(); return; }
    if (result.acted) return; result.acted = true;
    const auto close = qScopeGuard([&] { if (dialog->isVisible()) dialog->reject(); });
    edit(*dialog);
  });
  timer.start(5); result.accepted = manageEditorToolbar(&harness.window, harness.actions); timer.stop(); return result;
}
}
class NativeEditorToolbarTest : public QObject {
  Q_OBJECT
private slots:
  void init() { initializeNativeEditorPreferences(); }
  void cleanup() { initializeNativeEditorPreferences(); }
  void applyCancelAndResetAreAtomicAcrossWindows_data() {
    QTest::addColumn<bool>("apply"); QTest::addColumn<bool>("reset");
    for (bool apply : {false, true}) for (bool reset : {false, true})
      QTest::newRow(qPrintable(QString("%1-%2").arg(apply).arg(reset))) << apply << reset;
  }
  void applyCancelAndResetAreAtomicAcrossWindows() {
    QFETCH(bool, apply); QFETCH(bool, reset); QTemporaryDir temporary; const auto path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{item("save"), item("fit-image", "F8")}, nativeEditorPreferenceRevision()).saved);
    Harness one, two; const auto stored = bytes(path); const auto revision = nativeEditorPreferenceRevision();
    const auto initial = one.layoutIds(); QSignalSpy published(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::toolbarChanged);
    const auto result = runDialog(one, [&](QDialog &dialog) {
      QCOMPARE(dialog.objectName(), QString("toolbarDialog"));
      auto *table = dialog.findChild<QTableWidget *>("toolbarItems"); QVERIFY(table);
      auto *accessible = QAccessible::queryAccessibleInterface(table); QVERIFY(accessible);
      QCOMPARE(accessible->role(), QAccessible::Table); QCOMPARE(accessible->text(QAccessible::Name), QString("Toolbar command order and settings"));
      const int fit = rowFor(*table, "fit-image"), save = rowFor(*table, "save"); QVERIFY(fit >= 0 && save >= 0);
      table->item(fit, 3)->setText("F6"); table->item(save, 1)->setCheckState(Qt::Unchecked);
      table->item(fit, 2)->setCheckState(Qt::Checked); table->setCurrentCell(fit, 0); dialog.findChild<QPushButton *>("toolbarUp")->click();
      QCOMPARE(one.layoutIds(), initial); QCOMPARE(two.layoutIds(), initial); QCOMPARE(bytes(path), stored);
      if (reset) dialog.findChild<QPushButton *>("resetToolbar")->click();
      dialog.findChild<QDialogButtonBox *>()->button(apply ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
    });
    QVERIFY(result.acted && !result.timedOut); QCOMPARE(result.accepted, apply);
    if (apply) {
      QCOMPARE(one.layoutIds(), two.layoutIds()); QCOMPARE(published.count(), 1); QVERIFY(bytes(path) != stored);
      QVERIFY(nativeEditorPreferenceRevision() > revision);
      if (!reset) QCOMPARE(one.layoutIds(), QStringList{"fit-image"});
      const auto saved = one.layoutIds(); QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); Harness restarted; QCOMPARE(restarted.layoutIds(), saved);
    } else {
      QCOMPARE(one.layoutIds(), initial); QCOMPARE(two.layoutIds(), initial); QCOMPARE(bytes(path), stored);
      QCOMPARE(nativeEditorPreferenceRevision(), revision); QCOMPARE(published.count(), 0);
    }
  }
  void invalidLockedStaleAndReadOnlyApplyPreserveThePublishedLayout_data() {
    QTest::addColumn<QString>("failure");
    for (const auto *kind : {"invalid", "locked", "stale", "read-only"}) QTest::newRow(kind) << QString(kind);
  }
  void invalidLockedStaleAndReadOnlyApplyPreserveThePublishedLayout() {
    QFETCH(QString, failure); QTemporaryDir temporary; const auto path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{item("save")}, nativeEditorPreferenceRevision()).saved);
    if (failure == u"read-only") {
      auto artifact = QJsonDocument::fromJson(bytes(path)).object(); artifact.insert("version", 2);
      QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write(QJsonDocument(artifact).toJson()) > 0); file.close();
      QCOMPARE(initializeNativeEditorPreferences(path), QStringList{"settings-newer-version"});
    }
    Harness harness; const auto before = harness.layoutIds(); QByteArray stored = bytes(path); quint64 revision = nativeEditorPreferenceRevision();
    QLockFile lock(path + ".lock"); QSignalSpy published(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::toolbarChanged);
    const auto result = runDialog(harness, [&](QDialog &dialog) {
      auto *table = dialog.findChild<QTableWidget *>("toolbarItems"); QVERIFY(table); const int row = rowFor(*table, "save"); QVERIFY(row >= 0);
      table->item(row, 3)->setText(failure == u"invalid" ? "Ctrl+O" : "F6");
      if (failure == u"locked") QVERIFY(lock.tryLock(0));
      if (failure == u"stale") {
        QVERIFY(applyDrawingDefaults({{"Thickness", 7}}, nativeEditorPreferenceRevision()).saved);
        stored = bytes(path); revision = nativeEditorPreferenceRevision();
      }
      dialog.findChild<QPushButton *>("applyToolbar")->click(); QVERIFY(dialog.isVisible());
      QVERIFY(!dialog.findChild<QLabel *>("toolbarError")->text().isEmpty());
      QCOMPARE(harness.layoutIds(), before); QCOMPARE(bytes(path), stored); QCOMPARE(nativeEditorPreferenceRevision(), revision);
    });
    QVERIFY(result.acted && !result.timedOut && !result.accepted); QCOMPARE(published.count(), 0);
    QCOMPARE(harness.layoutIds(), before); QCOMPARE(bytes(path), stored);
  }
  void hiddenCommandsAndGroupBoundariesKeepMenusAndHotkeys() {
    const QJsonArray placements{item("save", "Ctrl+S"), item("undo", "", false, true), item("fit-image", "F6")};
    QVERIFY(applyToolbarPreferences(placements, nativeEditorPreferenceRevision()).saved);
    Harness harness; harness.focusCanvas(); QCOMPARE(harness.layoutIds(), QStringList({"save", "|", "fit-image"}));
    QSignalSpy save(harness.actions["save"], &QAction::triggered), fit(harness.actions["fit-image"], &QAction::triggered);
    QTest::keyClick(harness.canvas, Qt::Key_S, Qt::ControlModifier); QCOMPARE(save.count(), 1);
    QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(fit.count(), 1);
    QVERIFY(applyToolbarPreferences(QJsonArray{item("save", "F6", false)}, nativeEditorPreferenceRevision()).saved);
    QVERIFY(harness.layoutIds().isEmpty()); QVERIFY(harness.actions["save"]->isVisible());
    QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(save.count(), 2);
    QTest::keyClick(harness.canvas, Qt::Key_S, Qt::ControlModifier); QCOMPARE(save.count(), 3);
    QVERIFY(applyToolbarPreferences(QJsonArray{}, nativeEditorPreferenceRevision()).saved); QVERIFY(harness.layoutIds().isEmpty());
    QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(save.count(), 3);
    QTest::keyClick(harness.canvas, Qt::Key_S, Qt::ControlModifier); QCOMPARE(save.count(), 4);
  }
  void applyCommitsTheActiveNativeCellEditor() {
    QTemporaryDir temporary; const auto path = temporary.filePath("native/settings.json");
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty()); Harness harness;
    const auto result = runDialog(harness, [&](QDialog &dialog) {
      auto *table = dialog.findChild<QTableWidget *>("toolbarItems"); QVERIFY(table);
      const int row = rowFor(*table, "save"); QVERIFY(row >= 0);
      auto *cell = table->item(row, 3); table->setCurrentItem(cell); table->editItem(cell);
      QTRY_VERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
      auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
      QTest::keyClicks(editor, "F6"); QCOMPARE(cell->text(), QString{});
      dialog.findChild<QPushButton *>("applyToolbar")->click();
    });
    QVERIFY(result.acted && result.accepted && !result.timedOut);
    const auto saved = nativeEditorPreferences().values.value("ToolbarItems").toArray();
    QCOMPARE(saved.at(0).toObject().value("Hotkey"), QJsonValue("F6"));
    harness.focusCanvas(); QSignalSpy save(harness.actions["save"], &QAction::triggered);
    QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(save.count(), 1);
    QVERIFY(initializeNativeEditorPreferences(path).isEmpty());
    QCOMPARE(nativeEditorPreferences().values.value("ToolbarItems"), QJsonValue(saved));
  }
  void textModalAndBusyInputTakePrecedenceOverAdditionalAndProtectedCommands() {
    QVERIFY(applyToolbarPreferences(QJsonArray{item("save", "F6")}, nativeEditorPreferenceRevision()).saved);
    Harness harness; harness.focusCanvas(); QSignalSpy save(harness.actions["save"], &QAction::triggered), undo(harness.actions["undo"], &QAction::triggered);
    harness.line->setText("seed"); harness.line->setFocus(); harness.line->selectAll(); QTest::keyClicks(harness.line, "changed");
    QTest::keyClick(harness.line, Qt::Key_F6); QTest::keyClick(harness.line, Qt::Key_S, Qt::ControlModifier);
    QTest::keyClick(harness.line, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(harness.line->text(), QString("seed")); QCOMPARE(save.count(), 0); QCOMPARE(undo.count(), 0);
    harness.focusCanvas(); harness.ready = false; QTest::keyClick(harness.canvas, Qt::Key_F6);
    QTest::keyClick(harness.canvas, Qt::Key_S, Qt::ControlModifier); QCOMPARE(save.count(), 0);
    harness.ready = true; harness.actions["save"]->setEnabled(false); QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(save.count(), 0);
    harness.actions["save"]->setEnabled(true);
    QDialog modal(&harness.window); modal.setModal(true); modal.show(); QTest::keyClick(&modal, Qt::Key_F6); QCOMPARE(save.count(), 0); modal.reject();
    harness.focusCanvas(); QTest::keyClick(harness.canvas, Qt::Key_F6); QCOMPARE(save.count(), 1);
  }
  void customizedOrderingNavigationKeepsNativeListAndTextPrecedence() {
    QVERIFY(applyToolbarPreferences(QJsonArray{item("bring-to-front", "Home", false), item("send-to-back", "End", false), item("bring-forward", "PageUp", false), item("send-backward", "PageDown", false)}, nativeEditorPreferenceRevision()).saved);
    Harness harness; auto *list = new QListWidget(harness.window.centralWidget()); list->setMinimumHeight(120); harness.window.resize(500, 600);
    qobject_cast<QVBoxLayout *>(harness.window.centralWidget()->layout())->addWidget(list);
    for (int i = 0; i < 100; ++i) list->addItem(QString::number(i));
    QSignalSpy front(harness.actions["bring-to-front"], &QAction::triggered), back(harness.actions["send-to-back"], &QAction::triggered), forward(harness.actions["bring-forward"], &QAction::triggered), backward(harness.actions["send-backward"], &QAction::triggered);
    harness.focusCanvas(); QTest::keyClick(harness.canvas, Qt::Key_Home); QTest::keyClick(harness.canvas, Qt::Key_End); QTest::keyClick(harness.canvas, Qt::Key_PageUp); QTest::keyClick(harness.canvas, Qt::Key_PageDown);
    QCOMPARE(front.count(), 1); QCOMPARE(back.count(), 1); QCOMPARE(forward.count(), 1); QCOMPARE(backward.count(), 1);
    list->setCurrentRow(50); list->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), list);
    QTRY_VERIFY(list->viewport()->height() >= 80);
    QTest::keyClick(list, Qt::Key_Home); QCOMPARE(list->currentRow(), 0); QTest::keyClick(list, Qt::Key_End); QCOMPARE(list->currentRow(), 99);
    QTRY_VERIFY(list->visualItemRect(list->currentItem()).intersects(list->viewport()->rect()));
    QTest::keyClick(list, Qt::Key_PageUp); QVERIFY(list->currentRow() < 99); const int row = list->currentRow(); QTest::keyClick(list, Qt::Key_PageDown); QVERIFY(list->currentRow() > row);
    QCOMPARE(front.count(), 1); QCOMPARE(back.count(), 1); QCOMPARE(forward.count(), 1); QCOMPARE(backward.count(), 1);
    harness.line->setText("native navigation"); harness.line->setFocus(); QTest::keyClick(harness.line, Qt::Key_Home); QCOMPARE(harness.line->cursorPosition(), 0);
    QTest::keyClick(harness.line, Qt::Key_End); QCOMPARE(harness.line->cursorPosition(), harness.line->text().size()); QCOMPARE(front.count(), 1); QCOMPARE(back.count(), 1);
    harness.focusCanvas(); harness.ready = false; QTest::keyClick(harness.canvas, Qt::Key_Home); QCOMPARE(front.count(), 1);
    harness.ready = true; QDialog modal(&harness.window); modal.setModal(true); modal.show(); QTest::keyClick(&modal, Qt::Key_Home); QCOMPARE(front.count(), 1); modal.reject();
    harness.focusCanvas(); QTest::keyClick(harness.canvas, Qt::Key_Home); QCOMPARE(front.count(), 2); QVERIFY(harness.layoutIds().isEmpty());
  }
  void extensionFutureDataAndRecentDraftAuthoritySurviveNativeApply() {
    QTemporaryDir temporary; const auto path = temporary.filePath("native/settings.json");
    const QJsonObject future{{"Id", "future-command"}, {"IsVisible", true}, {"Hotkey", "Escape"}, {"extension", 7}};
    auto known = item("save", "F6"); known.insert("extension", QJsonArray{1, "keep"});
    FilePreferenceStorage storage(path); EditorPreferenceStore store(storage);
    QVERIFY(store.save(loadEditorPreferences({{"ToolbarItems", QJsonArray{future, known}}, {"unrelated", 42}})).saved);
    QCOMPARE(initializeNativeEditorPreferences(path), QStringList{"toolbar-unknown-action"});
    auto recent = recentImageHistory(); QVERIFY(recent.setPreferences(true, 1)); QVERIFY(recent.remember(temporary.filePath("first.png")));
    QVERIFY(applyRecentImagePreferences(recent, recentImageHistory().revision()).saved);
    const auto recentRevision = recentImageHistory().revision(); Harness harness;
    const auto result = runDialog(harness, [&](QDialog &dialog) {
      auto *table = dialog.findChild<QTableWidget *>("toolbarItems"); QVERIFY(table);
      const int row = rowFor(*table, "future-command"); QVERIFY(row >= 0);
      QVERIFY(!(table->item(row, 3)->flags() & Qt::ItemIsEditable));
      dialog.findChild<QPushButton *>("resetToolbar")->click(); dialog.findChild<QPushButton *>("applyToolbar")->click();
    });
    QVERIFY(result.acted && result.accepted && !result.timedOut); QCOMPARE(recentImageHistory().revision(), recentRevision);
    const auto saved = nativeEditorPreferences().values.value("ToolbarItems").toArray();
    QCOMPARE(saved.at(saved.size() - 1).toObject(), future); QCOMPARE(saved.at(0).toObject().value("extension"), known.value("extension"));
    QCOMPARE(nativeEditorPreferences().values.value("unrelated"), QJsonValue(42));
    auto stale = recentImageHistory(), next = stale; QVERIFY(next.remember(temporary.filePath("second.png")));
    QVERIFY(applyRecentImagePreferences(next, recentRevision).saved);
    QCOMPARE(applyRecentImagePreferences(stale, recentRevision).diagnostic, QString("settings-changed-in-session"));
    QCOMPARE(initializeNativeEditorPreferences(path), QStringList{"toolbar-unknown-action"});
    QCOMPARE(nativeEditorPreferences().values.value("ToolbarItems"), QJsonValue(saved));
  }
};
QTEST_MAIN(NativeEditorToolbarTest)
#include "NativeEditorToolbarTest.moc"
