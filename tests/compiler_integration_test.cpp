#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/session.hpp"

#include "authoring/parse_engine_references.hpp"
#include "compile/engine_resolver.hpp"
#include "simulation/legacy_fixed_valvetrain.hpp"
#include "simulation/mechanism_kinematics_plan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace authoring = engine_sim_offline::authoring;
namespace compile = engine_sim_offline::compile;
namespace contract = engine_sim_offline::contract;
namespace compile_detail = engine_sim_offline::compile::detail;
namespace simulation = engine_sim_offline::simulation;

constexpr std::size_t kCylinderCount = 6U;
constexpr std::uint32_t kIrSampleRateHz = 44100U;

void expect(const bool condition, const std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

template <class Value>
[[nodiscard]] Value require_value(compile::CompileResult<Value> result,
                                  const std::string_view context) {
    if (const auto *report = std::get_if<authoring::DiagnosticReport>(&result)) {
        std::string message{context};
        if (!report->diagnostics.empty()) {
            message += " at " + report->diagnostics.front().json_pointer + ": " +
                       report->diagnostics.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<Value>(std::move(result));
}

template <class Result>
void require_diagnostic(const Result &result,
                        const authoring::DiagnosticCode expected_code,
                        const std::string_view expected_path_prefix,
                        const std::string_view context) {
    const auto *report = std::get_if<authoring::DiagnosticReport>(&result);
    if (report == nullptr || report->diagnostics.empty()) {
        throw std::runtime_error{std::string{context} +
                                 ": expected compiler diagnostic was absent"};
    }
    const auto &diagnostic = report->diagnostics.front();
    expect(diagnostic.code == expected_code,
           std::string{context} + ": diagnostic code changed");
    if (!expected_path_prefix.empty()) {
        expect(diagnostic.json_pointer.starts_with(expected_path_prefix),
               std::string{context} + ": diagnostic lost its authored path");
    }
}

[[nodiscard]] authoring::Quantity
quantity(const double value, std::string unit,
         std::optional<std::string> standard = std::nullopt) {
    return {value, std::move(unit), std::move(standard)};
}

void append_fourcc(std::vector<std::byte> &bytes, const std::string_view fourcc) {
    expect(fourcc.size() == 4U, "test WAVE FOURCC is malformed");
    for (const char byte : fourcc) {
        bytes.push_back(std::byte{static_cast<unsigned char>(byte)});
    }
}

void append_u16le(std::vector<std::byte> &bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xffU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xffU)});
}

void append_u32le(std::vector<std::byte> &bytes, const std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(
            std::byte{static_cast<unsigned char>((value >> shift) & 0xffU)});
    }
}

