// CORE-009: a pseudo-locale that lengthens every translated string by 40% neither clips controls nor
// overflows the screen, and a string missing from the translation falls back to its source text.
#include "AppearanceDialog.h"
#include "ColorPickerDialog.h"
#include "DrawingDefaultsDialog.h"
#include "FileUploadController.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageComparisonDialog.h"
#include "ImageInsertDialog.h"
#include "ImageRecognitionDialog.h"
#include "NativePipelineJournal.h"
#include "NewImageDialog.h"
#include "QrCodeDialog.h"
#include "RectanglePropertiesDialog.h"
#include "SettingsDialog.h"
#include "UploadDestinationsDialog.h"
#include "UrlOpenDialog.h"
#include "VideoConverterDialog.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "image-editor/AnnotationDocument.h"
#include "media/VideoConversion.h"
#include "settings/ApplicationSettings.h"
#include "uploads/CredentialStore.h"
#include "watch/WatchFolders.h"
#include "workflows/WorkflowShortcuts.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QScreen>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTranslator>
#include <QWidget>

#include <memory>

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

// A runner that never converts: the dialog's text must not depend on an encoder.
class IdleRunner final : public xerahs::media::ConversionRunner {
public:
  void start(const xerahs::media::Request &) override {}
  void cancel() override {}
  bool busy() const override { return false; }
};

// Counts the button and label texts that carry the pseudo-locale marker, and reports the first one that
// does not. Skipped object names hold runtime data (a size in pixels), not prose.
int translatedTexts(QDialog &dialog, const QStringList &runtimeData) {
  const auto skipped = [&runtimeData](const QString &name) {
    for (const auto &prefix : runtimeData)
      if (name.startsWith(prefix)) return true;
    return false;
  };
  int checked = 0;
  for (auto *button : dialog.findChildren<QAbstractButton *>()) {
    if (button->text().isEmpty() || skipped(button->objectName())) continue;
    if (!button->text().contains(QChar(0x2022))) {
      QTest::qFail(qPrintable(QStringLiteral("untranslated button text: %1").arg(button->text())), __FILE__, __LINE__);
      return checked;
    }
    ++checked;
  }
  for (auto *label : dialog.findChildren<QLabel *>()) {
    if (label->text().isEmpty() || skipped(label->objectName())) continue;
    if (!label->text().contains(QChar(0x2022))) {
      QTest::qFail(qPrintable(QStringLiteral("untranslated label text: %1").arg(label->text())), __FILE__, __LINE__);
      return checked;
    }
    ++checked;
  }
  return checked;
}

// The preference dialogs are opened by their own entry points, which run a modal event loop. A timer
// reaches the dialog once it appears, so its texts are collected there and the dialog is closed again.
struct CollectedTexts {
  bool entered = false;
  QString title;
  QStringList buttons, labels;
};
template <typename Open>
CollectedTexts collectDialogTexts(Open open) {
  CollectedTexts collected;
  QTimer probe;
  probe.setInterval(5);
  QObject::connect(&probe, &QTimer::timeout, &probe, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    if (!dialog) return;
    collected.entered = true;
    collected.title = dialog->windowTitle();
    for (auto *button : dialog->findChildren<QAbstractButton *>())
      if (!button->text().isEmpty()) collected.buttons << button->text();
    for (auto *label : dialog->findChildren<QLabel *>()) {
      if (label->text().isEmpty()) continue;
      // The import report counts machine dispositions and the saved-font note names a runtime font
      // family: both are runtime data, not translatable prose.
      if (label->objectName() == QLatin1String("editorPreferenceImportReport")) continue;
      if (label->objectName().endsWith(QLatin1String("FontUnavailable"))) continue;
      collected.labels << label->text();
    }
    probe.stop();
    dialog->reject();
  });
  probe.start();
  open();
  probe.stop();
  return collected;
}

