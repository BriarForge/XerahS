#pragma once
#include "recording/Recording.h"
namespace xerahs::app {
// One redacted terminal presentation for recording log, progress and notifications.
// History is committed by Session before finished is emitted.
class RecordingObservation final : public QObject {
  Q_OBJECT
public:
  explicit RecordingObservation(recording::Session &session, QObject *parent = nullptr);
signals:
  void terminalObserved(const QJsonObject &observation);
  void activityChanged(const QString &text);
  void notify(const QString &title, const QString &body, bool success);
};
}
