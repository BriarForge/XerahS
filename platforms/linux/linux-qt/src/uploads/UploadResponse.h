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
};
// XML supports absolute element paths, first matching sibling, no predicates,
// attributes, namespace resolution, external entities or general XPath evaluation.
UploadOutcome parseUploadResponse(const UploadDestination &, int httpStatus, const UploadHeaders &,
                                  const QByteArray &body, const QString &fileName = {});
}
Q_DECLARE_METATYPE(xerahs::uploads::UploadOutcome)
