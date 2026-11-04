// CORE-009: a pseudo-locale that lengthens every translated string by 40% neither clips controls nor
// overflows the screen, and a string missing from the translation falls back to its source text.
#include "AppearanceDialog.h"
#include "ActionSelectionDialog.h"
#include "AutoCaptureDialog.h"
#include "CapturePermissionHelp.h"
#include "CaptureSaveAsDialog.h"
#include "CanvasCropDialog.h"
#include "AnnotationColorDialog.h"
#include "AnnotationObjectNavigator.h"
#include "ApplicationWindow.h"
#include "CanvasRotationDialog.h"
#include "CaptureAnnotationDialog.h"
#include "CaptureController.h"
#include "CaptureHistoryWindow.h"
#include "ColorPickerDialog.h"
#include "ConveniencePreferencesDialog.h"
#include "DrawingDefaultsDialog.h"
#include "EditorCanvas.h"
#include "EditorSaveDialog.h"
#include "EditorSource.h"
#include "FileUploadController.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "ImageComparisonDialog.h"
#include "ImageInsertDialog.h"
#include "ImageRecognitionDialog.h"
#include "NativePipelineJournal.h"
#include "NewImageDialog.h"
#include "OnboardingProgress.h"
#include "OnboardingWizard.h"
#include "PipelineHistoryDialog.h"
#include "PipelineResultDialog.h"
#include "QrCodeDialog.h"
#include "PinWindow.h"
#include "RectanglePropertiesDialog.h"
#include "RegionOverlay.h"
#include "SettingsDialog.h"
#include "SelectionRotationDialog.h"
#include "TextContentDialog.h"
#include "UploadDestinationsDialog.h"
#include "UrlOpenDialog.h"
#include "VideoConverterDialog.h"
#include "WorkflowExecutionHistoryDialog.h"
#include "WatchFolderService.h"
#include "WatchFoldersDialog.h"
#include "image-editor/AnnotationDocument.h"
#include "image-editor/EditorSession.h"
#include "media/VideoConversion.h"
#include "settings/ApplicationSettings.h"
#include "uploads/CredentialStore.h"
#include "watch/WatchFolders.h"
#include "workflows/WorkflowShortcuts.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorSpace>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPixmap>
#include <QProgressBar>
#include <QScopedPointer>
#include <QScreen>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QWidget>

