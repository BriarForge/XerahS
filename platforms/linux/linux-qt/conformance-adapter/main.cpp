// Linux conformance adapter: a thin executable over the production code paths.
// Protocol (conformance/runner/README.md): one JSON request on stdin,
// {"capability", "operation", "input"}, and one JSON result object on stdout.

#include "naming/FilenameGenerator.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cstdio>
#include <map>

using namespace xerahs::naming;

namespace {

class InjectedFileAccess final : public RandomFileAccess {
public:
  explicit InjectedFileAccess(const QJsonObject &files) {
    for (auto it = files.begin(); it != files.end(); ++it) {
      const QJsonObject entry = it.value().toObject();
      RandomFileRead read;
      read.status = entry.value(u"permission").toString() == u"granted"
                        ? RandomFileRead::Status::Granted
                        : RandomFileRead::Status::Denied;
      read.content = entry.value(u"content").toString().toUtf8();
      m_files.emplace(it.key(), read);
    }
  }

  RandomFileRead read(const QString &path) override {
    const auto it = m_files.find(path);
    if (it == m_files.end()) return RandomFileRead{RandomFileRead::Status::Missing, {}};
    return it->second;
  }

private:
  std::map<QString, RandomFileRead> m_files;
};

void writeJson(const QJsonObject &object) {
  const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
  std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
  std::fputc('\n', stdout);
}

QJsonObject adapterError(const QString &message) {
  return QJsonObject{{QStringLiteral("adapter_error"), message}};
}

std::optional<QString> optionalString(const QJsonObject &object, const char *key) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (!value.isString()) return std::nullopt;
  return value.toString();
}

std::optional<qint64> optionalInteger(const QJsonObject &object, const char *key) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (!value.isDouble()) return std::nullopt;
  return value.toInteger();
}

// Wall-clock fields of an ISO 8601 local_time exactly as supplied (FN-020).
bool parseLocalTime(const QString &text, QDate &date, QTime &time) {
  static const QRegularExpression pattern(QStringLiteral(
      R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,9}))?(?:Z|[+-]\d{2}:\d{2})$)"));
  const auto match = pattern.match(text);
  if (!match.hasMatch()) return false;
  date = QDate(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
  const QString fraction = (match.captured(7) + QStringLiteral("000")).left(3);
  time = QTime(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt(),
               fraction.toInt());
  return date.isValid() && time.isValid();
}

QJsonObject runFilenameGeneration(const QString &operation, const QJsonObject &input) {
  ExpansionRequest request;
  request.pattern = input.value(u"pattern").toString();
  const QString mode = input.value(u"mode").toString();
  if (mode == u"filename") request.mode = ParseMode::Filename;
  else if (mode == u"path") request.mode = ParseMode::Path;
  else if (mode == u"text") request.mode = ParseMode::Text;
  else if (mode == u"url") request.mode = ParseMode::Url;
  else return adapterError(QStringLiteral("unknown mode: ") + mode);
  request.extension = optionalString(input, "extension");
  request.maxLength = optionalInteger(input, "max_length");

  const QJsonObject json = input.value(u"context").toObject();
  ExpansionContext context;
  if (!parseLocalTime(json.value(u"local_time").toString(), context.date, context.time)) {
    return adapterError(QStringLiteral("invalid local_time"));
  }
  context.unixTime = json.value(u"unix_time").toInteger();
  context.locale = QLocale(json.value(u"locale").toString());
  context.counter = json.value(u"counter").toInteger();
  context.width = optionalInteger(json, "width");
  context.height = optionalInteger(json, "height");
  context.windowTitle = optionalString(json, "window_title");
  context.processName = optionalString(json, "process_name");
  context.userName = optionalString(json, "user_name");
  context.loginDomain = optionalString(json, "login_domain");
  context.computerName = optionalString(json, "computer_name");

  auto random = makeFixedRandomSource(
      QByteArray::fromHex(json.value(u"random_bytes").toString().toLatin1()));
  context.random = random.get();
  InjectedFileAccess files(json.value(u"random_files").toObject());
  context.files = &files;

  ExpansionResult result;
  if (operation == u"expand") result = expand(request, context);
  else if (operation == u"preview") result = preview(request, context);
  else return adapterError(QStringLiteral("unsupported operation: ") + operation);

  QJsonObject output;
  if (result.ok()) {
    output.insert(QStringLiteral("value"), result.value());
  } else {
    const ExpansionError &error = result.error();
    output.insert(QStringLiteral("error"),
                  QJsonObject{{QStringLiteral("code"), errorCodeName(error.code)},
                              {QStringLiteral("token"),
                               error.token ? QJsonValue(*error.token) : QJsonValue(QJsonValue::Null)},
                              {QStringLiteral("offset"), error.offset}});
  }
  output.insert(QStringLiteral("next_counter"), result.nextCounter);
  if (operation == u"preview") output.insert(QStringLiteral("committed_counter"), context.counter);
  return output;
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication app(argc, argv);
  QFile in;
  if (!in.open(stdin, QIODevice::ReadOnly)) {
    writeJson(adapterError(QStringLiteral("cannot read stdin")));
    return 2;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(in.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    writeJson(adapterError(QStringLiteral("invalid request: ") + parseError.errorString()));
    return 2;
  }
  const QJsonObject request = document.object();
  const QString capability = request.value(u"capability").toString();
  const QString operation = request.value(u"operation").toString();
  const QJsonObject input = request.value(u"input").toObject();

  if (capability == u"FILENAME-GENERATION-001") {
    writeJson(runFilenameGeneration(operation, input));
    return 0;
  }
  writeJson(adapterError(QStringLiteral("unsupported capability: ") + capability));
  return 0;
}
