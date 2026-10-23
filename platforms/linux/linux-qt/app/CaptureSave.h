#pragma once
#include "actions/PipelineResultJson.h"
#include "image-editor/AnnotationDocument.h"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <optional>
class QIODevice;
namespace xerahs::app {
// Native PCA-007 artifact. Keep private paths/context out of ordinary diagnostics.
struct CaptureFileIdentity {
  quint64 device = 0, inode = 0;
  qint64 length = 0, modifiedSeconds = 0, modifiedNanos = 0, changedSeconds = 0, changedNanos = 0;
  bool operator==(const CaptureFileIdentity &other) const;
};
// This live native identity is never serialized or reconstructed from history.
struct CaptureFile : xerahs::actions::SavedFileArtifact {
  CaptureFile(QString path, QString type, qint64 length, QJsonObject context)
      : SavedFileArtifact{std::move(path), std::move(type), length, std::move(context)} {}
  std::optional<CaptureFileIdentity> identity;
};
std::optional<CaptureFileIdentity> captureFileIdentity(int descriptor);
QString validateCaptureFile(const CaptureFile &file);
// Hash only an unchanged, privately owned publication through its bound
// descriptor. Never borrow pixels or authority from an arbitrary path.
QString captureFileHash(const CaptureFile &file, const xerahs::actions::CancellationToken &cancellation);
// Default location of the persisted filename counters (app config folder).
QString defaultCounterPath();
struct CaptureSaveResult {
  std::optional<CaptureFile> file;
  QString diagnostic;
  bool cancelled = false;
};
// Native I/O ports: tests still execute real encoding/publication by default.
struct CaptureSaveHooks {
  std::function<bool(const QImage &, QIODevice *)> encode;
  std::function<void(const QString &)> beforePublish;
  // Worker write-ahead boundary, called before the observer above. The writer
  // supplies artifact kind explicitly; no path suffix grants authority.
  std::function<void(const QString &, bool companion)> publicationIntent = {};
  // FN-016: workflow id whose persisted counter a counter token uses; empty
  // means an ad-hoc save with a non-persisted counter. Empty path = app config.
  QString counterProfile = {}, counterPath = {};
};
CaptureSaveResult saveCapture(const QImage &media, const QString &directory, const QString &pattern,
                              const CaptureSaveHooks &hooks = {});
CaptureSaveResult saveCaptureAs(const QImage &media, const QString &path, const xerahs::actions::CancellationToken &cancellation,
                               const CaptureSaveHooks &hooks = {});
struct CaptureSidecarResult {
  bool succeeded = false;
  std::optional<CaptureFile> file;
  QString diagnostic;
};
CaptureSidecarResult saveCaptureSidecar(const CaptureFile &raster, const xerahs::editor::AnnotationDocument &document,
    const xerahs::actions::CancellationToken &cancellation, const CaptureSaveHooks &hooks = {});
} // namespace xerahs::app
