// Linux conformance adapter: a thin executable over the production code paths.
// Protocol (conformance/runner/README.md): one JSON request on stdin,
// {"capability", "operation", "input"}, and one JSON result object on stdout.

#include "actions/PostCapturePipeline.h"
#include "naming/FilenameGenerator.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cstdio>
#include <map>

using namespace xerahs::naming;
namespace pca = xerahs::actions;

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

QStringList stringList(const QJsonValue &value) {
  QStringList list;
  for (const QJsonValue &item : value.toArray()) list.append(item.toString());
  return list;
}

// Injected executor: each action's own work is replaced by the vector's
// injected outcome; absent actions succeed. cancel_during requests
// cancellation while the named action runs; an interruptible action then stops.
class InjectedExecutor final : public pca::ActionExecutor {
public:
  InjectedExecutor(QJsonObject outcomes, QString cancelDuring, pca::CancellationToken &token)
      : m_outcomes(std::move(outcomes)), m_cancelDuring(std::move(cancelDuring)), m_token(token) {}

  pca::ActionOutcome run(const pca::ActionRequest &request) override {
    pca::ActionOutcome outcome;
    const QJsonValue injected = m_outcomes.value(request.spec.id);
    QString state = QStringLiteral("succeeded");
    if (injected.isString()) {
      state = injected.toString();
    } else if (injected.isObject()) {
      const QJsonObject object = injected.toObject();
      state = object.value(u"state").toString(state);
      if (object.contains(u"selected")) outcome.replacementSelection = stringList(object.value(u"selected"));
    }
    if (request.spec.id == m_cancelDuring) {
      m_token.request();
      if (request.spec.interruptible && !injected.isString() && !injected.isObject()) {
        state = QStringLiteral("cancelled");
      }
    }
    if (state == u"failed") outcome.state = pca::ActionState::Failed;
    else if (state == u"cancelled") outcome.state = pca::ActionState::Cancelled;
    else outcome.state = pca::ActionState::Succeeded;
    return outcome;
  }

  bool createTemporaryUploadFile() override { return true; }
  void removeTemporaryUploadFile() override {}

private:
  QJsonObject m_outcomes;
  QString m_cancelDuring;
  pca::CancellationToken &m_token;
};

QJsonObject actionResultJson(const pca::ActionResult &result) {
  QJsonObject json{{QStringLiteral("action"), result.action},
                   {QStringLiteral("state"), pca::actionStateName(result.state)}};
  if (result.diagnostic) json.insert(QStringLiteral("diagnostic"), *result.diagnostic);
  if (result.missingArtifact) json.insert(QStringLiteral("missing_artifact"), *result.missingArtifact);
  if (result.inputArtifact) json.insert(QStringLiteral("input_artifact"), *result.inputArtifact);
  if (result.cancellationArrivedDuring) json.insert(QStringLiteral("cancellation_arrived_during"), true);
  return json;
}

QJsonObject runPostCaptureActions(const QString &operation, const QJsonObject &input) {
  if (operation == u"map-legacy-flags") {
    const pca::LegacyFlagMapping mapping = pca::mapLegacyFlags(stringList(input.value(u"legacy_flags")));
    QJsonObject output{{QStringLiteral("selected"), QJsonArray::fromStringList(mapping.selected)}};
    if (!mapping.unknownFlags.isEmpty()) {
      output.insert(QStringLiteral("unknown_flags"), QJsonArray::fromStringList(mapping.unknownFlags));
    }
    return output;
  }
  if (operation != u"run-pipeline") return adapterError(QStringLiteral("unsupported operation: ") + operation);

  const QJsonObject options = input.value(u"options").toObject();
  pca::PipelineOptions pipelineOptions;
  pipelineOptions.uploadTemporaryAllowed = options.value(u"upload_temporary_allowed").toBool(true);

  pca::CancellationToken token;
  InjectedExecutor executor(input.value(u"injected_outcomes").toObject(),
                            input.value(u"cancel_during").toString(), token);
  const QString cancelAfter = input.value(u"cancel_after").toString();
  pca::PipelineHooks hooks;
  hooks.actionFinished = [&](const pca::ActionResult &result) {
    if (!cancelAfter.isEmpty() && result.action == cancelAfter) token.request();
  };

  const pca::PipelineResult result = pca::runPipeline(QStringLiteral("conformance"), stringList(input.value(u"selected")),
                                                      pipelineOptions, executor, token, hooks);
  if (!result.ignoredSelection.isEmpty()) {
    return adapterError(QStringLiteral("unknown action IDs: ") + result.ignoredSelection.join(u','));
  }
  QJsonArray ordered;
  for (const pca::ActionResult &action : result.orderedResults) ordered.append(actionResultJson(action));
  return QJsonObject{{QStringLiteral("pipeline_state"), pca::pipelineStateName(result.state)},
                     {QStringLiteral("final_working_media"), result.finalWorkingMedia},
                     {QStringLiteral("ordered_results"), ordered},
                     {QStringLiteral("working_media_retained"), result.workingMediaRetained}};
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
  if (capability == u"POST-CAPTURE-ACTIONS-001") {
    writeJson(runPostCaptureActions(operation, input));
    return 0;
  }
  writeJson(adapterError(QStringLiteral("unsupported capability: ") + capability));
  return 0;
}
