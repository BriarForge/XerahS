#pragma once
#include "recording/Recording.h"
#include <QWidget>
class QCheckBox;
namespace xerahs::app {
// Per-run choices; saved device preferences never grant recording consent.
class RecordingAudioConsent final : public QWidget {
  Q_OBJECT
public:
  RecordingAudioConsent(QString systemDevice, QString microphone, QWidget *parent = nullptr);
  void setFormat(recording::Format);
  void apply(recording::Request &) const;
private:
  QString m_system, m_microphone;
  QCheckBox *m_systemChoice, *m_microphoneChoice;
  recording::Format m_format = recording::Format::Mp4;
};
}
