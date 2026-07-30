#include "engine_sim_offline/authoring/engine_document.hpp"
#include "engine_sim_offline/authoring/scenario_document.hpp"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "engine_sim_offline/session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
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
    crankshaft.friction_torque = std::nullopt;
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
        crankshaft.journals.push_back({journal_id});
        engine.journals.push_back({
            {journal_id},
            {"fixture-crank"},
            quantity(journal_phases_deg[index], "deg"),
            std::nullopt,
            std::nullopt,
        });
        engine.connecting_rods.push_back({
            {rod_id},
            quantity(148.0, "mm"),
            quantity(570.0, "g"),
            quantity(0.0011, "kg*m2"),
            std::nullopt,
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
            {"fixture-crank"},
            {indexed_id("fixture-journal-", one_based)},
            std::nullopt,
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

[[nodiscard]] authoring::EnginePackageDocument
make_inline_twin_document(const SyntheticAssets &assets) {
    auto package = make_engine_document(assets);
    auto &engine = package.engine;
    engine.identity.id.value = "fixture-inline-twin";
    engine.identity.display_name = "Synthetic compiler integration inline-twin";

    engine.crankshafts.front().journals.resize(2U);
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
               require_asset(reordered, compile::AssetKind::audio,
                             "fixture-ir-front")
                   .bytes,
               retained_front.bytes) &&
               std::ranges::equal(
                   require_asset(reordered, compile::AssetKind::audio,
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
    auto created = engine_sim_offline::create_engine_session(scenario);
    if (const auto *error =
            std::get_if<engine_sim_offline::EngineSessionError>(&created)) {
        throw std::runtime_error{
            "inline-twin one-route session creation failed: " +
            error->detail_code + ": " + error->message};
    }
    auto session =
        std::get<engine_sim_offline::EngineSession>(std::move(created));
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
            throw std::runtime_error{
                "inline-twin session execution failed: " + error->detail_code +
                ": " + error->message};
        }
        const auto &completion =
            std::get<engine_sim_offline::EngineSessionCompleted>(result);
        expect(block_count == descriptor.total_block_count &&
                   completion.block_count == block_count,
               "inline-twin session did not execute its complete one-route horizon");
        break;
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

void test_unsupported_capability_fails_closed() {
    const SyntheticAssets assets = make_assets();
    {
        auto document = make_engine_document(assets);
        auto &valvetrain = document.engine.valvetrains.front();
        valvetrain.kind = authoring::VtecValvetrain{
            {"fixture-intake-cam"},
            {"fixture-exhaust-cam"},
            {"fixture-intake-cam"},
            {"fixture-exhaust-cam"},
            {
                quantity(5200.0, "rpm"),
                quantity(20.0, "km/h"),
                quantity(25.0, "kPa"),
                0.6,
            },
        };
        auto views = assets.views();
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/valvetrains/0", "unsupported VTEC valvetrain");
    }
    {
        auto document = make_engine_document(assets);
        document.engine.layout = authoring::CylinderLayout::v_engine;
        auto views = assets.views();
        const auto result = compile::compile_engine(document, views);
        require_diagnostic(result, authoring::DiagnosticCode::unsupported_capability,
                           "/engine/layout",
                           "non-inline engine without executed bank-angle semantics");
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
        test_asset_admission_is_exact_and_closed();
        test_unsupported_capability_fails_closed();
        test_direct_scenario_dto_admission_fails_closed();
        std::cout << "compiler integration tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &exception) {
        std::cerr << "compiler integration test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
