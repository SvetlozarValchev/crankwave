#pragma once

#include "engine_sim_offline/profiles/bmw_m52b28_operating_profile.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_parity_request.hpp"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::profiles::detail {

enum class BmwProfileKind : std::uint8_t {
    parity_request_v1,
    low_order_operating_point_v1,
};

enum class BmwResolutionSource : std::uint8_t {
    legacy_asset,
    reference_fixture,
    reference_trajectory,
    reference_component_seed,
    profile_contract,
    declared_default,
    operating_literature,
    accessory_configuration,
    implemented_method,
};

class BmwProvenanceBuilder {
  public:
    explicit BmwProvenanceBuilder(BmwProfileKind profile_kind);

    template <class T>
    [[nodiscard]] contract::ResolvedValue<T>
    resolved(T value, std::string parameter_path, BmwResolutionSource source) {
        return {
            std::move(value),
            add_resolution(std::move(parameter_path), source),
        };
    }

    template <class T>
    [[nodiscard]] contract::ResolvedValue<T>
    derived(T value, std::string parameter_path, contract::MethodIdentity method,
            std::initializer_list<std::string_view> dependencies) {
        std::vector<std::string> owned_dependencies;
        owned_dependencies.reserve(dependencies.size());
        for (const auto dependency : dependencies) {
            owned_dependencies.emplace_back(dependency);
        }
        return {
            std::move(value),
            add_derived_resolution(std::move(parameter_path), std::move(method),
                                   std::move(owned_dependencies)),
        };
    }

    [[nodiscard]] contract::ProvenanceLedger finish();
    [[nodiscard]] BmwProfileKind profile_kind() const noexcept;
    [[nodiscard]] std::string_view engine_profile_id() const noexcept;
    [[nodiscard]] std::string_view provenance_schema_id() const noexcept;
    [[nodiscard]] std::string profile_path(std::string_view suffix) const;

  private:
    [[nodiscard]] std::string add_resolution(std::string parameter_path,
                                             BmwResolutionSource source);
    [[nodiscard]] std::string
    add_derived_resolution(std::string parameter_path, contract::MethodIdentity method,
                           std::vector<std::string> dependencies);

    contract::ProvenanceLedger ledger_;
    BmwProfileKind profile_kind_;
    std::uint32_t next_resolution_ = 1;
};

[[nodiscard]] contract::MethodIdentity legacy_low_order_method();
[[nodiscard]] contract::MethodIdentity fixed_rate_rpm_method();
[[nodiscard]] contract::MethodIdentity derived_method(std::string id);
[[nodiscard]] contract::Sha256Digest
bmw_m52b28_operating_accessory_descriptor_sha256();

[[nodiscard]] contract::EngineSpec
build_bmw_m52b28_low_order_engine(BmwProvenanceBuilder &builder);

[[nodiscard]] contract::RenderScenario
build_bmw_m52b28_parity_scenario(BmwProvenanceBuilder &builder,
                                 std::vector<double> post_step_rpm);

[[nodiscard]] BmwM52b28ParityRequest
build_bmw_m52b28_parity_request_unvalidated(std::vector<double> post_step_rpm);

[[nodiscard]] BmwM52b28OperatingProfile
build_bmw_m52b28_operating_profile_unvalidated();

[[nodiscard]] contract::ValidationReport
validate_bmw_m52b28_parity_rpm_input(std::span<const double> post_step_rpm);

[[nodiscard]] contract::ValidationReport validate_bmw_m52b28_parity_rpm_trajectory(
    const contract::FixedRateRpmTrajectory &trajectory);

} // namespace engine_sim_offline::profiles::detail
