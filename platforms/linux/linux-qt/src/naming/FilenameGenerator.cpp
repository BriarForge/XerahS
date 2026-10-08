#include "FilenameGenerator.h"

#include <QRandomGenerator>
#include <QStringDecoder>
#include <QUrl>

#include <algorithm>
#include <array>
#include <limits>

namespace xerahs::naming {

namespace {

using Text = std::u32string;

// tokens.json, scanned longest first (FN-018).
const std::array<const char *, 39> kTokenNames = {
    "%radjective", "%ranimal", "%remoji", "%height", "%width", "%mon2", "%unix",
    "%guid", "%GUID", "%mon", "%rna", "%uln", "%w2", "%yy", "%mo", "%mi", "%ms",
    "%pm", "%wy", "%pn", "%un", "%cn", "%ia", "%iA", "%ix", "%iX", "%rn", "%ra",
    "%rx", "%rX", "%rf", "%y", "%d", "%h", "%s", "%w", "%t", "%i", "%%",
};

// random-lists.json (FN-021). Index order is normative.
const char *const kDigits = "0123456789";
const char *const kAlphanumeric = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const char *const kNonAmbiguous = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";
const char *const kHexLower = "0123456789abcdef";
const char *const kHexUpper = "0123456789ABCDEF";
constexpr char32_t kEmojiFirst = 0x1F600;
constexpr int kEmojiCount = 64;

const std::array<const char *, 64> kAdjectives = {
    "Able", "Agile", "Amber", "Ancient", "Bold", "Brave", "Bright", "Brisk",
    "Calm", "Clever", "Cosmic", "Crisp", "Curious", "Daring", "Deep", "Eager",
    "Early", "Fair", "Fancy", "Fast", "Fearless", "Fierce", "Fluffy", "Gentle",
    "Giant", "Glad", "Golden", "Grand", "Happy", "Hidden", "Humble", "Icy",
    "Jolly", "Keen", "Kind", "Lively", "Lucky", "Lunar", "Mellow", "Merry",
    "Mighty", "Misty", "Noble", "Nimble", "Patient", "Polar", "Proud", "Quick",
    "Quiet", "Rapid", "Royal", "Rustic", "Silent", "Silver", "Smooth", "Solar",
    "Steady", "Sunny", "Swift", "Tidy", "Vivid", "Warm", "Wild", "Witty"};

const std::array<const char *, 64> kAnimals = {
    "Albatross", "Alpaca", "Badger", "Beaver", "Bison", "Bobcat", "Buffalo", "Camel",
    "Cheetah", "Cobra", "Cougar", "Coyote", "Crane", "Dingo", "Dolphin", "Eagle",
    "Echidna", "Elk", "Emu", "Falcon", "Ferret", "Finch", "Fox", "Gazelle",
    "Gecko", "Gibbon", "Heron", "Ibis", "Iguana", "Jackal", "Jaguar", "Kestrel",
    "Kiwi", "Koala", "Kookaburra", "Lemur", "Leopard", "Lynx", "Magpie", "Marmot",
    "Meerkat", "Moose", "Narwhal", "Ocelot", "Octopus", "Orca", "Otter", "Owl",
    "Panda", "Pelican", "Penguin", "Platypus", "Puffin", "Quokka", "Raven", "Salmon",
    "Seal", "Sparrow", "Tiger", "Toucan", "Walrus", "Wombat", "Yak", "Zebra"};

const std::array<const char *, 12> kEnglishMonths = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"};

const std::array<const char *, 7> kEnglishWeekdays = {
    "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

bool isCounterToken(const QString &name) {
  return name == u"%i" || name == u"%ia" || name == u"%iA" || name == u"%ix" || name == u"%iX";
}

bool isRandomCharacterToken(const QString &name) {
  return name == u"%rn" || name == u"%ra" || name == u"%rna" || name == u"%rx" ||
         name == u"%rX" || name == u"%remoji";
}

bool acceptsArgument(const QString &name) {
  return isCounterToken(name) || isRandomCharacterToken(name) || name == u"%rf";
}

// Unicode White_Space property.
bool isWhiteSpace(char32_t c) {
  return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F ||
         c == 0x205F || c == 0x3000;
}

Text trimWhiteSpace(const Text &value) {
  std::size_t begin = 0;
  std::size_t end = value.size();
  while (begin < end && isWhiteSpace(value[begin])) ++begin;
  while (end > begin && isWhiteSpace(value[end - 1])) --end;
  return value.substr(begin, end - begin);
}

Text toText(const QString &value) { return value.toStdU32String(); }
Text toText(const char *value) { return QString::fromLatin1(value).toStdU32String(); }
QString toQString(const Text &value) { return QString::fromStdU32String(value); }

// FN-011 character set.
bool isPortableInvalid(char32_t c) {
  return c <= 0x1F || c == U'<' || c == U'>' || c == U':' || c == U'"' || c == U'/' ||
         c == U'\\' || c == U'|' || c == U'?' || c == U'*';
}

// FN-011: replace each invalid character with '_' and collapse consecutive
// replacements to one '_'.
Text replaceInvalid(const Text &value) {
  Text out;
  out.reserve(value.size());
  bool previousReplaced = false;
  for (char32_t c : value) {
    if (isPortableInvalid(c)) {
      if (!previousReplaced) out.push_back(U'_');
      previousReplaced = true;
    } else {
      out.push_back(c);
      previousReplaced = false;
    }
  }
  return out;
}

void removeTrailingSpacesAndPeriods(Text &value) {
  while (!value.empty() && (value.back() == U' ' || value.back() == U'.')) value.pop_back();
}

// FN-013
void applyEmptyRule(Text &value) {
  if (value.empty() || value == U"." || value == U"..") value = U"_";
}

bool isReservedDeviceName(const Text &value) {
  const auto dot = value.find(U'.');
  const QString head = toQString(value.substr(0, dot)).toUpper();
  if (head == u"CON" || head == u"PRN" || head == u"AUX" || head == u"NUL") return true;
  if (head.size() == 4 && (head.startsWith(u"COM") || head.startsWith(u"LPT"))) {
    const QChar digit = head.at(3);
    return digit >= u'1' && digit <= u'9';
  }
  return false;
}

Text padNumber(qint64 value, int width) {
  Text digits = toText(QString::number(value));
  while (static_cast<int>(digits.size()) < width) digits.insert(digits.begin(), U'0');
  return digits;
}

Text formatCounter(qint64 value, int base, bool upper, int width) {
  QString digits = QString::number(value, base);
  if (upper) digits = digits.toUpper();
  Text out = toText(digits);
  while (static_cast<int>(out.size()) < width) out.insert(out.begin(), U'0');
  return out;
}

struct Segment {
  enum class Kind { Literal, Separator, Token };
  Kind kind = Kind::Literal;
  char32_t literal = 0;
  QString token;
  std::optional<Text> argument;
  int count = 1;  // validated width or repeat count
  qint64 offset = 0;
};

struct Failure {
  ExpansionError error;
};

ExpansionError makeError(ErrorCode code, const QString &token, qint64 offset) {
  return ExpansionError{code, token, offset};
}

// FN-018, FN-019: left-to-right longest-match scan.
std::variant<std::vector<Segment>, ExpansionError> tokenize(const Text &pattern, bool pathMode) {
  std::vector<Segment> segments;
  std::size_t i = 0;
  while (i < pattern.size()) {
    const char32_t c = pattern[i];
    if (c == U'%') {
      QString matched;
      for (const char *name : kTokenNames) {
        const Text candidate = toText(name);
        if (pattern.compare(i, candidate.size(), candidate) == 0 &&
            static_cast<qsizetype>(candidate.size()) > matched.size()) {
          matched = QString::fromLatin1(name);
        }
      }
      if (!matched.isEmpty()) {
        Segment segment;
        segment.kind = Segment::Kind::Token;
        segment.token = matched;
        segment.offset = static_cast<qint64>(i);
        std::size_t after = i + static_cast<std::size_t>(matched.size());
        if (acceptsArgument(matched) && after < pattern.size() && pattern[after] == U'{') {
          const auto close = pattern.find(U'}', after + 1);
          if (close == Text::npos) {
            return makeError(ErrorCode::UnterminatedArgument, matched, segment.offset);
          }
          segment.argument = pattern.substr(after + 1, close - after - 1);
          after = close + 1;
        }
        if (isCounterToken(matched) || isRandomCharacterToken(matched)) {
          if (segment.argument) {
            const Text &arg = *segment.argument;
            const bool digits = !arg.empty() && arg.size() <= 3 &&
                                std::all_of(arg.begin(), arg.end(),
                                            [](char32_t d) { return d >= U'0' && d <= U'9'; });
            const int number = digits ? toQString(arg).toInt() : 0;
            if (number < 1 || number > 256) {
              return makeError(ErrorCode::InvalidArgument, matched, segment.offset);
            }
            segment.count = number;
          } else {
            // Counter: no padding. Random character: one character.
            segment.count = isCounterToken(matched) ? 0 : 1;
          }
        } else if (matched == u"%rf" && !segment.argument) {
          return makeError(ErrorCode::InvalidArgument, matched, segment.offset);
        }
        segments.push_back(segment);
        i = after;
        continue;
      }
    }
    Segment literal;
    literal.kind = pathMode && (c == U'/' || c == U'\\') ? Segment::Kind::Separator
                                                          : Segment::Kind::Literal;
    literal.literal = c;
    literal.offset = static_cast<qint64>(i);
    segments.push_back(literal);
    ++i;
  }
  return segments;
}

class Expander {
public:
  Expander(const ExpansionContext &context, ParseMode mode, bool twelveHour, qint64 counterValue)
      : m_context(context), m_mode(mode), m_twelveHour(twelveHour), m_counter(counterValue) {}

  std::variant<Text, ExpansionError> expand(const Segment &segment) {
    const QString &t = segment.token;
    const QDate &date = m_context.date;
    const QTime &time = m_context.time;

    if (t == u"%%") return Text(U"%");
    if (t == u"%y") return padNumber(date.year(), 4);
    if (t == u"%yy") return padNumber(date.year() % 100, 2);
    if (t == u"%mo") return padNumber(date.month(), 2);
    if (t == u"%d") return padNumber(date.day(), 2);
    if (t == u"%h") {
      int hour = time.hour();
      if (m_twelveHour) {
        if (hour == 0) hour = 12;
        else if (hour > 12) hour -= 12;
      }
      return padNumber(hour, 2);
    }
    if (t == u"%mi") return padNumber(time.minute(), 2);
    if (t == u"%s") return padNumber(time.second(), 2);
    if (t == u"%ms") return padNumber(time.msec(), 3);
    if (t == u"%pm") return Text(time.hour() < 12 ? U"AM" : U"PM");
    if (t == u"%wy") return padNumber(date.weekNumber(), 2);
    if (t == u"%w") return toText(m_context.locale.dayName(date.dayOfWeek(), QLocale::LongFormat));
    if (t == u"%w2") return toText(kEnglishWeekdays[date.dayOfWeek() - 1]);
    if (t == u"%mon") return toText(m_context.locale.monthName(date.month(), QLocale::LongFormat));
    if (t == u"%mon2") return toText(kEnglishMonths[date.month() - 1]);
    if (t == u"%unix") return toText(QString::number(m_context.unixTime));
    if (t == u"%width") return dimension(m_context.width);
    if (t == u"%height") return dimension(m_context.height);
    if (t == u"%t") return metadata(m_context.windowTitle, true);
    if (t == u"%pn") return metadata(m_context.processName, true);
    if (t == u"%un") return metadata(m_context.userName, false);
    if (t == u"%uln") return metadata(m_context.loginDomain, false);
    if (t == u"%cn") return metadata(m_context.computerName, false);
    if (t == u"%i") return formatCounter(m_counter, 10, false, segment.count);
    if (t == u"%ia") return formatCounter(m_counter, 36, false, segment.count);
    if (t == u"%iA") return formatCounter(m_counter, 36, true, segment.count);
    if (t == u"%ix") return formatCounter(m_counter, 16, false, segment.count);
    if (t == u"%iX") return formatCounter(m_counter, 16, true, segment.count);
    if (t == u"%rn") return randomCharacters(segment, kDigits);
    if (t == u"%ra") return randomCharacters(segment, kAlphanumeric);
    if (t == u"%rna") return randomCharacters(segment, kNonAmbiguous);
    if (t == u"%rx") return randomCharacters(segment, kHexLower);
    if (t == u"%rX") return randomCharacters(segment, kHexUpper);
    if (t == u"%remoji") return randomEmoji(segment);
    if (t == u"%radjective") return randomWord(segment, kAdjectives);
    if (t == u"%ranimal") return randomWord(segment, kAnimals);
    if (t == u"%guid" || t == u"%GUID") return guid(segment, t == u"%GUID");
    if (t == u"%rf") return randomFileLine(segment);
    return toText(t);
  }

private:
  static Text dimension(const std::optional<qint64> &value) {
    if (!value || *value <= 0) return Text();
    return toText(QString::number(*value));
  }

  // FN-005, FN-022
  Text insertValue(Text value, bool replaceSpaces) const {
    value = trimWhiteSpace(value);
    if (replaceSpaces) std::replace(value.begin(), value.end(), U' ', U'_');
    switch (m_mode) {
      case ParseMode::Filename:
      case ParseMode::Path:
        return replaceInvalid(value);
      case ParseMode::Url:
        return toText(QString::fromLatin1(QUrl::toPercentEncoding(toQString(value))));
      case ParseMode::Text:
        return value;
    }
    return value;
  }

  Text metadata(const std::optional<QString> &value, bool replaceSpaces) const {
    return insertValue(value ? toText(*value) : Text(), replaceSpaces);
  }

  // FN-021 rejection sampling for a list of n entries.
  std::optional<int> pick(int n) {
    const int limit = 256 - (256 % n);
    while (true) {
      const auto byte = m_context.random ? m_context.random->next() : std::nullopt;
      if (!byte) return std::nullopt;
      if (*byte < limit) return *byte % n;
    }
  }

  std::variant<Text, ExpansionError> randomCharacters(const Segment &segment, const char *alphabet) {
    const int n = static_cast<int>(qstrlen(alphabet));
    Text out;
    for (int k = 0; k < segment.count; ++k) {
      const auto index = pick(n);
      if (!index) return exhausted(segment);
      out.push_back(static_cast<char32_t>(alphabet[*index]));
    }
    return out;
  }

  std::variant<Text, ExpansionError> randomEmoji(const Segment &segment) {
    Text out;
    for (int k = 0; k < segment.count; ++k) {
      const auto index = pick(kEmojiCount);
      if (!index) return exhausted(segment);
      out.push_back(kEmojiFirst + static_cast<char32_t>(*index));
    }
    return out;
  }

  std::variant<Text, ExpansionError> randomWord(const Segment &segment,
                                                const std::array<const char *, 64> &words) {
    const auto index = pick(static_cast<int>(words.size()));
    if (!index) return exhausted(segment);
    return toText(words[*index]);
  }

  std::variant<Text, ExpansionError> guid(const Segment &segment, bool upper) {
    std::array<std::uint8_t, 16> bytes{};
    for (auto &b : bytes) {
      const auto byte = m_context.random ? m_context.random->next() : std::nullopt;
      if (!byte) return exhausted(segment);
      b = *byte;
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
    const char *hex = upper ? kHexUpper : kHexLower;
    Text out;
    for (int k = 0; k < 16; ++k) {
      if (k == 4 || k == 6 || k == 8 || k == 10) out.push_back(U'-');
      out.push_back(static_cast<char32_t>(hex[bytes[k] >> 4]));
      out.push_back(static_cast<char32_t>(hex[bytes[k] & 0x0F]));
    }
    return out;
  }

  // FN-009, FN-026
  std::variant<Text, ExpansionError> randomFileLine(const Segment &segment) {
    const QString path = toQString(segment.argument.value_or(Text()));
    if (!m_context.files) return makeError(ErrorCode::FilePermissionDenied, segment.token, segment.offset);
    const RandomFileRead read = m_context.files->read(path);
    if (read.status == RandomFileRead::Status::Denied) {
      return makeError(ErrorCode::FilePermissionDenied, segment.token, segment.offset);
    }
    if (read.status == RandomFileRead::Status::Missing) {
      return makeError(ErrorCode::FileUnavailable, segment.token, segment.offset);
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString content = decoder(read.content);
    if (decoder.hasError() || content.contains(QChar(0))) {
      return makeError(ErrorCode::FileUnavailable, segment.token, segment.offset);
    }
    std::vector<Text> lines;
    for (QString line : content.split(u'\n')) {
      if (line.endsWith(u'\r')) line.chop(1);
      Text trimmed = trimWhiteSpace(toText(line));
      if (!trimmed.empty()) lines.push_back(trimmed);
    }
    if (lines.empty()) return makeError(ErrorCode::FileUnavailable, segment.token, segment.offset);
    if (lines.size() > 256) {
      // FN-021 draws one byte per element, so no byte could ever select from more
      // than 256 lines. Pending contract clarification under ROOT-ESCALATE-001;
      // until then only the first 256 lines are selectable.
      lines.resize(256);
    }
    const auto index = pick(static_cast<int>(lines.size()));
    if (!index) return exhausted(segment);
    return insertValue(lines[static_cast<std::size_t>(*index)], false);
  }

  ExpansionError exhausted(const Segment &segment) const {
    return makeError(ErrorCode::RandomSourceExhausted, segment.token, segment.offset);
  }

  const ExpansionContext &m_context;
  ParseMode m_mode;
  bool m_twelveHour;
  qint64 m_counter;
};

// FN-023 finalization of one component.
std::variant<Text, ExpansionError> finalizeComponent(const Text &expanded,
                                                     const std::optional<QString> &extension,
                                                     const std::optional<qint64> &maxLength) {
  Text name = replaceInvalid(expanded);
  removeTrailingSpacesAndPeriods(name);
  applyEmptyRule(name);

  const bool hasExtension = extension && !extension->isEmpty();
  Text suffix;
  if (hasExtension) suffix = U"." + replaceInvalid(toText(*extension));

  if (maxLength) {
    const qint64 limit = *maxLength;
    if (static_cast<qint64>(suffix.size()) >= limit) {
      return ExpansionError{ErrorCode::NameTooLong, std::nullopt, 0};
    }
    const auto baseLimit = static_cast<std::size_t>(limit - static_cast<qint64>(suffix.size()));
    if (name.size() > baseLimit) name.resize(baseLimit);
  }
  if (!hasExtension) {
    removeTrailingSpacesAndPeriods(name);
    applyEmptyRule(name);
  }

  if (isReservedDeviceName(name + suffix)) {
    name.insert(name.begin(), U'_');
    if (maxLength && static_cast<qint64>(name.size() + suffix.size()) > *maxLength) name.pop_back();
  }
  return name + suffix;
}

bool isBlank(const Text &pattern) {
  return std::all_of(pattern.begin(), pattern.end(), isWhiteSpace);
}

ExpansionResult failure(ExpansionError error, qint64 counter) {
  return ExpansionResult{std::move(error), counter};
}

}  // namespace

QString errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::InvalidArgument: return QStringLiteral("invalid-argument");
    case ErrorCode::UnterminatedArgument: return QStringLiteral("unterminated-argument");
    case ErrorCode::RandomSourceExhausted: return QStringLiteral("random-source-exhausted");
    case ErrorCode::FilePermissionDenied: return QStringLiteral("file-permission-denied");
    case ErrorCode::FileUnavailable: return QStringLiteral("file-unavailable");
    case ErrorCode::PathNotRelative: return QStringLiteral("path-not-relative");
    case ErrorCode::PathTraversal: return QStringLiteral("path-traversal");
    case ErrorCode::CounterOverflow: return QStringLiteral("counter-overflow");
    case ErrorCode::NameTooLong: return QStringLiteral("name-too-long");
  }
  return QString();
}

ExpansionResult expand(const ExpansionRequest &request, const ExpansionContext &context) {
  const Text pattern = toText(request.pattern);
  const qint64 counter = context.counter;
  const bool pathMode = request.mode == ParseMode::Path;
  const bool sanitized = request.mode == ParseMode::Filename || pathMode;

  if (isBlank(pattern)) {
    if (!sanitized) return ExpansionResult{QString(), counter};
  } else if (pathMode) {
    // FN-024: no leading separator or drive prefix.
    const bool leadingSeparator = pattern[0] == U'/' || pattern[0] == U'\\';
    const bool drive = pattern.size() >= 2 && pattern[1] == U':' &&
                       ((pattern[0] >= U'A' && pattern[0] <= U'Z') ||
                        (pattern[0] >= U'a' && pattern[0] <= U'z'));
    if (leadingSeparator || drive) {
      return failure(ExpansionError{ErrorCode::PathNotRelative, std::nullopt, 0}, counter);
    }
  }

  auto tokenized = tokenize(isBlank(pattern) ? Text() : pattern, pathMode);
  if (auto *error = std::get_if<ExpansionError>(&tokenized)) return failure(*error, counter);
  const auto &segments = std::get<std::vector<Segment>>(tokenized);

  // FN-007, FN-025: advance once before any counter token expands.
  qint64 nextCounter = counter;
  const auto firstCounter = std::find_if(segments.begin(), segments.end(), [](const Segment &s) {
    return s.kind == Segment::Kind::Token && isCounterToken(s.token);
  });
  if (firstCounter != segments.end()) {
    if (counter == std::numeric_limits<qint64>::max()) {
      return failure(makeError(ErrorCode::CounterOverflow, firstCounter->token, firstCounter->offset),
                     counter);
    }
    nextCounter = counter + 1;
  }

  // FN-003: %pm anywhere selects the twelve-hour clock.
  const bool twelveHour = std::any_of(segments.begin(), segments.end(), [](const Segment &s) {
    return s.kind == Segment::Kind::Token && s.token == u"%pm";
  });

  Expander expander(context, request.mode, twelveHour, nextCounter);

  // Components split on literal separators; non-path modes have exactly one.
  struct Component {
    Text text;
    qint64 offset = 0;
  };
  std::vector<Component> components(1);
  for (const Segment &segment : segments) {
    switch (segment.kind) {
      case Segment::Kind::Separator:
        components.push_back(Component{Text(), segment.offset + 1});
        break;
      case Segment::Kind::Literal:
        components.back().text.push_back(segment.literal);
        break;
      case Segment::Kind::Token: {
        auto value = expander.expand(segment);
        if (auto *error = std::get_if<ExpansionError>(&value)) return failure(*error, counter);
        components.back().text += std::get<Text>(value);
        break;
      }
    }
  }

  if (!sanitized) return ExpansionResult{toQString(components.front().text), nextCounter};

  Text result;
  for (std::size_t k = 0; k < components.size(); ++k) {
    const bool last = k + 1 == components.size();
    if (pathMode && components[k].text == U"..") {
      return failure(ExpansionError{ErrorCode::PathTraversal, std::nullopt, components[k].offset},
                     counter);
    }
    auto finalized = finalizeComponent(components[k].text, last ? request.extension : std::nullopt,
                                       last ? request.maxLength : std::nullopt);
    if (auto *error = std::get_if<ExpansionError>(&finalized)) return failure(*error, counter);
    if (k > 0) result.push_back(U'/');
    result += std::get<Text>(finalized);
  }
  return ExpansionResult{toQString(result), nextCounter};
}

ExpansionResult preview(const ExpansionRequest &request, ExpansionContext context) {
  auto random = makePreviewRandomSource();
  context.random = random.get();
  return expand(request, context);
}

namespace {

class SystemRandomSource final : public RandomSource {
public:
  std::optional<std::uint8_t> next() override {
    return static_cast<std::uint8_t>(QRandomGenerator::system()->bounded(256));
  }
};

class PreviewRandomSource final : public RandomSource {
public:
  std::optional<std::uint8_t> next() override { return m_next++; }

private:
  std::uint8_t m_next = 0;
};

class FixedRandomSource final : public RandomSource {
public:
  explicit FixedRandomSource(QByteArray bytes) : m_bytes(std::move(bytes)) {}
  std::optional<std::uint8_t> next() override {
    if (m_position >= m_bytes.size()) return std::nullopt;
    return static_cast<std::uint8_t>(m_bytes.at(m_position++));
  }

private:
  QByteArray m_bytes;
  qsizetype m_position = 0;
};

}  // namespace

std::unique_ptr<RandomSource> makeSystemRandomSource() { return std::make_unique<SystemRandomSource>(); }
std::unique_ptr<RandomSource> makePreviewRandomSource() { return std::make_unique<PreviewRandomSource>(); }
std::unique_ptr<RandomSource> makeFixedRandomSource(QByteArray bytes) {
  return std::make_unique<FixedRandomSource>(std::move(bytes));
}

}  // namespace xerahs::naming
