#pragma once

#include "engine_sim_offline/authoring/package_bake_document.hpp"
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

// The package compiler never resolves a URI or opens a platform resource. Callers
// provide the already parsed scenario document for every authored source identity.
struct PackageBakeScenarioInputView {
    std::string_view source_id;
    const authoring::ScenarioDocument *document = nullptr;
};

// This is one current reconstruction method, not a selectable compatibility
// profile. Replacing it requires replacing the package contract and its listening
// fixture together.
struct PackageBakeMethodGeometry {
    double rpm_grid_spacing = 25.0;
    std::uint32_t padding_rows_per_side = 3;
    std::uint32_t neighbor_radius_rows = 3;
    // Package boundaries index the already-rendered delivery-rate tape. The
    // exact physical crank crossing is shifted by the common linear-phase delay
    // of the 257-tap 20 kHz -> 192 kHz causal reconstruction. It deliberately
    // does not compensate modeled route propagation, conditioning, or IR phase.
    double cycle_signal_alignment_frames = 1228.8;
    std::uint32_t edge_guard_frames = 3840;
    double maximum_assignment_error_rpm = 12.5;

    friend bool operator==(const PackageBakeMethodGeometry &,
                           const PackageBakeMethodGeometry &) = default;
};

inline constexpr PackageBakeMethodGeometry kPackageBakeMethodGeometry{};

struct CompiledPackageBakeRpmRange {
    double playback_minimum_rpm = 0.0;
    double playback_maximum_rpm = 0.0;
    double padded_minimum_rpm = 0.0;
    double padded_maximum_rpm = 0.0;

    friend bool operator==(const CompiledPackageBakeRpmRange &,
                           const CompiledPackageBakeRpmRange &) = default;
};

struct CompiledPackageBakeScenarioSource {
    std::string id;
    std::string uri;
    compile::CompiledScenario scenario;
};

struct CompiledPackageBakeRunningPlane {
    std::string id;
    double load_coordinate = 0.0;
    authoring::PackageBakeRunningDirection direction =
        authoring::PackageBakeRunningDirection::rising;
    std::size_t scenario_source_index = 0;
    compile::CompiledScenario scenario;
};

namespace detail {
struct CompiledPackageBakeStorage;
class CompiledPackageBakeBuilder;
} // namespace detail

// Copying this handle shares one immutable package compilation. Each retained
// scenario remains bound to the exact CompiledEngine supplied to compilation.
class CompiledPackageBake final {
  public:
    CompiledPackageBake(const CompiledPackageBake &) noexcept = default;
    CompiledPackageBake(CompiledPackageBake &&other) noexcept;
    CompiledPackageBake &operator=(const CompiledPackageBake &) noexcept = default;
    CompiledPackageBake &operator=(CompiledPackageBake &&other) noexcept;
    ~CompiledPackageBake() = default;

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] compile::CompiledEngine engine() const noexcept;
    [[nodiscard]] std::uint64_t public_seed() const noexcept;
    [[nodiscard]] compile::SiRate audio_sample_rate() const noexcept;
    [[nodiscard]] std::span<const std::string> audio_bus_ids() const noexcept;
    [[nodiscard]] const PackageBakeMethodGeometry &method_geometry() const noexcept;
    [[nodiscard]] const CompiledPackageBakeRpmRange &rpm_range() const noexcept;
    [[nodiscard]] std::span<const CompiledPackageBakeScenarioSource>
    scenario_sources() const noexcept;
    [[nodiscard]] std::span<const CompiledPackageBakeRunningPlane>
    running_planes() const noexcept;
    [[nodiscard]] compile::CompiledScenario idle_scenario() const noexcept;
    [[nodiscard]] std::size_t idle_scenario_source_index() const noexcept;

  private:
    explicit CompiledPackageBake(
        std::shared_ptr<const detail::CompiledPackageBakeStorage> storage) noexcept;

    std::shared_ptr<const detail::CompiledPackageBakeStorage> storage_;

    friend class detail::CompiledPackageBakeBuilder;
};

using PackageBakeCompileResult =
    std::variant<CompiledPackageBake, authoring::DiagnosticReport>;

// Purely validates and binds an authored capture plan. This function performs no
// simulation, audio capture, filesystem access, or publication.
[[nodiscard]] PackageBakeCompileResult compile_package_bake(
    const authoring::PackageBakeDocument &document,
    const compile::CompiledEngine &engine,
    std::span<const PackageBakeScenarioInputView> scenario_inputs) noexcept;

} // namespace engine_sim_offline
