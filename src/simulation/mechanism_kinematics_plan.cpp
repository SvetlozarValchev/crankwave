#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] const contract::BankSpec *
find_bank(const contract::EngineSpec &engine, const contract::BankId bank_id) noexcept {
    const auto found =
        std::find_if(engine.banks.begin(), engine.banks.end(),
                     [&](const auto &bank) { return bank.id == bank_id; });
    return found == engine.banks.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::LegacyBankHeadProfile *
find_head_profile(const contract::LegacyGasPathProfile &gas_path,
                  const contract::BankId bank_id) noexcept {
    const auto found =
        std::find_if(gas_path.heads.begin(), gas_path.heads.end(),
                     [&](const auto &head) { return head.bank_id == bank_id; });
    return found == gas_path.heads.end() ? nullptr : &*found;
}

[[nodiscard]] bool
bank_head_topology_matches(const contract::EngineSpec &engine,
                           const contract::LegacyGasPathProfile &gas_path) noexcept {
    if (engine.banks.empty() || gas_path.heads.size() != engine.banks.size()) {
        return false;
    }
    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        if (!engine.banks[index].id.valid() || !gas_path.heads[index].bank_id.valid() ||
            gas_path.heads[index].bank_id != engine.banks[index].id ||
            (index != 0U &&
             engine.banks[index - 1U].id.value >= engine.banks[index].id.value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool single_crank_topology_matches(
    const contract::EngineSpec &engine,
    const contract::LegacyMechanismProfile &mechanism) noexcept {
    if (engine.crankshafts.size() != 1U || mechanism.cranks.size() != 1U ||
        !engine.output_crankshaft_id.valid() ||
        engine.output_crankshaft_id != mechanism.output_crankshaft_id) {
        return false;
    }
    const auto &public_crank = engine.crankshafts.front();
    const auto &resolved_crank = mechanism.cranks.front();
    return public_crank.id.valid() && resolved_crank.crankshaft_id.valid() &&
           public_crank.id == engine.output_crankshaft_id &&
           resolved_crank.crankshaft_id == mechanism.output_crankshaft_id &&
           public_crank.id == resolved_crank.crankshaft_id;
}

[[nodiscard]] bool co_phased_crank_group_topology_matches(
    const contract::EngineSpec &engine,
    const contract::LegacyMechanismProfile &mechanism) noexcept {
    if (engine.crankshafts.empty() ||
        engine.crankshafts.size() != mechanism.cranks.size() ||
        !engine.output_crankshaft_id.valid() ||
        engine.output_crankshaft_id != mechanism.output_crankshaft_id) {
        return false;
    }
    const auto *output_crank = contract::find_output_crank(mechanism);
    if (output_crank == nullptr ||
        !std::isfinite(output_crank->crank_tdc_reference_rad.value)) {
        return false;
    }
    const auto public_output = std::find_if(
        engine.crankshafts.begin(), engine.crankshafts.end(),
        [&](const auto &crank) { return crank.id == engine.output_crankshaft_id; });
    if (public_output == engine.crankshafts.end()) {
        return false;
    }
    for (std::size_t index = 0; index < engine.crankshafts.size(); ++index) {
        const auto &public_crank = engine.crankshafts[index];
        const auto &resolved_crank = mechanism.cranks[index];
        if (!public_crank.id.valid() || !resolved_crank.crankshaft_id.valid() ||
            public_crank.id != resolved_crank.crankshaft_id ||
            !std::isfinite(resolved_crank.crank_tdc_reference_rad.value) ||
            !same_binary64(resolved_crank.crank_tdc_reference_rad.value,
                           output_crank->crank_tdc_reference_rad.value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ValidationReport
validate_bank_head_topology(const contract::EngineSpec &engine,
                            const contract::LegacyGasPathProfile &gas_path) {
    ValidationReport report;
    bool ordered_engine_banks = !engine.banks.empty();
    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        ordered_engine_banks = ordered_engine_banks && engine.banks[index].id.valid() &&
                               (index == 0U || engine.banks[index - 1U].id.value <
                                                   engine.banks[index].id.value);
    }
    require(report, ordered_engine_banks, ContractIssueCode::inconsistent_shape,
            "engine.banks",
            "mechanism plan requires nonempty engine banks in unique BankId order");

    const bool exact_head_coverage = bank_head_topology_matches(engine, gas_path);
    require(report, exact_head_coverage, ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.heads",
            "mechanism plan requires exactly one head profile per engine bank in "
            "BankId order");

    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto bank_id = engine.cylinders[index].bank_id;
        require(report,
                bank_id.valid() && find_bank(engine, bank_id) != nullptr &&
                    find_head_profile(gas_path, bank_id) != nullptr,
                ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "].bank_id",
                "mechanism cylinder requires an existing bank and its exact "
                "bank-local head profile");
    }
    return report;
}

[[nodiscard]] double bank_angle_rad(const contract::BankSpec &bank) noexcept {
    return bank.angle_rad.has_value() ? bank.angle_rad->value : 0.0;
}

[[nodiscard]] const OneLevelMasterRodCylinder *
radial_cylinder(const OneLevelMasterRodMechanismCylinderPlan &planned) noexcept {
    return std::visit(
        [](const auto &kinematics) -> const OneLevelMasterRodCylinder * {
            return &kinematics.cylinder;
        },
        planned.kinematics);
}

[[nodiscard]] const char *
full_cycle_reason_name(const OneLevelMasterRodFullCycleReason reason) noexcept {
    switch (reason) {
    case OneLevelMasterRodFullCycleReason::admitted:
        return "admitted";
    case OneLevelMasterRodFullCycleReason::invalid_geometry:
        return "invalid_geometry";
    case OneLevelMasterRodFullCycleReason::reachability_not_certified:
        return "reachability_not_certified";
    case OneLevelMasterRodFullCycleReason::chamber_volume_not_certified:
        return "chamber_volume_not_certified";
    }
    return "invalid_geometry";
}

[[nodiscard]] MechanismKinematicsPlanCompileResult
compile_one_level_master_rod_kinematics_plan(
    const contract::EngineSpec &engine, const contract::LowOrderEngineCoreV1 &core) {
    ValidationReport report;
    const auto &mechanism = core.mechanism;
    const auto *output_crank = contract::find_output_crank(mechanism);
    require(report, single_crank_topology_matches(engine, mechanism),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cranks",
            "one-level master-rod execution currently requires one output "
            "crankshaft with matching public and resolved identity");
    require(report,
            mechanism.cylinders.size() == engine.cylinders.size() &&
                !mechanism.cylinders.empty(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "mechanism cylinders must match the nonempty engine cylinder order");
    require(report,
            mechanism.cylinders.size() <= std::numeric_limits<std::uint8_t>::max(),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cylinders",
            "event ordinals support at most 255 cylinders per mechanics session");
    require(report,
            output_crank != nullptr &&
                std::isfinite(output_crank->crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.output_crankshaft.crank_tdc_reference_"
            "rad.value",
            "crank TDC reference must be finite");
    if (!report.ok()) {
        return report;
    }

    std::unordered_map<std::uint32_t, std::size_t> cylinder_indices;
    cylinder_indices.reserve(engine.cylinders.size());
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        const auto &engine_cylinder = engine.cylinders[index];
        const auto &assembly = mechanism.cylinders[index];
        const auto path =
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) + "]";
        const bool identity_valid =
            engine_cylinder.id.valid() &&
            assembly.topology.cylinder_id == engine_cylinder.id &&
            engine_cylinder.crankshaft_id == mechanism.output_crankshaft_id &&
            assembly.topology.crankshaft_id == engine_cylinder.crankshaft_id &&
            cylinder_indices.emplace(engine_cylinder.id.value, index).second;
        require(report, identity_valid, ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "mechanism cylinder identity/order must be unique and match the "
                "engine topology");
    }
    if (!report.ok()) {
        return report;
    }

    OneLevelMasterRodMechanismKinematicsPlan plan;
    plan.engine_id = engine.id;
    plan.engine_profile_id = engine.profile_id.value;
    plan.output_crankshaft_id = mechanism.output_crankshaft_id;
    plan.crank_tdc_reference_rad = output_crank->crank_tdc_reference_rad.value;
    plan.cylinders.reserve(mechanism.cylinders.size());

    bool compiled_slave = false;
    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &engine_cylinder = engine.cylinders[index];
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto path =
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) + "]";
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == assembly.topology.exhaust_route_id;
            });
        const bool exhaust_route_valid =
            assembly.topology.exhaust_route_id.valid() &&
            route != engine.routes.end() &&
            route->kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust_route_valid, ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "mechanism cylinder requires an existing exhaust-outlet route");

        const auto *bank = find_bank(engine, engine_cylinder.bank_id);
        const auto *head = find_head_profile(core.gas_path, engine_cylinder.bank_id);
        const double resolved_bank_angle_rad =
            bank == nullptr ? std::numeric_limits<double>::quiet_NaN()
                            : bank_angle_rad(*bank);
        require(report, bank != nullptr && std::isfinite(resolved_bank_angle_rad),
                ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "].bank_id",
                "master-rod cylinder requires an existing bank with a finite "
                "axis angle");
        require(report, head != nullptr, ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "].bank_id",
                "master-rod cylinder requires one bank-local head profile");

        const double bore_m = parameters.bore_m.value;
        const double connecting_rod_length_m = parameters.connecting_rod_length_m.value;
        const double deck_height_m = parameters.deck_height_m.value;
        const double compression_height_m =
            parameters.piston_compression_height_m.value;
        const double head_volume_m3 = head == nullptr
                                          ? std::numeric_limits<double>::quiet_NaN()
                                          : head->chamber_volume_m3.value;
        const double piston_displacement_m3 =
            parameters.piston_displacement_term_m3.value;
        const double ignition_wire_angle_rad = parameters.ignition_wire_angle_rad.value;
        const double piston_area_m2 = kLegacyPi * bore_m * bore_m / 4.0;
        const double fixed_geometry_volume_m3 =
            head_volume_m3 + piston_area_m2 * (deck_height_m - compression_height_m);
        const bool common_geometry_valid =
            finite_positive(bore_m) && finite_positive(connecting_rod_length_m) &&
            finite_positive(deck_height_m) && finite_positive(compression_height_m) &&
            finite_positive(head_volume_m3) && std::isfinite(piston_displacement_m3) &&
            std::isfinite(ignition_wire_angle_rad) && finite_positive(piston_area_m2) &&
            finite_positive(fixed_geometry_volume_m3);
        require(report, common_geometry_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "one-level master-rod geometry must be finite and positive where "
                "required");

        const auto *direct =
            std::get_if<contract::LegacyDirectJournalKinematics>(&assembly.kinematics);
        const auto *slave = std::get_if<contract::LegacyMasterRodJournalKinematics>(
            &assembly.kinematics);
        require(report, direct != nullptr || slave != nullptr,
                ContractIssueCode::missing_value, path + ".kinematics",
                "master-rod mechanism plan requires an explicit journal "
                "attachment");

        OneLevelMasterRodMechanismCylinderPlan planned;
        planned.crankshaft_id = assembly.topology.crankshaft_id;
        planned.bank_id = engine_cylinder.bank_id;
        planned.chamber_volume_id = assembly.topology.chamber_volume_id;
        planned.exhaust_route_id = assembly.topology.exhaust_route_id;
        planned.bore_m = bore_m;
        planned.piston_area_m2 = piston_area_m2;
        planned.fixed_geometry_volume_m3 = fixed_geometry_volume_m3;
        planned.ignition_wire_angle_rad = ignition_wire_angle_rad;
        bool cylinder_compiled = false;

        if (direct != nullptr) {
            const bool attachment_matches =
                !engine_cylinder.master_rod_attachment.has_value();
            const bool direct_geometry_valid =
                finite_positive(direct->crank_radius_m.value) &&
                direct->crank_radius_m.value < connecting_rod_length_m &&
                std::isfinite(engine_cylinder.journal_phase_rad.value);
            require(
                report, attachment_matches, ContractIssueCode::inconsistent_semantics,
                "engine.cylinders[" + std::to_string(index) + "].master_rod_attachment",
                "direct root core kinematics require a direct public "
                "attachment");
            require(report, direct_geometry_valid, ContractIssueCode::invalid_value,
                    path + ".kinematics",
                    "direct master-rod root requires a finite global phase and a "
                    "positive crank radius below rod length");
            if (attachment_matches && direct_geometry_valid && common_geometry_valid &&
                exhaust_route_valid && bank != nullptr &&
                std::isfinite(resolved_bank_angle_rad)) {
                planned.kinematics = OneLevelMasterRodDirectRootPlan{
                    {
                        direct->crank_radius_m.value,
                        // Public journal phase is already global/body-local. Do
                        // not reconstruct it from the bank-relative direct core.
                        engine_cylinder.journal_phase_rad.value,
                        resolved_bank_angle_rad,
                        connecting_rod_length_m,
                    },
                    {
                        engine_cylinder.id,
                        resolved_bank_angle_rad,
                        connecting_rod_length_m,
                        piston_area_m2,
                        deck_height_m,
                        compression_height_m,
                        head_volume_m3,
                        piston_displacement_m3,
                        OneLevelMasterRodRootJournal{},
                    },
                };
                cylinder_compiled = true;
            }
        } else if (slave != nullptr) {
            compiled_slave = true;
            const auto master_index =
                cylinder_indices.find(slave->master_cylinder_id.value);
            const bool master_reference_valid =
                slave->master_cylinder_id.valid() &&
                master_index != cylinder_indices.end() && master_index->second != index;
            const auto &public_attachment = engine_cylinder.master_rod_attachment;
            const bool attachment_matches =
                public_attachment.has_value() &&
                public_attachment->master_cylinder_id == slave->master_cylinder_id &&
                same_binary64(public_attachment->throw_radius_m.value,
                              slave->throw_radius_m.value) &&
                same_binary64(engine_cylinder.journal_phase_rad.value,
                              slave->master_local_phase_rad.value);
            bool master_is_direct = false;
            if (master_reference_valid) {
                const auto referenced_index = master_index->second;
                master_is_direct =
                    !engine.cylinders[referenced_index]
                         .master_rod_attachment.has_value() &&
                    std::holds_alternative<contract::LegacyDirectJournalKinematics>(
                        mechanism.cylinders[referenced_index].kinematics);
            }
            const bool slave_geometry_valid =
                finite_positive(slave->throw_radius_m.value) &&
                std::isfinite(slave->master_local_phase_rad.value);
            require(report, master_reference_valid && master_is_direct,
                    ContractIssueCode::dangling_reference,
                    path + ".kinematics.master_cylinder_id",
                    "slave attachment must reference a stable direct-root "
                    "cylinder index");
            require(
                report, attachment_matches, ContractIssueCode::inconsistent_semantics,
                "engine.cylinders[" + std::to_string(index) + "].master_rod_attachment",
                "public and resolved slave attachment fields must match "
                "exactly");
            require(report, slave_geometry_valid, ContractIssueCode::invalid_value,
                    path + ".kinematics",
                    "slave attachment requires a positive throw and finite local "
                    "phase");
            if (master_reference_valid && master_is_direct && attachment_matches &&
                slave_geometry_valid && common_geometry_valid && exhaust_route_valid &&
                bank != nullptr && std::isfinite(resolved_bank_angle_rad)) {
                planned.kinematics = OneLevelMasterRodSlaveAttachmentPlan{
                    master_index->second,
                    {
                        engine_cylinder.id,
                        resolved_bank_angle_rad,
                        connecting_rod_length_m,
                        piston_area_m2,
                        deck_height_m,
                        compression_height_m,
                        head_volume_m3,
                        piston_displacement_m3,
                        OneLevelMasterRodSlavePin{
                            slave->throw_radius_m.value,
                            slave->master_local_phase_rad.value,
                        },
                    },
                };
                cylinder_compiled = true;
            }
        }

        if (cylinder_compiled) {
            plan.cylinders.push_back(std::move(planned));
        }
    }

    require(report, compiled_slave, ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "one-level master-rod plan requires at least one slave attachment");
    require(report, plan.cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "one-level master-rod plan must cover every cylinder exactly once");
    if (!report.ok()) {
        return report;
    }

    for (std::size_t index = 0; index < plan.cylinders.size(); ++index) {
        const auto &kinematics = plan.cylinders[index].kinematics;
        const OneLevelMasterRodDriver *driver = nullptr;
        const OneLevelMasterRodCylinder *cylinder = nullptr;
        if (const auto *root =
                std::get_if<OneLevelMasterRodDirectRootPlan>(&kinematics)) {
            driver = &root->driver;
            cylinder = &root->cylinder;
        } else if (const auto *slave =
                       std::get_if<OneLevelMasterRodSlaveAttachmentPlan>(&kinematics);
                   slave != nullptr &&
                   slave->master_cylinder_index < plan.cylinders.size()) {
            const auto *root = std::get_if<OneLevelMasterRodDirectRootPlan>(
                &plan.cylinders[slave->master_cylinder_index].kinematics);
            if (root != nullptr) {
                driver = &root->driver;
                cylinder = &slave->cylinder;
            }
        }

        OneLevelMasterRodFullCycleCheck certificate;
        if (driver != nullptr && cylinder != nullptr) {
            certificate = certify_one_level_master_rod_full_cycle(*driver, *cylinder);
        }
        if (!certificate.admitted()) {
            report.add(ContractIssueCode::unsupported_value,
                       "engine.physics_profile.mechanism.cylinders[" +
                           std::to_string(index) + "].kinematics",
                       "one-level master-rod full cycle is not certified: " +
                           std::string{full_cycle_reason_name(certificate.reason)});
        }
    }
    if (!report.ok()) {
        return report;
    }
    return std::make_shared<const MechanismKinematicsPlan>(
        std::in_place_type<OneLevelMasterRodMechanismKinematicsPlan>, std::move(plan));
}

[[nodiscard]] bool
contains_master_rod_attachment(const contract::EngineSpec &engine,
                               const contract::LowOrderEngineCoreV1 &core) noexcept {
    return std::any_of(engine.cylinders.begin(), engine.cylinders.end(),
                       [](const auto &cylinder) {
                           return cylinder.master_rod_attachment.has_value();
                       }) ||
           std::any_of(core.mechanism.cylinders.begin(), core.mechanism.cylinders.end(),
                       [](const auto &cylinder) {
                           return std::holds_alternative<
                               contract::LegacyMasterRodJournalKinematics>(
                               cylinder.kinematics);
                       });
}

} // namespace

