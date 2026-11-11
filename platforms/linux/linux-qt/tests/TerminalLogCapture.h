#pragma once
#include <QDebug>
#include <QStringList>
class TerminalLogCapture final {
public:
  TerminalLogCapture() { active = this; previous = qInstallMessageHandler(collect); }
  ~TerminalLogCapture() { qInstallMessageHandler(previous); active = nullptr; }
  QStringList lines;
private:
  static void collect(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (active && type == QtInfoMsg && message.startsWith("Workflow terminal ")) active->lines.append(message);
    else if (active && active->previous) active->previous(type, context, message);
  }
  inline static TerminalLogCapture *active = nullptr;
  QtMessageHandler previous = nullptr;
};
