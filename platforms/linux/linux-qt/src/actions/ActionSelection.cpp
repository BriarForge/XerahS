#include "ActionSelection.h"
namespace xerahs::actions {
namespace {
std::optional<int> selectorIndex(const QString &id) {
  const auto index = catalogueIndex(id);
  return index && catalogue()[size_t(*index)].kind == ActionKind::Selection ? index : std::nullopt;
}
}
std::vector<ActionSelectionChoice> actionSelectionChoices(const QString &selector, const QStringList &configured,
                                                         const QStringList &available) {
  std::vector<ActionSelectionChoice> choices;
  const auto index = selectorIndex(selector); if (!index) return choices;
  for (size_t i = size_t(*index + 1); i < catalogue().size(); ++i) {
    const auto &id = catalogue()[i].id;
    choices.push_back({id, available.contains(id), configured.contains(id)});
  }
  return choices;
}
ActionSelectionValidation validateActionSelection(const QString &selector, const QStringList &requested,
                                                  const QStringList &available) {
  const auto index = selectorIndex(selector);
  if (!index) return {false, {}, QStringLiteral("selection-invalid-selector")};
  for (const auto &id : requested) {
    const auto requestedIndex = catalogueIndex(id);
    if (!requestedIndex) return {false, {}, QStringLiteral("selection-unknown-action")};
    if (*requestedIndex > *index && !available.contains(id)) return {false, {}, QStringLiteral("selection-action-unavailable")};
  }
  QStringList selected;
  for (const auto &choice : actionSelectionChoices(selector, requested, available))
    if (choice.selected) selected.append(choice.action);
  return {true, selected, {}};
}
} // namespace xerahs::actions
