// CORE-008: onboarding progress store.
#include "OnboardingProgress.h"

#include <QDir>
#include <QJsonArray>
#include <QStandardPaths>

namespace xerahs::app {
namespace {

storage::StoreOptions options() {
  storage::StoreOptions o;
  o.format = QStringLiteral("com.xerahs.app/onboarding");
  o.maxVersion = 1;
  o.maxBytes = 64 * 1024;
  return o;
}

bool readList(const QJsonValue &value, QStringList *out) {
  if (!value.isArray()) return false;
  for (const auto &item : value.toArray()) {
    const auto id = item.toString();
    if (!item.isString() || !onboardingPages().contains(id) || out->contains(id)) return false;
    out->append(id);
  }
  return true;
}

}  // namespace

const QStringList &onboardingPages() {
  static const QStringList pages{QStringLiteral("welcome"), QStringLiteral("capture-permission"), QStringLiteral("save-location"),
      QStringLiteral("hotkeys"), QStringLiteral("uploads"), QStringLiteral("telemetry"), QStringLiteral("summary")};
  return pages;
}

QString onboardingDiagnosticMessage(const QString &code) {
  if (code == u"onboarding-store-newer") return QStringLiteral("The setup guide progress file was written by a newer XerahS. It was left untouched and this guide starts fresh without saving progress.");
  if (code == u"onboarding-store-invalid") return QStringLiteral("The setup guide progress file is unreadable. It was left untouched and this guide starts fresh without saving progress.");
  if (code == u"onboarding-store-unsafe") return QStringLiteral("The setup guide progress file or its folder is not private to you. It was left untouched and progress is not saved.");
  if (code == u"onboarding-store-changed") return QStringLiteral("The setup guide progress file changed on disk, so this progress was not saved. Reopen Setup guide to continue.");
  if (code == u"onboarding-store-busy") return QStringLiteral("The setup guide progress file is in use. This progress was not saved.");
  return QStringLiteral("The setup guide progress could not be read or saved. Check the XerahS configuration folder.");
}

OnboardingStore::OnboardingStore(QString path) : m_file(std::move(path), options()) {}

QString OnboardingStore::defaultPath() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath(QStringLiteral("onboarding.json"));
}

OnboardingLoad OnboardingStore::load() {
  using storage::StoreStatus;
  OnboardingLoad result;
  const auto read = m_file.read();
  m_snapshot = read.snapshot;
  m_writable = true;
  const auto blocked = [&](const QString &code) {
    m_writable = false; result.writable = false; result.state = {}; result.diagnostic = code; return result;
  };
  switch (read.status) {
    case StoreStatus::Missing: return result;
    case StoreStatus::Ok: break;
    case StoreStatus::NewerVersion: return blocked(QStringLiteral("onboarding-store-newer"));
    case StoreStatus::Unsafe: return blocked(QStringLiteral("onboarding-store-unsafe"));
    case StoreStatus::Invalid: return blocked(QStringLiteral("onboarding-store-invalid"));
    default: return blocked(QStringLiteral("onboarding-store-read-failed"));
  }
  result.existed = true;
  OnboardingState state;
  const auto doc = read.document;
  state.page = doc.value("page").toString();
  if (read.version != 1 || !onboardingPages().contains(state.page) || !readList(doc.value("completed"), &state.completed) ||
      !readList(doc.value("skipped"), &state.skipped) || !doc.value("finished").isBool())
    return blocked(QStringLiteral("onboarding-store-invalid"));
  state.finished = doc.value("finished").toBool();
  result.state = state;
  return result;
}

QString OnboardingStore::save(const OnboardingState &state) {
  using storage::StoreStatus;
  if (!m_writable) return QStringLiteral("onboarding-store-read-only");
  const QJsonObject payload{{"page", state.page}, {"completed", QJsonArray::fromStringList(state.completed)},
      {"skipped", QJsonArray::fromStringList(state.skipped)}, {"finished", state.finished}};
  switch (m_file.write(payload, 1, m_snapshot, &m_snapshot)) {
    case StoreStatus::Ok: return {};
    case StoreStatus::Changed: return QStringLiteral("onboarding-store-changed");
    case StoreStatus::Busy: return QStringLiteral("onboarding-store-busy");
    case StoreStatus::Unsafe: return QStringLiteral("onboarding-store-unsafe");
    default: return QStringLiteral("onboarding-store-write-failed");
  }
}

bool autoOpenOnboarding(bool interactiveLaunch, const OnboardingLoad &load) {
  return interactiveLaunch && load.writable && load.diagnostic.isEmpty() && !load.state.finished;
}

CommandSpec setupGuideCommand() {
  return CommandSpec{QStringLiteral("setup-guide"), QStringLiteral("Setup guide…"), CommandGroup::Preferences,
      CommandSurface::Tray | CommandSurface::PreferencesMenu,
      QStringLiteral("Opens the first-run setup guide: capture permission, save folder, hotkeys, uploads and privacy.")};
}

}  // namespace xerahs::app
