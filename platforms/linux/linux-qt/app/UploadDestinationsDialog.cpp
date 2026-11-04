#include "NativeAccessibility.h"
#include "UploadDestinationsDialog.h"
#include "OwnedWork.h"
#include "DestinationConnectionTest.h"
#include <QCloseEvent>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {

using uploads::CredentialStatus;
using uploads::DestinationCatalog;

namespace {
constexpr qint64 kMaxSxcuBytes = 1024 * 1024;

QString statusText(CredentialStatus status) {
  switch (status) {
  case CredentialStatus::Ok: return QCoreApplication::translate("UploadDestinationsDialog", "stored");
  case CredentialStatus::NotFound: return QCoreApplication::translate("UploadDestinationsDialog", "missing, set it before uploading");
  case CredentialStatus::Denied: return QCoreApplication::translate("UploadDestinationsDialog", "keyring locked or access refused");
  case CredentialStatus::Unavailable: return QCoreApplication::translate("UploadDestinationsDialog", "no keyring service available");
  }
  return {};
}

QString codeText(const QString &code, const QString &secretName = {}) {
  if (code == QStringLiteral("secret-store-failed"))
    return QCoreApplication::translate("UploadDestinationsDialog", "The secret \"%1\" could not be saved to the keyring, so the destination was not added.").arg(secretName);
  if (code == QStringLiteral("catalog-changed-reopen")) return QCoreApplication::translate("UploadDestinationsDialog", "Destinations were changed elsewhere. Close and reopen this window.");
  if (code == QStringLiteral("catalog-read-only")) return QCoreApplication::translate("UploadDestinationsDialog", "The destinations file could not be read, so it will not be changed.");
  if (code == QStringLiteral("secrets-not-removed")) return QCoreApplication::translate("UploadDestinationsDialog", "The destination was removed, but its secrets could not be deleted from the keyring.");
  if (code == QStringLiteral("invalid-json")) return QCoreApplication::translate("UploadDestinationsDialog", "This is not a valid custom uploader file.");
  if (code == QStringLiteral("too-large")) return QCoreApplication::translate("UploadDestinationsDialog", "This file is too large to be a custom uploader.");
  return QCoreApplication::translate("UploadDestinationsDialog", "The change failed (%1).").arg(code);
}
}  // namespace

