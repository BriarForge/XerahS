#pragma once
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QVariantMap>
#include <QSet>
#include <QStringList>
#include <functional>
#include <optional>

namespace xerahs::recording {
enum class State { Idle, RequestingPermission, Starting, Recording, Paused, Stopping, Completed, Cancelled, Failed, PermissionDenied };
enum class Event { Request, Granted, Started, Pause, Resume, Stop, Finalized, Abort, Error, Denied, Cancel };
bool terminal(State state);
QString stateName(State state);
class Session : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  State state() const { return m_state; }
  bool send(Event event, const QString &diagnostic = {});
signals:
  void changed();
  void finished(xerahs::recording::State state, const QString &diagnostic);
private:
  State m_state = State::Idle;
};
enum class Format { Mp4, WebM, Gif };
enum class Target { Monitor, Window };
struct Request {
  Target target = Target::Monitor;
  std::optional<QRect> crop; // resolved stream pixels
  QSize streamSize;
  std::optional<QRectF> region; // desktop logical bounds from the shared overlay
  QString systemAudioDevice, microphoneDevice;
  int frameRate = 30;
  bool cursor = true;
  Format format = Format::Mp4;
  int quality = 75; // 0..100
  QString folder, filename;
};
// CORE-017/018: geometry and explicit input selection, independent of native I/O.
struct CropMapping { QRect rectangle; QString diagnostic; };
CropMapping mapCrop(const QRectF &region, const QRectF &desktopStream, QSize streamSize);
struct AudioDevice { QString id, label; bool monitor = false; };
using DeviceMonitor = std::function<QVector<AudioDevice>()>;
AudioDevice audioDevice(const QVariantMap &properties, const QString &label);
struct AudioSelection { QVector<AudioDevice> devices; QStringList diagnostics; };
AudioSelection selectAudio(const Request &request, const DeviceMonitor &monitor);
QString extension(Format format);
QString validate(const Request &request);
QString generatedFilename(Format format);
struct Encoder { QString element; QStringList required; QString diagnostic; };
Encoder selectEncoder(Format format, const QSet<QString> &available);
// Source and output are native boundary parameters, never user pipeline text.
QString pipeline(const Request &request, const Encoder &encoder, const QString &source, int audioInputs = 0, const QString &audioEncoder = {});
class Output {
public:
  ~Output();
  QString reserve(const Request &request);
  QString temporary() const { return m_temporary; }
  QString destination() const { return m_destination; }
  QString publish();
  void abort();
private:
  QString m_temporary, m_destination;
};
} // namespace xerahs::recording
Q_DECLARE_METATYPE(xerahs::recording::State)
