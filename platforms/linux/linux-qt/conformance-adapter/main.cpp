// Linux conformance adapter: a thin executable over the production code paths.
// Protocol (conformance/runner/README.md): one JSON request on stdin,
// {"capability", "operation", "input"}, and one JSON result object on stdout.

#include "actions/PostCapturePipeline.h"
#include "capture/RegionGeometry.h"
#include "capture/RegionSession.h"
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
namespace rc = xerahs::capture;

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

rc::PhysicalRect physicalRect(const QJsonValue &value) {
  const QJsonObject o = value.toObject();
  return {o.value(u"left").toInteger(), o.value(u"top").toInteger(), o.value(u"right").toInteger(),
          o.value(u"bottom").toInteger()};
}

QJsonObject rectJson(const rc::PhysicalRect &r) {
  return QJsonObject{{QStringLiteral("left"), r.left}, {QStringLiteral("top"), r.top},
                     {QStringLiteral("right"), r.right}, {QStringLiteral("bottom"), r.bottom}};
}

QJsonArray sizeJson(const rc::PhysicalRect &r) { return QJsonArray{r.width(), r.height()}; }

rc::Topology topologyFrom(const QJsonValue &value) {
  rc::Topology topology;
  for (const QJsonValue &item : value.toArray()) {
    const QJsonObject o = item.toObject();
    const QJsonObject logical = o.value(u"logical").toObject();
    topology.push_back(rc::Display{o.value(u"id").toString(), physicalRect(o.value(u"physical")),
                                   rc::LogicalRect{logical.value(u"left").toDouble(), logical.value(u"top").toDouble(),
                                                   logical.value(u"right").toDouble(),
                                                   logical.value(u"bottom").toDouble()},
                                   o.value(u"scale").toDouble(1.0)});
  }
  return topology;
}