[[nodiscard]] std::vector<std::byte>
make_pcm16_mono_wave(const std::span<const std::int16_t> samples) {
    const auto data_size =
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<std::byte> bytes;
    bytes.reserve(44U + data_size);
    append_fourcc(bytes, "RIFF");
    append_u32le(bytes, 36U + data_size);
    append_fourcc(bytes, "WAVE");
    append_fourcc(bytes, "fmt ");
    append_u32le(bytes, 16U);
    append_u16le(bytes, 1U);
    append_u16le(bytes, 1U);
    append_u32le(bytes, kIrSampleRateHz);
    append_u32le(bytes, kIrSampleRateHz * 2U);
    append_u16le(bytes, 2U);
    append_u16le(bytes, 16U);
    append_fourcc(bytes, "data");
    append_u32le(bytes, data_size);
    for (const std::int16_t sample : samples) {
        append_u16le(bytes, static_cast<std::uint16_t>(sample));
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> bytes_from_text(const std::string_view text) {
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    return {bytes.begin(), bytes.end()};
}

[[nodiscard]] std::string digest_hex(const std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    const auto digest = contract::sha256(bytes);
    std::string result;
    result.reserve(digest.bytes.size() * 2U);
    for (const std::uint8_t byte : digest.bytes) {
        result.push_back(digits[byte >> 4U]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

struct SyntheticAssets {
    std::vector<std::byte> ir_front;
    std::vector<std::byte> ir_rear;
    std::vector<std::byte> accessory;

    [[nodiscard]] std::vector<compile::AssetPayloadView>
    views(const bool reverse = false) const {
        std::vector<compile::AssetPayloadView> result{
            {compile::AssetKind::audio, "fixture-ir-front", ir_front},
            {compile::AssetKind::audio, "fixture-ir-rear", ir_rear},
            {compile::AssetKind::accessory_configuration, "fixture-accessory-load",
             accessory},
        };
        if (reverse) {
            std::ranges::reverse(result);
        }
        return result;
    }

    [[nodiscard]] std::vector<compile::AssetPayloadView> twin_views() const {
        return {
            {compile::AssetKind::audio, "fixture-ir-front", ir_front},
            {compile::AssetKind::accessory_configuration, "fixture-accessory-load",
             accessory},
        };
    }
};

[[nodiscard]] SyntheticAssets make_assets() {
    constexpr std::size_t support_frame_count = 6907U;
    std::vector<std::int16_t> front(support_frame_count, 0);
    front[0] = 30000;
    front[11] = -7000;
    front.back() = 900;

    std::vector<std::int16_t> rear(support_frame_count, 0);
    rear[0] = 26000;
    rear[7] = 5000;
    rear.back() = -700;

    return {
        make_pcm16_mono_wave(front),
        make_pcm16_mono_wave(rear),
        bytes_from_text("{\"schema\":\"engine-sim-offline/accessory-configuration\","
                        "\"id\":\"fixture-accessory-load\","
                        "\"loads\":[{\"id\":\"alternator\",\"enabled\":true}]}\n"),
    };
}

[[nodiscard]] authoring::CurveDefinition make_port_flow_curve() {
    authoring::CurveDefinition curve;
    curve.id = {"fixture-port-flow"};
    curve.input_dimension = authoring::QuantityDimension::length;
    curve.output_dimension = authoring::QuantityDimension::volume_flow_rate;
    curve.evaluation = authoring::CurveEvaluation::triangle_weighted_samples;
    curve.triangle_filter_radius = quantity(1.27, "mm");
    curve.below_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.above_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.samples = {
        {quantity(0.0, "mm"), quantity(0.0, "cfm", std::string{"port_28_inh2o"})},
        {quantity(5.0, "mm"), quantity(92.0, "cfm", std::string{"port_28_inh2o"})},
        {quantity(10.0, "mm"), quantity(171.0, "cfm", std::string{"port_28_inh2o"})},
    };
    return curve;
}

[[nodiscard]] authoring::CurveDefinition make_exhaust_port_flow_curve() {
    auto curve = make_port_flow_curve();
    curve.id = {"fixture-exhaust-port-flow"};
    curve.samples = {
        {quantity(0.0, "mm"), quantity(0.0, "cfm", std::string{"port_28_inh2o"})},
        {quantity(5.0, "mm"), quantity(86.0, "cfm", std::string{"port_28_inh2o"})},
        {quantity(10.0, "mm"), quantity(158.0, "cfm", std::string{"port_28_inh2o"})},
    };
    return curve;
}

[[nodiscard]] authoring::CurveDefinition make_ignition_curve() {
    authoring::CurveDefinition curve;
    curve.id = {"fixture-ignition-timing"};
    curve.input_dimension = authoring::QuantityDimension::angular_speed;
    curve.output_dimension = authoring::QuantityDimension::angle;
    curve.evaluation = authoring::CurveEvaluation::triangle_weighted_samples;
    curve.triangle_filter_radius = quantity(800.0, "rpm");
    curve.below_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.above_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.samples = {
        {quantity(900.0, "rpm"), quantity(8.0, "deg")},
        {quantity(3500.0, "rpm"), quantity(27.0, "deg")},
        {quantity(6900.0, "rpm"), quantity(23.0, "deg")},
    };
    return curve;
}

[[nodiscard]] authoring::CurveDefinition make_flame_speed_curve() {
    authoring::CurveDefinition curve;
    curve.id = {"fixture-flame-speed"};
    curve.input_dimension = authoring::QuantityDimension::dimensionless;
    curve.output_dimension = authoring::QuantityDimension::speed;
    curve.evaluation = authoring::CurveEvaluation::triangle_weighted_samples;
    curve.triangle_filter_radius = quantity(0.5, "1");
    curve.below_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.above_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.samples = {
        {quantity(0.0, "1"), quantity(0.45, "m/s")},
        {quantity(1.0, "1"), quantity(1.0, "m/s")},
        {quantity(2.0, "1"), quantity(1.55, "m/s")},
    };
    return curve;
}

[[nodiscard]] authoring::HarmonicCamLobe make_harmonic_lobe(const bool intake) {
    return {
        quantity(intake ? 216.0 : 224.0, "deg"),
        quantity(0.050, "in"),
        quantity(intake ? 9.8 : 9.2, "mm"),
        intake ? 1.15 : 1.1,
        100U,
    };
}

[[nodiscard]] authoring::CurveDefinition
make_sampled_cam_curve(std::string id = "fixture-sampled-intake-cam") {
    authoring::CurveDefinition curve;
    curve.id = {std::move(id)};
    curve.input_dimension = authoring::QuantityDimension::angle;
    curve.output_dimension = authoring::QuantityDimension::length;
    curve.evaluation = authoring::CurveEvaluation::triangle_weighted_samples;
    curve.triangle_filter_radius = quantity(2.5, "deg");
    curve.below_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.above_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.samples = {
        {quantity(-120.0, "deg"), quantity(0.0, "mm")},
        {quantity(-40.0, "deg"), quantity(3.1, "mm")},
        {quantity(0.0, "deg"), quantity(9.8, "mm")},
        {quantity(40.0, "deg"), quantity(3.1, "mm")},
        {quantity(120.0, "deg"), quantity(0.0, "mm")},
    };
    return curve;
}

void use_sampled_intake_cam(authoring::EnginePackageDocument &document,
                            std::string curve_id = "fixture-sampled-intake-cam") {
    for (auto &lobe : document.engine.cam_lobes) {
        if (lobe.port_kind == authoring::PortKind::intake) {
            lobe.shape = authoring::SampledCamLobe{authoring::CurveRef{curve_id}};
        }
    }
}

[[nodiscard]] authoring::CurveDefinition sampled_cam_curve_from_table(
    std::string id, const std::span<const simulation::LegacyTrianglePoint> table,
    const double triangle_radius_rad) {
    authoring::CurveDefinition curve;
    curve.id = {std::move(id)};
    curve.input_dimension = authoring::QuantityDimension::angle;
    curve.output_dimension = authoring::QuantityDimension::length;
    curve.evaluation = authoring::CurveEvaluation::triangle_weighted_samples;
    curve.triangle_filter_radius = quantity(triangle_radius_rad, "rad");
    curve.below_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.above_domain = authoring::CurveBoundaryBehavior::clamp;
    curve.samples.reserve(table.size());
    for (const auto &point : table) {
        curve.samples.push_back({quantity(point.x, "rad"), quantity(point.y, "m")});
    }
    return curve;
}

[[nodiscard]] simulation::LegacyFixedValvetrain
require_fixed_valvetrain(const contract::EngineSpec &engine,
                         const contract::LowOrderOperatingPointV1Profile &profile,
                         const std::string_view context) {
    auto result = simulation::compile_legacy_fixed_valvetrain(engine, profile.core);
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        std::string message{context};
        if (!report->issues.empty()) {
            message += " at " + report->issues.front().path + ": " +
                       report->issues.front().message;
        }
        throw std::runtime_error{message};
    }
    return std::get<simulation::LegacyFixedValvetrain>(std::move(result));
}

[[nodiscard]] std::string indexed_id(const std::string_view prefix,
                                     const std::size_t one_based_index) {
    return std::string{prefix} + std::to_string(one_based_index);
}

[[nodiscard]] double wrap_four_stroke_angle_degrees(const double value) {
    const double wrapped = std::fmod(value, 720.0);
    return wrapped < 0.0 ? wrapped + 720.0 : wrapped;
}

[[nodiscard]] authoring::EnginePackageDocument
make_engine_document(const SyntheticAssets &assets) {
    authoring::EnginePackageDocument package;
    auto &engine = package.engine;
    engine.identity = {
        {"fixture-inline-six"},
        "Synthetic compiler integration inline-six",
        std::nullopt,
    };
    engine.cycle = authoring::EngineCycle::four_stroke;
    engine.layout = authoring::CylinderLayout::inline_engine;
    engine.limits.redline = quantity(6900.0, "rpm");
    engine.curves = {
        make_port_flow_curve(),
        make_exhaust_port_flow_curve(),
        make_ignition_curve(),
        make_flame_speed_curve(),
    };

    authoring::CrankshaftDefinition crankshaft;
    crankshaft.id = {"fixture-crank"};
    crankshaft.throw_radius = quantity(39.0, "mm");
    crankshaft.mass = quantity(13.0, "kg");
    crankshaft.flywheel_mass = quantity(7.5, "kg");
    crankshaft.moment_of_inertia = quantity(0.19, "kg*m2");
    crankshaft.friction_torque = quantity(10.0, "lb*ft");
    crankshaft.tdc_reference_angle = quantity(0.0, "deg");

    constexpr std::array<double, kCylinderCount> journal_phases_deg{
        0.0, 120.0, 240.0, 240.0, 120.0, 0.0,
    };
    constexpr std::array<std::size_t, kCylinderCount> firing_order{
        1U, 5U, 3U, 6U, 2U, 4U,
    };
    std::array<double, kCylinderCount> firing_angles_deg{};

    for (std::size_t index = 0; index < kCylinderCount; ++index) {
        const auto one_based = index + 1U;
        const auto journal_id = indexed_id("fixture-journal-", one_based);
        const auto rod_id = indexed_id("fixture-rod-", one_based);
        const auto piston_id = indexed_id("fixture-piston-", one_based);
        engine.journals.push_back({
            {journal_id},
            authoring::CrankshaftJournalAttachment{{"fixture-crank"}},
            quantity(journal_phases_deg[index], "deg"),
        });
        engine.connecting_rods.push_back({
            {rod_id},
            quantity(148.0, "mm"),
            quantity(570.0, "g"),
            quantity(0.0011, "kg*m2"),
            std::nullopt,
        });
        engine.pistons.push_back({
            {piston_id},
            quantity(420.0, "g"),
            quantity(31.0, "mm"),
            std::nullopt,
            quantity(0.0, "cm3"),
            authoring::FlowRestriction{authoring::FlowBenchRestriction{
                quantity(0.12, "cfm", std::string{"port_28_inh2o"}),
                quantity(28.0, "inH2O"),
            }},
        });
    }
    engine.crankshafts.push_back(std::move(crankshaft));

    engine.banks.push_back({
        {"fixture-bank"},
        quantity(0.0, "deg"),
        quantity(82.0, "mm"),
        quantity(218.0, "mm"),
        {"fixture-head"},
    });

    const authoring::FlowBenchRestriction main_restriction{
        quantity(305.0, "cfm", std::string{"carburetor_1p5_inhg"}),
        quantity(1.5, "inHg"),
    };
    const authoring::FlowBenchRestriction idle_restriction{
        quantity(18.0, "cfm", std::string{"carburetor_1p5_inhg"}),
        quantity(1.5, "inHg"),
    };
    const authoring::FlowBenchRestriction runner_restriction{
        quantity(132.0, "cfm", std::string{"carburetor_1p5_inhg"}),
        quantity(1.5, "inHg"),
    };
    engine.intakes.push_back({
        {"fixture-shared-intake"},
        quantity(3.4, "L"),
        quantity(19.0, "cm2"),
        quantity(325.0, "mm"),
        authoring::FlowRestriction{main_restriction},
        authoring::FlowRestriction{idle_restriction},
        authoring::FlowRestriction{runner_restriction},
        0.994,
        0.12,
    });

    for (const std::string_view exhaust_id :
         {"fixture-exhaust-front", "fixture-exhaust-rear"}) {
        engine.exhausts.push_back({
            {std::string{exhaust_id}},
            quantity(17.5, "cm2"),
            quantity(710.0, "mm"),
            std::nullopt,
            quantity(465.0, "mm"),
            quantity(3.9, "cm2"),
            authoring::FlowRestriction{authoring::FlowBenchRestriction{
                quantity(410.0, "cfm", std::string{"carburetor_1p5_inhg"}),
                quantity(1.5, "inHg"),
            }},
            authoring::FlowRestriction{authoring::FlowBenchRestriction{
                quantity(176.0, "cfm", std::string{"carburetor_1p5_inhg"}),
                quantity(1.5, "inHg"),
            }},
            0.11,
        });
    }

    engine.ports = {
        {
            {"fixture-intake-port"},
            {"fixture-head"},
            authoring::PortKind::intake,
            quantity(92.0, "cm3"),
            quantity(4.4, "cm2"),
            {"fixture-port-flow"},
        },
        {
            {"fixture-exhaust-port"},
            {"fixture-head"},
            authoring::PortKind::exhaust,
            quantity(78.0, "cm3"),
            quantity(3.9, "cm2"),
            {"fixture-exhaust-port-flow"},
        },
    };

    for (std::size_t rank = 0; rank < firing_order.size(); ++rank) {
        firing_angles_deg[firing_order[rank] - 1U] = static_cast<double>(rank) * 120.0;
    }
    authoring::CamshaftDefinition intake_camshaft{
        {"fixture-intake-cam"},
        quantity(0.0, "deg"),
        quantity(15.5, "mm"),
        {},
    };
    authoring::CamshaftDefinition exhaust_camshaft{
        {"fixture-exhaust-cam"},
        quantity(0.0, "deg"),
        quantity(15.5, "mm"),
        {},
    };
    for (std::size_t index = 0; index < kCylinderCount; ++index) {
        const auto one_based = index + 1U;
        const auto cylinder_id = indexed_id("fixture-cylinder-", one_based);
        const auto intake_lobe_id = indexed_id("fixture-intake-lobe-", one_based);
        const auto exhaust_lobe_id = indexed_id("fixture-exhaust-lobe-", one_based);
        engine.cam_lobes.push_back({
            {intake_lobe_id},
            {cylinder_id},
            authoring::PortKind::intake,
            quantity(wrap_four_stroke_angle_degrees(110.0 + firing_angles_deg[index]),
                     "deg"),
            authoring::CamLobeShape{make_harmonic_lobe(true)},
        });
        engine.cam_lobes.push_back({
            {exhaust_lobe_id},
            {cylinder_id},
            authoring::PortKind::exhaust,
            quantity(wrap_four_stroke_angle_degrees(250.0 + firing_angles_deg[index]),
                     "deg"),
            authoring::CamLobeShape{make_harmonic_lobe(false)},
        });
        intake_camshaft.lobes.push_back({intake_lobe_id});
        exhaust_camshaft.lobes.push_back({exhaust_lobe_id});
    }
    engine.camshafts = {
        std::move(intake_camshaft),
        std::move(exhaust_camshaft),
    };
    engine.valvetrains.push_back({
        {"fixture-standard-valvetrain"},
        authoring::ValvetrainKind{authoring::StandardValvetrain{
            {"fixture-intake-cam"},
            {"fixture-exhaust-cam"},
        }},
    });
    engine.heads.push_back({
        {"fixture-head"},
        quantity(44.0, "cm3"),
        {"fixture-standard-valvetrain"},
        {{"fixture-intake-port"}, {"fixture-exhaust-port"}},
    });

    engine.fuels.push_back({
        {"fixture-gasoline"},
        "Synthetic gasoline",
        quantity(105.0, "g/mol"),
        std::nullopt,
        quantity(43.4, "MJ/kg"),
        14.6,
        {"fixture-flame-speed"},
        {
            0.34,
            0.018,
            0.09,
            2.1,
            9.5,
        },
    });
    engine.default_fuel = {"fixture-gasoline"};
    engine.accessory_configurations.push_back({
        {"fixture-accessory-load"},
        "assets/fixture-accessory-load.json",
        digest_hex(assets.accessory),
    });
    engine.losses = authoring::ChenFlynnLossDefinition{
        quantity(18.0, "kPa"),    0.012,
        quantity(4.6, "kPa*s/m"), quantity(0.85, "kPa*s2/m2"),
        quantity(88.0, "degC"),   {"fixture-accessory-load"},
    };

    authoring::IgnitionDefinition ignition;
    ignition.timing_curve = {"fixture-ignition-timing"};
    for (std::size_t index = 0; index < kCylinderCount; ++index) {
        ignition.wires.push_back({{indexed_id("fixture-wire-", index + 1U)}});
    }
    for (std::size_t rank = 0; rank < firing_order.size(); ++rank) {
        ignition.firing_order.push_back({
            {indexed_id("fixture-wire-", firing_order[rank])},
            quantity(static_cast<double>(rank) * 120.0, "deg"),
        });
    }
    ignition.limiter = {
        quantity(6750.0, "rpm"),
        quantity(42.0, "ms"),
    };
    engine.ignition = std::move(ignition);

    engine.throttle_controllers = std::vector<authoring::ThrottleControllerDefinition>{{
        {"fixture-direct-throttle"},
        authoring::ThrottleControllerKind{authoring::DirectThrottleController{2.0}},
    }};
    engine.throttle_controller = {"fixture-direct-throttle"};
    engine.starter = authoring::MechanicallyDisengagedStarter{};

    for (std::size_t index = 0; index < kCylinderCount; ++index) {
        const auto one_based = index + 1U;
        const bool front_route = index < (kCylinderCount / 2U);
        engine.cylinders.push_back({
            {indexed_id("fixture-cylinder-", one_based)},
            {"fixture-bank"},
            {indexed_id("fixture-journal-", one_based)},
            {indexed_id("fixture-rod-", one_based)},
            {indexed_id("fixture-piston-", one_based)},
            {"fixture-shared-intake"},
            {front_route ? "fixture-exhaust-front" : "fixture-exhaust-rear"},
            {indexed_id("fixture-wire-", one_based)},
            {"fixture-intake-port"},
            {"fixture-exhaust-port"},
            quantity(front_route ? 440.0 : 495.0, "mm"),
        });
    }
    engine.source_routes = {
        {
            {"fixture-route-front"},
            authoring::SourceRouteBinding{
                authoring::ExhaustRouteSource{{"fixture-exhaust-front"}}},
        },
        {
            {"fixture-route-rear"},
            authoring::SourceRouteBinding{
                authoring::ExhaustRouteSource{{"fixture-exhaust-rear"}}},
        },
    };

    auto &presentation = package.presentation;
    presentation.assets = {
        {
            {"fixture-ir-front"},
            authoring::AudioAssetKind::impulse_response,
            "assets/fixture-ir-front.wav",
            digest_hex(assets.ir_front),
        },
        {
            {"fixture-ir-rear"},
            authoring::AudioAssetKind::impulse_response,
            "assets/fixture-ir-rear.wav",
            digest_hex(assets.ir_rear),
        },
    };
    for (std::size_t index = 0; index < kCylinderCount; ++index) {
        presentation.cylinder_routes.push_back({
            {indexed_id("fixture-cylinder-", index + 1U)},
            {index < (kCylinderCount / 2U) ? "fixture-route-front"
                                           : "fixture-route-rear"},
            0.91 + static_cast<double>(index) * 0.01,
        });
    }
    presentation.routes = {
        {
            {"fixture-route-front"},
            0.96,
            authoring::AudioAssetRef{"fixture-ir-front"},
            0.0018,
            0.64,
        },
        {
            {"fixture-route-rear"},
            1.04,
            authoring::AudioAssetRef{"fixture-ir-rear"},
            0.0015,
            0.58,
        },
    };
    presentation.conditioning = {
        0.06, quantity(720.0, "Hz"), 0.12, 0.035, quantity(7600.0, "Hz"),
    };
    presentation.buses = {
        {
            {"raw"},
            {{"fixture-route-front"}, {"fixture-route-rear"}},
            1.0,
            true,
        },
        {
            {"audition"},
            {{"fixture-route-front"}, {"fixture-route-rear"}},
            1.0,
            true,
        },
    };
    presentation.audition = {
        {{"audition"}},
        0.9,
        quantity(8.0, "ms"),
        quantity(12.0, "ms"),
    };
    presentation.publication_gain_linear = 0.95;
    package.rig = std::nullopt;
    return package;
}

void use_four_cam_vtec(authoring::EnginePackageDocument &document) {
    auto &engine = document.engine;
    auto alternate_intake = engine.camshafts[0];
    auto alternate_exhaust = engine.camshafts[1];
    alternate_intake.id.value = "fixture-alternate-intake-cam";
    alternate_exhaust.id.value = "fixture-alternate-exhaust-cam";
    alternate_intake.lobes.clear();
    alternate_exhaust.lobes.clear();

    const auto clone_lobes = [&](const authoring::CamshaftDefinition &base,
                                 authoring::CamshaftDefinition &alternate,
                                 const std::string_view role) {
        for (std::size_t index = 0; index < base.lobes.size(); ++index) {
            const auto found =
                std::ranges::find(engine.cam_lobes, base.lobes[index].value,
                                  [](const auto &lobe) { return lobe.id.value; });
            expect(found != engine.cam_lobes.end(),
                   "VTEC fixture base cam lobe did not resolve");
            auto lobe = *found;
            lobe.id.value = "fixture-alternate-" + std::string{role} + "-lobe-" +
                            std::to_string(index + 1U);
            auto &harmonic = std::get<authoring::HarmonicCamLobe>(lobe.shape);
            harmonic.duration_at_reference_lift =
                quantity(role == "intake" ? 240.0 : 232.0, "deg");
            harmonic.maximum_lift = quantity(role == "intake" ? 11.5 : 10.5, "mm");
            alternate.lobes.push_back({lobe.id.value});
            engine.cam_lobes.push_back(std::move(lobe));
        }
    };

    clone_lobes(engine.camshafts[0], alternate_intake, "intake");
    clone_lobes(engine.camshafts[1], alternate_exhaust, "exhaust");
    engine.camshafts.push_back(std::move(alternate_intake));
    engine.camshafts.push_back(std::move(alternate_exhaust));
    engine.valvetrains.front().kind = authoring::VtecValvetrain{
        {"fixture-intake-cam"},
        {"fixture-exhaust-cam"},
        {"fixture-alternate-intake-cam"},
        {"fixture-alternate-exhaust-cam"},
        {
            quantity(5800.0, "rpm"),
            quantity(84393.05666666664, "Pa"),
            0.3,
        },
    };
}

[[nodiscard]] authoring::EnginePackageDocument
make_inline_twin_document(const SyntheticAssets &assets) {
    auto package = make_engine_document(assets);
    auto &engine = package.engine;
    engine.identity.id.value = "fixture-inline-twin";
    engine.identity.display_name = "Synthetic compiler integration inline-twin";

    engine.journals.resize(2U);
    engine.journals[0].phase = quantity(0.0, "deg");
    engine.journals[1].phase = quantity(180.0, "deg");
    engine.connecting_rods.resize(2U);
    engine.pistons.resize(2U);
    engine.cylinders.resize(2U);
    engine.exhausts.resize(1U);
    engine.source_routes.resize(1U);

    engine.cam_lobes.resize(4U);
    engine.cam_lobes[0].centerline = quantity(110.0, "deg");
    engine.cam_lobes[1].centerline = quantity(250.0, "deg");
    engine.cam_lobes[2].centerline = quantity(470.0, "deg");
    engine.cam_lobes[3].centerline = quantity(610.0, "deg");
    engine.camshafts[0].lobes.resize(2U);
    engine.camshafts[1].lobes.resize(2U);

    engine.ignition.wires.resize(2U);
    engine.ignition.firing_order = {
        {{"fixture-wire-1"}, quantity(0.0, "deg")},
        {{"fixture-wire-2"}, quantity(360.0, "deg")},
    };

    auto &presentation = package.presentation;
    presentation.assets.resize(1U);
    presentation.cylinder_routes.resize(2U);
    presentation.routes.resize(1U);
    for (auto &bus : presentation.buses) {
        bus.routes.resize(1U);
    }
    return package;
}

[[nodiscard]] authoring::EnginePackageDocument
make_master_rod_twin_document(const SyntheticAssets &assets) {
    auto package = make_inline_twin_document(assets);
    auto &engine = package.engine;
    engine.identity.id.value = "fixture-master-rod-twin";
    engine.identity.display_name = "Synthetic compiler integration master-rod twin";
    engine.journals[1].attachment = authoring::MasterRodJournalAttachment{
        {"fixture-cylinder-1"},
        quantity(29.0, "mm"),
    };
    engine.journals[1].phase = quantity(72.0, "deg");
    return package;
}

[[nodiscard]] authoring::EnginePackageDocument
make_v_six_document(const SyntheticAssets &assets) {
    auto package = make_engine_document(assets);
    auto &engine = package.engine;
    engine.identity.id.value = "fixture-v-six";
    engine.identity.display_name = "Synthetic compiler integration V-six";
    engine.layout = authoring::CylinderLayout::v_engine;

    auto left_bank = engine.banks.front();
    left_bank.id.value = "fixture-bank-left";
    left_bank.angle = quantity(-45.0, "deg");
    auto right_bank = left_bank;
    right_bank.id.value = "fixture-bank-right";
    right_bank.angle = quantity(45.0, "deg");
    right_bank.bore = quantity(84.0, "mm");
    right_bank.deck_height = quantity(220.0, "mm");
    engine.banks = {std::move(left_bank), std::move(right_bank)};

    constexpr std::array<std::size_t, kCylinderCount> shared_journal_indices{
        1U, 1U, 3U, 3U, 5U, 5U,
    };
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        engine.cylinders[index].bank.value =
            index % 2U == 0U ? "fixture-bank-left" : "fixture-bank-right";
        engine.cylinders[index].journal.value =
            indexed_id("fixture-journal-", shared_journal_indices[index]);
    }
    engine.journals = {
        engine.journals[0],
        engine.journals[2],
        engine.journals[4],
    };
    std::get<authoring::DirectThrottleController>(
        engine.throttle_controllers->front().kind)
        .gamma = 1.65;
    engine.intakes.front().idle_throttle_position_01 = 0.99715;
    std::get<authoring::FlowBenchRestriction>(
        engine.intakes.front().idle_bypass_restriction)
        .rated_flow.value = 0.0;
    return package;
}

[[nodiscard]] authoring::EnginePackageDocument
make_custom_six_document(const SyntheticAssets &assets) {
    auto package = make_engine_document(assets);
    auto &engine = package.engine;
    engine.identity.id.value = "fixture-custom-six";
    engine.identity.display_name = "Synthetic compiler integration custom-six";
    engine.layout = authoring::CylinderLayout::custom;

    auto bank_zero = engine.banks.front();
    bank_zero.id.value = "fixture-bank-zero";
    bank_zero.angle = quantity(0.0, "deg");
    auto bank_120 = bank_zero;
    bank_120.id.value = "fixture-bank-120";
    bank_120.angle = quantity(120.0, "deg");
    auto bank_240 = bank_zero;
    bank_240.id.value = "fixture-bank-240";
    bank_240.angle = quantity(240.0, "deg");
    engine.banks = {
        std::move(bank_zero),
        std::move(bank_120),
        std::move(bank_240),
    };

    constexpr std::array<std::string_view, kCylinderCount> bank_ids{
        "fixture-bank-zero", "fixture-bank-120", "fixture-bank-240",
        "fixture-bank-240",  "fixture-bank-120", "fixture-bank-zero",
    };
    for (std::size_t index = 0; index < engine.cylinders.size(); ++index) {
        engine.cylinders[index].bank.value = bank_ids[index];
    }
    return package;
}

void use_equivalent_split_bank_heads_and_standard_valvetrains(
    authoring::EnginePackageDocument &package) {
    auto &engine = package.engine;
    expect(engine.banks.size() == 2U && engine.heads.size() == 1U &&
               engine.ports.size() == 2U && engine.valvetrains.size() == 1U &&
               engine.camshafts.size() == 2U,
           "split-head fixture requires the canonical shared-head V engine");

    constexpr std::string_view kRightBankId = "fixture-bank-right";
    constexpr std::string_view kRightHeadId = "fixture-head-right";
    constexpr std::string_view kRightValvetrainId = "fixture-standard-valvetrain-right";
    constexpr std::string_view kRightIntakePortId = "fixture-intake-port-right";
    constexpr std::string_view kRightExhaustPortId = "fixture-exhaust-port-right";
    constexpr std::string_view kRightIntakeCamId = "fixture-intake-cam-right";
    constexpr std::string_view kRightExhaustCamId = "fixture-exhaust-cam-right";

    const auto cylinder_uses_right_bank = [&](const std::string_view cylinder_id) {
        const auto cylinder = std::ranges::find(
            engine.cylinders, cylinder_id,
            [](const auto &value) -> std::string_view { return value.id.value; });
        expect(cylinder != engine.cylinders.end(),
               "split-head fixture cam lobe references a missing cylinder");
        return cylinder->bank.value == kRightBankId;
    };
    const auto lobe_uses_right_bank = [&](const authoring::CamLobeRef &reference) {
        const auto lobe = std::ranges::find(
            engine.cam_lobes, reference.value,
            [](const auto &value) -> std::string_view { return value.id.value; });
        expect(lobe != engine.cam_lobes.end(),
               "split-head fixture camshaft references a missing lobe");
        return cylinder_uses_right_bank(lobe->cylinder.value);
    };

    auto right_intake_cam = engine.camshafts[0];
    auto right_exhaust_cam = engine.camshafts[1];
    right_intake_cam.id.value = kRightIntakeCamId;
    right_exhaust_cam.id.value = kRightExhaustCamId;
    std::erase_if(engine.camshafts[0].lobes, lobe_uses_right_bank);
    std::erase_if(engine.camshafts[1].lobes, lobe_uses_right_bank);
    std::erase_if(right_intake_cam.lobes, [&](const auto &reference) {
        return !lobe_uses_right_bank(reference);
    });
    std::erase_if(right_exhaust_cam.lobes, [&](const auto &reference) {
        return !lobe_uses_right_bank(reference);
    });
    expect(!engine.camshafts[0].lobes.empty() && !engine.camshafts[1].lobes.empty() &&
               !right_intake_cam.lobes.empty() && !right_exhaust_cam.lobes.empty(),
           "split-head fixture produced an empty bank-local camshaft");
    engine.camshafts.push_back(std::move(right_intake_cam));
    engine.camshafts.push_back(std::move(right_exhaust_cam));

    auto right_valvetrain = engine.valvetrains.front();
    right_valvetrain.id.value = kRightValvetrainId;
    auto &right_standard =
        std::get<authoring::StandardValvetrain>(right_valvetrain.kind);
    right_standard.intake_camshaft.value = kRightIntakeCamId;
    right_standard.exhaust_camshaft.value = kRightExhaustCamId;
    engine.valvetrains.push_back(std::move(right_valvetrain));

    auto right_intake_port = engine.ports[0];
    auto right_exhaust_port = engine.ports[1];
    right_intake_port.id.value = kRightIntakePortId;
    right_intake_port.head.value = kRightHeadId;
    right_exhaust_port.id.value = kRightExhaustPortId;
    right_exhaust_port.head.value = kRightHeadId;
    engine.ports.push_back(std::move(right_intake_port));
    engine.ports.push_back(std::move(right_exhaust_port));

    auto right_head = engine.heads.front();
    right_head.id.value = kRightHeadId;
    right_head.valvetrain.value = kRightValvetrainId;
    right_head.ports = {
        {std::string{kRightIntakePortId}},
        {std::string{kRightExhaustPortId}},
    };
    engine.heads.push_back(std::move(right_head));
    engine.banks[1].head.value = kRightHeadId;

    for (auto &cylinder : engine.cylinders) {
        if (cylinder.bank.value != kRightBankId) {
            continue;
        }
        cylinder.intake_port.value = kRightIntakePortId;
        cylinder.exhaust_port.value = kRightExhaustPortId;
    }
}

void reorder_harmless_collections(authoring::EnginePackageDocument &package) {
    std::ranges::reverse(package.engine.curves);
    std::ranges::reverse(package.engine.journals);
    std::ranges::reverse(package.engine.connecting_rods);
    std::ranges::reverse(package.engine.pistons);
    std::ranges::reverse(package.engine.exhausts);
    std::ranges::reverse(package.engine.ports);
    std::ranges::reverse(package.engine.cam_lobes);
    std::ranges::reverse(package.engine.camshafts);
    std::ranges::reverse(package.engine.source_routes);
    std::ranges::reverse(package.presentation.assets);
    std::ranges::reverse(package.presentation.cylinder_routes);
    std::ranges::reverse(package.presentation.routes);
    std::ranges::reverse(package.presentation.buses);
}

[[nodiscard]] authoring::ScenarioDocument make_scenario_document() {
    authoring::ScenarioDocument scenario;
    scenario.id = {"fixture-held-listen"};
    scenario.engine = {"fixture-inline-six"};
    scenario.fuel = {"fixture-gasoline"};
    scenario.ambient = {
        quantity(100.8, "kPa"),
        quantity(296.15, "K"),
        0.42,
    };
    scenario.initial_thermal_state = {
        quantity(296.15, "K"),
        quantity(350.15, "K"),
        quantity(361.15, "K"),
        quantity(361.15, "K"),
    };
    scenario.crankcase = {
        quantity(100.8, "kPa"),
        quantity(363.15, "K"),
    };
    scenario.initial_state = {
        quantity(3200.0, "rpm"), quantity(0.0, "deg"), true, true, false, true, false,
    };
    scenario.preparation = authoring::FixedHorizonPreparation{
        quantity(120.0, "ms"),
        2U,
    };
    scenario.mode = authoring::HeldSpeedMode{
        quantity(3200.0, "rpm"),
        {
            authoring::TrajectoryInterpolation::right_continuous_hold,
            {{quantity(0.0, "s"), 0.57}},
        },
    };
    scenario.events = {
        {
            {"fixture-limiter-event"},
            quantity(180.0, "ms"),
            authoring::ScenarioEventPayload{authoring::OperatingStatePatch{
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                false,
            }},
        },
    };
    scenario.rates = {
        {10000U, 1U, "Hz"},  {10000U, 1U, "Hz"},  {192000U, 1U, "Hz"},
        {192000U, 1U, "Hz"}, {192000U, 1U, "Hz"},
    };
    scenario.quality = {
        "fixture-preview",
        3840U,
        3800U,
        96000U,
    };
    scenario.total_duration = quantity(520.0, "ms");
    scenario.audible_start = quantity(120.0, "ms");
    scenario.audible_duration = quantity(400.0, "ms");
    scenario.public_seed = UINT64_C(0x123456789abcdef0);
    scenario.output = {
        {{"raw"}, {"audition"}},
        {},
    };
    return scenario;
}

void expect_session_audio_exact(const compile::CompiledScenario &left_scenario,
                                const compile::CompiledScenario &right_scenario,
                                const std::string_view context) {
    const auto make_session = [&](const compile::CompiledScenario &scenario) {
        auto created = engine_sim_offline::create_engine_session(
            scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&created)) {
            throw std::runtime_error{std::string{context} +
                                     " session creation failed: " + error->detail_code +
                                     ": " + error->message};
        }
        return std::get<engine_sim_offline::EngineSession>(std::move(created));
    };
    auto left = make_session(left_scenario);
    auto right = make_session(right_scenario);

    std::uint64_t compared_audio_samples = 0;
    while (true) {
        auto left_result = left.process_block();
        auto right_result = right.process_block();
        const auto *left_block =
            std::get_if<engine_sim_offline::EngineSessionBlockView>(&left_result);
        const auto *right_block =
            std::get_if<engine_sim_offline::EngineSessionBlockView>(&right_result);
        if (left_block != nullptr || right_block != nullptr) {
            expect(left_block != nullptr && right_block != nullptr &&
                       left_block->block_ordinal() == right_block->block_ordinal() &&
                       left_block->phase() == right_block->phase() &&
                       left_block->first_physics_frame() ==
                           right_block->first_physics_frame() &&
                       left_block->physics_frame_count() ==
                           right_block->physics_frame_count() &&
                       left_block->first_delivery_frame() ==
                           right_block->first_delivery_frame() &&
                       left_block->delivery_frame_count() ==
                           right_block->delivery_frame_count() &&
                       left_block->audio_buses().size() ==
                           right_block->audio_buses().size(),
                   std::string{context} + " sessions diverged in block structure");
            for (std::size_t index = 0; index < left_block->audio_buses().size();
                 ++index) {
                const auto &left_bus = left_block->audio_buses()[index];
                const auto &right_bus = right_block->audio_buses()[index];
                expect(left_bus.descriptor.id == right_bus.descriptor.id &&
                           left_bus.descriptor.kind == right_bus.descriptor.kind &&
                           left_bus.descriptor.route_id ==
                               right_bus.descriptor.route_id &&
                           std::ranges::equal(std::as_bytes(left_bus.samples),
                                              std::as_bytes(right_bus.samples)),
                       std::string{context} + " changed a session audio bus");
                compared_audio_samples += left_bus.samples.size();
            }
            continue;
        }

        if (std::holds_alternative<engine_sim_offline::EngineSessionError>(
                left_result) ||
            std::holds_alternative<engine_sim_offline::EngineSessionError>(
                right_result)) {
            throw std::runtime_error{std::string{context} +
                                     " session failed during execution"};
        }
        const auto *left_completed =
            std::get_if<engine_sim_offline::EngineSessionCompleted>(&left_result);
        const auto *right_completed =
            std::get_if<engine_sim_offline::EngineSessionCompleted>(&right_result);
        expect(left_completed != nullptr && right_completed != nullptr &&
                   left_completed->physics_frame_count ==
                       right_completed->physics_frame_count &&
                   left_completed->delivery_frame_count ==
                       right_completed->delivery_frame_count &&
                   left_completed->block_count == right_completed->block_count &&
                   compared_audio_samples != 0U,
               std::string{context} + " sessions did not complete identically");
        break;
    }
}

void attach_mixed_unit_rig(authoring::EnginePackageDocument &package) {
    authoring::RigDefinition rig;
    rig.id = {"fixture-bench"};
    rig.vehicle = authoring::VehicleDefinition{
        {"fixture-car"},
        quantity(1395000.0, "g"),
        0.29,
        quantity(20500.0, "cm2"),
        2.93,
        quantity(315.0, "mm"),
        quantity(165.0, "N"),
        quantity(14500.0, "N"),
    };
    rig.transmission = authoring::TransmissionDefinition{
        {"fixture-five-speed"},
        quantity(380.0, "N*m"),
        {
            {{"gear-low"}, 4.21},
            {{"gear-high"}, 1.0},
            {{"gear-overdrive"}, 0.73},
        },
    };
    rig.dyno_defaults = authoring::DynoDefaultsDefinition{
        quantity(100.0, "rad/s"),
        quantity(6500.0, "rpm"),
        quantity(25.0, "rad/s"),
    };
    package.rig = std::move(rig);
}

[[nodiscard]] compile::CompiledAssetView
require_asset(const compile::CompiledEngine &engine, const compile::AssetKind kind,
              const std::string_view id) {
    for (std::size_t index = 0; index < engine.asset_count(); ++index) {
        const auto asset = engine.asset(index);
        if (asset && asset->kind == kind && asset->asset_id == id) {
            return *asset;
        }
    }
    throw std::runtime_error{"compiled engine omitted asset '" + std::string{id} + "'"};
}

[[nodiscard]] const contract::EvidenceSource &
require_evidence(const compile::CompiledEngine &engine, const std::string_view id) {
    const auto &evidence = engine.provenance().evidence;
    const auto found = std::ranges::find(evidence, id, &contract::EvidenceSource::id);
    if (found == evidence.end()) {
        throw std::runtime_error{"compiled engine omitted evidence '" +
                                 std::string{id} + "'"};
    }
    return *found;
}

[[nodiscard]] compile::RuntimeObjectId
require_runtime_id(const compile::CompiledEngine &engine,
                   const std::string_view object_namespace,
                   const std::string_view authored_id) {
    const auto assignments = engine.stable_id_assignments();
    const auto found = std::ranges::find_if(assignments, [&](const auto &value) {
        return value.object_namespace == object_namespace &&
               value.authored_id == authored_id;
    });
    if (found == assignments.end()) {
        throw std::runtime_error{"compiled engine omitted stable ID '" +
                                 std::string{authored_id} + "'"};
    }
    return found->runtime_id;
}

void expect_same_assets(const compile::CompiledEngine &first,
                        const compile::CompiledEngine &second) {
    expect(first.asset_count() == second.asset_count(),
           "harmless reordering changed compiled asset count");
    for (std::size_t index = 0; index < first.asset_count(); ++index) {
        const auto left = first.asset(index);
        const auto right = second.asset(index);
        expect(left && right && left->kind == right->kind &&
                   left->asset_id == right->asset_id &&
                   std::ranges::equal(left->bytes, right->bytes),
               "harmless reordering changed compiled asset ownership");
    }
}

void test_complete_generic_compile_and_determinism() {
    SyntheticAssets first_assets = make_assets();
    const auto first_document = make_engine_document(first_assets);
    auto first_views = first_assets.views();
    auto first = require_value(compile::compile_engine(first_document, first_views),
                               "complete generic inline-six engine compile failed");
    auto moved_first = std::move(first);
    expect(first.id() == "fixture-inline-six" && moved_first.id() == first.id(),
           "moving an immutable compiled engine invalidated its source handle");

    const auto retained_front =
        require_asset(first, compile::AssetKind::audio, "fixture-ir-front");
    const auto retained_accessory = require_asset(
        first, compile::AssetKind::accessory_configuration, "fixture-accessory-load");
    const auto front_digest = contract::sha256(first_assets.ir_front);
    const auto accessory_digest = contract::sha256(first_assets.accessory);
    const auto &front_evidence =
        require_evidence(first, "engine.asset.audio.fixture-ir-front");
    const auto &accessory_evidence =
        require_evidence(first, "engine.asset.accessory.fixture-accessory-load");
    expect(first.asset_count() == 3U &&
               std::ranges::equal(retained_front.bytes, first_assets.ir_front) &&
               std::ranges::equal(retained_accessory.bytes, first_assets.accessory) &&
               front_evidence.locator == "assets/fixture-ir-front.wav" &&
               front_evidence.content_sha256 ==
                   std::optional<contract::Sha256Digest>{front_digest} &&
               accessory_evidence.locator == "assets/fixture-accessory-load.json" &&
               accessory_evidence.content_sha256 ==
                   std::optional<contract::Sha256Digest>{accessory_digest},
           "compiled engine did not retain every exact admitted asset");
    const auto retained_first_byte = retained_front.bytes.front();
    first_assets.ir_front.front() ^= std::byte{0xff};
    first_assets.accessory.front() ^= std::byte{0xff};
    expect(retained_front.bytes.front() == retained_first_byte &&
               retained_accessory.bytes.front() != first_assets.accessory.front(),
           "compiled engine borrowed mutable caller asset storage");

    SyntheticAssets reordered_assets = make_assets();
    auto reordered_document = make_engine_document(reordered_assets);
    reorder_harmless_collections(reordered_document);
    auto reordered_views = reordered_assets.views(true);
    auto reordered =
        require_value(compile::compile_engine(reordered_document, reordered_views),
                      "reordered generic inline-six engine compile failed");

    expect(first.id() == reordered.id() &&
               std::ranges::equal(first.stable_id_assignments(),
                                  reordered.stable_id_assignments()) &&
               first.provenance() == reordered.provenance(),
           "harmless document collection reordering changed compiled identity");
    expect(!first.stable_id_assignments().empty(),
           "complete engine compile emitted no stable runtime IDs");
    expect(require_runtime_id(first, "engine.cylinder", "fixture-cylinder-1") == 1U &&
               require_runtime_id(first, "engine.cylinder", "fixture-cylinder-6") ==
                   6U &&
               require_runtime_id(first, "engine.route", "fixture-route-front") == 1U &&
               require_runtime_id(first, "engine.route", "fixture-route-rear") == 2U,
           "canonical dense runtime IDs changed");
    expect_same_assets(first, reordered);

    const auto scenario_document = make_scenario_document();
    auto scenario =
        require_value(compile::compile_scenario(first, scenario_document),
                      "admitted generic held-speed scenario compile failed");
    auto reordered_scenario =
        require_value(compile::compile_scenario(reordered, scenario_document),
                      "scenario compile against reordered engine failed");
    auto moved_scenario = std::move(scenario);
    expect(scenario.id() == "fixture-held-listen" &&
               moved_scenario.id() == scenario.id(),
           "moving an immutable compiled scenario invalidated its source handle");
    expect(scenario.id() == "fixture-held-listen" &&
               scenario.id() == reordered_scenario.id() &&
               std::ranges::equal(scenario.stable_id_assignments(),
                                  reordered_scenario.stable_id_assignments()) &&
               scenario.provenance() == reordered_scenario.provenance(),
           "generic scenario compilation is not deterministic");
    expect(scenario.session_capacities() ==
               compile::CompiledSessionCapacities{
                   3840U,
                   3800U,
                   96000U,
               },
           "compiled scenario lost its delivery/control/telemetry capacities");

    const auto retained_engine = scenario.engine();
    expect(retained_engine.id() == first.id() &&
               require_asset(retained_engine, compile::AssetKind::audio,
                             "fixture-ir-front")
                       .bytes.data() == retained_front.bytes.data(),
           "compiled scenario did not retain the exact immutable engine");

    const auto retained_rear =
        require_asset(first, compile::AssetKind::audio, "fixture-ir-rear");
    expect(std::ranges::equal(
               require_asset(reordered, compile::AssetKind::audio, "fixture-ir-front")
                   .bytes,
               retained_front.bytes) &&
               std::ranges::equal(require_asset(reordered, compile::AssetKind::audio,
                                                "fixture-ir-rear")
                                      .bytes,
                                  retained_rear.bytes),
           "immutable compiled assets changed under harmless document reordering");
}

void test_inline_twin_one_route_reaches_executable_boundary() {
    const SyntheticAssets assets = make_assets();
    const auto document = make_inline_twin_document(assets);
    auto views = assets.twin_views();
    auto engine = require_value(compile::compile_engine(document, views),
                                "inline-twin one-route engine compile failed");

    auto scenario_document = make_scenario_document();
    scenario_document.id.value = "fixture-inline-twin-held";
    scenario_document.engine.value = "fixture-inline-twin";
    auto scenario = require_value(compile::compile_scenario(engine, scenario_document),
                                  "inline-twin one-route scenario compile failed");
    auto created = engine_sim_offline::create_engine_session(
        scenario, engine_sim_offline::EngineSessionExecutionKind::finite_scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&created)) {
        throw std::runtime_error{"inline-twin one-route session creation failed: " +
                                 error->detail_code + ": " + error->message};
    }
    auto session = std::get<engine_sim_offline::EngineSession>(std::move(created));
    const auto descriptor = session.descriptor();
    expect(descriptor.audio_buses.size() == 5U &&
               descriptor.capacities.control_command_queue_capacity == 3800U,
           "inline-twin did not retain its one-route executable session shape");

    std::uint64_t block_count = 0;
    while (true) {
        auto result = session.process_block();
        if (const auto *block =
                std::get_if<engine_sim_offline::EngineSessionBlockView>(&result)) {
            expect(block->audio_buses().size() == 5U,
                   "inline-twin session block lost a public audio bus");
            ++block_count;
            continue;
        }
        if (const auto *error =
                std::get_if<engine_sim_offline::EngineSessionError>(&result)) {
            throw std::runtime_error{"inline-twin session execution failed: " +
                                     error->detail_code + ": " + error->message};
        }
        const auto &completion =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(block_count == descriptor.total_block_count &&
                   completion.block_count == block_count,
               "inline-twin session did not execute its complete one-route horizon");
        break;
    }
}

void test_v_engine_resolves_bank_geometry_and_axis_relative_journals() {
    const SyntheticAssets assets = make_assets();
    const auto document = make_v_six_document(assets);
    auto views = assets.views();
    auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "V-six shared-head engine resolution failed");

    expect(resolved.engine.cylinder_layout.value ==
                   contract::CylinderLayoutKind::vee_engine &&
               resolved.engine.banks.size() == 2U,
           "V-six did not retain its public layout and two-bank topology");

    const auto find_cylinder = [&](const std::string_view semantic_id) {
        const auto found = std::ranges::find(
            resolved.engine.cylinders, semantic_id,
            [](const contract::CylinderSpec &cylinder) -> std::string_view {
                return cylinder.semantic_id.value;
            });
        if (found == resolved.engine.cylinders.end()) {
            throw std::runtime_error{"resolved V-six omitted cylinder"};
        }
        return &*found;
    };
    const auto find_mechanism_cylinder = [&](const contract::CylinderId id) {
        const auto &physics = std::get<contract::LowOrderOperatingPointV1Profile>(
            resolved.engine.physics_profile);
        const auto found =
            std::ranges::find(physics.core.mechanism.cylinders, id,
                              [](const contract::LegacyCylinderAssembly &cylinder) {
                                  return cylinder.topology.cylinder_id;
                              });
        if (found == physics.core.mechanism.cylinders.end()) {
            throw std::runtime_error{"resolved V-six omitted mechanism cylinder"};
        }
        return &*found;
    };

    const auto *left = find_cylinder("fixture-cylinder-1");
    const auto *right = find_cylinder("fixture-cylinder-2");
    const auto *left_core = find_mechanism_cylinder(left->id);
    const auto *right_core = find_mechanism_cylinder(right->id);
    const auto &left_direct =
        std::get<contract::LegacyDirectJournalKinematics>(left_core->kinematics);
    const auto &right_direct =
        std::get<contract::LegacyDirectJournalKinematics>(right_core->kinematics);
    const auto &physics = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    constexpr double kLegacyDegreesToRadians = 3.14159265359 / 180.0;
    const auto near = [](const double lhs, const double rhs) {
        return std::abs(lhs - rhs) <= 1.0e-12;
    };
    expect(left->bank_id != right->bank_id &&
               near(left->journal_phase_rad.value, 0.0) &&
               near(right->journal_phase_rad.value, 0.0) &&
               near(left->bore_m.value, 0.082) && near(right->bore_m.value, 0.084),
           "V-six lost its raw shared-journal phase or per-bank bore geometry");
    expect(near(left_direct.journal_angle_rad.value,
                45.0 * kLegacyDegreesToRadians) &&
               near(right_direct.journal_angle_rad.value,
                    -45.0 * kLegacyDegreesToRadians) &&
               near(left_core->parameters.deck_height_m.value, 0.218) &&
               near(right_core->parameters.deck_height_m.value, 0.220),
           "V-six core did not resolve raw journal phase minus bank angle");
    expect(
        near(std::get<contract::DirectThrottleControllerV1>(
                 physics.core.throttle_controller)
                 .gamma.value,
             1.65) &&
            near(physics.core.gas_path.intake.idle_throttle_plate_position_01.value,
                 0.99715) &&
            near(physics.core.gas_path.intake.idle_bypass.source_rating.value, 0.0) &&
            near(physics.core.gas_path.intake.idle_bypass.resolved_k.value, 0.0),
        "V-six lost its authored direct throttle or closed idle bypass");

    const auto journal_resolution = std::ranges::find(
        resolved.provenance.resolutions,
        "engine.physics.low-order-operating-point-v1.mechanism.cylinders."
        "fixture-cylinder-1.journal_angle_rad",
        &contract::ResolutionRecord::parameter_path);
    expect(journal_resolution != resolved.provenance.resolutions.end() &&
               journal_resolution->mode == contract::ResolutionMode::derived &&
               journal_resolution->method.has_value() &&
               journal_resolution->method->id ==
                   "cylinder-axis-relative-journal-phase-v1" &&
               journal_resolution->dependency_parameter_paths ==
                   std::vector<std::string>{
                       "engine.banks.fixture-bank-left.angle_rad",
                       "engine.cylinders.fixture-cylinder-1.journal_phase_rad",
                   },
           "V-six effective journal phase lost its explicit derivation");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected admitted V-six");
}

