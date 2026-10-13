#pragma once
#include <QString>
namespace xerahs::actions {
struct SaveAsTarget { QString path, diagnostic; bool valid() const { return diagnostic.isEmpty() && !path.isEmpty(); } };
// Current native PNG save-as policy. No filesystem reads or mutations.
SaveAsTarget captureSaveAsTarget(const QString &requested);
} // namespace xerahs::actions
