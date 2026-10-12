#pragma once
#include "actions/PipelineResultJson.h"
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <optional>
class QIODevice;
namespace xerahs::app {
// Native PCA-007 artifact. Keep private paths/context out of ordinary diagnostics.
using CaptureFile = xerahs::actions::SavedFileArtifact;
struct CaptureSaveResult {
  std::optional<CaptureFile> file;
  QString diagnostic;
};
// Native I/O ports: tests still execute real encoding/publication by default.
struct CaptureSaveHooks {
  std::function<bool(const QImage &, QIODevice *)> encode;
  std::function<void(const QString &)> beforePublish;
};
CaptureSaveResult saveCapture(const QImage &media, const QString &directory, const QString &pattern,
                              const CaptureSaveHooks &hooks = {});
} // namespace xerahs::app
