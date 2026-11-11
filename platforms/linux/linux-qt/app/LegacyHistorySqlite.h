#pragma once
#include "history/LegacyHistory.h"
#include <functional>
#include <optional>

namespace xerahs::app {
struct LegacySqliteRead {
  std::optional<history::SqliteHistorySnapshot> snapshot;
  QString code;
};
LegacySqliteRead readLegacySqliteHistory(const QString &path,
    const std::function<bool()> &cancelled = {}, const std::function<void(int)> &progress = {});
}
