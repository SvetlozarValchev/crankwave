#include "excitation/captured_exhaust_excitation.hpp"
#include "profiles/bmw_m52b28_parity_request_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::excitation;

constexpr std::size_t kFrames = kCapturedExcitationFramesPerBlock;
constexpr std::size_t kCylinders = kCapturedExcitationCylinderCount;
constexpr std::size_t kRoutes = kCapturedExcitationRouteCount;
constexpr std::size_t kDelayFrames = 180U;
constexpr RationalRateHz kRate{10000, 1};
constexpr double kAtmospherePa = 101325.0;
constexpr double kExcitationScale = 1600.0;
constexpr double kSpeedThresholdRpm = 40.0;
constexpr double kCylinderDivisor = 6.0;
constexpr double kTotalAudioLengthM = 6.167266379343297;

void expect(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::uint64_t bits(double value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

void expect_same_bits(double actual, double expected, std::string_view message) {
    if (bits(actual) != bits(expected)) {
        throw std::runtime_error{std::string{message} + ": binary64 mismatch"};
    }
}

[[nodiscard]] profiles::BmwM52b28ParityRequest make_request() {
    return profiles::detail::build_bmw_m52b28_parity_request_unvalidated({});
}

[[nodiscard]] CapturedExhaustExcitationSession
require_session(CapturedExhaustExcitationCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "canonical BMW excitation request was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{message};
    }
    return std::get<CapturedExhaustExcitationSession>(std::move(result));
}

[[nodiscard]] bool is_physical_route(SourceRouteKind kind) noexcept {
    return kind == SourceRouteKind::exhaust_outlet ||
           kind == SourceRouteKind::intake_inlet ||
           kind == SourceRouteKind::mechanical_engine ||
           kind == SourceRouteKind::mechanical_starter;
}

/**
 * Owned, independently constructed CaptureBlock storage. No simulator or reference
 * fixture supplies these values; only the public capture contract shapes the view.
 */
class SyntheticCaptureBlock final {
  public:
    SyntheticCaptureBlock(const EngineSpec &engine, std::uint64_t first_frame_index)
        : engine_id_(engine.id), first_frame_index_(first_frame_index) {
        cylinders_.reserve(engine.cylinders.size());
        for (const auto &cylinder : engine.cylinders) {
            cylinders_.push_back(cylinder.id);
        }
        ports_.reserve(engine.ports.size());
        for (const auto &port : engine.ports) {
            ports_.push_back({port.id, port.cylinder_id, port.kind.value});
        }
        gas_volumes_.reserve(engine.gas_volumes.size());
        for (const auto &volume : engine.gas_volumes) {
            gas_volumes_.push_back({volume.id, volume.kind.value});
        }
        flow_edges_.reserve(engine.flow_edges.size());
        for (const auto &edge : engine.flow_edges) {
            flow_edges_.push_back(
                {edge.id, edge.endpoint_0_volume_id, edge.endpoint_1_volume_id});
        }
        routes_.reserve(engine.routes.size());
        for (const auto &route : engine.routes) {
            if (!is_physical_route(route.kind.value)) {
                continue;
            }
            routes_.push_back({
                route.id,
                route.kind.value,
                route.source_volume_id,
                route.default_parent_route_id,
                route.emitter_anchor_id.has_value()
                    ? std::optional<std::string>{route.emitter_anchor_id->value}
                    : std::nullopt,
            });
        }

        engine_samples_.resize(kFrames);
        cylinder_samples_.resize(kFrames * cylinders_.size());
        port_samples_.resize(kFrames * ports_.size());
        volume_samples_.resize(kFrames * gas_volumes_.size());
        edge_samples_.resize(kFrames * flow_edges_.size());
        route_samples_.resize(kFrames * routes_.size());
        event_offsets_.assign(kFrames + 1U, 0U);
        filtered_rpm_.resize(kFrames);
        parity_cylinders_.resize(kFrames * cylinders_.size());

        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            engine_samples_[frame].step_end_index =
                first_frame_index_ + static_cast<std::uint64_t>(frame) + 1U;
        }
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            for (std::size_t route = 0; route < routes_.size(); ++route) {
                const auto kind = routes_[route].kind;
                if (kind == SourceRouteKind::exhaust_outlet ||
                    kind == SourceRouteKind::intake_inlet) {
                    route_samples_[frame * routes_.size() + route] =
                        GasSourceRouteCaptureSample{};
                } else {
                    route_samples_[frame * routes_.size() + route] =
                        MechanicalSourceRouteCaptureSample{};
                }
            }
        }
    }

    void fill_distinct_excitation() {
        expect(cylinders_.size() == kCylinders,
               "synthetic excitation requires the six canonical cylinders");
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            const auto global = first_frame_index_ + static_cast<std::uint64_t>(frame);
            switch (global % 4U) {
            case 0U:
                filtered_rpm_[frame] = 0.0;
                break;
            case 1U:
                filtered_rpm_[frame] = -20.0;
                break;
            case 2U:
                filtered_rpm_[frame] = 40.0;
                break;
            default:
                filtered_rpm_[frame] = 80.0;
                break;
            }

            for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
                const auto signed_pattern =
                    static_cast<int>((global * 7U + cylinder * 3U) % 31U) - 15;
                auto &sample = parity_cylinders_[frame * kCylinders + cylinder];
                sample.exhaust_primary_static_pressure_pa_abs =
                    kAtmospherePa + static_cast<double>(signed_pattern) * 0.25;
                sample.dynamic_pressure_forward_pa =
                    static_cast<double>((global % 13U) + 1U) *
                    static_cast<double>(cylinder + 1U) * 0.25;
                sample.dynamic_pressure_reverse_pa =
                    static_cast<double>((global % 7U) + 2U) *
                    static_cast<double>(kCylinders - cylinder) * 0.125;
            }
        }
    }

    [[nodiscard]] CaptureBlockView view() const noexcept {
        const auto layout = CaptureLayoutView::borrow_for_callback(
            engine_id_, cylinders_, ports_, gas_volumes_, flow_edges_, routes_);
        const auto journal =
            EventJournalView::borrow_for_callback(event_offsets_, events_);
        const auto parity = ReferenceParityBlockView::borrow_for_callback(
            filtered_rpm_, parity_cylinders_);
        return CaptureBlockView::borrow_for_callback(
            layout,
            CaptureClock{kRate, first_frame_index_, first_frame_index_ + 1U,
                         SamplePhase::post_step},
            static_cast<std::uint32_t>(kFrames), static_cast<std::uint32_t>(kFrames),
            static_cast<std::uint32_t>(kFrames * 19U), engine_samples_,
            cylinder_samples_, port_samples_, volume_samples_, edge_samples_,
            route_samples_, journal, parity);
    }

    [[nodiscard]] std::vector<double> &filtered_rpm() noexcept {
        return filtered_rpm_;
    }

    [[nodiscard]] std::vector<ReferenceParityCylinderSample> &
    parity_cylinders() noexcept {
        return parity_cylinders_;
    }

    [[nodiscard]] const std::vector<double> &filtered_rpm() const noexcept {
        return filtered_rpm_;
    }

    [[nodiscard]] const std::vector<ReferenceParityCylinderSample> &
    parity_cylinders() const noexcept {
        return parity_cylinders_;
    }

  private:
    EngineId engine_id_;
    std::uint64_t first_frame_index_ = 0;
    std::vector<CylinderId> cylinders_;
    std::vector<PortIdentity> ports_;
    std::vector<GasVolumeIdentity> gas_volumes_;
    std::vector<FlowEdgeIdentity> flow_edges_;
    std::vector<RouteIdentity> routes_;
    std::vector<EngineCaptureSample> engine_samples_;
    std::vector<CylinderCaptureSample> cylinder_samples_;
    std::vector<PortCaptureSample> port_samples_;
    std::vector<GasVolumeCaptureSample> volume_samples_;
    std::vector<FlowEdgeCaptureSample> edge_samples_;
    std::vector<SourceRouteCaptureSample> route_samples_;
    std::vector<std::uint32_t> event_offsets_;
    std::vector<EngineEvent> events_;
    std::vector<double> filtered_rpm_;
    std::vector<ReferenceParityCylinderSample> parity_cylinders_;
};

