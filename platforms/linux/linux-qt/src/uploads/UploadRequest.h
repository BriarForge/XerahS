// CORE-013, CORE-020, CORE-021, COMPCAT-011: bounded HTTP preparation.
#pragma once
#include "CredentialStore.h"
#include "UploadDestination.h"
#include <QUrl>

namespace xerahs::uploads {
struct UploadInput {
  QString fileName;
  QByteArray fileBytes;
  QString filePath;
  QString mimeType;
};
struct UploadRequestOptions {
  qint64 maxInputBytes = 200LL * 1024 * 1024;
  std::optional<quint32> randomSeed;
  bool allowCredentialPrompts = true;
};
struct PreparedUploadRequest {
  QByteArray method;
  QUrl url;
  QVector<Field> headers;
  QByteArray body;
  QByteArray contentType;
  UploadDestination destination;
  QString fileName;
  // Transient only: used to redact server and transport diagnostics.
  QStringList sensitiveValues;
};
// Codes: destination validation codes, missing-secret, credential-unavailable,
// input-too-large, input-unreadable, invalid-header, invalid-content-type.
struct UploadRequestResult {
  std::optional<PreparedUploadRequest> request;
  QString code;
  QString errorMessage;
};
// SETCAT-016 / IFCAT-015: local-only preparation of a content-free HEAD probe.
// Credentials resolve only inside the destination ID's existing keyring scope.
UploadRequestResult buildConnectionTestRequest(const UploadDestination &, const CredentialStore &);
UploadRequestResult buildUploadRequest(const UploadDestination &, const UploadInput &, const CredentialStore &,
                                       const UploadRequestOptions & = {});
}  // namespace xerahs::uploads
