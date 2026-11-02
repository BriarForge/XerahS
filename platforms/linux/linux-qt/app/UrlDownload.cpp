#include "UrlDownload.h"
#include "RasterSource.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslError>

namespace xerahs::app {
using namespace xerahs::editor;
static_assert(xerahs::editor::kMaxDownloadBytes == kMaxEncodedImageBytes, "download cap must equal the shared decoder's encoded cap");
namespace {
QString text(const char16_t *value) { return QString::fromUtf16(value); }
bool isRedirect(int status) { return status == 301 || status == 302 || status == 303 || status == 307 || status == 308; }
}  // namespace

UrlDownload::UrlDownload(QNetworkAccessManager *manager, QObject *parent) : QObject(parent), m_manager(manager) {
  qRegisterMetaType<UrlDownloadResult>();
}
UrlDownload::~UrlDownload() {
  if (m_reply) { m_reply->disconnect(this); m_reply->abort(); delete m_reply; m_reply = nullptr; }
}

void UrlDownload::start(const QUrl &url, bool allowPlainHttp, DownloadLimits limits) {
  if (m_running) return;
  m_running = true; m_allowPlainHttp = allowPlainHttp; m_limits = limits; m_abortReason.clear(); m_result = {};
  request(url);
}

void UrlDownload::request(const QUrl &url) {
  m_current = url; m_result.bytes.clear(); m_result.finalUrl = url;
  QNetworkRequest req(url);
  // Redirects are decided by decideRedirect(), not by Qt.
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  req.setTransferTimeout(m_limits.timeoutMs);
  req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("XerahS"));
  req.setRawHeader("Accept", "image/*, */*;q=0.1");
  req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);  // no local copy through Qt's cache either
  req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  if (!m_extraCas.isEmpty()) {
    auto tls = QSslConfiguration::defaultConfiguration(); tls.addCaCertificates(m_extraCas); req.setSslConfiguration(tls);
  }
  m_reply = m_manager->get(req);
  m_reply->setReadBufferSize(1 << 20);
  connect(m_reply, &QNetworkReply::readyRead, this, &UrlDownload::drain);
  connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
    if (total > m_limits.maxBytes) { abortWith(text(urlDiagnostic::tooLarge)); return; }
    emit progress(received, total);
  });
  connect(m_reply, &QNetworkReply::finished, this, &UrlDownload::onFinished);
}

void UrlDownload::drain() {
  if (!m_reply || !m_abortReason.isEmpty()) return;
  const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (isRedirect(status) || status >= 400) { m_reply->readAll(); return; }  // body is not image data
  const qint64 declared = m_reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
  if (declared > m_limits.maxBytes) { abortWith(text(urlDiagnostic::tooLarge)); return; }
  // Enforce the cap while streaming, whatever Content-Length claimed.
  while (m_reply && m_reply->bytesAvailable() > 0) {
    const QByteArray chunk = m_reply->read(65536);
    if (m_result.bytes.size() + chunk.size() > m_limits.maxBytes) { abortWith(text(urlDiagnostic::tooLarge)); return; }
    m_result.bytes.append(chunk);
  }
}

void UrlDownload::abortWith(const QString &diagnostic) {
  if (!m_abortReason.isEmpty()) return;
  m_abortReason = diagnostic;
  m_result.bytes.clear(); m_result.bytes.squeeze();
  if (m_reply) m_reply->abort(); else complete(diagnostic);
}

void UrlDownload::cancel() {
  if (!m_running) return;
  abortWith(text(urlDiagnostic::cancelled));
}

void UrlDownload::onFinished() {
  QPointer<UrlDownload> guard(this);
  QNetworkReply *reply = m_reply;
  if (!reply) return;
  if (m_abortReason.isEmpty() && reply->error() == QNetworkReply::NoError) drain();  // bytes that arrived with the last chunk
  m_reply = nullptr;
  reply->disconnect(this);
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const auto error = reply->error();
  const QString location = reply->header(QNetworkRequest::LocationHeader).toString();
  const QString rawLocation = location.isEmpty() ? QString::fromLatin1(reply->rawHeader("Location")) : location;
  reply->deleteLater();
  if (!m_abortReason.isEmpty()) { complete(m_abortReason); return; }
  if (error == QNetworkReply::TimeoutError) { complete(text(urlDiagnostic::timeout)); return; }
  if (error == QNetworkReply::SslHandshakeFailedError) { complete(text(urlDiagnostic::tlsFailed)); return; }
  if (error == QNetworkReply::OperationCanceledError) { complete(text(urlDiagnostic::cancelled)); return; }
  if (isRedirect(status) && error == QNetworkReply::NoError) {
    const auto decision = decideRedirect(m_current, rawLocation, m_result.redirects, m_allowPlainHttp, m_limits.maxRedirects);
    if (!decision.target) { complete(decision.error); return; }
    ++m_result.redirects;
    request(*decision.target);
    return;
  }
  if (error != QNetworkReply::NoError) {
    complete(status >= 400 ? text(urlDiagnostic::httpStatus) : text(urlDiagnostic::network)); return;
  }
  if (status < 200 || status >= 300) { complete(text(urlDiagnostic::httpStatus)); return; }
  if (m_result.bytes.isEmpty()) { complete(text(urlDiagnostic::emptyResponse)); return; }
  complete({});
}

void UrlDownload::complete(const QString &diagnostic) {
  if (!m_running) return;
  m_running = false;
  UrlDownloadResult result = std::move(m_result); m_result = {};
  result.diagnostic = diagnostic;
  if (!diagnostic.isEmpty()) { result.bytes.clear(); result.bytes.squeeze(); }
  emit finished(result);
}
}  // namespace xerahs::app