struct PublishedBlockCopy {
    std::uint64_t first_frame_index = 0;
    RationalRateHz sample_rate{};
    std::array<CylinderId, kCylinders> cylinder_ids{};
    std::array<RouteId, kRoutes> route_ids{};
    std::vector<double> pre_delay;
    std::vector<double> post_delay;
    std::vector<presentation::ExhaustExcitationFrame> route_frames;
};

[[nodiscard]] PublishedBlockCopy
copy_callback_views(const presentation::ExhaustExcitationBlockView &output,
                    const ExhaustExcitationDiagnosticBlockView &diagnostic) {
    expect(output.first_frame_index() == diagnostic.first_frame_index() &&
               output.sample_rate() == diagnostic.sample_rate() &&
               output.route_ids() == diagnostic.route_ids(),
           "presentation and diagnostic callback metadata diverged");
    expect(output.frames().data() == diagnostic.route_bus_frames().data() &&
               output.frames().size() == diagnostic.route_bus_frames().size(),
           "diagnostics did not expose the exact published route-frame storage");
    return {
        output.first_frame_index(),
        output.sample_rate(),
        diagnostic.cylinder_ids(),
        diagnostic.route_ids(),
        {diagnostic.pre_delay_cylinder_values_engine_sim_source_unit().begin(),
         diagnostic.pre_delay_cylinder_values_engine_sim_source_unit().end()},
        {diagnostic.post_delay_cylinder_values_engine_sim_source_unit().begin(),
         diagnostic.post_delay_cylinder_values_engine_sim_source_unit().end()},
        {output.frames().begin(), output.frames().end()},
    };
}

