#pragma once
#include <QtGlobal>

namespace xerahs::uploads {
inline constexpr qint64 kDefaultMaximumUploadInputBytes = 200LL * 1024 * 1024;
inline constexpr qint64 kMaximumUploadResponseBytes = 4LL * 1024 * 1024;
inline constexpr int kMaximumUploadAttempts = 3;
inline constexpr int kInitialUploadBackoffMs = 250;
}
