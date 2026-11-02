// EC-003 network worker: one bounded, cancellable image download. Every policy
// decision (scheme, redirects, limits) comes from xerahs_core ImageUrl.
#pragma once
#include "image-editor/ImageUrl.h"
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSslCertificate>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace xerahs::app {

static_assert(xerahs::editor::kMaxDownloadBytes == 1073741824, "download cap must equal RasterSource kMaxEncodedImageBytes");

struct UrlDownloadResult {
  QByteArray bytes;
  QString diagnostic;  // empty on success, otherwise a stable urlDiagnostic
  QUrl finalUrl;       // after redirects; never shown with its query
  int redirects = 0;
};

// Exactly one finished() per start(). Bytes are held in memory only and are
// never written to disk by this class. Destroying or cancelling aborts the
// transfer; cancel() reports "url-cancelled".
class UrlDownload final : public QObject {
  Q_OBJECT
public:
  explicit UrlDownload(QNetworkAccessManager *manager, QObject *parent = nullptr);
  ~UrlDownload() override;
  // `url` must already have passed validateImageUrl with the same opt-in.
  void start(const QUrl &url, bool allowPlainHttp, xerahs::editor::DownloadLimits limits = {});
  void cancel();
  // TEST SEAM ONLY: extra trusted CAs added to the default TLS configuration.
  // Production never calls this; verification stays on and unchanged.
  void setExtraTrustedCas(const QList<QSslCertificate> &cas) { m_extraCas = cas; }
  bool running() const { return m_running; }
signals:
  void progress(qint64 received, qint64 total);
  void finished(const xerahs::app::UrlDownloadResult &result);

private:
  void request(const QUrl &url);
  void drain();
  void onFinished();
  void complete(const QString &diagnostic);
  void abortWith(const QString &diagnostic);

  QNetworkAccessManager *m_manager;
  QNetworkReply *m_reply = nullptr;
  xerahs::editor::DownloadLimits m_limits;
  bool m_allowPlainHttp = false, m_running = false;
  QString m_abortReason;
  UrlDownloadResult m_result;
  QUrl m_current;
  QList<QSslCertificate> m_extraCas;
};
}  // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::app::UrlDownloadResult)