void test_custom_engine_resolves_arbitrary_bank_axes_for_direct_rods() {
    const SyntheticAssets assets = make_assets();
    const auto document = make_custom_six_document(assets);
    auto views = assets.views();
    auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "custom-six direct-rod engine resolution failed");

    expect(resolved.engine.cylinder_layout.value ==
                   contract::CylinderLayoutKind::other &&
               resolved.engine.banks.size() == 3U &&
               std::ranges::all_of(
                   resolved.engine.banks,
                   [](const auto &bank) { return bank.angle_rad.has_value(); }),
           "custom-six did not retain its explicit arbitrary bank axes");

    const auto &physics = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    expect(physics.core.mechanism.cylinders.size() == kCylinderCount &&
               std::ranges::all_of(
                   physics.core.mechanism.cylinders,
                   [](const auto &cylinder) {
                       const auto *direct =
                           std::get_if<contract::LegacyDirectJournalKinematics>(
                               &cylinder.kinematics);
                       return direct != nullptr &&
                              std::abs(direct->journal_angle_rad.value) <= 1.0e-12;
                   }),
           "custom-six did not resolve raw journal phase minus each bank axis");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected admitted custom-six");
}

void test_equivalent_split_bank_heads_normalize_to_exact_execution() {
    const SyntheticAssets assets = make_assets();
    const auto shared_document = make_v_six_document(assets);
    auto split_document = shared_document;
    use_equivalent_split_bank_heads_and_standard_valvetrains(split_document);
    auto views = assets.views();

    const auto shared_resolved =
        require_value(compile_detail::resolve_engine_package(shared_document, views),
                      "shared-head V-six engine resolution failed");
    const auto split_resolved =
        require_value(compile_detail::resolve_engine_package(split_document, views),
                      "equivalent split-head V-six engine resolution failed");
    expect(shared_resolved.engine == split_resolved.engine,
           "equivalent split heads changed the resolved executable engine");

    const auto shared_engine =
        require_value(compile::compile_engine(shared_document, views),
                      "shared-head V-six public compilation failed");
    const auto split_engine =
        require_value(compile::compile_engine(split_document, views),
                      "equivalent split-head V-six public compilation failed");
    auto scenario_document = make_scenario_document();
    scenario_document.id.value = "fixture-v-six-split-head-equivalence";
    scenario_document.engine.value = "fixture-v-six";
    const auto shared_scenario =
        require_value(compile::compile_scenario(shared_engine, scenario_document),
                      "shared-head V-six scenario compilation failed");
    const auto split_scenario =
        require_value(compile::compile_scenario(split_engine, scenario_document),
                      "split-head V-six scenario compilation failed");
    expect_session_audio_exact(shared_scenario, split_scenario,
                               "equivalent split-head V-six");
}

