#pragma once
#include "RecordingController.h"
#include <QAction>
#include <QMap>
#include <functional>
class QMenu;
class QSystemTrayIcon;
namespace xerahs::app {
class RecordingActions : public QObject {
  Q_OBJECT
public:
  RecordingActions(RecordingController &controller, QMenu &menu, QSystemTrayIcon &tray,
                   std::function<QString()> folder, QObject *parent = nullptr);
  QMap<QString, QAction *> actions() const { return m_actions; }
  void start(recording::Format format);
private:
  void refresh();
  RecordingController &m_controller;
  QSystemTrayIcon &m_tray;
  std::function<QString()> m_folder;
  QMap<QString, QAction *> m_actions;
  QIcon m_icon;
};
}
