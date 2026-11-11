#pragma once
#include "history/LegacyHistory.h"
#include <optional>
#include <functional>
namespace xerahs::app {
struct LegacyHistorySourceRead {
  history::LegacyHistoryPreview preview;
  std::optional<history::LegacyHistorySnapshot> source;
};
LegacyHistorySourceRead readLegacyHistorySource(const QString &path,
    const std::function<bool()> &cancelled = {}, const std::function<void(int)> &progress = {});
}
