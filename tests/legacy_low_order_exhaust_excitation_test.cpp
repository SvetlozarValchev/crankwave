#include "authored_engine_fixture_support.hpp"
#include "excitation/captured_exhaust_excitation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
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

constexpr std::size_t kFrames = 400U;
constexpr std::size_t kCylinders = 6U;
constexpr std::size_t kRoutes = 2U;
constexpr std::size_t kDelayFrames = 360U;
constexpr RationalRateHz kRate{20000, 1};
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

[[nodiscard]] CapturedExhaustExcitationCompileResult
compile_fixture_session(const EngineSpec &engine, const RenderScenario &scenario) {
    return compile_captured_exhaust_excitation_session(engine,
                                                       test::low_order_core(engine),
                                                       scenario);
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
    SyntheticCaptureBlock(const EngineSpec &engine, std::uint64_t first_frame_index,
                          RationalRateHz rate = kRate)
        : engine_id_(engine.id), first_frame_index_(first_frame_index), rate_(rate) {
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
        const std::size_t cylinder_count = cylinders_.size();
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

            for (std::size_t cylinder = 0; cylinder < cylinder_count; ++cylinder) {
                const auto signed_pattern =
                    static_cast<int>((global * 7U + cylinder * 3U) % 31U) - 15;
                auto &sample = parity_cylinders_[frame * cylinder_count + cylinder];
                sample.exhaust_primary_static_pressure_pa_abs =
                    kAtmospherePa + static_cast<double>(signed_pattern) * 0.25;
                sample.dynamic_pressure_forward_pa =
                    static_cast<double>((global % 13U) + 1U) *
                    static_cast<double>(cylinder + 1U) * 0.25;
                sample.dynamic_pressure_reverse_pa =
                    static_cast<double>((global % 7U) + 2U) *
                    static_cast<double>(cylinder_count - cylinder) * 0.125;
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
            CaptureClock{rate_, first_frame_index_, first_frame_index_ + 1U,
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
    RationalRateHz rate_{};
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
    std::size_t frame_count = 0;
    std::vector<CylinderId> cylinder_ids;
    std::vector<RouteId> route_ids;
    std::vector<double> pre_delay;
    std::vector<double> post_delay;
    std::vector<double> route_bus_values;
};

[[nodiscard]] PublishedBlockCopy
copy_callback_views(const presentation::ExhaustExcitationBlockView &output,
                    const ExhaustExcitationDiagnosticBlockView &diagnostic) {
    expect(output.first_frame_index() == diagnostic.first_frame_index() &&
               output.sample_rate() == diagnostic.sample_rate() &&
               output.frame_count() == diagnostic.frame_count() &&
               output.route_count() == diagnostic.route_count() &&
               std::ranges::equal(output.route_ids(), diagnostic.route_ids()),
           "presentation and diagnostic callback metadata diverged");
    expect(output.values_engine_sim_source_unit().data() ==
                   diagnostic.route_bus_values_engine_sim_source_unit().data() &&
               output.values_engine_sim_source_unit().size() ==
                   diagnostic.route_bus_values_engine_sim_source_unit().size(),
           "diagnostics did not expose the exact published route-value storage");
    expect(diagnostic.cylinder_count() == diagnostic.cylinder_ids().size() &&
               diagnostic.route_count() == diagnostic.route_ids().size() &&
               diagnostic.pre_delay_cylinder_values_engine_sim_source_unit().size() ==
                   diagnostic.frame_count() * diagnostic.cylinder_count() &&
               diagnostic.post_delay_cylinder_values_engine_sim_source_unit().size() ==
                   diagnostic.frame_count() * diagnostic.cylinder_count() &&
               diagnostic.route_bus_values_engine_sim_source_unit().size() ==
                   diagnostic.frame_count() * diagnostic.route_count(),
           "dynamic excitation diagnostic spans are not complete frame-major matrices");
    for (std::size_t frame = 0; frame < output.frame_count(); ++frame) {
        const auto frame_values = output.frame_values_engine_sim_source_unit(frame);
        expect(frame_values.size() == output.route_count() &&
                   frame_values.data() ==
                       output.values_engine_sim_source_unit().data() +
                           frame * output.route_count(),
               "indexed excitation frame view does not match flat frame-major storage");
        for (std::size_t route = 0; route < output.route_count(); ++route) {
            expect_same_bits(
                output.value_engine_sim_source_unit(frame, route),
                output.values_engine_sim_source_unit()[frame * output.route_count() +
                                                       route],
                "indexed excitation value does not match flat frame-major storage");
        }
    }
    return {
        output.first_frame_index(),
        output.sample_rate(),
        output.frame_count(),
        {diagnostic.cylinder_ids().begin(), diagnostic.cylinder_ids().end()},
        {diagnostic.route_ids().begin(), diagnostic.route_ids().end()},
        {diagnostic.pre_delay_cylinder_values_engine_sim_source_unit().begin(),
         diagnostic.pre_delay_cylinder_values_engine_sim_source_unit().end()},
        {diagnostic.post_delay_cylinder_values_engine_sim_source_unit().begin(),
         diagnostic.post_delay_cylinder_values_engine_sim_source_unit().end()},
        {output.values_engine_sim_source_unit().begin(),
         output.values_engine_sim_source_unit().end()},
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

[[nodiscard]] EngineSpec make_three_cylinder_single_route_engine(EngineSpec engine) {
    auto &core = test::low_order_core(engine);

    engine.cylinders.resize(3U);
    std::erase_if(engine.ports, [&](const PortSpec &port) {
        return std::ranges::none_of(engine.cylinders,
                                    [&](const CylinderSpec &cylinder) {
                                        return cylinder.id == port.cylinder_id;
                                    });
    });
    engine.routes.resize(1U);

    core.mechanism.cylinders.resize(3U);
    const auto selected_route_id = engine.routes.front().id;
    const auto gas_route = *std::ranges::find_if(
        core.gas_path.exhaust_routes, [&](const LegacyExhaustRouteProfile &route) {
            return route.topology.route_id == selected_route_id;
        });
    core.gas_path.exhaust_routes = {gas_route};

    const auto excitation_route = *std::ranges::find_if(
        core.excitation.routes, [&](const LegacyExcitationRoute &route) {
            return route.route_id == selected_route_id;
        });
    core.excitation.routes = {excitation_route};
    core.excitation.cylinder_count_divisor.value =
        static_cast<double>(engine.cylinders.size());

    std::erase_if(core.excitation.cylinder_paths,
                  [&](const LegacyExcitationCylinderPath &path) {
                      return std::ranges::none_of(
                          engine.cylinders, [&](const CylinderSpec &cylinder) {
                              return cylinder.id == path.cylinder_id;
                          });
                  });
    for (auto &path : core.excitation.cylinder_paths) {
        path.route_id = selected_route_id;
        path.sound_attenuation_linear.value =
            path.cylinder_id == CylinderId{2} ? 1.0 : 0x1p60;
    }
    for (auto &cylinder : core.mechanism.cylinders) {
        cylinder.topology.exhaust_route_id = selected_route_id;
    }

    core.excitation.cylinder_accumulation_order.value = {CylinderId{1}, CylinderId{3},
                                                         CylinderId{2}};
    return engine;
}

void expect_equal_block(const PublishedBlockCopy &actual,
                        const PublishedBlockCopy &expected, std::string_view message) {
    expect(actual.first_frame_index == expected.first_frame_index &&
               actual.sample_rate == expected.sample_rate &&
               actual.frame_count == expected.frame_count &&
               actual.cylinder_ids == expected.cylinder_ids &&
               actual.route_ids == expected.route_ids &&
               actual.pre_delay.size() == expected.pre_delay.size() &&
               actual.post_delay.size() == expected.post_delay.size() &&
               actual.route_bus_values.size() == expected.route_bus_values.size(),
           std::string{message} + ": shape or metadata mismatch");
    for (std::size_t index = 0; index < actual.pre_delay.size(); ++index) {
        expect_same_bits(actual.pre_delay[index], expected.pre_delay[index], message);
        expect_same_bits(actual.post_delay[index], expected.post_delay[index], message);
    }
    for (std::size_t index = 0; index < actual.route_bus_values.size(); ++index) {
        expect_same_bits(actual.route_bus_values[index],
                         expected.route_bus_values[index], message);
    }
}

void test_exact_arithmetic_delay_routes_and_continuity(
    const EngineSpec &engine, const RenderScenario &scenario) {
    SyntheticCaptureBlock block_0{engine, 0U};
    SyntheticCaptureBlock block_1{engine, kFrames};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();

    auto session = require_session(compile_fixture_session(engine, scenario));
    const auto actual_0 = publish(session, block_0.view(), 0U);
    const auto actual_1 = publish(session, block_1.view(), 1U);

    expect(actual_0.first_frame_index == 0U && actual_1.first_frame_index == kFrames &&
               actual_0.sample_rate == kRate && actual_1.sample_rate == kRate &&
               std::ranges::equal(actual_0.cylinder_ids,
                                  std::array<CylinderId, kCylinders>{
                                      CylinderId{1}, CylinderId{2}, CylinderId{3},
                                      CylinderId{4}, CylinderId{5}, CylinderId{6}}) &&
               std::ranges::equal(actual_0.route_ids,
                                  std::array<RouteId, kRoutes>{RouteId{1}, RouteId{2}}),
           "excitation callback IDs, ordering, or clock changed");
    expect(actual_0.pre_delay.size() == kFrames * kCylinders &&
               actual_0.post_delay.size() == kFrames * kCylinders &&
               actual_0.frame_count == kFrames &&
               actual_0.route_bus_values.size() == kFrames * kRoutes,
           "excitation diagnostics do not cover the complete 400-frame block");

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
                    actual.route_bus_values[frame * kRoutes + route],
                    expected_buses[route],
                    "stable cylinder accumulation, divisor, length, or route changed");
            }
        }
    }

    expect(session.next_frame_index() == 2U * kFrames &&
               session.published_block_count() == 2U && !session.faulted(),
           "two-block excitation session progress changed");
}

void test_canonical_delay_is_derived_at_session_admission(
    const EngineSpec &engine, RenderScenario scenario) {
    constexpr RationalRateHz rate = kRate;
    constexpr std::size_t expected_delay_frames = 360U;
    static_assert(expected_delay_frames == kDelayFrames);
    expect(static_cast<std::size_t>(std::round(
               (kTotalAudioLengthM / 343.0) *
               static_cast<double>(rate.numerator) /
               static_cast<double>(rate.denominator))) == expected_delay_frames,
           "20 kHz path-time derivation did not resolve to 360 samples");

    scenario.rates.physics = rate;
    scenario.rates.capture = rate;
    scenario.quality.value.capture_block_capacity_frames =
        static_cast<std::uint32_t>(kFrames);

    SyntheticCaptureBlock block_0{engine, 0U, rate};
    SyntheticCaptureBlock block_1{engine, kFrames, rate};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();
    block_0.filtered_rpm().front() = 80.0;

    auto session = require_session(compile_fixture_session(engine, scenario));
    const auto actual_0 = publish(session, block_0.view(), 0U);
    const auto actual_1 = publish(session, block_1.view(), 1U);

    expect(actual_0.sample_rate == rate && actual_1.sample_rate == rate &&
               actual_0.frame_count == kFrames && actual_1.frame_count == kFrames,
           "20 kHz excitation did not publish on its admitted capture clock");
    for (std::size_t frame = 0; frame < expected_delay_frames; ++frame) {
        for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
            expect(bits(actual_0.post_delay[frame * kCylinders + cylinder]) ==
                       bits(+0.0),
                   "20 kHz propagation delay arrived before 360 capture samples");
        }
    }

    bool observed_nonzero_arrival = false;
    for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
        const double expected = actual_0.pre_delay[cylinder];
        const double actual =
            actual_0.post_delay[expected_delay_frames * kCylinders + cylinder];
        expect_same_bits(actual, expected,
                         "20 kHz propagation delay was not derived from path time");
        observed_nonzero_arrival = observed_nonzero_arrival || actual != 0.0;
    }
    expect(observed_nonzero_arrival,
           "20 kHz propagation-delay proof did not observe a nonzero arrival");
    expect(session.next_frame_index() == 2U * kFrames &&
               session.published_block_count() == 2U && !session.faulted(),
           "20 kHz excitation session progress changed");
}