// Returns the collected texts that the pseudo-locale did not translate.
QString untranslatedTexts(const CollectedTexts &collected) {
  QStringList offenders;
  for (const auto &button : collected.buttons) if (!button.contains(QChar(0x2022))) offenders << button;
  for (const auto &label : collected.labels) if (!label.contains(QChar(0x2022))) offenders << label;
  if (!collected.title.contains(QChar(0x2022))) offenders << collected.title;
  return offenders.join(QStringLiteral(" | "));
}
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

  void toolDialogsTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      // The watch-folder dialog needs a service, as the modal dialog test builds it.
      auto credentials = std::make_shared<xerahs::uploads::MemoryCredentialStore>();
      auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
      xerahs::watch::WatchFolders watchStore(dir.filePath("watch"));
      FileUploadPorts uploadPorts; uploadPorts.catalogPath = dir.filePath("destinations.json");
      uploadPorts.credentials = credentials; uploadPorts.history = journal;
      FileUploadController uploads(uploadPorts);
      WatchFolderService watcher(watchStore, uploads, [&] { return dir.filePath("captures"); });
      xerahs::media::NoElements availability;

      WatchFoldersDialog watchDialog(watcher);
      IconConverterDialog iconDialog;
      HashCheckerDialog hashDialog;
      VideoConverterDialog videoDialog(new IdleRunner, &availability);

      // The icon size boxes show the bare pixel count: runtime data, not prose.
      const int watchChecked = translatedTexts(watchDialog, {});
      const int iconChecked = translatedTexts(iconDialog, {QStringLiteral("iconSize")});
      const int hashChecked = translatedTexts(hashDialog, {});
      const int videoChecked = translatedTexts(videoDialog, {});
      QVERIFY2(watchChecked >= 3, qPrintable(QString::number(watchChecked)));
      QVERIFY2(iconChecked >= 3, qPrintable(QString::number(iconChecked)));
      QVERIFY2(hashChecked >= 3, qPrintable(QString::number(hashChecked)));
      QVERIFY2(videoChecked >= 3, qPrintable(QString::number(videoChecked)));
      QVERIFY2(watchDialog.windowTitle().contains(QChar(0x2022)), qPrintable(watchDialog.windowTitle()));
      QVERIFY2(iconDialog.windowTitle().contains(QChar(0x2022)), qPrintable(iconDialog.windowTitle()));
      QVERIFY2(hashDialog.windowTitle().contains(QChar(0x2022)), qPrintable(hashDialog.windowTitle()));
      QVERIFY2(videoDialog.windowTitle().contains(QChar(0x2022)), qPrintable(videoDialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void editorAndPickerDialogsTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      // The editor and picker dialogs the modal dialog test opens as rows 1, 4 and 6.
      QWidget host;
      ColorPickerDialog colourDialog(&host);
      NewImageDialog newImageDialog(&host);
      ImageInsertDialog insertDialog(&host);

      // The eight colour format lines hold colour values and the sampled-pixel line holds
      // coordinates: runtime data, not prose.
      const int colourChecked = translatedTexts(colourDialog, {QStringLiteral("colorFormat"), QStringLiteral("pickedScreenCoordinates")});
      const int newImageChecked = translatedTexts(newImageDialog, {});
      const int insertChecked = translatedTexts(insertDialog, {});
      QVERIFY2(colourChecked >= 3, qPrintable(QString::number(colourChecked)));
      QVERIFY2(newImageChecked >= 3, qPrintable(QString::number(newImageChecked)));
      QVERIFY2(insertChecked >= 3, qPrintable(QString::number(insertChecked)));
      QVERIFY2(colourDialog.windowTitle().contains(QChar(0x2022)), qPrintable(colourDialog.windowTitle()));
      QVERIFY2(newImageDialog.windowTitle().contains(QChar(0x2022)), qPrintable(newImageDialog.windowTitle()));
      QVERIFY2(insertDialog.windowTitle().contains(QChar(0x2022)), qPrintable(insertDialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void comparisonUrlAndRecognitionDialogsTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      // The comparison, URL and recognition dialogs the modal dialog test opens as rows 9, 5 and 7.
      QWidget host;
      QImage comparisonImage(64, 48, QImage::Format_ARGB32); comparisonImage.fill(Qt::white);
      ImageComparisonDialog comparisonDialog(xerahs::editor::AnnotationDocument{}, comparisonImage, &host);
      UrlOpenDialog urlDialog(&host);
      ImageRecognitionDialog recognitionDialog(ImageRecognitionKind::QrCode, &host);

      // The comparison input, divider percentage, canvas and status labels hold a chosen file
      // name, a live percentage, a canvas size and a live load sentence: runtime data, not prose.
      // The URL status label holds a live transfer sentence for the same reason.
      const int comparisonChecked = translatedTexts(comparisonDialog, {QStringLiteral("comparisonInput"),
                                                                        QStringLiteral("comparisonPercent"),
                                                                        QStringLiteral("comparisonGeometry"),
                                                                        QStringLiteral("comparisonStatus")});
      const int urlChecked = translatedTexts(urlDialog, {QStringLiteral("openImageUrlStatus")});
      const int recognitionChecked = translatedTexts(recognitionDialog, {});
      QVERIFY2(comparisonChecked >= 3, qPrintable(QString::number(comparisonChecked)));
      QVERIFY2(urlChecked >= 3, qPrintable(QString::number(urlChecked)));
      QVERIFY2(recognitionChecked >= 3, qPrintable(QString::number(recognitionChecked)));
      QVERIFY2(comparisonDialog.windowTitle().contains(QChar(0x2022)), qPrintable(comparisonDialog.windowTitle()));
      QVERIFY2(urlDialog.windowTitle().contains(QChar(0x2022)), qPrintable(urlDialog.windowTitle()));
      QVERIFY2(recognitionDialog.windowTitle().contains(QChar(0x2022)), qPrintable(recognitionDialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void appearanceDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QWidget parent;
      const auto collected = collectDialogTexts([&] { manageEditorAppearance(&parent); });
      QVERIFY2(collected.entered, "the appearance dialog did not open");
      const auto offenders = untranslatedTexts(collected);
      QVERIFY2(offenders.isEmpty(), qPrintable(offenders));
      QVERIFY2(collected.buttons.size() + collected.labels.size() >= 3,
          qPrintable(QString::number(collected.buttons.size() + collected.labels.size())));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void drawingDefaultsDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QWidget parent;
      const auto collected = collectDialogTexts([&] { manageDrawingDefaults(&parent); });
      QVERIFY2(collected.entered, "the drawing defaults dialog did not open");
      const auto offenders = untranslatedTexts(collected);
      QVERIFY2(offenders.isEmpty(), qPrintable(offenders));
      QVERIFY2(collected.buttons.size() + collected.labels.size() >= 3,
          qPrintable(QString::number(collected.buttons.size() + collected.labels.size())));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void rectanglePropertiesDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      xerahs::editor::RectangleAnnotation annotation;
      annotation.start = {0, 0}; annotation.end = {120, 80};
      RectanglePropertiesDialog dialog(annotation, nullptr, RectanglePropertiesDialog::Purpose::Edit);
      // The numeric and colour fields hold runtime values; only the prose is translated.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }
};

QTEST_MAIN(LocalizationExpansionTest)
#include "LocalizationExpansionTest.moc"
