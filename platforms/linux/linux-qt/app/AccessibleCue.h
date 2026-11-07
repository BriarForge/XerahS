// One route for the accessible state cues the native surfaces raise (alerts, description changes).
// Qt drops QAccessible::updateAccessibility() while no assistive client is active, and on some
// platforms (Qt 6.4 offscreen, X11 without an AT-SPI bus) it cannot be activated at all, so an
// observer lets tests see exactly which cue the production code raised.
#pragma once
#include <QAccessible>
namespace xerahs::app {
using AccessibleCueObserver = void (*)(QObject *target, QAccessible::Event type);
inline AccessibleCueObserver &accessibleCueObserver() { static AccessibleCueObserver observer = nullptr; return observer; }
inline void raiseAccessibleCue(QObject *target, QAccessible::Event type) {
  if (auto observer = accessibleCueObserver()) observer(target, type);
  QAccessibleEvent event(target, type);
  QAccessible::updateAccessibility(&event);
}
}