void test_three_cylinder_authored_order_collector(const EngineSpec &canonical_engine,
                                                  const RenderScenario &scenario) {
    const auto engine = make_three_cylinder_single_route_engine(canonical_engine);
    SyntheticCaptureBlock block_0{engine, 0U};
    SyntheticCaptureBlock block_1{engine, kFrames};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();
    block_0.filtered_rpm().front() = 80.0;
    for (std::size_t cylinder = 0; cylinder < 3U; ++cylinder) {
        auto &sample = block_0.parity_cylinders()[cylinder];
        sample.exhaust_primary_static_pressure_pa_abs =
            cylinder == 2U ? kAtmospherePa - 1.0 : kAtmospherePa + 1.0;
        sample.dynamic_pressure_forward_pa = 0.0;
        sample.dynamic_pressure_reverse_pa = 0.0;
    }

    auto session = require_session(compile_fixture_session(engine, scenario));
    const auto actual_0 = publish(session, block_0.view(), 0U);
    const auto actual_1 = publish(session, block_1.view(), 1U);

    expect(actual_0.frame_count == kFrames &&
               actual_0.cylinder_ids == std::vector<CylinderId>{CylinderId{1},
                                                                CylinderId{2},
                                                                CylinderId{3}} &&
               actual_0.route_ids == std::vector<RouteId>{RouteId{1}} &&
               actual_0.pre_delay.size() == kFrames * 3U &&
               actual_0.post_delay.size() == kFrames * 3U &&
               actual_0.route_bus_values.size() == kFrames &&
               actual_1.route_bus_values.size() == kFrames,
           "dynamic excitation did not publish the admitted 3-cylinder/1-route shape");

    const auto &source = test::low_order_core(engine).excitation;
    expect(source.routes.size() == 1U && source.cylinder_paths.size() == 3U &&
               source.cylinder_accumulation_order.value ==
                   std::vector<CylinderId>{CylinderId{1}, CylinderId{3}, CylinderId{2}},
           "shared-route collector fixture lost its three authored-order lanes");
    const auto &route = source.routes.front();
    constexpr std::size_t arrival_frame = kDelayFrames;
    constexpr std::array<double, 3> expected_delayed{1600.0, 1600.0, -1600.0};
    for (std::size_t cylinder = 0; cylinder < expected_delayed.size(); ++cylinder) {
        expect_same_bits(actual_0.post_delay[arrival_frame * 3U + cylinder],
                         expected_delayed[cylinder],
                         "post-delay lane left canonical cylinder identity order");
    }
    const auto route_term = [&](const std::size_t cylinder) {
        const auto path =
            std::ranges::find_if(source.cylinder_paths, [&](const auto &candidate) {
                return candidate.cylinder_id ==
                       CylinderId{static_cast<std::uint32_t>(cylinder + 1U)};
            });
        expect(path != source.cylinder_paths.end(),
               "adversarial collector path did not resolve");
        return path->sound_attenuation_linear.value *
               ((route.audio_volume_linear.value * expected_delayed[cylinder]) /
                source.cylinder_count_divisor.value) *
               (1.0 / (route.exhaust_system_length_m.value *
                       route.exhaust_system_length_m.value));
    };
    const std::array<double, 3> terms{route_term(0U), route_term(1U), route_term(2U)};
    double authored_fold = +0.0;
    authored_fold += terms[0];
    authored_fold += terms[2];
    authored_fold += terms[1];
    double capture_order_fold = +0.0;
    capture_order_fold += terms[0];
    capture_order_fold += terms[1];
    capture_order_fold += terms[2];
    expect(bits(authored_fold) != bits(capture_order_fold),
           "collector-order fixture is not sensitive to reassociation");
    expect(authored_fold != 0.0,
           "authored collector-order fixture did not retain its quiet lane");
    expect_same_bits(actual_0.route_bus_values[arrival_frame], authored_fold,
                     "collector did not use the authored serial cylinder order");
    expect(session.next_frame_index() == 2U * kFrames &&
               session.published_block_count() == 2U && !session.faulted(),
           "dynamic 3-cylinder/1-route session progress changed");
}

