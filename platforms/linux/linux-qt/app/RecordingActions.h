#pragma once
#include "RecordingController.h"
#include <QAction>
#include <QList>
#include <QMap>
#include <functional>
#include <optional>
#include <QJsonObject>
class QSystemTrayIcon;
class QDialog;
namespace xerahs::app {
QDialog *makeRecordingResultDialog(const recording::ControlResult &result);
class RecordingActions : public QObject {
  Q_OBJECT
public:
  // Actions are owned here. Callers register them; this type does not insert
  // them into a menu (CORE-007).
  RecordingActions(RecordingController &controller, QSystemTrayIcon &tray,
                   std::function<QString()> folder, QObject *parent = nullptr);
  QMap<QString, QAction *> actions() const { return m_actions; }
  // Registration order, which is the tray order within the Record group.
  QList<QAction *> orderedActions() const { return m_ordered; }
  void start(recording::Format format, bool region = false, QString origin = QStringLiteral("native-shell"));
  // Dispatcher body for an already admitted StartRecording or StartRegionRecording.
  // Its reservation is the caller's, so this must not treat reserved() as foreign.
  void beginAdmittedStart(recording::Format format, bool region, QString origin);
  void armRequestedFormat(recording::Format format) { m_requestedFormat = format; }
  void disarmRequestedFormat() { m_requestedFormat.reset(); }
  recording::Format takeRequestedFormat(recording::Format fallback) {
    const auto chosen = m_requestedFormat.value_or(fallback);
    m_requestedFormat.reset();
    return chosen;
  }
  // True when a reservation is held and this call is not the admitted dispatch body.
  bool blockedByForeignAdmission() const;
  void setForeignAdmission(std::function<bool()> blocked, std::function<void()> refuse) {
    m_foreignAdmission = std::move(blocked); m_refuse = std::move(refuse);
  }
  // Tray, menu and shortcut clicks. The shell route admits; the dispatch body calls beginAdmittedStart.
  void setStartRoute(std::function<void(recording::Format, bool)> route) { m_route = std::move(route); }
  void setInputs(std::function<QJsonObject()> settings, std::function<void(std::function<void(QRectF)>)> selectRegion) {
    m_settings = std::move(settings); m_selectRegion = std::move(selectRegion);
  }
private:
  void refresh();
  void requestStart(recording::Format format, bool region = false);
  bool refuseForeignAdmission();
  RecordingController &m_controller;
  QSystemTrayIcon &m_tray;
  std::function<QString()> m_folder;
  QMap<QString, QAction *> m_actions;
  QList<QAction *> m_ordered;
  QIcon m_icon;
  std::function<QJsonObject()> m_settings;
  std::function<void(std::function<void(QRectF)>)> m_selectRegion;
  std::function<void(recording::Format, bool)> m_route;
  std::function<bool()> m_foreignAdmission;
  std::function<void()> m_refuse;
  std::optional<recording::Format> m_requestedFormat;
  int m_admittedDepth = 0;
};
}
