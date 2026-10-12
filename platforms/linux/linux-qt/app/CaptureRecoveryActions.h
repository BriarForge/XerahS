#pragma once
#include "CaptureController.h"
#include <QPointer>
#include <functional>
class QAction;
class QMessageBox;
class QWidget;
namespace xerahs::app {
// The opener receives an immutable image snapshot on the GUI thread. It owns
// preparation/editor lifetime, returns true only for an actual opened editor,
// and reports typed preparation errors; false includes ordinary user cancel.
// No receipt is consumed by opening. Callback exceptions are redacted here.
using CapturedImageOpener = std::function<bool(const QImage &, QWidget *)>;
class CaptureRecoveryActions final : public QObject {
  Q_OBJECT
public:
  CaptureRecoveryActions(CaptureController &capture, QWidget *dialogParent,
                         CapturedImageOpener opener, QObject *parent = nullptr);
  ~CaptureRecoveryActions() override;
  QAction *editAction() const { return m_edit; }
  QAction *discardAction() const { return m_discard; }
signals:
  void notify(const QString &body, bool success);
private:
  void updateAvailability();
  void edit();
  void discard();
  QPointer<CaptureController> m_capture;
  QPointer<QWidget> m_dialogParent;
  QPointer<QMessageBox> m_dialog;
  CapturedImageOpener m_opener;
  QAction *m_edit = nullptr, *m_discard = nullptr;
  quint64 m_operation = 0;
  bool m_running = false;
};
} // namespace xerahs::app
