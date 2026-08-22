#pragma once

#include "authoring/document_reader.hpp"
#include "crankwave/authoring/engine_document.hpp"

namespace crankwave::authoring::detail {

// Validates the authoritative cylinder -> journal -> crankshaft/master-cylinder
// attachment graph for both parsed documents and direct compiler DTOs.
[[nodiscard]] DiagnosticReport
validate_engine_mechanism_graph(const EngineDefinition &engine);

// Runs only after structural/domain parsing succeeds, preventing malformed values
// from producing cascades of meaningless graph diagnostics.
void validate_engine_document(DocumentReader &reader,
                              const EnginePackageDocument &document);

} // namespace crankwave::authoring::detail
