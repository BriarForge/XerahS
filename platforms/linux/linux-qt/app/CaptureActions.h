// Native post-capture actions for the linux-qt app: the ActionExecutor that
// PostCapturePipeline drives (PCA-014). Actions without a native
// implementation yet fail honestly with `action-unavailable` (CORE-007).
#pragma once

#include "actions/PostCapturePipeline.h"
#include "CaptureSave.h"
#include "EditorSource.h"

#include <QImage>
#include <QString>

#include <optional>

namespace xerahs::app {
struct CaptureSaveAsChoice { std::optional<QString> path; QString diagnostic; };
struct CaptureAnnotationChoice { std::optional<xerahs::editor::AnnotationDocument> document; QString diagnostic; };
struct CaptureActionPorts {
  // CapturePipeline invokes save/actionStarted on its worker and clipboard on
  // its GUI thread. Its selection bridge waits on the worker for asynchronous
  // GUI completion. External observers must respect those native boundaries.
  CaptureSaveHooks save;
  std::function<xerahs::actions::ActionOutcome(const QImage &, const xerahs::actions::CancellationToken &)> clipboard;
  std::function<void(const QString &)> actionStarted;
  // Internal worker-to-GUI selection bridge supplied by CapturePipeline.
  std::function<xerahs::actions::ActionOutcome(const xerahs::actions::CancellationToken &)> selection;
  // Worker bridge replaced by CapturePipeline; the injected actual boundary
  // runs on the GUI, like clipboard. It must report its actual outcome.
  std::function<xerahs::actions::ActionOutcome(const QString &, const CaptureFile &, const xerahs::actions::CancellationToken &)> fileOutput;
  std::function<CaptureSaveAsChoice(const QString &, const xerahs::actions::CancellationToken &)> chooseSaveAs;
  std::function<CaptureAnnotationChoice(const LoadedSource &, const xerahs::actions::CancellationToken &)> annotation;
  xerahs::editor::RectangleStyle annotationStyle;
};

class CaptureActions final : public xerahs::actions::ActionExecutor {
public:
  CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports = {}, std::optional<CaptureFile> inheritedFile = {}, std::vector<CaptureFile> inheritedFiles = {}, std::optional<xerahs::editor::AnnotationDocument> inheritedDocument = {});

  xerahs::actions::ActionOutcome run(const xerahs::actions::ActionRequest &request) override;
  bool createTemporaryUploadFile() override { return false; }
  void removeTemporaryUploadFile() override {}

  const std::optional<QString> &savedPath() const { return m_savedPath; }
  const std::optional<CaptureFile> &savedFile() const { return m_savedFile; }
  std::vector<CaptureFile> savedFiles() const;
  const QImage &workingMedia() const { return m_media; }
  const std::optional<xerahs::editor::AnnotationDocument> &annotationDocument() const { return m_annotationDocument; }
  static xerahs::actions::ActionOutcome copyImageToNativeClipboard(const QImage &media);
  static QStringList selectableActions();
  static QString actionLabel(const QString &id);

  // Pending clarification (ROOT-ESCALATE-001): the contract names no code for
  // an action this build cannot perform yet.
  static const QString kActionUnavailable;

private:
  xerahs::actions::ActionOutcome save();
  xerahs::actions::ActionOutcome annotate(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome saveAs(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome copyImage(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome outputFile(const xerahs::actions::ActionRequest &request);

  QImage m_media;
  QString m_directory;
  QString m_pattern;
  std::optional<QString> m_savedPath;
  std::optional<CaptureFile> m_savedFile;
  std::optional<CaptureFile> m_savedAsFile;
  CaptureActionPorts m_ports;
  std::optional<xerahs::editor::AnnotationDocument> m_annotationDocument;
};

}  // namespace xerahs::app
