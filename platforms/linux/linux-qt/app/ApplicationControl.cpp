#include "ApplicationControl.h"

#include <QApplication>

namespace xerahs::app {
QString applicationControlAvailability(bool workBusy, bool presentationAllowed) {
  if (!presentationAllowed) return QStringLiteral("presentation-required");
  if (workBusy || QApplication::activeModalWidget()) return QStringLiteral("application-busy");
  return {};
}
}
