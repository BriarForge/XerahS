#include "BackgroundRemoverDialog.h"

#include "NativeAccessibility.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace xerahs::app {

using bg::CatalogueEntry;

namespace {

QString humanSize(qint64 bytes) {
  return QStringLiteral("%L1 bytes (about %2 MB)")
      .arg(bytes)
      .arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
}

}  // namespace

BackgroundRemoverDialog::BackgroundRemoverDialog(QWidget *parent) : QDialog(parent) {
  buildUi();
  const bg::CatalogueResult catalogue =
      bg::loadCatalogueFromFile(QStringLiteral(XERAHS_BACKGROUND_CATALOGUE));
  if (catalogue.ok) setCatalogue(catalogue.entries);
  else {
    m_entries.clear();
    m_modelCombo->clear();
    showError(catalogue.error);
    setSetupStatus(QStringLiteral("No approved background model could be loaded from the contract catalogue."));
    m_download->setEnabled(false);
    m_chooseLocal->setEnabled(false);
  }
}

void BackgroundRemoverDialog::buildUi() {
  setWindowTitle(tr("Background remover"));
  setMinimumWidth(560);

  auto *root = new QVBoxLayout(this);

  m_modelCombo = new QComboBox(this);
  m_modelCombo->setObjectName(QStringLiteral("modelCombo"));
  root->addWidget(new QLabel(tr("Model:"), this));
  root->addWidget(m_modelCombo);

  m_description = new QLabel(this);
  m_description->setObjectName(QStringLiteral("descriptionValue"));
  m_description->setWordWrap(true);
  root->addWidget(m_description);

  auto *disclosure = new QGroupBox(tr("Model supply and disclosure"), this);
  auto *form = new QFormLayout(disclosure);
  m_upstream = new QLabel(disclosure);
  m_licence = new QLabel(disclosure);
  m_size = new QLabel(disclosure);
  m_sourceUrl = new QLabel(disclosure);
  m_sourceUrl->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_host = new QLabel(disclosure);
  m_checksums = new QLabel(disclosure);
  m_checksums->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_storeValue = new QLabel(disclosure);
  m_network = new QLabel(
      tr("This download is the only network activity. Your images are never uploaded."), disclosure);
  for (auto *label : {m_upstream, m_licence, m_size, m_sourceUrl, m_host, m_checksums, m_storeValue, m_network}) {
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  m_upstream->setObjectName(QStringLiteral("upstreamValue"));
  m_licence->setObjectName(QStringLiteral("licenceValue"));
  m_size->setObjectName(QStringLiteral("sizeValue"));
  m_sourceUrl->setObjectName(QStringLiteral("urlValue"));
  m_host->setObjectName(QStringLiteral("hostValue"));
  m_checksums->setObjectName(QStringLiteral("checksumValue"));
  m_storeValue->setObjectName(QStringLiteral("storeValue"));
  m_network->setObjectName(QStringLiteral("networkValue"));
  form->addRow(tr("Upstream project:"), m_upstream);
  form->addRow(tr("Licence:"), m_licence);
  form->addRow(tr("Download size:"), m_size);
  form->addRow(tr("Artifact:"), m_sourceUrl);
  form->addRow(tr("Host:"), m_host);
  form->addRow(tr("Integrity:"), m_checksums);
  form->addRow(tr("Model store:"), m_storeValue);
  form->addRow(QString(), m_network);
  root->addWidget(disclosure);

  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("progress"));
  m_progress->hide();
  root->addWidget(m_progress);

  auto *actions = new QHBoxLayout;
  m_download = new QPushButton(tr("Download model"), this);
  m_download->setObjectName(QStringLiteral("download"));
  m_cancelDownload = new QPushButton(tr("Cancel download"), this);
  m_cancelDownload->setObjectName(QStringLiteral("cancelDownload"));
  m_cancelDownload->hide();
  m_chooseLocal = new QPushButton(tr("Use a local model file…"), this);
  m_chooseLocal->setObjectName(QStringLiteral("chooseLocal"));
  actions->addWidget(m_download);
  actions->addWidget(m_cancelDownload);
  actions->addStretch(1);
  actions->addWidget(m_chooseLocal);
  root->addLayout(actions);

  auto *statusGroup = new QGroupBox(tr("Status"), this);
  auto *statusForm = new QFormLayout(statusGroup);
  m_modelStatus = new QLabel(statusGroup);
  m_modelStatus->setObjectName(QStringLiteral("modelStatus"));
  m_deviceStatus = new QLabel(statusGroup);
  m_deviceStatus->setObjectName(QStringLiteral("deviceStatus"));
  m_setupStatus = new QLabel(statusGroup);
  m_setupStatus->setObjectName(QStringLiteral("setupStatus"));
  m_inferenceStatus = new QLabel(statusGroup);
  m_inferenceStatus->setObjectName(QStringLiteral("inferenceStatus"));
  for (auto *label : {m_modelStatus, m_deviceStatus, m_setupStatus, m_inferenceStatus})
    label->setWordWrap(true);
  statusForm->addRow(tr("Model:"), m_modelStatus);
  statusForm->addRow(tr("Device:"), m_deviceStatus);
  statusForm->addRow(tr("Setup:"), m_setupStatus);
  statusForm->addRow(tr("Inference:"), m_inferenceStatus);
  root->addWidget(statusGroup);

  m_error = new QLabel(this);
  m_error->setObjectName(QStringLiteral("errorLabel"));
  m_error->setWordWrap(true);
  root->addWidget(m_error);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  m_deviceStatus->setText(tr("CPU. Auto and GPU selection need the inference engine, which is not "
                             "linked in this build."));
  m_inferenceStatus->setText(tr("Not available in this build: the approved inference engine "
                                "(onnxruntime, MIT) is not linked. Model download and validation "
                                "still work."));

  connect(m_modelCombo, &QComboBox::currentIndexChanged, this, [this] { refreshStoreValidation(); });
  connect(m_download, &QPushButton::clicked, this, &BackgroundRemoverDialog::downloadModel);
  connect(m_cancelDownload, &QPushButton::clicked, this, &BackgroundRemoverDialog::cancelDownload);
  connect(m_chooseLocal, &QPushButton::clicked, this, &BackgroundRemoverDialog::chooseLocalModel);

  new NativeAccessibility(*this, tr("Background remover model setup window"));
}