void test_independent_sessions_are_bit_deterministic(
    const EngineSpec &engine, const RenderScenario &scenario) {
    SyntheticCaptureBlock block_0{engine, 0U};
    SyntheticCaptureBlock block_1{engine, kFrames};
    block_0.fill_distinct_excitation();
    block_1.fill_distinct_excitation();

    auto first = require_session(compile_fixture_session(engine, scenario));
    auto second = require_session(compile_fixture_session(engine, scenario));
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

void test_complete_prevalidation_is_terminal_and_does_not_advance(
    const EngineSpec &engine, const RenderScenario &scenario) {
    SyntheticCaptureBlock malformed{engine, 0U};
    malformed.fill_distinct_excitation();
    malformed.parity_cylinders().back().dynamic_pressure_reverse_pa =
        std::numeric_limits<double>::quiet_NaN();

    auto session = require_session(compile_fixture_session(engine, scenario));
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

void test_consumer_rejection_and_exception_are_terminal(
    const EngineSpec &engine, const RenderScenario &scenario) {
    SyntheticCaptureBlock block{engine, 0U};
    block.fill_distinct_excitation();

    {
        auto session = require_session(compile_fixture_session(engine, scenario));
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
        auto session = require_session(compile_fixture_session(engine, scenario));
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

void test_reentrant_callback_preserves_outer_views_and_faults(
    const EngineSpec &engine, const RenderScenario &scenario) {
    SyntheticCaptureBlock block{engine, 0U};
    block.fill_distinct_excitation();
    auto session = require_session(compile_fixture_session(engine, scenario));

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

void expect_compile_rejected(EngineSpec engine, RenderScenario scenario,
                             std::string_view mutation) {
    const auto result = compile_fixture_session(engine, scenario);
    const auto *report = std::get_if<ValidationReport>(&result);
    expect(report != nullptr && !report->ok(),
           std::string{mutation} + " was admitted by the excitation compiler");
}

void test_compile_rejects_method_profile_rate_and_layout_drift(
    const EngineSpec &canonical_engine, const RenderScenario &canonical_scenario) {
    {
        auto engine = canonical_engine;
        engine.methods.excitation.value.configuration_sha256.bytes[0] ^= 0x01U;
        expect_compile_rejected(std::move(engine), canonical_scenario,
                                "drifted excitation method configuration");
    }
    {
        auto engine = canonical_engine;
        test::low_order_core(engine).excitation.cylinder_count_divisor.value = 5.0;
        expect_compile_rejected(std::move(engine), canonical_scenario,
                                "drifted excitation profile");
    }
    {
        auto scenario = canonical_scenario;
        scenario.rates.capture = {9999, 1};
        expect_compile_rejected(canonical_engine, std::move(scenario),
                                "capture rate divergent from physics");
    }
    {
        auto engine = canonical_engine;
        std::swap(engine.cylinders[0], engine.cylinders[1]);
        expect_compile_rejected(std::move(engine), canonical_scenario,
                                "drifted same-shape cylinder layout");
    }
}

void run_tests(const std::filesystem::path &repository_root) {
    const auto fixture = test::load_canonical_authored_engine_fixture(repository_root);
    test_exact_arithmetic_delay_routes_and_continuity(fixture.engine,
                                                      fixture.scenario);
    test_canonical_delay_is_derived_at_session_admission(fixture.engine,
                                                          fixture.scenario);
    test_three_cylinder_authored_order_collector(fixture.engine, fixture.scenario);
    test_independent_sessions_are_bit_deterministic(fixture.engine,
                                                    fixture.scenario);
    test_complete_prevalidation_is_terminal_and_does_not_advance(
        fixture.engine, fixture.scenario);
    test_consumer_rejection_and_exception_are_terminal(fixture.engine,
                                                       fixture.scenario);
    test_reentrant_callback_preserves_outer_views_and_faults(fixture.engine,
                                                             fixture.scenario);
    test_compile_rejects_method_profile_rate_and_layout_drift(fixture.engine,
                                                              fixture.scenario);
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "usage: legacy_low_order_exhaust_excitation_test "
                          "<repository-root>");
        run_tests(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "legacy low-order exhaust excitation test failed: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
