// CORE-021, COMPCAT-011: the destinations window with injected pickers and a memory keyring.
#include "UploadDestinationsDialog.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace xerahs::app;
using namespace xerahs::uploads;

namespace {
const QByteArray kSxcu =
    "{\"Version\":\"17.0.0\",\"Name\":\"Example\",\"RequestMethod\":\"POST\","
    "\"RequestURL\":\"https://example.com/upload.php\","
    "\"Headers\":{\"Authorization\":\"Client-ID super-secret-value\"},"
    "\"Body\":\"MultipartFormData\",\"FileFormName\":\"file\",\"URL\":\"{json:url}\"}";
}  // namespace

class UploadDestinationsDialogTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QStringList m_reports;
  std::optional<QString> m_secretAnswer;
  bool m_confirm = true;

  QString writeSxcu(const QByteArray &bytes) {
    const auto path = m_dir.filePath(QStringLiteral("uploader.sxcu"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    file.write(bytes);
    return path;
  }
  UploadDestinationsPorts ports() {
    UploadDestinationsPorts ports;
    ports.chooseSxcu = [](QWidget *) { return QString(); };
    ports.askSecret = [this](QWidget *, const QString &) { return m_secretAnswer; };
    ports.confirmRemove = [this](QWidget *, const QString &) { return m_confirm; };
    ports.report = [this](QWidget *, const QString &message) { m_reports << message; };
    return ports;
  }
  static void settle(UploadDestinationsDialog &dialog) {
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.busy(), 5000);
  }
  static QStringList rows(const QWidget &dialog, const QString &name) {
    QStringList texts;
    auto *list = dialog.findChild<QListWidget *>();
    for (auto *candidate : dialog.findChildren<QListWidget *>())
      if (candidate->accessibleName() == name) list = candidate;
    for (int i = 0; i < list->count(); ++i) texts << list->item(i)->text();
    return texts;
  }

private slots:
  void init() {
    m_reports.clear(); m_secretAnswer.reset(); m_confirm = true;
    QFile::remove(m_dir.filePath(QStringLiteral("destinations.json")));
  }

  void importsShowsSecretStatusAndKeepsValuesOutOfTheWindow() {
    DestinationCatalog catalog(m_dir.filePath(QStringLiteral("destinations.json")));
    MemoryCredentialStore credentials;
    UploadDestinationsDialog dialog(catalog, credentials, ports());
    dialog.importFile(writeSxcu(kSxcu));
    settle(dialog);
    QVERIFY2(m_reports.isEmpty(), qPrintable(m_reports.join('\n')));
    QCOMPARE(rows(dialog, "Destinations"), QStringList{QStringLiteral("Example (default)")});
    QTRY_COMPARE(rows(dialog, "Secrets"), QStringList{QStringLiteral("Authorization: stored")});
    for (auto *label : dialog.findChildren<QLabel *>()) QVERIFY(!label->text().contains("super-secret-value"));
    QCOMPARE(credentials.lookup(catalog.defaultId(), "Authorization").value, QStringLiteral("Client-ID super-secret-value"));
  }

  void setSecretReplacesAndReportsKeyringFailure() {
    DestinationCatalog catalog(m_dir.filePath(QStringLiteral("destinations.json")));
    MemoryCredentialStore credentials;
    UploadDestinationsDialog dialog(catalog, credentials, ports());
    dialog.importFile(writeSxcu(kSxcu));
    settle(dialog);
    const auto id = catalog.defaultId();

    m_secretAnswer = QStringLiteral("Client-ID rotated");
    dialog.setSecret("Authorization");
    settle(dialog);
    QCOMPARE(credentials.lookup(id, "Authorization").value, QStringLiteral("Client-ID rotated"));

    m_secretAnswer.reset();
    dialog.setSecret("Authorization");
    QVERIFY(!dialog.busy());

    credentials.setForcedStatus(CredentialStatus::Denied);
    m_secretAnswer = QStringLiteral("x");
    dialog.setSecret("Authorization");
    settle(dialog);
    QCOMPARE(m_reports.size(), 1);
    QVERIFY(m_reports.first().contains("keyring locked"));
    QTRY_COMPARE(rows(dialog, "Secrets"), QStringList{QStringLiteral("Authorization: keyring locked or access refused")});
  }

  void failedKeyringImportAddsNothing() {
    DestinationCatalog catalog(m_dir.filePath(QStringLiteral("destinations.json")));
    MemoryCredentialStore credentials;
    credentials.setForcedStatus(CredentialStatus::Unavailable);
    UploadDestinationsDialog dialog(catalog, credentials, ports());
    dialog.importFile(writeSxcu(kSxcu));
    settle(dialog);
    QCOMPARE(m_reports.size(), 1);
    QVERIFY(m_reports.first().contains("\"Authorization\""));
    QVERIFY(catalog.entries().isEmpty());

    m_reports.clear();
    dialog.importFile(writeSxcu("not json"));
    QCOMPARE(m_reports, QStringList{QStringLiteral("This is not a valid custom uploader file.")});
  }

  void removeAsksFirstAndDeletesSecrets() {
    DestinationCatalog catalog(m_dir.filePath(QStringLiteral("destinations.json")));
    MemoryCredentialStore credentials;
    UploadDestinationsDialog dialog(catalog, credentials, ports());
    dialog.importFile(writeSxcu(kSxcu));
    settle(dialog);
    const auto id = catalog.defaultId();

    m_confirm = false;
    dialog.removeSelected();
    settle(dialog);
    QCOMPARE(catalog.entries().size(), 1);

    m_confirm = true;
    dialog.removeSelected();
    settle(dialog);
    QVERIFY(catalog.entries().isEmpty());
    QCOMPARE(credentials.lookup(id, "Authorization").status, CredentialStatus::NotFound);
    QVERIFY(rows(dialog, "Destinations").isEmpty());
  }

  void closingWhileBusyDoesNotTouchTheDialog() {
    DestinationCatalog catalog(m_dir.filePath(QStringLiteral("destinations.json")));
    MemoryCredentialStore credentials;
    {
      UploadDestinationsDialog dialog(catalog, credentials, ports());
      dialog.importFile(writeSxcu(kSxcu));
    }
    QTRY_COMPARE_WITH_TIMEOUT(catalog.entries().size(), 1, 5000);
    QTest::qWait(50);
  }
};

QTEST_MAIN(UploadDestinationsDialogTest)
#include "UploadDestinationsDialogTest.moc"