void test_heterogeneous_split_bank_heads_fail_closed() {
    const SyntheticAssets assets = make_assets();
    auto views = assets.views();

    {
        auto document = make_v_six_document(assets);
        use_equivalent_split_bank_heads_and_standard_valvetrains(document);
        document.engine.heads[1].chamber_volume = quantity(45.0, "cm3");
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/heads", "heterogeneous split-head chamber volume");
    }
    {
        auto document = make_v_six_document(assets);
        use_equivalent_split_bank_heads_and_standard_valvetrains(document);
        const auto right_intake_port = std::ranges::find(
            document.engine.ports, "fixture-intake-port-right",
            [](const auto &port) -> std::string_view { return port.id.value; });
        expect(right_intake_port != document.engine.ports.end(),
               "heterogeneous port fixture omitted its right intake port");
        right_intake_port->runner_volume = quantity(93.0, "cm3");

        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/ports",
                           "heterogeneous split-head intake runner volume");
    }
    {
        auto document = make_v_six_document(assets);
        use_equivalent_split_bank_heads_and_standard_valvetrains(document);
        const auto right_cylinder =
            std::ranges::find(document.engine.cylinders, "fixture-bank-right",
                              [](const auto &cylinder) -> std::string_view {
                                  return cylinder.bank.value;
                              });
        expect(right_cylinder != document.engine.cylinders.end(),
               "cross-head port fixture omitted a right-bank cylinder");
        right_cylinder->intake_port.value = "fixture-intake-port";

        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/cylinders",
                           "right-bank cylinder bound to the left-head intake port");
    }
    {
        auto document = make_v_six_document(assets);
        use_equivalent_split_bank_heads_and_standard_valvetrains(document);
        const auto right_intake_cam = std::ranges::find(
            document.engine.camshafts, "fixture-intake-cam-right",
            [](const auto &camshaft) -> std::string_view { return camshaft.id.value; });
        expect(right_intake_cam != document.engine.camshafts.end() &&
                   !right_intake_cam->lobes.empty(),
               "heterogeneous cam fixture omitted its right intake cam");
        const auto right_intake_lobe = std::ranges::find(
            document.engine.cam_lobes, right_intake_cam->lobes.front().value,
            [](const auto &lobe) -> std::string_view { return lobe.id.value; });
        expect(right_intake_lobe != document.engine.cam_lobes.end(),
               "heterogeneous cam fixture omitted its right intake lobe");
        auto &shape = std::get<authoring::HarmonicCamLobe>(right_intake_lobe->shape);
        shape.maximum_lift = quantity(12.0, "mm");

        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/cam_lobes", "heterogeneous split-bank cam shape");
    }
}