void BackgroundRemoverDialog::setCatalogue(const QList<bg::CatalogueEntry> &entries) {
  m_entries = entries;
  m_localModelPath.clear();
  m_modelCombo->clear();
  for (const CatalogueEntry &entry : m_entries) m_modelCombo->addItem(entry.label);
  if (!m_entries.isEmpty()) {
    m_download->setEnabled(true);
    m_chooseLocal->setEnabled(true);
    clearError();
    refreshModelDisclosure();
    refreshStoreValidation();
  }
}

void BackgroundRemoverDialog::setStoreDirectory(const QString &directory) { m_storeDir = directory; }

void BackgroundRemoverDialog::setModelFilePicker(ModelFilePicker picker) { m_picker = std::move(picker); }

void BackgroundRemoverDialog::setDownloadConfirm(DownloadConfirm confirm) { m_confirm = std::move(confirm); }

QString BackgroundRemoverDialog::defaultStoreDirectory() const {
  if (!m_storeDir.isEmpty()) return m_storeDir;
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath(QStringLiteral("background-models"));
}

const CatalogueEntry *BackgroundRemoverDialog::currentEntry() const {
  const int index = m_modelCombo->currentIndex();
  return index >= 0 && index < m_entries.size() ? &m_entries.at(index) : nullptr;
}

QString BackgroundRemoverDialog::modelStatus() const { return m_modelStatus->text(); }
QString BackgroundRemoverDialog::deviceStatus() const { return m_deviceStatus->text(); }
QString BackgroundRemoverDialog::setupStatus() const { return m_setupStatus->text(); }
QString BackgroundRemoverDialog::inferenceStatus() const { return m_inferenceStatus->text(); }
QString BackgroundRemoverDialog::errorText() const { return m_error->text(); }

void BackgroundRemoverDialog::setSetupStatus(const QString &text) { m_setupStatus->setText(text); }

void BackgroundRemoverDialog::showError(const QString &message) { m_error->setText(message); }
void BackgroundRemoverDialog::clearError() { m_error->setText(QString()); }

void BackgroundRemoverDialog::refreshModelDisclosure() {
  const CatalogueEntry *entry = currentEntry();
  if (!entry) return;
  m_description->setText(entry->description);
  m_upstream->setText(QStringLiteral("%1 — %2\n%3")
                          .arg(entry->upstream.project, entry->upstream.authors, entry->upstream.url));
  m_licence->setText(QStringLiteral("%1 (%2)\n%3")
                        .arg(entry->licence.id, entry->licence.note, entry->licence.url));
  m_size->setText(humanSize(entry->artifact.sizeBytes));
  m_sourceUrl->setText(entry->artifact.url);
  m_host->setText(QStringLiteral("%1 (GitHub may redirect the bytes to its content CDN; the "
                                 "pinned checksums still apply)")
                      .arg(entry->artifact.host));
  m_checksums->setText(QStringLiteral("SHA-256 %1\nMD5 %2").arg(entry->artifact.sha256, entry->artifact.md5));
  m_storeValue->setText(defaultStoreDirectory());
  m_download->setText(tr("Download %1 (%2 from %3)")
                          .arg(entry->label, humanSize(entry->artifact.sizeBytes), entry->artifact.host));
}

