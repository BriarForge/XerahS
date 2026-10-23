#include "RecordingActions.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QSpinBox>
#include <QSystemTrayIcon>
namespace xerahs::app {
using namespace recording;
RecordingActions::RecordingActions(RecordingController &controller, QMenu &menu, QSystemTrayIcon &tray,
                                 std::function<QString()> folder, QObject *parent)
    : QObject(parent), m_controller(controller), m_tray(tray), m_folder(std::move(folder)), m_icon(tray.icon()) {
  for (const auto &entry : QList<QPair<QString, QString>>{{"start-region-recording", "Record region…"}, {"start-recording", "Start screen recording…"}, {"start-gif-recording", "Start GIF recording…"}, {"pause-recording", "Pause recording"}, {"stop-recording", "Stop recording"}, {"abort-recording", "Abort recording"}}) {
    auto *action = menu.addAction(entry.second); action->setObjectName(entry.first); m_actions.insert(entry.first, action);
  }
  connect(m_actions["start-region-recording"], &QAction::triggered, this, [this] { start(Format::Mp4, true); });
  connect(m_actions["start-recording"], &QAction::triggered, this, [this] { start(Format::Mp4); });
  connect(m_actions["start-gif-recording"], &QAction::triggered, this, [this] { start(Format::Gif); });
  connect(m_actions["pause-recording"], &QAction::triggered, &controller, &RecordingController::pauseResume);
  connect(m_actions["stop-recording"], &QAction::triggered, &controller, &RecordingController::stop);
  connect(m_actions["abort-recording"], &QAction::triggered, &controller, &RecordingController::abort);
  connect(&controller, &RecordingController::changed, this, &RecordingActions::refresh); refresh();
}
void RecordingActions::refresh() {
  for (const auto &id : {QString("start-recording"), QString("start-region-recording"), QString("start-gif-recording")}) {
    QString reason = m_controller.unavailable(id != "start-gif-recording" ? Format::Mp4 : Format::Gif);
    if (id != "start-gif-recording" && !reason.isEmpty() && m_controller.unavailable(Format::WebM).isEmpty()) reason.clear();
    auto *action = m_actions[id]; action->setEnabled(!m_controller.busy() && reason.isEmpty()); action->setToolTip(reason);
    action->setStatusTip(reason); action->setText((id == "start-region-recording" ? QStringLiteral("Record region…") : id == "start-recording" ? QStringLiteral("Start screen recording…") : QStringLiteral("Start GIF recording…")) + (reason.isEmpty() ? QString{} : " (" + reason + ')'));
  }
  const auto state = m_controller.state();
  m_actions["pause-recording"]->setEnabled(state == State::Recording || state == State::Paused);
  m_actions["pause-recording"]->setText(state == State::Paused ? "Resume recording" : "Pause recording");
  m_actions["stop-recording"]->setEnabled(state == State::Recording || state == State::Paused);
  m_actions["abort-recording"]->setEnabled(m_controller.busy());
  m_tray.setToolTip(m_controller.busy() ? m_controller.indicator() : QStringLiteral("XerahS"));
  if (m_controller.busy()) {
    QPixmap pixmap = m_icon.pixmap(32, 32); QPainter painter(&pixmap); painter.setPen(Qt::white); painter.setBrush(Qt::red); painter.drawEllipse(20, 20, 11, 11); painter.end(); m_tray.setIcon(QIcon(pixmap));
  } else m_tray.setIcon(m_icon);
}
void RecordingActions::start(Format format, bool region) {
  if (m_controller.busy()) return;
  QDialog dialog; dialog.setWindowTitle(region ? "Record region" : "Screen recording"); QFormLayout layout(&dialog);
  QComboBox target; target.addItems({"Monitor (choose in portal)", "Window (choose in portal)"}); layout.addRow("Target", &target); target.setEnabled(!region);
  QComboBox formats;
  for (auto f : {Format::Mp4, Format::WebM, Format::Gif}) if (m_controller.unavailable(f).isEmpty()) formats.addItem(f == Format::Mp4 ? "MP4 / H.264" : f == Format::WebM ? "WebM / VP9" : "GIF", int(f));
  if (!formats.count()) return;
  formats.setCurrentIndex(qMax(0, formats.findData(int(format)))); layout.addRow("Format", &formats);
  QSpinBox fps; fps.setRange(1, 120); fps.setValue(30); layout.addRow("Frames per second", &fps);
  QSpinBox quality; quality.setRange(0, 100); quality.setValue(75); layout.addRow("Quality (GIF uses encoder palette)", &quality);
  QCheckBox cursor("Show cursor"); cursor.setChecked(true); layout.addRow(&cursor);
  QLineEdit folder(m_folder()); layout.addRow("Output folder", &folder);
  QLineEdit filename(generatedFilename(Format(formats.currentData().toInt()))); layout.addRow("Filename", &filename);
  connect(&formats, &QComboBox::currentIndexChanged, &dialog, [&] { filename.setText(generatedFilename(Format(formats.currentData().toInt()))); });
  QLabel note("Audio inputs are selected in Settings → Recording (none by default). GIF has no audio. For a region, confirm the rectangle, then choose its monitor in the portal. Pause excludes paused time. Stop finalizes the file; Abort removes partial output."); note.setWordWrap(true); layout.addRow(&note);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  Request request; request.target = target.currentIndex() ? Target::Window : Target::Monitor; request.format = Format(formats.currentData().toInt());
  request.frameRate = fps.value(); request.quality = quality.value(); request.cursor = cursor.isChecked(); request.folder = folder.text(); request.filename = filename.text();
  if (m_settings) {
    const auto settings = m_settings();
    request.systemAudioDevice = settings.value("systemAudioDevice").toString(); request.microphoneDevice = settings.value("microphoneDevice").toString();
  }
  if (region) {
    if (!m_selectRegion) return;
    m_selectRegion([guard = QPointer<RecordingActions>(this), request](QRectF rectangle) mutable {
      if (!guard) return;
      request.region = rectangle; guard->m_controller.start(request);
    });
  } else m_controller.start(request);
}
}
