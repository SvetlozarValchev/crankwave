#include "simulation/one_level_master_rod_cycle_accounting_plan.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] contract::CylinderId
cylinder_id_of(const OneLevelMasterRodMechanismCylinderPlan &cylinder) noexcept {
    return std::visit(
        [](const auto &kinematics) { return kinematics.cylinder.cylinder_id; },
        cylinder.kinematics);
}

struct MechanismCylinderBinding {
    contract::CylinderId cylinder_id;
    std::size_t source_index = 0;
};

[[nodiscard]] const MechanismCylinderBinding *
find_mechanism_binding(const std::vector<MechanismCylinderBinding> &bindings,
                       contract::CylinderId cylinder_id) noexcept {
    const auto found = std::lower_bound(
        bindings.begin(), bindings.end(), cylinder_id,
        [](const MechanismCylinderBinding &binding, contract::CylinderId sought) {
            return binding.cylinder_id < sought;
        });
    return found == bindings.end() || found->cylinder_id != cylinder_id ? nullptr
                                                                        : &*found;
}

[[nodiscard]] const contract::GasVolumeIdentity *
find_capture_gas_volume(const detail::LowOrderCaptureBufferPlan &capture_buffer,
                        contract::GasVolumeId gas_volume_id) noexcept {
    const auto found = std::find_if(capture_buffer.gas_volumes.begin(),
                                    capture_buffer.gas_volumes.end(),
                                    [&](const contract::GasVolumeIdentity &identity) {
                                        return identity.id == gas_volume_id;
                                    });
    return found == capture_buffer.gas_volumes.end() ? nullptr : &*found;
}

} // namespace