[[nodiscard]] PublishedBlockCopy publish(CapturedExhaustExcitationSession &session,
                                         const CaptureBlockView &input,
                                         std::uint64_t expected_block_ordinal) {
    std::optional<PublishedBlockCopy> copy;
    const auto result = session.process_block(
        input, [&](const presentation::ExhaustExcitationBlockView &output,
                   const ExhaustExcitationDiagnosticBlockView &diagnostic) {
            copy = copy_callback_views(output, diagnostic);
            return true;
        });
    const auto *published = std::get_if<ExhaustExcitationBlockPublished>(&result);
    expect(published != nullptr && copy.has_value(),
           "valid synthetic capture did not publish one excitation block");
    expect(*published ==
               ExhaustExcitationBlockPublished{
                   expected_block_ordinal,
                   input.clock().first_sample_index,
                   input.frame_count(),
                   input.clock().first_sample_index + input.frame_count(),
               },
           "excitation publication progress changed");
    return std::move(*copy);
}

[[nodiscard]] double
independent_pre_delay(double filtered_rpm,
                      const ReferenceParityCylinderSample &sample) noexcept {
    const double speed_fraction =
        std::min(std::abs(filtered_rpm), kSpeedThresholdRpm) / kSpeedThresholdRpm;
    const double speed_squared = speed_fraction * speed_fraction;
    const double speed_cubed = speed_squared * speed_fraction;
    const double speed_scale = speed_cubed * kExcitationScale;
    const double gauge_static =
        sample.exhaust_primary_static_pressure_pa_abs - kAtmospherePa;
    const double forward = 0.1 * sample.dynamic_pressure_forward_pa;
    const double reverse = 0.1 * sample.dynamic_pressure_reverse_pa;
    const double pressure_term = gauge_static + forward + reverse;
    return speed_scale * pressure_term;
}

[[nodiscard]] double independent_route_term(double delayed, double volume) noexcept {
    const double total_audio_length_squared = kTotalAudioLengthM * kTotalAudioLengthM;
    const double inverse_length_squared = 1.0 / total_audio_length_squared;
    return 1.0 * ((volume * delayed) / kCylinderDivisor) * inverse_length_squared;
}

[[nodiscard]] std::vector<double>
independent_pre_delay(const SyntheticCaptureBlock &block) {
    std::vector<double> result(kFrames * kCylinders);
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
            const auto index = frame * kCylinders + cylinder;
            result[index] = independent_pre_delay(block.filtered_rpm()[frame],
                                                  block.parity_cylinders()[index]);
        }
    }
    return result;
}

