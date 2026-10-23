// CORE-008: the native first-run setup guide. Non-modal so capture, shortcut
// and upload windows it opens can run. Every page but the informational ones
// can be skipped, and declining changes nothing: core local capture keeps its
// defaults (save and copy to the clipboard, nothing uploaded).
#pragma once

#include "OnboardingProgress.h"

#include <QJsonObject>
#include <QStringList>
#include <QWizard>

#include <functional>

class QLabel;
class QPushButton;
class QTimer;

namespace xerahs::app {

struct OnboardingWorkflowInfo {
  QStringList actions;  // ordered action ids of the default workflow
  QString folder, pattern;
};

struct OnboardingPorts {
  std::function<QJsonObject()> settings;                     // current profile values
  std::function<QString(const QJsonObject &)> saveSettings;  // returns an error text, empty on success
  QString defaultFolder;
  std::function<OnboardingWorkflowInfo()> defaultWorkflow;
  std::function<void()> testCapture;                         // starts a real full-screen capture; answer via reportCaptureResult
  std::function<void(const QString &commandId)> runCommand;  // triggers a registry command
  std::function<void(const OnboardingState &)> persist;      // called on every progress change; must not block
};

// "Captures are saved to X and copied to the clipboard; nothing is uploaded."
QString describeDefaultWorkflow(const OnboardingWorkflowInfo &info);

class OnboardingWizard final : public QWizard {
  Q_OBJECT
public:
  OnboardingWizard(OnboardingPorts ports, OnboardingState initial, QString notice = {}, QWidget *parent = nullptr);
  const OnboardingState &state() const { return m_state; }
  bool testCapturePending() const { return m_testPending; }
  QString currentPageName() const;
  bool skippable(const QString &page) const;
  void skipCurrent();
  void finishLater();

public slots:
  // The true outcome of the test capture; the message is shown unmodified.
  void reportCaptureResult(bool success, const QString &message);

private:
  void markCompleted(const QString &page);
  void markSkipped(const QString &page);
  void changed();
  void buildPages(const QString &notice);
  void startTestCapture();

  OnboardingPorts m_ports;
  OnboardingState m_state;
  bool m_skipping = false, m_testPending = false;
  QPushButton *m_testButton = nullptr;
  QLabel *m_testStatus = nullptr;
  QTimer *m_watchdog = nullptr;
};

}  // namespace xerahs::app
