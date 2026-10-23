// INTERFACE-CATALOG-001: the screen colour picker window, offscreen.
#include "ColorPickerDialog.h"

#include "tools/ColorFormats.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>

using namespace xerahs::app;
using namespace xerahs::tools;

namespace {
QLabel *lineLabel(const ColorPickerDialog &dialog, int index) {
  return dialog.findChild<QLabel *>(QStringLiteral("colorFormat%1").arg(index));
}
QPushButton *copyButton(const ColorPickerDialog &dialog, const QString &name) {
  for (auto *button : dialog.findChildren<QPushButton *>())
    if (button->accessibleName() == name) return button;
  return nullptr;
}
QStringList recentTexts(const ColorPickerDialog &dialog) {
  auto *list = dialog.findChild<QListWidget *>(QStringLiteral("recentColours"));
  QStringList texts;
  if (!list) return texts;
  for (int index = 0; index < list->count(); ++index) texts << list->item(index)->text();
  return texts;
}
}  // namespace

class ColorPickerDialogTest final : public QObject {
  Q_OBJECT
private slots:
  void showingAColorFillsAllEightLines() {
    ColorPickerDialog dialog;
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    QCOMPARE(dialog.color(), orange);
    const QStringList expected = formatAll(orange);
    QCOMPARE(expected.size(), 8);
    for (int index = 0; index < 8; ++index) {
      auto *line = lineLabel(dialog, index);
      QVERIFY(line);
      QCOMPARE(line->text(), expected.at(index));
      QVERIFY(!line->accessibleName().isEmpty());
    }
    auto *swatch = dialog.findChild<QLabel *>(QStringLiteral("colourSwatch"));
    QVERIFY(swatch);
    QCOMPARE(swatch->accessibleName(), QStringLiteral("Colour swatch"));
    QCOMPARE(swatch->accessibleDescription(), QStringLiteral("#FF8000"));
    auto *hex = dialog.findChild<QLineEdit *>(QStringLiteral("hexColour"));
    QVERIFY(hex);
    QCOMPARE(hex->accessibleName(), QStringLiteral("Hex colour"));
    QCOMPARE(hex->text(), QStringLiteral("#FF8000"));
    auto *recent = dialog.findChild<QListWidget *>(QStringLiteral("recentColours"));
    QVERIFY(recent);
    QCOMPARE(recent->accessibleName(), QStringLiteral("Recent colours"));
    auto *pickAgain = dialog.findChild<QPushButton *>(QStringLiteral("pickAgain"));
    QVERIFY(pickAgain);
    QCOMPARE(pickAgain->accessibleName(), QStringLiteral("Pick again"));
  }

  void copyPutsThatLineOnTheClipboard() {
    ColorPickerDialog dialog;
    dialog.show();
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    const QStringList expected = formatAll(orange);
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    for (int index = 0; index < 8; ++index) {
      const QString name = QStringLiteral("Copy %1").arg(formatName(static_cast<ColorFormat>(index)));
      auto *button = copyButton(dialog, name);
      QVERIFY(button);
      QCOMPARE(button->text(), QStringLiteral("Copy"));
      button->click();
      QCOMPARE(QApplication::clipboard()->text(), expected.at(index));
      QCOMPARE(lineLabel(dialog, index)->text(), expected.at(index));
    }
  }

