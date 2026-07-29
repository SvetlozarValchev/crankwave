#include "experimental/bmw_physical_input_capture.hpp"

#include "engine_sim_offline/profiles/bmw_m52b28_held_regression_request.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_inertial_dyno_listening_request.hpp"
#include "engine_sim_offline/request_identity.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::experimental {
namespace {

inline constexpr std::size_t kBmwCylinderCount = 6U;
inline constexpr std::size_t kHeldRegressionPointIndex = 2U;
inline constexpr std::uint64_t kCaptureBlockFrameCount = 200U;
inline constexpr contract::RationalRateHz kCaptureRate{10'000U, 1U};

struct CaptureRequest {
    contract::EngineSpec engine;
    contract::RenderScenario scenario;
    contract::ProvenanceLedger provenance;
};

[[nodiscard]] std::string validation_error(std::string prefix,
                                           const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return prefix + ": validation failed without a diagnostic";
    }
    const auto &issue = report.issues.front();
    return std::move(prefix) + ": " + issue.path + ": " + issue.message;
}

[[nodiscard]] std::variant<CaptureRequest, std::string>
make_capture_request(BmwPhysicalListeningMode mode) {
    if (mode == BmwPhysicalListeningMode::held_3000_throttle_0p85) {
        auto result = profiles::make_bmw_m52b28_held_regression_request_set();
        if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
            return validation_error(
                "canonical BMW held-regression request construction failed", *report);
        }

        auto requests =
            std::get<profiles::BmwM52b28HeldRegressionRequestSet>(std::move(result));
        if (kHeldRegressionPointIndex >= requests.size()) {
            return std::string{
                "canonical BMW held-regression request set has no point index 2"};
        }
        auto request = std::move(requests[kHeldRegressionPointIndex]);
        if (request.point_key != "rpm3000-throttle0p85") {
            return std::string{
                "canonical BMW held-regression point index 2 changed identity"};
        }
        return CaptureRequest{
            std::move(request.engine),
            std::move(request.scenario),
            std::move(request.provenance),
        };
    }

    if (mode == BmwPhysicalListeningMode::inertial_dyno) {
        auto result = profiles::make_bmw_m52b28_inertial_dyno_listening_request();
        if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
            return validation_error(
                "canonical BMW inertial-dyno request construction failed", *report);
        }
        auto request = std::get<profiles::BmwM52b28InertialDynoListeningRequest>(
            std::move(result));
        return CaptureRequest{
            std::move(request.engine),
            std::move(request.scenario),
            std::move(request.provenance),
        };
    }

    return std::string{"unsupported BMW physical-listening mode"};
}

[[nodiscard]] std::optional<std::array<std::size_t, kBmwCylinderCount>>
resolve_exhaust_port_indices(const contract::CaptureLayoutView &layout) {
    if (layout.cylinders().size() != kBmwCylinderCount) {
        return std::nullopt;
    }

    std::array<std::size_t, kBmwCylinderCount> result{};
    for (std::size_t cylinder = 0; cylinder < kBmwCylinderCount; ++cylinder) {
        const auto cylinder_id = layout.cylinders()[cylinder];
        if (cylinder_id !=
            contract::CylinderId{static_cast<std::uint32_t>(cylinder + 1U)}) {
            return std::nullopt;
        }
        std::optional<std::size_t> exhaust_port_index;
        for (std::size_t port = 0; port < layout.ports().size(); ++port) {
            const auto &identity = layout.ports()[port];
            if (identity.cylinder_id != cylinder_id ||
                identity.kind != contract::PortKind::exhaust) {
                continue;
            }
            if (exhaust_port_index.has_value()) {
                return std::nullopt;
            }
            exhaust_port_index = port;
        }
        if (!exhaust_port_index.has_value()) {
            return std::nullopt;
        }
        result[cylinder] = *exhaust_port_index;
    }
    return result;
}

[[nodiscard]] bool has_validity(contract::CaptureValidityMask mask,
                                contract::CaptureValidity validity) noexcept {
    return (mask & contract::capture_validity_mask(validity)) != 0U;
}