MechanismKinematicsPlanCompileResult
compile_mechanism_kinematics_plan(const contract::EngineSpec &engine,
                                  const contract::LowOrderEngineCoreV1 &core) {
    auto bank_head_report = validate_bank_head_topology(engine, core.gas_path);
    if (!bank_head_report.ok()) {
        return bank_head_report;
    }
    if (contains_master_rod_attachment(engine, core)) {
        return compile_one_level_master_rod_kinematics_plan(engine, core);
    }

    ValidationReport report;
    const auto &mechanism = core.mechanism;
    const auto *output_crank = contract::find_output_crank(mechanism);
    require(report, co_phased_crank_group_topology_matches(engine, mechanism),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cranks",
            "direct mechanism execution requires exact ordered public/resolved "
            "crankshaft identity and one co-phased 1:1 rigid group");

    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &kinematics = mechanism.cylinders[index].kinematics;
        if (std::holds_alternative<std::monostate>(kinematics)) {
            report.add(ContractIssueCode::missing_value,
                       "engine.physics_profile.mechanism.cylinders[" +
                           std::to_string(index) + "].kinematics",
                       "mechanism cylinder requires an explicit journal attachment");
        }
    }
    if (!report.ok()) {
        return report;
    }

    require(report,
            output_crank != nullptr &&
                std::isfinite(output_crank->crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.output_crankshaft.crank_tdc_reference_"
            "rad.value",
            "crank TDC reference must be finite");
    require(report,
            mechanism.cylinders.size() == engine.cylinders.size() &&
                !mechanism.cylinders.empty(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "mechanism cylinders must match the nonempty engine cylinder order");
    require(report,
            mechanism.cylinders.size() <= std::numeric_limits<std::uint8_t>::max(),
            ContractIssueCode::unsupported_value,
            "engine.physics_profile.mechanism.cylinders",
            "event ordinals support at most 255 cylinders per mechanics session");
    if (!report.ok()) {
        return report;
    }

    DirectMechanismKinematicsPlan plan;
    plan.engine_id = engine.id;
    plan.engine_profile_id = engine.profile_id.value;
    plan.output_crankshaft_id = mechanism.output_crankshaft_id;
    plan.crank_tdc_reference_rad = output_crank->crank_tdc_reference_rad.value;
    plan.authored_crank_inertia_kg_m2 =
        output_crank->authored_crank_inertia_kg_m2.value;
    plan.cylinders.reserve(mechanism.cylinders.size());

    std::unordered_set<std::uint32_t> cylinder_ids;
    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &assembly = mechanism.cylinders[index];
        const auto *head =
            index < engine.cylinders.size()
                ? find_head_profile(core.gas_path, engine.cylinders[index].bank_id)
                : nullptr;
        const auto &parameters = assembly.parameters;
        const auto *direct =
            std::get_if<contract::LegacyDirectJournalKinematics>(&assembly.kinematics);
        const auto *cylinder_crank =
            contract::find_crank(mechanism, assembly.topology.crankshaft_id);
        const auto path =
            "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) + "]";
        const bool identity_valid =
            assembly.topology.cylinder_id.valid() && index < engine.cylinders.size() &&
            assembly.topology.cylinder_id == engine.cylinders[index].id &&
            assembly.topology.crankshaft_id == engine.cylinders[index].crankshaft_id &&
            cylinder_crank != nullptr &&
            cylinder_ids.insert(assembly.topology.cylinder_id.value).second;
        require(report, identity_valid, ContractIssueCode::inconsistent_semantics,
                path + ".topology.cylinder_id",
                "mechanism cylinder identity/order must be unique and match the "
                "engine topology");
        require(report, head != nullptr, ContractIssueCode::dangling_reference,
                "engine.cylinders[" + std::to_string(index) + "].bank_id",
                "direct cylinder requires one bank-local head profile");
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == assembly.topology.exhaust_route_id;
            });
        const bool exhaust_route_valid =
            assembly.topology.exhaust_route_id.valid() &&
            route != engine.routes.end() &&
            route->kind.value == contract::SourceRouteKind::exhaust_outlet;
        require(report, exhaust_route_valid, ContractIssueCode::dangling_reference,
                path + ".topology.exhaust_route_id",
                "mechanism cylinder requires an existing exhaust-outlet route");

        require(report, direct != nullptr, ContractIssueCode::unsupported_value,
                path + ".kinematics",
                "direct mechanism plan requires direct-journal kinematics");
        if (direct == nullptr) {
            continue;
        }

        const double bore_m = parameters.bore_m.value;
        const double stroke_m = direct->stroke_m.value;
        const double crank_radius_m = direct->crank_radius_m.value;
        const double rod_length_m = parameters.connecting_rod_length_m.value;
        const double deck_height_m = parameters.deck_height_m.value;
        const double compression_height_m =
            parameters.piston_compression_height_m.value;
        const double head_volume_m3 = head == nullptr
                                          ? std::numeric_limits<double>::quiet_NaN()
                                          : head->chamber_volume_m3.value;
        const double piston_displacement_m3 =
            parameters.piston_displacement_term_m3.value;
        const double journal_angle_rad = direct->journal_angle_rad.value;
        const double ignition_wire_angle_rad = parameters.ignition_wire_angle_rad.value;

        const bool numeric_inputs_valid =
            finite_positive(bore_m) && finite_positive(stroke_m) &&
            finite_positive(crank_radius_m) &&
            same_binary64(stroke_m, 2.0 * crank_radius_m) &&
            finite_positive(rod_length_m) && crank_radius_m < rod_length_m &&
            finite_positive(deck_height_m) && finite_positive(compression_height_m) &&
            finite_positive(head_volume_m3) && std::isfinite(piston_displacement_m3) &&
            std::isfinite(journal_angle_rad) && std::isfinite(ignition_wire_angle_rad);
        require(report, numeric_inputs_valid, ContractIssueCode::invalid_value,
                path + ".parameters",
                "centered slider-crank inputs must be finite, positive where "
                "required, and have crank radius below rod length");
        if (!numeric_inputs_valid || !identity_valid || !exhaust_route_valid ||
            head == nullptr) {
            continue;
        }

        // Keep this direct-path calculation sequence byte-for-byte aligned with
        // the former mechanics compiler: derive first, then form/wrap TDC.
        const auto geometry = derive_legacy_cylinder_geometry(
            bore_m, crank_radius_m, rod_length_m, deck_height_m, compression_height_m,
            head_volume_m3, piston_displacement_m3);
        const double geometric_tdc_rad =
            legacy_wrap_2pi(cylinder_crank->crank_tdc_reference_rad.value +
                            journal_angle_rad - kLegacyPi / 2.0);
        const bool derived_valid = finite_positive(geometry.piston_area_m2) &&
                                   finite_positive(geometry.clearance_volume_m3) &&
                                   finite_positive(geometry.fixed_geometry_volume_m3) &&
                                   std::isfinite(geometric_tdc_rad);
        require(report, derived_valid, ContractIssueCode::invalid_value, path,
                "compiled slider-crank area, clearance, and phase must be valid");
        if (!derived_valid) {
            continue;
        }

        plan.cylinders.push_back({
            assembly.topology.crankshaft_id,
            {
                assembly.topology.cylinder_id,
                geometric_tdc_rad,
                geometry.piston_area_m2,
                crank_radius_m,
                rod_length_m,
                geometry.clearance_volume_m3,
                ignition_wire_angle_rad,
            },
            assembly.topology.chamber_volume_id,
            assembly.topology.exhaust_route_id,
            bore_m,
            stroke_m,
            deck_height_m,
            compression_height_m,
            head_volume_m3,
            piston_displacement_m3,
            journal_angle_rad,
            geometry.fixed_geometry_volume_m3,
            parameters.piston_mass_kg.value,
            parameters.connecting_rod_mass_kg.value,
            parameters.connecting_rod_inertia_kg_m2.value,
        });
    }

    const auto cycle_mean_calculation =
        calculate_centered_slider_crank_cycle_mean_inertia(mechanism);
    const auto *cycle_mean =
        std::get_if<CenteredSliderCrankCycleMeanInertia>(&cycle_mean_calculation);
    require(report, cycle_mean != nullptr, ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism",
            "configuration-dependent inertia rejected the admitted centered-slider "
            "mechanism");
    require(report, plan.cylinders.size() == mechanism.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "direct mechanism plan must cover every mechanism cylinder exactly "
            "once");
    if (!report.ok() || cycle_mean == nullptr) {
        return report;
    }
    plan.cycle_mean_inertia = *cycle_mean;

    return std::make_shared<const MechanismKinematicsPlan>(
        std::in_place_type<DirectMechanismKinematicsPlan>, std::move(plan));
}

