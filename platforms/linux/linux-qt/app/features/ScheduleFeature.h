// CORE-007, CORE-016: delayed and automatic capture commands.
#pragma once
#include "capture/CaptureConditions.h"
#include <functional>
namespace xerahs::app {
struct AppContext;
xerahs::capture::AutoCondition automaticCaptureCondition(AppContext &ctx, bool locked,
    const std::function<qint64(const QString &)> &freeBytes = {});
void installSchedule(AppContext &ctx);
}  // namespace xerahs::app
