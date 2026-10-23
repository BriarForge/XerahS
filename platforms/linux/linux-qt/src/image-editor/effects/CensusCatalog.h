// EDITOR-EFFECTS-001: EE-001..EE-003, EE-017 census metadata (not availability).
#pragma once
#include "Effect.h"

namespace xerahs::effects {
struct CensusParameter {
  QString id;
  ParamKind kind;
  QString label;
};
struct CensusEffect {
  QString id;
  Category category;
  Mode mode;
  QVector<CensusParameter> parameters;
};
const QVector<CensusEffect> &censusCatalog();
}  // namespace xerahs::effects