OneLevelMasterRodCycleAccountingPlanCompileResult
compile_one_level_master_rod_dynamic_cycle_accounting_plan(
    const OneLevelMasterRodMechanismKinematicsPlan &mechanism_plan,
    const LowOrderCapturePlan &capture_plan,
    const OneLevelMasterRodDynamicAccountingInputs &inputs) {
    ValidationReport report;

    require(report, mechanism_plan.engine_id.valid(), ContractIssueCode::invalid_value,
            "mechanism_plan.engine_id",
            "cycle accounting requires a valid mechanism engine identity");
    require(report, capture_plan.capture_buffer.engine_id.valid(),
            ContractIssueCode::invalid_value, "capture_plan.capture_buffer.engine_id",
            "cycle accounting requires a valid capture engine identity");
    require(report, mechanism_plan.engine_id == capture_plan.capture_buffer.engine_id,
            ContractIssueCode::inconsistent_semantics,
            "capture_plan.capture_buffer.engine_id",
            "capture and mechanism plans must identify the same engine");
    require(report, contract::is_valid_semantic_id(mechanism_plan.engine_profile_id),
            ContractIssueCode::invalid_value, "mechanism_plan.engine_profile_id",
            "cycle accounting requires a valid mechanism profile identity");
    require(report, contract::is_valid_semantic_id(capture_plan.engine_profile_id),
            ContractIssueCode::invalid_value, "capture_plan.engine_profile_id",
            "cycle accounting requires a valid capture profile identity");
    require(report, mechanism_plan.engine_profile_id == capture_plan.engine_profile_id,
            ContractIssueCode::inconsistent_semantics, "capture_plan.engine_profile_id",
            "capture and mechanism plans must identify the same engine profile");
    require(report, std::isfinite(mechanism_plan.crank_tdc_reference_rad),
            ContractIssueCode::invalid_value, "mechanism_plan.crank_tdc_reference_rad",
            "cycle accounting requires a finite crank reference angle");
    require(report, finite_positive(inputs.initial_engine_speed_rpm),
            ContractIssueCode::invalid_value, "inputs.initial_engine_speed_rpm",
            "dynamic cycle accounting requires a finite positive initial speed");
    require(report, !mechanism_plan.cylinders.empty(), ContractIssueCode::missing_value,
            "mechanism_plan.cylinders",
            "cycle accounting requires at least one mechanism cylinder");
    require(report,
            capture_plan.cylinder_chambers.size() == mechanism_plan.cylinders.size(),
            ContractIssueCode::inconsistent_shape, "capture_plan.cylinder_chambers",
            "capture and mechanism plans must contain the same cylinder population");
    require(report, !capture_plan.physical_gas_volume_ids.empty(),
            ContractIssueCode::missing_value, "capture_plan.physical_gas_volume_ids",
            "cycle accounting requires a nonempty physical gas inventory");

    std::vector<MechanismCylinderBinding> mechanism_bindings;
    mechanism_bindings.reserve(mechanism_plan.cylinders.size());
    for (std::size_t index = 0; index < mechanism_plan.cylinders.size(); ++index) {
        const auto &cylinder = mechanism_plan.cylinders[index];
        const auto cylinder_id = cylinder_id_of(cylinder);
        const auto prefix = "mechanism_plan.cylinders[" + std::to_string(index) + "]";
        require(report, cylinder_id.valid(), ContractIssueCode::invalid_value,
                prefix + ".kinematics.cylinder.cylinder_id",
                "mechanism cylinder identity must be valid");
        require(report, cylinder.chamber_volume_id.valid(),
                ContractIssueCode::invalid_value, prefix + ".chamber_volume_id",
                "mechanism cylinder chamber identity must be valid");
        require(report,
                finite_positive(cylinder.full_cycle_geometry.swept_displacement_m3),
                ContractIssueCode::invalid_value,
                prefix + ".full_cycle_geometry.swept_displacement_m3",
                "cycle accounting requires finite positive swept displacement");
        require(report,
                finite_positive(cylinder.full_cycle_geometry
                                    .piston_axis_path_length_m_per_crank_revolution),
                ContractIssueCode::invalid_value,
                prefix + ".full_cycle_geometry."
                         "piston_axis_path_length_m_per_crank_revolution",
                "cycle accounting requires finite positive piston-axis path length");
        mechanism_bindings.push_back({cylinder_id, index});
    }
    std::sort(mechanism_bindings.begin(), mechanism_bindings.end(),
              [](const auto &left, const auto &right) {
                  return left.cylinder_id < right.cylinder_id;
              });
    for (std::size_t index = 1; index < mechanism_bindings.size(); ++index) {
        if (mechanism_bindings[index - 1U].cylinder_id ==
            mechanism_bindings[index].cylinder_id) {
            report.add(ContractIssueCode::duplicate_identity,
                       "mechanism_plan.cylinders[" +
                           std::to_string(mechanism_bindings[index].source_index) +
                           "].kinematics.cylinder.cylinder_id",
                       "mechanism cylinder identities must be unique");
        }
    }

    contract::GasVolumeId previous_physical_volume_id;
    for (std::size_t index = 0; index < capture_plan.physical_gas_volume_ids.size();
         ++index) {
        const auto gas_volume_id = capture_plan.physical_gas_volume_ids[index];
        const auto path =
            "capture_plan.physical_gas_volume_ids[" + std::to_string(index) + "]";
        require(report, gas_volume_id.valid(), ContractIssueCode::invalid_value, path,
                "physical gas-volume identity must be valid");
        require(report, index == 0U || previous_physical_volume_id < gas_volume_id,
                ContractIssueCode::inconsistent_semantics, path,
                "physical gas-volume identities must be strictly ascending");

        const auto *capture_identity =
            find_capture_gas_volume(capture_plan.capture_buffer, gas_volume_id);
        require(report, capture_identity != nullptr,
                ContractIssueCode::dangling_reference, path,
                "physical gas-volume identity must resolve in the capture layout");
        if (capture_identity != nullptr) {
            require(report,
                    capture_identity->kind != contract::GasVolumeKind::atmosphere &&
                        capture_identity->kind != contract::GasVolumeKind::unspecified,
                    ContractIssueCode::inconsistent_semantics, path,
                    "physical gas inventory cannot contain atmosphere or an "
                    "unspecified volume");
        }
        previous_physical_volume_id = gas_volume_id;
    }

    std::vector<bool> consumed_mechanism_cylinders(mechanism_plan.cylinders.size(),
                                                   false);
    contract::CylinderId previous_capture_cylinder_id;
    for (std::size_t index = 0; index < capture_plan.cylinder_chambers.size();
         ++index) {
        const auto &binding = capture_plan.cylinder_chambers[index];
        const auto prefix =
            "capture_plan.cylinder_chambers[" + std::to_string(index) + "]";
        require(report, binding.cylinder_id.valid(), ContractIssueCode::invalid_value,
                prefix + ".cylinder_id", "capture cylinder identity must be valid");
        require(report,
                index == 0U || previous_capture_cylinder_id < binding.cylinder_id,
                ContractIssueCode::inconsistent_semantics, prefix + ".cylinder_id",
                "capture cylinder bindings must be strictly ascending");
        require(report, binding.chamber_volume_id.valid(),
                ContractIssueCode::invalid_value, prefix + ".chamber_volume_id",
                "capture chamber identity must be valid");
        const bool physical_index_valid =
            binding.physical_volume_index < capture_plan.physical_gas_volume_ids.size();
        require(report, physical_index_valid, ContractIssueCode::dangling_reference,
                prefix + ".physical_volume_index",
                "capture chamber must index the physical gas inventory");
        if (physical_index_valid) {
            require(
                report,
                capture_plan.physical_gas_volume_ids[binding.physical_volume_index] ==
                    binding.chamber_volume_id,
                ContractIssueCode::inconsistent_semantics,
                prefix + ".physical_volume_index",
                "capture chamber identity must exactly match its physical "
                "inventory entry");
        }

        const auto *mechanism_binding =
            find_mechanism_binding(mechanism_bindings, binding.cylinder_id);
        require(report, mechanism_binding != nullptr,
                ContractIssueCode::dangling_reference, prefix + ".cylinder_id",
                "capture cylinder must resolve in the mechanism plan");
        if (mechanism_binding != nullptr) {
            const auto &mechanism_cylinder =
                mechanism_plan.cylinders[mechanism_binding->source_index];
            require(report,
                    mechanism_cylinder.chamber_volume_id == binding.chamber_volume_id,
                    ContractIssueCode::inconsistent_semantics,
                    prefix + ".chamber_volume_id",
                    "capture and mechanism plans must bind the cylinder to the "
                    "same chamber");
            consumed_mechanism_cylinders[mechanism_binding->source_index] = true;
        }
        previous_capture_cylinder_id = binding.cylinder_id;
    }
    for (std::size_t index = 0; index < consumed_mechanism_cylinders.size(); ++index) {
        require(report, consumed_mechanism_cylinders[index],
                ContractIssueCode::dangling_reference,
                "mechanism_plan.cylinders[" + std::to_string(index) + "]",
                "every mechanism cylinder must resolve in the capture plan");
    }

    if (!report.ok()) {
        return report;
    }

    OperatingCycleAccountingPlan compiled;
    compiled.quadrature.cycle_reference_theta_rad =
        mechanism_plan.crank_tdc_reference_rad;
    compiled.engine_speed_rpm = inputs.initial_engine_speed_rpm;
    compiled.starter_mechanically_disengaged = true;
    compiled.indicated_terms = contract::indicated_gas_torque_term_mask();
    compiled.aggregate_loss_terms = inputs.aggregate_loss_terms;
    compiled.starter_terms = inputs.starter_terms;
    compiled.physically_resolved_gas_volumes = capture_plan.physical_gas_volume_ids;
    compiled.derive_mean_engine_speed_from_cycle_duration = true;
    compiled.cylinders.reserve(capture_plan.cylinder_chambers.size());

    PerCylinderTravelChenFlynnLossPlan aggregate_loss;
    aggregate_loss.coefficients = inputs.coefficients;
    aggregate_loss.cylinders.reserve(capture_plan.cylinder_chambers.size());

    double total_displacement_m3 = 0.0;
    for (std::size_t index = 0; index < capture_plan.cylinder_chambers.size();
         ++index) {
        const auto &capture_binding = capture_plan.cylinder_chambers[index];
        const auto *mechanism_binding =
            find_mechanism_binding(mechanism_bindings, capture_binding.cylinder_id);
        const auto &mechanism_cylinder =
            mechanism_plan.cylinders[mechanism_binding->source_index];
        const auto &geometry = mechanism_cylinder.full_cycle_geometry;
        const double next_total =
            total_displacement_m3 + geometry.swept_displacement_m3;
        require(report, std::isfinite(next_total) && next_total > total_displacement_m3,
                ContractIssueCode::invalid_value,
                "mechanism_plan.cylinders[" +
                    std::to_string(mechanism_binding->source_index) +
                    "].full_cycle_geometry.swept_displacement_m3",
                "ordered total swept-displacement reduction must remain finite "
                "and strictly increase");
        if (!report.ok()) {
            return report;
        }
        total_displacement_m3 = next_total;
        compiled.cylinders.push_back({capture_binding.cylinder_id,
                                      capture_binding.chamber_volume_id,
                                      geometry.swept_displacement_m3});
        aggregate_loss.cylinders.push_back(
            {capture_binding.cylinder_id,
             geometry.piston_axis_path_length_m_per_crank_revolution});
    }

    compiled.quadrature.total_displacement_m3 = total_displacement_m3;
    compiled.aggregate_loss = std::move(aggregate_loss);
    return compiled;
}

} // namespace engine_sim_offline::simulation
