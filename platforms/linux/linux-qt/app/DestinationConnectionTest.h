#pragma once
#include "uploads/UploadRequest.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
class QNetworkAccessManager;
class QNetworkReply;
namespace xerahs::app {
// A single explicit HEAD probe. No redirects, cookies, content or response text
// are exposed; completion contains a fixed, value-free code only.
class DestinationConnectionTest final : public QObject {
  Q_OBJECT
public:
  DestinationConnectionTest(QNetworkAccessManager *manager = nullptr, QObject *parent = nullptr, int timeoutMs = 15000);
  ~DestinationConnectionTest() override;
  void start(const uploads::PreparedUploadRequest &);
  void cancel();
signals:
  void finished(const QString &code);
private:
  void complete(const QString &code);
  QPointer<QNetworkAccessManager> m_manager;
  QPointer<QNetworkReply> m_reply;
  QTimer m_timer;
  int m_timeoutMs;
  bool m_active = false;
};
}