void BackgroundRemoverDialog::refreshStoreValidation() {
  const CatalogueEntry *entry = currentEntry();
  if (!entry) return;
  m_localModelPath.clear();
  refreshModelDisclosure();
  const QString path = bg::storeModelPath(defaultStoreDirectory(), entry->id);
  const bg::ValidationResult result = bg::validateModelFile(path, *entry);
  switch (result.status) {
  case bg::ValidationStatus::Ok:
    m_modelStatus->setText(tr("%1 — verified in your model store (integrity and tensor contract OK).")
                               .arg(entry->label));
    setSetupStatus(tr("Ready. The model is pinned and validated."));
    m_download->setEnabled(false);
    clearError();
    break;
  case bg::ValidationStatus::Missing:
    m_modelStatus->setText(tr("%1 — not in your model store yet.").arg(entry->label));
    setSetupStatus(tr("Download the model to use it. Nothing is downloaded without your confirmation."));
    m_download->setEnabled(true);
    break;
  default:
    m_modelStatus->setText(tr("%1 — failed validation: %2.").arg(entry->label).arg(result.errorId()));
    setSetupStatus(tr("The stored model is not the approved one. Re-download it to replace it."));
    m_download->setEnabled(true);
    break;
  }
}

void BackgroundRemoverDialog::downloadModel() {
  const CatalogueEntry *entry = currentEntry();
  if (!entry || m_reply) return;
  clearError();

  // EU-010: the explicit opt-in. The default confirmation repeats every
  // disclosure item; a seam replaces it in tests.
  DownloadConfirm confirm = m_confirm;
  if (!confirm) {
    confirm = [this](const CatalogueEntry &candidate) {
      const QString text = QStringLiteral(
                                "Download %1?\n\n"
                                "Size: %2\n"
                                "Source: %3\n"
                                "Host: %4 (GitHub may redirect the bytes to its content CDN; the "
                                "pinned checksums still apply)\n"
                                "Licence: %5 — %6\n"
                                "Integrity: verified against SHA-256 %7 and MD5 %8 after the "
                                "download; any mismatch is discarded\n"
                                "Saved in: %9\n\n"
                                "This download is the only network activity. Your images are never "
                                "uploaded.")
          .arg(candidate.label, humanSize(candidate.artifact.sizeBytes), candidate.artifact.url,
               candidate.artifact.host, candidate.licence.id, candidate.licence.url,
               candidate.artifact.sha256, candidate.artifact.md5, defaultStoreDirectory());
      return QMessageBox::question(this, tr("Download model"), text,
                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
             QMessageBox::Yes;
    };
  }
  if (!confirm(*entry)) {
    setSetupStatus(tr("Download declined. Nothing was sent or saved."));
    return;
  }

  const QString storeDir = defaultStoreDirectory();
  if (bg::ensureStoreDir(storeDir) != bg::StoreStatus::Ok) {
    showError(tr("The model store directory could not be created."));
    setSetupStatus(tr("Failed: the model store directory is not writable."));
    return;
  }
  m_downloadTemp = bg::storeTempPath(storeDir, entry->id);
  QFile::remove(m_downloadTemp);
  QFile target(m_downloadTemp);
  if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    showError(tr("The temporary download file could not be created."));
    setSetupStatus(tr("Failed: the model store directory is not writable."));
    m_downloadTemp.clear();
    return;
  }
  target.close();

  if (!m_net) m_net = new QNetworkAccessManager(this);
  QNetworkRequest request(QUrl(entry->artifact.url));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  m_reply = m_net->get(request);
  m_abandoned = false;

  m_progress->setRange(0, int(entry->artifact.sizeBytes));
  m_progress->setValue(0);
  m_progress->show();
  m_download->setEnabled(false);
  m_cancelDownload->show();
  setSetupStatus(tr("Downloading…"));

  const QString tempPath = m_downloadTemp;
  const CatalogueEntry model = *entry;

  connect(m_reply, &QNetworkReply::downloadProgress, this, [this, model](qint64 received, qint64 total) {
    if (total > model.artifact.sizeBytes) {
      // A server streaming more than the pinned artifact is refused early.
      m_abandoned = true;
      m_reply->abort();
      return;
    }
    if (total > 0) m_progress->setRange(0, int(total));
    m_progress->setValue(int(received));
  });
  connect(m_reply, &QNetworkReply::readyRead, this, [this, tempPath] {
    if (!m_reply) return;
    QFile file(tempPath);
    if (!file.open(QIODevice::Append)) {
      m_reply->abort();
      return;
    }
    file.write(m_reply->readAll());
  });
  connect(m_reply, &QNetworkReply::finished, this, [this, tempPath, model] {
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();
    m_cancelDownload->hide();
    m_progress->hide();
    m_download->setEnabled(true);

    const QNetworkReply::NetworkError error = reply->error();
    if (m_abandoned) {
      QFile::remove(tempPath);
      m_downloadTemp.clear();
      showError(tr("The download was larger than the approved model (model-size-mismatch) and was "
                   "discarded."));
      setSetupStatus(tr("Failed: the served bytes are not the approved model."));
      return;
    }
    if (error == QNetworkReply::OperationCanceledError) {
      QFile::remove(tempPath);
      m_downloadTemp.clear();
      setSetupStatus(tr("Download cancelled. Nothing was saved."));
      return;
    }
    if (error != QNetworkReply::NoError) {
      QFile::remove(tempPath);
      m_downloadTemp.clear();
      showError(tr("The download failed before completing (%1).").arg(QString::number(int(error))));
      setSetupStatus(tr("Failed: the download did not complete."));
      return;
    }

    // EU-010: verify, then adopt. A bad artifact never reaches the store.
    QString detail;
    const bg::AdoptStatus adopted = bg::adoptModel(tempPath, defaultStoreDirectory(), model, &detail);
    m_downloadTemp.clear();
    if (adopted != bg::AdoptStatus::Ok) {
      QFile::remove(tempPath);
      if (detail == QStringLiteral("store-dir-failed")) {
        showError(tr("The model store directory is not writable."));
        setSetupStatus(tr("Failed: the downloaded model could not be stored."));
      } else {
        showError(tr("The downloaded model failed verification (%1) and was discarded.")
                      .arg(detail.isEmpty() ? QStringLiteral("model-unverified") : detail));
        setSetupStatus(tr("Failed: the downloaded bytes are not the approved model."));
      }
      return;
    }
    // The settled state ("pinned and validated") comes from the store check.
    refreshStoreValidation();
  });
}

