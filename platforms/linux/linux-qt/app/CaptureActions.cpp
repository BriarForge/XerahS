#include "CaptureActions.h"

#include "naming/FilenameGenerator.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>

namespace xerahs::app {

using namespace xerahs::actions;
using namespace xerahs::naming;

namespace {

// %rf is not offered by the capture pattern; no file is ever granted.
class NoFileAccess final : public RandomFileAccess {
public:
  RandomFileRead read(const QString &) override { return {RandomFileRead::Status::Denied, {}}; }
};

ActionOutcome failed(const QString &diagnostic) {
  return ActionOutcome{ActionState::Failed, diagnostic, std::nullopt};
}

}  // namespace

const QString CaptureActions::kActionUnavailable = QStringLiteral("action-unavailable");

CaptureActions::CaptureActions(QImage media, QString directory, QString pattern)
    : m_media(std::move(media)), m_directory(std::move(directory)), m_pattern(std::move(pattern)) {}

ActionOutcome CaptureActions::run(const ActionRequest &request) {
  if (request.spec.id == QStringLiteral("save")) return save();
  if (request.spec.id == QStringLiteral("image-clipboard")) return copyImage();
  return failed(kActionUnavailable);
}

ActionOutcome CaptureActions::save() {
  if (!QDir().mkpath(m_directory)) return failed(QStringLiteral("save-directory-unavailable"));
  auto random = makeSystemRandomSource();
  NoFileAccess files;
  const QDateTime now = QDateTime::currentDateTime();
  ExpansionContext context;
  context.date = now.date();
  context.time = now.time();
  context.unixTime = now.toSecsSinceEpoch();
  context.locale = QLocale::system();
  context.counter = 0;
  context.width = m_media.width();
  context.height = m_media.height();
  context.random = random.get();
  context.files = &files;

  // A random-token collision is retried with fresh randomness; an existing
  // file is never overwritten.
  for (int attempt = 0; attempt < 3; ++attempt) {
    const ExpansionResult name = expand({m_pattern, ParseMode::Filename, QStringLiteral("png"), std::nullopt}, context);
    if (!name.ok()) return failed(QStringLiteral("filename-") + errorCodeName(name.error().code));
    const QString path = QDir(m_directory).filePath(name.value());
    if (QFileInfo::exists(path)) continue;
    // PCA-007: atomic write; the absolute native path is the save.file artifact.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !m_media.save(&file, "PNG") || !file.commit()) {
      return failed(QStringLiteral("save-write-failed"));
    }
    m_savedPath = QFileInfo(path).absoluteFilePath();
    return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
  }
  return failed(QStringLiteral("save-name-collision"));
}

ActionOutcome CaptureActions::copyImage() {
  QClipboard *clipboard = QGuiApplication::clipboard();
  clipboard->setImage(m_media);
  // CORE-007: report success only when this process actually owns the
  // clipboard afterwards.
  if (!clipboard->ownsClipboard()) return failed(QStringLiteral("clipboard-unavailable"));
  return ActionOutcome{ActionState::Succeeded, std::nullopt, std::nullopt};
}

}  // namespace xerahs::app
