// CORE-020, CORE-021, COMPCAT-011.
#include "UploadResponse.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace xerahs::uploads {
namespace {
QString jsonPath(const QByteArray &body, const QString &path) {
  const auto doc = QJsonDocument::fromJson(body);
  QJsonValue value = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());
  static const QRegularExpression step(QStringLiteral(R"((?:^|\.)([^.\[\]]+)|\[(\d+)\])"));
  qsizetype end = 0;
  auto matches = step.globalMatch(path);
  while (matches.hasNext()) {
    const auto m = matches.next();
    if (m.capturedStart() != end) return {};
    if (m.captured(2).isEmpty()) value = value.toObject().value(m.captured(1));
    else {
      bool ok = false;
      const int index = m.captured(2).toInt(&ok);
      const auto array = value.toArray();
      if (!ok || index >= array.size()) return {};
      value = array.at(index);
    }
    end = m.capturedEnd();
  }
  if (end != path.size() || end == 0) return {};
  if (value.isString()) return value.toString();
  if (value.isDouble() || value.isBool()) {
    const auto json = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
  }
  return {};
}
QString xmlPath(const QByteArray &body, const QString &path) {
  if (!path.startsWith(u'/')) return {};
  QXmlStreamReader xml(body);
  QStringList stack;
  QString result;
  bool found = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isDTD() || xml.isEntityReference()) return {};
    if (xml.isStartElement()) {
      stack.append(xml.qualifiedName().toString());
      if (!found && u'/' + stack.join(u'/') == path) {
        result = xml.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement);
        stack.removeLast(); found = true;
      }
    } else if (xml.isEndElement() && !stack.isEmpty()) stack.removeLast();
  }
  return xml.hasError() ? QString() : result;
}
bool webUrl(const QString &s) {
  const QUrl url(s, QUrl::StrictMode);
  return url.isValid() && !url.host().isEmpty() &&
      (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}
}
UploadOutcome parseUploadResponse(const UploadDestination &d, int status, const UploadHeaders &headers,
                                  const QByteArray &body, const QString &fileName) {
  const auto response = QString::fromUtf8(body);
  auto expand = [&](const QString &text) {
    // A regex ends at its numeric group suffix, allowing braces in patterns.
    static const QRegularExpression token(QStringLiteral(R"(\{(response|filename|json:[^{}]*|xml:[^{}]*|header:[^{}]*|regex:[\s\S]*?\|\d+)\})"));
    QString out;
    qsizetype end = 0;
    auto matches = token.globalMatch(text);
    while (matches.hasNext()) {
      const auto m = matches.next();
      out += text.mid(end, m.capturedStart() - end);
      const auto key = m.captured(1);
      if (key == QLatin1String("response")) out += response;
      else if (key == QLatin1String("filename")) out += fileName;
      else if (key.startsWith(QLatin1String("json:"))) out += jsonPath(body, key.mid(5));
      else if (key.startsWith(QLatin1String("xml:"))) out += xmlPath(body, key.mid(4));
      else if (key.startsWith(QLatin1String("header:"))) {
        for (const auto &h : headers) if (QString::fromLatin1(h.first).compare(key.mid(7), Qt::CaseInsensitive) == 0) { out += QString::fromUtf8(h.second); break; }
      } else {
        const auto payload = key.mid(6);
        const auto separator = payload.lastIndexOf(u'|');
        bool ok = false;
        const int group = payload.mid(separator + 1).toInt(&ok);
        if (separator >= 0 && ok && group >= 0) {
          const QRegularExpression re(payload.left(separator));
          if (re.isValid()) out += re.match(response).captured(group);
        }
      }
      end = m.capturedEnd();
    }
    return out + text.mid(end);
  };
  UploadOutcome result;
  result.httpStatus = status;
  result.errorMessage = expand(d.errorMessage).trimmed();
  if (status < 200 || status >= 300) {
    result.code = QStringLiteral("http-error");
    result.errorMessage = QStringLiteral("HTTP %1: %2").arg(status).arg(result.errorMessage.isEmpty() ? response.left(200) : result.errorMessage);
    return result;
  }
  result.url = (d.resultUrl.isEmpty() ? response : expand(d.resultUrl)).trimmed();
  result.thumbnailUrl = expand(d.thumbnailUrl).trimmed();
  result.deletionUrl = expand(d.deletionUrl).trimmed();
  if (result.url.isEmpty()) result.code = QStringLiteral("parse-failed");
  else if (!webUrl(result.url) || (!result.thumbnailUrl.isEmpty() && !webUrl(result.thumbnailUrl)) ||
           (!result.deletionUrl.isEmpty() && !webUrl(result.deletionUrl))) result.code = QStringLiteral("invalid-result-url");
  return result;
}
}
