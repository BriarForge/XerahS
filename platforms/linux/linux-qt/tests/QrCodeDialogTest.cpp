// EDITOR-UTILITIES-001 / EU-003: the QR dialog validates input, previews
// deterministically, and copies, saves and uploads only on explicit action.
#include "QrCodeDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;
using xerahs::tools::QrEcc;

class QrCodeDialogTest : public QObject {
  Q_OBJECT
  static QPushButton *button(QrCodeDialog &d, const char *n) { return d.findChild<QPushButton *>(QLatin1String(n)); }

private slots:
  void emptyTextIsTypedAndDisablesActions() {
    QrCodeDialog d;
    d.setUploadPort([](const QString &) {});
    QCOMPARE(d.errorCode(), QStringLiteral("qr-empty-content"));
    QVERIFY(d.statusText().contains(QStringLiteral("qr-empty-content")));
    QVERIFY(d.image().isNull());
    for (auto n : {"qr-copy", "qr-save", "qr-upload"}) QVERIFY(!button(d, n)->isEnabled());
    d.setText(QStringLiteral("hello"));
    QVERIFY(d.errorCode().isEmpty());
    for (auto n : {"qr-copy", "qr-save", "qr-upload"}) QVERIFY(button(d, n)->isEnabled());
    d.setText(QString());
    QCOMPARE(d.errorCode(), QStringLiteral("qr-empty-content"));
  }
  void defaultsToLevelM() {
    QrCodeDialog d;
    QCOMPARE(d.findChild<QComboBox *>(QStringLiteral("qr-ecc"))->currentIndex(), 1);
  }
  void previewIsDeterministic() {
    QrCodeDialog a, b;
    a.setText(QStringLiteral("https://example.com")); b.setText(QStringLiteral("https://example.com"));
    QVERIFY(!a.image().isNull());
    QCOMPARE(a.image(), b.image());
    const auto before = a.image();
    a.setText(QStringLiteral("other")); a.setText(QStringLiteral("https://example.com"));
    QCOMPARE(a.image(), before);
    a.setEcc(QrEcc::H);
    QVERIFY(a.image() != before);
    QVERIFY(a.findChild<QLabel *>(QStringLiteral("qr-preview"))->pixmap().width() > 0);
  }
  void tooLongIsTyped() {
    QrCodeDialog d;
    d.setText(QString(3000, QLatin1Char('a')));
    QCOMPARE(d.errorCode(), QStringLiteral("qr-content-too-long"));
    QVERIFY(!button(d, "qr-save")->isEnabled());
  }
  void invalidSizeIsTyped() {
    QrCodeDialog d;
    d.setText(QStringLiteral("hello"));  // 21 modules + 8
    d.setImageSize(10);
    QCOMPARE(d.errorCode(), QStringLiteral("qr-invalid-size"));
    QVERIFY(!button(d, "qr-copy")->isEnabled());
    d.setImageSize(5000);
    QCOMPARE(d.errorCode(), QStringLiteral("qr-invalid-size"));
    d.setImageSize(300);
    QVERIFY(d.errorCode().isEmpty());
    QVERIFY(!d.image().isNull());
  }
  void saveWritesPngAtomically() {
    QrCodeDialog d;
    d.setText(QStringLiteral("save me"));
    QTemporaryDir dir;
    const auto path = dir.filePath(QStringLiteral("out.png"));
    QVERIFY(d.saveTo(path));
    QImage loaded(path);
    QVERIFY(!loaded.isNull());
    QCOMPARE(loaded.size(), d.image().size());
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);  // no leftover temp file
    // A failing save leaves the existing file untouched and reports a typed error.
    QVERIFY(!d.saveTo(dir.filePath(QStringLiteral("missing/out.png"))));
    QCOMPARE(d.errorCode(), QStringLiteral("qr-save-failed"));
    QVERIFY(QFileInfo::exists(path));
  }
  void copyPutsImageOnClipboard() {
    QrCodeDialog d;
    d.setText(QStringLiteral("clip"));
    QApplication::clipboard()->clear();
    button(d, "qr-copy")->click();
    QVERIFY(d.errorCode().isEmpty());
    QCOMPARE(QApplication::clipboard()->image().size(), d.image().size());
  }
  void uploadPortReceivesExistingPng() {
    QrCodeDialog d;
    d.setText(QStringLiteral("upload me"));
    QVERIFY(!button(d, "qr-upload")->isEnabled());  // no port yet
    QStringList got;
    bool existed = false;
    QImage loaded;
    d.setUploadPort([&](const QString &p) { got << p; existed = QFileInfo::exists(p); loaded = QImage(p); });
    QVERIFY(button(d, "qr-upload")->isEnabled());
    button(d, "qr-upload")->click();
    QCOMPARE(got.size(), 1);
    QVERIFY(existed);
    QVERIFY(got.first().endsWith(QStringLiteral(".png")));
    QCOMPARE(loaded.size(), d.image().size());
  }
  void uploadFilesAreRemovedWithDialog() {
    QString path;
    {
      QrCodeDialog d;
      d.setText(QStringLiteral("x"));
      d.setUploadPort([&](const QString &p) { path = p; });
      d.upload();
      QVERIFY(QFileInfo::exists(path));
    }
    QVERIFY(!QFileInfo::exists(path));
  }
  void controlsAreNamedForAccessibility() {
    QrCodeDialog d;
    for (auto n : {"qr-copy", "qr-save", "qr-upload"}) QVERIFY(!button(d, n)->text().isEmpty());
    QVERIFY(!d.findChild<QPlainTextEdit *>(QStringLiteral("qr-text"))->accessibleName().isEmpty());
    QVERIFY(!d.findChild<QSpinBox *>(QStringLiteral("qr-size"))->accessibleName().isEmpty());
  }
};

QTEST_MAIN(QrCodeDialogTest)
#include "QrCodeDialogTest.moc"
