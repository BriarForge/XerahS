// CORE-009: a pseudo-locale that lengthens every translated string by 40% neither clips controls nor
// overflows the screen, and a string missing from the translation falls back to its source text.
#include "QrCodeDialog.h"
#include "SettingsDialog.h"
#include "UploadDestinationsDialog.h"
#include "settings/ApplicationSettings.h"
#include "workflows/WorkflowShortcuts.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QScreen>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

using namespace xerahs::app;

namespace {
class PseudoLocale final : public QTranslator {
public:
  QSet<QString> missing;
  QString translate(const char *, const char *source, const char *, int) const override {
    const QString text = QString::fromUtf8(source);
    if (missing.contains(text)) return {};  // empty: Qt falls back to the source string
    return text + QString(int(std::ceil(text.size() * 0.4)), QChar(0x2022));
  }
  bool isEmpty() const override { return false; }
};
QString visible(QString text) { return text.remove(QLatin1Char('&')); }
}  // namespace

class LocalizationExpansionTest : public QObject {
  Q_OBJECT
private slots:
  void expandedTextIsNotClippedAndFitsTheScreen() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QrCodeDialog dialog;
      dialog.setText(QStringLiteral("hello"));
      dialog.show();
      QVERIFY(QTest::qWaitForWindowExposed(&dialog));
      QCOMPARE(dialog.windowTitle(), QStringLiteral("QR Code") + QString(int(std::ceil(7 * 0.4)), QChar(0x2022)));
      int checked = 0;
      for (auto *button : dialog.findChildren<QAbstractButton *>()) {
        if (button->text().isEmpty()) continue;
        QVERIFY2(button->text().contains(QChar(0x2022)), qPrintable(button->text()));
        QVERIFY2(button->width() >= button->fontMetrics().horizontalAdvance(visible(button->text())), qPrintable(button->text()));
        ++checked;
      }
      for (auto *label : dialog.findChildren<QLabel *>()) {
        if (label->text().isEmpty() || label->wordWrap() || label->pixmap().cacheKey() != 0) continue;
        QVERIFY2(label->width() >= label->fontMetrics().horizontalAdvance(visible(label->text())), qPrintable(label->text()));
        if (label->text().contains(QChar(0x2022))) ++checked;
      }
      QVERIFY(checked >= 6);
      const auto available = dialog.screen()->availableGeometry();
      QVERIFY(dialog.minimumSizeHint().width() <= available.width());
      QVERIFY(dialog.minimumSizeHint().height() <= available.height());
      QVERIFY(dialog.width() >= dialog.minimumSizeHint().width());
    }
    QApplication::removeTranslator(&pseudo);
  }

  void missingTranslationFallsBackToTheSourceText() {
    PseudoLocale pseudo;
    pseudo.missing = {QStringLiteral("&Upload"), QStringLiteral("QR Code")};
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QrCodeDialog dialog;
      QCOMPARE(dialog.windowTitle(), QStringLiteral("QR Code"));
      bool sawUpload = false, sawTranslated = false;
      for (auto *button : dialog.findChildren<QAbstractButton *>()) {
        QVERIFY(!button->text().isEmpty() || button->objectName().isEmpty());
        if (button->objectName() == QLatin1String("qr-upload")) { QCOMPARE(button->text(), QStringLiteral("&Upload")); sawUpload = true; }
        if (button->objectName() == QLatin1String("qr-copy")) { QVERIFY(button->text().contains(QChar(0x2022))); sawTranslated = true; }
      }
      QVERIFY(sawUpload && sawTranslated);
    }
    QApplication::removeTranslator(&pseudo);
    QrCodeDialog plain;
    QCOMPARE(plain.windowTitle(), QStringLiteral("QR Code"));
  }

  void preferencesDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      xerahs::settings::ApplicationSettingsStore store(dir.filePath("ApplicationSettings.json"), dir.path());
      xerahs::app::WorkflowShortcutActions shortcuts([] { return xerahs::workflows::WorkflowShortcutPlan{}; }, [] { return true; });
      xerahs::app::SettingsDialog dialog(store, dir.path(), {}, shortcuts, [] {});
      dialog.show();
      int checked = 0;
      for (auto *button : dialog.findChildren<QAbstractButton *>()) {
        if (button->text().isEmpty()) continue;
        QVERIFY2(button->text().contains(QChar(0x2022)), qPrintable(button->text()));
        ++checked;
      }
      for (auto *label : dialog.findChildren<QLabel *>()) {
        if (label->text().isEmpty()) continue;
        // The import report counts machine dispositions; it is a diagnostic, not translatable prose.
        if (label->objectName() == QLatin1String("settingsImportReport")) continue;
        QVERIFY2(label->text().contains(QChar(0x2022)), qPrintable(label->text()));
        ++checked;
      }
      QVERIFY(checked >= 15);
      bool sawSound = false;
      for (auto *box : dialog.findChildren<QCheckBox *>()) {
        if (!box->accessibleName().startsWith(QLatin1String("Play capture sound"))) continue;
        QVERIFY2(box->accessibleName().contains(QChar(0x2022)), qPrintable(box->accessibleName()));
        sawSound = true;
      }
      QVERIFY(sawSound);
    }
    QApplication::removeTranslator(&pseudo);
  }

  void uploadDestinationsDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      xerahs::uploads::DestinationCatalog catalog(dir.filePath(QStringLiteral("destinations.json")));
      xerahs::uploads::MemoryCredentialStore credentials;
      UploadDestinationsPorts ports;
      ports.chooseSxcu = [](QWidget *) { return QString(); };
      ports.askSecret = [](QWidget *, const QString &) { return std::optional<QString>{}; };
      ports.confirmRemove = [](QWidget *, const QString &) { return true; };
      ports.report = [](QWidget *, const QString &) {};
      UploadDestinationsDialog dialog(catalog, credentials, ports);
      int checked = 0;
      for (auto *button : dialog.findChildren<QAbstractButton *>()) {
        if (button->text().isEmpty()) continue;
        QVERIFY2(button->text().contains(QChar(0x2022)), qPrintable(button->text()));
        ++checked;
      }
      for (auto *label : dialog.findChildren<QLabel *>()) {
        if (label->text().isEmpty()) continue;
        QVERIFY2(label->text().contains(QChar(0x2022)), qPrintable(label->text()));
        ++checked;
      }
      QVERIFY(checked >= 5);
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }
};

QTEST_MAIN(LocalizationExpansionTest)
#include "LocalizationExpansionTest.moc"
