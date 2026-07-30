#pragma once

#include "authoring/document_reader.hpp"
#include "engine_sim_offline/authoring/engine_document.hpp"

namespace engine_sim_offline::authoring::detail {

// Runs only after structural/domain parsing succeeds, preventing malformed values
// from producing cascades of meaningless graph diagnostics.
void validate_engine_document(DocumentReader &reader,
                              const EnginePackageDocument &document);

} // namespace engine_sim_offline::authoring::detail