void expect_equal_block(const PublishedBlockCopy &actual,
                        const PublishedBlockCopy &expected, std::string_view message) {
    expect(actual.first_frame_index == expected.first_frame_index &&
               actual.sample_rate == expected.sample_rate &&
               actual.cylinder_ids == expected.cylinder_ids &&
               actual.route_ids == expected.route_ids &&
               actual.pre_delay.size() == expected.pre_delay.size() &&
               actual.post_delay.size() == expected.post_delay.size() &&
               actual.route_frames.size() == expected.route_frames.size(),
           std::string{message} + ": shape or metadata mismatch");
    for (std::size_t index = 0; index < actual.pre_delay.size(); ++index) {
        expect_same_bits(actual.pre_delay[index], expected.pre_delay[index], message);
        expect_same_bits(actual.post_delay[index], expected.post_delay[index], message);
    }
    for (std::size_t frame = 0; frame < actual.route_frames.size(); ++frame) {
        for (std::size_t route = 0; route < kRoutes; ++route) {
            expect_same_bits(
                actual.route_frames[frame].route_values_engine_sim_source_unit[route],
                expected.route_frames[frame].route_values_engine_sim_source_unit[route],
                message);
        }
    }
}

void test_exact_arithmetic_delay_routes_and_continuity() {
    const auto request = make_request();
    SyntheticCaptureBlock block_0{request.engine, 0U};
    SyntheticCaptureBlock block_1{request.engine, kFrames};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();

    auto session =
        require_session(compile_captured_exhaust_excitation_session(request.engine));
    const auto actual_0 = publish(session, block_0.view(), 0U);
    const auto actual_1 = publish(session, block_1.view(), 1U);

    expect(actual_0.first_frame_index == 0U && actual_1.first_frame_index == kFrames &&
               actual_0.sample_rate == kRate && actual_1.sample_rate == kRate &&
               actual_0.cylinder_ids ==
                   std::array<CylinderId, kCylinders>{CylinderId{1}, CylinderId{2},
                                                      CylinderId{3}, CylinderId{4},
                                                      CylinderId{5}, CylinderId{6}} &&
               actual_0.route_ids ==
                   std::array<RouteId, kRoutes>{RouteId{1}, RouteId{2}},
           "excitation callback IDs, ordering, or clock changed");
    expect(actual_0.pre_delay.size() == kFrames * kCylinders &&
               actual_0.post_delay.size() == kFrames * kCylinders &&
               actual_0.route_frames.size() == kFrames,
           "excitation diagnostics do not cover the complete 200-frame block");

    const auto expected_pre_0 = independent_pre_delay(block_0);
    const auto expected_pre_1 = independent_pre_delay(block_1);
    std::vector<double> all_pre;
    all_pre.reserve(expected_pre_0.size() + expected_pre_1.size());
    all_pre.insert(all_pre.end(), expected_pre_0.begin(), expected_pre_0.end());
    all_pre.insert(all_pre.end(), expected_pre_1.begin(), expected_pre_1.end());

    const std::array<const PublishedBlockCopy *, 2> actual_blocks{&actual_0, &actual_1};
    for (std::size_t block = 0; block < actual_blocks.size(); ++block) {
        const auto &actual = *actual_blocks[block];
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            const auto global = block * kFrames + frame;
            std::array<double, kRoutes> expected_buses{};
            for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
                const auto local = frame * kCylinders + cylinder;
                const auto global_cylinder = global * kCylinders + cylinder;
                const auto expected_pre = all_pre[global_cylinder];
                expect_same_bits(actual.pre_delay[local], expected_pre,
                                 "frozen pre-delay arithmetic changed");

                const double expected_post =
                    global < kDelayFrames
                        ? 0.0
                        : all_pre[(global - kDelayFrames) * kCylinders + cylinder];
                expect_same_bits(actual.post_delay[local], expected_post,
                                 "180-frame delay history or block continuity changed");
                if (global < kDelayFrames) {
                    expect(bits(actual.post_delay[local]) == bits(0.0),
                           "delay startup did not emit canonical positive zero");
                }

                const bool even_cylinder_id = ((cylinder + 1U) % 2U) == 0U;
                const std::size_t route = even_cylinder_id ? 0U : 1U;
                const double volume = even_cylinder_id ? 0.5 : 1.0;
                expected_buses[route] += independent_route_term(expected_post, volume);
            }
            for (std::size_t route = 0; route < kRoutes; ++route) {
                expect_same_bits(
                    actual.route_frames[frame]
                        .route_values_engine_sim_source_unit[route],
                    expected_buses[route],
                    "stable cylinder accumulation, divisor, length, or route changed");
            }
        }
    }

    expect(session.next_frame_index() == 2U * kFrames &&
               session.published_block_count() == 2U && !session.faulted(),
           "two-block excitation session progress changed");
}