[[nodiscard]] std::variant<std::vector<std::array<PhysicalValveBoundary, 6>>,
                           std::string>
capture_block_frames(const contract::CaptureBlockView &block,
                     const contract::EngineSpec &engine,
                     const contract::RenderScenario &scenario,
                     std::uint64_t expected_first_frame) {
    const auto validation = contract::validate(block, engine, scenario);
    if (!validation.ok()) {
        return validation_error("BMW physical-input capture block is invalid",
                                validation);
    }
    if (block.clock().rate != kCaptureRate ||
        block.clock().phase != contract::SamplePhase::post_step ||
        block.clock().first_sample_index != expected_first_frame ||
        block.declared_block_capacity_frames() != kCaptureBlockFrameCount) {
        return std::string{
            "BMW physical-input capture requires contiguous 10 kHz post-step blocks"};
    }

    const auto exhaust_port_indices = resolve_exhaust_port_indices(block.layout());
    if (!exhaust_port_indices.has_value()) {
        return std::string{
            "BMW physical-input capture requires six cylinders with one exhaust "
            "port each"};
    }

    std::vector<std::array<PhysicalValveBoundary, 6>> frames;
    frames.reserve(block.frame_count());
    for (std::size_t frame = 0; frame < block.frame_count(); ++frame) {
        std::array<PhysicalValveBoundary, kBmwCylinderCount> boundaries{};
        for (std::size_t cylinder = 0; cylinder < kBmwCylinderCount; ++cylinder) {
            const auto *chamber = block.cylinder_sample(frame, cylinder);
            const auto *port =
                block.port_sample(frame, (*exhaust_port_indices)[cylinder]);
            if (chamber == nullptr || port == nullptr ||
                !has_validity(chamber->validity,
                              contract::CaptureValidity::thermodynamic_state) ||
                !has_validity(port->validity,
                              contract::CaptureValidity::gas_exchange) ||
                !std::isfinite(chamber->pressure_pa_abs) ||
                chamber->pressure_pa_abs <= 0.0 ||
                !std::isfinite(chamber->temperature_k) ||
                chamber->temperature_k <= 0.0 ||
                !std::isfinite(
                    port->effective_molar_flow_conductance_m2_sqrt_mol_per_kg) ||
                port->effective_molar_flow_conductance_m2_sqrt_mol_per_kg < 0.0) {
                return std::string{
                    "BMW physical-input capture encountered an invalid chamber or "
                    "exhaust-valve boundary"};
            }
            boundaries[cylinder] = {
                chamber->pressure_pa_abs,
                chamber->temperature_k,
                port->effective_molar_flow_conductance_m2_sqrt_mol_per_kg,
            };
        }
        frames.push_back(boundaries);
    }
    return frames;
}

