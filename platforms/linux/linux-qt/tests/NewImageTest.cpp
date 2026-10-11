// EC-001/EC-017: independently expected source-over pixels and real PNG profiles.
#include "NewImageSource.h"
#include "image-editor/EditorSession.h"
#include <QColorSpace>
#include <QTest>
#include <cmath>
#include <limits>

using namespace xerahs::editor;
using namespace xerahs::app;
class NewImageTest : public QObject {
  Q_OBJECT
private slots:
  void blankImageUsesExactChosenBackgroundAndDimensions() {
    const auto created = createImage({3, 2, 0x80445566});
    QVERIFY(created.image && !created.error);
    QCOMPARE(created.image->width, 3);
    QCOMPARE(created.image->height, 2);
    QVERIFY(created.image->pixels == std::vector<Argb>(6, 0x80445566));
  }
  void initialPixelsKeepNativeSizeAndAreCroppedOrPaddedAtTopLeft() {
    const ArgbImage initial{3, 2, {0x80FF0000, 0x00010203, 0xFF00FF00, 0xFF0000FF, 0x40ABCDEF, 0xFFFFFFFF}};
    const auto created = createImage({2, 3}, &initial);
    QVERIFY(created.image);
    QVERIFY(created.image->pixels == (std::vector<Argb>{0x80FF0000, 0x00010203, 0xFF0000FF, 0x40ABCDEF, 0, 0}));
    QVERIFY(initial.pixels ==
            (std::vector<Argb>{0x80FF0000, 0x00010203, 0xFF00FF00, 0xFF0000FF, 0x40ABCDEF, 0xFFFFFFFF}));
  }
  void sourceOverHasIndependentAlphaAndChannelResults() {
    const ArgbImage initial{1, 1, {0x80FF0000}};
    auto created = createImage({1, 1, 0xFF0000FF}, &initial);
    QVERIFY(created.image);
    QCOMPARE(created.image->at(0, 0), Argb(0xFF80007F));
    // As=Ad=128/255 -> alpha 192, straight red 170, blue 85.
    created = createImage({1, 1, 0x800000FF}, &initial);
    QVERIFY(created.image);
    QCOMPARE(created.image->at(0, 0), Argb(0xC0AA0055));
    const ArgbImage invisible{1, 1, {0x00ABCDEF}};
    created = createImage({1, 1, 0xFF112233}, &invisible);
    QVERIFY(created.image);
    QCOMPARE(created.image->at(0, 0), Argb(0xFF112233));
  }
  void invalidDimensionsProfilesAndInitialBuffersFailBeforePublication() {
    QCOMPARE(validateNewImage({0, 1}), std::optional<QString>(diagnostic::documentInvalid));
    QCOMPARE(validateNewImage({1, -1}), std::optional<QString>(diagnostic::documentInvalid));
    QCOMPARE(validateNewImage({100001, 1}), std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(validateNewImage({100000, 100000}), std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(validateNewImage({std::numeric_limits<qint64>::max(), 2}),
             std::optional<QString>(diagnostic::documentTooLarge));
    QCOMPARE(validateNewImage({1, 1, 0, NewImageProfile(-1)}), std::optional<QString>(diagnostic::documentInvalid));
    const ArgbImage invalid{2, 2, {1}};
    const auto created = createImage({1, 1}, &invalid);
    QVERIFY(!created.image);
    QCOMPARE(created.error, std::optional<QString>(diagnostic::documentInvalid));
  }
  void cancellationProducesNoPartialPixelsOrDocument() {
    int percent = 0;
    const auto created = createImage({10, 10}, nullptr, {[&] { return percent >= 50; }, [&](int p) { percent = p; }});
    QVERIFY(!created.image);
    QCOMPARE(created.error, std::optional<QString>(QStringLiteral("canvas-cancelled")));
    percent = 0;
    const auto prepared = prepareNewImage({10, 10}, {}, {[&] { return percent >= 80; }, [&](int p) { percent = p; }});
    QVERIFY(!prepared.document && prepared.source.isNull());
    QCOMPARE(prepared.diagnostic, QStringLiteral("canvas-cancelled"));
  }
  void profileMetadataSurvivesPngAndXann_data() {
    QTest::addColumn<int>("profile");
    QTest::newRow("srgb") << int(NewImageProfile::SRgb);
    QTest::newRow("linear-srgb") << int(NewImageProfile::SRgbLinear);
    QTest::newRow("display-p3") << int(NewImageProfile::DisplayP3);
    QTest::newRow("adobe-rgb") << int(NewImageProfile::AdobeRgb);
  }
  void profileMetadataSurvivesPngAndXann() {
    QFETCH(int, profile);
    const NewImageOptions options{3, 2, 0x80808080, NewImageProfile(profile)};
    const auto prepared = prepareNewImage(options);
    QVERIFY2(prepared.document, qPrintable(prepared.diagnostic));
    QCOMPARE(prepared.source.colorSpace(), newImageColorSpace(options.profile));
    QCOMPARE(prepared.source.size(), QSize(3, 2));
    QCOMPARE(qAlpha(prepared.source.pixel(0, 0)), 128);
    if (options.profile == NewImageProfile::SRgb)
      QCOMPARE(qRed(prepared.source.pixel(0, 0)), 128);
    if (options.profile == NewImageProfile::SRgbLinear)
      QVERIFY(std::abs(qRed(prepared.source.pixel(0, 0)) - 55) <= 1);
    QVERIFY(prepared.document->imagePath.isEmpty() && prepared.document->imageHash.isEmpty());
    QVERIFY(prepared.document->annotations.empty());
    const QImage decoded = QImage::fromData(prepared.document->sourceImagePng, "PNG");
    QCOMPARE(decoded, prepared.source);
    QCOMPARE(decoded.colorSpace(), prepared.source.colorSpace());
    const auto bytes = writeXann(*prepared.document);
    QVERIFY(bytes);
    const auto reopened = readXann(*bytes);
    QVERIFY(reopened.document);
    QCOMPARE(reopened.document->sourceImagePng, prepared.document->sourceImagePng);
  }
  void initialColourConversionChangesOnlyThePrivateNewSource() {
    QImage initial(2, 1, QImage::Format_ARGB32);
    initial.fill(qRgba(128, 0, 0, 80));
    initial.setColorSpace(QColorSpace::SRgbLinear);
    const QImage before = initial;
    const auto prepared = prepareNewImage({3, 2}, initial);
    QVERIFY(prepared.document);
    QCOMPARE(prepared.source.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(std::abs(qRed(prepared.source.pixel(0, 0)) - 188) <= 1);
    QCOMPARE(qAlpha(prepared.source.pixel(0, 0)), 80);
    QCOMPARE(prepared.source.pixel(2, 1), QRgb(0));
    QCOMPARE(initial, before);
    QCOMPARE(initial.colorSpace(), before.colorSpace());
  }
};
QTEST_GUILESS_MAIN(NewImageTest)
#include "NewImageTest.moc"
