// EDITOR-UTILITIES-001 / EU-005, EU-006: the hash checker window computes on a
// worker, shows the digest, compares a pasted value, and copies to the clipboard.
#include "HashCheckerDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace xerahs::app;
using namespace xerahs::tools;

namespace {

const auto kAbcSha256 = QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
const auto kAbcMd5 = QStringLiteral("900150983cd24fb0d6963f7d28e17f72");
const auto kAbcSha1 = QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d");
const auto kAbcSha512 =
    QStringLiteral("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

QString writeFile(const QString &path, const QByteArray &bytes) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
  return file.write(bytes) == bytes.size() ? path : QString();
}

QLineEdit *field(HashCheckerDialog &dialog, const char *name) {
  return dialog.findChild<QLineEdit *>(QLatin1String(name));
}

QPushButton *button(HashCheckerDialog &dialog, const char *name) {
  return dialog.findChild<QPushButton *>(QLatin1String(name));
}

// A named pipe whose writer is held open without sending anything, so a read
// of it is provably still in flight. Hashing a large regular file instead would
// race the worker, which is too fast to observe reliably.
class HeldOpenPipe {
public:
  explicit HeldOpenPipe(const QString &path) {
    m_thread = std::thread([this, bytes = path.toLocal8Bit()] {
      const int descriptor = ::open(bytes.constData(), O_WRONLY);  // Waits for a reader.
      if (descriptor < 0) return;
      while (!m_release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const char payload[] = "abc";
      if (::write(descriptor, payload, sizeof(payload) - 1) < 0) { /* The read ended first. */ }
      ::close(descriptor);
    });
  }
  ~HeldOpenPipe() {
    release();
    if (m_thread.joinable()) m_thread.join();
  }
  void release() { m_release.store(true); }

private:
  std::atomic_bool m_release{false};
  std::thread m_thread;
};

QString makePipe(const QTemporaryDir &dir) {
  const auto path = dir.filePath("pipe");
  return ::mkfifo(path.toLocal8Bit().constData(), 0600) == 0 ? path : QString();
}

}  // namespace

class HashCheckerDialogTest : public QObject {
  Q_OBJECT

private slots:
  void init() { QApplication::clipboard()->setText(QStringLiteral("clipboard before hashing")); }

  // A digest has to arrive through the dialog's own worker, not by assignment.
  void computeShowsTheDigest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    HashCheckerDialog dialog;
    dialog.show();
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    QSignalSpy busy(&dialog, &HashCheckerDialog::busyChanged);
    dialog.setFile(path);
    // Wait for the finished signal, so the digest and status are both published.
    QTRY_COMPARE(hashed.count(), 1);
    QCOMPARE(hashed.first().first().toString(), kAbcSha256);
    QCOMPARE(dialog.file(), path);
    QCOMPARE(dialog.digest(), kAbcSha256);
    QVERIFY(!dialog.busy());
    // The read ran off the GUI thread, so it announced itself both ways.
    QCOMPARE(busy.count(), 2);
    QCOMPARE(busy.first().first().toBool(), true);
    QCOMPARE(busy.last().first().toBool(), false);
    QCOMPARE(dialog.findChild<QProgressBar *>()->value(), 100);
    QVERIFY(button(dialog, "copyHash")->isEnabled());
    QVERIFY(dialog.statusText().contains(QStringLiteral("SHA-256")));
    QVERIFY(dialog.statusText().contains(QStringLiteral("abc")));
  }