void BackgroundRemoverDialog::cancelDownload() {
  if (m_reply) m_reply->abort();
}

void BackgroundRemoverDialog::chooseLocalModel() {
  ModelFilePicker picker = m_picker;
  if (!picker) {
    picker = [this] {
      return QFileDialog::getOpenFileName(this, tr("Choose a local model file"), QString(),
                                           tr("ONNX models (*.onnx)"));
    };
  }
  const QString path = picker();
  if (path.isEmpty()) return;
  clearError();

  const bg::HeaderResult header = bg::readModelHeader(path);
  if (header.status != bg::HeaderStatus::Ok || !bg::headerIsUsable(header.header)) {
    const QString reason = header.status == bg::HeaderStatus::Ok
                               ? tr("the model does not declare float32 tensor input and output")
                               : header.message();
    showError(tr("“%1” is not a usable model: %2.").arg(QFileInfo(path).fileName(), reason));
    return;
  }
  m_localModelPath = path;
  const QString contract =
      QStringLiteral("%1 → %2").arg(header.header.inputs.first().display(),
                                   header.header.outputs.first().display());
  m_modelStatus->setText(tr("User-supplied model: %1\n%2\nProvenance: your file, checksums not verified.")
                             .arg(QFileInfo(path).fileName(), contract));
  setSetupStatus(tr("Local model selected. Inference is not available in this build."));
}

void BackgroundRemoverDialog::reject() {
  // EU-017: closing a busy window cancels its download rather than leaving
  // background work behind.
  if (m_reply) {
    disconnect(m_reply, &QNetworkReply::finished, this, nullptr);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    if (!m_downloadTemp.isEmpty()) {
      QFile::remove(m_downloadTemp);
      m_downloadTemp.clear();
    }
    m_cancelDownload->hide();
    m_progress->hide();
    m_download->setEnabled(true);
    setSetupStatus(tr("Download cancelled on close. Nothing was saved."));
  }
  QDialog::reject();
}

}  // namespace xerahs::app
