// EC-003: policy for opening an image from a URL. Qt Core only; the native
// network worker and dialog (app/UrlDownload, app/UrlOpenDialog) enforce these
// decisions and never make their own.
#pragma once
#include <QString>
#include <QUrl>
#include <optional>

namespace xerahs::editor {

namespace urlDiagnostic {
inline constexpr char16_t empty[] = u"url-empty";
inline constexpr char16_t invalid[] = u"url-invalid";
inline constexpr char16_t tooLong[] = u"url-too-long";
inline constexpr char16_t scheme[] = u"url-scheme-unsupported";
inline constexpr char16_t insecure[] = u"url-insecure";
inline constexpr char16_t userInfo[] = u"url-userinfo";
inline constexpr char16_t noHost[] = u"url-host-empty";
inline constexpr char16_t tooManyRedirects[] = u"url-too-many-redirects";
inline constexpr char16_t downgrade[] = u"url-redirect-downgrade";
inline constexpr char16_t badRedirect[] = u"url-redirect-invalid";
inline constexpr char16_t httpStatus[] = u"url-http-status";
inline constexpr char16_t tooLarge[] = u"url-too-large";
inline constexpr char16_t timeout[] = u"url-timeout";
inline constexpr char16_t network[] = u"url-network-failed";
inline constexpr char16_t cancelled[] = u"url-cancelled";
inline constexpr char16_t emptyResponse[] = u"url-empty-response";
}  // namespace urlDiagnostic

// Equals the encoded-image ceiling of the shared RasterSource decoder
// (app/RasterSource.h kMaxEncodedImageBytes); the app asserts the equality.
inline constexpr qint64 kMaxDownloadBytes = 1073741824;
inline constexpr int kMaxUrlLength = 2048;
inline constexpr int kMaxRedirects = 5;
inline constexpr int kTransferTimeoutMs = 30000;

struct DownloadLimits {
  qint64 maxBytes = kMaxDownloadBytes;
  int maxRedirects = kMaxRedirects;
  int timeoutMs = kTransferTimeoutMs;  // no-progress timeout
};

struct ImageUrlResult {
  std::optional<QUrl> url;  // normalized, fragment removed
  QString error;            // stable urlDiagnostic when `url` is empty
};

// HTTPS only. Plain http:// is accepted only when the caller passes the
// per-request opt-in. Other schemes, credentials in the URL and empty hosts are
// always refused.
ImageUrlResult validateImageUrl(const QString &text, bool allowPlainHttp);

struct RedirectDecision {
  std::optional<QUrl> target;  // set when the redirect may be followed
  QString error;
};
// `redirectsSoFar` counts redirects already followed. A relative Location is
// resolved against `current`. https -> http is never allowed, even with opt-in.
RedirectDecision decideRedirect(const QUrl &current, const QString &location, int redirectsSoFar, bool allowPlainHttp,
                                int maxRedirects = kMaxRedirects);

// "host" or "host:port": never the path, query, fragment or credentials.
QString sourceLabel(const QUrl &url);

// User-facing text for a urlDiagnostic. Contains no URL, query or server data.
QString describeUrlDiagnostic(const QString &diagnostic);

}  // namespace xerahs::editor
