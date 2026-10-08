// FILENAME-GENERATION-001: deterministic filename token expansion and portable
// sanitization. Normative behaviour lives in
// product-contract/capabilities/FILENAME-GENERATION-001/SPEC.md; requirement IDs
// in comments refer to that file.
#pragma once

#include <QDate>
#include <QLocale>
#include <QString>
#include <QTime>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>

namespace xerahs::naming {

enum class ParseMode { Filename, Path, Text, Url };

// FN-021: ordered byte source consumed left to right across one parse.
class RandomSource {
public:
  virtual ~RandomSource() = default;
  // Returns nullopt when the source is exhausted.
  virtual std::optional<std::uint8_t> next() = 0;
};

// Production source backed by the operating system CSPRNG.
std::unique_ptr<RandomSource> makeSystemRandomSource();
// FN-027: 0x00, 0x01, ... wrapping from 0xFF to 0x00.
std::unique_ptr<RandomSource> makePreviewRandomSource();
// Fixed byte sequence, used by conformance and tests.
std::unique_ptr<RandomSource> makeFixedRandomSource(QByteArray bytes);

// FN-009: `%rf` reads only files the caller has granted.
struct RandomFileRead {
  enum class Status { Granted, Denied, Missing };
  Status status = Status::Missing;
  QByteArray content;
};

class RandomFileAccess {
public:
  virtual ~RandomFileAccess() = default;
  virtual RandomFileRead read(const QString &path) = 0;
};

// FN-001, FN-006: one immutable context per parse.
struct ExpansionContext {
  QDate date;              // wall-clock fields of local_time, as supplied (FN-020)
  QTime time;
  qint64 unixTime = 0;     // whole seconds (FN-020)
  QLocale locale;
  qint64 counter = 0;      // zero-based, non-negative (FN-025)
  std::optional<qint64> width;
  std::optional<qint64> height;
  std::optional<QString> windowTitle;
  std::optional<QString> processName;
  std::optional<QString> userName;
  std::optional<QString> loginDomain;
  std::optional<QString> computerName;
  RandomSource *random = nullptr;
  RandomFileAccess *files = nullptr;
};

struct ExpansionRequest {
  QString pattern;
  ParseMode mode = ParseMode::Filename;
  std::optional<QString> extension;  // without a leading '.'
  std::optional<qint64> maxLength;   // Unicode scalar values (FN-014)
};

// FN-026 error codes.
enum class ErrorCode {
  InvalidArgument,
  UnterminatedArgument,
  RandomSourceExhausted,
  FilePermissionDenied,
  FileUnavailable,
  PathNotRelative,
  PathTraversal,
  CounterOverflow,
  NameTooLong,
};

QString errorCodeName(ErrorCode code);

struct ExpansionError {
  ErrorCode code;
  std::optional<QString> token;  // nullopt for path-structure errors
  qint64 offset = 0;             // Unicode scalar values from the pattern start
};

struct ExpansionResult {
  std::variant<QString, ExpansionError> outcome;
  qint64 nextCounter = 0;  // unchanged on error

  bool ok() const { return std::holds_alternative<QString>(outcome); }
  const QString &value() const { return std::get<QString>(outcome); }
  const ExpansionError &error() const { return std::get<ExpansionError>(outcome); }
};

// Expands a pattern. The caller commits nextCounter only after it reserves
// the name (FN-016).
ExpansionResult expand(const ExpansionRequest &request, const ExpansionContext &context);

// FN-015, FN-027: same rules as expand with the preview random source. The
// returned nextCounter is what execution would commit; nothing is committed.
ExpansionResult preview(const ExpansionRequest &request, ExpansionContext context);

}  // namespace xerahs::naming
