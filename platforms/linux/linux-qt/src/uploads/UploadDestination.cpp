#include "UploadDestination.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace xerahs::uploads {
namespace {

constexpr int kMaxFields = 256;
constexpr int kMaxName = 1024;
constexpr int kMaxValue = 16384;
constexpr int kMaxData = 256 * 1024;

bool within(const QString &text, int limit) { return text.size() <= limit; }

QJsonArray fieldsJson(const QVector<Field> &fields) {
  QJsonArray array;
  for (const auto &field : fields) array.append(QJsonObject{{"name", field.name}, {"value", field.value}});
  return array;
}

bool takeFields(QJsonObject *object, const char *key, QVector<Field> *fields) {
  const auto value = object->value(QLatin1String(key));
  if (value.isUndefined()) return true;
  object->remove(QLatin1String(key));
  if (!value.isArray()) return false;
  const auto array = value.toArray();
  if (array.size() > kMaxFields) return false;
  for (const auto &item : array) {
    if (!item.isObject()) return false;
    const auto entry = item.toObject();
    if (!entry.value(QStringLiteral("name")).isString() || !entry.value(QStringLiteral("value")).isString()) return false;
    Field field{entry.value(QStringLiteral("name")).toString(), entry.value(QStringLiteral("value")).toString()};
    if (!within(field.name, kMaxName) || !within(field.value, kMaxValue)) return false;
    fields->append(std::move(field));
  }
  return true;
}

bool takeString(QJsonObject *object, const char *key, QString *out, bool required, int limit) {
  const auto value = object->value(QLatin1String(key));
  if (value.isUndefined()) return !required;
  object->remove(QLatin1String(key));
  if (!value.isString()) return false;
  *out = value.toString();
  return within(*out, limit);
}

RequestMethod parseMethod(const QString &text, QString *raw) {
  const auto name = text.trimmed().toUpper();
  if (name == QLatin1String("GET")) return RequestMethod::Get;
  if (name == QLatin1String("POST")) return RequestMethod::Post;
  if (name == QLatin1String("PUT")) return RequestMethod::Put;
  if (name == QLatin1String("PATCH")) return RequestMethod::Patch;
  if (name == QLatin1String("DELETE")) return RequestMethod::Delete;
  *raw = text;
  return RequestMethod::Unknown;
}

BodyType parseBody(const QString &text, QString *raw) {
  const auto name = text.trimmed();
  if (name.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0) return BodyType::None;
  if (name.compare(QLatin1String("MultipartFormData"), Qt::CaseInsensitive) == 0) return BodyType::MultipartFormData;
  if (name.compare(QLatin1String("FormURLEncoded"), Qt::CaseInsensitive) == 0) return BodyType::FormUrlEncoded;
  if (name.compare(QLatin1String("JSON"), Qt::CaseInsensitive) == 0) return BodyType::Json;
  if (name.compare(QLatin1String("Binary"), Qt::CaseInsensitive) == 0) return BodyType::Binary;
  *raw = text;
  return BodyType::Unknown;
}

bool hasControl(const QString &text) {
  for (const auto c : text)
    if (c.unicode() < 32 || c.unicode() == 127) return true;
  return false;
}

}  // namespace

QString requestMethodString(RequestMethod method) {
  switch (method) {
    case RequestMethod::Get: return QStringLiteral("GET");
    case RequestMethod::Post: return QStringLiteral("POST");
    case RequestMethod::Put: return QStringLiteral("PUT");
    case RequestMethod::Patch: return QStringLiteral("PATCH");
    case RequestMethod::Delete: return QStringLiteral("DELETE");
    case RequestMethod::Unknown: return {};
  }
  return {};
}

QString bodyTypeString(BodyType body) {
  switch (body) {
    case BodyType::None: return QStringLiteral("None");
    case BodyType::MultipartFormData: return QStringLiteral("MultipartFormData");
    case BodyType::FormUrlEncoded: return QStringLiteral("FormURLEncoded");
    case BodyType::Json: return QStringLiteral("JSON");
    case BodyType::Binary: return QStringLiteral("Binary");
    case BodyType::Unknown: return {};
  }
  return {};
}

