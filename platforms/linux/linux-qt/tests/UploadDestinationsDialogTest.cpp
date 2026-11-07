// CORE-021, COMPCAT-011: the destinations window with injected pickers and a memory keyring.
#include "UploadDestinationsDialog.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPointer>
#include <QMessageBox>
#include <QTimer>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>

using namespace xerahs::app;
using namespace xerahs::uploads;

namespace {
class ReadCountingCredentials final : public CredentialStore {
public:
  mutable std::atomic<int> reads{0};
  CredentialResult store(const QUuid &, const QString &, const QString &) override { return {}; }
  CredentialResult lookup(const QUuid &, const QString &) const override { ++reads; return {}; }
  CredentialResult lookupWithoutPrompt(const QUuid &, const QString &) const override { ++reads; return {}; }
  CredentialResult remove(const QUuid &, const QString &) override { return {}; }
  CredentialResult removeAll(const QUuid &) override { return {}; }
};
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
  void selectedDestinationPublishesValueFreeCapabilitiesWithoutNetworkOrCredentialLookup() {
    DestinationCatalog catalog(m_dir.filePath("destinations.json"));
    UploadDestination first; first.id = QUuid::createUuid(); first.name = "PRIVATE-name";
    first.requestUrl = "https://PRIVATE.example/upload?token=PRIVATE-token";
    first.headers = {{"Authorization", "{secret:PRIVATE-secret}"}}; first.deletionUrl = "https://PRIVATE.example/delete";
    auto second = first; second.id = QUuid::createUuid(); second.method = RequestMethod::Put; second.deletionUrl.clear();
    QVERIFY(catalog.add(first).isEmpty()); QVERIFY(catalog.add(second).isEmpty());
    ReadCountingCredentials credentials; int networks = 0;
    auto p = ports(); p.connectionNetwork = [&](QObject *) -> QNetworkAccessManager * { ++networks; return nullptr; };
    UploadDestinationsDialog dialog(catalog, credentials, p);
    settle(dialog);
    dialog.show();
    auto *button = dialog.findChild<QPushButton *>("viewDestinationCapabilities"); QVERIFY(button); QVERIFY(button->isEnabled());
    button->click();
    QPointer<QDialog> review = dialog.findChild<QDialog *>("destinationCapabilitiesDialog"); QVERIFY(review); QVERIFY(review->isVisible());
    auto *text = review->findChild<QPlainTextEdit *>("destinationCapabilities"); QVERIFY(text); QVERIFY(text->isReadOnly());
    const auto value = QJsonDocument::fromJson(text->toPlainText().toUtf8()).object();
    QCOMPARE(value, destinationCapabilities(first));
    QVERIFY(!text->toPlainText().contains("PRIVATE")); QCOMPARE(networks, 0); QCOMPARE(credentials.reads.load(), 0); QVERIFY(m_reports.isEmpty());
    review->reject(); QTRY_VERIFY(!review);
    QListWidget *destinations = nullptr;
    for (auto *list : dialog.findChildren<QListWidget *>()) if (list->accessibleName() == "Destinations") destinations = list;
    QVERIFY(destinations); destinations->setCurrentRow(1); settle(dialog); button->click();
    review = dialog.findChild<QDialog *>("destinationCapabilitiesDialog"); QVERIFY(review);
    const auto changed = QJsonDocument::fromJson(review->findChild<QPlainTextEdit *>("destinationCapabilities")->toPlainText().toUtf8()).object();
    QCOMPARE(changed, destinationCapabilities(second)); QCOMPARE(networks, 0); QCOMPARE(credentials.reads.load(), 0);
    review->reject(); QTRY_VERIFY(!review);
    DestinationCatalog empty(m_dir.filePath("empty-destinations.json"));
    UploadDestinationsDialog emptyDialog(empty, credentials, p);
    QVERIFY(!emptyDialog.findChild<QPushButton *>("viewDestinationCapabilities")->isEnabled());
  }

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
    auto *report = dialog.findChild<QLabel *>("destinationImportReport"); QVERIFY(report);
    QVERIFY(report->text().contains("transformed:")); QVERIFY(!report->text().contains("super-secret-value"));
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
    QCOMPARE(m_reports.size(), 1);
    QVERIFY(m_reports.first().startsWith("This is not a valid custom uploader file."));
    QVERIFY(m_reports.first().contains("rejected: 1"));
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

  void changingDefaultRequiresExplicitConfirmationAndDefaultsToNo() {
    DestinationCatalog catalog(m_dir.filePath("destinations.json")); MemoryCredentialStore credentials;
    UploadDestination first; first.id = QUuid::createUuid(); first.name = "First"; first.requestUrl = "https://example.test/first";
    auto second = first; second.id = QUuid::createUuid(); second.name = "Second"; second.requestUrl = "https://example.test/second";
    QVERIFY(catalog.add(first).isEmpty()); QVERIFY(catalog.add(second).isEmpty()); QCOMPARE(catalog.defaultId(), first.id);
    auto injected = ports(); injected.confirmDefault = nativeUploadDestinationsPorts().confirmDefault;
    UploadDestinationsDialog dialog(catalog, credentials, injected);
    auto *list = dialog.findChild<QListWidget *>(); QVERIFY(list); list->setCurrentRow(1); settle(dialog);
    bool asked = false; bool allow = false; QTimer answer;
    connect(&answer, &QTimer::timeout, [&] {
      auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box) return;
      asked = true; QCOMPARE(box->windowTitle(), QString("Change upload destination"));
      QCOMPARE(box->defaultButton(), box->button(QMessageBox::No));
      box->button(allow ? QMessageBox::Yes : QMessageBox::No)->click();
    });
    answer.start(1); dialog.setSelectedAsDefault(); answer.stop(); QVERIFY(asked); QCOMPARE(catalog.defaultId(), first.id);
    allow = true; asked = false; answer.start(1); dialog.setSelectedAsDefault(); answer.stop(); QVERIFY(asked); QCOMPARE(catalog.defaultId(), second.id);
    DestinationCatalog reopened(m_dir.filePath("destinations.json")); QCOMPARE(reopened.defaultId(), second.id);
    // A missing presentation/confirmation port cannot expand authority.
    auto absent = ports(); UploadDestinationsDialog noPresentation(catalog, credentials, absent);
    noPresentation.findChild<QListWidget *>()->setCurrentRow(0); settle(noPresentation); noPresentation.setSelectedAsDefault();
    QCOMPARE(catalog.defaultId(), second.id);
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
