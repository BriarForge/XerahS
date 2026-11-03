// Qt Core-only presentation table for the stable interface error codes (CLI,
// activation IPC, workflow discovery and execution lookup; IFCAT-016). One row
// per code gives the fixed user text, process exit status and retry rule, so
// every surface presents a code the same way and never a raw payload.
#pragma once
#include <QString>
#include <vector>

namespace xerahs::shell {
// Where a code can be produced. A code may belong to several surfaces.
enum ErrorSurface : unsigned {
  WireReplySurface = 1,   // error field of an authenticated activation reply
  LaunchSurface = 2,      // CLI launch acknowledgement / refusal
  DiscoverySurface = 4,   // --list-workflows
  ExecutionSurface = 8,   // --workflow-result
};
// Safe-retry rule. Retry is offered (automatically or in wording) only for Safe.
enum class RetryAdvice {
  Safe,         // Nothing started or only a read failed: the identical request may be repeated now.
  AfterChange,  // The identical request fails the same way until input, configuration or environment changes.
  CheckFirst,   // The outcome is unknown: inspect state first; never repeat automatically.
};
struct ErrorPresentation {
  const char *code;
  unsigned surfaces;
  int exitCode;       // Process status when the code ends a CLI invocation.
  RetryAdvice retry;
  const char *message;  // Fixed text: no arguments, paths, payloads or traces.
};
const std::vector<ErrorPresentation> &errorPresentations();
const ErrorPresentation *findErrorPresentation(const QString &code);
bool isStableErrorCode(const QString &code, unsigned surface);
// Total functions: an unknown code gets the generic text, exit 1 and CheckFirst.
QString errorMessage(const QString &code);
int errorExitCode(const QString &code);
RetryAdvice retryAdvice(const QString &code);
inline bool retryIsSafe(const QString &code) { return retryAdvice(code) == RetryAdvice::Safe; }
} // namespace xerahs::shell
