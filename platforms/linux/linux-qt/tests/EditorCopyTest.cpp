#include "EditorCopy.h"
#include "EditorSource.h"
#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QMimeData>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <thread>
#include <stdexcept>
using namespace xerahs::app;
using namespace xerahs::actions;
using namespace xerahs::editor;
namespace {
LoadedSource fixture() {
  QImage image(40, 30, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); image.fill(qRgba(12, 34, 56, 73));
  auto source = prepareCapturedSource(image); EditorSession session(*source.document); session.createRectangle({5, 5}, {20, 20}); source.document = session.document(); return source;
}
struct Gate { std::atomic<bool> entered{false}, released{false}, finished{false}; };
EditorCopyPorts gated(const std::shared_ptr<Gate> &gate) {
  EditorCopyPorts ports; ports.beforeRender = [gate] {
    gate->entered.store(true); while (!gate->released.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); gate->finished.store(true);
  }; return ports;
}
}
class EditorCopyTest : public QObject {
  Q_OBJECT
private slots:
  void init() { QApplication::clipboard()->setText("KEEP"); }
  void workerUsesOneSnapshotAndPublishesOnlyOnTheGuiWithOwnedAlphaAndPng() {
    auto source = fixture(); const auto original = source.image; const auto gate = std::make_shared<Gate>(); auto ports = gated(gate);
    bool worker = false, gui = false, changed = false; int publications = 0; EditorCopyPayloadPtr retained;
    ports.encode = [&](const QImage &image, QIODevice *device) { worker = QThread::currentThread() != qApp->thread(); return image.save(device, "PNG"); };
    ports.publish = [&](const EditorCopyPayload &payload) {
      gui = QThread::currentThread() == qApp->thread(); ++publications; retained = std::make_shared<EditorCopyPayload>(payload); return ActionOutcome{};
    };
    QWidget owner; QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { if (gate->entered.load() && !changed) { changed = true; source.image.fill(Qt::green); source.document->annotations.clear(); gate->released.store(true); } });
    timer.start(5); const auto result = runEditorCopy(source.image, *source.document, &owner, ports); timer.stop();
    QVERIFY(worker && gui && changed); QCOMPARE(publications, 1); QCOMPARE(result.action.state, ActionState::Succeeded); QVERIFY(!result.action.outcomeUnknown);
    QVERIFY(retained); source = {}; QCOMPARE(retained->image.pixel(0, 0), original.pixel(0, 0)); QCOMPARE(retained->image.pixel(5, 12), qRgb(255, 0, 0));
    QCOMPARE(QImage::fromData(retained->png, "PNG").convertToFormat(QImage::Format_ARGB32), retained->image);
    QCOMPARE(retained->image.colorSpace(), QColorSpace(QColorSpace::SRgb)); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void nativeOfferHasBothInteroperableRepresentationsAndReportsOwnership() {
    const auto source = fixture(); QWidget owner; const auto result = runEditorCopy(source.image, *source.document, &owner);
    QVERIFY(result.publicationAttempted && result.payload); auto *clipboard = QApplication::clipboard(); const auto *mime = clipboard->mimeData(); QVERIFY(mime);
    QVERIFY(mime->hasImage() && mime->hasFormat("image/png")); QCOMPARE(mime->imageData().value<QImage>(), result.payload->image);
    QCOMPARE(QImage::fromData(mime->data("image/png"), "PNG").convertToFormat(QImage::Format_ARGB32), result.payload->image);
    QCOMPARE(qAlpha(result.payload->image.pixel(0, 0)), 73);
    if (clipboard->ownsClipboard()) { QCOMPARE(result.action.state, ActionState::Succeeded); QVERIFY(!result.action.outcomeUnknown); }
    else { QVERIFY(result.action.outcomeUnknown); QCOMPARE(result.action.diagnostic, std::optional<QString>{"clipboard-delivery-unknown"}); }
  }
  void synchronousReplacementCannotBecomeAcknowledgedCopyOrBeCleared() {
    const auto source = fixture(); QWidget owner; bool replaced = false;
    const auto connection = connect(QApplication::clipboard(), &QClipboard::dataChanged, [&] {
      if (!replaced) { replaced = true; QApplication::clipboard()->setText("FOREIGN"); }
    });
    const auto result = runEditorCopy(source.image, *source.document, &owner); disconnect(connection);
    QVERIFY(replaced && result.action.outcomeUnknown); QCOMPARE(QApplication::clipboard()->text(), QString("FOREIGN"));
  }
  void cancelBeforePublicationAndDestructionCannotPublishLater_data() { QTest::addColumn<bool>("destroy"); QTest::newRow("Cancel button") << false; QTest::newRow("owner destruction") << true; }
  void cancelBeforePublicationAndDestructionCannotPublishLater() {
    QFETCH(bool, destroy); auto source = fixture(); const auto gate = std::make_shared<Gate>(); auto ports = gated(gate); std::atomic<int> publications{0};
    ports.publish = [&](const EditorCopyPayload &) { ++publications; return ActionOutcome{}; };
    auto *owner = new QWidget; QTimer timer; bool acted = false;
    connect(&timer, &QTimer::timeout, [&] {
      if (!gate->entered.load() || acted) return; acted = true;
      if (destroy) { delete owner; owner = nullptr; }
      else { auto *dialog = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->findChild<QPushButton *>()->click(); gate->released.store(true); }
    });
    timer.start(5); const auto result = runEditorCopy(source.image, *source.document, owner, ports); timer.stop();
    QVERIFY(acted); QCOMPARE(result.action.state, ActionState::Cancelled); QVERIFY(!result.publicationAttempted); QCOMPARE(result.abandoned, destroy);
    if (destroy) { QVERIFY(!gate->finished.load()); source = {}; gate->released.store(true); QTRY_VERIFY(gate->finished.load()); }
    delete owner; QCOMPARE(publications.load(), 0); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void encodingFailureAndMissingProviderDoNotChangeTheClipboard() {
    const auto source = fixture(); QWidget owner; EditorCopyPorts ports; int publications = 0;
    ports.publish = [&](const EditorCopyPayload &) { ++publications; return ActionOutcome{}; };
    ports.encode = [](const QImage &, QIODevice *device) { device->write("partial"); return false; };
    const auto failed = runEditorCopy(source.image, *source.document, &owner, ports); QCOMPARE(failed.action.state, ActionState::Failed); QVERIFY(!failed.payload && !failed.publicationAttempted);
    QCOMPARE(failed.action.diagnostic, std::optional<QString>{"clipboard-encode-failed"}); QCOMPARE(publications, 0);
    ports.publish = {}; const auto absent = runEditorCopy(source.image, *source.document, &owner, ports); QCOMPARE(absent.action.diagnostic, std::optional<QString>{"clipboard-unavailable"});
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void unsupportedVisibleObjectsFailWithoutSilentlyDroppingThem() {
    auto source = fixture(); const auto id = QUuid::createUuid(); source.document->annotations.emplace_back(UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-shape"}, {"visible", true}}});
    QWidget owner; EditorCopyPorts ports; int publications = 0; ports.publish = [&](const EditorCopyPayload &) { ++publications; return ActionOutcome{}; };
    const auto result = runEditorCopy(source.image, *source.document, &owner, ports); QCOMPARE(result.action.state, ActionState::Failed); QVERIFY(!result.payload); QCOMPARE(publications, 0);
    QCOMPARE(result.action.diagnostic, std::optional<QString>{"clipboard-annotation-unsupported"});
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP"));
  }
  void knownFailureRetryKeepsOnePayloadWithoutRenderingOrEncodingAgain() {
    const auto source = fixture(); QWidget owner; int renders = 0, encodes = 0, publications = 0; EditorCopyPorts ports;
    ports.beforeRender = [&] { ++renders; }; ports.encode = [&](const QImage &image, QIODevice *device) { ++encodes; return image.save(device, "PNG"); };
    ports.publish = [&](const EditorCopyPayload &) { ++publications; return ActionOutcome{ActionState::Failed, "PRIVATE host diagnostic", std::nullopt}; };
    const auto first = runEditorCopy(source.image, *source.document, &owner, ports); QVERIFY(first.payload); QCOMPARE(first.action.diagnostic, std::optional<QString>{"clipboard-failed"});
    bool same = false; ports.publish = [&](const EditorCopyPayload &payload) { ++publications; same = payload.png == first.payload->png; return ActionOutcome{}; };
    const auto repaired = runEditorCopy(source.image, *source.document, &owner, ports, first.payload);
    QVERIFY(same); QCOMPARE(repaired.action.state, ActionState::Succeeded); QCOMPARE(repaired.payload, first.payload); QCOMPARE(renders, 1); QCOMPARE(encodes, 1); QCOMPARE(publications, 2);
  }
  void callbackExceptionAndLateCancellationKeepActualDeliveryFacts_data() { QTest::addColumn<bool>("throws"); QTest::newRow("exception after effect") << true; QTest::newRow("late cancellation") << false; }
  void callbackExceptionAndLateCancellationKeepActualDeliveryFacts() {
    QFETCH(bool, throws); const auto source = fixture(); QWidget owner; EditorCopyPorts ports; bool progressPresent = false;
    ports.publish = [&](const EditorCopyPayload &payload) {
      QApplication::clipboard()->setImage(payload.image);
      if (throws) throw std::runtime_error("PRIVATE callback payload");
      auto *dialog = qobject_cast<QProgressDialog *>(QApplication::activeModalWidget()); progressPresent = dialog;
      if (dialog) dialog->findChild<QPushButton *>()->click(); return ActionOutcome{};
    };
    const auto result = runEditorCopy(source.image, *source.document, &owner, ports); QVERIFY(result.publicationAttempted); QVERIFY(!QApplication::clipboard()->image().isNull());
    if (throws) { QVERIFY(result.action.outcomeUnknown); QCOMPARE(result.action.diagnostic, std::optional<QString>{"clipboard-delivery-unknown"}); }
    else { QVERIFY(progressPresent); QCOMPARE(result.action.state, ActionState::Succeeded); QVERIFY(result.cancellationArrivedDuring); }
  }
  void colourConversionIsDisclosedAndDoesNotMutateTheSource() {
    QImage image(4, 4, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgbLinear); image.fill(qRgba(128, 128, 128, 73)); const auto source = prepareCapturedSource(image);
    QWidget owner; EditorCopyPorts ports; ports.publish = [](const EditorCopyPayload &) { return ActionOutcome{}; };
    const auto result = runEditorCopy(source.image, *source.document, &owner, ports); QVERIFY(result.payload);
    QVERIFY(result.payload->disclosures.contains("clipboard-colour-converted-to-srgb")); QCOMPARE(qRed(result.payload->image.pixel(0, 0)), 188); QCOMPARE(qAlpha(result.payload->image.pixel(0, 0)), 73);
    QCOMPARE(source.image, image); QCOMPARE(source.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
  }
  void malformedPublicationFactsStayUnknownAfterAnEffect_data() {
    QTest::addColumn<int>("kind"); QTest::newRow("skipped") << 0; QTest::newRow("invalid enum") << 1; QTest::newRow("selection override") << 2;
  }
  void malformedPublicationFactsStayUnknownAfterAnEffect() {
    QFETCH(int, kind); const auto source = fixture(); QWidget owner; EditorCopyPorts ports;
    ports.publish = [&](const EditorCopyPayload &payload) {
      QApplication::clipboard()->setImage(payload.image); ActionOutcome response;
      if (kind == 0) response.state = ActionState::Skipped;
      if (kind == 1) response.state = static_cast<ActionState>(42);
      if (kind == 2) response.replacementSelection = QStringList{"PRIVATE"};
      return response;
    };
    const auto result = runEditorCopy(source.image, *source.document, &owner, ports); QVERIFY(result.action.outcomeUnknown);
    QCOMPARE(result.action.diagnostic, std::optional<QString>{"clipboard-delivery-unknown"}); QVERIFY(!QApplication::clipboard()->image().isNull());
  }
};
QTEST_MAIN(EditorCopyTest)
#include "EditorCopyTest.moc"