  // EU-006: the algorithm shown must identify the digest, so every algorithm
  // has to produce its own result rather than the default.
  void algorithmChoiceSelectsTheAlgorithm_data() {
    QTest::addColumn<int>("index");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");
    QTest::newRow("md5") << 1 << QStringLiteral("MD5") << kAbcMd5;
    QTest::newRow("sha1") << 2 << QStringLiteral("SHA-1") << kAbcSha1;
    QTest::newRow("sha256") << 3 << QStringLiteral("SHA-256") << kAbcSha256;
    QTest::newRow("sha512") << 5 << QStringLiteral("SHA-512") << kAbcSha512;
  }
  void weakAlgorithmsAreLabelledUnsuitableForSecurity() {
    // EU-006: CRC32, MD5 and SHA-1 carry the warning; SHA-256 and stronger do not.
    HashCheckerDialog dialog; dialog.show();
    auto *algorithm = dialog.findChild<QComboBox *>(QStringLiteral("hashAlgorithm"));
    auto *warning = dialog.findChild<QLabel *>(QStringLiteral("hashWeakWarning"));
    QVERIFY(algorithm && warning);
    QCOMPARE(algorithm->count(), 6);
    QVERIFY(!warning->isVisible());
    for (const int weak : {0, 1, 2}) { algorithm->setCurrentIndex(weak); QVERIFY(warning->isVisible()); }
    for (const int strong : {3, 4, 5}) { algorithm->setCurrentIndex(strong); QVERIFY(!warning->isVisible()); }
    QVERIFY(warning->text().contains(QStringLiteral("not suitable for security verification")));
  }
  void algorithmChoiceSelectsTheAlgorithm() {
    QFETCH(int, index);
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    HashCheckerDialog dialog;
    dialog.show();
    auto *algorithm = dialog.findChild<QComboBox *>();
    QVERIFY(algorithm);
    QCOMPARE(algorithm->count(), 6);
    QCOMPARE(algorithm->itemText(index), name);
    QCOMPARE(algorithm->currentText(), QStringLiteral("SHA-256"));  // EU-005 default.
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    algorithm->setCurrentIndex(index);
    field(dialog, "hashFile")->setText(path);
    button(dialog, "computeHash")->click();
    QTRY_COMPARE(hashed.count(), 1);
    QCOMPARE(dialog.digest(), expected);
    QCOMPARE(algorithm->currentText(), name);
    QVERIFY(dialog.statusText().contains(name));
  }

  void compareAcceptsCaseAndSurroundingSpaces_data() {
    QTest::addColumn<QString>("entered");
    QTest::addColumn<QString>("verdict");
    QTest::newRow("exact") << kAbcSha256 << QStringLiteral("Match");
    QTest::newRow("uppercase") << kAbcSha256.toUpper() << QStringLiteral("Match");
    QTest::newRow("mixed case") << QStringLiteral("bA7816bf8F01cfea414140de5DAE2223b00361a396177a9cb410ff61f20015AD") << QStringLiteral("Match");
    QTest::newRow("padded") << QStringLiteral("  ") + kAbcSha256 + QStringLiteral("   ") << QStringLiteral("Match");
    QTest::newRow("padded uppercase") << QStringLiteral("\t") + kAbcSha256.toUpper() + QStringLiteral("\n") << QStringLiteral("Match");
    QTest::newRow("wrong digit") << QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ae") << QStringLiteral("No match");
    QTest::newRow("truncated") << kAbcSha256.left(60) << QStringLiteral("No match");
    // Nothing to compare against, so the window offers no verdict either way.
    QTest::newRow("blank") << QStringLiteral("    ") << QString();
  }
  void compareAcceptsCaseAndSurroundingSpaces() {
    QFETCH(QString, entered);
    QFETCH(QString, verdict);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    HashCheckerDialog dialog;
    dialog.show();
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    dialog.setFile(path);
    QTRY_COMPARE(hashed.count(), 1);
    QCOMPARE(dialog.digest(), kAbcSha256);
    QVERIFY(dialog.comparison().isEmpty());  // Nothing to compare against yet.
    field(dialog, "hashCompare")->setText(entered);
    QCOMPARE(dialog.comparison(), verdict);
    // Recomputing keeps the pasted value as the comparison target.
    button(dialog, "computeHash")->click();
    QTRY_COMPARE(hashed.count(), 2);
    QCOMPARE(dialog.digest(), kAbcSha256);
    QCOMPARE(dialog.comparison(), verdict);
    QVERIFY(field(dialog, "hashCompare")->text() == entered);
  }