void test_independent_sessions_are_bit_deterministic() {
    const auto request = make_request();
    SyntheticCaptureBlock block_0{request.engine, 0U};
    SyntheticCaptureBlock block_1{request.engine, kFrames};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();

    auto first =
        require_session(compile_captured_exhaust_excitation_session(request.engine));
    auto second =
        require_session(compile_captured_exhaust_excitation_session(request.engine));
    const auto first_0 = publish(first, block_0.view(), 0U);
    const auto second_0 = publish(second, block_0.view(), 0U);
    const auto first_1 = publish(first, block_1.view(), 1U);
    const auto second_1 = publish(second, block_1.view(), 1U);
    expect_equal_block(first_0, second_0,
                       "independent excitation sessions diverged in block zero");
    expect_equal_block(first_1, second_1,
                       "independent excitation sessions shared or reset state");
}

[[nodiscard]] const FailureContext &
require_fault(const CapturedExhaustExcitationProcessResult &result,
              std::string_view code, std::string_view message) {
    const auto *failure = std::get_if<FailureContext>(&result);
    expect(failure != nullptr && failure->detail_code == code, std::string{message});
    return *failure;
}

void test_complete_prevalidation_is_terminal_and_does_not_advance() {
    const auto request = make_request();
    SyntheticCaptureBlock malformed{request.engine, 0U};
    malformed.fill_distinct_excitation();
    malformed.parity_cylinders().back().dynamic_pressure_reverse_pa =
        std::numeric_limits<double>::quiet_NaN();

    auto session =
        require_session(compile_captured_exhaust_excitation_session(request.engine));
    std::size_t callbacks = 0U;
    const auto first = session.process_block(
        malformed.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &) {
            ++callbacks;
            return true;
        });
    const FailureContext first_fault = require_fault(
        first, "captured-excitation-block-invalid",
        "malformed final input lane was not rejected by full prevalidation");
    expect(callbacks == 0U && session.faulted() && session.next_frame_index() == 0U &&
               session.published_block_count() == 0U,
           "prevalidation failure entered callback, advanced delay, or published");

    malformed.parity_cylinders().back().dynamic_pressure_reverse_pa = 1.0;
    const auto repeated = session.process_block(
        malformed.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &) {
            ++callbacks;
            return true;
        });
    const auto &repeated_fault =
        require_fault(repeated, first_fault.detail_code,
                      "prevalidation fault did not remain terminal on corrected input");
    expect(repeated_fault == first_fault && callbacks == 0U &&
               session.next_frame_index() == 0U &&
               session.published_block_count() == 0U,
           "prevalidation failure was not stable and state-preserving");
}

void test_consumer_rejection_and_exception_are_terminal() {
    const auto request = make_request();
    SyntheticCaptureBlock block{request.engine, 0U};
    block.fill_distinct_excitation();

    {
        auto session = require_session(
            compile_captured_exhaust_excitation_session(request.engine));
        std::size_t callbacks = 0U;
        const auto rejected = session.process_block(
            block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &) {
                ++callbacks;
                return false;
            });
        const FailureContext fault =
            require_fault(rejected, "captured-excitation-consumer-rejected",
                          "false excitation consumer did not reject publication");
        const auto repeated = session.process_block(
            block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &) {
                ++callbacks;
                return true;
            });
        expect(require_fault(repeated, fault.detail_code,
                             "consumer-rejection fault was not stable") == fault &&
                   callbacks == 1U && session.faulted() &&
                   session.next_frame_index() == 0U &&
                   session.published_block_count() == 0U,
               "consumer rejection published or retried callback work");
    }

    {
        auto session = require_session(
            compile_captured_exhaust_excitation_session(request.engine));
        std::size_t callbacks = 0U;
        const auto thrown = session.process_block(
            block.view(),
            [&](const presentation::ExhaustExcitationBlockView &,
                const ExhaustExcitationDiagnosticBlockView &) -> bool {
                ++callbacks;
                throw std::runtime_error{"intentional excitation consumer failure"};
            });
        const FailureContext fault =
            require_fault(thrown, "captured-excitation-consumer-threw",
                          "excitation consumer exception escaped its transaction");
        const auto repeated = session.process_block(
            block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &) {
                ++callbacks;
                return true;
            });
        expect(require_fault(repeated, fault.detail_code,
                             "consumer-exception fault was not stable") == fault &&
                   callbacks == 1U && session.faulted() &&
                   session.next_frame_index() == 0U &&
                   session.published_block_count() == 0U,
               "consumer exception published or retried callback work");
    }
}

