#pragma once

#include <QString>

namespace xerahs::app {
// Shared native control admission. Headless callers must never enter a dialog.
QString applicationControlAvailability(bool workBusy = false, bool presentationAllowed = true);
}
