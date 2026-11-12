// CORE-020, CORE-021, COMPCAT-011.
#include "UploadExecutor.h"
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <cstring>
namespace xerahs::uploads {
namespace {
bool transientNetworkError(QNetworkReply::NetworkError error) {
  return error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::TimeoutError ||
      error == QNetworkReply::TemporaryNetworkFailureError || error == QNetworkReply::NetworkSessionFailedError ||
      error == QNetworkReply::ConnectionRefusedError;
}
// Qt 6.4's HTTP channel calls uploadByteDevice->reset() and sends the same
// request again when the peer closes after the body and before a status line
// (QHttpNetworkConnectionPrivate::_q_startNextRequest). QByteArray uploads
// rewind. A non-idempotent body must not. Position stays valid so the first
// transmission still matches Content-Length; only rewind is refused.
class NonRewindableBody final : public QIODevice {
public:
  explicit NonRewindableBody(QByteArray bytes) : m_bytes(std::move(bytes)) { open(QIODevice::ReadOnly); }
  bool isSequential() const override { return false; }
  qint64 size() const override { return m_bytes.size(); }
  bool reset() override { return false; }
  bool seek(qint64 offset) override { return offset == pos() && QIODevice::seek(offset); }
protected:
  qint64 readData(char *out, qint64 max) override {
    const qint64 n = qMin(max, qint64(m_bytes.size()) - pos());
    if (n <= 0) return 0;
    std::memcpy(out, m_bytes.constData() + pos(), size_t(n));
    return n;
  }
  qint64 writeData(const char *, qint64) override { return -1; }
private:
  QByteArray m_bytes;
};
}
UploadExecutor::UploadExecutor(QNetworkAccessManager *manager, QObject *parent, int timeoutMs)
    : QObject(parent), m_manager(manager), m_timeoutMs(timeoutMs) {
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, [this] {
    UploadOutcome out; out.code = QStringLiteral("timeout"); attemptFinished(out, true);
  });
  m_retryTimer.setSingleShot(true);
  connect(&m_retryTimer, &QTimer::timeout, this, &UploadExecutor::sendAttempt);
}
UploadExecutor::~UploadExecutor() {
  m_finishing = true; m_retryTimer.stop(); releaseReply();
}
void UploadExecutor::start(const PreparedUploadRequest &request) {
  if (m_active || m_finishing) return;
  m_active = true; m_request = request; m_attempts = 0;
  sendAttempt();
}
void UploadExecutor::sendAttempt() {
  if (!m_active) return;
  m_body.clear();
  const auto generation = ++m_generation;
  if (!m_manager) {
    QTimer::singleShot(0, this, [this, generation] {
      if (!m_active || generation != m_generation) return;
      UploadOutcome out; out.code = QStringLiteral("network-error");
      out.errorMessage = QStringLiteral("Network manager unavailable"); complete(out);
    });
    return;
  }
  ++m_attempts;
  const auto &request = m_request;
  QNetworkRequest network(request.url);
  network.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  for (const auto &h : request.headers) network.setRawHeader(h.name.toUtf8(), h.value.toUtf8());
  if (!request.contentType.isEmpty()) network.setRawHeader("Content-Type", request.contentType);
  if (retrySafe()) {
    m_reply = m_manager->sendCustomRequest(network, request.method, request.body);
  } else {
    // Qt's HTTP/2 redirect handler ignores a failed body reset (6.4 and 6.12).
    // Use the HTTP/1 channel, which refuses the follow-up before sending it.
    network.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    auto *body = new NonRewindableBody(request.body);
    network.setHeader(QNetworkRequest::ContentLengthHeader, qint64(request.body.size()));
    network.setAttribute(QNetworkRequest::DoNotBufferUploadDataAttribute, true);
    m_reply = m_manager->sendCustomRequest(network, request.method, body);
    if (m_reply) body->setParent(m_reply);
    else delete body;
  }
  m_reply->setReadBufferSize(kMaximumUploadResponseBytes + 1);
  connect(m_reply, &QNetworkReply::uploadProgress, this, &UploadExecutor::progress);
  connect(m_reply, &QNetworkReply::readyRead, this, &UploadExecutor::drain);
  connect(m_reply, &QObject::destroyed, this, [this] {
    if (m_active) { UploadOutcome out; out.code = QStringLiteral("network-error"); attemptFinished(out, false); }
  });
  connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
    if (!m_active || !m_reply || generation != m_generation) return;
    const QPointer<UploadExecutor> guard(this);
    drain();
    if (!guard || !m_active || !m_reply || generation != m_generation) return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_reply->error() != QNetworkReply::NoError && (status == 0 || (status >= 200 && status < 400))) {
      UploadOutcome out; out.code = QStringLiteral("network-error"); out.errorMessage = m_reply->errorString();
      attemptFinished(out, transientNetworkError(m_reply->error())); return;
    }
    attemptFinished(parseUploadResponse(m_request.destination, status, m_reply->rawHeaderPairs(), m_body, m_request.fileName), status >= 500 && status <= 599);
  });
  m_timer.start(qMax(1, m_timeoutMs));
  emit progress(0, request.body.size());
}
void UploadExecutor::drain() {
  if (!m_active || !m_reply) return;
  m_body += m_reply->read(kMaximumUploadResponseBytes + 1 - m_body.size());
  if (m_body.size() > kMaximumUploadResponseBytes) {
    UploadOutcome out; out.code = QStringLiteral("response-too-large"); complete(out);
  }
}
void UploadExecutor::cancel() {
  if (!m_active || m_finishing) return;
  UploadOutcome out; out.code = QStringLiteral("cancelled"); complete(out);
}
bool UploadExecutor::retrySafe() const {
  const auto method = m_request.method;
  if (method == "GET" || method == "HEAD" || method == "PUT" || method == "DELETE") return true;
  if (method != "POST" && method != "PATCH") return false;
  int keys = 0;
  for (const auto &header : m_request.headers) {
    if (header.name.compare(QLatin1String("Idempotency-Key"), Qt::CaseInsensitive) != 0) continue;
    if (header.value.trimmed().isEmpty()) return false;
    ++keys;
  }
  return keys == 1;
}
void UploadExecutor::releaseReply() {
  m_timer.stop();
  auto reply = m_reply; m_reply.clear();
  if (reply) { reply->disconnect(this); if (!reply->isFinished()) reply->abort(); if (reply) reply->deleteLater(); }
}
void UploadExecutor::attemptFinished(UploadOutcome out, bool transient) {
  if (!m_active) return;
  if (!transient || !retrySafe() || m_attempts >= kMaximumUploadAttempts) { complete(out); return; }
  const QPointer<UploadExecutor> guard(this);
  const auto generation = m_generation;
  releaseReply();
  if (!guard || !m_active || generation != m_generation) return;
  m_body.clear();
  // The prepared body, destination and key stay frozen across all attempts.
  const int backoff = kInitialUploadBackoffMs * (1 << (m_attempts - 1));
  m_retryTimer.start(backoff);
  emit retryScheduled(m_attempts + 1, backoff);
}
void UploadExecutor::complete(UploadOutcome out) {
  if (!m_active || m_finishing) return;
  const QPointer<UploadExecutor> guard(this);
  m_finishing = true; m_retryTimer.stop(); releaseReply();
  if (!guard) return;
  out.attempts = m_attempts;
  for (const auto &secret : m_request.sensitiveValues) {
    out.errorMessage.replace(secret, QStringLiteral("[redacted]"));
    out.errorMessage.replace(QString::fromLatin1(QUrl::toPercentEncoding(secret)), QStringLiteral("[redacted]"));
  }
  m_body.clear(); m_request = {};
  m_active = false; m_finishing = false;
  emit finished(out);
}
}
