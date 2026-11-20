// The approved background-removal inference engine factory (onnxruntime,
// MIT; decision EU-009-010-011-BACKGROUND-MODEL-SUPPLY-2026-11-20). The
// engine itself is compiled only when the build found the runtime; without
// it the factory returns nullptr and the Background Remover reports the
// engine as not linked (the EU-001 missing-host explanation, never a
// waiver). EU-009 device policy and EU-011 failures are the engine port's
// own vocabulary in src/background/BackgroundModels.h.
#pragma once

#include "background/BackgroundModels.h"

#include <memory>

namespace xerahs::app {

// Returns the onnxruntime mask engine, or nullptr in a build without the
// optional runtime linked.
std::unique_ptr<xerahs::bg::MaskEngine> makeOnnxMaskEngine();

}  // namespace xerahs::app
