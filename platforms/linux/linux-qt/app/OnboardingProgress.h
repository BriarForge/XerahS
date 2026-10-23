// CORE-008: first-run onboarding progress. Resumable, versioned, private.
//
// Progress is a small envelope {"format": "com.xerahs.app/onboarding", "version": 1}
// on PrivateJsonFile. A missing file means a first launch. A file this build
// cannot trust (corrupt, wrong format, newer version, unsafe owner) starts a
// fresh in-memory guide, is reported, and is never overwritten.
#pragma once

#include "CommandRegistry.h"
#include "storage/PrivateJsonFile.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace xerahs::app {

// Page ids in wizard order. The last page is the summary.
const QStringList &onboardingPages();

struct OnboardingState {
  QString page = QStringLiteral("welcome");
  QStringList completed, skipped;
  bool finished = false;  // set once; a finished guide never auto-opens again
  bool operator==(const OnboardingState &other) const {
    return page == other.page && completed == other.completed && skipped == other.skipped && finished == other.finished;
  }
};

struct OnboardingLoad {
  OnboardingState state;
  QString diagnostic;    // stable code, empty when the file was read or is missing
  bool writable = true;  // false: progress stays in memory and the file is left alone
  bool existed = false;
};

QString onboardingDiagnosticMessage(const QString &code);

class OnboardingStore {
public:
  explicit OnboardingStore(QString path);
  static QString defaultPath();
  // Blocking file I/O; call from a worker (OwnedWork), never the GUI thread.
  OnboardingLoad load();
  QString save(const OnboardingState &state);  // empty on success, else a stable code

private:
  storage::PrivateJsonFile m_file;
  QByteArray m_snapshot;
  bool m_writable = true;
};

// Auto-open only for a plain interactive launch whose progress is readable,
// writable and not finished. --run-workflow, --edit, --capture-* and every
// forwarded or headless activation pass interactiveLaunch = false.
bool autoOpenOnboarding(bool interactiveLaunch, const OnboardingLoad &load);

// The "Setup guide…" command; registered once by the feature installer.
CommandSpec setupGuideCommand();

}  // namespace xerahs::app
