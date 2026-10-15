#include "CaptureWorkflows.h"
#include "CapturePipeline.h"
#include "ActionSelectionDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
using namespace xerahs::app;
using namespace xerahs::workflows;
using namespace xerahs::actions;
namespace {
QByteArray bytes(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
void chooseActions(QDialog &dialog, const QStringList &actions) {
  auto *list = dialog.findChild<QListWidget *>("captureWorkflowActions"); QVERIFY(list);
  for (int row = 0; row < list->count(); ++row) list->item(row)->setCheckState(actions.contains(list->item(row)->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
}
void nameWorkflow(QDialog &dialog, const QString &name) {
  auto *input = dialog.findChild<QLineEdit *>("captureWorkflowName"); input->setFocus(); QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(input, name); QCOMPARE(input->text(), name);
}
CapturePipelineReceipt receipt(const QSignalSpy &spy) { return qvariant_cast<CapturePipelineReceipt>(spy.first().first()); }
}
class NativeCaptureWorkflowsTest final : public QObject {
  Q_OBJECT
  QTemporaryDir privateSettings;
private slots:
  void initTestCase() {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QVERIFY(privateSettings.isValid()); QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, privateSettings.path()); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, privateSettings.path());
  }
  void createSaveRestartRunSelectedAndPublishActualOutputs_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"save", "clipboard", "ask", "empty"}) QTest::newRow(mode) << QString(mode);
  }
  void createSaveRestartRunSelectedAndPublishActualOutputs() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); NativeCaptureWorkflows host(path); QWidget parent; parent.show();
    const QStringList actions = mode == "clipboard" ? QStringList{"image-clipboard"} : mode == "empty" ? QStringList{} : QStringList{"save"};
    const auto label = "Quick " + mode; bool edited = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); });
      dialog->findChild<QPushButton *>("newCaptureWorkflow")->click(); nameWorkflow(*dialog, label); chooseActions(*dialog, actions);
      dialog->findChild<QLineEdit *>("captureWorkflowSaveDirectory")->setText(dir.filePath("profile/nested"));
      dialog->findChild<QLineEdit *>("captureWorkflowFilenamePattern")->setText("profile-%widthx%height");
      QCOMPARE(dialog->findChild<QLineEdit *>("captureWorkflowFilenamePreview")->text(), QString("profile-1920x1080.png"));
      dialog->findChild<QCheckBox *>("captureWorkflowChooseActions")->setChecked(mode == "ask"); dialog->findChild<QPushButton *>("defaultCaptureWorkflow")->click();
      QVERIFY(!dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled()); dialog->findChild<QPushButton *>("saveCaptureWorkflows")->click();
      QVERIFY(dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled()); edited = true;
      if (mode == "save" && qEnvironmentVariableIsSet("XERAHS_WORKFLOWS_QA")) QVERIFY(dialog->grab().save(qEnvironmentVariable("XERAHS_WORKFLOWS_QA")));
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(edited); QVERIFY(QFile::exists(path)); const auto stored = bytes(path);
    NativeCaptureWorkflows restarted(path); const auto configured = restarted.resolve(); QVERIFY(configured.workflow); QCOMPARE(configured.workflow->name, label); QCOMPARE(configured.workflow->actions, actions); QCOMPARE(configured.workflow->output.directory, dir.filePath("profile/nested")); QCOMPARE(configured.workflow->output.pattern, QString("profile-%widthx%height"));
    QTimer::singleShot(0, this, [&] { auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); QCOMPARE(dialog->findChild<QLineEdit *>("captureWorkflowName")->text(), label); QCOMPARE(dialog->findChild<QLineEdit *>("captureWorkflowId")->text(), configured.workflow->id.toString(QUuid::WithoutBraces)); QVERIFY(dialog->findChild<QLineEdit *>("captureWorkflowId")->isReadOnly()); dialog->findChild<QPushButton *>("runCaptureWorkflow")->click(); });
    const auto selected = manageCaptureWorkflows(restarted, &parent, [] { return true; }); QVERIFY(selected); QCOMPARE(selected->id, configured.workflow->id);
    int copied = 0; bool correctClipboard = true; CaptureActionPorts ports; ports.clipboard = [&](const QImage &image, const CancellationToken &) { ++copied; correctClipboard &= image.size() == QSize(12, 8); return ActionOutcome{}; };
    QImage image(12, 8, QImage::Format_ARGB32); image.fill(QColor(42, 71, 100, 155));
    auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("journal"));
    CapturePipeline pipeline(image, selected->output.directory, selected->output.pattern, workflowActions(*selected), nullptr, ports, journal, &parent, workflowSnapshot(*selected)); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    QTimer chooser; connect(&chooser, &QTimer::timeout, this, [&] { if (auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget())) dialog->accept(); }); chooser.start(5);
    pipeline.start(); QTRY_COMPARE(done.count(), 1); chooser.stop(); const auto result = receipt(done); QCOMPARE(result->pipeline.state, PipelineState::Completed); QCOMPARE(copied, mode == "clipboard" ? 1 : 0);
    QVERIFY(correctClipboard);
    const bool saved = mode == "save" || mode == "ask"; QCOMPARE(bool(result->file), saved);
    if (saved) { QCOMPARE(result->file->path, dir.filePath("profile/nested/profile-12x8.png")); const QImage actual(result->file->path); QCOMPARE(actual.size(), image.size()); QCOMPARE(actual.pixelColor(2, 2), image.pixelColor(2, 2)); }
    else QVERIFY(result->files.empty());
    QCOMPARE(result->pipeline.workflowSnapshot.value("name"), QJsonValue(label)); QCOMPARE(bytes(path), stored);
    const auto records = journal->list().entries; QCOMPARE(records.size(), size_t(1)); QVERIFY(records[0].parsed.record && records[0].parsed.record->terminal);
    const auto recovered = records[0].parsed.record->report.value("extensions").toObject().value("com.xerahs.app").toObject().value("capture_workflow").toObject(); QCOMPARE(recovered.value("id"), QJsonValue(selected->id.toString(QUuid::WithoutBraces)));
    QCOMPARE(recovered.value("output").toObject().value("directory"), QJsonValue(selected->output.directory));
  }
  void draftCancelLockedAndExternallyChangedSavePreserveConfiguration_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"cancel", "locked", "external"}) QTest::newRow(mode) << QString(mode);
  }
  void draftCancelLockedAndExternallyChangedSavePreserveConfiguration() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); NativeCaptureWorkflows host(path); auto catalog = *host.store().current().catalog;
    QVERIFY(host.save(catalog, host.store().revision()).saved); const auto original = bytes(path); QWidget parent; parent.show(); bool edited = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); }); nameWorkflow(*dialog, "Unsaved name");
      QLockFile lock(path + ".lock"); if (mode == "locked") QVERIFY(lock.tryLock(0));
      if (mode == "external") { NativeCaptureWorkflows other(path); auto next = *other.store().current().catalog; next.entries[0].name = "External change"; QVERIFY(other.save(next, other.store().revision()).saved); }
      const auto expected = bytes(path);
      if (mode != "cancel") dialog->findChild<QPushButton *>("saveCaptureWorkflows")->click();
      QCOMPARE(bytes(path), expected); QVERIFY(!dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled()); edited = true;
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(edited); if (mode != "external") QCOMPARE(bytes(path), original);
  }
  void retryRetainsOriginalWorkflowAfterStoredProfileChanges() {
    QTemporaryDir dir; NativeCaptureWorkflows host(dir.filePath("workflows.json")); auto catalog = *host.store().current().catalog; catalog.entries[0].actions = QStringList{"save"}; catalog.entries[0].name = "Original save"; catalog.entries[0].output.directory = dir.filePath("original"); catalog.entries[0].output.pattern = "original-%width";
    QVERIFY(host.save(catalog, host.store().revision()).saved); const auto selected = host.resolve(); QVERIFY(selected.workflow);
    int encodes = 0; CaptureActionPorts ports; ports.save.encode = [&](const QImage &image, QIODevice *device) { return ++encodes == 1 ? false : image.save(device, "PNG"); };
    QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::blue);
    CapturePipeline first(image, selected.workflow->output.directory, selected.workflow->output.pattern, workflowActions(*selected.workflow), nullptr, ports, {}, nullptr, workflowSnapshot(*selected.workflow)); QSignalSpy done(&first, &CapturePipeline::finished); first.start(); QTRY_COMPARE(done.count(), 1); const auto retained = receipt(done); QVERIFY(retained->retryPlan().available());
    catalog.entries[0].actions.clear(); catalog.entries[0].name = "Changed later"; catalog.entries[0].output.directory = dir.filePath("changed"); catalog.entries[0].output.pattern = "changed"; QVERIFY(host.save(catalog, host.store().revision()).saved);
    CapturePipeline retry(retained, nullptr, ports); QSignalSpy retried(&retry, &CapturePipeline::finished); retry.start(); QTRY_COMPARE(retried.count(), 1); const auto result = receipt(retried);
    QVERIFY(result->file); QCOMPARE(result->file->path, dir.filePath("original/original-12.png")); QVERIFY(!QFile::exists(dir.filePath("changed"))); QCOMPARE(encodes, 2); QCOMPARE(result->pipeline.workflowSnapshot.value("name"), QJsonValue("Original save")); QCOMPARE(result->pipeline.workflowSnapshot.value("actions"), QJsonValue(QJsonArray{"save"}));
  }
  void unsupportedSavedActionsAreVisibleAndCannotRun() {
    QTemporaryDir dir; NativeCaptureWorkflows host(dir.filePath("workflows.json")); auto catalog = *host.store().current().catalog; catalog.entries[0].actions = QStringList{"upload"};
    QVERIFY(host.save(catalog, host.store().revision()).saved); QVERIFY(!host.resolve().workflow); QWidget parent; bool visible = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog);
      const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); });
      auto *list = dialog->findChild<QListWidget *>("captureWorkflowActions");
      for (int row = 0; row < list->count(); ++row) if (list->item(row)->data(Qt::UserRole).toString() == "upload") visible = list->item(row)->text().contains("Unavailable") && list->item(row)->checkState() == Qt::Checked;
      QVERIFY(!dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled());
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(visible);
  }
  void deletingSavedDefaultRequiresConfirmationAndUpdatesDefaultOnSave() {
    QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); NativeCaptureWorkflows host(path); auto catalog = *host.store().current().catalog;
    auto second = catalog.entries[0]; second.id = QUuid::createUuid(); second.name = "Remove this profile"; catalog.entries.append(second); catalog.defaultId = second.id;
    QVERIFY(host.save(catalog, host.store().revision()).saved); QWidget parent; bool confirmed = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); });
      QTimer::singleShot(0, this, [&] { auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(question); confirmed = true; question->done(QMessageBox::Discard); });
      dialog->findChild<QPushButton *>("deleteCaptureWorkflow")->click(); QCOMPARE(host.store().current().catalog->entries.size(), 2);
      dialog->findChild<QPushButton *>("saveCaptureWorkflows")->click();
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(confirmed);
    NativeCaptureWorkflows restarted(path); QCOMPARE(restarted.store().current().catalog->entries.size(), 1); QCOMPARE(restarted.resolve().workflow->id, catalog.entries[0].id);
  }
  void invalidOutputDraftCanBeCorrectedWithoutChangingSavedSettings_data() {
    QTest::addColumn<QString>("mode"); for (const auto *mode : {"counter", "repeat", "relative", "file"}) QTest::newRow(mode) << QString(mode);
  }
  void invalidOutputDraftCanBeCorrectedWithoutChangingSavedSettings() {
    QFETCH(QString, mode); QTemporaryDir dir; const auto path = dir.filePath("workflows.json"); NativeCaptureWorkflows host(path);
    QVERIFY(host.save(*host.store().current().catalog, host.store().revision()).saved); const auto original = bytes(path); QWidget parent; bool corrected = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); });
      auto *folder = dialog->findChild<QLineEdit *>("captureWorkflowSaveDirectory"); auto *pattern = dialog->findChild<QLineEdit *>("captureWorkflowFilenamePattern");
      folder->setText(mode == "relative" ? "relative/folder" : dir.filePath("chosen"));
      pattern->setText(mode == "counter" ? "%i" : mode == "repeat" ? "x%ra{0}" : mode == "file" ? "%rf{/private/input}" : "valid");
      QVERIFY(!dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled()); dialog->findChild<QPushButton *>("saveCaptureWorkflows")->click();
      QCOMPARE(bytes(path), original); QVERIFY(!dialog->findChild<QLabel *>("captureWorkflowPatternHelp")->text().isEmpty());
      folder->setText(dir.filePath("chosen")); pattern->setText("valid-%width"); QCOMPARE(dialog->findChild<QLineEdit *>("captureWorkflowFilenamePreview")->text(), QString("valid-1920.png"));
      dialog->findChild<QPushButton *>("saveCaptureWorkflows")->click(); QVERIFY(dialog->findChild<QPushButton *>("runCaptureWorkflow")->isEnabled()); corrected = true;
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(corrected); NativeCaptureWorkflows reopened(path); QVERIFY(reopened.resolve().workflow);
    QCOMPARE(reopened.resolve().workflow->output.directory, dir.filePath("chosen")); QCOMPARE(reopened.resolve().workflow->output.pattern, QString("valid-%width")); QVERIFY(!QFile::exists(dir.filePath("chosen")));
  }
  void directoryPickerOnlyChangesTheDraftUntilSave_data() {
    QTest::addColumn<bool>("accept"); QTest::newRow("cancel") << false; QTest::newRow("accept") << true;
  }
  void directoryPickerOnlyChangesTheDraftUntilSave() {
    QFETCH(bool, accept); QTemporaryDir dir; NativeCaptureWorkflows host(dir.filePath("workflows.json")); QWidget parent; bool picked = false;
    QTimer::singleShot(0, this, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); const QPointer<QDialog> weak(dialog); const auto close = qScopeGuard([weak] { if (weak) weak->reject(); });
      QTimer::singleShot(0, this, [&] {
        auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()); QVERIFY(picker); const QPointer<QFileDialog> weakPicker(picker); const auto closePicker = qScopeGuard([weakPicker] { if (weakPicker && weakPicker->isVisible()) weakPicker->reject(); });
        QCOMPARE(picker->fileMode(), QFileDialog::Directory); picker->setDirectory(dir.path()); picker->selectFile(dir.path());
        if (accept) QVERIFY(QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection)); else picker->reject(); picked = true;
      });
      dialog->findChild<QPushButton *>("browseCaptureWorkflowDirectory")->click();
      QCOMPARE(dialog->findChild<QLineEdit *>("captureWorkflowSaveDirectory")->text(), accept ? dir.path() : QString{});
      QVERIFY(host.resolve().workflow->output.directory.isEmpty()); QVERIFY(!QFile::exists(dir.filePath("workflows.json")));
    });
    QVERIFY(!manageCaptureWorkflows(host, &parent, [] { return true; })); QVERIFY(picked);
  }
  void configuredSaveFailureKeepsExistingFilesAndIndependentClipboard_data() {
    QTest::addColumn<bool>("collision"); QTest::newRow("collision") << true; QTest::newRow("folder-unavailable") << false;
  }
  void configuredSaveFailureKeepsExistingFilesAndIndependentClipboard() {
    QFETCH(bool, collision); QTemporaryDir dir; auto workflow = defaultCaptureWorkflows().entries[0];
    workflow.actions = QStringList{"save", "image-clipboard", "path-clipboard"}; workflow.output.directory = collision ? dir.path() : dir.filePath("blocked/child"); workflow.output.pattern = "fixed";
    const auto kept = collision ? dir.filePath("fixed.png") : dir.filePath("blocked"); QFile file(kept); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("existing user data"); file.close();
    int copied = 0; CaptureActionPorts ports; ports.clipboard = [&](const QImage &, const CancellationToken &) { ++copied; return ActionOutcome{}; };
    QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::blue); CapturePipeline pipeline(image, workflow.output.directory, workflow.output.pattern, workflowActions(workflow), nullptr, ports, {}, nullptr, workflowSnapshot(workflow)); QSignalSpy done(&pipeline, &CapturePipeline::finished);
    pipeline.start(); QTRY_COMPARE(done.count(), 1); const auto result = receipt(done); QCOMPARE(copied, 1); QVERIFY(result->files.empty()); QCOMPARE(bytes(kept), QByteArray("existing user data"));
    bool save = false, dependency = false;
    for (const auto &step : result->pipeline.orderedResults) {
      if (step.action == "save") { save = true; QCOMPARE(step.state, ActionState::Failed); QCOMPARE(step.diagnostic.value_or(QString{}), collision ? QString("save-name-collision") : QString("save-directory-unavailable")); }
      if (step.action == "path-clipboard") { dependency = true; QCOMPARE(step.state, ActionState::Skipped); }
    }
    QVERIFY(save && dependency);
  }
};
QTEST_MAIN(NativeCaptureWorkflowsTest)
#include "NativeCaptureWorkflowsTest.moc"
