// EDITOR-UTILITIES-001 / EU-012, EU-017: runner contract implemented natively (GStreamer) and by test fakes.
#pragma once

#include "media/VideoConversion.h"

#include <QObject>

namespace xerahs::media {

class ConversionRunner : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start(const Request &request) = 0;
  virtual void cancel() = 0;           // idempotent; removes partial output
  virtual bool busy() const = 0;
signals:
  void progress(int percent);
  void finished(const xerahs::media::ConversionResult &result);
};

}  // namespace xerahs::media