const DirectMechanismKinematicsPlan *
direct_mechanism_kinematics_plan(const SharedMechanismKinematicsPlan &plan) noexcept {
    return plan == nullptr ? nullptr
                           : std::get_if<DirectMechanismKinematicsPlan>(plan.get());
}

const OneLevelMasterRodMechanismKinematicsPlan *
one_level_master_rod_mechanism_kinematics_plan(
    const SharedMechanismKinematicsPlan &plan) noexcept {
    return plan == nullptr
               ? nullptr
               : std::get_if<OneLevelMasterRodMechanismKinematicsPlan>(plan.get());
}

OneLevelMasterRodSample
evaluate_one_level_master_rod_plan(const OneLevelMasterRodMechanismKinematicsPlan &plan,
                                   const std::size_t cylinder_index,
                                   const double body_angle_psi_rad,
                                   const double angular_speed_rad_s) noexcept {
    if (cylinder_index >= plan.cylinders.size()) {
        return {};
    }
    const auto &kinematics = plan.cylinders[cylinder_index].kinematics;
    if (const auto *root = std::get_if<OneLevelMasterRodDirectRootPlan>(&kinematics)) {
        return evaluate_one_level_master_rod(root->driver, root->cylinder,
                                             body_angle_psi_rad, angular_speed_rad_s);
    }
    const auto *slave = std::get_if<OneLevelMasterRodSlaveAttachmentPlan>(&kinematics);
    if (slave == nullptr || slave->master_cylinder_index >= plan.cylinders.size()) {
        return {};
    }
    const auto *root = std::get_if<OneLevelMasterRodDirectRootPlan>(
        &plan.cylinders[slave->master_cylinder_index].kinematics);
    if (root == nullptr) {
        return {};
    }
    return evaluate_one_level_master_rod(root->driver, slave->cylinder,
                                         body_angle_psi_rad, angular_speed_rad_s);
}

