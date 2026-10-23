#pragma once
#include "recording/Recording.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusUnixFileDescriptor>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#ifdef XERAHS_GSTREAMER
#include <gst/gst.h>
#endif
namespace xerahs::app {
class RecordingController : public QObject {
  Q_OBJECT
public:
  explicit RecordingController(QObject *parent = nullptr);
  ~RecordingController() override;
  QString unavailable(recording::Format format) const;
  bool busy() const;
  recording::State state() const;
  QString indicator() const;
  void start(recording::Request request);
  void pauseResume();
  void stop();
  void abort();
signals:
  void changed();
  void notify(const QString &title, const QString &body, bool success);
private slots:
  void response(uint code, const QVariantMap &results, const QDBusMessage &message);
  void closed();
private:
  void call(const QString &method, QVariantList arguments, QVariantMap options);
  void encode(uint node);
  void poll();
  void fail(const QString &code);
  void teardown();
  recording::Request m_request;
  std::unique_ptr<recording::Session> m_session;
  recording::Output m_output;
  recording::Encoder m_encoder;
  QSet<QString> m_elements;
  QDBusConnection m_bus = QDBusConnection::sessionBus();
  QString m_owner, m_path, m_sessionPath, m_method;
  QDBusUnixFileDescriptor m_fd;
  QTimer m_poll, m_deadline;
  QElapsedTimer m_clock;
  qint64 m_elapsed = 0;
  int m_generation = 0;
#ifdef XERAHS_GSTREAMER
  GstElement *m_pipeline = nullptr;
#endif
};
}