[[nodiscard]] BmwPhysicalInputCaptureResult
capture_request(CaptureRequest request, BmwPhysicalListeningMode mode) {
    auto identity_result = identity::encode_simulation_request_identity_v3(
        request.engine, request.scenario, request.provenance.bundle);
    if (const auto *error =
            std::get_if<identity::SimulationRequestIdentityError>(&identity_result)) {
        return "BMW physical-input request identity failed (" + error->detail_code +
               "): " + error->message;
    }
    const auto request_identity = std::get<identity::SimulationRequestIdentityEncoding>(
                                      std::move(identity_result))
                                      .sha256;

    auto compilation = simulation::compile_low_order_capture_session(
        request.engine, request.scenario, request_identity);
    if (const auto *report = std::get_if<contract::ValidationReport>(&compilation)) {
        return validation_error("BMW physical-input simulation compilation failed",
                                *report);
    }
    auto capture = std::get<simulation::LowOrderCaptureSession>(std::move(compilation));

    const auto expected_frame_count = contract::resolve_frame_index(
        request.scenario.total_duration_s.value, request.scenario.rates.capture);
    if (!expected_frame_count.has_value() ||
        *expected_frame_count >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::string{
            "BMW physical-input scenario has an unrepresentable capture horizon"};
    }

    BmwPhysicalInputHistory history;
    history.scenario_id = request.scenario.scenario_id;
    history.ambient_pressure_pa = request.scenario.ambient.pressure_pa_abs.value;
    history.ambient_temperature_k = request.scenario.ambient.temperature_k.value;
    history.audible_start_s = request.scenario.audible_start_s.value;
    history.audible_duration_s = request.scenario.audible_duration_s.value;
    history.total_duration_s = request.scenario.total_duration_s.value;
    history.frames_10khz.reserve(static_cast<std::size_t>(*expected_frame_count));

    std::optional<simulation::LowOrderCaptureCompleted> completion;
    while (!completion.has_value()) {
        std::optional<std::string> callback_error;
        auto result =
            capture.publish_next_block([&](const contract::CaptureBlockView &block) {
                auto block_result = capture_block_frames(
                    block, request.engine, request.scenario,
                    static_cast<std::uint64_t>(history.frames_10khz.size()));
                if (auto *error = std::get_if<std::string>(&block_result)) {
                    callback_error = std::move(*error);
                    return false;
                }
                auto frames =
                    std::get<std::vector<std::array<PhysicalValveBoundary, 6>>>(
                        std::move(block_result));
                history.frames_10khz.insert(history.frames_10khz.end(),
                                            std::make_move_iterator(frames.begin()),
                                            std::make_move_iterator(frames.end()));
                return true;
            });

        if (callback_error.has_value()) {
            return std::move(*callback_error);
        }
        if (const auto *failure = std::get_if<contract::FailureContext>(&result)) {
            return "BMW physical-input simulation failed (" + failure->detail_code +
                   "): " + failure->state_summary;
        }
        if (const auto *completed =
                std::get_if<simulation::LowOrderCaptureCompleted>(&result)) {
            completion = *completed;
        }
    }

    if (completion->sample_count != *expected_frame_count ||
        completion->block_count !=
            (*expected_frame_count + kCaptureBlockFrameCount - 1U) /
                kCaptureBlockFrameCount ||
        capture.published_sample_count() != *expected_frame_count ||
        capture.published_block_count() != completion->block_count ||
        history.frames_10khz.size() !=
            static_cast<std::size_t>(*expected_frame_count)) {
        return std::string{
            "BMW physical-input simulation completed with a mismatched frame count"};
    }

    if (mode == BmwPhysicalListeningMode::held_3000_throttle_0p85) {
        if (!completion->held_speed_operating_point.has_value() ||
            completion->inertial_dyno.has_value()) {
            return std::string{
                "BMW held physical-input simulation returned wrong completion "
                "evidence"};
        }
        const auto validation =
            contract::validate(*completion->held_speed_operating_point,
                               request.scenario, request.engine, request_identity);
        if (!validation.ok()) {
            return validation_error(
                "BMW held physical-input completion evidence is invalid", validation);
        }
        history.held = std::move(completion->held_speed_operating_point);
    } else {
        if (completion->held_speed_operating_point.has_value() ||
            !completion->inertial_dyno.has_value()) {
            return std::string{
                "BMW inertial physical-input simulation returned wrong completion "
                "evidence"};
        }
        const auto validation = contract::validate(*completion->inertial_dyno,
                                                   request.scenario, request_identity);
        if (!validation.ok()) {
            return validation_error(
                "BMW inertial physical-input completion evidence is invalid",
                validation);
        }
        history.inertial = std::move(completion->inertial_dyno);
    }

    return history;
}

} // namespace

BmwPhysicalInputCaptureResult
capture_bmw_physical_input(BmwPhysicalListeningMode mode) {
    try {
        auto request_result = make_capture_request(mode);
        if (auto *error = std::get_if<std::string>(&request_result)) {
            return std::move(*error);
        }
        return capture_request(std::get<CaptureRequest>(std::move(request_result)),
                               mode);
    } catch (const std::exception &error) {
        return "BMW physical-input capture threw: " + std::string{error.what()};
    } catch (...) {
        return std::string{"BMW physical-input capture threw a non-standard exception"};
    }
}

} // namespace engine_sim_offline::experimental