// state-events default: a single 1920x1080 display at scale 1.0.
rc::Topology defaultTopology() {
  return {rc::Display{QStringLiteral("display-a"), {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, 1.0}};
}

std::optional<rc::PhysicalPoint> mapPoint(const rc::Topology &topology, const QJsonValue &value) {
  const QJsonObject o = value.toObject();
  const rc::Display *display = rc::findDisplay(topology, o.value(u"display").toString());
  const QJsonArray logical = o.value(u"logical").toArray();
  if (!display || logical.size() != 2) return std::nullopt;
  return rc::mapToPhysical(*display, logical.at(0).toDouble(), logical.at(1).toDouble());
}

rc::PhysicalPoint physicalPoint(const QJsonValue &value) {
  const QJsonArray a = value.toArray();
  return {a.at(0).toInteger(), a.at(1).toInteger()};
}

QJsonObject runRegionCapture(const QString &operation, const QJsonObject &input) {
  if (operation == u"map-selection") {
    const rc::Topology topology = topologyFrom(input.value(u"topology"));
    const auto start = mapPoint(topology, input.value(u"start"));
    const auto end = mapPoint(topology, input.value(u"end"));
    if (!start || !end) return adapterError(QStringLiteral("unknown display or malformed point"));
    const rc::PhysicalRect rect = rc::normalized(*start, *end);
    return QJsonObject{{QStringLiteral("rectangle"), rectJson(rect)}, {QStringLiteral("output_size"), sizeJson(rect)}};
  }
  if (operation == u"keyboard-adjust") {
    const rc::PhysicalRect bounds = rc::boundingBox(topologyFrom(input.value(u"topology")));
    rc::PhysicalRect rect = physicalRect(input.value(u"rectangle"));
    for (const QJsonValue &item : input.value(u"steps").toArray()) {
      const QJsonObject o = item.toObject();
      rc::KeyboardStep step;
      step.precision = o.value(u"precision").toBool();
      const QString action = o.value(u"action").toString();
      if (action == u"move") {
        step.action = rc::KeyboardStep::Action::Move;
        step.dx = o.value(u"dx").toInteger();
        step.dy = o.value(u"dy").toInteger();
      } else if (action == u"resize") {
        step.action = rc::KeyboardStep::Action::Resize;
        const QString edge = o.value(u"edge").toString();
        if (edge == u"left") step.edge = rc::Edge::Left;
        else if (edge == u"top") step.edge = rc::Edge::Top;
        else if (edge == u"right") step.edge = rc::Edge::Right;
        else if (edge == u"bottom") step.edge = rc::Edge::Bottom;
        else return adapterError(QStringLiteral("unknown edge: ") + edge);
        step.delta = o.value(u"delta").toInteger();
      } else {
        return adapterError(QStringLiteral("unknown step action: ") + action);
      }
      rect = rc::applyKeyboardStep(rect, step, bounds);
    }
    return QJsonObject{{QStringLiteral("rectangle"), rectJson(rect)}};
  }
  if (operation == u"composite-coverage") {
    const rc::Topology topology = topologyFrom(input.value(u"topology"));
    const rc::PhysicalRect rect = physicalRect(input.value(u"rectangle"));
    // Injected capture source: every display is opaque mid-grey.
    const rc::CompositeResult result = rc::composite(rect, topology, [](const rc::Display &, const rc::PhysicalRect &source) {
      rc::RgbaImage image{source.width(), source.height(), {}};
      image.pixels = QByteArray(static_cast<qsizetype>(source.width() * source.height() * 4), '\x80');
      for (qsizetype i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = '\xff';
      return std::optional<rc::RgbaImage>(image);
    });
    if (!result.image) return QJsonObject{{QStringLiteral("error"), *result.diagnostic}};
    return QJsonObject{{QStringLiteral("output_size"), QJsonArray{result.image->width, result.image->height}},
                       {QStringLiteral("transparent_pixels"), rc::transparentBlackPixels(*result.image)},
                       {QStringLiteral("source_displays"), QJsonArray::fromStringList(result.sourceDisplays)}};
  }
  if (operation == u"last-region") {
    const QJsonObject stored = input.value(u"stored").toObject();
    rc::LastRegion region;
    region.version = stored.value(u"version").toInt();
    region.rectangle = physicalRect(stored.value(u"rectangle"));
    for (const QJsonValue &item : stored.value(u"displays").toArray()) {
      const QJsonObject o = item.toObject();
      region.displays.push_back({o.value(u"id").toString(), physicalRect(o.value(u"physical"))});
    }
    const auto rect = rc::validateLastRegion(region, topologyFrom(input.value(u"topology")));
    if (!rect) {
      return QJsonObject{{QStringLiteral("error"), rc::diagnostic::lastRegionInvalid}, {QStringLiteral("captured"), false}};
    }
    return QJsonObject{{QStringLiteral("error"), QJsonValue::Null}, {QStringLiteral("rectangle"), rectJson(*rect)}};
  }
  if (operation != u"state-events") return adapterError(QStringLiteral("unsupported operation: ") + operation);

  const QJsonObject settings = input.value(u"settings").toObject();
  rc::SessionSettings sessionSettings;
  sessionSettings.quickConfirm = settings.value(u"quick_confirm").toBool(true);
  sessionSettings.snapping = settings.value(u"snapping").toBool(false);
  std::vector<rc::SnapTarget> targets;
  for (const QJsonValue &item : input.value(u"snap_targets").toArray()) {
    targets.push_back(rc::SnapTarget{physicalRect(item.toObject().value(u"physical"))});
  }
  const rc::Topology topology =
      input.contains(u"topology") ? topologyFrom(input.value(u"topology")) : defaultTopology();
  std::optional<rc::SessionOutcome> published;
  rc::RegionSession session(topology, sessionSettings, std::move(targets),
                            [&](const rc::SessionOutcome &outcome) { published = outcome; });
  const QString initial = input.value(u"initial_state").toString(QStringLiteral("authorizing"));
  const auto initialState = rc::sessionStateFromName(initial);
  if (!initialState) return adapterError(QStringLiteral("unknown initial_state: ") + initial);
  if (*initialState == rc::SessionState::Authorizing) session.start();
  else session.resumeAt(*initialState);

  for (const QJsonValue &item : input.value(u"events").toArray()) {
    const QJsonObject o = item.toObject();
    const QString event = o.value(u"event").toString();
    if (event == u"permission") {
      const QString value = o.value(u"value").toString();
      if (value == u"authorized") session.permission(rc::Permission::Authorized);
      else if (value == u"denied") session.permission(rc::Permission::Denied);
      else if (value == u"restricted") session.permission(rc::Permission::Restricted);
      else return adapterError(QStringLiteral("unknown permission: ") + value);
    } else if (event == u"ready") session.ready();
    else if (event == u"pointer-down") session.pointerDown(physicalPoint(o.value(u"physical")));
    else if (event == u"pointer-move") session.pointerMove(physicalPoint(o.value(u"physical")));
    else if (event == u"pointer-up") session.pointerUp(physicalPoint(o.value(u"physical")));
    else if (event == u"key-enter") session.keyEnter();
    else if (event == u"confirm") session.confirm();
    else if (event == u"key-escape") session.keyEscape();
    else if (event == u"cancel") session.cancel();
    else if (event == u"topology-changed") session.topologyChanged();
    else if (event == u"capture-succeeded") session.captureSucceeded();
    else if (event == u"capture-failed") session.captureFailed();
    else return adapterError(QStringLiteral("unknown event: ") + event);
  }

  QJsonValue result = QJsonValue::Null;
  if (published && published->rectangle) {
    result = QJsonObject{{QStringLiteral("rectangle"), rectJson(*published->rectangle)},
                         {QStringLiteral("output_size"), sizeJson(*published->rectangle)}};
  }
  const auto selection = session.selection();
  const auto diagnosticCode = session.diagnostic();
  return QJsonObject{{QStringLiteral("state"), rc::sessionStateName(session.state())},
                     {QStringLiteral("rectangle"), selection ? QJsonValue(rectJson(*selection)) : QJsonValue::Null},
                     {QStringLiteral("result"), result},
                     {QStringLiteral("diagnostic"), diagnosticCode ? QJsonValue(*diagnosticCode) : QJsonValue::Null},
                     {QStringLiteral("published_results"), session.publishedResults()},
                     {QStringLiteral("last_region_updated"), session.lastRegionUpdated()},
                     {QStringLiteral("overlay_shown"), session.overlayShown()}};
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
  if (capability == u"REGION-CAPTURE-001") {
    writeJson(runRegionCapture(operation, input));
    return 0;
  }
  if (capability == u"POST-CAPTURE-ACTIONS-001") {
    writeJson(runPostCaptureActions(operation, input));
    return 0;
  }
  writeJson(adapterError(QStringLiteral("unsupported capability: ") + capability));
  return 0;
}
