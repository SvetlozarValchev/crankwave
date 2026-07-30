#pragma once

#include "engine_sim_offline/authoring/diagnostic.hpp"
#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/json.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

namespace engine_sim_offline::authoring {

template <class Document>
using DocumentParseResult = std::variant<Document, DiagnosticReport>;

struct AuthoringParseLimits {
    JsonParseLimits json;
    std::size_t maximum_diagnostics = 256U;
    std::size_t maximum_array_items = 65536U;
    std::size_t maximum_string_bytes = 65536U;
    std::uint32_t maximum_process_block_capacity_frames = 1048576U;
    std::uint32_t maximum_event_queue_capacity = 1048576U;
    std::uint32_t maximum_telemetry_capacity_frames = 16777216U;

    friend bool operator==(const AuthoringParseLimits &,
                           const AuthoringParseLimits &) = default;
};

using EngineDocumentParseResult = DocumentParseResult<EnginePackageDocument>;
using ScenarioDocumentParseResult = DocumentParseResult<ScenarioDocument>;

// These functions parse exactly one strict product document. Unknown members are
// errors. Syntax failures and schema failures use the same path-bearing diagnostic
// surface as semantic authoring failures.
[[nodiscard]] EngineDocumentParseResult
parse_engine_document(std::string_view json, AuthoringParseLimits limits = {}) noexcept;

[[nodiscard]] ScenarioDocumentParseResult
parse_scenario_document(std::string_view json,
                        AuthoringParseLimits limits = {}) noexcept;

// Cross-document references cannot be decided while parsing a scenario in isolation.
// The compiler will call this check after both documents have parsed successfully.
[[nodiscard]] DiagnosticReport
validate_scenario_references(const ScenarioDocument &scenario,
                             const EnginePackageDocument &engine) noexcept;

} // namespace engine_sim_offline::authoring