void test_asset_admission_is_exact_and_closed() {
    {
        SyntheticAssets assets = make_assets();
        const auto document = make_engine_document(assets);
        assets.ir_rear[47U] ^= std::byte{0x01};
        auto views = assets.views();
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::asset_hash_mismatch,
                           "/presentation/assets/", "changed impulse-response payload");
    }
    {
        const SyntheticAssets assets = make_assets();
        const auto document = make_engine_document(assets);
        auto views = assets.views();
        views.erase(views.begin() + 1);
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::missing_asset,
                           "/presentation/assets/", "missing declared asset");
    }
    {
        const SyntheticAssets assets = make_assets();
        const auto document = make_engine_document(assets);
        auto views = assets.views();
        views.push_back({
            compile::AssetKind::audio,
            "fixture-unowned-audio",
            assets.ir_front,
        });
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::disconnected_object, "",
                           "unowned supplied asset");
    }
}

void test_rig_compiles_to_immutable_si_descriptors() {
    const SyntheticAssets assets = make_assets();
    auto document = make_engine_document(assets);
    attach_mixed_unit_rig(document);
    auto views = assets.views();

    auto resolved_result = compile_detail::resolve_engine_package(document, views);
    auto resolved =
        require_value(std::move(resolved_result), "mixed-unit rig resolution failed");
    expect(resolved.rig.has_value(), "resolved engine package omitted its rig");
    const auto &rig = *resolved.rig;
    const auto &physics = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    expect(physics.core.mechanism.crank.running_friction_torque_magnitude_nm.value ==
               10.0 * (4.44822 * ((1.0 / 100.0) * 2.54 * 12.0)),
           "running crank friction lost the legacy lb-ft operation order");
    expect(rig.semantic_id.value == "fixture-bench" && rig.runtime_id == 1U &&
               rig.vehicle.has_value() && rig.transmission.has_value() &&
               rig.dyno_defaults.has_value(),
           "resolved rig lost an authored component");
    const auto near = [](const double left, const double right) {
        return std::abs(left - right) <= 1.0e-12;
    };
    expect(near(rig.vehicle->mass_kg.value, 1395.0) &&
               near(rig.vehicle->frontal_area_m2.value, 2.05) &&
               near(rig.vehicle->tire_radius_m.value, 0.315) &&
               near(rig.vehicle->rolling_resistance_force_n.value, 165.0) &&
               rig.vehicle->maximum_service_brake_force_n.has_value() &&
               near(rig.vehicle->maximum_service_brake_force_n->value, 14500.0),
           "vehicle rig quantities were not canonicalized to SI");
    expect(rig.transmission->gears.size() == 3U &&
               rig.transmission->gears[0].semantic_id.value == "gear-low" &&
               rig.transmission->gears[0].authored_ordinal.value == 1U &&
               rig.transmission->gears[1].semantic_id.value == "gear-high" &&
               rig.transmission->gears[1].authored_ordinal.value == 2U &&
               rig.transmission->gears[2].semantic_id.value == "gear-overdrive" &&
               rig.transmission->gears[2].ratio.value == 0.73,
           "ordered forward-gear data stopped matching authored order");
    expect(rig.transmission->gears[0].runtime_id == 2U &&
               rig.transmission->gears[1].runtime_id == 1U &&
               rig.transmission->gears[2].runtime_id == 3U,
           "gear runtime IDs stopped using canonical stable-ID order");
    expect(std::ranges::any_of(
               resolved.provenance.resolutions,
               [](const auto &resolution) {
                   return resolution.parameter_path ==
                          "rig.transmission.gears.gear-low.authored_ordinal";
               }),
           "authored gear order is absent from resolved provenance");
    expect(std::ranges::any_of(
               resolved.provenance.resolutions,
               [](const auto &resolution) {
                   return resolution.parameter_path ==
                          "engine.physics.low-order-operating-point-v1."
                          "mechanism.crank.running_friction_torque_magnitude_nm";
               }),
           "running crank friction is absent from resolved provenance");
    expect(rig.dyno_defaults->minimum_engine_speed_rad_s.value == 100.0 &&
               rig.dyno_defaults->maximum_engine_speed_rad_s.value ==
                   6500.0 * (2.0 * std::numbers::pi_v<double> / 60.0) &&
               rig.dyno_defaults->hold_step_rad_s.value == 25.0,
           "dyno defaults were not canonicalized to radians per second");

    auto compiled =
        require_value(compile::compile_engine(document, views),
                      "public compile rejected the admitted mixed-unit rig");
    expect(require_runtime_id(compiled, "rig", "fixture-bench") == 1U &&
               require_runtime_id(compiled, "rig.vehicle", "fixture-car") == 1U &&
               require_runtime_id(compiled, "rig.transmission", "fixture-five-speed") ==
                   1U &&
               require_runtime_id(compiled, "rig.gear", "gear-high") == 1U &&
               require_runtime_id(compiled, "rig.gear", "gear-low") == 2U,
           "compiled engine omitted canonical rig runtime IDs");

    auto inconsistent = document;
    inconsistent.rig->dyno_defaults->minimum_engine_speed = quantity(1000.0, "rpm");
    inconsistent.rig->dyno_defaults->maximum_engine_speed = quantity(100.0, "rad/s");
    const auto inconsistent_result = compile::compile_engine(inconsistent, views);
    require_diagnostic(
        inconsistent_result, authoring::DiagnosticCode::inconsistent_value,
        "/rig/dyno_defaults/maximum_engine_speed", "mixed-unit inverted dyno range");

    auto reverse_gear = document;
    reverse_gear.rig->transmission->gears[2].ratio = -3.7;
    const auto reverse_gear_result = compile::compile_engine(reverse_gear, views);
    require_diagnostic(reverse_gear_result, authoring::DiagnosticCode::out_of_range,
                       "/rig/transmission/gears/2/ratio",
                       "negative reverse gear in forward-only transmission");
}

