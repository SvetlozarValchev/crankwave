#include "simulation/legacy_fixed_valvetrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

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

[[nodiscard]] std::optional<AdmittedCamShape>
admit_cam_shape(const contract::LegacyCamShape &shape, ValidationReport &report,
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

[[nodiscard]] std::vector<LegacyTrianglePoint>
construct_lobe_table(const contract::LegacyCamShape &shape, double radius_rad) {
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
    std::vector<LegacyTrianglePoint> intake_lobe_table,
    std::vector<LegacyTrianglePoint> exhaust_lobe_table,
    std::vector<LegacyTrianglePoint> intake_flow_table,
    std::vector<LegacyTrianglePoint> exhaust_flow_table,
    double intake_lobe_triangle_radius_rad, double exhaust_lobe_triangle_radius_rad,
    double flow_triangle_radius_m, double intake_advance_rad,
    double exhaust_advance_rad, double intake_base_radius_m,
    double exhaust_base_radius_m)
    : crank_tdc_reference_rad_(crank_tdc_reference_rad),
      cylinder_bindings_(std::move(cylinder_bindings)),
      intake_lobe_table_(std::move(intake_lobe_table)),
      exhaust_lobe_table_(std::move(exhaust_lobe_table)),
      intake_flow_table_(std::move(intake_flow_table)),
      exhaust_flow_table_(std::move(exhaust_flow_table)),
      intake_lobe_triangle_radius_rad_(intake_lobe_triangle_radius_rad),
      exhaust_lobe_triangle_radius_rad_(exhaust_lobe_triangle_radius_rad),
      flow_triangle_radius_m_(flow_triangle_radius_m),
      intake_advance_rad_(intake_advance_rad),
      exhaust_advance_rad_(exhaust_advance_rad),
      intake_base_radius_m_(intake_base_radius_m),
      exhaust_base_radius_m_(exhaust_base_radius_m) {}

std::span<const LegacyValvetrainCylinderBinding>
LegacyFixedValvetrain::cylinder_bindings() const noexcept {
    return cylinder_bindings_;
}

std::span<const LegacyTrianglePoint>
LegacyFixedValvetrain::intake_lobe_table() const noexcept {
    return intake_lobe_table_;
}

std::span<const LegacyTrianglePoint>
LegacyFixedValvetrain::exhaust_lobe_table() const noexcept {
    return exhaust_lobe_table_;
}

std::span<const LegacyTrianglePoint>
LegacyFixedValvetrain::intake_flow_table() const noexcept {
    return intake_flow_table_;
}

std::span<const LegacyTrianglePoint>
LegacyFixedValvetrain::exhaust_flow_table() const noexcept {
    return exhaust_flow_table_;
}

double LegacyFixedValvetrain::intake_lobe_triangle_radius_rad() const noexcept {
    return intake_lobe_triangle_radius_rad_;
}

double LegacyFixedValvetrain::exhaust_lobe_triangle_radius_rad() const noexcept {
    return exhaust_lobe_triangle_radius_rad_;
}

double LegacyFixedValvetrain::flow_triangle_radius_m() const noexcept {
    return flow_triangle_radius_m_;
}

double LegacyFixedValvetrain::intake_advance_rad() const noexcept {
    return intake_advance_rad_;
}

double LegacyFixedValvetrain::exhaust_advance_rad() const noexcept {
    return exhaust_advance_rad_;
}

double LegacyFixedValvetrain::intake_base_radius_m() const noexcept {
    return intake_base_radius_m_;
}

double LegacyFixedValvetrain::exhaust_base_radius_m() const noexcept {
    return exhaust_base_radius_m_;
}

LegacyCylinderValveSample LegacyFixedValvetrain::sample_admitted_cylinder(
    std::size_t cylinder_index, double body_angle_psi_rad) const noexcept {
    const auto &binding = cylinder_bindings_[cylinder_index];
    const double intake_base =
        cam_base(body_angle_psi_rad, crank_tdc_reference_rad_, intake_advance_rad_);
    const double exhaust_base =
        cam_base(body_angle_psi_rad, crank_tdc_reference_rad_, exhaust_advance_rad_);
    const double intake_argument =
        wrap_to_minus_pi_inclusive(intake_base + binding.intake_stored_lobe_angle_rad);
    const double exhaust_argument = wrap_to_minus_pi_inclusive(
        exhaust_base + binding.exhaust_stored_lobe_angle_rad);
    const double intake_lift = legacy_triangle_sample(
        intake_lobe_table_, intake_argument, intake_lobe_triangle_radius_rad_);
    const double exhaust_lift = legacy_triangle_sample(
        exhaust_lobe_table_, exhaust_argument, exhaust_lobe_triangle_radius_rad_);

    return {
        binding.cylinder_id,
        binding.intake_port_id,
        binding.exhaust_port_id,
        intake_argument,
        exhaust_argument,
        intake_lift,
        exhaust_lift,
        legacy_triangle_sample(intake_flow_table_, intake_lift,
                               flow_triangle_radius_m_),
        legacy_triangle_sample(exhaust_flow_table_, exhaust_lift,
                               flow_triangle_radius_m_),
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
    const auto &head = core.gas_path.head;
    require(report, std::isfinite(mechanism.crank.crank_tdc_reference_rad.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.mechanism.crank.crank_tdc_reference_rad.value",
            "crank TDC reference must be finite");
    require(report,
            !engine.cylinders.empty() &&
                mechanism.cylinders.size() == engine.cylinders.size(),
            ContractIssueCode::inconsistent_shape,
            "engine.physics_profile.mechanism.cylinders",
            "valvetrain requires nonempty mechanism cylinders matching engine order");

    const auto intake_shape = admit_cam_shape(
        valvetrain.intake.shape, report, "engine.physics_profile.valvetrain.intake");
    const auto exhaust_shape = admit_cam_shape(
        valvetrain.exhaust.shape, report, "engine.physics_profile.valvetrain.exhaust");
    require(report, finite_positive(head.flow_table_triangle_radius_m.value),
            ContractIssueCode::invalid_value,
            "engine.physics_profile.gas_path.head.flow_table_triangle_radius_m.value",
            "valve-flow triangle radius must be finite and positive");
    admit_flow_table(head.intake_flow, report,
                     "engine.physics_profile.gas_path.head.intake_flow");
    admit_flow_table(head.exhaust_flow, report,
                     "engine.physics_profile.gas_path.head.exhaust_flow");

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
        for (std::size_t index = 0; index < camshaft.lobes.size(); ++index) {
            const auto &lobe = camshaft.lobes[index];
            const auto *port = find_port(engine, lobe.port_id);
            const bool valid = lobe.cylinder_id.valid() && lobe.port_id.valid() &&
                               std::isfinite(lobe.crank_center_rad.value) &&
                               port != nullptr &&
                               port->cylinder_id == lobe.cylinder_id &&
                               port->kind.value == expected_kind;
            require(report, valid, ContractIssueCode::inconsistent_semantics,
                    path + ".lobes[" + std::to_string(index) + "]",
                    "cam lobe must bind a finite center to its cylinder's matching "
                    "engine port kind");
        }
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
        if (order_and_identity_valid && lobe_bindings_valid) {
            bindings.push_back({
                cylinder_id,
                intake_lobe->port_id,
                exhaust_lobe->port_id,
                intake_lobe->crank_center_rad.value / 2.0,
                exhaust_lobe->crank_center_rad.value / 2.0,
            });
        }
    }

    if (!report.ok() || !intake_shape.has_value() || !exhaust_shape.has_value()) {
        return report;
    }

    return LegacyFixedValvetrain{
        mechanism.crank.crank_tdc_reference_rad.value,
        std::move(bindings),
        construct_lobe_table(valvetrain.intake.shape, intake_shape->radius_rad),
        construct_lobe_table(valvetrain.exhaust.shape, exhaust_shape->radius_rad),
        compile_flow_table(head.intake_flow),
        compile_flow_table(head.exhaust_flow),
        intake_shape->radius_rad,
        exhaust_shape->radius_rad,
        head.flow_table_triangle_radius_m.value,
        valvetrain.intake.shape.advance_rad.value,
        valvetrain.exhaust.shape.advance_rad.value,
        valvetrain.intake.shape.base_radius_m.value,
        valvetrain.exhaust.shape.base_radius_m.value,
    };
}

} // namespace engine_sim_offline::simulation