  void copyPutsTheDigestOnTheClipboard() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = writeFile(dir.filePath("abc"), QByteArrayLiteral("abc"));
    QVERIFY(!path.isEmpty());
    HashCheckerDialog dialog;
    dialog.show();
    // Copy before any hash must not clear or invent clipboard content.
    QVERIFY(!button(dialog, "copyHash")->isEnabled());
    button(dialog, "copyHash")->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("clipboard before hashing"));
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    dialog.setFile(path);
    QTRY_COMPARE(hashed.count(), 1);
    QCOMPARE(dialog.digest(), kAbcSha256);
    button(dialog, "copyHash")->click();
    QCOMPARE(QApplication::clipboard()->text(), kAbcSha256);
    QVERIFY(dialog.statusText().contains(QStringLiteral("clipboard")));
  }

  void unreadableFileReportsFailureWithoutADigest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HashCheckerDialog dialog;
    dialog.show();
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    dialog.setFile(dir.filePath("absent"));
    QTRY_COMPARE(hashed.count(), 1);
    QVERIFY(hashed.first().first().toString().isEmpty());
    QVERIFY(dialog.digest().isEmpty());
    QVERIFY(!dialog.busy());
    QVERIFY(!button(dialog, "copyHash")->isEnabled());
    QVERIFY(dialog.statusText().contains(QStringLiteral("could not be read")));
  }

  void computingWithoutAFileDoesNothing() {
    HashCheckerDialog dialog;
    dialog.show();
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    button(dialog, "computeHash")->click();
    QTest::qWait(50);
    QCOMPARE(hashed.count(), 0);
    QVERIFY(!dialog.busy());
    QVERIFY(dialog.digest().isEmpty());
    QVERIFY(dialog.statusText().contains(QStringLiteral("Choose a file")));
  }

  // Cancel has to answer a read that is genuinely still in flight, without
  // waiting for it and without leaving a digest over part of the file.
  void cancelAnswersAnInFlightReadWithoutADigest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = makePipe(dir);
    QVERIFY(!path.isEmpty());
    HeldOpenPipe writer(path);
    HashCheckerDialog dialog;
    dialog.show();
    QSignalSpy hashed(&dialog, &HashCheckerDialog::hashed);
    dialog.setFile(path);
    QTRY_VERIFY(dialog.busy());
    QVERIFY(button(dialog, "cancelHash")->isEnabled());
    QVERIFY(!button(dialog, "computeHash")->isEnabled());
    QVERIFY(!button(dialog, "chooseHashFile")->isEnabled());

    QElapsedTimer elapsed;
    elapsed.start();
    button(dialog, "cancelHash")->click();
    QVERIFY2(elapsed.elapsed() < 500, "Cancel waited for the read instead of answering it");
    QVERIFY(dialog.busy());  // Still reading: the cancel is a request, not a wait.

    writer.release();  // Let the read end so the worker can report its outcome.
    QTRY_COMPARE(hashed.count(), 1);
    QVERIFY(hashed.first().first().toString().isEmpty());
    QVERIFY(dialog.digest().isEmpty());
    QVERIFY(!dialog.busy());
    QVERIFY(dialog.statusText().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QVERIFY(!button(dialog, "copyHash")->isEnabled());
    // A later run is still possible after a cancel.
    QVERIFY(button(dialog, "computeHash")->isEnabled());
  }

  // Closing while reading answers the read instead of orphaning its worker.
  void closingWhileHashingStopsTheRead() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = makePipe(dir);
    QVERIFY(!path.isEmpty());
    HeldOpenPipe writer(path);
    auto *dialog = new HashCheckerDialog;
    dialog->show();
    dialog->setFile(path);
    QTRY_VERIFY(dialog->busy());
    QElapsedTimer elapsed;
    elapsed.start();
    dialog->reject();
    QVERIFY(elapsed.elapsed() < 500);
    writer.release();
    QTRY_VERIFY(!dialog->busy());
    QVERIFY(dialog->digest().isEmpty());
    delete dialog;
  }

  // Destruction with a read still running must not touch the gone dialog.
  void destroyingWhileHashingIsSafe() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = makePipe(dir);
    QVERIFY(!path.isEmpty());
    HeldOpenPipe writer(path);
    auto *dialog = new HashCheckerDialog;
    dialog->show();
    dialog->setFile(path);
    QTRY_VERIFY(dialog->busy());
    delete dialog;
    // The worker now finishes into a guard, so the event loop stays usable.
    writer.release();
    QElapsedTimer elapsed;
    elapsed.start();
    QTest::qWait(200);
    QVERIFY(elapsed.elapsed() < 2000);
  }
};

QTEST_MAIN(HashCheckerDialogTest)
#include "HashCheckerDialogTest.moc"