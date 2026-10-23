// EDITOR-UTILITIES-001 / EU-007: the icon converter window offers the sizes,
// refuses an unusable image, and writes a container a reader accepts.
#include "IconConverterDialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;

namespace {

QImage sourceImage() {
  QImage image(64, 64, QImage::Format_ARGB32);
  image.fill(QColor(0, 0, 255, 255));
  image.setPixelColor(0, 0, QColor(0, 0, 0, 0));
  return image;
}

QPushButton *saveButton(IconConverterDialog &dialog) {
  return dialog.findChild<QPushButton *>(QStringLiteral("saveIcon"));
}

QLabel *previewLabel(IconConverterDialog &dialog) {
  return dialog.findChild<QLabel *>(QStringLiteral("iconPreview"));
}

}  // namespace

class IconConverterDialogTest : public QObject {
  Q_OBJECT

private slots:
  // EU-007: a container is written only after at least one size is selected.
  void saveIsDisabledUntilASizeIsChecked() {
    IconConverterDialog dialog;
    dialog.show();
    auto *save = saveButton(dialog);
    QVERIFY(save);
    const auto boxes = dialog.sizeCheckBoxes();
    QCOMPARE(boxes.size(), 6);
    for (auto *box : boxes) box->setChecked(false);
    QVERIFY(!save->isEnabled());
    boxes.at(2)->setChecked(true);
    QVERIFY(save->isEnabled());
    boxes.at(2)->setChecked(false);
    QVERIFY(!save->isEnabled());
  }

  // EU-007: the six offered sizes, with 16, 32, 48 and 256 selected.
  void defaultSizesMatchTheContract() {
    IconConverterDialog dialog;
    const auto boxes = dialog.sizeCheckBoxes();
    QCOMPARE(boxes.size(), 6);
    const QList<int> sizes{16, 32, 48, 64, 128, 256};
    const QList<bool> defaults{true, true, true, false, false, true};
    for (int i = 0; i < boxes.size(); ++i) {
      QCOMPARE(boxes.at(i)->property("iconEntrySize").toInt(), sizes.at(i));
      QCOMPARE(boxes.at(i)->text(), QString::number(sizes.at(i)));
      QCOMPARE(boxes.at(i)->isChecked(), defaults.at(i));
    }
    const QList<int> selected = dialog.selectedSizes();
    QCOMPARE(selected.size(), 4);
    QCOMPARE(selected.at(0), 16);
    QCOMPARE(selected.at(1), 32);
    QCOMPARE(selected.at(2), 48);
    QCOMPARE(selected.at(3), 256);
  }

  // Both modes are offered, and the true colour mode is the default.
  void bothModesAreOffered() {
    IconConverterDialog dialog;
    auto *trueColor = dialog.findChild<QRadioButton *>(QStringLiteral("iconModeTrueColor32"));
    auto *palette = dialog.findChild<QRadioButton *>(QStringLiteral("iconModePalette8"));
    QVERIFY(trueColor && palette);
    QVERIFY(trueColor->isChecked());
    QVERIFY(!palette->isChecked());
    QCOMPARE(dialog.options().mode, IconOptions::Mode::TrueColor32);
    palette->setChecked(true);
    QCOMPARE(dialog.options().mode, IconOptions::Mode::Palette8);
  }

  // An unusable image is reported instead of silently ignored.
  void invalidImageShowsAnError() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("broken.png");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("this is not an image"));
    file.close();

    IconConverterDialog dialog;
    dialog.setImage(path);
    QVERIFY(!dialog.errorText().isEmpty());
    QVERIFY(dialog.errorText().contains(QStringLiteral("could not be read")));
    QCOMPARE(dialog.imagePath(), path);
  }

  // An image over the side limit is refused before it is decoded.
  void oversizedImageIsRefused() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("wide.png");
    QImage wide(16385, 1, QImage::Format_ARGB32);
    wide.fill(QColor(0, 0, 255, 255));
    QVERIFY(wide.save(path, "PNG"));

    IconConverterDialog dialog;
    dialog.setImage(path);
    QVERIFY(!dialog.errorText().isEmpty());
    QVERIFY(dialog.errorText().contains(QStringLiteral("16384")));
  }

  // The preview shows the 32 pixel entry once an image is loaded.
  void previewShowsThe32PixelEntry() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("source.png");
    QVERIFY(sourceImage().save(path, "PNG"));

    IconConverterDialog dialog;
    auto *preview = previewLabel(dialog);
    QVERIFY(preview);
    QVERIFY(preview->pixmap().isNull());
    dialog.setImage(path);
    QVERIFY(dialog.errorText().isEmpty());
    QVERIFY(!preview->pixmap().isNull());
    QCOMPARE(preview->pixmap().size(), QSize(128, 128));
  }

  // Saving through the injected chooser writes a container a reader accepts.
  void savingWritesAnIconTheReaderAccepts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath("source.png");
    QVERIFY(sourceImage().save(source, "PNG"));
    const QString target = dir.filePath("out.ico");

    IconConverterDialog dialog;
    QString suggested;
    dialog.setFileChooser([&](const QString &path) {
      suggested = path;
      return target;
    });
    dialog.setImage(source);
    QVERIFY(dialog.errorText().isEmpty());
    auto *save = saveButton(dialog);
    QVERIFY(save);
    QVERIFY(save->isEnabled());
    save->click();

    QVERIFY(suggested.endsWith(QStringLiteral(".ico")));
    QVERIFY(QFileInfo::exists(target));
    QVERIFY(QFileInfo(target).size() > 0);
    QImageReader reader(target, "ico");
    QCOMPARE(reader.imageCount(), 4);
    QVERIFY(!reader.read().isNull());
    QVERIFY(dialog.errorText().isEmpty());
  }

  // A cancelled chooser writes nothing.
  void aCancelledSaveWritesNothing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath("source.png");
    QVERIFY(sourceImage().save(source, "PNG"));

    IconConverterDialog dialog;
    dialog.setFileChooser([](const QString &) { return QString(); });
    dialog.setImage(source);
    saveButton(dialog)->click();
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    QVERIFY(dialog.errorText().isEmpty());
  }
};

QTEST_MAIN(IconConverterDialogTest)
#include "IconConverterDialogTest.moc"
