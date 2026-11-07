#include "RecordingAudioConsent.h"
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
namespace xerahs::app {
RecordingAudioConsent::RecordingAudioConsent(QString systemDevice, QString microphone, QWidget *parent)
    : QWidget(parent), m_system(std::move(systemDevice)), m_microphone(std::move(microphone)) {
  auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
  auto *note = new QLabel(tr("Allow audio for this recording only. Unchecked inputs are not opened. If a selected input fails, recording stops and any recoverable partial file is retained. Platform permission is still required."), this);
  note->setWordWrap(true); layout->addWidget(note);
  m_systemChoice = new QCheckBox(tr("Record system audio"), this); m_systemChoice->setObjectName("recordSystemAudioConsent");
  m_microphoneChoice = new QCheckBox(tr("Record microphone"), this); m_microphoneChoice->setObjectName("recordMicrophoneConsent");
  for (const auto &entry : {qMakePair(m_systemChoice, m_system), qMakePair(m_microphoneChoice, m_microphone)}) {
    layout->addWidget(entry.first);
    auto *device = new QLabel(entry.second.isEmpty() ? tr("No input selected in Recording settings") : entry.second, this);
    device->setTextFormat(Qt::PlainText); device->setWordWrap(true); layout->addWidget(device);
    entry.first->setAccessibleDescription(device->text());
  }
  setFormat(m_format);
}
void RecordingAudioConsent::setFormat(recording::Format format) {
  m_format = format;
  m_systemChoice->setEnabled(format != recording::Format::Gif && !m_system.isEmpty());
  m_microphoneChoice->setEnabled(format != recording::Format::Gif && !m_microphone.isEmpty());
  // Returning from GIF requires a fresh explicit audio choice.
  if (format == recording::Format::Gif) { m_systemChoice->setChecked(false); m_microphoneChoice->setChecked(false); }
}
void RecordingAudioConsent::apply(recording::Request &request) const {
  request.systemAudioDevice.clear(); request.microphoneDevice.clear(); request.consentedAudioDevices.clear();
  if (m_format == recording::Format::Gif) return;
  if (m_systemChoice->isEnabled() && m_systemChoice->isChecked()) {
    request.systemAudioDevice = m_system; request.consentedAudioDevices << m_system;
  }
  if (m_microphoneChoice->isEnabled() && m_microphoneChoice->isChecked()) {
    request.microphoneDevice = m_microphone; request.consentedAudioDevices << m_microphone;
  }
}
}