DestinationRead readDestination(const QByteArray &bytes) {
  if (bytes.size() > kMaxDestinationBytes) return {{}, QStringLiteral("too-large")};
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return {{}, QStringLiteral("invalid-json")};
  auto root = document.object();
  const auto version = root.value(QStringLiteral("schemaVersion"));
  if (!version.isDouble() || version.toInt() != kDestinationSchemaVersion || version.toDouble() != kDestinationSchemaVersion) {
    return {{}, QStringLiteral("unsupported-schema")};
  }
  root.remove(QStringLiteral("schemaVersion"));

  UploadDestination destination;
  destination.schemaVersion = kDestinationSchemaVersion;
  QString id;
  QString method;
  QString body;
  const bool fields =
      takeString(&root, "id", &id, true, 64) && takeString(&root, "name", &destination.name, true, kMaxName) &&
      takeString(&root, "method", &method, true, 64) && takeString(&root, "url", &destination.requestUrl, true, kMaxValue) &&
      takeString(&root, "body", &body, true, 64) && takeString(&root, "data", &destination.data, false, kMaxData) &&
      takeString(&root, "fileFormName", &destination.fileFormName, false, kMaxName) &&
      takeString(&root, "resultUrl", &destination.resultUrl, false, kMaxValue) &&
      takeString(&root, "thumbnailUrl", &destination.thumbnailUrl, false, kMaxValue) &&
      takeString(&root, "deletionUrl", &destination.deletionUrl, false, kMaxValue) &&
      takeString(&root, "errorMessage", &destination.errorMessage, false, kMaxValue) &&
      takeFields(&root, "query", &destination.query) && takeFields(&root, "headers", &destination.headers) &&
      takeFields(&root, "arguments", &destination.arguments);
  if (!fields) return {{}, QStringLiteral("invalid-document")};
  destination.id = QUuid(id);
  if (destination.id.isNull() || destination.id.toString(QUuid::WithoutBraces).compare(id, Qt::CaseInsensitive) != 0) {
    return {{}, QStringLiteral("invalid-document")};
  }
  destination.method = parseMethod(method, &destination.rawMethod);
  destination.body = parseBody(body, &destination.rawBody);
  destination.extra = root;
  return {destination, {}};
}

QByteArray writeDestination(const UploadDestination &destination) {
  auto object = destination.extra;
  object.insert(QStringLiteral("schemaVersion"), destination.schemaVersion);
  object.insert(QStringLiteral("id"), destination.id.toString(QUuid::WithoutBraces));
  object.insert(QStringLiteral("name"), destination.name);
  object.insert(QStringLiteral("method"),
                destination.method == RequestMethod::Unknown ? destination.rawMethod : requestMethodString(destination.method));
  object.insert(QStringLiteral("url"), destination.requestUrl);
  object.insert(QStringLiteral("query"), fieldsJson(destination.query));
  object.insert(QStringLiteral("headers"), fieldsJson(destination.headers));
  object.insert(QStringLiteral("body"), destination.body == BodyType::Unknown ? destination.rawBody : bodyTypeString(destination.body));
  object.insert(QStringLiteral("arguments"), fieldsJson(destination.arguments));
  object.insert(QStringLiteral("data"), destination.data);
  object.insert(QStringLiteral("fileFormName"), destination.fileFormName);
  object.insert(QStringLiteral("resultUrl"), destination.resultUrl);
  object.insert(QStringLiteral("thumbnailUrl"), destination.thumbnailUrl);
  object.insert(QStringLiteral("deletionUrl"), destination.deletionUrl);
  object.insert(QStringLiteral("errorMessage"), destination.errorMessage);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QStringList validateDestination(const UploadDestination &destination) {
  QStringList errors;
  if (destination.id.isNull()) errors.append(QStringLiteral("empty-id"));
  if (destination.name.trimmed().isEmpty()) errors.append(QStringLiteral("empty-name"));
  if (destination.method == RequestMethod::Unknown) errors.append(QStringLiteral("unsupported-method"));
  const auto urlText = destination.requestUrl.trimmed();
  if (urlText.isEmpty()) {
    errors.append(QStringLiteral("empty-url"));
  } else if (hasControl(urlText)) {
    errors.append(QStringLiteral("invalid-url"));
  } else {
    const QUrl url(urlText, QUrl::StrictMode);
    const auto scheme = url.scheme().toLower();
    if (!url.isValid() || scheme.isEmpty()) errors.append(QStringLiteral("invalid-url"));
    else if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) errors.append(QStringLiteral("non-http-scheme"));
    else if (url.host().isEmpty()) errors.append(QStringLiteral("invalid-url"));
    else if (!url.userName().isEmpty() || !url.password().isEmpty()) errors.append(QStringLiteral("url-userinfo"));
  }
  if (destination.body == BodyType::Unknown) errors.append(QStringLiteral("unsupported-body"));
  if (destination.body == BodyType::MultipartFormData && destination.fileFormName.trimmed().isEmpty()) {
    errors.append(QStringLiteral("missing-file-form-name"));
  }
  return errors;
}

}  // namespace xerahs::uploads
