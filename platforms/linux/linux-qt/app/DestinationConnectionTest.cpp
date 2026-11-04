#include "DestinationConnectionTest.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
namespace xerahs::app {
DestinationConnectionTest::DestinationConnectionTest(QNetworkAccessManager *manager, QObject *parent, int timeoutMs)
    : QObject(parent), m_manager(manager ? manager : new QNetworkAccessManager(this)), m_timeoutMs(timeoutMs) {
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, [this] { complete("connection-test-timeout"); });
}
DestinationConnectionTest::~DestinationConnectionTest() {
  if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); }
}
void DestinationConnectionTest::start(const uploads::PreparedUploadRequest &request) {
  if (m_active) return;
  m_active = true;
  if (!m_manager) { complete("connection-test-unavailable"); return; }
  QNetworkRequest network(request.url);
  network.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  network.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
  network.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
  network.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
  for (const auto &field : request.headers) network.setRawHeader(field.name.toUtf8(), field.value.toUtf8());
  m_reply = m_manager->head(network);
  m_reply->setReadBufferSize(4096);
  connect(m_reply, &QIODevice::readyRead, this, [this] { if (m_reply) m_reply->readAll(); });
  const auto finish = [this] {
    if (!m_active || !m_reply) return;
    const auto status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) complete("connection-test-redirect-refused");
    else if (status == 405 || status == 501) complete("connection-test-unsupported");
    else if (status >= 400) complete("connection-test-http-failed");
    else if (m_reply->error() != QNetworkReply::NoError) complete("connection-test-network-failed");
    else complete(status >= 200 && status < 300 ? "connection-test-succeeded" : "connection-test-http-failed");
  };
  connect(m_reply, &QNetworkReply::finished, this, finish);
  connect(m_reply, &QObject::destroyed, this, [this] { if (m_active) complete("connection-test-network-failed"); });
  m_timer.start(qMax(1, m_timeoutMs));
  if (m_reply->isFinished()) QTimer::singleShot(0, this, finish);
}
void DestinationConnectionTest::cancel() { complete("connection-test-cancelled"); }
void DestinationConnectionTest::complete(const QString &code) {
  if (!m_active) return;
  m_active = false; m_timer.stop();
  auto reply = m_reply; m_reply.clear();
  if (reply) { reply->disconnect(this); if (!reply->isFinished()) reply->abort(); reply->deleteLater(); }
  emit finished(code);
}
}
