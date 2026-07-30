#pragma once

#include "engine_sim_offline/authoring/diagnostic.hpp"
#include "engine_sim_offline/authoring/quantity.hpp"
#include "engine_sim_offline/contract/provenance.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace engine_sim_offline::authoring {
struct EnginePackageDocument;
struct ScenarioDocument;
} // namespace engine_sim_offline::authoring

namespace engine_sim_offline::compile {

using RuntimeObjectId = std::uint32_t;

// Runtime IDs are dense only inside one object namespace. The compiler assigns them
// from canonical stable-ID order, never from JSON array order.
struct StableIdAssignment {
    std::string object_namespace;
    std::string authored_id;
    RuntimeObjectId runtime_id = 0;

    friend bool operator==(const StableIdAssignment &,
                           const StableIdAssignment &) = default;
};

enum class FlowCalibrationStandard : std::uint8_t {
    none,
    carburetor_1p5_inhg,
    port_28_inh2o,
};

struct SiQuantity {
    double value = 0.0;
    authoring::QuantityDimension dimension =
        authoring::QuantityDimension::dimensionless;
    FlowCalibrationStandard flow_calibration = FlowCalibrationStandard::none;

    friend bool operator==(const SiQuantity &, const SiQuantity &) = default;
};

// Frequencies remain exact rationals. numerator_hz/denominator is always reduced and
// both members are positive.
struct SiRate {
    std::uint64_t numerator_hz = 0;
    std::uint64_t denominator = 1;

    friend bool operator==(const SiRate &, const SiRate &) = default;
};

enum class AssetKind : std::uint8_t {
    audio,
    accessory_configuration,
};

// Compile callers supply already-loaded bytes. The portable compiler never opens a
// filesystem path, URL, browser object, or platform handle. Kind is part of identity:
// equal text IDs in the two asset namespaces do not collide.
struct AssetPayloadView {
    AssetKind kind = AssetKind::audio;
    std::string_view asset_id;
    std::span<const std::byte> bytes;
};

// Borrowed views remain valid only while a CompiledEngine or CompiledScenario sharing
// their immutable storage remains alive.
struct CompiledAssetView {
    AssetKind kind = AssetKind::audio;
    std::string_view asset_id;
    std::span<const std::byte> bytes;
};

namespace detail {
struct CompiledEngineStorage;
struct CompiledScenarioStorage;
class CompiledEngineBuilder;
class CompiledScenarioBuilder;
} // namespace detail

// Copying either handle shares immutable resolved ownership. CompiledScenario retains
// the engine storage it was compiled against, so a scenario cannot outlive or silently
// detach from that exact compiled engine.
class CompiledEngine final {
  public:
    CompiledEngine(const CompiledEngine &) noexcept = default;
    // Shared immutable handles deliberately use copy-like move semantics so a moved
    // source never becomes an invalid, accessor-crashing state.
    CompiledEngine(CompiledEngine &&other) noexcept;
    CompiledEngine &operator=(const CompiledEngine &) noexcept = default;
    CompiledEngine &operator=(CompiledEngine &&other) noexcept;
    ~CompiledEngine() = default;

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::span<const StableIdAssignment>
    stable_id_assignments() const noexcept;
    [[nodiscard]] const contract::ProvenanceLedger &provenance() const noexcept;
    [[nodiscard]] std::size_t asset_count() const noexcept;
    [[nodiscard]] std::optional<CompiledAssetView>
    asset(std::size_t index) const noexcept;

  private:
    explicit CompiledEngine(
        std::shared_ptr<const detail::CompiledEngineStorage> storage) noexcept;

    std::shared_ptr<const detail::CompiledEngineStorage> storage_;

    friend class detail::CompiledEngineBuilder;
    friend class detail::CompiledScenarioBuilder;
    friend class CompiledScenario;
};

class CompiledScenario final {
  public:
    CompiledScenario(const CompiledScenario &) noexcept = default;
    CompiledScenario(CompiledScenario &&other) noexcept;
    CompiledScenario &operator=(const CompiledScenario &) noexcept = default;
    CompiledScenario &operator=(CompiledScenario &&other) noexcept;
    ~CompiledScenario() = default;

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] CompiledEngine engine() const noexcept;
    [[nodiscard]] std::span<const StableIdAssignment>
    stable_id_assignments() const noexcept;
    [[nodiscard]] const contract::ProvenanceLedger &provenance() const noexcept;

  private:
    explicit CompiledScenario(
        std::shared_ptr<const detail::CompiledScenarioStorage> storage) noexcept;

    std::shared_ptr<const detail::CompiledScenarioStorage> storage_;

    friend class detail::CompiledScenarioBuilder;
};

template <class Value>
using CompileResult = std::variant<Value, authoring::DiagnosticReport>;

using EngineCompileResult = CompileResult<CompiledEngine>;
using ScenarioCompileResult = CompileResult<CompiledScenario>;

// Compilation is pure with respect to platform services. The caller parses JSON and
// supplies every referenced byte payload; the compiler performs no filesystem, URL,
// browser, or process-global access.
[[nodiscard]] EngineCompileResult
compile_engine(const authoring::EnginePackageDocument &document,
               std::span<const AssetPayloadView> assets) noexcept;

// A compiled scenario remains bound to the exact immutable engine passed here. A
// scenario naming another engine or an unsupported execution capability fails with a
// path-bearing diagnostic rather than selecting an approximation.
[[nodiscard]] ScenarioCompileResult
compile_scenario(const CompiledEngine &engine,
                 const authoring::ScenarioDocument &document) noexcept;

} // namespace engine_sim_offline::compile
