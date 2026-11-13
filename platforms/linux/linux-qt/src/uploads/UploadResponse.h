// CORE-020, CORE-021, COMPCAT-011: data-only response templates.
#pragma once
#include "UploadDestination.h"
#include <QMetaType>
namespace xerahs::uploads {
using UploadHeaders = QList<QPair<QByteArray, QByteArray>>;
struct UploadOutcome {
  QString url;
  QString thumbnailUrl;
  QString deletionUrl;
  QString errorMessage;
  QString code;
  int httpStatus = 0;
  int attempts = 0; // actual network attempts, not published objects
  // CORE-025: attempts that ended with an actual HTTP status. An attempt that
  // timed out, was aborted, or failed without a status is not answered. The
  // count proves whether every attempt was answered; the last status alone
  // cannot, because a later answered attempt does not repair an earlier
  // unanswered one.
  int answeredAttempts = 0;
};
// XML supports absolute element paths, first matching sibling, no predicates,
// attributes, namespace resolution, external entities or general XPath evaluation.
UploadOutcome parseUploadResponse(const UploadDestination &, int httpStatus, const UploadHeaders &,
                                  const QByteArray &body, const QString &fileName = {});
}
Q_DECLARE_METATYPE(xerahs::uploads::UploadOutcome)
