// Frozen desktop snapshot through org.freedesktop.portal.Screenshot (RC-001,
// RC-002, RC-014). The portal owns consent: a denial is reported once and is
// never re-prompted by this client.
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QDBusPendingCallWatcher;

namespace xerahs::app {

class PortalScreenshot final : public QObject {
  Q_OBJECT

public:
  enum class Outcome { Captured, Denied, Failed };

  explicit PortalScreenshot(QObject *parent = nullptr);

  // Starts one non-interactive screenshot request. Exactly one finished()
  // follows.
  void request();

signals:
  void finished(xerahs::app::PortalScreenshot::Outcome outcome, const QImage &image, const QString &detail);

private slots:
  void onResponse(uint response, const QVariantMap &results);

private:
  void fail(Outcome outcome, const QString &detail);

  QString m_requestPath;
  bool m_done = false;
};

}  // namespace xerahs::app
