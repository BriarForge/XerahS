// CORE-021, SETCAT-014, SETCAT-016: versioned custom HTTP destination.
// Request building is intentionally not in this type.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>

namespace xerahs::uploads {

inline constexpr int kDestinationSchemaVersion = 1;
inline constexpr qsizetype kMaxDestinationBytes = 1024 * 1024;

enum class RequestMethod { Get, Post, Put, Patch, Delete, Unknown };
enum class BodyType { None, MultipartFormData, FormUrlEncoded, Json, Binary, Unknown };

struct Field {
  QString name;
  QString value;
  bool operator==(const Field &other) const { return name == other.name && value == other.value; }
};

// rawMethod / rawBody preserve an unrecognised token so a document can
// round-trip. validateDestination reports that token instead of sending it.
struct UploadDestination {
  QUuid id;
  QString name;
  int schemaVersion = kDestinationSchemaVersion;
  RequestMethod method = RequestMethod::Post;
  QString rawMethod;
  QString requestUrl;
  QVector<Field> query;
  QVector<Field> headers;
  BodyType body = BodyType::None;
  QString rawBody;
  QVector<Field> arguments;
  QString data;
  QString fileFormName;
  QString resultUrl;
  QString thumbnailUrl;
  QString deletionUrl;
  QString errorMessage;
  QJsonObject extra;
};

QString requestMethodString(RequestMethod method);
QString bodyTypeString(BodyType body);

// Structural read. Semantic problems are left for validateDestination.
// Codes: invalid-json, too-large, unsupported-schema, invalid-document.
struct DestinationRead {
  std::optional<UploadDestination> destination;
  QString code;
};

DestinationRead readDestination(const QByteArray &bytes);
QByteArray writeDestination(const UploadDestination &destination);

// Stable codes, in field order: empty-id, empty-name, unsupported-method,
// empty-url, invalid-url, non-http-scheme, url-userinfo, unsupported-body,
// missing-file-form-name.
QStringList validateDestination(const UploadDestination &destination);

}  // namespace xerahs::uploads
