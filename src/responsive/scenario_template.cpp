#include "crankwave/responsive/scenario_template.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crankwave::responsive {
namespace {

constexpr double kLegacyPi = 3.14159265359;
constexpr std::uint64_t kPublicSeed = UINT64_C(12648430);

[[nodiscard]] authoring::Quantity quantity(const double value,
                                           std::string unit) {
    return authoring::Quantity{value, std::move(unit), std::nullopt};
}

void append_u64(std::vector<std::byte> &bytes, const std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((value >> shift) & UINT64_C(0xff)));
    }
}

void append_string(std::vector<std::byte> &bytes, const std::string_view value) {
    append_u64(bytes, value.size());
    for (const auto character : value) {
        bytes.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

void append_f64(std::vector<std::byte> &bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value));
}

[[nodiscard]] contract::Sha256Digest template_identity(
    const authoring::ScenarioDocument &scenario,
    const ResponsiveBakeProfile &profile) {
    std::vector<std::byte> preimage;
    append_string(preimage, kResponsiveScenarioTemplateIdentityMethodId);
    append_string(preimage, scenario.id.value);
    append_string(preimage, scenario.engine.value);
    append_string(preimage, scenario.fuel.value);
    append_f64(preimage, scenario.initial_state.crank_angle.value);
    append_f64(preimage, profile.rpm.anchors.front());
    append_u64(preimage, profile.capture.physics_rate_hz);
    append_u64(preimage, kPublicSeed);
    append_string(preimage, "master-engine-raw");
    append_string(preimage, "master-engine-audition");
    return contract::sha256(preimage);
}

[[nodiscard]] contract::ValidationReport error(std::string path,
                                               std::string message) {
    contract::ValidationReport result;
    result.add(contract::ContractIssueCode::invalid_value, std::move(path),
               std::move(message));
    return result;
}

} // namespace

ResponsiveScenarioTemplateResult make_responsive_scenario_template(
    const authoring::EnginePackageDocument &engine,
    const ResponsiveBakeProfile &profile) {
    const auto profile_report = validate_responsive_bake_profile(profile);
    if (!profile_report.ok()) {
        return profile_report;
    }
    const auto &definition = engine.engine;
    if (definition.identity.id.value.empty()) {
        return error("engine.identity.id", "engine identity is empty");
    }
    if (definition.default_fuel.value.empty() ||
        std::none_of(definition.fuels.begin(), definition.fuels.end(),
                     [&](const auto &fuel) {
                         return fuel.id.value == definition.default_fuel.value;
                     })) {
        return error("engine.default_fuel",
                     "engine default fuel is absent from the engine document");
    }
    const auto crank = std::find_if(
        definition.crankshafts.begin(), definition.crankshafts.end(),
        [&](const auto &candidate) {
            return candidate.id.value == definition.output_crankshaft.value;
        });
    if (crank == definition.crankshafts.end()) {
        return error("engine.output_crankshaft",
                     "engine output crankshaft is absent");
    }
    double crank_angle_rad = 0.0;
    if (crank->tdc_reference_angle.unit == "rad") {
        crank_angle_rad = crank->tdc_reference_angle.value;
    } else if (crank->tdc_reference_angle.unit == "deg") {
        crank_angle_rad =
            crank->tdc_reference_angle.value * (kLegacyPi / 180.0);
    } else {
        return error("engine.output_crankshaft.tdc_reference_angle",
                     "output crank TDC reference must use rad or deg");
    }
    if (!std::isfinite(crank_angle_rad)) {
        return error("engine.output_crankshaft.tdc_reference_angle",
                     "output crank TDC reference is not finite");
    }

    authoring::ScenarioDocument scenario;
    scenario.id = {definition.identity.id.value + "-" + profile.id + "-template"};
    scenario.engine = {definition.identity.id.value};
    scenario.fuel = {definition.default_fuel.value};
    scenario.ambient = {
        quantity(101325.0, "Pa"), quantity(298.15, "K"), 0.0};
    scenario.initial_thermal_state = {
        quantity(298.15, "K"), quantity(363.15, "K"),
        quantity(363.15, "K"), quantity(363.15, "K")};
    scenario.crankcase = {
        quantity(101325.0, "Pa"), quantity(298.15, "K")};
    scenario.initial_state = {
        quantity(profile.rpm.anchors.front(), "rpm"),
        quantity(crank_angle_rad, "rad"),
        true,
        true,
        false,
        false,
        true,
    };
    scenario.preparation = authoring::FixedHorizonPreparation{
        quantity(1.0, "s"), 4U};
    scenario.mode = authoring::FreeEngineMode{
        std::nullopt,
        authoring::ScalarTrajectory{
            authoring::TrajectoryInterpolation::right_continuous_hold,
            {{quantity(0.0, "s"), 0.1}}},
        std::nullopt,
    };
    scenario.events.clear();
    const auto physics_rate = static_cast<std::uint64_t>(
        profile.capture.physics_rate_hz);
    scenario.rates = {
        {physics_rate, 1U, "Hz"},
        {physics_rate, 1U, "Hz"},
        {192000U, 1U, "Hz"},
        {192000U, 1U, "Hz"},
        {192000U, 1U, "Hz"},
    };
    scenario.quality = {"listening", 3840U, 3800U, 1U};
    scenario.total_duration = quantity(7.0, "s");
    scenario.audible_start = quantity(1.0, "s");
    scenario.audible_duration = quantity(6.0, "s");
    scenario.public_seed = kPublicSeed;
    scenario.output = {
        {authoring::AudioBusRef{"master-engine-raw"},
         authoring::AudioBusRef{"master-engine-audition"}},
        {},
    };

    return ResponsiveScenarioTemplate{
        scenario, template_identity(scenario, profile)};
}

} // namespace crankwave::responsive