void test_reentrant_callback_preserves_outer_views_and_faults() {
    const auto request = make_request();
    SyntheticCaptureBlock block{request.engine, 0U};
    block.fill_distinct_excitation();
    auto session =
        require_session(compile_captured_exhaust_excitation_session(request.engine));

    std::size_t outer_callbacks = 0U;
    std::size_t nested_callbacks = 0U;
    std::optional<FailureContext> nested_fault;
    std::exception_ptr callback_error;
    const auto outer = session.process_block(
        block.view(), [&](const presentation::ExhaustExcitationBlockView &output,
                          const ExhaustExcitationDiagnosticBlockView &diagnostic) {
            try {
                ++outer_callbacks;
                const auto before = copy_callback_views(output, diagnostic);
                const auto nested = session.process_block(
                    block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                                      const ExhaustExcitationDiagnosticBlockView &) {
                        ++nested_callbacks;
                        return true;
                    });
                nested_fault =
                    require_fault(nested, "captured-excitation-consumer-reentrant",
                                  "nested excitation publication was not rejected");
                const auto after = copy_callback_views(output, diagnostic);
                expect_equal_block(after, before,
                                   "nested call mutated the borrowed outer views");
                return true;
            } catch (...) {
                callback_error = std::current_exception();
                return false;
            }
        });
    if (callback_error != nullptr) {
        std::rethrow_exception(callback_error);
    }

    const auto &outer_fault =
        require_fault(outer, "captured-excitation-consumer-reentrant",
                      "outer excitation transaction ignored its nested terminal fault");
    expect(nested_fault.has_value() && outer_fault == *nested_fault &&
               outer_callbacks == 1U && nested_callbacks == 0U && session.faulted() &&
               session.next_frame_index() == 0U &&
               session.published_block_count() == 0U,
           "reentrant excitation publication was not one stable unpublished fault");
}

void expect_compile_rejected(EngineSpec engine, std::string_view mutation) {
    const auto result = compile_captured_exhaust_excitation_session(engine);
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr && !report->ok(),
           std::string{mutation} + " was admitted by the excitation compiler");
}

void test_compile_rejects_method_profile_rate_and_layout_drift() {
    const auto request = make_request();
    {
        auto engine = request.engine;
        engine.methods.excitation.value.configuration_sha256.bytes[0] ^= 0x01U;
        expect_compile_rejected(std::move(engine),
                                "drifted excitation method configuration");
    }
    {
        auto engine = request.engine;
        auto &profile = std::get<LegacyLowOrderV1Profile>(engine.physics_profile);
        profile.core.excitation.cylinder_count_divisor.value = 5.0;
        expect_compile_rejected(std::move(engine), "drifted excitation profile");
    }
    {
        auto engine = request.engine;
        auto &profile = std::get<LegacyLowOrderV1Profile>(engine.physics_profile);
        profile.core.excitation.delay_rate.value = {9999, 1};
        expect_compile_rejected(std::move(engine), "drifted excitation rate");
    }
    {
        auto engine = request.engine;
        std::swap(engine.cylinders[0], engine.cylinders[1]);
        expect_compile_rejected(std::move(engine),
                                "drifted same-shape cylinder layout");
    }
}

void run_tests() {
    test_exact_arithmetic_delay_routes_and_continuity();
    test_independent_sessions_are_bit_deterministic();
    test_complete_prevalidation_is_terminal_and_does_not_advance();
    test_consumer_rejection_and_exception_are_terminal();
    test_reentrant_callback_preserves_outer_views_and_faults();
    test_compile_rejects_method_profile_rate_and_layout_drift();
}

} // namespace

int main() {
    try {
        run_tests();
    } catch (const std::exception &error) {
        std::cerr << "legacy low-order exhaust excitation test failed: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