bool mechanism_kinematics_plan_matches_source(
    const SharedMechanismKinematicsPlan &plan, const contract::EngineSpec &engine,
    const contract::LowOrderEngineCoreV1 &core) noexcept {
    const auto &mechanism = core.mechanism;
    const auto *output_crank = contract::find_output_crank(mechanism);
    if (!bank_head_topology_matches(engine, core.gas_path) || output_crank == nullptr) {
        return false;
    }
    const auto *radial = one_level_master_rod_mechanism_kinematics_plan(plan);
    if (radial != nullptr) {
        if (!single_crank_topology_matches(engine, mechanism) ||
            radial->engine_id != engine.id ||
            radial->engine_profile_id != engine.profile_id.value ||
            radial->output_crankshaft_id != mechanism.output_crankshaft_id ||
            radial->cylinders.empty() ||
            radial->cylinders.size() != engine.cylinders.size() ||
            radial->cylinders.size() != mechanism.cylinders.size() ||
            !same_binary64(radial->crank_tdc_reference_rad,
                           output_crank->crank_tdc_reference_rad.value)) {
            return false;
        }

        for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
            const auto duplicate =
                std::find_if(engine.cylinders.begin(), engine.cylinders.begin() + index,
                             [&](const auto &candidate) {
                                 return candidate.id == engine.cylinders[index].id;
                             });
            if (!engine.cylinders[index].id.valid() ||
                duplicate != engine.cylinders.begin() + index) {
                return false;
            }
        }

        bool matched_slave = false;
        for (std::size_t index = 0; index < radial->cylinders.size(); ++index) {
            const auto &planned = radial->cylinders[index];
            const auto &engine_cylinder = engine.cylinders[index];
            const auto &assembly = mechanism.cylinders[index];
            const auto &parameters = assembly.parameters;
            const auto *geometry = radial_cylinder(planned);
            const auto *bank = find_bank(engine, engine_cylinder.bank_id);
            const auto *head =
                find_head_profile(core.gas_path, engine_cylinder.bank_id);
            const auto route = std::find_if(
                engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                    return candidate.id == planned.exhaust_route_id;
                });
            if (geometry == nullptr || bank == nullptr || head == nullptr ||
                route == engine.routes.end() ||
                route->kind.value != contract::SourceRouteKind::exhaust_outlet ||
                planned.crankshaft_id != engine_cylinder.crankshaft_id ||
                planned.crankshaft_id != assembly.topology.crankshaft_id ||
                planned.bank_id != engine_cylinder.bank_id ||
                geometry->cylinder_id != engine_cylinder.id ||
                geometry->cylinder_id != assembly.topology.cylinder_id ||
                planned.chamber_volume_id != assembly.topology.chamber_volume_id ||
                planned.exhaust_route_id != assembly.topology.exhaust_route_id ||
                !same_binary64(geometry->bank_angle_rad, bank_angle_rad(*bank)) ||
                !same_binary64(geometry->connecting_rod_length_m,
                               parameters.connecting_rod_length_m.value) ||
                !same_binary64(geometry->piston_area_m2, planned.piston_area_m2) ||
                !same_binary64(geometry->deck_height_m,
                               parameters.deck_height_m.value) ||
                !same_binary64(geometry->piston_compression_height_m,
                               parameters.piston_compression_height_m.value) ||
                !same_binary64(geometry->head_chamber_volume_m3,
                               head->chamber_volume_m3.value) ||
                !same_binary64(geometry->piston_displacement_term_m3,
                               parameters.piston_displacement_term_m3.value) ||
                !same_binary64(planned.bore_m, parameters.bore_m.value) ||
                !same_binary64(planned.piston_area_m2,
                               kLegacyPi * parameters.bore_m.value *
                                   parameters.bore_m.value / 4.0) ||
                !same_binary64(
                    planned.fixed_geometry_volume_m3,
                    head->chamber_volume_m3.value +
                        planned.piston_area_m2 *
                            (parameters.deck_height_m.value -
                             parameters.piston_compression_height_m.value)) ||
                !same_binary64(planned.ignition_wire_angle_rad,
                               parameters.ignition_wire_angle_rad.value)) {
                return false;
            }

            if (const auto *root =
                    std::get_if<OneLevelMasterRodDirectRootPlan>(&planned.kinematics)) {
                const auto *direct =
                    std::get_if<contract::LegacyDirectJournalKinematics>(
                        &assembly.kinematics);
                if (direct == nullptr ||
                    engine_cylinder.master_rod_attachment.has_value() ||
                    !std::holds_alternative<OneLevelMasterRodRootJournal>(
                        root->cylinder.journal) ||
                    !same_binary64(root->driver.crank_radius_m,
                                   direct->crank_radius_m.value) ||
                    !same_binary64(root->driver.crank_journal_global_phase_rad,
                                   engine_cylinder.journal_phase_rad.value) ||
                    !same_binary64(root->driver.master_bank_angle_rad,
                                   root->cylinder.bank_angle_rad) ||
                    !same_binary64(root->driver.master_connecting_rod_length_m,
                                   root->cylinder.connecting_rod_length_m)) {
                    return false;
                }
            } else {
                const auto *slave = std::get_if<OneLevelMasterRodSlaveAttachmentPlan>(
                    &planned.kinematics);
                const auto *resolved_attachment =
                    std::get_if<contract::LegacyMasterRodJournalKinematics>(
                        &assembly.kinematics);
                const auto *pin = slave == nullptr
                                      ? nullptr
                                      : std::get_if<OneLevelMasterRodSlavePin>(
                                            &slave->cylinder.journal);
                if (slave == nullptr || resolved_attachment == nullptr ||
                    pin == nullptr ||
                    !engine_cylinder.master_rod_attachment.has_value() ||
                    slave->master_cylinder_index >= radial->cylinders.size()) {
                    return false;
                }
                const auto master = std::find_if(
                    engine.cylinders.begin(), engine.cylinders.end(),
                    [&](const auto &candidate) {
                        return candidate.id == resolved_attachment->master_cylinder_id;
                    });
                const auto master_index =
                    static_cast<std::size_t>(master - engine.cylinders.begin());
                if (master == engine.cylinders.end() ||
                    master_index != slave->master_cylinder_index ||
                    std::get_if<OneLevelMasterRodDirectRootPlan>(
                        &radial->cylinders[slave->master_cylinder_index].kinematics) ==
                        nullptr ||
                    engine_cylinder.master_rod_attachment->master_cylinder_id !=
                        resolved_attachment->master_cylinder_id ||
                    !same_binary64(
                        engine_cylinder.master_rod_attachment->throw_radius_m.value,
                        resolved_attachment->throw_radius_m.value) ||
                    !same_binary64(engine_cylinder.journal_phase_rad.value,
                                   resolved_attachment->master_local_phase_rad.value) ||
                    !same_binary64(pin->throw_radius_m,
                                   resolved_attachment->throw_radius_m.value) ||
                    !same_binary64(pin->local_phase_rad,
                                   resolved_attachment->master_local_phase_rad.value)) {
                    return false;
                }
                matched_slave = true;
            }
        }
        return matched_slave;
    }

    const auto *direct = direct_mechanism_kinematics_plan(plan);
    if (!co_phased_crank_group_topology_matches(engine, mechanism) ||
        direct == nullptr || direct->engine_id != engine.id ||
        direct->engine_profile_id != engine.profile_id.value ||
        direct->output_crankshaft_id != mechanism.output_crankshaft_id ||
        direct->cylinders.empty() ||
        direct->cylinders.size() != engine.cylinders.size() ||
        direct->cylinders.size() != mechanism.cylinders.size() ||
        !same_binary64(direct->crank_tdc_reference_rad,
                       output_crank->crank_tdc_reference_rad.value) ||
        !same_binary64(direct->authored_crank_inertia_kg_m2,
                       output_crank->authored_crank_inertia_kg_m2.value) ||
        !same_binary64(direct->cycle_mean_inertia.authored_crank_inertia_kg_m2,
                       output_crank->authored_crank_inertia_kg_m2.value)) {
        return false;
    }

    for (std::size_t index = 0; index < direct->cylinders.size(); ++index) {
        const auto &planned = direct->cylinders[index];
        const auto &assembly = mechanism.cylinders[index];
        const auto &parameters = assembly.parameters;
        const auto *head =
            find_head_profile(core.gas_path, engine.cylinders[index].bank_id);
        const auto *kinematics =
            std::get_if<contract::LegacyDirectJournalKinematics>(&assembly.kinematics);
        const auto route = std::find_if(
            engine.routes.begin(), engine.routes.end(), [&](const auto &candidate) {
                return candidate.id == planned.exhaust_route_id;
            });
        if (kinematics == nullptr || head == nullptr ||
            engine.cylinders[index].master_rod_attachment.has_value() ||
            planned.crankshaft_id != engine.cylinders[index].crankshaft_id ||
            planned.crankshaft_id != assembly.topology.crankshaft_id ||
            planned.crank.cylinder_id != engine.cylinders[index].id ||
            planned.crank.cylinder_id != assembly.topology.cylinder_id ||
            planned.chamber_volume_id != assembly.topology.chamber_volume_id ||
            planned.exhaust_route_id != assembly.topology.exhaust_route_id ||
            route == engine.routes.end() ||
            route->kind.value != contract::SourceRouteKind::exhaust_outlet ||
            !same_binary64(planned.bore_m, parameters.bore_m.value) ||
            !same_binary64(planned.stroke_m, kinematics->stroke_m.value) ||
            !same_binary64(planned.crank.crank_radius_m,
                           kinematics->crank_radius_m.value) ||
            !same_binary64(planned.crank.connecting_rod_length_m,
                           parameters.connecting_rod_length_m.value) ||
            !same_binary64(planned.deck_height_m, parameters.deck_height_m.value) ||
            !same_binary64(planned.piston_compression_height_m,
                           parameters.piston_compression_height_m.value) ||
            !same_binary64(planned.head_chamber_volume_m3,
                           head->chamber_volume_m3.value) ||
            !same_binary64(planned.piston_displacement_term_m3,
                           parameters.piston_displacement_term_m3.value) ||
            !same_binary64(planned.authored_journal_angle_rad,
                           kinematics->journal_angle_rad.value) ||
            !same_binary64(planned.crank.ignition_wire_angle_rad,
                           parameters.ignition_wire_angle_rad.value) ||
            !same_binary64(planned.piston_mass_kg, parameters.piston_mass_kg.value) ||
            !same_binary64(planned.connecting_rod_mass_kg,
                           parameters.connecting_rod_mass_kg.value) ||
            !same_binary64(planned.connecting_rod_inertia_kg_m2,
                           parameters.connecting_rod_inertia_kg_m2.value)) {
            return false;
        }
    }
    return true;
}

} // namespace engine_sim_offline::simulation
