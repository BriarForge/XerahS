#include "SxcuImport.h"
#include "storage/JsonBoundary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

namespace xerahs::uploads {
namespace {

constexpr int kMaxDepth = 32;
constexpr int kMaxFields = 256;
constexpr int kMaxName = 1024;
constexpr int kMaxValue = 16384;

struct Bag {
  QVector<Secret> secrets;
  QSet<QString> used;
  QVector<ImportWarning> warnings;
  QSet<QString> seen;
};

void warn(Bag *bag, const QString &code, const QString &detail) {
  const auto key = code + QLatin1Char('\n') + detail;
  if (bag->seen.contains(key)) return;
  bag->seen.insert(key);
  bag->warnings.append({code, detail.left(64)});
}

bool isSecretName(const QString &name) {
  const auto lower = name.toLower();
  static constexpr const char *parts[] = {"apikey", "key", "token", "secret", "auth", "password", "cookie"};
  for (const char *part : parts)
    if (lower.contains(QLatin1String(part))) return true;
  return false;
}

bool unsafeMetadata(const QJsonValue &value, const QString &key = {}) {
  if ((!key.isEmpty() && compatibility::unsafeUnknown(key) && (!isSecretName(key) || key.contains("auth", Qt::CaseInsensitive) || !value.isString())) ||
      key == "Regex" || key == "RegexList") return true;
  if (value.isObject()) {
    const auto o = value.toObject();
    for (auto it = o.begin(); it != o.end(); ++it) if (unsafeMetadata(it.value(), it.key())) return true;
  } else if (value.isArray()) {
    for (const auto &item : value.toArray()) if (unsafeMetadata(item)) return true;
  }
  return false;
}

QString allocate(Bag *bag, const QString &name) {
  QString base;
  base.reserve(name.size());
  for (const auto c : name) {
    if (c == u'{' || c == u'}' || c.unicode() < 32) base.append(u'_');
    else base.append(c);
  }
  if (base.isEmpty()) base = QStringLiteral("secret");
  auto candidate = base;
  for (int n = 2; bag->used.contains(candidate); ++n) candidate = base + QLatin1Char('#') + QString::number(n);
  bag->used.insert(candidate);
  return candidate;
}

void scanSyntax(Bag *bag, const QString &text) {
  for (int i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (c == u'\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    const bool dollar = c == u'$';
    if (!dollar && c != u'{') continue;
    const int begin = i + 1;
    if (begin >= text.size()) {
      warn(bag, QStringLiteral("unsupported-syntax"), QStringLiteral("unterminated"));
      return;
    }
    const QChar first = text.at(begin);
    if (!first.isLetter() && !(dollar && first.isDigit())) continue;
    const int close = text.indexOf(dollar ? u'$' : u'}', begin + 1);
    if (close < 0) {
      warn(bag, QStringLiteral("unsupported-syntax"), QStringLiteral("unterminated"));
      return;
    }
    const auto name = text.mid(begin, close - begin).section(u':', 0, 0);
    i = close;
    if (dollar && !name.isEmpty() && name.at(0).isDigit()) {
      warn(bag, QStringLiteral("unsupported-syntax"), QStringLiteral("regex-group"));
      continue;
    }
    const auto lower = name.toLower();
    if (lower == QLatin1String("json") || lower == QLatin1String("xml") || lower == QLatin1String("input") ||
        lower == QLatin1String("filename") || lower == QLatin1String("response") || lower == QLatin1String("responseurl") ||
        lower == QLatin1String("header") || lower == QLatin1String("secret")) {
      continue;
    }
    warn(bag, QStringLiteral("unsupported-syntax"), lower);
  }
}

void takeSecret(Bag *bag, QString *value, const QString &name) {
  scanSyntax(bag, *value);
  if (!isSecretName(name) || value->isEmpty() || referencedSecret(*value)) return;
  const auto ref = allocate(bag, name);
  bag->secrets.append({ref, *value});
  *value = QStringLiteral("{secret:%1}").arg(ref);
  warn(bag, QStringLiteral("secret-extracted"), ref);
}

QVector<Field> readMap(Bag *bag, const QJsonValue &value, const QString &label) {
  QVector<Field> fields;
  if (value.isUndefined() || value.isNull()) return fields;
  if (!value.isObject()) {
    warn(bag, QStringLiteral("unsupported-value"), label);
    return fields;
  }
  const auto object = value.toObject();
  if (object.size() > kMaxFields) {
    warn(bag, QStringLiteral("too-large"), label);
    return fields;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (!it.value().isString() || it.key().size() > kMaxName || it.value().toString().size() > kMaxValue) {
      warn(bag, it.value().isString() ? QStringLiteral("too-large") : QStringLiteral("unsupported-value"), it.key());
      continue;
    }
    fields.append({it.key(), it.value().toString()});
  }
  return fields;
}

QString readText(Bag *bag, const QJsonValue &value, const QString &label, int limit) {
  if (value.isUndefined() || value.isNull()) return {};
  if (!value.isString() || value.toString().size() > limit) {
    warn(bag, value.isString() ? QStringLiteral("too-large") : QStringLiteral("unsupported-value"), label);
    return {};
  }
  auto text = value.toString();
  scanSyntax(bag, text);
  return text;
}

RequestMethod readMethod(Bag *bag, const QJsonValue &value, QString *raw) {
  if (!value.isString()) {
    if (value.isDouble()) *raw = QString::number(value.toInt());
    warn(bag, QStringLiteral("unsupported-method"), raw->isEmpty() ? QStringLiteral("RequestMethod") : *raw);
    return RequestMethod::Unknown;
  }
  const auto text = value.toString();
  const auto name = text.trimmed().toUpper();
  if (name == QLatin1String("GET")) return RequestMethod::Get;
  if (name == QLatin1String("POST")) return RequestMethod::Post;
  if (name == QLatin1String("PUT")) return RequestMethod::Put;
  if (name == QLatin1String("PATCH")) return RequestMethod::Patch;
  if (name == QLatin1String("DELETE")) return RequestMethod::Delete;
  *raw = text;
  warn(bag, QStringLiteral("unsupported-method"), text.left(64));
  return RequestMethod::Unknown;
}

BodyType readBody(Bag *bag, const QJsonValue &value, QString *raw, bool *present) {
  *present = !(value.isUndefined() || value.isNull());
  if (!*present) return BodyType::None;
  if (!value.isString()) {
    if (value.isDouble()) *raw = QString::number(value.toInt());
    warn(bag, QStringLiteral("unsupported-body"), raw->isEmpty() ? QStringLiteral("Body") : *raw);
    return BodyType::Unknown;
  }
  const auto text = value.toString().trimmed();
  if (text.compare(QLatin1String("None"), Qt::CaseInsensitive) == 0) return BodyType::None;
  if (text.compare(QLatin1String("MultipartFormData"), Qt::CaseInsensitive) == 0) return BodyType::MultipartFormData;
  if (text.compare(QLatin1String("FormURLEncoded"), Qt::CaseInsensitive) == 0) return BodyType::FormUrlEncoded;
  if (text.compare(QLatin1String("JSON"), Qt::CaseInsensitive) == 0) return BodyType::Json;
  if (text.compare(QLatin1String("Binary"), Qt::CaseInsensitive) == 0) return BodyType::Binary;
  *raw = value.toString();
  warn(bag, QStringLiteral("unsupported-body"), text.left(64));
  return BodyType::Unknown;
}

void splitQuery(Bag *bag, UploadDestination *destination) {
  auto &url = destination->requestUrl;
  const auto fragment = url.indexOf(u'#');
  const auto query = url.indexOf(u'?');
  if (query < 0 || (fragment >= 0 && query > fragment)) return;
  const auto end = fragment < 0 ? url.size() : fragment;
  const auto queryText = url.mid(query + 1, end - query - 1);
  url.remove(query, end - query);
  if (queryText.isEmpty()) return;
  warn(bag, QStringLiteral("split-query"), QStringLiteral("RequestURL"));
  QSet<QString> existing;
  for (const auto &field : destination->query) existing.insert(field.name);
  for (const auto &part : queryText.split(u'&', Qt::SkipEmptyParts)) {
    const auto eq = part.indexOf(u'=');
    const auto name = QUrl::fromPercentEncoding((eq < 0 ? part : part.left(eq)).toUtf8());
    const auto value = QUrl::fromPercentEncoding((eq < 0 ? QString() : part.mid(eq + 1)).toUtf8());
    if (name.isEmpty() || name.size() > kMaxName || value.size() > kMaxValue) {
      warn(bag, QStringLiteral("too-large"), QStringLiteral("RequestURL"));
      continue;
    }
    if (existing.contains(name)) {
      warn(bag, QStringLiteral("duplicate-parameter"), name);
      continue;
    }
    existing.insert(name);
    destination->query.append({name, value});
  }
}

// Userinfo is always secret material. It is removed from the URL; the names
// url-username and url-password point at the returned secrets.
void stripUserInfo(Bag *bag, UploadDestination *destination) {
  const QUrl url(destination->requestUrl, QUrl::StrictMode);
  if (!url.isValid() || (url.userName().isEmpty() && url.password().isEmpty())) return;
  auto force = [&](const QString &name, const QString &value) {
    if (value.isEmpty() || referencedSecret(value)) return;
    scanSyntax(bag, value);
    const auto ref = allocate(bag, name);
    bag->secrets.append({ref, value});
    warn(bag, QStringLiteral("secret-extracted"), ref);
  };
  force(QStringLiteral("url-username"), url.userName());
  force(QStringLiteral("url-password"), url.password());
  QUrl cleaned(url);
  cleaned.setUserName({});
  cleaned.setPassword({});
  destination->requestUrl = cleaned.toString(QUrl::FullyEncoded);
}

QJsonValue scrub(Bag *bag, const QJsonValue &value, const QString &key, bool keyed) {
  if (value.isObject()) {
    QJsonObject out;
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it) out.insert(it.key(), scrub(bag, it.value(), it.key(), true));
    return out;
  }
  if (value.isArray()) {
    QJsonArray out;
    for (const auto &item : value.toArray()) out.append(scrub(bag, item, {}, false));
    return out;
  }
  if (!value.isString()) return value;
  auto text = value.toString();
  if (keyed && isSecretName(key)) takeSecret(bag, &text, key);
  else scanSyntax(bag, text);
  return text;
}

const char *const kConsumed[] = {"Name", "RequestURL", "RequestMethod", "RequestType", "Parameters", "Headers",
                                  "Body", "Arguments", "FileFormName", "Data", "URL", "ThumbnailURL", "DeletionURL",
                                  "ErrorMessage", "ResponseType"};

const char *const kExecutable[] = {"RegexList", "Regex", "Script", "Code", "Command", "Commands"};

}  // namespace

std::optional<QString> referencedSecret(const QString &value) {
  const auto prefix = QStringLiteral("{secret:");
  if (!value.startsWith(prefix) || !value.endsWith(u'}') || value.size() < prefix.size() + 2) return std::nullopt;
  const auto name = value.mid(prefix.size(), value.size() - prefix.size() - 1);
  if (name.isEmpty() || name.contains(u'{') || name.contains(u'}')) return std::nullopt;
  return name;
}

SxcuImportResult importSxcu(const QByteArray &bytes) {
  using compatibility::ImportDisposition;
  const auto parsed = compatibility::readJson(bytes, {kMaxDestinationBytes, kMaxDepth, 100000});
  if (!parsed.ok() || !parsed.document.isObject()) {
    SxcuImportResult failed; failed.code = parsed.ok() ? QStringLiteral("invalid-json") : parsed.code;
    failed.report.add(ImportDisposition::Rejected); return failed;
  }
  auto root = parsed.document.object();
  const auto original = root;
  Bag bag;
  UploadDestination destination;
  destination.schemaVersion = kDestinationSchemaVersion;
  destination.id = QUuid::createUuid();

  const auto requestMethod = root.value(QStringLiteral("RequestMethod"));
  const auto requestType = root.value(QStringLiteral("RequestType"));
  const bool hasMethod = root.contains(QStringLiteral("RequestMethod")) && !requestMethod.isNull();
  const bool hasType = root.contains(QStringLiteral("RequestType")) && !requestType.isNull();
  if (hasMethod && hasType) {
    const auto methodText = requestMethod.isString() ? requestMethod.toString().trimmed() : QString::number(requestMethod.toInt());
    const auto typeText = requestType.isString() ? requestType.toString().trimmed() : QString::number(requestType.toInt());
    if (methodText.compare(typeText, Qt::CaseInsensitive) != 0) warn(&bag, QStringLiteral("conflicting-method"), methodText.left(64));
  }
  if (hasMethod || hasType) destination.method = readMethod(&bag, hasMethod ? requestMethod : requestType, &destination.rawMethod);
  else destination.method = RequestMethod::Post;

  bool bodyPresent = false;
  destination.body = readBody(&bag, root.value(QStringLiteral("Body")), &destination.rawBody, &bodyPresent);
  destination.requestUrl = readText(&bag, root.value(QStringLiteral("RequestURL")), QStringLiteral("RequestURL"), kMaxValue);
  destination.fileFormName = readText(&bag, root.value(QStringLiteral("FileFormName")), QStringLiteral("FileFormName"), kMaxName);
  destination.data = readText(&bag, root.value(QStringLiteral("Data")), QStringLiteral("Data"), 256 * 1024);
  destination.name = readText(&bag, root.value(QStringLiteral("Name")), QStringLiteral("Name"), kMaxName);
  destination.resultUrl = readText(&bag, root.value(QStringLiteral("URL")), QStringLiteral("URL"), kMaxValue);
  destination.thumbnailUrl = readText(&bag, root.value(QStringLiteral("ThumbnailURL")), QStringLiteral("ThumbnailURL"), kMaxValue);
  destination.deletionUrl = readText(&bag, root.value(QStringLiteral("DeletionURL")), QStringLiteral("DeletionURL"), kMaxValue);
  destination.errorMessage = readText(&bag, root.value(QStringLiteral("ErrorMessage")), QStringLiteral("ErrorMessage"), kMaxValue);
  destination.query = readMap(&bag, root.value(QStringLiteral("Parameters")), QStringLiteral("Parameters"));
  destination.headers = readMap(&bag, root.value(QStringLiteral("Headers")), QStringLiteral("Headers"));
  destination.arguments = readMap(&bag, root.value(QStringLiteral("Arguments")), QStringLiteral("Arguments"));

  if (!bodyPresent && !destination.fileFormName.isEmpty()) {
    destination.body = BodyType::MultipartFormData;
    destination.rawBody.clear();
    warn(&bag, QStringLiteral("inferred-body"), QStringLiteral("MultipartFormData"));
  }

  const auto response = root.value(QStringLiteral("ResponseType"));
  if (root.contains(QStringLiteral("ResponseType")) && !response.isNull()) {
    const auto text = response.isString() && response.toString().compare(QLatin1String("Text"), Qt::CaseInsensitive) == 0;
    const auto zero = response.isDouble() && response.toInt() == 0;
    if (!text && !zero) warn(&bag, QStringLiteral("unsupported-syntax"), QStringLiteral("ResponseType"));
  }
  for (const char *key : kExecutable)
    if (root.contains(QLatin1String(key))) warn(&bag, QStringLiteral("unsupported-syntax"), QString::fromLatin1(key));

  for (const char *key : kConsumed) root.remove(QLatin1String(key));
  splitQuery(&bag, &destination);
  for (auto &field : destination.headers) takeSecret(&bag, &field.value, field.name);
  for (auto &field : destination.query) takeSecret(&bag, &field.value, field.name);
  for (auto &field : destination.arguments) takeSecret(&bag, &field.value, field.name);
  stripUserInfo(&bag, &destination);

  QJsonObject extra, quarantine;
  for (auto it = root.begin(); it != root.end(); ++it) {
    // Executable/authority metadata has no active destination representation.
    // Keep it separate from extra, which is ordinary round-trip data.
    if (unsafeMetadata(it.value(), it.key()) || compatibility::containsTraversal(it.value(), it.key())) quarantine.insert(it.key(), it.value());
    else extra.insert(it.key(), scrub(&bag, it.value(), it.key(), true));
  }
  destination.extra = extra;

  if (destination.name.trimmed().isEmpty()) {
    const QUrl url(destination.requestUrl, QUrl::StrictMode);
    if (!url.host().isEmpty()) destination.name = url.host();
  }

  SxcuImportResult result;
  result.ok = true;
  destination.name = destination.name.normalized(QString::NormalizationForm_C);
  result.destination = std::move(destination);
  result.secrets = std::move(bag.secrets);
  result.warnings = std::move(bag.warnings);
  result.quarantine = std::move(quarantine);
  result.report.add(ImportDisposition::Retained, original.size() - result.quarantine.size());
  if (result.destination.name != original.value("Name").toString() && original.value("Name").isString())
    result.report.add(ImportDisposition::Transformed);
  if (!hasMethod && !hasType) result.report.add(ImportDisposition::Defaulted);
  if (!bodyPresent) result.report.add(ImportDisposition::Defaulted);
  if (original.value("Name").toString().trimmed().isEmpty()) result.report.add(ImportDisposition::Defaulted);
  result.report.add(ImportDisposition::Quarantined, result.quarantine.size());
  result.acceptanceBlocked = !result.quarantine.isEmpty();
  for (const auto &warning : result.warnings) {
    if (warning.code == "secret-extracted" || warning.code == "split-query" || warning.code == "inferred-body")
      result.report.add(ImportDisposition::Transformed);
    else {
      result.report.add(ImportDisposition::Rejected);
      result.acceptanceBlocked = true;
    }
  }
  if (result.acceptanceBlocked) result.code = QStringLiteral("import-critical-unsupported");
  // RequestType is a legacy alias; when both are present only RequestMethod wins.
  if (hasMethod && hasType) result.report.add(ImportDisposition::Ignored);
  return result;
}

}  // namespace xerahs::uploads