#include <cstring>
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
// does not. A skipped object name holds runtime data (a size in pixels) or a label that another file owns,
// not prose in this window. A foreign action names a toolbar action whose text lives in another file, so
// its button is skipped as well.
int translatedTexts(QWidget &dialog, const QStringList &skippedNames, const QStringList &foreignActions = {}) {
  const auto skipped = [&skippedNames](const QString &name) {
    for (const auto &prefix : skippedNames)
      if (name.startsWith(prefix)) return true;
    return false;
  };
  const auto foreign = [&foreignActions](const QString &name) {
    for (const auto &id : foreignActions)
      if (name == id || name.endsWith(id)) return true;
    return false;
  };
  int checked = 0;
  for (auto *button : dialog.findChildren<QAbstractButton *>()) {
    if (button->text().isEmpty() || skipped(button->objectName())) continue;
    const auto *tool = qobject_cast<const QToolButton *>(button);
    const auto *action = tool ? tool->defaultAction() : nullptr;
    if (action && foreign(action->objectName())) continue;
    if (!button->text().contains(QChar(0x2022))) {
      QTest::qFail(qPrintable(QStringLiteral("untranslated button text: %1").arg(button->text())), __FILE__, __LINE__);
      return checked;
    }
    ++checked;
  }
  for (auto *label : dialog.findChildren<QLabel *>()) {
    // A label that holds only whitespace is a layout spacer, not prose.
    if (label->text().trimmed().isEmpty() || skipped(label->objectName())) continue;
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
      // A label that holds only whitespace is a layout spacer, not prose.
      if (label->text().trimmed().isEmpty()) continue;
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

  void textContentDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      xerahs::editor::RectangleAnnotation text;
      text.content.type = xerahs::editor::shape::text;
      TextContentDialog textDialog(text, nullptr);
      xerahs::editor::RectangleAnnotation balloon;
      balloon.content.type = xerahs::editor::shape::speechBalloon;
      TextContentDialog balloonDialog(balloon, nullptr);
      // The font, size and colour fields hold runtime values; only the prose is translated.
      const int textChecked = translatedTexts(textDialog, {});
      const int balloonChecked = translatedTexts(balloonDialog, {});
      QVERIFY2(textChecked >= 3, qPrintable(QString::number(textChecked)));
      QVERIFY2(balloonChecked >= 3, qPrintable(QString::number(balloonChecked)));
      QVERIFY2(textDialog.windowTitle().contains(QChar(0x2022)), qPrintable(textDialog.windowTitle()));
      QVERIFY2(balloonDialog.windowTitle().contains(QChar(0x2022)), qPrintable(balloonDialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void conveniencePreferencesDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QWidget parent;
      const auto collected = collectDialogTexts([&] { manageConveniencePreferences(&parent); });
      QVERIFY2(collected.entered, "the convenience preferences dialog did not open");
      const auto offenders = untranslatedTexts(collected);
      QVERIFY2(offenders.isEmpty(), qPrintable(offenders));
      QVERIFY2(collected.buttons.size() + collected.labels.size() >= 3,
          qPrintable(QString::number(collected.buttons.size() + collected.labels.size())));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void annotationColorDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QWidget parent;
      // The scope sentence belongs to the caller and is already translated there, so it stays empty here.
      const auto collected = collectDialogTexts([&] {
        chooseAnnotationColor(xerahs::editor::EditorSession::ColorTarget::Stroke, 0xFF3E83F2, QString(), &parent);
      });
      QVERIFY2(collected.entered, "the annotation colour dialog did not open");
      const auto offenders = untranslatedTexts(collected);
      QVERIFY2(offenders.isEmpty(), qPrintable(offenders));
      QVERIFY2(collected.buttons.size() + collected.labels.size() >= 3,
          qPrintable(QString::number(collected.buttons.size() + collected.labels.size())));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void annotationObjectNavigatorTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      xerahs::editor::AnnotationDocument document;
      document.canvasWidth = 800; document.canvasHeight = 600;
      xerahs::editor::RectangleAnnotation rectangle;
      rectangle.id = QUuid::createUuid(); rectangle.start = {0, 100}; rectangle.end = {50, 150};
      document.annotations.push_back(rectangle);
      QImage source(800, 600, QImage::Format_ARGB32); source.fill(Qt::white);
      xerahs::editor::EditorSession session(document);
      EditorCanvas canvas(session, source);
      AnnotationObjectNavigator navigator(canvas);
      // The navigator paints its rows through the model, so its visible text is the accessible
      // metadata and the model rows. The object UUID and the bounds are runtime values.
      QStringList texts{navigator.accessibleName(), navigator.accessibleDescription()};
      const auto *model = navigator.model();
      for (int row = 0; row < model->rowCount(); ++row) {
        texts << model->data(model->index(row, 0), Qt::DisplayRole).toString();
        texts << model->data(model->index(row, 0), Qt::ToolTipRole).toString();
      }
      int checked = 0;
      for (const auto &value : texts) {
        if (value.isEmpty()) continue;
        QVERIFY2(value.contains(QChar(0x2022)), qPrintable(value));
        ++checked;
      }
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void onboardingWizardFirstPageTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      OnboardingWizard wizard(OnboardingPorts{}, OnboardingState{});
      wizard.show();
      // The wizard's pages hold runtime data in their tables and preview lines; the prose is translated.
      const int checked = translatedTexts(wizard, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(wizard.windowTitle().contains(QChar(0x2022)), qPrintable(wizard.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void onboardingProgressTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      // The progress store explains a diagnostic in prose and keeps the code itself literal.
      const auto message = onboardingDiagnosticMessage(QStringLiteral("onboarding-store-newer"));
      QVERIFY2(message.contains(QChar(0x2022)), qPrintable(message));
      QVERIFY2(message.startsWith("The setup guide progress file"), qPrintable(message));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void captureHistoryWindowTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
      CaptureHistoryWindow window(journal, [] { return false; }, [](const QString &, QWidget *) {});
      // The file names, sizes and dates in the list hold runtime values; the buttons and the status are prose.
      const int checked = translatedTexts(window, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(window.windowTitle().contains(QChar(0x2022)), qPrintable(window.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void workflowExecutionHistoryDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      WorkflowHistoryPorts ports;
      ports.list = [] { return WorkflowExecutionListing{}; };
      WorkflowExecutionHistoryDialog dialog(ports, [] { return false; });
      // The execution rows and their diagnostics hold runtime values; the notice and buttons are prose.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void pipelineHistoryDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      auto journal = std::make_shared<NativePipelineJournal>(dir.filePath("history"));
      PipelineHistoryDialog dialog(journal, [] { return false; });
      // The record rows and their diagnostics hold runtime values; the notice and buttons are prose.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void pipelineResultDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QScopedPointer<QDialog> dialog(makePipelineResultDialog(QJsonObject{}, nullptr));
      QVERIFY(dialog);
      // The action rows and the report JSON hold runtime values; the summary and privacy text are prose.
      const int checked = translatedTexts(*dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog->windowTitle().contains(QChar(0x2022)), qPrintable(dialog->windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void captureSaveAsDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      CaptureSaveAsDialog dialog(dir.path());
      // The chosen path is runtime data; the guidance text and the picker captions are prose.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void editorSaveDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      EditorSaveDialog dialog(dir.filePath("capture.png"));
      // The chosen path and the quality value are runtime data; the option labels are prose.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void capturePermissionHelpTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      // A stand-in settings program, so the dialog offers its Open Settings action.
      QFile settings(dir.filePath("gnome-control-center"));
      QVERIFY(settings.open(QIODevice::WriteOnly));
      settings.close();
      QVERIFY(settings.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
      CapturePermissionHelpPorts ports;
      ports.searchPath = [&] { return QStringList{dir.path()}; };
      ports.desktop = [] { return QStringLiteral("GNOME"); };
      ports.launch = [](const QString &, const QStringList &) { return true; };
      QScopedPointer<QDialog> dialog(makeCapturePermissionHelp(nullptr, ports));
      QVERIFY(dialog);
      // The settings program name is a product name; the guidance text and the button are prose.
      const int checked = translatedTexts(*dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog->windowTitle().contains(QChar(0x2022)), qPrintable(dialog->windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void actionSelectionDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      ActionSelectionDialog dialog(QStringList{}, QSize(1920, 1080));
      // The captured image size is runtime data; the action names and the scope text are prose.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void autoCaptureDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      AutoCaptureDialog dialog([] { return qint64(1700000000000); }, false);
      // The interval, count and end time hold runtime values; only the prose is translated.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void canvasCropDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QImage source(64, 48, QImage::Format_ARGB32); source.fill(Qt::white);
      xerahs::editor::CropRegion region{0, 0, 64, 48};
      CanvasCropDialog dialog(xerahs::editor::AnnotationDocument{}, source, region);
      // The crop bounds and the preview status hold runtime values; only the prose is translated.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void canvasRotationDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QImage source(64, 48, QImage::Format_ARGB32); source.fill(Qt::white);
      CanvasRotationDialog dialog(xerahs::editor::AnnotationDocument{}, source);
      // The angle, the preview status and the chosen fill colour hold runtime values.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void selectionRotationDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      // One selected rectangle, so the dialog shows its own rotation summary rather than the
      // unsupported-selection message that the shared annotation helper produces.
      xerahs::editor::RectangleAnnotation rectangle;
      rectangle.id = QUuid::createUuid();
      rectangle.start = {10, 10}; rectangle.end = {60, 40};
      xerahs::editor::AnnotationDocument document;
      document.annotations = {rectangle};
      SelectionRotationDialog dialog(document, QSet<QUuid>{rectangle.id});
      // The preview lists object UUIDs and angles: runtime values. Only the prose is translated.
      const int checked = translatedTexts(dialog, {});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void pinWindowTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      PinWindow window(QImage(32, 32, QImage::Format_ARGB32));
      // The pin window paints the image and holds no button or label, so its visible text is the
      // window title, the screen-reader name and the context-menu actions.
      QContextMenuEvent event(QContextMenuEvent::Mouse, window.rect().center(), window.rect().center());
      QApplication::sendEvent(&window, &event);
      auto *menu = window.findChild<QMenu *>();
      QVERIFY(menu);
      QStringList texts{window.windowTitle(), window.accessibleName()};
      for (const auto *action : menu->actions())
        if (!action->text().isEmpty()) texts << action->text();
      menu->close();
      int checked = 0;
      for (const auto &value : texts) {
        if (value.isEmpty()) continue;
        QVERIFY2(value.contains(QChar(0x2022)), qPrintable(value));
        ++checked;
      }
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void regionOverlayTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QTemporaryDir dir;
      QVERIFY(dir.isValid());
      CaptureController controller(dir.path(), nullptr);
      xerahs::capture::Display display;
      display.id = QStringLiteral("localization-overlay");
      display.physical = {0, 0, 800, 600};
      display.logical = {0, 0, 800, 600};
      QImage snapshot(800, 600, QImage::Format_ARGB32); snapshot.fill(Qt::white);
      RegionOverlay overlay(controller, QGuiApplication::primaryScreen(), display, snapshot);
      overlay.show();
      QVERIFY(QTest::qWaitForWindowExposed(&overlay));
      // The instruction bar is only painted on the screen the controller points at.
      QVERIFY(overlay.screen() == QGuiApplication::primaryScreen());
      const QString title = overlay.windowTitle();
      const QString name = overlay.accessibleName();
      // The overlay paints its text, so the locale has to change the painted pixels.
      QPixmap translated(overlay.size());
      overlay.render(&translated);
      QApplication::removeTranslator(&pseudo);
      QPixmap plain(overlay.size());
      overlay.render(&plain);
      QVERIFY2(title.contains(QChar(0x2022)), qPrintable(title));
      QVERIFY2(name.contains(QChar(0x2022)), qPrintable(name));
      // The painted change has to come from the instruction bar at the bottom of the overlay.
      const auto plainImage = plain.toImage();
      const auto translatedImage = translated.toImage();
      QCOMPARE(plainImage.size(), translatedImage.size());
      int changedRows = 0, top = plainImage.height(), bottom = -1;
      for (int y = 0; y < plainImage.height(); ++y) {
        if (std::memcmp(plainImage.constScanLine(y), translatedImage.constScanLine(y), plainImage.bytesPerLine()) == 0) continue;
        ++changedRows;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
      }
      QVERIFY2(changedRows > 0, "the overlay's painted instruction text did not change with the locale");
      QVERIFY2(top > plainImage.height() / 2, "the painted change did not come from the instruction bar");
    }
    QApplication::removeTranslator(&pseudo);
  }

  void captureAnnotationDialogTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      QImage image(80, 60, QImage::Format_ARGB32); image.fill(Qt::blue); image.setColorSpace(QColorSpace::SRgb);
      const auto loaded = prepareCapturedSource(image);
      QVERIFY(loaded.document);
      CaptureAnnotationDialog dialog(loaded);
      dialog.show();
      // The annotation tool, delete and order actions are built in app/NativeAnnotationTools.cpp, and the
      // instruction label beside them comes from the same file. This slice may not touch it, so its
      // buttons and that one label stay untranslated.
      const int checked = translatedTexts(dialog, {QStringLiteral("annotationToolInstructions")},
          {QStringLiteral("Tool"), QStringLiteral("deleteSelection"), QStringLiteral("bringForward"),
              QStringLiteral("sendBackward"), QStringLiteral("bringToFront"), QStringLiteral("sendToBack")});
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
      QVERIFY2(dialog.windowTitle().contains(QChar(0x2022)), qPrintable(dialog.windowTitle()));
    }
    QApplication::removeTranslator(&pseudo);
  }

  void applicationWindowTextIsTranslatable() {
    PseudoLocale pseudo;
    QVERIFY(QApplication::installTranslator(&pseudo));
    {
      // An empty command map: the grid buttons carry the caller's own labels, so only the
      // window's own prose is checked here.
      ApplicationWindow window({});
      // The window title and the accessible name are the product name, so they stay literal.
      QCOMPARE(window.windowTitle(), QStringLiteral("XerahS"));
      QCOMPARE(window.accessibleName(), QStringLiteral("XerahS"));
      QStringList texts{window.accessibleDescription()};
      for (const auto *menu : window.menuBar()->findChildren<QMenu *>())
        if (!menu->title().isEmpty()) texts << menu->title();
      texts << window.findChild<QLabel *>("operationStatus")->accessibleName();
      texts << window.findChild<QProgressBar *>("operationProgress")->accessibleName();
      int checked = translatedTexts(window, {});
      for (const auto &value : texts) {
        if (value.isEmpty()) continue;
        QVERIFY2(value.contains(QChar(0x2022)), qPrintable(value));
        ++checked;
      }
      QVERIFY2(checked >= 3, qPrintable(QString::number(checked)));
    }
    QApplication::removeTranslator(&pseudo);
  }
};

QTEST_MAIN(LocalizationExpansionTest)
#include "LocalizationExpansionTest.moc"
