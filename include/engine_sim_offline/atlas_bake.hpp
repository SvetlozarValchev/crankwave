#pragma once

#include "engine_sim_offline/authoring/atlas_bake_document.hpp"
#include "engine_sim_offline/authoring/diagnostic.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline {

// The atlas compiler does not resolve source URIs. Callers supply already parsed
// scenario documents in the exact authored scenario_sources order.
struct AtlasBakeScenarioInputView {
    std::string_view source_id;
    const authoring::ScenarioDocument *document = nullptr;
};

struct CompiledAtlasBakeAudioBus {
    // Stable identity authored by atlas-bake.json.
    std::string id;
    // Exact semantic identity exposed by EngineSession audio-bus descriptors.
    std::string session_bus_id;

    friend bool operator==(const CompiledAtlasBakeAudioBus &,
                           const CompiledAtlasBakeAudioBus &) = default;
};

struct CompiledAtlasBakeScenarioSource {
    std::string id;
    std::string uri;
    compile::CompiledScenario scenario;
};

// Authored capture metadata remains intact and in authored order. The scenario
// handle shares the exact independently compiled source retained by scenario_sources.
struct CompiledAtlasBakeMovingSegment {
    authoring::AtlasBakeMovingSegment capture;
    std::size_t scenario_source_index = 0;
    compile::CompiledScenario scenario;
};

namespace detail {
struct CompiledAtlasBakeStorage;
class CompiledAtlasBakeBuilder;
} // namespace detail

// Copying this handle shares one immutable compilation. It owns every compiled
// source scenario and therefore remains independent of the input document views.
class CompiledAtlasBake final {
  public:
    CompiledAtlasBake(const CompiledAtlasBake &) noexcept = default;
    CompiledAtlasBake(CompiledAtlasBake &&other) noexcept;
    CompiledAtlasBake &operator=(const CompiledAtlasBake &) noexcept = default;
    CompiledAtlasBake &operator=(CompiledAtlasBake &&other) noexcept;
    ~CompiledAtlasBake() = default;

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] compile::CompiledEngine engine() const noexcept;
    [[nodiscard]] std::uint64_t public_seed() const noexcept;
    [[nodiscard]] compile::SiRate audio_sample_rate() const noexcept;
    [[nodiscard]] std::span<const CompiledAtlasBakeAudioBus>
    audio_buses() const noexcept;
    [[nodiscard]] const authoring::AtlasBakeDomain &domain() const noexcept;
    [[nodiscard]] std::span<const CompiledAtlasBakeScenarioSource>
    scenario_sources() const noexcept;
    [[nodiscard]] std::span<const CompiledAtlasBakeMovingSegment>
    moving_segments() const noexcept;

  private:
    explicit CompiledAtlasBake(
        std::shared_ptr<const detail::CompiledAtlasBakeStorage> storage) noexcept;

    std::shared_ptr<const detail::CompiledAtlasBakeStorage> storage_;

    friend class detail::CompiledAtlasBakeBuilder;
};

using AtlasBakeCompileResult =
    std::variant<CompiledAtlasBake, authoring::DiagnosticReport>;

// Purely validates and binds a moving-segment capture plan. It performs no session
// execution, audio capture, filesystem access, or publication.
[[nodiscard]] AtlasBakeCompileResult compile_atlas_bake(
    const authoring::AtlasBakeDocument &document,
    const compile::CompiledEngine &engine,
    std::span<const AtlasBakeScenarioInputView> scenario_inputs) noexcept;

} // namespace engine_sim_offline
