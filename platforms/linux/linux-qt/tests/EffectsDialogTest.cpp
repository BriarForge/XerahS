// EDITOR-EFFECTS-001: EE-002..EE-006, EE-010..EE-014.
#include "EffectsDialog.h"
#include "image-editor/effects/SxiePreset.h"
#include "EditorSource.h"
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "NativeEditorPreferences.h"
#include <QAction>
#include <QDialogButtonBox>
#include <QMenu>
#include <QToolBar>
#include <QPointer>
#include <QSettings>
#include <QTest>
#include <QColorSpace>
#include <QTemporaryDir>
#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QSignalSpy>
#include <QThread>
#include <QBuffer>
#include <QAccessible>
#include <atomic>
using namespace xerahs::app;
using namespace xerahs::editor;
using namespace xerahs::effects;
namespace {
struct Probe { std::atomic<int> started{0}, finished{0}; };
class Fake : public Effect {
  EffectSpec s;
  std::shared_ptr<Probe> probe;
public:
  Fake(QString id, Category category, bool size = false, Mode mode = Mode::Previewable, std::shared_ptr<Probe> p = {}) : probe(std::move(p)) {
    s.id = id; s.displayName = id.toUpper(); s.category = category; s.changesSize = size; s.mode = mode;
    s.parameters = {{"amount", "Amount", ParamKind::Int, 1, 0, 10},
      {"strength", "Strength", ParamKind::Float, 0.5, 0, 1},
      {"enabled", "Enabled", ParamKind::Bool, true},
      {"color", "Colour", ParamKind::Color, QColor(Qt::red)},
      {"choice", "Choice", ParamKind::Enum, QString("a"), 0, 0, {"a", "b"}},
      {"text", "Text", ParamKind::Text, QString("default")},
      {"path", "Asset", ParamKind::FilePath, QString()}};
  }
  const EffectSpec &spec() const override { return s; }
  EffectResult apply(const QImage &source, const ParamValues &values, const EffectContext &context) const override {
    if (probe) ++probe->started;
    const int amount = values.value("amount").toInt();
    if (s.id == "slow") {
      for (int i = 0; i < (amount == 1 ? 80 : 3); ++i) { QThread::msleep(5); context.progress(i); }
    }
    if (probe) ++probe->finished;
    if (s.id == "fail") return {{}, "effect-failed"};
    QImage result = s.changesSize ? QImage(source.width() + 4, source.height() + 6, QImage::Format_ARGB32) : source.copy();
    result.setColorSpace(source.colorSpace()); result.fill(qRgba(amount, 20, 30, 40)); return {result, {}};
  }
};
EffectRegistry registry(QString id = "fake", bool size = false, Mode mode = Mode::Previewable, std::shared_ptr<Probe> probe = {}) {
  EffectRegistry r; r.add(std::make_unique<Fake>(id, Category::Filters, size, mode, probe)); return r;
}
QImage pixels() {
  QImage image(16, 12, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, qRgba(x * 5, y * 7, 90, (x + y) * 8));
  return image;
}
LoadedSource source() { return prepareCapturedSource(pixels()); }
}
class EffectsDialogTest : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase() {
    QVERIFY(settings.isValid());
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  }
  void editorEntryAndHistory() {
    QVERIFY(initializeNativeEditorPreferences().isEmpty());
    QVERIFY(applyToolbarPreferences(QJsonArray{QJsonObject{{"Id", "effects"},
        {"IsVisible", true}, {"Hotkey", "F12"}}}, nativeEditorPreferenceRevision()).saved);
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(pixels()));
    QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    auto *action = window->findChild<QAction *>("effectsImage"); QVERIFY(action);
    QCOMPARE(action->text(), QString("Effects…")); QVERIFY(window->actions().contains(action));
    bool inMenu = false;
    for (auto *menu : window->findChildren<QMenu *>()) inMenu |= menu->actions().contains(action);
    QVERIFY(inMenu);
    QVERIFY(window->findChild<QToolBar *>("editorToolbar")->actions().contains(action));
    const auto before = serializeDocument(canvas->annotationDocument());
    auto *undo = window->findChild<QAction *>("undo");
    auto *redo = window->findChild<QAction *>("redo");
    QVERIFY(undo && redo); QVERIFY(!undo->isEnabled()); QVERIFY(!redo->isEnabled());
    action->trigger();
    QPointer<EffectsDialog> dialog = window->findChild<EffectsDialog *>(); QVERIFY(dialog);
    QVERIFY(!canvas->isEnabled()); QVERIFY(!action->isEnabled());
    dialog->findChild<QLineEdit *>("effectsSearch")->setText("brightness");
    QTRY_VERIFY(!dialog->previewImage().isNull());
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before);
    dialog->reject(); QTRY_VERIFY(!dialog);
    QCOMPARE(serializeDocument(canvas->annotationDocument()), before); QVERIFY(!undo->isEnabled());
    window->activateWindow(); canvas->setFocus();
    QTRY_VERIFY(window->isActiveWindow());
    QTest::keyClick(canvas, Qt::Key_F12);
    dialog = window->findChild<EffectsDialog *>(); QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("effectsSearch")->setText("invert");
    auto *apply = dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply);
    QTRY_VERIFY(apply->isEnabled());
    apply->click(); QTRY_VERIFY(!dialog);
    QVERIFY(canvas->isEnabled()); QVERIFY(action->isEnabled()); QVERIFY(undo->isEnabled()); QVERIFY(!redo->isEnabled());
    const auto after = serializeDocument(canvas->annotationDocument()); QVERIFY(after != before);
    const auto output = QImage::fromData(canvas->annotationDocument().sourceImagePng);
    QCOMPARE(output.pixelColor(2, 3), QColor(245, 234, 165, 40));
    undo->trigger(); QTRY_COMPARE(serializeDocument(canvas->annotationDocument()), before);
    QVERIFY(!undo->isEnabled()); QVERIFY(redo->isEnabled());
    redo->trigger(); QTRY_COMPARE(serializeDocument(canvas->annotationDocument()), after);
    QVERIFY(undo->isEnabled()); QVERIFY(!redo->isEnabled());
  }
  void editorOwnerTeardown() {
    QVERIFY(initializeNativeEditorPreferences().isEmpty());
    auto *window = EditorWindow::fromCapturedImage(pixels()); QVERIFY(window);
    window->show(); window->findChild<QAction *>("effectsImage")->trigger();
    QPointer<EffectsDialog> dialog = window->findChild<EffectsDialog *>(); QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("effectsSearch")->setText("invert");
    delete window; QVERIFY(!dialog);
    QTest::qWait(100);
  }
  void seedsPreviewHistoryAndRecents() {
    QTemporaryDir temp; const auto loaded = source(); const auto path = temp.filePath("seeds.json");
    EditorSession session(*loaded.document); const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    EffectsDialog d(*loaded.document, loaded.image, [&](PreparedCanvasEdit edit) { return session.commitCanvas(*edit.result.document, state); }, nullptr, path);
    d.show(); d.findChild<QLineEdit *>("effectsSearch")->setText("tv_static");
    QTRY_VERIFY(!d.previewImage().isNull());
    auto *seed = d.findChild<QLineEdit *>("effectsSeed"); QVERIFY(seed);
    QCOMPARE(seed->text(), QString::number(defaultEffectSeed("tv_static")));
    const auto first = d.previewImage();
    d.findChild<QPushButton *>("effectsNewSeed")->click(); const auto selectedSeed = seed->text().toULongLong();
    QTRY_VERIFY(!d.previewImage().isNull()); QVERIFY(d.previewImage() != first); const auto preview = d.previewImage();
    seed->setText("18446744073709551616"); QVERIFY(!d.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->isEnabled());
    seed->setText(QString::number(selectedSeed)); QTRY_VERIFY(!d.previewImage().isNull()); QCOMPARE(d.previewImage(), preview);
    d.accept(); QTRY_COMPARE(d.result(), int(QDialog::Accepted)); QCOMPARE(session.undoCount(), 1);
    const auto applied = serializeDocument(session.document());
    QCOMPARE(session.document().extra.value("effectOperations").toArray().last().toObject().value("seed").toString(), QString::number(selectedSeed));
    QCOMPARE(QImage::fromData(session.document().sourceImagePng), preview);
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
    // Use the ordinary context callbacks when replaying the recorded seed.
    EffectContext context; context.seed = selectedSeed;
    const auto replay = prepareEffectEdit(session.document(), loaded.image, *builtinEffects().find("tv_static"), {}, context);
    QVERIFY(!replay.result.error); QCOMPARE(replay.source, preview);
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), applied);
    EffectsStore store(path); QCOMPARE(store.load(builtinEffects()).seeds.value("tv_static"), selectedSeed);
    EffectsDialog again(*loaded.document, loaded.image, {}, nullptr, path); again.show();
    again.findChild<QLineEdit *>("effectsSearch")->setText("tv_static");
    QTRY_VERIFY(again.findChild<QLineEdit *>("effectsSeed")->text() == QString::number(selectedSeed));
    QTRY_COMPARE(again.previewImage(), preview);
  }
  void browser() {
    QTemporaryDir temp; const auto loaded = source();
    EffectsDialog d(*loaded.document, loaded.image, {}, nullptr, temp.filePath("state.json")); d.show();
    auto *list = d.findChild<QListWidget *>("effectsList"); auto *categories = d.findChild<QComboBox *>("effectsCategory");
    auto *search = d.findChild<QLineEdit *>("effectsSearch");
    QCOMPARE(list->count(), builtinEffects().size());
    for (int i = 0; i < 4; ++i) {
      categories->setCurrentIndex(i + 1); int count = 0;
      const auto catalog = builtinEffects();
      for (const auto *s : catalog.specs()) if (int(s->category) == i) ++count;
      QCOMPARE(list->count(), count);
    }
    categories->setCurrentIndex(0); search->setText("INVERT"); QCOMPARE(list->count(), 1); QCOMPARE(d.selectedId(), QString("invert"));
    QVERIFY(QAccessible::queryAccessibleInterface(list)); QVERIFY(!categories->accessibleName().isEmpty());
    search->clear(); list->setFocus(); QTest::keyClick(list, Qt::Key_End); QCOMPARE(list->currentRow(), list->count() - 1);
  }
  void storeRoundTripUnknownAndConflict() {
    QTemporaryDir temp; const auto path = temp.filePath("state.json");
    xerahs::storage::PrivateJsonFile raw(path, {"com.xerahs.app/effects-recents"}); QByteArray snapshot;
    QCOMPARE(raw.write({{"recent", QJsonArray{"invert", "retired", "invert", "alpha"}}, {"favorites", QJsonArray{"retired", "alpha", "alpha"}}}, 1, {}, &snapshot), xerahs::storage::StoreStatus::Ok);
    EffectsStore store(path); auto lists = store.load(builtinEffects()); QVERIFY(lists.diagnostic.isEmpty());
    QCOMPARE(lists.recent, QStringList({"invert", "alpha"})); QCOMPARE(lists.favorites, QStringList({"alpha"}));
    const auto stale = lists; lists.recent = {"alpha", "invert"}; lists.favorites.clear();
    QVERIFY(store.save(lists).diagnostic.isEmpty());
    auto again = store.load(builtinEffects()); QCOMPARE(again.recent, lists.recent); QVERIFY(again.favorites.isEmpty());
    QCOMPARE(store.save(stale).diagnostic, QString("effects-store-changed"));
  }
  void controlsResetAndAccessibility() {
    QTemporaryDir temp; const auto loaded = source(); EffectsDialog d(*loaded.document, loaded.image, {}, nullptr, temp.filePath("state"), registry());
    QVERIFY(d.findChild<QSpinBox *>("param:amount")); auto *spin = d.findChild<QDoubleSpinBox *>("param:strength"); QVERIFY(spin);
    QCOMPARE(spin->minimum(), 0.0); QCOMPARE(spin->maximum(), 1.0);
    QVERIFY(d.findChild<QCheckBox *>("param:enabled")); QVERIFY(d.findChild<QPushButton *>("param:color"));
    QCOMPARE(d.findChild<QComboBox *>("param:choice")->count(), 2);
    QVERIFY(d.findChild<QLineEdit *>("param:text")); QVERIFY(d.findChild<QLineEdit *>("param:path"));
    for (auto *widget : d.findChildren<QWidget *>()) if (widget->objectName().startsWith("param:")) QVERIFY(!widget->accessibleName().isEmpty());
    spin->setValue(0.75); QCOMPARE(d.parameters().value("strength").toDouble(), 0.75);
    d.findChild<QPushButton *>("effectsReset")->click(); QCOMPARE(d.parameters().value("strength").toDouble(), 0.5);
    QCOMPARE(d.parameters().value("amount").toInt(), 1);
  }
  void previewCancelAndApplyHistory() {
    QTemporaryDir temp; const auto loaded = source(); EditorSession session(*loaded.document); session.markSaved();
    const auto before = serializeDocument(session.document()); const auto state = session.stateId();
    int commits = 0;
    EffectsDialog d(*loaded.document, loaded.image, [&](PreparedCanvasEdit edit) { ++commits; return session.commitCanvas(*edit.result.document, state); }, nullptr, temp.filePath("state"), registry());
    QSignalSpy preview(&d, &EffectsDialog::previewChanged); QSignalSpy applied(&d, &EffectsDialog::effectCommitted); d.show();
    QTRY_VERIFY(preview.count() > 0); QCOMPARE(commits, 0); QCOMPARE(serializeDocument(session.document()), before); QVERIFY(!session.dirty()); QCOMPARE(session.undoCount(), 0);
    d.findChild<QSpinBox *>("param:amount")->setValue(7); QTRY_VERIFY(!d.previewImage().isNull()); QTRY_COMPARE(qRed(d.previewImage().pixel(0, 0)), 7);
    QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0);
    d.accept(); QTRY_COMPARE(applied.count(), 1); QCOMPARE(commits, 1); QCOMPARE(session.undoCount(), 1); QVERIFY(session.dirty());
    QCOMPARE(QImage::fromData(session.document().sourceImagePng).pixel(0, 0), qRgba(7, 20, 30, 40));
    const auto changed = serializeDocument(session.document()); QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(QImage::fromData(session.document().sourceImagePng), loaded.image); QVERIFY(!session.dirty());
    QVERIFY(session.redo()); QCOMPARE(serializeDocument(session.document()), changed); QVERIFY(session.undo());
    const auto next = prepareEffectEdit(session.document(), loaded.image, *registry().find("fake"), {{"amount", 3}}, {});
    QVERIFY(!next.result.error); QVERIFY(session.commitCanvas(*next.result.document, session.stateId())); QCOMPARE(session.redoCount(), 0); QCOMPARE(session.undoCount(), 1);
    QTRY_COMPARE(d.result(), int(QDialog::Accepted)); EffectsStore store(temp.filePath("state")); QTRY_COMPARE(store.load(registry()).recent, QStringList{"fake"});
  }
  void latestWinsAndCancel() {
    QTemporaryDir temp; const auto loaded = source(); auto probe = std::make_shared<Probe>(); int commits = 0;
    EffectsDialog d(*loaded.document, loaded.image, [&](PreparedCanvasEdit) { ++commits; return true; }, nullptr, temp.filePath("state"), registry("slow", false, Mode::Previewable, probe));
    d.show(); QSignalSpy preview(&d, &EffectsDialog::previewChanged);
    QTRY_COMPARE(probe->started.load(), 1); d.findChild<QSpinBox *>("param:amount")->setValue(2);
    QTRY_VERIFY(!d.previewImage().isNull()); QTRY_COMPARE(qRed(d.previewImage().pixel(0, 0)), 2); QTRY_COMPARE(probe->finished.load(), 2);
    QCOMPARE(preview.count(), 1); QCOMPARE(qRed(d.previewImage().pixel(0, 0)), 2); QCOMPARE(commits, 0);
    d.findChild<QSpinBox *>("param:amount")->setValue(1); QTRY_COMPARE(probe->started.load(), 3);
    QTest::keyClick(&d, Qt::Key_Escape); QTRY_COMPARE(probe->finished.load(), 3); QCOMPARE(preview.count(), 1); QCOMPARE(commits, 0); QCOMPARE(d.result(), int(QDialog::Rejected));
  }
  void failureAndApplyCancellation() {
    QTemporaryDir temp; const auto loaded = source(); EditorSession session(*loaded.document); const auto before = serializeDocument(session.document()); int commits = 0;
    EffectsDialog d(*loaded.document, loaded.image, [&](PreparedCanvasEdit) { ++commits; return true; }, nullptr, temp.filePath("state"), registry("fail"));
    d.show(); auto *status = d.findChild<QLabel *>("effectsStatus"); QTRY_VERIFY(status->text().startsWith("effect-failed")); d.accept(); QTRY_VERIFY(status->text().startsWith("effect-failed")); QCOMPARE(commits, 0);
    auto probe = std::make_shared<Probe>(); EffectsDialog slow(*loaded.document, loaded.image, [&](PreparedCanvasEdit) { ++commits; return true; }, nullptr, temp.filePath("other"), registry("slow", false, Mode::Immediate, probe));
    slow.show(); QTRY_VERIFY(slow.findChild<QPushButton *>("effectsFavorite")->isEnabled()); slow.accept(); QTRY_COMPARE(probe->started.load(), 1); slow.reject(); QTRY_COMPARE(probe->finished.load(), 1);
    QCOMPARE(commits, 0); QCOMPARE(serializeDocument(session.document()), before); QCOMPARE(session.undoCount(), 0); QVERIFY(!session.dirty());
  }
  void immediateAndSizeCanvasGeometry() {
    QTemporaryDir temp; const auto loaded = source(); EditorSession session(*loaded.document); QVERIFY(session.createRectangle({2, 2}, {8, 8}));
    const auto doc = session.document(); const auto before = serializeDocument(doc); auto probe = std::make_shared<Probe>();
    EffectsDialog d(doc, loaded.image, [&](PreparedCanvasEdit edit) { return session.commitCanvas(*edit.result.document, session.stateId()); }, nullptr, temp.filePath("state"), registry("sized", true, Mode::Immediate, probe));
    d.show(); QTRY_VERIFY(d.findChild<QPushButton *>("effectsFavorite")->isEnabled()); QTest::qWait(100); QCOMPARE(probe->started.load(), 0);
    QSignalSpy applied(&d, &EffectsDialog::effectCommitted); d.accept(); QTRY_COMPARE(applied.count(), 1); QCOMPARE(probe->started.load(), 1); QCOMPARE(session.undoCount(), 2);
    CanvasOperation op; op.action = CanvasAction::ResizeCanvas; op.width = 20; op.height = 18;
    const auto expected = prepareCanvasEdit(doc, loaded.image, op); QVERIFY(!expected.result.error);
    auto actual = session.document(); auto geometry = *expected.result.document; actual.sourceImagePng.clear(); geometry.sourceImagePng.clear();
    const auto operation = actual.extra.value("effectOperations").toArray().last().toObject();
    QCOMPARE(operation.value("id").toString(), QString("sized"));
    QCOMPARE(operation.value("seed").toString(), QString::number(defaultEffectSeed("sized")));
    QCOMPARE(operation.value("parameters").toObject(), serializeParameters(registry("sized", true).find("sized")->spec(), resolveParameters(registry("sized", true).find("sized")->spec(), {}).values));
    actual.extra.remove("effectOperations");
    actual.modifiedAt = geometry.modifiedAt; QCOMPARE(serializeDocument(actual), serializeDocument(geometry));
    QVERIFY(session.undo()); QCOMPARE(serializeDocument(session.document()), before);
  }
  void favoritesAndRecentEditing() {
    QTemporaryDir temp; const auto loaded = source(); const auto path = temp.filePath("state");
    EffectsDialog d(*loaded.document, loaded.image, {}, nullptr, path); d.show();
    auto *favorite = d.findChild<QPushButton *>("effectsFavorite"); QTRY_VERIFY(favorite->isEnabled()); const auto id = d.selectedId(); favorite->click();
    EffectsStore store(path); QTRY_COMPARE(store.load(builtinEffects()).favorites, QStringList{id});
    auto *category = d.findChild<QComboBox *>("effectsCategory"); category->setCurrentIndex(6); QCOMPARE(d.findChild<QListWidget *>("effectsList")->count(), 1); favorite->click();
    QTRY_VERIFY(store.load(builtinEffects()).favorites.isEmpty()); QCOMPARE(d.findChild<QListWidget *>("effectsList")->count(), 0);
  }
};
QTEST_MAIN(EffectsDialogTest)
#include "EffectsDialogTest.moc"
