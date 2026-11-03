// Native post-capture actions for the linux-qt app: the ActionExecutor that
// PostCapturePipeline drives (PCA-014). Actions without a native
// implementation yet fail honestly with `action-unavailable` (CORE-007).
#pragma once

#include "actions/PostCapturePipeline.h"
#include "CaptureSave.h"
#include "CaptureDelete.h"
#include "EditorSource.h"
#include "ImageRecognition.h"
#include "uploads/CredentialStore.h"
#include "uploads/UploadDestination.h"
#include <QUrl>
#include <memory>

#include <QImage>
#include <QString>
#include <QTemporaryFile>

#include <optional>

namespace xerahs::app {
struct CaptureSaveAsChoice { std::optional<QString> path; QString diagnostic; };
struct CaptureAnnotationChoice {
  std::optional<xerahs::editor::AnnotationDocument> document;
  QString diagnostic;
  bool sidecarIntent = false;
  std::optional<xerahs::editor::CropRegion> sourceRegion = {};
  // Task mode additionally returns an owned rendered result. Legacy annotation
  // ports may omit it; both paths retain document/source validation.
  QImage rendered = {};
  bool cancelTask = false;
  QString partialDiagnostic = {};
};
struct CaptureUploadReview {
  QImage preview;
  QSize originalDimensions;
  QString destinationName;
  QUrl origin;
};
struct CaptureActionPorts {
  // CapturePipeline invokes save/actionStarted on its worker and clipboard on
  // its GUI thread. Its selection bridge waits on the worker for asynchronous
  // GUI completion. External observers must respect those native boundaries.
  CaptureSaveHooks save;
  // Empty path uses the application's settings directory. Lookups and request
  // preparation run on the pipeline worker; tests may supply an in-memory store.
  QString uploadCatalogPath;
  std::shared_ptr<xerahs::uploads::CredentialStore> uploadCredentials;

  std::function<xerahs::actions::ActionOutcome(const QImage &, const xerahs::actions::CancellationToken &)> clipboard;
  // PCA-024: plain-text clipboard; like clipboard it runs on the GUI thread.
  std::function<xerahs::actions::ActionOutcome(const QString &, const xerahs::actions::CancellationToken &)> textClipboard;
  // POST-CAPTURE-ACTIONS-001 pin/print. Like clipboard, both run on the GUI
  // thread; an empty port uses the native PinWindow / editor print path.
  // `print` receives the prepared source and returns the real outcome: Cancelled
  // when the user cancels the dialog, Failed with a stable code on printer failure.
  std::function<xerahs::actions::ActionOutcome(const QImage &, const xerahs::actions::CancellationToken &)> pin;
  std::function<xerahs::actions::ActionOutcome(const LoadedSource &, const xerahs::actions::CancellationToken &)> print;
  std::function<void(const QString &)> actionStarted;
  // Internal worker-to-GUI selection bridge supplied by CapturePipeline.
  std::function<xerahs::actions::ActionOutcome(const QString &, const xerahs::actions::CancellationToken &)> selection;
  // Worker bridge replaced by CapturePipeline; the injected actual boundary
  // runs on the GUI, like clipboard. It must report its actual outcome.
  std::function<xerahs::actions::ActionOutcome(const QString &, const CaptureFile &, const xerahs::actions::CancellationToken &)> fileOutput;
  std::function<CaptureSaveAsChoice(const QString &, const xerahs::actions::CancellationToken &)> chooseSaveAs;
  std::function<CaptureAnnotationChoice(const LoadedSource &, const xerahs::actions::CancellationToken &)> annotation;
  xerahs::editor::RectangleStyle annotationStyle;
  std::function<QJsonObject()> editorPreferences; // GUI-only, sampled once on open
  std::function<void(const std::vector<CaptureFile> &)> rasterPublished;
  // Empty selects the system temporary directory. The managed PNG is private
  // and belongs to this executor, never to saved capture history.
  QString uploadTemporaryDirectory;
  // GUI-only owned-value presentation. The service keeps its own values if
  // successful. Empty uses an independent native recognition result window.
  std::function<xerahs::actions::ActionOutcome(ImageRecognitionKind, const QStringList &,
      const xerahs::actions::CancellationToken &)> recognitionResult;
  std::function<xerahs::actions::ActionOutcome(const CaptureUploadReview &,
      const xerahs::actions::CancellationToken &)> beforeUpload;
  CaptureDeleteHooks deletion;

};

class CaptureActions final : public xerahs::actions::ActionExecutor {
public:
  CaptureActions(QImage media, QString directory, QString pattern, CaptureActionPorts ports = {}, std::optional<CaptureFile> inheritedFile = {}, std::vector<CaptureFile> inheritedFiles = {}, std::optional<xerahs::editor::AnnotationDocument> inheritedDocument = {}, bool sidecarIntent = false);

