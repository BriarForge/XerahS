#include "ImageUrl.h"

namespace xerahs::editor {
namespace {
bool hasControl(const QString &text) {
  for (const QChar c : text) if (c.unicode() < 0x20 || c.unicode() == 0x7f || c.isSpace()) return true;
  return false;
}
ImageUrlResult fail(const char16_t *error) { return {std::nullopt, QString::fromUtf16(error)}; }
// Shared by the initial URL and every redirect target.
ImageUrlResult check(QUrl url, bool allowPlainHttp) {
  if (!url.isValid() || url.isRelative()) return fail(urlDiagnostic::invalid);
  const QString scheme = url.scheme().toLower();
  if (scheme != u"https" && scheme != u"http") return fail(urlDiagnostic::scheme);
  if (!url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@'))) return fail(urlDiagnostic::userInfo);
  if (url.host().isEmpty()) return fail(urlDiagnostic::noHost);
  if (scheme == u"http" && !allowPlainHttp) return fail(urlDiagnostic::insecure);
  if (url.port() == 0) return fail(urlDiagnostic::invalid);
  url.setScheme(scheme);
  url.setFragment({});
  return {url, {}};
}
}  // namespace

ImageUrlResult validateImageUrl(const QString &text, bool allowPlainHttp) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) return fail(urlDiagnostic::empty);
  if (trimmed.size() > kMaxUrlLength) return fail(urlDiagnostic::tooLong);
  if (hasControl(trimmed)) return fail(urlDiagnostic::invalid);
  return check(QUrl(trimmed, QUrl::StrictMode), allowPlainHttp);
}

RedirectDecision decideRedirect(const QUrl &current, const QString &location, int redirectsSoFar, bool allowPlainHttp, int maxRedirects) {
  if (redirectsSoFar >= maxRedirects) return {std::nullopt, QString::fromUtf16(urlDiagnostic::tooManyRedirects)};
  const QString trimmed = location.trimmed();
  if (trimmed.isEmpty() || trimmed.size() > kMaxUrlLength || hasControl(trimmed))
    return {std::nullopt, QString::fromUtf16(urlDiagnostic::badRedirect)};
  const QUrl target = current.resolved(QUrl(trimmed, QUrl::StrictMode));
  if (current.scheme().toLower() == u"https" && target.scheme().toLower() == u"http")
    return {std::nullopt, QString::fromUtf16(urlDiagnostic::downgrade)};
  const auto checked = check(target, allowPlainHttp);
  if (!checked.url) {
    // A redirect cannot name a different failure than "not an acceptable target".
    const bool insecure = checked.error == QString::fromUtf16(urlDiagnostic::insecure);
    return {std::nullopt, insecure ? QString::fromUtf16(urlDiagnostic::downgrade) : QString::fromUtf16(urlDiagnostic::badRedirect)};
  }
  return {checked.url, {}};
}

QString sourceLabel(const QUrl &url) {
  QString host = url.host();
  if (host.contains(QLatin1Char(':'))) host = QStringLiteral("[%1]").arg(host);
  return url.port() > 0 ? QStringLiteral("%1:%2").arg(host).arg(url.port()) : host;
}

QString describeUrlDiagnostic(const QString &d) {
  const auto is = [&](const char16_t *v) { return d == QString::fromUtf16(v); };
  if (is(urlDiagnostic::empty)) return QStringLiteral("Enter an https:// address.");
  if (is(urlDiagnostic::tooLong)) return QStringLiteral("The address is too long.");
  if (is(urlDiagnostic::scheme)) return QStringLiteral("Only https:// addresses can be opened.");
  if (is(urlDiagnostic::insecure)) return QStringLiteral("Unencrypted http:// is refused. Use https://, or tick the option below for this request only.");
  if (is(urlDiagnostic::userInfo)) return QStringLiteral("Addresses containing a user name or password are refused.");
  if (is(urlDiagnostic::noHost)) return QStringLiteral("The address has no host name.");
  if (is(urlDiagnostic::tooManyRedirects)) return QStringLiteral("The server redirected too many times. Nothing was opened.");
  if (is(urlDiagnostic::downgrade)) return QStringLiteral("The server tried to redirect to an unencrypted address. Nothing was opened.");
  if (is(urlDiagnostic::badRedirect)) return QStringLiteral("The server redirected to an address that cannot be opened. Nothing was opened.");
  if (is(urlDiagnostic::httpStatus)) return QStringLiteral("The server did not return an image (it reported an error).");
  if (is(urlDiagnostic::tooLarge)) return QStringLiteral("The download is larger than the size limit. Nothing was opened.");
  if (is(urlDiagnostic::timeout)) return QStringLiteral("The server stopped responding. Nothing was opened.");
  if (is(urlDiagnostic::network)) return QStringLiteral("The download failed. Check the address and your connection.");
  if (is(urlDiagnostic::cancelled)) return QStringLiteral("The download was cancelled. Nothing was opened.");
  if (is(urlDiagnostic::emptyResponse)) return QStringLiteral("The server returned no data.");
  if (is(urlDiagnostic::invalid)) return QStringLiteral("This is not a valid web address.");
  return QStringLiteral("The URL could not be opened.");
}
}  // namespace xerahs::editor
