// CORE-013, CORE-020, CORE-021, COMPCAT-011.
#include "UploadRequest.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <random>

namespace xerahs::uploads {
namespace {
QString jsonEscape(const QString &value) {
  const auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
  return QString::fromUtf8(bytes.mid(2, bytes.size() - 4));
}
bool controls(const QString &s) {
  for (auto c : s) if (c.unicode() < 32 || c.unicode() == 127) return true;
  return false;
}
QByteArray quoted(QString s) {
  s.replace(u'\\', QStringLiteral("\\\\"));
  s.replace(u'"', QStringLiteral("\\\""));
  s.remove(u'\r'); s.remove(u'\n');
  return s.toUtf8();
}
QByteArray encoded(const QString &s) { return QUrl::toPercentEncoding(s); }
}
UploadRequestResult buildUploadRequest(const UploadDestination &d, const UploadInput &input,
                                       const CredentialStore &store, const UploadRequestOptions &options) {
  // Strict QUrl rejects literal template braces. Mask recognized tokens
  // before structural validation, then validate the expanded target again.
  auto structural = d;
  static const QRegularExpression urlToken(QStringLiteral(R"(\{(?:filename|random:[^{}]*|base64:[^{}]*|secret:[^{}]*)\})"));
  auto urlMatches = urlToken.globalMatch(d.requestUrl);
  qsizetype offset = 0;
  while (urlMatches.hasNext()) {
    const auto m = urlMatches.next();
    const auto replacement = QStringLiteral("0");
    structural.requestUrl.replace(m.capturedStart() + offset, m.capturedLength(), replacement);
    offset += replacement.size() - m.capturedLength();
  }
  const auto errors = validateDestination(structural);
  if (!errors.isEmpty()) return {{}, errors.first(), {}};
  QByteArray bytes = input.fileBytes;
  if (options.maxInputBytes < 0 || bytes.size() > options.maxInputBytes)
    return {{}, QStringLiteral("input-too-large"), {}};
  if (!input.filePath.isEmpty()) {
    QFile file(input.filePath);
    if (!file.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("input-unreadable"), {}};
    if (file.size() > options.maxInputBytes) return {{}, QStringLiteral("input-too-large"), {}};
    bytes = file.read(options.maxInputBytes + 1);
    if (bytes.size() > options.maxInputBytes) return {{}, QStringLiteral("input-too-large"), {}};
    if (file.error() != QFileDevice::NoError) return {{}, QStringLiteral("input-unreadable"), {}};
  }
  std::mt19937 random(options.randomSeed ? *options.randomSeed : QRandomGenerator::global()->generate());
  QString code, message;
  QStringList sensitiveValues;
  auto expand = [&](const QString &text, bool json = false, bool url = false) {
    static const QRegularExpression token(QStringLiteral(R"(\{(filename|random:[^{}]*|base64:[^{}]*|secret:[^{}]*)\})"));
    QString out;
    qsizetype last = 0;
    auto matches = token.globalMatch(text);
    while (matches.hasNext()) {
      const auto m = matches.next();
      out += text.mid(last, m.capturedStart() - last);
      const auto key = m.captured(1);
      QString value;
      if (key == QLatin1String("filename")) value = input.fileName;
      else if (key.startsWith(QLatin1String("random:"))) {
        const auto choices = key.mid(7).split(u'|');
        value = choices.at(std::uniform_int_distribution<int>(0, choices.size() - 1)(random));
      } else if (key.startsWith(QLatin1String("base64:"))) value = QString::fromLatin1(key.mid(7).toUtf8().toBase64());
      else {
        const auto name = key.mid(7);
        const auto secret = store.lookup(d.id, name);
        if (!secret.ok() && code.isEmpty()) {
          code = secret.status == CredentialStatus::NotFound ? QStringLiteral("missing-secret") : QStringLiteral("credential-unavailable");
          message = name;
        }
        if (secret.ok()) {
          value = secret.value;
          if (!value.isEmpty()) sensitiveValues.append(value);
        }
      }
      out += json ? jsonEscape(value) : url ? QString::fromLatin1(encoded(value)) : value;
      last = m.capturedEnd();
    }
    return out + text.mid(last);
  };
  PreparedUploadRequest r;
  r.method = requestMethodString(d.method).toLatin1();
  r.url = QUrl(expand(d.requestUrl, false, true), QUrl::StrictMode);
  if (!code.isEmpty()) return {{}, code, message};
  auto checked = d;
  checked.requestUrl = r.url.toString(QUrl::FullyEncoded);
  const auto expandedErrors = validateDestination(checked);
  if (!expandedErrors.isEmpty()) return {{}, expandedErrors.first(), {}};
  // Encode each query component before merging, including literal '+' and '&'.
  QByteArray query = r.url.query(QUrl::FullyEncoded).toUtf8();
  for (const auto &field : d.query) {
    if (!query.isEmpty()) query += '&';
    query += encoded(field.name) + '=' + encoded(expand(field.value));
  }
  r.url.setQuery(QString::fromUtf8(query));
  for (const auto &field : d.headers) {
    const auto value = expand(field.value);
    static const QRegularExpression name(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
    if (!name.match(field.name).hasMatch() || controls(value)) return {{}, QStringLiteral("invalid-header"), {}};
    if (field.name.compare(QLatin1String("Content-Length"), Qt::CaseInsensitive) == 0 ||
        field.name.compare(QLatin1String("Transfer-Encoding"), Qt::CaseInsensitive) == 0 ||
        field.name.compare(QLatin1String("Host"), Qt::CaseInsensitive) == 0)
      return {{}, QStringLiteral("invalid-header"), {}};
    r.headers.append({field.name, value});
  }
  const auto mime = input.mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : input.mimeType;
  if (controls(mime)) return {{}, QStringLiteral("invalid-content-type"), {}};
  if (d.body == BodyType::MultipartFormData) {
    const auto boundary = QByteArray("xerahs-") + QUuid::createUuid().toByteArray(QUuid::Id128);
    r.contentType = "multipart/form-data; boundary=" + boundary;
    for (const auto &field : d.arguments)
      r.body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + quoted(field.name) + "\"\r\n\r\n" + expand(field.value).toUtf8() + "\r\n";
    r.body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"" + quoted(d.fileFormName) + "\"; filename=\"" + quoted(input.fileName) + "\"\r\nContent-Type: " + mime.toUtf8() + "\r\n\r\n" + bytes + "\r\n--" + boundary + "--\r\n";
  } else if (d.body == BodyType::FormUrlEncoded) {
    r.contentType = "application/x-www-form-urlencoded";
    for (const auto &field : d.arguments) {
      if (!r.body.isEmpty()) r.body += '&';
      r.body += encoded(field.name) + '=' + encoded(expand(field.value));
    }
  } else if (d.body == BodyType::Json) {
    r.contentType = "application/json";
    r.body = expand(d.data, true).toUtf8();
  } else if (d.body == BodyType::Binary) {
    r.contentType = mime.toUtf8(); r.body = bytes;
  }
  if (!code.isEmpty()) return {{}, code, message};
  r.destination = d; r.fileName = input.fileName; r.sensitiveValues = sensitiveValues;
  return {r, {}, {}};
}
}  // namespace xerahs::uploads