  xerahs::actions::ActionOutcome run(const xerahs::actions::ActionRequest &request) override;
  bool createTemporaryUploadFile() override;
  void removeTemporaryUploadFile() override { m_uploadFile.reset(); }

  const std::optional<QString> &savedPath() const { return m_savedPath; }
  const std::optional<CaptureFile> &savedFile() const { return m_savedFile; }
  std::vector<CaptureFile> savedFiles() const;
  const QImage &workingMedia() const { return m_media; }
  const std::optional<xerahs::editor::AnnotationDocument> &annotationDocument() const { return m_annotationDocument; }
  bool sidecarIntent() const { return m_sidecarIntent; }
  static xerahs::actions::ActionOutcome copyImageToNativeClipboard(const QImage &media);
  static xerahs::actions::ActionOutcome copyTextToNativeClipboard(const QString &text);
  static xerahs::actions::ActionOutcome pinToNativeScreen(const QImage &media);
  static xerahs::actions::ActionOutcome printNativeImage(const LoadedSource &source);
  static xerahs::actions::ActionOutcome showNativeRecognition(ImageRecognitionKind, const QStringList &);
  static xerahs::actions::ActionOutcome confirmNativeUpload(const CaptureUploadReview &, const xerahs::actions::CancellationToken &);
  static QStringList selectableActions();
  static QString actionLabel(const QString &id);

  // Pending clarification (ROOT-ESCALATE-001): the contract names no code for
  // an action this build cannot perform yet.
  static const QString kActionUnavailable;

private:
  xerahs::actions::ActionOutcome save(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome finishSave(const QString &action, const CaptureFile &raster, const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome failedSave(const QString &action, const QString &diagnostic) const;
  xerahs::actions::ActionOutcome annotate(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome saveAs(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome copyImage(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome pin(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome print(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome outputFile(const xerahs::actions::ActionRequest &request);
  xerahs::actions::ActionOutcome removeFile(const xerahs::actions::ActionRequest &request);

  xerahs::actions::ActionOutcome upload(const xerahs::actions::ActionRequest &request);
  xerahs::actions::ActionOutcome copyUrl(const xerahs::actions::CancellationToken &cancellation);
  xerahs::actions::ActionOutcome recognize(ImageRecognitionKind, const xerahs::actions::CancellationToken &);
  xerahs::actions::ActionOutcome copyRecognizedText(const xerahs::actions::CancellationToken &);
  xerahs::actions::ActionOutcome beforeUpload(const xerahs::actions::CancellationToken &);
  std::optional<xerahs::uploads::UploadDestination> m_confirmedUploadDestination;
  QString m_recognizedText;
  QString m_uploadUrl;
  std::unique_ptr<QTemporaryFile> m_uploadFile;
  QImage m_media;
  QString m_directory;
  QString m_pattern;
  std::optional<QString> m_savedPath;
  std::optional<CaptureFile> m_savedFile;
  std::optional<CaptureFile> m_savedAsFile;
  CaptureActionPorts m_ports;
  std::optional<xerahs::editor::AnnotationDocument> m_annotationDocument;
  bool m_sidecarIntent = false;
  std::vector<CaptureFile> m_sidecars;
  QStringList m_currentPublications;
};

}  // namespace xerahs::app
