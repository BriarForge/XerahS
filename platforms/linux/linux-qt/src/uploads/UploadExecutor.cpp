// CORE-020, CORE-021, COMPCAT-011.
#include "UploadExecutor.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
namespace xerahs::uploads {
namespace { constexpr qint64 responseLimit = 4 * 1024 * 1024; }
UploadExecutor::UploadExecutor(QNetworkAccessManager *manager, QObject *parent, int timeoutMs)
    : QObject(parent), m_manager(manager), m_timeoutMs(timeoutMs) {
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, [this] {
    UploadOutcome out; out.code = QStringLiteral("timeout"); complete(out);
  });
}
UploadExecutor::~UploadExecutor() {
  if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
}
void UploadExecutor::start(const PreparedUploadRequest &request) {
  if (m_active) return;
  m_active = true; m_body.clear(); m_request = request;
  const auto generation = ++m_generation;
  if (!m_manager) {
    QTimer::singleShot(0, this, [this, generation] {
      if (!m_active || generation != m_generation) return;
      UploadOutcome out; out.code = QStringLiteral("network-error");
      out.errorMessage = QStringLiteral("Network manager unavailable"); complete(out);
    });
    return;
  }
  QNetworkRequest network(request.url);
  network.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  for (const auto &h : request.headers) network.setRawHeader(h.name.toUtf8(), h.value.toUtf8());
  if (!request.contentType.isEmpty()) network.setRawHeader("Content-Type", request.contentType);
  m_reply = m_manager->sendCustomRequest(network, request.method, request.body);
  m_reply->setReadBufferSize(responseLimit + 1);
  connect(m_reply, &QNetworkReply::uploadProgress, this, &UploadExecutor::progress);
  connect(m_reply, &QNetworkReply::readyRead, this, &UploadExecutor::drain);
  connect(m_reply, &QObject::destroyed, this, [this] {
    if (m_active) { UploadOutcome out; out.code = QStringLiteral("network-error"); complete(out); }
  });
  connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
    drain();
    if (!m_active || !m_reply || generation != m_generation) return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_reply->error() != QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 400))) {
      UploadOutcome out; out.code = QStringLiteral("network-error"); out.errorMessage = m_reply->errorString();
      complete(out); return;
    }
    complete(parseUploadResponse(m_request.destination, status, m_reply->rawHeaderPairs(), m_body, m_request.fileName));
  });
  m_timer.start(qMax(1, m_timeoutMs));
  emit progress(0, request.body.size());
}
void UploadExecutor::drain() {
  if (!m_active || !m_reply) return;
  m_body += m_reply->read(responseLimit + 1 - m_body.size());
  if (m_body.size() > responseLimit) {
    UploadOutcome out; out.code = QStringLiteral("response-too-large"); complete(out);
  }
}
void UploadExecutor::cancel() {
  if (!m_active) return;
  UploadOutcome out; out.code = QStringLiteral("cancelled"); complete(out);
}
void UploadExecutor::complete(UploadOutcome out) {
  if (!m_active) return;
  m_active = false; m_timer.stop();
  auto reply = m_reply; m_reply.clear();
  if (reply) { reply->disconnect(this); if (!reply->isFinished()) reply->abort(); reply->deleteLater(); }
  for (const auto &secret : m_request.sensitiveValues) {
    out.errorMessage.replace(secret, QStringLiteral("[redacted]"));
    out.errorMessage.replace(QString::fromLatin1(QUrl::toPercentEncoding(secret)), QStringLiteral("[redacted]"));
  }
  m_body.clear(); m_request = {};
  emit finished(out);
}
}
