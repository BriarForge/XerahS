// IFCAT-005: modal dialogs trap focus only while active, identify their purpose,
// expose a safe default and a cancel action, restore focus on close, and never
// hide a destructive consequence behind generic confirmation text.
#include "ColorPickerDialog.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "NewImageDialog.h"
#include "UrlOpenDialog.h"
#include "QrCodeDialog.h"
#include "UploadDestinationsDialog.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTest>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

using namespace xerahs::app;

namespace {
bool safeLabel(const QString &text) {
  static const QRegularExpression destructive(R"(delete|remove|clear|discard|forget|reset|overwrite|replace|erase)", QRegularExpression::CaseInsensitiveOption);
  return !destructive.match(text).hasMatch();
}
}

class ModalDialogsTest final : public QObject {
  Q_OBJECT
  // A host window whose line edit owns focus before a modal opens.
  struct Host {
    QWidget window; QLineEdit *edit = new QLineEdit(&window);
    Host() { auto *layout = new QVBoxLayout(&window); layout->addWidget(edit); window.show(); window.activateWindow(); edit->setFocus(); }
  };
private slots:
  void everyChoiceConfirmationDefaultsToASafeButtonAndHasAPurposefulTitle() {
    QDirIterator files(XERAHS_APP_DIR, {"*.cpp"}, QDir::Files, QDirIterator::Subdirectories);
    const QRegularExpression start(R"((new QMessageBox\(|QMessageBox\s+\w+\(|QMessageBox::question\())");
    const QRegularExpression choice(R"(QMessageBox::(Cancel|No|Discard|Yes|Reset|Close|Save)\b)");
    const QRegularExpression safeDefault(R"(setDefaultButton\(QMessageBox::(Cancel|No|Save)\))");
    const QRegularExpression staticDefault(R"(QMessageBox::(No|Cancel)\))");
    const QRegularExpression title("(?:QMessageBox::(?:Question|Warning|Information|Critical),\\s*|question\\([^,]+,\\s*)(?:QStringLiteral\\()?\"([^\"]*)\"");
    const QStringList generic{"Confirm", "Confirmation", "Are you sure?", "Warning", "Question", "Message", "Error", "Information"};
    int checked = 0;
    while (files.hasNext()) {
      const auto path = files.next(); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto text = QString::fromUtf8(file.readAll());
      for (auto it = start.globalMatch(text); it.hasNext();) {
        const auto match = it.next(); const int begin = match.capturedStart(), end = text.indexOf(");", begin);
        const auto head = text.mid(begin, end - begin + 2); const int line = text.left(begin).count('\n') + 1;
        if (!choice.match(head).hasMatch()) continue;  // Single-button information box.
        ++checked; const auto where = QString("%1:%2").arg(path).arg(line);
        if (head.startsWith("QMessageBox::question")) QVERIFY2(staticDefault.match(head).hasMatch(), qPrintable(where + " relies on the implicit (Yes) default"));
        else QVERIFY2(safeDefault.match(text.mid(begin, 2500)).hasMatch(), qPrintable(where + " has no safe explicit default"));
        const auto named = title.match(head);
        if (named.hasMatch()) { QVERIFY2(!generic.contains(named.captured(1)) && named.captured(1).size() >= 8, qPrintable(where + " generic title " + named.captured(1))); }
      }
    }
    QVERIFY2(checked >= 20, qPrintable(QString::number(checked)));
  }
  void removingADestinationSaysWhatIsDeletedDefaultsToNoAndRestoresFocus() {
    Host host; QTRY_VERIFY(host.edit->hasFocus());
    auto ports = nativeUploadDestinationsPorts(); QVERIFY(ports.confirmRemove);
    QString text, defaultText; bool trapped = false, probed = false;
    QTimer probe; probe.setInterval(5);
    connect(&probe, &QTimer::timeout, &probe, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || probed) return;
      probed = true; text = box->text(); defaultText = box->defaultButton() ? box->defaultButton()->text() : QString();
      trapped = QApplication::activeModalWidget() == box;
      QTest::keyClick(box, Qt::Key_Escape);
    });
    probe.start();
    const bool removed = ports.confirmRemove(&host.window, "Example uploader"); probe.stop();
    QVERIFY(probed); QVERIFY(!removed);
    QVERIFY(text.contains("Example uploader")); QVERIFY(text.contains("delete its secrets from the keyring"));  // Consequence stated, not "Are you sure?".
    QVERIFY2(defaultText.contains("No"), qPrintable(defaultText)); QVERIFY(trapped);
    QVERIFY(!QApplication::activeModalWidget()); QTRY_VERIFY(host.window.isActiveWindow()); QTRY_VERIFY(host.edit->hasFocus());
  }
  void modalDialogsTrapFocusOnlyWhileActiveIdentifyPurposeEscapeAndRestoreFocus_data() {
    QTest::addColumn<int>("which");
    QTest::newRow("qr-code") << 0; QTest::newRow("color-picker") << 1; QTest::newRow("hash-checker") << 2; QTest::newRow("icon-converter") << 3; QTest::newRow("new-image") << 4; QTest::newRow("url-open") << 5;
  }
  void modalDialogsTrapFocusOnlyWhileActiveIdentifyPurposeEscapeAndRestoreFocus() {
    QFETCH(int, which); Host host; QTRY_VERIFY(host.edit->hasFocus());
    QDialog *dialog = which == 0 ? static_cast<QDialog *>(new QrCodeDialog(&host.window)) : which == 1 ? static_cast<QDialog *>(new ColorPickerDialog(&host.window))
        : which == 2 ? static_cast<QDialog *>(new HashCheckerDialog(&host.window)) : which == 3 ? static_cast<QDialog *>(new IconConverterDialog(&host.window))
        : which == 4 ? static_cast<QDialog *>(new NewImageDialog(&host.window)) : static_cast<QDialog *>(new UrlOpenDialog(&host.window));
    QVERIFY(!QApplication::activeModalWidget());  // No trap before it opens.
    dialog->setModal(true); dialog->open();
    QTRY_VERIFY(dialog->isVisible()); QCOMPARE(QApplication::activeModalWidget(), dialog);
    QVERIFY(!dialog->windowTitle().trimmed().isEmpty());  // Identifies its purpose.
    // A cancel/close action is exposed, and no default button carries a destructive label.
    bool cancelAction = false;
    for (auto *button : dialog->findChildren<QPushButton *>()) {
      if (!button->isVisibleTo(dialog)) continue;
      const auto label = button->text().remove('&');
      cancelAction |= label.contains("Close", Qt::CaseInsensitive) || label.contains("Cancel", Qt::CaseInsensitive);
      if (button->isDefault()) QVERIFY2(safeLabel(label), qPrintable(label));
    }
    for (auto *box : dialog->findChildren<QDialogButtonBox *>()) cancelAction |= box->button(QDialogButtonBox::Cancel) || box->button(QDialogButtonBox::Close);
    QVERIFY(cancelAction);
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->isVisible()); QCOMPARE(dialog->result(), int(QDialog::Rejected));
    QVERIFY(!QApplication::activeModalWidget());  // Trap released on close.
    QTRY_VERIFY(host.window.isActiveWindow()); QTRY_VERIFY(host.edit->hasFocus());  // Focus restored.
    delete dialog;
  }
};
QTEST_MAIN(ModalDialogsTest)
#include "ModalDialogsTest.moc"