void test_cranking_starter_resolves_to_si_capability() {
    const SyntheticAssets assets = make_assets();
    auto document = make_engine_document(assets);
    document.engine.starter = authoring::CrankingStarter{
        quantity(110.0, "lb*ft"),
        quantity(240.0, "rpm"),
    };
    auto views = assets.views();

    auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "cranking starter engine resolution failed");
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    constexpr double kExpectedTorqueNm =
        110.0 * (4.44822 * ((1.0 / 100.0) * 2.54 * 12.0));
    constexpr double kExpectedTargetSpeedRadS = 240.0 * 0.104719755;
    expect(profile.starter.type.value == contract::StarterCapabilityType::cranking &&
               profile.starter.maximum_torque_nm.value == kExpectedTorqueNm &&
               profile.starter.target_speed_rad_s.value == kExpectedTargetSpeedRadS &&
               profile.starter.included_terms.value ==
                   contract::torque_term_mask(contract::TorqueTerm::starter),
           "cranking starter capability did not resolve type, maximum torque, "
           "target speed, and torque ownership in SI");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected a cranking-capable engine");
}

void test_sampled_fixed_cam_resolves_si_curve_and_provenance() {
    const SyntheticAssets assets = make_assets();
    auto document = make_engine_document(assets);
    document.engine.curves.push_back(make_sampled_cam_curve());
    use_sampled_intake_cam(document);
    auto views = assets.views();

    auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "valid sampled fixed cam was rejected");
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    const auto *sampled = std::get_if<contract::LegacySampledCamShape>(
        &profile.core.valvetrain.intake.shape);
    const auto *harmonic = std::get_if<contract::LegacyHarmonicCamShape>(
        &profile.core.valvetrain.exhaust.shape);
    expect(sampled != nullptr && harmonic != nullptr,
           "sampled intake changed the independent harmonic exhaust shape");

    const auto near = [](const double left, const double right) {
        return std::abs(left - right) <= 1.0e-12;
    };
    const double degrees_to_radians = std::numbers::pi_v<double> / 180.0;
    expect(sampled->samples.size() == 5U &&
               sampled->samples[0].sample_id.value == "sample-1" &&
               sampled->samples[4].sample_id.value == "sample-5" &&
               near(sampled->triangle_radius_rad.value, 2.5 * degrees_to_radians) &&
               near(sampled->samples[0].angle_rad.value, -120.0 * degrees_to_radians) &&
               near(sampled->samples[2].angle_rad.value, 0.0) &&
               near(sampled->samples[2].lift_m.value, 0.0098) &&
               near(sampled->advance_rad.value, 0.0) &&
               near(sampled->base_radius_m.value, 0.0155),
           "sampled fixed cam lost stable sample IDs or canonical SI values");

    const auto has_resolution = [&](const std::string_view path) {
        return std::ranges::any_of(
            resolved.provenance.resolutions, [&](const auto &resolution) {
                return resolution.parameter_path == path &&
                       resolution.mode == contract::ResolutionMode::authored;
            });
    };
    expect(has_resolution(
               "engine.physics.low-order-operating-point-v1.valvetrain.intake.shape."
               "triangle_radius_rad") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.intake."
                   "shape.samples.sample-1.sample_id") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.intake."
                   "shape.samples.sample-1.angle_rad") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.intake."
                   "shape.samples.sample-1.lift_m"),
           "sampled fixed cam lost authored per-sample provenance");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected a valid sampled fixed cam");
}

void test_harmonic_and_equivalent_sampled_cam_sessions_are_identical() {
    const SyntheticAssets assets = make_assets();
    const auto harmonic_document = make_engine_document(assets);
    auto views = assets.views();
    const auto harmonic_resolved =
        require_value(compile_detail::resolve_engine_package(harmonic_document, views),
                      "harmonic equivalence fixture failed to resolve");
    const auto &harmonic_profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        harmonic_resolved.engine.physics_profile);
    const auto harmonic_valvetrain = require_fixed_valvetrain(
        harmonic_resolved.engine, harmonic_profile,
        "harmonic equivalence fixture failed to compile its valvetrain");

    auto sampled_document = harmonic_document;
    constexpr std::string_view kIntakeCurveId = "sampled-equivalent-intake-lift";
    constexpr std::string_view kExhaustCurveId = "sampled-equivalent-exhaust-lift";
    sampled_document.engine.curves.push_back(sampled_cam_curve_from_table(
        std::string{kIntakeCurveId}, harmonic_valvetrain.intake_lobe_table(),
        harmonic_valvetrain.intake_lobe_triangle_radius_rad()));
    sampled_document.engine.curves.push_back(sampled_cam_curve_from_table(
        std::string{kExhaustCurveId}, harmonic_valvetrain.exhaust_lobe_table(),
        harmonic_valvetrain.exhaust_lobe_triangle_radius_rad()));
    for (auto &lobe : sampled_document.engine.cam_lobes) {
        lobe.shape = authoring::SampledCamLobe{authoring::CurveRef{std::string{
            lobe.port_kind == authoring::PortKind::intake ? kIntakeCurveId
                                                          : kExhaustCurveId}}};
    }

    const auto sampled_resolved =
        require_value(compile_detail::resolve_engine_package(sampled_document, views),
                      "equivalent sampled fixture failed to resolve");
    const auto &sampled_profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        sampled_resolved.engine.physics_profile);
    const auto sampled_valvetrain = require_fixed_valvetrain(
        sampled_resolved.engine, sampled_profile,
        "equivalent sampled fixture failed to compile its valvetrain");
    expect(std::ranges::equal(std::as_bytes(harmonic_valvetrain.intake_lobe_table()),
                              std::as_bytes(sampled_valvetrain.intake_lobe_table())) &&
               std::ranges::equal(
                   std::as_bytes(harmonic_valvetrain.exhaust_lobe_table()),
                   std::as_bytes(sampled_valvetrain.exhaust_lobe_table())) &&
               harmonic_valvetrain.intake_lobe_triangle_radius_rad() ==
                   sampled_valvetrain.intake_lobe_triangle_radius_rad() &&
               harmonic_valvetrain.exhaust_lobe_triangle_radius_rad() ==
                   sampled_valvetrain.exhaust_lobe_triangle_radius_rad(),
           "generated harmonic and equivalent sampled cam tables differ");

    const auto harmonic_engine =
        require_value(compile::compile_engine(harmonic_document, views),
                      "harmonic equivalence engine failed public compilation");
    const auto sampled_engine =
        require_value(compile::compile_engine(sampled_document, views),
                      "sampled equivalence engine failed public compilation");
    const auto scenario_document = make_scenario_document();
    const auto harmonic_scenario =
        require_value(compile::compile_scenario(harmonic_engine, scenario_document),
                      "harmonic equivalence scenario failed compilation");
    const auto sampled_scenario =
        require_value(compile::compile_scenario(sampled_engine, scenario_document),
                      "sampled equivalence scenario failed compilation");
    expect_session_audio_exact(harmonic_scenario, sampled_scenario,
                               "equivalent sampled cam");
}