  void copyAllPutsEveryFormatOnTheClipboard() {
    ColorPickerDialog dialog;
    dialog.show();
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    const QStringList expected = formatAll(orange);
    QCOMPARE(expected.size(), 8);
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("copyAllColorFormats"));
    QVERIFY(button);
    QCOMPARE(button->text(), QStringLiteral("Copy all"));
    QCOMPARE(button->accessibleName(), QStringLiteral("Copy all formats"));
    QVERIFY(button->isEnabled());
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    button->click();
    QCOMPARE(QApplication::clipboard()->text(), expected.join(QStringLiteral("\n")));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("colourPickerStatus"));
    QVERIFY(status);
    QCOMPARE(status->text(), QStringLiteral("All formats copied."));
    QVERIFY(status->isVisible());
  }

  void copyAllIsDisabledUntilAColourIsShown() {
    ColorPickerDialog dialog;
    dialog.show();
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("copyAllColorFormats"));
    QVERIFY(button);
    QVERIFY(!button->isEnabled());
    for (int index = 0; index < 8; ++index) {
      auto *copy = dialog.findChild<QPushButton *>(QStringLiteral("copyFormat%1").arg(index));
      QVERIFY(copy);
      QVERIFY(!copy->isEnabled());
    }
    dialog.showColor(QColor(255, 128, 0));
    QVERIFY(button->isEnabled());
    for (int index = 0; index < 8; ++index) {
      auto *copy = dialog.findChild<QPushButton *>(QStringLiteral("copyFormat%1").arg(index));
      QVERIFY(copy);
      QVERIFY(copy->isEnabled());
    }
  }

  void copyAllAfterAnInvalidHexKeepsTheLastValidColour() {
    ColorPickerDialog dialog;
    dialog.show();
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    auto *hex = dialog.findChild<QLineEdit *>(QStringLiteral("hexColour"));
    QVERIFY(hex);
    hex->setFocus();
    hex->setText(QStringLiteral("nope"));
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(dialog.color(), orange);
    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("copyAllColorFormats"));
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    button->click();
    QCOMPARE(QApplication::clipboard()->text(), formatAll(orange).join(QStringLiteral("\n")));
  }

  void invalidHexKeepsThePreviousColor() {
    ColorPickerDialog dialog;
    dialog.show();
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    const QStringList before = formatAll(orange);
    auto *hex = dialog.findChild<QLineEdit *>(QStringLiteral("hexColour"));
    auto *error = dialog.findChild<QLabel *>(QStringLiteral("hexColourError"));
    QVERIFY(hex);
    QVERIFY(error);
    hex->setFocus();
    hex->setText(QStringLiteral("nope"));
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(dialog.color(), orange);
    QCOMPARE(hex->text(), QStringLiteral("nope"));
    QVERIFY(error->isVisible());
    QVERIFY(!error->text().isEmpty());
    QCOMPARE(error->accessibleName(), QStringLiteral("Hex colour error"));
    for (int index = 0; index < 8; ++index) QCOMPARE(lineLabel(dialog, index)->text(), before.at(index));
    QCOMPARE(recentTexts(dialog), QStringList{QStringLiteral("#FF8000")});

    hex->setText(QStringLiteral("#112233"));
    QTest::keyClick(hex, Qt::Key_Return);
    QCOMPARE(dialog.color(), QColor(0x11, 0x22, 0x33));
    QVERIFY(!error->isVisible());
    QCOMPARE(lineLabel(dialog, 0)->text(), QStringLiteral("HEX: #112233"));
    // Typing a hex value is not a screen pick, so the recent list stays put.
    QCOMPARE(recentTexts(dialog), QStringList{QStringLiteral("#FF8000")});
  }

  void recentListCapsAtTen() {
    ColorPickerDialog dialog;
    for (int index = 0; index < 11; ++index) dialog.showColor(QColor(index, 0, 0));
    const QStringList recent = recentTexts(dialog);
    QCOMPARE(recent.size(), 10);
    QCOMPARE(recent.first(), formatColor(QColor(10, 0, 0), ColorFormat::Hex));
    QCOMPARE(recent.last(), formatColor(QColor(1, 0, 0), ColorFormat::Hex));
    QVERIFY(!recent.contains(formatColor(QColor(0, 0, 0), ColorFormat::Hex)));
    dialog.showColor(QColor(10, 0, 0));
    QCOMPARE(recentTexts(dialog).size(), 10);
    QCOMPARE(recentTexts(dialog).first(), formatColor(QColor(10, 0, 0), ColorFormat::Hex));
  }

  void portalCancelLeavesTheDialogUnchanged() {
    ColorPickerDialog dialog;
    dialog.show();
    const QColor orange(255, 128, 0);
    dialog.showColor(orange);
    const QStringList lines = formatAll(orange);
    const QStringList recent = recentTexts(dialog);
    auto *hex = dialog.findChild<QLineEdit *>(QStringLiteral("hexColour"));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("colourPickerStatus"));
    QVERIFY(status);
    QVERIFY(!status->isVisible());
    dialog.notePortalResult(PortalColorPicker::Outcome::Cancelled, QColor(1, 2, 3));
    QCOMPARE(dialog.color(), orange);
    QCOMPARE(hex->text(), QStringLiteral("#FF8000"));
    for (int index = 0; index < 8; ++index) QCOMPARE(lineLabel(dialog, index)->text(), lines.at(index));
    QCOMPARE(recentTexts(dialog), recent);
    QVERIFY(!status->isVisible());
    QVERIFY(status->text().isEmpty());
    QCOMPARE(QApplication::activeModalWidget(), nullptr);
    QVERIFY(dialog.findChildren<QMessageBox *>().isEmpty());
  }

  void portalFailureIsAVisibleMessage() {
    ColorPickerDialog dialog;
    dialog.show();
    dialog.showColor(QColor(255, 128, 0));
    auto *status = dialog.findChild<QLabel *>(QStringLiteral("colourPickerStatus"));
    dialog.notePortalResult(PortalColorPicker::Outcome::Denied, QColor());
    QVERIFY(status->isVisible());
    QCOMPARE(status->text(), QStringLiteral("The desktop refused colour picking."));
    QCOMPARE(status->accessibleName(), QStringLiteral("Colour picker status"));
    QCOMPARE(dialog.color(), QColor(255, 128, 0));
    QVERIFY(dialog.findChildren<QMessageBox *>().isEmpty());

    dialog.notePortalResult(PortalColorPicker::Outcome::Failed, QColor());
    QVERIFY(status->isVisible());
    QCOMPARE(status->text(), QStringLiteral("The screen colour picker is unavailable."));
    QCOMPARE(dialog.color(), QColor(255, 128, 0));
    QVERIFY(dialog.findChildren<QMessageBox *>().isEmpty());
  }
};

QTEST_MAIN(ColorPickerDialogTest)
#include "ColorPickerDialogTest.moc"
