#include "simulation/legacy_fixed_valvetrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

namespace engine_sim_offline::simulation {
namespace {

using contract::ContractIssueCode;
using contract::ValidationReport;

struct AdmittedCamShape {
    double radius_rad = 0.0;
};

// Admission bound for profile-authored table construction. Runtime sampling cost is
// independent of this count, but compilation must not permit a uint32-sized hostile
// allocation. The canonical profile uses 100 construction steps.
inline constexpr std::uint32_t kMaximumLegacyCamConstructionSteps = 1'000'000U;

void require(ValidationReport &report, bool condition, ContractIssueCode code,
             std::string path, std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool exact_legacy_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method) noexcept {
    return method.value.id == "legacy_low_order_v1" && method.value.version == 1;
}

template <class Predicate>
[[nodiscard]] bool all_unique(std::size_t size, Predicate predicate) {
    for (std::size_t left = 0; left < size; ++left) {
        for (std::size_t right = left + 1U; right < size; ++right) {
            if (predicate(left, right)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::optional<AdmittedCamShape>
admit_cam_shape(const contract::LegacyHarmonicCamShape &shape, ValidationReport &report,
                const std::string &path) {
    const double maximum_lift_m = shape.maximum_lift_m.value;
    const double duration_at_reference_lift_rad =
        shape.duration_at_reference_lift_rad.value;
    const double exponent = shape.exponent.value;
    const std::uint32_t construction_steps = shape.construction_steps.value;

    const bool direct_values_valid =
        finite_positive(maximum_lift_m) &&
        finite_positive(duration_at_reference_lift_rad) && finite_positive(exponent) &&
        construction_steps > 5U &&
        construction_steps <= kMaximumLegacyCamConstructionSteps &&
        std::isfinite(shape.advance_rad.value) &&
        finite_positive(shape.base_radius_m.value);
    require(report, direct_values_valid, ContractIssueCode::invalid_value,
            path + ".shape",
            "legacy cam shape requires finite positive lift, duration, exponent, "
            "and base radius, finite advance, and a construction-step count in the "
            "supported range [6,1000000]");
    if (!direct_values_valid) {
        return std::nullopt;
    }

    const double centimetre_source = 1.0 / 100.0;
    const double inch_source = centimetre_source * 2.54;
    const double reference_lift_m = 50.0 * (inch_source / 1000.0);
    const double angle = duration_at_reference_lift_rad / 4.0;
    const double q =
        std::pow(2.0 * reference_lift_m / maximum_lift_m, 1.0 / exponent) - 1.0;
    const bool q_valid = std::isfinite(q) && q >= -1.0 && q <= 1.0;
    require(report, q_valid, ContractIssueCode::invalid_value, path + ".shape",
            "legacy harmonic cam construction requires an acos argument in [-1,1]");
    if (!q_valid) {
        return std::nullopt;
    }

    const double k = std::acos(q) / angle;
    const double extent_rad = kLegacyPi / k;
    const double radius_rad =
        extent_rad / (static_cast<double>(construction_steps) - 5.0);
    const bool derived_values_valid = finite_positive(k) &&
                                      finite_positive(extent_rad) &&
                                      finite_positive(radius_rad);
    require(report, derived_values_valid, ContractIssueCode::invalid_value,
            path + ".shape",
            "legacy harmonic cam construction produced a non-finite or zero scale");
    if (!derived_values_valid) {
        return std::nullopt;
    }

    return AdmittedCamShape{radius_rad};
}

[[nodiscard]] std::optional<AdmittedCamShape>
admit_cam_shape(const contract::LegacySampledCamShape &shape, ValidationReport &report,
                const std::string &path) {
    const bool metadata_valid = finite_positive(shape.triangle_radius_rad.value) &&
                                std::isfinite(shape.advance_rad.value) &&
                                finite_positive(shape.base_radius_m.value);
    require(report, metadata_valid, ContractIssueCode::invalid_value, path + ".shape",
            "sampled cam shape requires a finite positive triangle radius and base "
            "radius and finite advance");

    const bool count_valid = shape.samples.size() >= 2U;
    require(report, count_valid, ContractIssueCode::inconsistent_shape,
            path + ".shape.samples",
            "sampled cam shape requires at least two angle/lift samples");

    bool points_valid = true;
    bool unique_sample_ids = true;
    std::unordered_set<std::string> sample_ids;
    sample_ids.reserve(shape.samples.size());
    for (std::size_t index = 0; index < shape.samples.size(); ++index) {
        const auto &point = shape.samples[index];
        const bool point_valid =
            contract::is_valid_semantic_id(point.sample_id.value) &&
            std::isfinite(point.angle_rad.value) && std::isfinite(point.lift_m.value) &&
            point.lift_m.value >= 0.0 &&
            (index == 0U ||
             point.angle_rad.value > shape.samples[index - 1U].angle_rad.value);
        require(report, point_valid, ContractIssueCode::invalid_value,
                path + ".shape.samples[" + std::to_string(index) + "]",
                "sampled cam points require identity, finite nonnegative lift, and "
                "finite strictly increasing angle");
        points_valid = points_valid && point_valid;
        unique_sample_ids =
            sample_ids.insert(point.sample_id.value).second && unique_sample_ids;
    }
    require(report, unique_sample_ids, ContractIssueCode::duplicate_identity,
            path + ".shape.samples",
            "sampled cam point identities must be unique within the profile");

    if (!metadata_valid || !count_valid || !points_valid || !unique_sample_ids) {
        return std::nullopt;
    }
    return AdmittedCamShape{shape.triangle_radius_rad.value};
}

[[nodiscard]] std::optional<AdmittedCamShape>
admit_cam_shape(const contract::LegacyCamShape &shape, ValidationReport &report,
                const std::string &path) {
    return std::visit(
        [&](const auto &resolved) { return admit_cam_shape(resolved, report, path); },
        shape);
}

[[nodiscard]] std::vector<LegacyTrianglePoint>
construct_lobe_table(const contract::LegacyHarmonicCamShape &shape, double radius_rad) {
    const double centimetre_source = 1.0 / 100.0;
    const double inch_source = centimetre_source * 2.54;
    const double reference_lift_m = 50.0 * (inch_source / 1000.0);
    const double maximum_lift_m = shape.maximum_lift_m.value;
    const double exponent = shape.exponent.value;
    const double angle = shape.duration_at_reference_lift_rad.value / 4.0;
    const double q =
        std::pow(2.0 * reference_lift_m / maximum_lift_m, 1.0 / exponent) - 1.0;
    const double k = std::acos(q) / angle;
    const double extent_rad = kLegacyPi / k;
    const std::uint32_t construction_steps = shape.construction_steps.value;

    std::vector<LegacyTrianglePoint> table;
    table.reserve(static_cast<std::size_t>(construction_steps) * 2U - 1U);
    table.push_back({0.0, maximum_lift_m});
    for (std::uint32_t index = 1; index < construction_steps; ++index) {
        const double x = static_cast<double>(index) * radius_rad;
        const double y =
            x >= extent_rad
                ? 0.0
                : maximum_lift_m * std::pow(0.5 + 0.5 * std::cos(k * x), exponent);
        table.push_back({x, y});
        table.push_back({-x, y});
    }
    std::sort(table.begin(), table.end(),
              [](const LegacyTrianglePoint &left, const LegacyTrianglePoint &right) {
                  return left.x < right.x;
              });
    return table;
}

[[nodiscard]] std::vector<LegacyTrianglePoint>
construct_lobe_table(const contract::LegacySampledCamShape &shape, double) {
    std::vector<LegacyTrianglePoint> table;
    table.reserve(shape.samples.size());
    for (const auto &point : shape.samples) {
        table.push_back({point.angle_rad.value, point.lift_m.value});
    }
    return table;
}

[[nodiscard]] std::vector<LegacyTrianglePoint>
construct_lobe_table(const contract::LegacyCamShape &shape, double radius_rad) {
    return std::visit(
        [&](const auto &resolved) {
            return construct_lobe_table(resolved, radius_rad);
        },
        shape);
}

[[nodiscard]] double cam_advance_rad(const contract::LegacyCamShape &shape) noexcept {
    return std::visit([](const auto &resolved) { return resolved.advance_rad.value; },
                      shape);
}

[[nodiscard]] double cam_base_radius_m(const contract::LegacyCamShape &shape) noexcept {
    return std::visit([](const auto &resolved) { return resolved.base_radius_m.value; },
                      shape);
}

[[nodiscard]] LegacyCompiledCamProfile
compile_cam_profile(const contract::LegacyCamShape &shape,
                    const AdmittedCamShape admitted) {
    return {
        construct_lobe_table(shape, admitted.radius_rad),
        admitted.radius_rad,
        cam_advance_rad(shape),
        cam_base_radius_m(shape),
    };
}

void admit_flow_table(const std::vector<contract::LegacyValveFlowPoint> &source,
                      ValidationReport &report, const std::string &path) {
    require(report, source.size() >= 2U, ContractIssueCode::inconsistent_shape, path,
            "legacy valve-flow table requires at least two samples");
    for (std::size_t index = 0; index < source.size(); ++index) {
        const auto &point = source[index];
        const bool point_valid =
            contract::is_valid_semantic_id(point.sample_id.value) &&
            std::isfinite(point.lift_m.value) && point.lift_m.value >= 0.0 &&
            std::isfinite(point.source_cfm_at_28_inh2o.value) &&
            point.source_cfm_at_28_inh2o.value >= 0.0 &&
            std::isfinite(point.resolved_k.value) && point.resolved_k.value >= 0.0 &&
            (index == 0 || point.lift_m.value > source[index - 1].lift_m.value);
        require(report, point_valid, ContractIssueCode::invalid_value,
                path + "[" + std::to_string(index) + "]",
                "valve-flow samples require identity, finite nonnegative values, "
                "and strictly increasing lift");
    }
    const bool unique_sample_ids =
        all_unique(source.size(), [&](std::size_t left, std::size_t right) {
            return source[left].sample_id.value == source[right].sample_id.value;
        });
    require(report, unique_sample_ids, ContractIssueCode::duplicate_identity, path,
            "valve-flow sample identities must be unique");
}

[[nodiscard]] std::vector<LegacyTrianglePoint>
compile_flow_table(const std::vector<contract::LegacyValveFlowPoint> &source) {
    std::vector<LegacyTrianglePoint> result;
    result.reserve(source.size());
    for (const auto &point : source) {
        result.push_back({point.lift_m.value, point.resolved_k.value});
    }
    return result;
}

[[nodiscard]] const contract::LegacyCamLobe *
find_lobe(const contract::LegacyCamshaftProfile &camshaft,
          contract::CylinderId cylinder_id) noexcept {
    const auto found =
        std::find_if(camshaft.lobes.begin(), camshaft.lobes.end(),
                     [&](const auto &lobe) { return lobe.cylinder_id == cylinder_id; });
    return found == camshaft.lobes.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::PortSpec *find_port(const contract::EngineSpec &engine,
                                                  contract::PortId port_id) noexcept {
    const auto found =
        std::find_if(engine.ports.begin(), engine.ports.end(),
                     [&](const auto &port) { return port.id == port_id; });
    return found == engine.ports.end() ? nullptr : &*found;
}

[[nodiscard]] const contract::LegacyBankHeadProfile *
find_head(const contract::LegacyGasPathProfile &gas_path,
          contract::BankId bank_id) noexcept {
    const auto found =
        std::find_if(gas_path.heads.begin(), gas_path.heads.end(),
                     [&](const auto &head) { return head.bank_id == bank_id; });
    return found == gas_path.heads.end() ? nullptr : &*found;
}

[[nodiscard]] std::optional<std::size_t>
find_bank_index(const contract::EngineSpec &engine, contract::BankId bank_id) noexcept {
    const auto found =
        std::find_if(engine.banks.begin(), engine.banks.end(),
                     [&](const auto &bank) { return bank.id == bank_id; });
    if (found == engine.banks.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - engine.banks.begin());
}

[[nodiscard]] double wrap_to_minus_pi_inclusive(double value) noexcept {
    double wrapped = std::fmod(value, 2.0 * kLegacyPi);
    if (wrapped < 0.0) {
        wrapped += 2.0 * kLegacyPi;
    }
    if (wrapped >= kLegacyPi) {
        wrapped -= 2.0 * kLegacyPi;
    }
    return wrapped;
}

[[nodiscard]] double cam_base(double body_angle_psi_rad, double crank_tdc_reference_rad,
                              double advance_rad) noexcept {
    const double crank_get_angle = body_angle_psi_rad - crank_tdc_reference_rad;
    double result = std::fmod((crank_get_angle + advance_rad) * 0.5, 2.0 * kLegacyPi);
    if (result < 0.0) {
        result += 2.0 * kLegacyPi;
    }
    return result;
}

} // namespace

LegacyFixedValvetrain::LegacyFixedValvetrain(
    double crank_tdc_reference_rad,
    std::vector<LegacyValvetrainCylinderBinding> cylinder_bindings,
    std::vector<LegacyCompiledCamProfile> intake_cam_profiles,
    std::vector<LegacyCompiledCamProfile> exhaust_cam_profiles,
    std::vector<LegacyValvetrainFlowProfile> flow_profiles)
    : crank_tdc_reference_rad_(crank_tdc_reference_rad),
      cylinder_bindings_(std::move(cylinder_bindings)),
      intake_cam_profiles_(std::move(intake_cam_profiles)),
      exhaust_cam_profiles_(std::move(exhaust_cam_profiles)),
      flow_profiles_(std::move(flow_profiles)) {}

std::span<const LegacyValvetrainCylinderBinding>
LegacyFixedValvetrain::cylinder_bindings() const noexcept {
    return cylinder_bindings_;
}

std::span<const LegacyCompiledCamProfile>
LegacyFixedValvetrain::intake_cam_profiles() const noexcept {
    return intake_cam_profiles_;
}

std::span<const LegacyCompiledCamProfile>
LegacyFixedValvetrain::exhaust_cam_profiles() const noexcept {
    return exhaust_cam_profiles_;
}

std::span<const LegacyValvetrainFlowProfile>
LegacyFixedValvetrain::flow_profiles() const noexcept {
    return flow_profiles_;
}

LegacyCylinderValveSample LegacyFixedValvetrain::sample_admitted_cylinder(
    std::size_t cylinder_index, double body_angle_psi_rad) const noexcept {
    const auto &binding = cylinder_bindings_[cylinder_index];
    const auto &flow_profile = flow_profiles_[binding.flow_profile_index];
    const auto &intake_profile = intake_cam_profiles_[binding.intake_cam_profile_index];
    const auto &exhaust_profile =
        exhaust_cam_profiles_[binding.exhaust_cam_profile_index];
    const double intake_base = cam_base(body_angle_psi_rad, crank_tdc_reference_rad_,
                                        intake_profile.advance_rad);
    const double exhaust_base = cam_base(body_angle_psi_rad, crank_tdc_reference_rad_,
                                         exhaust_profile.advance_rad);
    const double intake_argument =
        wrap_to_minus_pi_inclusive(intake_base + binding.intake_stored_lobe_angle_rad);
    const double exhaust_argument = wrap_to_minus_pi_inclusive(
        exhaust_base + binding.exhaust_stored_lobe_angle_rad);
    const double intake_lift =
        legacy_triangle_sample(intake_profile.lobe_table, intake_argument,
                               intake_profile.lobe_triangle_radius_rad);
    const double exhaust_lift =
        legacy_triangle_sample(exhaust_profile.lobe_table, exhaust_argument,
                               exhaust_profile.lobe_triangle_radius_rad);

    return {
        binding.cylinder_id,
        binding.intake_port_id,
        binding.exhaust_port_id,
        intake_argument,
        exhaust_argument,
        intake_lift,
        exhaust_lift,
        legacy_triangle_sample(flow_profile.intake_flow_table, intake_lift,
                               flow_profile.intake_flow_triangle_radius_m),
        legacy_triangle_sample(flow_profile.exhaust_flow_table, exhaust_lift,
                               flow_profile.exhaust_flow_triangle_radius_m),
    };
}

std::optional<LegacyCylinderValveSample>
LegacyFixedValvetrain::sample_cylinder(std::size_t cylinder_index,
                                       double body_angle_psi_rad) const noexcept {
    if (cylinder_index >= cylinder_bindings_.size() ||
        !std::isfinite(body_angle_psi_rad)) {
        return std::nullopt;
    }
    return sample_admitted_cylinder(cylinder_index, body_angle_psi_rad);
}

std::optional<LegacyCylinderValveSample>
LegacyFixedValvetrain::sample_cylinder(contract::CylinderId cylinder_id,
                                       double body_angle_psi_rad) const noexcept {
    if (!cylinder_id.valid() || !std::isfinite(body_angle_psi_rad)) {
        return std::nullopt;
    }
    const auto found = std::find_if(
        cylinder_bindings_.begin(), cylinder_bindings_.end(),
        [&](const auto &binding) { return binding.cylinder_id == cylinder_id; });
    if (found == cylinder_bindings_.end()) {
        return std::nullopt;
    }
    return sample_admitted_cylinder(
        static_cast<std::size_t>(found - cylinder_bindings_.begin()),
        body_angle_psi_rad);
}

bool LegacyFixedValvetrain::sample_all(
    double body_angle_psi_rad,
    std::span<LegacyCylinderValveSample> output) const noexcept {
    if (!std::isfinite(body_angle_psi_rad) ||
        output.size() != cylinder_bindings_.size()) {
        return false;
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = sample_admitted_cylinder(index, body_angle_psi_rad);
    }
    return true;
}

LegacyFixedValvetrainCompileResult
compile_legacy_fixed_valvetrain(const contract::EngineSpec &engine,
                                const contract::LowOrderEngineCoreV1 &core) {
    ValidationReport report;
    require(report, exact_legacy_method(engine.methods.valvetrain),
            ContractIssueCode::unsupported_value, "engine.methods.valvetrain",
            "fixed valvetrain requires legacy_low_order_v1 version 1");
    require(report, engine.cycle.value == contract::EngineCycle::four_stroke,
            ContractIssueCode::unsupported_value, "engine.cycle.value",
            "legacy half-speed camshaft requires a four-stroke engine");

    const auto &mechanism = core.mechanism;
    const auto &valvetrain = core.valvetrain;
    const auto &gas_path = core.gas_path;
    const auto *output_crank = contract::find_output_crank(mechanism);
    require(report,
            output_crank != nullptr &&
                std::isfinite(output_crank->crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.output_crankshaft.crank_tdc_reference_"
            "rad.value",
            "crank TDC reference must be finite");
    require(report,
            !engine.cylinders.empty() &&
                mechanism.cylinders.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "valvetrain requires nonempty mechanism cylinders matching engine order");

    const auto admit_cam_profiles = [&](const contract::LegacyCamshaftProfile &camshaft,
                                        const std::string &path) {
        std::vector<std::optional<AdmittedCamShape>> admitted;
        require(report, !camshaft.profiles.empty(), ContractIssueCode::missing_value,
                path + ".profiles",
                "camshaft role requires at least one bank-local profile");
        admitted.reserve(camshaft.profiles.size());
        for (std::size_t index = 0; index < camshaft.profiles.size(); ++index) {
            admitted.push_back(
                admit_cam_shape(camshaft.profiles[index], report,
                                path + ".profiles[" + std::to_string(index) + "]"));
        }
        return admitted;
    };
    const auto intake_shapes = admit_cam_profiles(
        valvetrain.intake, "engine.physics_profile.valvetrain.intake");
    const auto exhaust_shapes = admit_cam_profiles(
        valvetrain.exhaust, "engine.physics_profile.valvetrain.exhaust");
    bool exact_ordered_head_coverage =
        !engine.banks.empty() && gas_path.heads.size() == engine.banks.size();
    for (std::size_t index = 0; index < engine.banks.size(); ++index) {
        exact_ordered_head_coverage =
            exact_ordered_head_coverage && engine.banks[index].id.valid() &&
            (index == 0 ||
             engine.banks[index - 1].id.value < engine.banks[index].id.value) &&
            index < gas_path.heads.size() && gas_path.heads[index].bank_id.valid() &&
            gas_path.heads[index].bank_id == engine.banks[index].id;
    }
    require(report, exact_ordered_head_coverage, ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.gas_path.heads",
            "valvetrain requires one flow profile per engine bank in exact BankId "
            "order");
    for (std::size_t index = 0; index < gas_path.heads.size(); ++index) {
        const auto &head = gas_path.heads[index];
        const std::string path =
            "engine.physics_profile.gas_path.heads[" + std::to_string(index) + "]";
        require(report,
                index < engine.banks.size() && head.bank_id.valid() &&
                    head.bank_id == engine.banks[index].id,
                ContractIssueCode::inconsistent_semantics, path + ".bank_id",
                "valvetrain flow profile identity/order must match the engine bank "
                "order");
        require(report, finite_positive(head.intake_flow_triangle_radius_m.value),
                ContractIssueCode::invalid_value,
                path + ".intake_flow_triangle_radius_m.value",
                "intake valve-flow triangle radius must be finite and positive");
        require(report, finite_positive(head.exhaust_flow_triangle_radius_m.value),
                ContractIssueCode::invalid_value,
                path + ".exhaust_flow_triangle_radius_m.value",
                "exhaust valve-flow triangle radius must be finite and positive");
        admit_flow_table(head.intake_flow, report, path + ".intake_flow");
        admit_flow_table(head.exhaust_flow, report, path + ".exhaust_flow");
    }
    const auto admit_cam_lobes = [&](const contract::LegacyCamshaftProfile &camshaft,
                                     contract::PortKind expected_kind,
                                     const std::string &path) {
        require(report, camshaft.lobes.size() == engine.cylinders.size(),
                ContractIssueCode::inconsistent_shape, path + ".lobes",
                "camshaft must have one lobe per engine cylinder");
        const bool unique_cylinders =
            all_unique(camshaft.lobes.size(), [&](std::size_t left, std::size_t right) {
                return camshaft.lobes[left].cylinder_id ==
                       camshaft.lobes[right].cylinder_id;
            });
        const bool unique_ports =
            all_unique(camshaft.lobes.size(), [&](std::size_t left, std::size_t right) {
                return camshaft.lobes[left].port_id == camshaft.lobes[right].port_id;
            });
        require(report, unique_cylinders && unique_ports,
                ContractIssueCode::duplicate_identity, path + ".lobes",
                "camshaft cylinder and port bindings must be unique");
        std::unordered_set<std::uint32_t> used_profile_indices;
        for (std::size_t index = 0; index < camshaft.lobes.size(); ++index) {
            const auto &lobe = camshaft.lobes[index];
            const auto *port = find_port(engine, lobe.port_id);
            const bool profile_valid = lobe.profile_index < camshaft.profiles.size();
            const bool valid =
                lobe.cylinder_id.valid() && lobe.port_id.valid() && profile_valid &&
                std::isfinite(lobe.crank_center_rad.value) && port != nullptr &&
                port->cylinder_id == lobe.cylinder_id &&
                port->kind.value == expected_kind;
            require(report, valid, ContractIssueCode::inconsistent_semantics,
                    path + ".lobes[" + std::to_string(index) + "]",
                    "cam lobe must bind a finite center to its cylinder's matching "
                    "engine port kind");
            if (profile_valid) {
                used_profile_indices.insert(lobe.profile_index);
            }
        }
        require(report, used_profile_indices.size() == camshaft.profiles.size(),
                ContractIssueCode::inconsistent_shape, path + ".profiles",
                "every bank-local cam profile must be used by at least one lobe");
        std::unordered_set<std::uint32_t> encountered_profile_indices;
        std::uint32_t next_profile_index = 0U;
        bool canonical_profile_order = true;
        for (const auto &cylinder : engine.cylinders) {
            const auto *lobe = find_lobe(camshaft, cylinder.id);
            if (lobe == nullptr ||
                !encountered_profile_indices.insert(lobe->profile_index).second) {
                continue;
            }
            canonical_profile_order =
                canonical_profile_order && lobe->profile_index == next_profile_index;
            ++next_profile_index;
        }
        require(report, canonical_profile_order,
                ContractIssueCode::inconsistent_semantics, path + ".profiles",
                "cam profiles must be indexed by first use in engine-cylinder "
                "order");
    };
    admit_cam_lobes(valvetrain.intake, contract::PortKind::intake,
                    "engine.physics_profile.valvetrain.intake");
    admit_cam_lobes(valvetrain.exhaust, contract::PortKind::exhaust,
                    "engine.physics_profile.valvetrain.exhaust");

    std::vector<LegacyValvetrainCylinderBinding> bindings;
    bindings.reserve(mechanism.cylinders.size());
    std::unordered_set<std::uint32_t> mechanism_cylinder_ids;
    for (std::size_t index = 0; index < mechanism.cylinders.size(); ++index) {
        const auto &assembly = mechanism.cylinders[index];
        const auto cylinder_id = assembly.topology.cylinder_id;
        const auto *intake_lobe = find_lobe(valvetrain.intake, cylinder_id);
        const auto *exhaust_lobe = find_lobe(valvetrain.exhaust, cylinder_id);
        const auto flow_profile_index =
            index < engine.cylinders.size()
                ? find_bank_index(engine, engine.cylinders[index].bank_id)
                : std::nullopt;
        const bool order_and_identity_valid =
            index < engine.cylinders.size() && cylinder_id.valid() &&
            cylinder_id == engine.cylinders[index].id &&
            mechanism_cylinder_ids.insert(cylinder_id.value).second;
        require(report, order_and_identity_valid,
                ContractIssueCode::inconsistent_semantics,
                "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                    "].topology.cylinder_id",
                "mechanism cylinder identity/order must be unique and match the "
                "engine cylinder order");
        const bool flow_binding_valid =
            index < engine.cylinders.size() && flow_profile_index.has_value() &&
            find_head(gas_path, engine.cylinders[index].bank_id) != nullptr;
        require(report, flow_binding_valid, ContractIssueCode::inconsistent_semantics,
                "engine.cylinders[" + std::to_string(index) + "].bank_id",
                "engine cylinder bank must bind one compiled valvetrain flow "
                "profile");
        const bool lobe_bindings_valid =
            intake_lobe != nullptr && exhaust_lobe != nullptr &&
            assembly.topology.intake_port_id.valid() &&
            assembly.topology.exhaust_port_id.valid() &&
            intake_lobe->port_id == assembly.topology.intake_port_id &&
            exhaust_lobe->port_id == assembly.topology.exhaust_port_id;
        require(report, lobe_bindings_valid, ContractIssueCode::inconsistent_semantics,
                "engine.physics_profile.mechanism.cylinders[" + std::to_string(index) +
                    "].topology",
                "intake and exhaust lobes must bind the mechanism cylinder's exact "
                "port IDs");
        if (order_and_identity_valid && flow_binding_valid && lobe_bindings_valid) {
            bindings.push_back({
                cylinder_id,
                intake_lobe->port_id,
                exhaust_lobe->port_id,
                *flow_profile_index,
                intake_lobe->profile_index,
                exhaust_lobe->profile_index,
                intake_lobe->crank_center_rad.value / 2.0,
                exhaust_lobe->crank_center_rad.value / 2.0,
            });
        }
    }

    if (!report.ok() || output_crank == nullptr) {
        return report;
    }

    std::vector<LegacyCompiledCamProfile> intake_cam_profiles;
    intake_cam_profiles.reserve(valvetrain.intake.profiles.size());
    for (std::size_t index = 0; index < valvetrain.intake.profiles.size(); ++index) {
        intake_cam_profiles.push_back(compile_cam_profile(
            valvetrain.intake.profiles[index], *intake_shapes[index]));
    }
    std::vector<LegacyCompiledCamProfile> exhaust_cam_profiles;
    exhaust_cam_profiles.reserve(valvetrain.exhaust.profiles.size());
    for (std::size_t index = 0; index < valvetrain.exhaust.profiles.size(); ++index) {
        exhaust_cam_profiles.push_back(compile_cam_profile(
            valvetrain.exhaust.profiles[index], *exhaust_shapes[index]));
    }

    std::vector<LegacyValvetrainFlowProfile> flow_profiles;
    flow_profiles.reserve(engine.banks.size());
    for (const auto &bank : engine.banks) {
        const auto *head = find_head(gas_path, bank.id);
        flow_profiles.push_back({
            bank.id,
            compile_flow_table(head->intake_flow),
            compile_flow_table(head->exhaust_flow),
            head->intake_flow_triangle_radius_m.value,
            head->exhaust_flow_triangle_radius_m.value,
        });
    }

    return LegacyFixedValvetrain{
        output_crank->crank_tdc_reference_rad.value,
        std::move(bindings),
        std::move(intake_cam_profiles),
        std::move(exhaust_cam_profiles),
        std::move(flow_profiles),
    };
}

LegacySelectableValvetrain::LegacySelectableValvetrain(
    LegacyFixedValvetrain base, std::optional<LegacyFixedValvetrain> alternate,
    std::optional<LegacyVtecSelectorThresholds> thresholds)
    : base_(std::move(base)), alternate_(std::move(alternate)),
      thresholds_(std::move(thresholds)) {}

bool LegacySelectableValvetrain::alternate_profile_active(
    const LegacyVtecSelectorInput &input) const noexcept {
    return alternate_.has_value() && thresholds_.has_value() &&
           legacy_vtec_alternate_profile_active(*thresholds_, input);
}

const LegacyFixedValvetrain &LegacySelectableValvetrain::profile_for(
    const LegacyVtecSelectorInput &input) const noexcept {
    return alternate_profile_active(input) ? *alternate_ : base_;
}

LegacySelectableValvetrainCompileResult
compile_legacy_selectable_valvetrain(const contract::EngineSpec &engine,
                                     const contract::LowOrderEngineCoreV1 &core) {
    auto base_result = compile_legacy_fixed_valvetrain(engine, core);
    if (const auto *report = std::get_if<contract::ValidationReport>(&base_result)) {
        return *report;
    }
    auto base = std::get<LegacyFixedValvetrain>(std::move(base_result));

    if (!core.valvetrain.alternate.has_value()) {
        return LegacySelectableValvetrain{
            std::move(base),
            std::nullopt,
            std::nullopt,
        };
    }

    const auto &source = *core.valvetrain.alternate;
    const double minimum_engine_speed_rad_s =
        source.activation.minimum_engine_speed_rad_s.value;
    const double minimum_manifold_pressure_pa_abs =
        source.activation.minimum_mean_manifold_pressure_pa_abs.value;
    const double minimum_throttle_linkage_opening_01 =
        source.activation.minimum_throttle_linkage_opening_01.value;
    const bool activation_valid = std::isfinite(minimum_engine_speed_rad_s) &&
                                  minimum_engine_speed_rad_s >= 0.0 &&
                                  finite_positive(minimum_manifold_pressure_pa_abs) &&
                                  std::isfinite(minimum_throttle_linkage_opening_01) &&
                                  minimum_throttle_linkage_opening_01 >= 0.0 &&
                                  minimum_throttle_linkage_opening_01 <= 1.0;
    if (!activation_valid) {
        contract::ValidationReport report;
        report.add(
            contract::ContractIssueCode::invalid_value,
            "engine.physics_profile.valvetrain.alternate.activation",
            "VTEC activation requires finite nonnegative engine speed, finite "
            "positive absolute manifold pressure, and finite unit-interval throttle "
            "linkage opening");
        return report;
    }

    auto alternate_core = core;
    alternate_core.valvetrain.intake = source.intake;
    alternate_core.valvetrain.exhaust = source.exhaust;
    alternate_core.valvetrain.alternate.reset();
    auto alternate_result = compile_legacy_fixed_valvetrain(engine, alternate_core);
    if (auto *report = std::get_if<contract::ValidationReport>(&alternate_result)) {
        constexpr std::string_view prefix = "engine.physics_profile.valvetrain.";
        for (auto &issue : report->issues) {
            if (issue.path.starts_with(prefix)) {
                issue.path.insert(prefix.size(), "alternate.");
            }
        }
        return *report;
    }
    auto alternate = std::get<LegacyFixedValvetrain>(std::move(alternate_result));

    return LegacySelectableValvetrain{
        std::move(base),
        std::move(alternate),
        LegacyVtecSelectorThresholds{
            minimum_engine_speed_rad_s,
            minimum_manifold_pressure_pa_abs,
            minimum_throttle_linkage_opening_01,
        },
    };
}

} // namespace engine_sim_offline::simulation