template <class Mutation>
void expect_sampled_fixed_cam_rejected(const SyntheticAssets &assets, Mutation mutation,
                                       const authoring::DiagnosticCode code,
                                       const std::string_view context) {
    auto document = make_engine_document(assets);
    document.engine.curves.push_back(make_sampled_cam_curve());
    use_sampled_intake_cam(document);
    mutation(document);
    auto views = assets.views();
    const auto result = compile::compile_engine(document, views);
    require_diagnostic(result, code, "/engine/cam_lobes", context);
}

void test_invalid_sampled_fixed_cams_fail_closed() {
    const SyntheticAssets assets = make_assets();

    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) {
            document.engine.curves.back().input_dimension =
                authoring::QuantityDimension::angular_speed;
        },
        authoring::DiagnosticCode::unsupported_capability,
        "sampled cam with a non-angle domain");
    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) {
            document.engine.curves.back().triangle_filter_radius->value =
                std::numeric_limits<double>::infinity();
        },
        authoring::DiagnosticCode::invalid_value,
        "sampled cam with a non-finite triangle radius");
    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) { document.engine.curves.back().samples.resize(1U); },
        authoring::DiagnosticCode::unsupported_capability,
        "sampled cam with fewer than two samples");
    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) {
            document.engine.curves.back().samples[2].input =
                document.engine.curves.back().samples[1].input;
        },
        authoring::DiagnosticCode::invalid_value,
        "sampled cam with non-increasing angles");
    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) {
            document.engine.curves.back().samples[2].output = quantity(-0.1, "mm");
        },
        authoring::DiagnosticCode::invalid_value, "sampled cam with negative lift");
    expect_sampled_fixed_cam_rejected(
        assets,
        [](auto &document) {
            document.engine.curves.push_back(
                make_sampled_cam_curve("fixture-second-sampled-intake-cam"));
            bool retained_first = false;
            for (auto &lobe : document.engine.cam_lobes) {
                if (lobe.port_kind != authoring::PortKind::intake) {
                    continue;
                }
                if (!retained_first) {
                    retained_first = true;
                    continue;
                }
                lobe.shape = authoring::SampledCamLobe{
                    authoring::CurveRef{"fixture-second-sampled-intake-cam"}};
                break;
            }
        },
        authoring::DiagnosticCode::unsupported_capability,
        "one camshaft referencing two separately authored sampled curves");
}

void test_four_cam_vtec_resolves_to_si_and_provenance() {
    const SyntheticAssets assets = make_assets();
    auto document = make_engine_document(assets);
    use_four_cam_vtec(document);
    auto views = assets.views();

    auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "valid four-cam VTEC engine resolution failed");
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    expect(profile.core.valvetrain.alternate.has_value(),
           "resolved VTEC profile omitted its alternate cam pair");
    const auto &alternate = *profile.core.valvetrain.alternate;
    const auto &alternate_intake =
        std::get<contract::LegacyHarmonicCamShape>(alternate.intake.shape);
    const auto near = [](const double left, const double right) {
        return std::abs(left - right) <= 1.0e-12;
    };
    expect(
        near(alternate_intake.maximum_lift_m.value, 0.0115) &&
            near(alternate.activation.minimum_engine_speed_rad_s.value,
                 5800.0 * 0.104719755) &&
            near(alternate.activation.minimum_mean_manifold_pressure_pa_abs.value,
                 84393.05666666664) &&
            near(alternate.activation.minimum_throttle_linkage_opening_01.value, 0.3),
        "VTEC alternate cam or activation thresholds lost canonical SI values");

    const auto has_resolution = [&](const std::string_view path) {
        return std::ranges::any_of(
            resolved.provenance.resolutions, [&](const auto &resolution) {
                return resolution.parameter_path == path &&
                       resolution.mode == contract::ResolutionMode::authored;
            });
    };
    expect(has_resolution(
               "engine.physics.low-order-operating-point-v1.valvetrain.alternate."
               "intake.shape.maximum_lift_m") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.alternate."
                   "activation.minimum_engine_speed_rad_s") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.alternate."
                   "activation.minimum_mean_manifold_pressure_pa_abs") &&
               has_resolution(
                   "engine.physics.low-order-operating-point-v1.valvetrain.alternate."
                   "activation.minimum_throttle_linkage_opening_01"),
           "VTEC alternate cam or activation thresholds lost authored provenance");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected a valid four-cam VTEC engine");
}

void test_governor_resolves_to_executable_controller() {
    const SyntheticAssets assets = make_assets();
    auto document = make_engine_document(assets);
    document.engine.throttle_controllers =
        std::vector<authoring::ThrottleControllerDefinition>{{
            {"fixture-governor"},
            authoring::ThrottleControllerKind{authoring::GovernorThrottleController{
                quantity(1600.0, "rpm"),
                quantity(366.5191425, "rad/s"),
                -5.0,
                5.0,
                0.0006,
                200.0,
                2.0,
            }},
        }};
    document.engine.throttle_controller = {"fixture-governor"};
    auto views = assets.views();
    const auto resolved =
        require_value(compile_detail::resolve_engine_package(document, views),
                      "valid governor engine resolution failed");
    const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
        resolved.engine.physics_profile);
    const auto *governor = std::get_if<contract::GovernorThrottleControllerV1>(
        &profile.core.throttle_controller);
    const auto near = [](double left, double right) {
        return std::abs(left - right) <= 1.0e-12;
    };
    expect(governor != nullptr &&
               near(governor->minimum_engine_speed_rad_s.value, 1600.0 * 0.104719755) &&
               near(governor->maximum_engine_speed_rad_s.value, 366.5191425) &&
               governor->minimum_velocity_per_s.value == -5.0 &&
               governor->maximum_velocity_per_s.value == 5.0 &&
               governor->k_s.value == 0.0006 && governor->k_d_per_s.value == 200.0 &&
               governor->gamma.value == 2.0,
           "governor controller lost its source parameters or SI conversion");

    (void)require_value(compile::compile_engine(document, views),
                        "public compiler rejected a valid governor engine");
}

void test_layout_shape_fails_closed() {
    const SyntheticAssets assets = make_assets();
    auto views = assets.views();
    {
        auto document = make_engine_document(assets);
        document.engine.layout = authoring::CylinderLayout::opposed;
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/banks", "one-bank opposed engine topology");
    }
    {
        auto document = make_v_six_document(assets);
        document.engine.layout = authoring::CylinderLayout::opposed;
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/banks", "non-antipodal opposed bank axes");
    }
}

