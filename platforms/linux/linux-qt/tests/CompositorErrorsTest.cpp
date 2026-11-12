// A failed compositor capture reports a stable diagnostic code. The interface shows one
// translated sentence per known code, and it never shows the code, an output name or a path.
#include "capture/CompositorErrors.h"
#include "capture/CompositorFrame.h"

#include <QCoreApplication>
#include <QTest>
#include <QStringList>
#include <QTranslator>

#include <algorithm>

using xerahs::app::compositorCaptureFailureMessage;
using xerahs::app::kCompositorCaptureFailed;
using xerahs::app::kCompositorCaptureTimedOut;
using xerahs::app::kCompositorCaptureUnsupported;
using xerahs::app::kCompositorLayoutUnproven;
using xerahs::app::kCompositorSourceMismatch;

namespace {

const QString kUnsupportedMessage = QStringLiteral(
    "This desktop does not support screenshots with the pointer. Turn off Include the pointer in screenshots to use the Screenshot portal.");
const QString kLayoutMessage = QStringLiteral(
    "Screenshots with the pointer currently require one named display with integer scaling and no rotation. Turn off Include the pointer in screenshots to use the Screenshot portal.");
const QString kMismatchMessage = QStringLiteral(
    "The compositor frame does not match the display. Capture was stopped. Try again after checking display settings.");
const QString kTimedOutMessage = QStringLiteral(
    "The desktop did not return a screenshot in time. Try again or turn off Include the pointer in screenshots.");
const QString kGenericMessage = QStringLiteral(
    "The desktop could not provide a screenshot with the pointer. Try again or turn off Include the pointer in screenshots.");

// Records what the interface asks to translate, so the translation context is proven at run time.
class RecordingTranslator final : public QTranslator {
public:
  mutable QStringList contexts;
  mutable QStringList sources;
  QString translate(const char *context, const char *sourceText, const char *, int) const override {
    contexts << QString::fromUtf8(context);
    sources << QString::fromUtf8(sourceText);
    return QStringLiteral("TRANSLATED");
  }
};

}  // namespace

class CompositorErrorsTest : public QObject {
  Q_OBJECT
private slots:
  void failureMessageFollowsTheDiagnostic_data() {
    QTest::addColumn<QString>("diagnostic");
    QTest::addColumn<QString>("expected");
    QTest::newRow("capture-unsupported") << kCompositorCaptureUnsupported << kUnsupportedMessage;
    QTest::newRow("layout-unproven") << kCompositorLayoutUnproven << kLayoutMessage;
    QTest::newRow("source-mismatch") << kCompositorSourceMismatch << kMismatchMessage;
    QTest::newRow("capture-timed-out") << kCompositorCaptureTimedOut << kTimedOutMessage;
    QTest::newRow("capture-source-failed") << kCompositorCaptureFailed << kGenericMessage;
    QTest::newRow("empty") << QString() << kGenericMessage;
  }

  void failureMessageFollowsTheDiagnostic() {
    QFETCH(QString, diagnostic);
    QFETCH(QString, expected);
    const QString message = compositorCaptureFailureMessage(diagnostic);
    QCOMPARE(message, expected);
    QVERIFY(!message.isEmpty());
    // The diagnostic code itself never reaches the user.
    if (!diagnostic.isEmpty()) QVERIFY2(!message.contains(diagnostic), qPrintable(diagnostic));
  }

  void unknownDiagnosticIsNeverEchoed() {
    const QString secret = QStringLiteral("token-9f3a2b7c");
    const QString path = QStringLiteral("/home/user/.config/xerahs/upload.secret");
    const QString output = QStringLiteral("DP-2");
    const QStringList unknown{secret + path, path, output, QStringLiteral("compositor-layout-unproven-typo"),
                              QStringLiteral("compositor-capture-unsupported ")};
    for (const auto &diagnostic : unknown) {
      const QString message = compositorCaptureFailureMessage(diagnostic);
      QCOMPARE(message, kGenericMessage);
      QVERIFY2(!message.contains(diagnostic), qPrintable(diagnostic));
      QVERIFY2(!message.contains(secret), qPrintable(secret));
      QVERIFY2(!message.contains(path), qPrintable(path));
      QVERIFY2(!message.contains(output), qPrintable(output));
    }
  }

  void messageUsesTheCompositorCaptureTranslationContext() {
    RecordingTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    const QString message = compositorCaptureFailureMessage(kCompositorCaptureUnsupported);
    QCoreApplication::removeTranslator(&translator);
    QCOMPARE(message, QStringLiteral("TRANSLATED"));
    QVERIFY(!translator.contexts.isEmpty());
    QVERIFY2(std::all_of(translator.contexts.cbegin(), translator.contexts.cend(),
                          [](const QString &context) { return context == QLatin1String("CompositorCapture"); }),
             qPrintable(translator.contexts.join(QLatin1Char(','))));
    QVERIFY(translator.sources.contains(kUnsupportedMessage));
  }
};

QTEST_GUILESS_MAIN(CompositorErrorsTest)
#include "CompositorErrorsTest.moc"
