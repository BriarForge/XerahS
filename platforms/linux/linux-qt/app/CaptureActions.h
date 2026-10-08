// Native post-capture actions for the linux-qt app: the ActionExecutor that
// PostCapturePipeline drives (PCA-014). Actions without a native
// implementation yet fail honestly with `action-unavailable` (CORE-007).
#pragma once

#include "actions/PostCapturePipeline.h"

#include <QImage>
#include <QString>

#include <optional>

namespace xerahs::app {

class CaptureActions final : public xerahs::actions::ActionExecutor {
public:
  CaptureActions(QImage media, QString directory, QString pattern);

  xerahs::actions::ActionOutcome run(const xerahs::actions::ActionRequest &request) override;
  bool createTemporaryUploadFile() override { return false; }
  void removeTemporaryUploadFile() override {}

  const std::optional<QString> &savedPath() const { return m_savedPath; }

  // Pending clarification (ROOT-ESCALATE-001): the contract names no code for
  // an action this build cannot perform yet.
  static const QString kActionUnavailable;

private:
  xerahs::actions::ActionOutcome save();
  xerahs::actions::ActionOutcome copyImage();

  QImage m_media;
  QString m_directory;
  QString m_pattern;
  std::optional<QString> m_savedPath;
};

}  // namespace xerahs::app