void test_master_rod_graph_contract_and_execution_gate() {
    const SyntheticAssets assets = make_assets();
    auto views = assets.views();
    const auto require_graph_diagnostic = [](const authoring::DiagnosticReport &report,
                                             const authoring::DiagnosticCode code,
                                             const std::string_view path,
                                             const std::string_view context) {
        const auto found =
            std::ranges::find_if(report.diagnostics, [&](const auto &diagnostic) {
                return diagnostic.code == code && diagnostic.json_pointer == path;
            });
        expect(found != report.diagnostics.end(),
               std::string{context} + ": graph diagnostic changed");
    };

    {
        const auto document = make_master_rod_twin_document(assets);
        const auto twin_views = assets.twin_views();
        auto resolved =
            require_value(compile_detail::resolve_engine_package(document, twin_views),
                          "valid master-rod graph failed to resolve");
        expect(resolved.engine.cylinders.size() == 2U,
               "resolved master-rod engine lost a cylinder");
        const auto &master = resolved.engine.cylinders[0];
        const auto &slave = resolved.engine.cylinders[1];
        expect(!master.master_rod_attachment.has_value() &&
                   slave.master_rod_attachment.has_value(),
               "resolved master-rod attachment kind changed");
        expect(slave.master_rod_attachment->master_cylinder_id == master.id &&
                   slave.master_rod_attachment->throw_radius_m.value == 0.029 &&
                   slave.journal_phase_rad.value ==
                       72.0 * (3.14159265359 / 180.0),
               "resolved master-rod attachment lost its master, throw, or local "
               "phase");

        const auto &profile = std::get<contract::LowOrderOperatingPointV1Profile>(
            resolved.engine.physics_profile);
        expect(profile.core.mechanism.cylinders.size() == 2U,
               "resolved master-rod core lost a cylinder");
        const auto *direct_core =
            std::get_if<contract::LegacyDirectJournalKinematics>(
                &profile.core.mechanism.cylinders[0].kinematics);
        const auto *master_core =
            std::get_if<contract::LegacyMasterRodJournalKinematics>(
                &profile.core.mechanism.cylinders[1].kinematics);
        expect(direct_core != nullptr && master_core != nullptr &&
                   direct_core->stroke_m.value == master.stroke_m.value &&
                   direct_core->crank_radius_m.value == 0.039 &&
                   master_core->master_cylinder_id == master.id &&
                   master_core->throw_radius_m.value ==
                       slave.master_rod_attachment->throw_radius_m.value &&
                   master_core->master_local_phase_rad.value ==
                       slave.journal_phase_rad.value,
               "resolved core lost its exact direct/master attachment alternatives");
        const auto &torque_capability = resolved.engine.torque_capability.value;
        expect(torque_capability.instantaneous_net_shaft.availability ==
                       contract::Availability::unavailable &&
                   torque_capability.instantaneous_net_shaft.completeness ==
                       contract::Completeness::incomplete &&
                   torque_capability.instantaneous_net_shaft.included_terms == 0 &&
                   torque_capability.instantaneous_net_shaft.omitted_terms == 0 &&
                   torque_capability.cycle_mean_net_shaft.availability ==
                       contract::Availability::unavailable &&
                   torque_capability.cycle_mean_net_shaft.completeness ==
                       contract::Completeness::incomplete &&
                   torque_capability.cycle_mean_net_shaft.included_terms == 0 &&
                   torque_capability.cycle_mean_net_shaft.omitted_terms == 0 &&
                   !torque_capability.equivalent_inertia_available,
               "geometry-only master core falsely advertised torque or inertia");
        const auto plan_result = simulation::compile_mechanism_kinematics_plan(
            resolved.engine, profile.core);
        const auto *shared_plan =
            std::get_if<simulation::SharedMechanismKinematicsPlan>(&plan_result);
        expect(shared_plan != nullptr,
               "resolved master-rod graph failed geometry-plan compilation");
        const auto *radial_plan =
            shared_plan == nullptr
                ? nullptr
                : simulation::one_level_master_rod_mechanism_kinematics_plan(
                      *shared_plan);
        expect(radial_plan != nullptr && radial_plan->cylinders.size() == 2U &&
                   simulation::direct_mechanism_kinematics_plan(*shared_plan) ==
                       nullptr &&
                   radial_plan->crank_tdc_reference_rad ==
                       profile.core.mechanism.crank.crank_tdc_reference_rad.value,
               "master-rod graph did not select its separate geometry plan");
        const auto *root_plan =
            radial_plan == nullptr
                ? nullptr
                : std::get_if<simulation::OneLevelMasterRodDirectRootPlan>(
                      &radial_plan->cylinders[0].kinematics);
        const auto *slave_plan =
            radial_plan == nullptr
                ? nullptr
                : std::get_if<simulation::OneLevelMasterRodSlaveAttachmentPlan>(
                      &radial_plan->cylinders[1].kinematics);
        const auto *slave_pin =
            slave_plan == nullptr ? nullptr
                                  : std::get_if<simulation::OneLevelMasterRodSlavePin>(
                                        &slave_plan->cylinder.journal);
        expect(root_plan != nullptr && slave_plan != nullptr && slave_pin != nullptr &&
                   root_plan->cylinder.cylinder_id == master.id &&
                   root_plan->driver.crank_journal_global_phase_rad ==
                       master.journal_phase_rad.value &&
                   slave_plan->cylinder.cylinder_id == slave.id &&
                   slave_plan->master_cylinder_index == 0U &&
                   slave_pin->throw_radius_m ==
                       slave.master_rod_attachment->throw_radius_m.value &&
                   slave_pin->local_phase_rad == slave.journal_phase_rad.value &&
                   radial_plan->cylinders[0].chamber_volume_id ==
                       profile.core.mechanism.cylinders[0].topology.chamber_volume_id &&
                   radial_plan->cylinders[1].exhaust_route_id ==
                       profile.core.mechanism.cylinders[1].topology.exhaust_route_id,
               "compiled master-rod plan lost root/slave identity, global/local "
               "phase, stable master index, or chamber/route binding");
        const auto root_sample = radial_plan == nullptr
                                     ? simulation::OneLevelMasterRodSample{}
                                     : simulation::evaluate_one_level_master_rod_plan(
                                           *radial_plan, 0U, 0.0, 100.0);
        const auto slave_sample = radial_plan == nullptr
                                      ? simulation::OneLevelMasterRodSample{}
                                      : simulation::evaluate_one_level_master_rod_plan(
                                            *radial_plan, 1U, 0.0, 100.0);
        expect(root_sample.valid && slave_sample.valid &&
                   root_sample.chamber_volume_m3 > 0.0 &&
                   slave_sample.chamber_volume_m3 > 0.0,
               "compiled master-rod geometry failed pure evaluator dispatch");
        expect(shared_plan != nullptr &&
                   simulation::mechanism_kinematics_plan_matches_source(
                       *shared_plan, resolved.engine, profile.core),
               "fresh master-rod geometry plan did not match its source");
        auto stale_engine = resolved.engine;
        stale_engine.cylinders[0].journal_phase_rad.value += 0.01;
        expect(shared_plan != nullptr &&
                   !simulation::mechanism_kinematics_plan_matches_source(
                       *shared_plan, stale_engine, profile.core),
               "master-rod plan accepted a stale public root phase");
        auto stale_bank_engine = resolved.engine;
        auto same_angle_bank = stale_bank_engine.banks[0];
        same_angle_bank.id.value += 1000U;
        stale_bank_engine.banks.push_back(same_angle_bank);
        stale_bank_engine.cylinders[0].bank_id = same_angle_bank.id;
        expect(shared_plan != nullptr &&
                   !simulation::mechanism_kinematics_plan_matches_source(
                       *shared_plan, stale_bank_engine, profile.core),
               "master-rod plan accepted a stale same-angle bank reassignment");
        auto stale_core = profile.core;
        std::get<contract::LegacyMasterRodJournalKinematics>(
            stale_core.mechanism.cylinders[1].kinematics)
            .throw_radius_m.value += 0.001;
        expect(shared_plan != nullptr &&
                   !simulation::mechanism_kinematics_plan_matches_source(
                       *shared_plan, resolved.engine, stale_core),
               "master-rod plan accepted a stale resolved slave throw");

        auto phase_probe_document = make_master_rod_twin_document(assets);
        phase_probe_document.engine.layout = authoring::CylinderLayout::custom;
        phase_probe_document.engine.banks[0].angle = quantity(15.0, "deg");
        phase_probe_document.engine.journals[0].phase = quantity(40.0, "deg");
        auto phase_probe_resolved = require_value(
            compile_detail::resolve_engine_package(phase_probe_document, twin_views),
            "nonzero-bank master-rod phase probe failed to resolve");
        const auto &phase_probe_core =
            std::get<contract::LowOrderOperatingPointV1Profile>(
                phase_probe_resolved.engine.physics_profile)
                .core;
        const auto phase_probe_result = simulation::compile_mechanism_kinematics_plan(
            phase_probe_resolved.engine, phase_probe_core);
        const auto *phase_probe_shared =
            std::get_if<simulation::SharedMechanismKinematicsPlan>(&phase_probe_result);
        const auto *phase_probe_plan =
            phase_probe_shared == nullptr
                ? nullptr
                : simulation::one_level_master_rod_mechanism_kinematics_plan(
                      *phase_probe_shared);
        const auto *phase_probe_root =
            phase_probe_plan == nullptr
                ? nullptr
                : std::get_if<simulation::OneLevelMasterRodDirectRootPlan>(
                      &phase_probe_plan->cylinders[0].kinematics);
        const auto *phase_probe_direct =
            std::get_if<contract::LegacyDirectJournalKinematics>(
                &phase_probe_core.mechanism.cylinders[0].kinematics);
        expect(
            phase_probe_root != nullptr && phase_probe_direct != nullptr &&
                phase_probe_root->driver.crank_journal_global_phase_rad ==
                    phase_probe_resolved.engine.cylinders[0].journal_phase_rad.value &&
                phase_probe_root->driver.master_bank_angle_rad ==
                    phase_probe_resolved.engine.banks[0].angle_rad->value &&
                phase_probe_root->driver.crank_journal_global_phase_rad !=
                    phase_probe_direct->journal_angle_rad.value,
            "master-rod root phase was reconstructed from its bank-relative "
            "core phase instead of preserving the public global phase");

        const auto public_result = compile::compile_engine(document, twin_views);
        require_diagnostic(public_result,
                           authoring::DiagnosticCode::unsupported_capability,
                           "/engine/cylinders/1/master_rod_attachment",
                           "public master-rod execution gate");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        std::get<authoring::MasterRodJournalAttachment>(
            document.engine.journals[1].attachment)
            .master_cylinder.value = "missing-cylinder";
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::dangling_reference,
                           "/engine/journals/1/master_cylinder",
                           "dangling master cylinder");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        std::get<authoring::MasterRodJournalAttachment>(
            document.engine.journals[1].attachment)
            .throw_radius.value = 0.0;
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::out_of_range,
                           "/engine/journals/1/throw_radius/value",
                           "zero master-rod throw");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        document.engine.journals[1].phase.value =
            std::numeric_limits<double>::quiet_NaN();
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::invalid_value,
                           "/engine/journals/1/phase/value",
                           "nonfinite master-rod phase");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        std::get<authoring::MasterRodJournalAttachment>(
            document.engine.journals[1].attachment)
            .master_cylinder.value = "fixture-cylinder-2";
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::forbidden_cycle,
                           "/engine/journals/1/master_cylinder",
                           "self master-rod cycle");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        document.engine.journals[0].attachment = authoring::MasterRodJournalAttachment{
            {"fixture-cylinder-2"},
            quantity(29.0, "mm"),
        };
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::forbidden_cycle,
                           "/engine/journals/0/master_cylinder",
                           "two-journal master-rod cycle");
    }
    {
        auto document = make_master_rod_twin_document(assets);
        document.engine.cylinders[1].journal.value = "fixture-journal-1";
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::disconnected_object,
                           "/engine/journals/1", "unconsumed master-rod journal");
    }
    {
        authoring::EngineDefinition graph;
        graph.crankshafts.resize(1U);
        graph.crankshafts[0].id.value = "crank";
        graph.journals = {
            {{"direct"},
             authoring::CrankshaftJournalAttachment{{"crank"}},
             quantity(0.0, "deg")},
            {{"master-level-1"},
             authoring::MasterRodJournalAttachment{{"cylinder-1"},
                                                   quantity(20.0, "mm")},
             quantity(60.0, "deg")},
            {{"master-level-2"},
             authoring::MasterRodJournalAttachment{{"cylinder-2"},
                                                   quantity(15.0, "mm")},
             quantity(120.0, "deg")},
        };
        graph.cylinders.resize(3U);
        graph.cylinders[0].id.value = "cylinder-1";
        graph.cylinders[0].journal.value = "direct";
        graph.cylinders[1].id.value = "cylinder-2";
        graph.cylinders[1].journal.value = "master-level-1";
        graph.cylinders[2].id.value = "cylinder-3";
        graph.cylinders[2].journal.value = "master-level-2";
        const auto report = authoring::detail::validate_engine_mechanism_graph(graph);
        require_graph_diagnostic(report, authoring::DiagnosticCode::inconsistent_value,
                                 "/engine/journals/2/master_cylinder",
                                 "acyclic nested master-rod graph");
        expect(
            std::ranges::none_of(report.diagnostics,
                                 [](const auto &diagnostic) {
                                     return diagnostic.code ==
                                            authoring::DiagnosticCode::forbidden_cycle;
                                 }),
            "acyclic nested graph was misclassified as a cycle");
    }
    {
        authoring::EngineDefinition graph;
        graph.crankshafts.resize(1U);
        graph.crankshafts[0].id.value = "crank";
        graph.journals = {
            {{"direct"},
             authoring::CrankshaftJournalAttachment{{"crank"}},
             quantity(0.0, "deg")},
            {{"slave"},
             authoring::MasterRodJournalAttachment{{"cylinder-1"},
                                                   quantity(20.0, "mm")},
             quantity(60.0, "deg")},
        };
        graph.cylinders.resize(3U);
        graph.cylinders[0].id.value = "cylinder-1";
        graph.cylinders[0].journal.value = "direct";
        graph.cylinders[1].id.value = "cylinder-2";
        graph.cylinders[1].journal.value = "slave";
        graph.cylinders[2].id.value = "cylinder-3";
        graph.cylinders[2].journal.value = "slave";
        const auto report = authoring::detail::validate_engine_mechanism_graph(graph);
        require_graph_diagnostic(report, authoring::DiagnosticCode::inconsistent_value,
                                 "/engine/cylinders/2/journal",
                                 "multiply consumed master-rod journal");
        expect(
            std::ranges::none_of(report.diagnostics,
                                 [](const auto &diagnostic) {
                                     return diagnostic.code ==
                                            authoring::DiagnosticCode::forbidden_cycle;
                                 }),
            "double-consumer graph unexpectedly depended on a self-cycle");
    }
}

void test_direct_engine_dto_identity_and_enum_admission_fails_closed() {
    const SyntheticAssets assets = make_assets();
    auto views = assets.views();

    {
        auto document = make_engine_document(assets);
        document.engine.ports[1].id = document.engine.ports[0].id;
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::duplicate_id,
                           "/engine/ports/1/id", "duplicate direct-DTO port ID");
    }
    {
        auto document = make_engine_document(assets);
        document.engine.cylinders[1].id = document.engine.cylinders[0].id;
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::duplicate_id,
                           "/engine/cylinders/1/id",
                           "duplicate direct-DTO cylinder ID");
    }
    {
        auto document = make_engine_document(assets);
        document.engine.ports[1].kind = static_cast<authoring::PortKind>(0xffU);
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::invalid_value,
                           "/engine/ports/1/kind", "invalid direct-DTO port kind");
    }
}

void test_direct_scenario_dto_admission_fails_closed() {
    const SyntheticAssets assets = make_assets();
    const auto engine_document = make_engine_document(assets);
    auto views = assets.views();
    auto engine = require_value(compile::compile_engine(engine_document, views),
                                "scenario admission fixture engine compile failed");

    auto stale_schema = make_scenario_document();
    stale_schema.schema = "engine-sim-offline/scenario-obsolete";
    const auto stale_result = compile::compile_scenario(engine, stale_schema);
    require_diagnostic(stale_result, authoring::DiagnosticCode::unsupported_schema,
                       "/schema", "stale direct scenario DTO");

    auto unsupported_angle = make_scenario_document();
    unsupported_angle.initial_state.crank_angle = quantity(16.0, "deg");
    const auto angle_result = compile::compile_scenario(engine, unsupported_angle);
    require_diagnostic(angle_result, authoring::DiagnosticCode::unsupported_capability,
                       "/initial_state/crank_angle",
                       "executor-incompatible initial crank angle");
}

} // namespace

int main() {
    try {
        test_complete_generic_compile_and_determinism();
        test_inline_twin_one_route_reaches_executable_boundary();
        test_v_engine_resolves_bank_geometry_and_axis_relative_journals();
        test_custom_engine_resolves_arbitrary_bank_axes_for_direct_rods();
        test_equivalent_split_bank_heads_normalize_to_exact_execution();
        test_heterogeneous_split_bank_heads_fail_closed();
        test_asset_admission_is_exact_and_closed();
        test_rig_compiles_to_immutable_si_descriptors();
        test_cranking_starter_resolves_to_si_capability();
        test_sampled_fixed_cam_resolves_si_curve_and_provenance();
        test_harmonic_and_equivalent_sampled_cam_sessions_are_identical();
        test_invalid_sampled_fixed_cams_fail_closed();
        test_four_cam_vtec_resolves_to_si_and_provenance();
        test_governor_resolves_to_executable_controller();
        test_layout_shape_fails_closed();
        test_master_rod_graph_contract_and_execution_gate();
        test_direct_engine_dto_identity_and_enum_admission_fails_closed();
        test_direct_scenario_dto_admission_fails_closed();
        std::cout << "compiler integration tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "compiler integration test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
