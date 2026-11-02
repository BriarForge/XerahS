#include "UrlOpenDialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace xerahs::app {
using namespace xerahs::editor;

QString UrlOpenDialog::privacyText() {
  return QStringLiteral("Nothing is downloaded until you press Download. The server will see your request, your IP address "
                        "and the address you enter. The image is kept in memory only: no local copy is written unless you "
                        "save it from the editor.");
}

UrlOpenDialog::UrlOpenDialog(QWidget *parent, UrlOpenOptions options) : QDialog(parent), m_options(std::move(options)) {
  setObjectName(QStringLiteral("openImageUrlDialog")); setWindowTitle(QStringLiteral("Open Image from URL"));
  setAccessibleName(windowTitle()); setWindowModality(Qt::WindowModal);
  m_manager = m_options.manager ? m_options.manager : new QNetworkAccessManager(this);
  auto *layout = new QVBoxLayout(this);
  auto *label = new QLabel(QStringLiteral("Image address (https://…)"), this);
  m_url = new QLineEdit(this); m_url->setObjectName(QStringLiteral("openImageUrlInput"));
  m_url->setAccessibleName(QStringLiteral("Image address")); m_url->setPlaceholderText(QStringLiteral("https://example.com/image.png"));
  label->setBuddy(m_url);
  m_allowHttp = new QCheckBox(QStringLiteral("Allow unencrypted http:// for this request only (anyone on the network can read or alter it)"), this);
  m_allowHttp->setObjectName(QStringLiteral("openImageUrlAllowHttp"));
  auto *privacy = new QLabel(privacyText(), this); privacy->setObjectName(QStringLiteral("openImageUrlPrivacy"));
  privacy->setWordWrap(true); privacy->setTextFormat(Qt::PlainText);
  m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("openImageUrlStatus")); m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText); m_status->setAccessibleName(QStringLiteral("Download status"));
  m_progress = new QProgressBar(this); m_progress->setObjectName(QStringLiteral("openImageUrlProgress")); m_progress->setVisible(false);
  m_progress->setAccessibleName(QStringLiteral("Download progress"));
  auto *buttons = new QDialogButtonBox(this);
  m_start = buttons->addButton(QStringLiteral("Download"), QDialogButtonBox::AcceptRole); m_start->setObjectName(QStringLiteral("openImageUrlDownload"));
  m_cancel = buttons->addButton(QDialogButtonBox::Cancel); m_cancel->setObjectName(QStringLiteral("openImageUrlCancel"));
  m_start->setDefault(true);
  for (auto *w : std::initializer_list<QWidget *>{label, m_url, m_allowHttp, privacy, m_status, m_progress, buttons}) layout->addWidget(w);
  connect(m_start, &QPushButton::clicked, this, [this] { begin(); });
  connect(m_cancel, &QPushButton::clicked, this, [this] { reject(); });
  connect(m_url, &QLineEdit::returnPressed, m_start, &QPushButton::click);
  setMinimumWidth(460);
}

UrlOpenDialog::~UrlOpenDialog() { if (m_download) { m_download->disconnect(this); m_download->cancel(); } }

std::optional<DownloadedImage> UrlOpenDialog::takeResult() { auto r = std::move(m_result); m_result.reset(); return r; }

void UrlOpenDialog::setBusy(bool busy) {
  m_url->setEnabled(!busy); m_allowHttp->setEnabled(!busy); m_start->setEnabled(!busy); m_progress->setVisible(busy);
  if (busy) m_progress->setRange(0, 0);
}

void UrlOpenDialog::begin() {
  if (m_download) return;  // reentrant starts are ignored
  const auto checked = validateImageUrl(m_url->text(), m_allowHttp->isChecked());
  if (!checked.url) { m_status->setText(describeUrlDiagnostic(checked.error)); m_url->setFocus(); return; }
  m_requestedHttp = m_allowHttp->isChecked();
  m_status->setText(QStringLiteral("Contacting %1…").arg(sourceLabel(*checked.url)));
  setBusy(true);
  m_download = new UrlDownload(m_manager, this);
  connect(m_download, &UrlDownload::progress, this, [this](qint64 received, qint64 total) {
    if (total > 0) { m_progress->setRange(0, 1000); m_progress->setValue(int(double(received) / total * 1000)); }
    m_status->setText(QStringLiteral("Downloaded %1 KiB…").arg(received / 1024));
  });
  connect(m_download, &UrlDownload::finished, this, [this](const UrlDownloadResult &r) { finish(r); });
  m_download->start(*checked.url, m_requestedHttp, m_options.limits);
}

void UrlOpenDialog::finish(const UrlDownloadResult &result) {
  m_download->deleteLater(); m_download = nullptr;
  if (!result.diagnostic.isEmpty()) {
    setBusy(false); m_status->setText(describeUrlDiagnostic(result.diagnostic));
    if (result.diagnostic == QString::fromUtf16(urlDiagnostic::cancelled)) QDialog::reject();
    return;
  }
  m_result = DownloadedImage{result.bytes, result.finalUrl, sourceLabel(result.finalUrl)};
  accept();
}

void UrlOpenDialog::reject() {
  if (m_download && m_download->running()) { m_status->setText(QStringLiteral("Cancelling…")); m_download->cancel(); return; }
  QDialog::reject();
}
}  // namespace xerahs::app
