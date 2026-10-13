// Native post-capture actions for the linux-qt app: the ActionExecutor that
// PostCapturePipeline drives (PCA-014). Actions without a native
// implementation yet fail honestly with `action-unavailable` (CORE-007).
#pragma once

#include "actions/PostCapturePipeline.h"
#include "CaptureSave.h"

#include <QImage>
#include <QString>

#include <optional>

namespace xerahs::app {
struct CaptureActionPorts {
  // CapturePipeline invokes save/actionStarted on its worker and clipboard on
  // its GUI thread. External observers must respect those native boundaries.
  CaptureSaveHooks save;
  std::function<xerahs::actions::ActionOutcome(const QImage &, const xerahs::actions::CancellationToken &)> clipboard;
  std::function<void(const QString &)> actionStarted;
};

class CaptureActions final : public xerahs::actions::ActionExecutor {
public:
  CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports = {}, std::optional<CaptureFile> inheritedFile = {});

  xerahs::actions::ActionOutcome run(const xerahs::actions::ActionRequest &request) override;
  bool createTemporaryUploadFile() override { return false; }
  void removeTemporaryUploadFile() override {}

  const std::optional<QString> &savedPath() const { return m_savedPath; }
  const std::optional<CaptureFile> &savedFile() const { return m_savedFile; }
  static xerahs::actions::ActionOutcome copyImageToNativeClipboard(const QImage &media);

  // Pending clarification (ROOT-ESCALATE-001): the contract names no code for
  // an action this build cannot perform yet.
  static const QString kActionUnavailable;

private:
  xerahs::actions::ActionOutcome save();
  xerahs::actions::ActionOutcome copyImage(const xerahs::actions::CancellationToken &cancellation);

  QImage m_media;
  QString m_directory;
  QString m_pattern;
  std::optional<QString> m_savedPath;
  std::optional<CaptureFile> m_savedFile;
  CaptureActionPorts m_ports;
};

}  // namespace xerahs::app
