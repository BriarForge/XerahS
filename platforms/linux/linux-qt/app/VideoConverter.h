// EDITOR-UTILITIES-001 / EU-012..EU-014, EU-017: native GStreamer runner for the Video Converter.
// Output goes to a hidden partial file beside the destination and is renamed on success, so a reader
// never sees a half-written video; Cancel and failure delete the partial file.
#pragma once

#include "ConversionRunner.h"
#include "OwnedWork.h"

#include <QTimer>

#include <memory>

typedef struct _GstElement GstElement;

namespace xerahs::app {

// EU-013: real availability port (gst_element_factory_find). Initialises GStreamer on first use.
class GstElementAvailability final : public media::ElementAvailability {
public:
  bool hasElement(const QString &name) const override;
};

class GstConversionRunner final : public media::ConversionRunner {
  Q_OBJECT
public:
  explicit GstConversionRunner(QObject *parent = nullptr);
  ~GstConversionRunner() override;
  void start(const media::Request &request) override;
  void cancel() override;
  bool busy() const override { return m_state != State::Idle; }

private:
  enum class State { Idle, Probing, Running };
  void probed(quint64 generation, media::InputInfo info, bool failed);
  void launch(const media::Request &request, const media::InputInfo &info);
  void poll();
  void finish(media::Outcome outcome, media::ErrorCode code, const QString &message);
  void teardown();

  State m_state = State::Idle;
  quint64 m_generation = 0;
  media::Request m_request;
  QStringList m_diagnostics;
  QString m_partial;
  GstElement *m_pipeline = nullptr;
  QTimer m_timer;
  int m_lastPercent = -1;
  OwnedWork m_probeWork;
};

}  // namespace xerahs::app
