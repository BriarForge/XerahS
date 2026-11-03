#include "CaptureFileOutput.h"
#include <QApplication>
#include <QClipboard>
#include <QImage>
#include <QTest>
#include <QUrl>
using namespace xerahs::app;
using namespace xerahs::actions;
namespace {
QImage pixels() { QImage image(3, 2, QImage::Format_ARGB32); image.fill(QColor(20, 40, 60, 80)); return image; }
}
class ClipboardWorkflowTest final : public QObject {
  Q_OBJECT
private slots:
  void init() { QApplication::clipboard()->setText("KEEP private clipboard sentinel"); }
  void supportedFormatsPublishOnlyThePreparedRepresentation_data() {
    QTest::addColumn<int>("format");
    for (const auto &row : {std::pair{"image", WorkflowClipboardFormat::Image}, {"file", WorkflowClipboardFormat::File},
        {"path", WorkflowClipboardFormat::Path}, {"url", WorkflowClipboardFormat::Url}, {"ocr-text", WorkflowClipboardFormat::Text}})
      QTest::newRow(row.first) << int(row.second);
  }
  void supportedFormatsPublishOnlyThePreparedRepresentation() {
    QFETCH(int, format); const auto kind = WorkflowClipboardFormat(format);
    auto offer = std::make_unique<QMimeData>();
    const QString privateText = "PRIVATE recognized content", path = "/tmp/PRIVATE capture ü #.png", url = "https://example.invalid/PRIVATE?token=PRIVATE";
    switch (kind) {
      case WorkflowClipboardFormat::Image: offer->setImageData(pixels()); break;
      case WorkflowClipboardFormat::File: offer->setUrls({QUrl::fromLocalFile(path)}); break;
      case WorkflowClipboardFormat::Path: offer->setText(path); break;
      case WorkflowClipboardFormat::Url: offer->setText(url); break;
      case WorkflowClipboardFormat::Text: offer->setText(privateText); break;
    }
    // Preparing an offer has no effect on the currently owned clipboard.
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP private clipboard sentinel"));
    const auto result = publishWorkflowClipboard(std::move(offer), kind);
    const auto *actual = QApplication::clipboard()->mimeData(); QVERIFY(actual);
    QCOMPARE(actual->data(clipboardPrivacyMime), QByteArray("private"));
    if (kind == WorkflowClipboardFormat::Image) QCOMPARE(qvariant_cast<QImage>(actual->imageData()), pixels());
    else if (kind == WorkflowClipboardFormat::File) QCOMPARE(actual->urls(), QList<QUrl>{QUrl::fromLocalFile(path)});
    else QCOMPARE(actual->text(), kind == WorkflowClipboardFormat::Path ? path : kind == WorkflowClipboardFormat::Url ? url : privateText);
    QVERIFY(!result.diagnostic || !result.diagnostic->contains("PRIVATE"));
    if (QApplication::clipboard()->ownsClipboard()) { QCOMPARE(result.state, ActionState::Succeeded); QVERIFY(!result.outcomeUnknown); }
    else { QCOMPARE(result.state, ActionState::Failed); QVERIFY(!result.outcomeUnknown); QCOMPARE(result.diagnostic, std::optional<QString>{"clipboard-unavailable"}); }
  }
  void unsupportedFormatsPreserveTheExistingClipboard_data() {
    QTest::addColumn<int>("format");
    for (const auto &row : {std::pair{"text-given-image-only", WorkflowClipboardFormat::Text}, {"file-given-image-only", WorkflowClipboardFormat::File},
        {"path-given-image-only", WorkflowClipboardFormat::Path}, {"url-given-image-only", WorkflowClipboardFormat::Url}})
      QTest::newRow(row.first) << int(row.second);
  }
  void unsupportedFormatsPreserveTheExistingClipboard() {
    QFETCH(int, format); auto offer = std::make_unique<QMimeData>(); offer->setImageData(pixels());
    const auto result = publishWorkflowClipboard(std::move(offer), WorkflowClipboardFormat(format));
    QCOMPARE(result.state, ActionState::Failed); QCOMPARE(result.diagnostic, std::optional<QString>{"unsupported-content"});
    QVERIFY(!result.outcomeUnknown); QCOMPARE(QApplication::clipboard()->text(), QString("KEEP private clipboard sentinel"));
  }
  void invalidReadyPayloadsNeverClearTheClipboard() {
    auto image = std::make_unique<QMimeData>(); image->setImageData(QImage{});
    QCOMPARE(publishWorkflowClipboard(std::move(image), WorkflowClipboardFormat::Image).diagnostic, std::optional<QString>{"unsupported-content"});
    auto url = std::make_unique<QMimeData>(); url->setText("PRIVATE invalid URL");
    QCOMPARE(publishWorkflowClipboard(std::move(url), WorkflowClipboardFormat::Url).diagnostic, std::optional<QString>{"unsupported-content"});
    auto files = std::make_unique<QMimeData>(); files->setUrls({QUrl("https://example.invalid/PRIVATE")});
    QCOMPARE(publishWorkflowClipboard(std::move(files), WorkflowClipboardFormat::File).diagnostic, std::optional<QString>{"unsupported-content"});
    QCOMPARE(publishWorkflowClipboard({}, WorkflowClipboardFormat::Text).diagnostic, std::optional<QString>{"unsupported-content"});
    QCOMPARE(QApplication::clipboard()->text(), QString("KEEP private clipboard sentinel"));
  }
  void synchronousReplacementCannotBeClaimedAsSuccessOrOverwriteForeignContent() {
    bool replaced = false;
    const auto connection = connect(QApplication::clipboard(), &QClipboard::dataChanged, [&] {
      if (!replaced) { replaced = true; QApplication::clipboard()->setText("FOREIGN PRIVATE content"); }
    });
    auto offer = std::make_unique<QMimeData>(); offer->setText("PRIVATE offered result");
    const auto result = publishWorkflowClipboard(std::move(offer), WorkflowClipboardFormat::Text);
    disconnect(connection);
    QVERIFY(replaced); QCOMPARE(result.state, ActionState::Failed); QVERIFY(!result.outcomeUnknown);
    QCOMPARE(result.diagnostic, std::optional<QString>{"clipboard-unavailable"});
    QCOMPARE(QApplication::clipboard()->text(), QString("FOREIGN PRIVATE content"));
  }
  void explicitPublicClassificationSurvivesPublication() {
    auto offer = std::make_unique<QMimeData>(); offer->setText("public result");
    publishWorkflowClipboard(std::move(offer), WorkflowClipboardFormat::Text, ClipboardPrivacy::Public);
    QCOMPARE(QApplication::clipboard()->mimeData()->data(clipboardPrivacyMime), QByteArray("public"));
  }
};
QTEST_MAIN(ClipboardWorkflowTest)
#include "ClipboardWorkflowTest.moc"
