#pragma once
#include "PostCapturePipeline.h"

namespace xerahs::actions {
// PCA-009/023: draft choices are scoped to the remaining catalogue. Native
// availability is supplied by the executor, never inferred from selection.
struct ActionSelectionChoice { QString action; bool available = false; bool selected = false; };
std::vector<ActionSelectionChoice> actionSelectionChoices(const QString &selector, const QStringList &configured,
                                                         const QStringList &available);
struct ActionSelectionValidation {
  bool accepted = false;
  QStringList selected;
  QString diagnostic;
};
// Empty selection is valid. Unsupported/unknown selections reject the whole
// draft rather than silently removing requested work; IDs at/before the
// selector are ignored as required by PCA-023.
ActionSelectionValidation validateActionSelection(const QString &selector, const QStringList &requested,
                                                  const QStringList &available);
} // namespace xerahs::actions
