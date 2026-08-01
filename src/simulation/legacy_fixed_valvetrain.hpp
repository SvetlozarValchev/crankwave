#pragma once

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/parity_model.hpp"
#include "simulation/legacy_mechanics_primitives.hpp"
#include "simulation/legacy_vtec_selector.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::simulation {

struct LegacyValvetrainFlowProfile {
    contract::BankId bank_id;
    std::vector<LegacyTrianglePoint> intake_flow_table;
    std::vector<LegacyTrianglePoint> exhaust_flow_table;
    double intake_flow_triangle_radius_m = 0.0;
    double exhaust_flow_triangle_radius_m = 0.0;

    friend bool operator==(const LegacyValvetrainFlowProfile &,
                           const LegacyValvetrainFlowProfile &) = default;
};

struct LegacyCompiledCamProfile {
    std::vector<LegacyTrianglePoint> lobe_table;
    double lobe_triangle_radius_rad = 0.0;
    double advance_rad = 0.0;
    double base_radius_m = 0.0;

    friend bool operator==(const LegacyCompiledCamProfile &,
                           const LegacyCompiledCamProfile &) = default;
};

struct LegacyValvetrainCylinderBinding {
    contract::CylinderId cylinder_id;
    contract::PortId intake_port_id;
    contract::PortId exhaust_port_id;
    std::size_t flow_profile_index = 0;
    std::size_t intake_cam_profile_index = 0;
    std::size_t exhaust_cam_profile_index = 0;
    double intake_stored_lobe_angle_rad = 0.0;
    double exhaust_stored_lobe_angle_rad = 0.0;

    friend bool operator==(const LegacyValvetrainCylinderBinding &,
                           const LegacyValvetrainCylinderBinding &) = default;
};

struct LegacyCylinderValveSample {
    contract::CylinderId cylinder_id;
    contract::PortId intake_port_id;
    contract::PortId exhaust_port_id;
    double intake_lobe_argument_rad = 0.0;
    double exhaust_lobe_argument_rad = 0.0;
    double intake_lift_m = 0.0;
    double exhaust_lift_m = 0.0;
    double intake_valve_k = 0.0;
    double exhaust_valve_k = 0.0;

    friend bool operator==(const LegacyCylinderValveSample &,
                           const LegacyCylinderValveSample &) = default;
};

class LegacyFixedValvetrain final {
  public:
    LegacyFixedValvetrain(const LegacyFixedValvetrain &) = default;
    LegacyFixedValvetrain &operator=(const LegacyFixedValvetrain &) = default;
    LegacyFixedValvetrain(LegacyFixedValvetrain &&) noexcept = default;
    LegacyFixedValvetrain &operator=(LegacyFixedValvetrain &&) noexcept = default;

    [[nodiscard]] std::span<const LegacyValvetrainCylinderBinding>
    cylinder_bindings() const noexcept;
    [[nodiscard]] std::span<const LegacyCompiledCamProfile>
    intake_cam_profiles() const noexcept;
    [[nodiscard]] std::span<const LegacyCompiledCamProfile>
    exhaust_cam_profiles() const noexcept;
    [[nodiscard]] std::span<const LegacyValvetrainFlowProfile>
    flow_profiles() const noexcept;

    // body_angle_psi_rad is LegacyMechanismStep::body_angle_psi_rad. Sampling is
    // read-only and performs no allocation.
    [[nodiscard]] std::optional<LegacyCylinderValveSample>
    sample_cylinder(std::size_t cylinder_index,
                    double body_angle_psi_rad) const noexcept;
    [[nodiscard]] std::optional<LegacyCylinderValveSample>
    sample_cylinder(contract::CylinderId cylinder_id,
                    double body_angle_psi_rad) const noexcept;

    // output must have exactly cylinder_bindings().size() elements. Samples are
    // written in the engine/mechanism cylinder order frozen at admission.
    [[nodiscard]] bool
    sample_all(double body_angle_psi_rad,
               std::span<LegacyCylinderValveSample> output) const noexcept;

  private:
    LegacyFixedValvetrain(
        double crank_tdc_reference_rad,
        std::vector<LegacyValvetrainCylinderBinding> cylinder_bindings,
        std::vector<LegacyCompiledCamProfile> intake_cam_profiles,
        std::vector<LegacyCompiledCamProfile> exhaust_cam_profiles,
        std::vector<LegacyValvetrainFlowProfile> flow_profiles);

    [[nodiscard]] LegacyCylinderValveSample
    sample_admitted_cylinder(std::size_t cylinder_index,
                             double body_angle_psi_rad) const noexcept;

    double crank_tdc_reference_rad_ = 0.0;
    std::vector<LegacyValvetrainCylinderBinding> cylinder_bindings_;
    std::vector<LegacyCompiledCamProfile> intake_cam_profiles_;
    std::vector<LegacyCompiledCamProfile> exhaust_cam_profiles_;
    std::vector<LegacyValvetrainFlowProfile> flow_profiles_;

    friend std::variant<LegacyFixedValvetrain, contract::ValidationReport>
    compile_legacy_fixed_valvetrain(const contract::EngineSpec &,
                                    const contract::LowOrderEngineCoreV1 &);
};

using LegacyFixedValvetrainCompileResult =
    std::variant<LegacyFixedValvetrain, contract::ValidationReport>;

// Compiles all source-shaped tables and topology bindings once. The result retains
// no EngineSpec references and sampling has no mutable state.
[[nodiscard]] LegacyFixedValvetrainCompileResult
compile_legacy_fixed_valvetrain(const contract::EngineSpec &engine,
                                const contract::LowOrderEngineCoreV1 &core);

class LegacySelectableValvetrain final {
  public:
    LegacySelectableValvetrain(const LegacySelectableValvetrain &) = default;
    LegacySelectableValvetrain &operator=(const LegacySelectableValvetrain &) = default;
    LegacySelectableValvetrain(LegacySelectableValvetrain &&) noexcept = default;
    LegacySelectableValvetrain &
    operator=(LegacySelectableValvetrain &&) noexcept = default;

    [[nodiscard]] const LegacyFixedValvetrain &
    profile_for(const LegacyVtecSelectorInput &input) const noexcept;

  private:
    LegacySelectableValvetrain(LegacyFixedValvetrain base,
                               std::optional<LegacyFixedValvetrain> alternate,
                               std::optional<LegacyVtecSelectorThresholds> thresholds);
    [[nodiscard]] bool
    alternate_profile_active(const LegacyVtecSelectorInput &input) const noexcept;

    LegacyFixedValvetrain base_;
    std::optional<LegacyFixedValvetrain> alternate_;
    std::optional<LegacyVtecSelectorThresholds> thresholds_;

    friend std::variant<LegacySelectableValvetrain, contract::ValidationReport>
    compile_legacy_selectable_valvetrain(const contract::EngineSpec &,
                                         const contract::LowOrderEngineCoreV1 &);
};

using LegacySelectableValvetrainCompileResult =
    std::variant<LegacySelectableValvetrain, contract::ValidationReport>;

// Compiles the fixed base pair unchanged. When an alternate pair is present, it is
// independently compiled through the same fixed-valvetrain compiler and selected by
// the stateless pristine VTEC predicate.
[[nodiscard]] LegacySelectableValvetrainCompileResult
compile_legacy_selectable_valvetrain(const contract::EngineSpec &engine,
                                     const contract::LowOrderEngineCoreV1 &core);

} // namespace engine_sim_offline::simulation
