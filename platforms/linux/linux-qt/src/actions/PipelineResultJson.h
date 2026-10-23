#pragma once
#include "actions/PostCapturePipeline.h"
#include <QJsonObject>

namespace xerahs::actions {
// PCA-007: artifact data, not an ordinary diagnostic. Native I/O supplies
// these actual values; serialization never rereads or touches the saved file.
struct SavedFileArtifact {
  QString path;
  QString mediaType;
  qint64 byteLength = 0;
  QJsonObject filenameContext;
  QString artifact = QStringLiteral("save.file");
  QString creatorTaskId = {}; // empty in older receipts; never deletion authority
};

// PCA-016: the four canonical fields from post-capture-result-v1. Additional
// task identity, timing and native file facts are under extensions.com.xerahs.app.
// Optional upload_result contains public URLs and redacted engine messages.
// No media bytes, image clipboard payload, OCR text or executor exception is accepted
// by this boundary. This is a report, not a restart journal or retry authority.
QJsonObject pipelineResultJson(const PipelineResult &result,
                              const std::vector<SavedFileArtifact> &files = {});
} // namespace xerahs::actions