UploadDestinationsPorts nativeUploadDestinationsPorts() {
  UploadDestinationsPorts ports;
  ports.chooseSxcu = [](QWidget *parent) {
    return QFileDialog::getOpenFileName(parent, QCoreApplication::translate("UploadDestinationsDialog", "Import custom uploader"), {},
                                       QCoreApplication::translate("UploadDestinationsDialog", "Custom uploaders (*.sxcu *.json)"));
  };
  ports.askSecret = [](QWidget *parent, const QString &name) -> std::optional<QString> {
    bool ok = false;
    const auto value = QInputDialog::getText(parent, QCoreApplication::translate("UploadDestinationsDialog", "Set secret"),
                                             QCoreApplication::translate("UploadDestinationsDialog", "Value for \"%1\":").arg(name),
                                             QLineEdit::Password, {}, &ok);
    if (!ok) return std::nullopt;
    return value;
  };
  ports.confirmDefault = [](QWidget *parent, const QString &name) {
    return QMessageBox::question(parent, QCoreApplication::translate("UploadDestinationsDialog", "Change upload destination"),
        QCoreApplication::translate("UploadDestinationsDialog", "Use \"%1\" for future uploads? Only explicitly requested uploads will be sent.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  };
  ports.confirmRemove = [](QWidget *parent, const QString &name) {
    return QMessageBox::question(parent, QCoreApplication::translate("UploadDestinationsDialog", "Remove destination"),
                                 QCoreApplication::translate("UploadDestinationsDialog", "Remove \"%1\" and delete its secrets from the keyring?").arg(name),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  };
  ports.report = [](QWidget *parent, const QString &message) { QMessageBox::warning(parent, QCoreApplication::translate("UploadDestinationsDialog", "Upload destinations"), message); };
  return ports;
}

UploadDestinationsDialog::UploadDestinationsDialog(DestinationCatalog &catalog, uploads::CredentialStore &credentials,
                                                   UploadDestinationsPorts ports, QWidget *parent)
    : QDialog(parent), m_catalog(catalog), m_credentials(credentials), m_ports(std::move(ports)) {
  new NativeAccessibility(*this, tr("Manage destinations and keyring references. Connection tests are explicit and cancellable. Close cancels a running connection test."), true);
  setWindowTitle(tr("Upload destinations"));
  m_list = new QListWidget(this);
  m_list->setAccessibleName(tr("Destinations"));
  m_details = new QLabel(this);
  m_details->setWordWrap(true);
  m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_secrets = new QListWidget(this);
  m_secrets->setAccessibleName(tr("Secrets"));
  m_import = new QPushButton(tr("&Import .sxcu…"), this);
  m_default = new QPushButton(tr("Make &default"), this);
  m_secret = new QPushButton(tr("Set &secret…"), this);
  m_remove = new QPushButton(tr("&Remove"), this);
  m_test = new QPushButton(tr("&Test connection"), this);
  m_test->setObjectName("testDestinationConnection"); m_test->setAccessibleName(tr("Test destination connection"));
  m_test->setAccessibleDescription(tr("Explicitly sends a HEAD request with destination credentials, without capture content. Upload permission is not verified."));
  m_cancelTest = new QPushButton(tr("Cancel test"), this);
  m_cancelTest->setObjectName("cancelDestinationConnection"); m_cancelTest->setAccessibleName(tr("Cancel connection test"));
  m_cancelTest->setEnabled(false);
  m_testStatus = new QLabel(tr("Connection tests send an explicit HEAD request with destination credentials. No capture content is sent; upload permission is not verified."), this);
  m_testStatus->setObjectName("destinationConnectionStatus"); m_testStatus->setAccessibleName(tr("Destination connection test status"));
  m_testStatus->setWordWrap(true); m_testStatus->setTextFormat(Qt::PlainText);
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this);

  auto *side = new QVBoxLayout;
  side->addWidget(m_details);
  side->addWidget(new QLabel(tr("Secrets:"), this));
  side->addWidget(m_secrets);
  auto *columns = new QHBoxLayout;
  columns->addWidget(m_list, 1);
  columns->addLayout(side, 2);
  auto *buttons = new QHBoxLayout;
  for (auto *button : {m_import, m_default, m_secret, m_remove}) buttons->addWidget(button);
  buttons->addStretch();
  buttons->addWidget(close);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(columns);
  m_importReport = new QLabel(this); m_importReport->setObjectName(QStringLiteral("destinationImportReport"));
  m_importReport->setTextFormat(Qt::PlainText); m_importReport->setWordWrap(true);
  m_importReport->setAccessibleName(tr("Destination import report")); layout->addWidget(m_importReport);
  layout->addLayout(buttons);
  auto *testButtons = new QHBoxLayout; testButtons->addWidget(m_test); testButtons->addWidget(m_cancelTest); testButtons->addStretch();
  layout->addLayout(testButtons); layout->addWidget(m_testStatus);
  connect(m_test, &QPushButton::clicked, this, &UploadDestinationsDialog::testConnection);
  connect(m_cancelTest, &QPushButton::clicked, this, &UploadDestinationsDialog::cancelConnectionTest);
  resize(720, 420);

  connect(m_list, &QListWidget::currentRowChanged, this, &UploadDestinationsDialog::showSelected);
  connect(m_import, &QPushButton::clicked, this, [this] {
    const auto path = m_ports.chooseSxcu ? m_ports.chooseSxcu(this) : QString();
    if (!path.isEmpty()) importFile(path);
  });
  connect(m_default, &QPushButton::clicked, this, &UploadDestinationsDialog::setSelectedAsDefault);
  connect(m_secret, &QPushButton::clicked, this, [this] {
    if (auto *item = m_secrets->currentItem()) setSecret(item->data(Qt::UserRole).toString());
  });
  connect(m_remove, &QPushButton::clicked, this, &UploadDestinationsDialog::removeSelected);
  connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
  refresh();
  if (!m_catalog.writable()) m_ports.report(this, codeText(QStringLiteral("catalog-read-only")));
}

QUuid UploadDestinationsDialog::selectedId() const {
  const auto *item = m_list->currentItem();
  return item ? QUuid::fromString(item->data(Qt::UserRole).toString()) : QUuid();
}

void UploadDestinationsDialog::refresh(const QUuid &select) {
  const auto keep = select.isNull() ? selectedId() : select;
  const QSignalBlocker blocker(m_list);
  m_list->clear();
  int row = 0;
  for (const auto &entry : m_catalog.entries()) {
    auto label = entry.destination.name;
    if (entry.destination.id == m_catalog.defaultId()) label += tr(" (default)");
    if (!entry.problems.isEmpty()) label += tr(" (cannot upload)");
    auto *item = new QListWidgetItem(label, m_list);
    item->setData(Qt::UserRole, entry.destination.id.toString(QUuid::WithoutBraces));
    if (entry.destination.id == keep) row = m_list->count() - 1;
  }
  if (m_list->count() > 0) m_list->setCurrentRow(row);
  showSelected();
}

void UploadDestinationsDialog::showSelected() {
  m_secrets->clear();
  const auto destination = m_catalog.find(selectedId());
  updateButtons();
  QStringList recovery;
  if (!m_catalog.loadCode().isEmpty()) recovery << tr("%1: Saved destinations are unavailable. Restore a compatible file and reopen this window; the original is kept.").arg(m_catalog.loadCode());
  if (m_catalog.unreadableCount()) recovery << tr("%1 unreadable or duplicate-ID entries are preserved without being used. Repair the saved file to recover them.").arg(m_catalog.unreadableCount());
  if (!m_catalog.entries().isEmpty() && m_catalog.defaultId().isNull()) recovery << tr("The default destination is missing. Select a destination and choose Make default.");
  if (!destination) {
    if (!recovery.isEmpty()) { m_details->setText(recovery.join('\n')); return; }
    m_details->setText(m_catalog.entries().isEmpty() ? tr("No destinations yet. Import a ShareX custom uploader (.sxcu) to add one.") : QString());
    return;
  }
  QStringList lines = recovery;
  lines << tr("%1 %2").arg(uploads::requestMethodString(destination->method), destination->requestUrl) << tr("Body: %1").arg(uploads::bodyTypeString(destination->body));
  if (const auto problems = uploads::validateDestination(*destination); !problems.isEmpty())
    lines << tr("Cannot upload: %1").arg(problems.join(QStringLiteral(", ")));
  m_details->setText(lines.join('\n'));
  for (const auto &name : DestinationCatalog::secretNames(*destination)) {
    auto *item = new QListWidgetItem(tr("%1: checking…").arg(name), m_secrets);
    item->setData(Qt::UserRole, name);
  }
  if (m_secrets->count() > 0) m_secrets->setCurrentRow(0);
  checkSecrets();
}

void UploadDestinationsDialog::checkSecrets() {
  const auto id = selectedId();
  QStringList names;
  for (int i = 0; i < m_secrets->count(); ++i) names << m_secrets->item(i)->data(Qt::UserRole).toString();
  if (id.isNull() || names.isEmpty()) { emit secretsChecked(); return; }
  auto *credentials = &m_credentials;
  runCredentialWork(
      [credentials, id, names] {
        QVariantList statuses;
        for (const auto &name : names) statuses << int(credentials->availability(id, name));
        return QVariant(statuses);
      },
      [this, id, names](const QVariant &result) {
        const auto statuses = result.toList();
        if (selectedId() == id && m_secrets->count() == names.size())
          for (int i = 0; i < names.size(); ++i)
            m_secrets->item(i)->setText(tr("%1: %2").arg(names.at(i), statusText(CredentialStatus(statuses.value(i).toInt()))));
        emit secretsChecked();
      });
}

void UploadDestinationsDialog::importFile(const QString &path) {
  if (m_busy) return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    compatibility::ImportReport report; report.add(compatibility::ImportDisposition::Rejected);
    m_importReport->setText(report.summary());
    m_ports.report(this, tr("The file could not be opened.\n") + report.summary()); return;
  }
  const auto import = uploads::importSxcu(file.read(kMaxSxcuBytes + 1));
  m_importReport->setText(import.report.summary());
  if (!import.ok || import.acceptanceBlocked) {
    m_ports.report(this, codeText(import.ok ? QStringLiteral("import-critical-unsupported") : import.code) + "\n" + import.report.summary()); return;
  }
  auto *catalog = &m_catalog;
  auto *credentials = &m_credentials;
  runCredentialWork(
      [catalog, credentials, import] {
        const auto outcome = catalog->addImported(import, *credentials);
        return QVariant(QStringList{outcome.code, outcome.id.toString(QUuid::WithoutBraces), outcome.secretName});
      },
      [this](const QVariant &result) {
        const auto outcome = result.toStringList();
        if (!outcome.value(0).isEmpty()) m_ports.report(this, codeText(outcome.value(0), outcome.value(2)));
        refresh(QUuid::fromString(outcome.value(1)));
      });
}

void UploadDestinationsDialog::setSelectedAsDefault() {
  const auto id = selectedId();
  if (busy() || id.isNull() || id == m_catalog.defaultId() || !m_ports.confirmDefault) return;
  const auto destination = m_catalog.find(id); if (!destination) return;
  QPointer<UploadDestinationsDialog> self(this);
  const bool confirmed = m_ports.confirmDefault(this, destination->name);
  if (!self || !confirmed || busy() || selectedId() != id) return;
  if (const auto code = m_catalog.setDefault(id); !code.isEmpty()) m_ports.report(this, codeText(code));
  refresh(id);
}

void UploadDestinationsDialog::setSecret(const QString &name) {
  const auto id = selectedId();
  if (m_busy || id.isNull() || name.isEmpty() || !m_ports.askSecret) return;
  const auto value = m_ports.askSecret(this, name);
  if (!value) return;
  auto *credentials = &m_credentials;
  runCredentialWork([credentials, id, name, secret = *value] { return QVariant(int(credentials->store(id, name, secret).status)); },
                    [this, name](const QVariant &result) {
                      const auto status = CredentialStatus(result.toInt());
                      if (status != CredentialStatus::Ok)
                        m_ports.report(this, tr("The secret \"%1\" was not saved: %2.").arg(name, statusText(status)));
                      showSelected();
                    });
}

void UploadDestinationsDialog::removeSelected() {
  const auto destination = m_catalog.find(selectedId());
  if (m_busy || !destination) return;
  if (m_ports.confirmRemove && !m_ports.confirmRemove(this, destination->name)) return;
  auto *catalog = &m_catalog;
  auto *credentials = &m_credentials;
  const auto id = destination->id;
  runCredentialWork([catalog, credentials, id] { return QVariant(catalog->remove(id, *credentials)); },
                    [this](const QVariant &result) {
                      if (!result.toString().isEmpty()) m_ports.report(this, codeText(result.toString()));
                      refresh();
                    });
}

void UploadDestinationsDialog::setBusy(bool busy) {
  if (m_busy == busy) return;
  m_busy = busy;
  m_list->setEnabled(!busy && !m_testing);
  updateButtons();
  emit busyChanged(busy);
}

void UploadDestinationsDialog::updateButtons() {
  const auto destination = m_catalog.find(selectedId());
  const bool editable = m_catalog.writable() && !m_busy && !m_testing;
  m_import->setEnabled(editable);
  m_default->setEnabled(editable && destination && destination->id != m_catalog.defaultId());
  m_remove->setEnabled(editable && destination.has_value());
  m_secret->setEnabled(!m_busy && !m_testing && destination.has_value());
  m_test->setEnabled(!m_busy && !m_testing && destination.has_value());
  m_cancelTest->setEnabled(m_testing);
}

UploadDestinationsDialog::~UploadDestinationsDialog() {
  m_testPreparation.cancel();
  if (m_connectionTest) { m_connectionTest->disconnect(this); delete m_connectionTest.data(); }
}
void UploadDestinationsDialog::reject() { const QPointer<UploadDestinationsDialog> owner(this); cancelConnectionTest(); if (owner) QDialog::reject(); }
void UploadDestinationsDialog::closeEvent(QCloseEvent *event) { const QPointer<UploadDestinationsDialog> owner(this); cancelConnectionTest(); if (owner) QDialog::closeEvent(event); }
void UploadDestinationsDialog::testConnection() {
  if (busy()) return;
  const auto destination = m_catalog.find(selectedId()); if (!destination) return;
  m_testing = true; const auto generation = ++m_testGeneration;
  m_list->setEnabled(false); updateButtons();
  m_testStatus->setText(tr("Preparing connection test… Cancel stops the test before any further network action."));
  m_testStatus->setAccessibleDescription(m_testStatus->text());
  const QPointer<UploadDestinationsDialog> owner(this); emit busyChanged(true); if (!owner) return;
  auto *credentials = &m_credentials;
  m_testPreparation = OwnedWork::run<uploads::UploadRequestResult>(this,
      [destination = *destination, credentials, lifetime = m_ports.credentialLifetime](const OwnedWork::Context &work) {
        if (work.cancelled()) return uploads::UploadRequestResult{{}, "connection-test-cancelled", {}};
        try { return uploads::buildConnectionTestRequest(destination, *credentials); }
        catch (...) { return uploads::UploadRequestResult{{}, "credential-unavailable", {}}; }
      }, [this, generation](uploads::UploadRequestResult prepared) {
        if (generation != m_testGeneration || !m_testing) return;
        if (m_testPreparation.cancelled()) { finishConnectionTest("connection-test-cancelled"); return; }
        if (!prepared.request) { finishConnectionTest("connection-test-configuration-failed"); return; }
        auto *manager = m_ports.connectionNetwork ? m_ports.connectionNetwork(this) : nullptr;
        auto *test = new DestinationConnectionTest(manager, this); m_connectionTest = test;
        connect(test, &DestinationConnectionTest::finished, this, [this, test, generation](const QString &code) {
          if (m_connectionTest == test) m_connectionTest.clear();
          test->deleteLater();
          if (generation == m_testGeneration) finishConnectionTest(code);
        });
        m_testStatus->setText(tr("Testing destination connection… No capture content is being sent. Cancel is available."));
        m_testStatus->setAccessibleDescription(m_testStatus->text());
        test->start(*prepared.request);
      });
}
void UploadDestinationsDialog::cancelConnectionTest() {
  if (!m_testing) return;
  ++m_testGeneration; m_testPreparation.cancel();
  const QPointer<UploadDestinationsDialog> owner(this);
  if (m_connectionTest) { auto *test = m_connectionTest.data(); m_connectionTest.clear(); test->cancel(); }
  if (!owner) return;
  finishConnectionTest("connection-test-cancelled");
}
void UploadDestinationsDialog::finishConnectionTest(const QString &code) {
  m_testing = false; m_list->setEnabled(!m_busy); updateButtons();
  const auto explanation = code == "connection-test-succeeded" ? tr("Endpoint accepted HEAD. Upload permission is not verified.") :
      code == "connection-test-cancelled" ? tr("Connection test cancelled.") :
      code == "connection-test-unsupported" ? tr("Endpoint does not support HEAD. No upload was attempted.") :
      code == "connection-test-redirect-refused" ? tr("Endpoint redirected. Credentials were not forwarded; check the configured endpoint.") :
      tr("Connection test failed. Check the endpoint, credentials and service availability before testing again.");
  m_testStatus->setText(code + ": " + explanation); m_testStatus->setAccessibleDescription(m_testStatus->text());
  emit busyChanged(m_busy);
}

void UploadDestinationsDialog::runCredentialWork(std::function<QVariant()> work, std::function<void(const QVariant &)> done) {
  setBusy(true);
  OwnedWork::run<QVariant>(this,
      [work = std::move(work), credentials = m_ports.credentialLifetime, catalog = m_ports.catalogLifetime](const OwnedWork::Context &) { return work(); },
      [this, done = std::move(done)](QVariant result) {
        setBusy(false);
        done(result);
      });
}

}  // namespace xerahs::app
