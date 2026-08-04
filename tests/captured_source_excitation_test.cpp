#include "authored_engine_fixture_support.hpp"
#include "excitation/captured_source_excitation.hpp"

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
#include <numbers>
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
constexpr std::size_t kTotalDelayFrames = 360U;
constexpr std::size_t kPrimaryDelayFrames = 0U;
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

[[nodiscard]] CapturedSourceExcitationCompileResult
compile_fixture_session(const EngineSpec &engine, const RenderScenario &scenario) {
    return compile_captured_source_excitation_session(
        engine, test::low_order_core(engine), scenario);
}

[[nodiscard]] CapturedSourceExcitationSession
require_session(CapturedSourceExcitationCompileResult result) {
    if (const auto *report = std::get_if<ValidationReport>(&result)) {
        std::string message = "canonical BMW excitation request was rejected";
        for (const auto &issue : report->issues) {
            message += "\n  " + issue.path + ": " + issue.message;
        }
        throw std::runtime_error{message};
    }
    return std::get<CapturedSourceExcitationSession>(std::move(result));
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
                auto &pressure = cylinder_samples_[frame * cylinder_count + cylinder];
                pressure.validity =
                    capture_validity_mask(CaptureValidity::thermodynamic_state);
                pressure.pressure_pa_abs =
                    kAtmospherePa + 100.0 * static_cast<double>(signed_pattern);
                pressure.temperature_k = 400.0;
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

    void set_cylinder_pressure(std::size_t frame, std::size_t cylinder,
                               double pressure_pa_abs) {
        cylinder_samples_.at(frame * cylinders_.size() + cylinder).pressure_pa_abs =
            pressure_pa_abs;
    }

    [[nodiscard]] double cylinder_pressure(std::size_t frame,
                                           std::size_t cylinder) const {
        return cylinder_samples_.at(frame * cylinders_.size() + cylinder)
            .pressure_pa_abs;
    }

    void set_intake_pressure(RouteId route_id, std::size_t frame,
                             double pressure_pa_abs) {
        const auto route = std::ranges::find(routes_, route_id, &RouteIdentity::id);
        expect(route != routes_.end() && route->kind == SourceRouteKind::intake_inlet &&
                   frame < kFrames,
               "intake pressure fixture route or frame is invalid");
        const auto route_index = static_cast<std::size_t>(route - routes_.begin());
        auto *sample = std::get_if<GasSourceRouteCaptureSample>(
            &route_samples_[frame * routes_.size() + route_index]);
        expect(sample != nullptr,
               "intake pressure fixture did not own a gas-source sample");
        sample->pressure_pa_abs = pressure_pa_abs;
    }

    [[nodiscard]] double intake_pressure(RouteId route_id, std::size_t frame) const {
        const auto route = std::ranges::find(routes_, route_id, &RouteIdentity::id);
        expect(route != routes_.end() && route->kind == SourceRouteKind::intake_inlet &&
                   frame < kFrames,
               "intake pressure fixture route or frame is invalid");
        const auto route_index = static_cast<std::size_t>(route - routes_.begin());
        const auto *sample = std::get_if<GasSourceRouteCaptureSample>(
            &route_samples_[frame * routes_.size() + route_index]);
        expect(sample != nullptr,
               "intake pressure fixture did not own a gas-source sample");
        return sample->pressure_pa_abs;
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
    std::vector<RouteId> intake_route_ids;
    std::vector<double> intake_pressure_pa_abs;
    std::uintptr_t intake_storage_address = 0U;
    std::vector<double> axial_pressure_force_n;
    std::uintptr_t pressure_force_storage_address = 0U;
};

[[nodiscard]] PublishedBlockCopy copy_callback_views(
    const presentation::ExhaustExcitationBlockView &output,
    const IntakePressureBlockView &intake,
    const ExhaustExcitationDiagnosticBlockView &diagnostic,
    const CylinderAxialPressureForceDiagnosticBlockView &pressure_force) {
    expect(output.first_frame_index() == diagnostic.first_frame_index() &&
               output.sample_rate() == diagnostic.sample_rate() &&
               output.frame_count() == diagnostic.frame_count() &&
               output.route_count() == diagnostic.route_count() &&
               std::ranges::equal(output.route_ids(), diagnostic.route_ids()),
           "presentation and diagnostic callback metadata diverged");
    expect(intake.first_frame_index() == output.first_frame_index() &&
               intake.sample_rate() == output.sample_rate() &&
               intake.sample_rate() == kCapturedSourceRateHz &&
               intake.frame_count() == output.frame_count() &&
               intake.route_count() == intake.route_ids().size() &&
               intake.pressure_pa_abs().size() ==
                   intake.frame_count() * intake.route_count(),
           "intake pressure callback metadata or frame-major extent diverged");
    expect(output.values_engine_sim_source_unit().data() ==
                   diagnostic.route_bus_values_engine_sim_source_unit().data() &&
               output.values_engine_sim_source_unit().size() ==
                   diagnostic.route_bus_values_engine_sim_source_unit().size(),
           "diagnostics did not expose the exact published route-value storage");
    expect(pressure_force.first_frame_index() == output.first_frame_index() &&
               pressure_force.sample_rate() == output.sample_rate() &&
               pressure_force.sample_rate() == kCapturedSourceRateHz &&
               pressure_force.frame_count() == output.frame_count() &&
               std::ranges::equal(pressure_force.cylinder_ids(),
                                  diagnostic.cylinder_ids()) &&
               pressure_force.force_n().size() ==
                   pressure_force.frame_count() * pressure_force.cylinder_count(),
           "axial pressure-force metadata, order, or frame-major extent diverged");
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
        const auto frame_pressure = intake.frame_pressure_pa_abs(frame);
        expect(frame_pressure.size() == intake.route_count(),
               "indexed intake pressure frame has the wrong route extent");
        if (intake.route_count() != 0U) {
            expect(frame_pressure.data() ==
                       intake.pressure_pa_abs().data() + frame * intake.route_count(),
                   "indexed intake pressure frame does not address flat storage");
        }
        for (std::size_t route = 0; route < intake.route_count(); ++route) {
            expect_same_bits(
                intake.pressure_pa_abs(frame, route),
                intake.pressure_pa_abs()[frame * intake.route_count() + route],
                "indexed intake pressure does not match flat frame-major storage");
        }
        const auto frame_force = pressure_force.frame_force_n(frame);
        expect(frame_force.size() == pressure_force.cylinder_count() &&
                   frame_force.data() == pressure_force.force_n().data() +
                                             frame * pressure_force.cylinder_count(),
               "indexed axial pressure force does not address flat storage");
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
        {intake.route_ids().begin(), intake.route_ids().end()},
        {intake.pressure_pa_abs().begin(), intake.pressure_pa_abs().end()},
        reinterpret_cast<std::uintptr_t>(intake.pressure_pa_abs().data()),
        {pressure_force.force_n().begin(), pressure_force.force_n().end()},
        reinterpret_cast<std::uintptr_t>(pressure_force.force_n().data()),
    };
}

[[nodiscard]] PublishedBlockCopy publish(CapturedSourceExcitationSession &session,
                                         const CaptureBlockView &input,
                                         std::uint64_t expected_block_ordinal) {
    std::optional<PublishedBlockCopy> copy;
    const auto result = session.process_block(
        input,
        [&](const presentation::ExhaustExcitationBlockView &output,
            const IntakePressureBlockView &intake,
            const ExhaustExcitationDiagnosticBlockView &diagnostic,
            const CylinderAxialPressureForceDiagnosticBlockView &pressure_force) {
            copy = copy_callback_views(output, intake, diagnostic, pressure_force);
            return true;
        });
    const auto *published = std::get_if<CapturedSourceBlockPublished>(&result);
    expect(published != nullptr && copy.has_value(),
           "valid synthetic capture did not publish one excitation block");
    expect(*published ==
               CapturedSourceBlockPublished{
                   expected_block_ordinal,
                   input.clock().first_sample_index,
                   input.frame_count(),
                   input.clock().first_sample_index + input.frame_count(),
               },
           "excitation publication progress changed");
    return std::move(*copy);
}

[[nodiscard]] const FailureContext &
require_fault(const CapturedSourceProcessResult &result, std::string_view code,
              std::string_view message);

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

struct IntakeRouteFixture {
    EngineSpec engine;
    RouteId intake_route_id;
};

[[nodiscard]] IntakeRouteFixture
make_interleaved_intake_route_engine(EngineSpec engine) {
    expect(engine.routes.size() >= 2U,
           "canonical excitation fixture has fewer than two exhaust routes");
    const auto &core = test::low_order_core(engine);
    expect(!core.gas_path.intakes.empty(),
           "canonical excitation fixture has no intake plenum topology");

    const auto maximum_route =
        std::ranges::max_element(engine.routes, {}, &RouteSpec::id);
    expect(maximum_route != engine.routes.end(),
           "canonical excitation fixture has no route identity");
    auto intake = engine.routes.front();
    intake.id = RouteId{maximum_route->id.value + 1U};
    intake.semantic_id.value = "intake.capture.test";
    intake.kind.value = SourceRouteKind::intake_inlet;
    intake.source_volume_id = core.gas_path.intakes.front().topology.plenum_volume_id;
    intake.default_parent_route_id.reset();
    intake.emitter_anchor_id.reset();
    const auto intake_route_id = intake.id;
    engine.routes.insert(engine.routes.begin() + 1, std::move(intake));
    return {std::move(engine), intake_route_id};
}

void expect_equal_block(const PublishedBlockCopy &actual,
                        const PublishedBlockCopy &expected, std::string_view message) {
    expect(actual.first_frame_index == expected.first_frame_index &&
               actual.sample_rate == expected.sample_rate &&
               actual.frame_count == expected.frame_count &&
               actual.cylinder_ids == expected.cylinder_ids &&
               actual.route_ids == expected.route_ids &&
               actual.intake_route_ids == expected.intake_route_ids &&
               actual.pre_delay.size() == expected.pre_delay.size() &&
               actual.post_delay.size() == expected.post_delay.size() &&
               actual.route_bus_values.size() == expected.route_bus_values.size() &&
               actual.intake_pressure_pa_abs.size() ==
                   expected.intake_pressure_pa_abs.size() &&
               actual.axial_pressure_force_n.size() ==
                   expected.axial_pressure_force_n.size(),
           std::string{message} + ": shape or metadata mismatch");
    for (std::size_t index = 0; index < actual.pre_delay.size(); ++index) {
        expect_same_bits(actual.pre_delay[index], expected.pre_delay[index], message);
        expect_same_bits(actual.post_delay[index], expected.post_delay[index], message);
    }
    for (std::size_t index = 0; index < actual.route_bus_values.size(); ++index) {
        expect_same_bits(actual.route_bus_values[index],
                         expected.route_bus_values[index], message);
    }
    for (std::size_t index = 0; index < actual.intake_pressure_pa_abs.size(); ++index) {
        expect_same_bits(actual.intake_pressure_pa_abs[index],
                         expected.intake_pressure_pa_abs[index], message);
    }
    for (std::size_t index = 0; index < actual.axial_pressure_force_n.size(); ++index) {
        expect_same_bits(actual.axial_pressure_force_n[index],
                         expected.axial_pressure_force_n[index], message);
    }
}

void expect_equal_exhaust(const PublishedBlockCopy &actual,
                          const PublishedBlockCopy &expected,
                          std::string_view message) {
    expect(actual.first_frame_index == expected.first_frame_index &&
               actual.sample_rate == expected.sample_rate &&
               actual.frame_count == expected.frame_count &&
               actual.cylinder_ids == expected.cylinder_ids &&
               actual.route_ids == expected.route_ids &&
               actual.pre_delay.size() == expected.pre_delay.size() &&
               actual.post_delay.size() == expected.post_delay.size() &&
               actual.route_bus_values.size() == expected.route_bus_values.size(),
           std::string{message} + ": exhaust shape or metadata mismatch");
    for (std::size_t index = 0; index < actual.pre_delay.size(); ++index) {
        expect_same_bits(actual.pre_delay[index], expected.pre_delay[index], message);
        expect_same_bits(actual.post_delay[index], expected.post_delay[index], message);
    }
    for (std::size_t index = 0; index < actual.route_bus_values.size(); ++index) {
        expect_same_bits(actual.route_bus_values[index],
                         expected.route_bus_values[index], message);
    }
}

void test_exact_arithmetic_delay_routes_and_continuity(const EngineSpec &engine,
                                                       const RenderScenario &scenario) {
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
               actual_0.axial_pressure_force_n.size() == kFrames * kCylinders &&
               actual_1.axial_pressure_force_n.size() == kFrames * kCylinders &&
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
    const std::array<const SyntheticCaptureBlock *, 2> capture_blocks{&block_0,
                                                                      &block_1};
    for (std::size_t block = 0; block < actual_blocks.size(); ++block) {
        const auto &actual = *actual_blocks[block];
        const auto &capture = *capture_blocks[block];
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
                    all_pre[(global - kPrimaryDelayFrames) * kCylinders + cylinder];
                expect_same_bits(actual.post_delay[local], expected_post,
                                 "primary-delay lane or block continuity changed");

                const double bore_m = engine.cylinders[cylinder].bore_m.value;
                const double piston_crown_area_m2 =
                    (std::numbers::pi_v<double> * (bore_m * bore_m)) / 4.0;
                const double expected_force_n =
                    piston_crown_area_m2 * (capture.cylinder_pressure(frame, cylinder) -
                                            scenario.crankcase.pressure_pa_abs.value);
                expect_same_bits(actual.axial_pressure_force_n[local], expected_force_n,
                                 "axial piston-crown pressure force changed");

                const bool even_cylinder_id = ((cylinder + 1U) % 2U) == 0U;
                const std::size_t route = even_cylinder_id ? 0U : 1U;
                const double volume = even_cylinder_id ? 0.5 : 1.0;
                const double expected_arrival =
                    global < kTotalDelayFrames
                        ? +0.0
                        : all_pre[(global - kTotalDelayFrames) * kCylinders + cylinder];
                expected_buses[route] +=
                    independent_route_term(expected_arrival, volume);
            }
            for (std::size_t route = 0; route < kRoutes; ++route) {
                expect_same_bits(
                    actual.route_bus_values[frame * kRoutes + route],
                    expected_buses[route],
                    "stable cylinder accumulation, divisor, length, or route changed");
                if (global < kTotalDelayFrames) {
                    expect(bits(actual.route_bus_values[frame * kRoutes + route]) ==
                               bits(+0.0),
                           "route-delay startup did not emit canonical positive zero");
                }
            }
        }
    }

    expect(actual_0.pressure_force_storage_address != 0U &&
               actual_0.pressure_force_storage_address ==
                   actual_1.pressure_force_storage_address,
           "axial pressure-force scratch was not preallocated and reused");

    expect(session.next_frame_index() == 2U * kFrames &&
               session.published_block_count() == 2U && !session.faulted(),
           "two-block excitation session progress changed");
}

void test_pressure_force_uses_each_cylinder_bore(
    const EngineSpec &canonical_engine, const RenderScenario &scenario) {
    constexpr std::size_t distinct_cylinder = 3U;
    constexpr std::size_t observed_frame = 0U;
    constexpr double common_pressure_pa_abs = kAtmospherePa + 1000.0;

    auto engine = canonical_engine;
    engine.cylinders[distinct_cylinder].bore_m.value *= 1.25;
    SyntheticCaptureBlock block{engine, 0U};
    block.fill_distinct_excitation();
    for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
        block.set_cylinder_pressure(observed_frame, cylinder,
                                    common_pressure_pa_abs);
    }

    auto session = require_session(compile_fixture_session(engine, scenario));
    const auto output = publish(session, block.view(), 0U);
    const double pressure_delta_pa =
        common_pressure_pa_abs - scenario.crankcase.pressure_pa_abs.value;
    const double common_bore_m = engine.cylinders.front().bore_m.value;
    const double common_force_n =
        std::numbers::pi_v<double> * common_bore_m * common_bore_m * 0.25 *
        pressure_delta_pa;
    for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
        const double bore_m = engine.cylinders[cylinder].bore_m.value;
        const double expected_force_n =
            std::numbers::pi_v<double> * bore_m * bore_m * 0.25 *
            pressure_delta_pa;
        const double actual_force_n =
            output.axial_pressure_force_n[observed_frame * kCylinders + cylinder];
        expect_same_bits(actual_force_n, expected_force_n,
                         "pressure force used another cylinder's bore");
        expect((cylinder == distinct_cylinder) ==
                   (bits(actual_force_n) != bits(common_force_n)),
               "distinct piston crown area did not remain on its cylinder lane");
    }
}

void test_intake_pressure_is_exact_without_changing_exhaust(
    const EngineSpec &canonical_engine, const RenderScenario &scenario) {
    auto intake_fixture = make_interleaved_intake_route_engine(canonical_engine);
    const auto intake_route_id = intake_fixture.intake_route_id;

    SyntheticCaptureBlock baseline_0{canonical_engine, 0U};
    SyntheticCaptureBlock baseline_1{canonical_engine, kFrames};
    SyntheticCaptureBlock intake_0{intake_fixture.engine, 0U};
    SyntheticCaptureBlock intake_1{intake_fixture.engine, kFrames};
    baseline_0.fill_distinct_excitation();
    baseline_1.fill_distinct_excitation();
    intake_0.fill_distinct_excitation();
    intake_1.fill_distinct_excitation();
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        intake_0.set_intake_pressure(intake_route_id, frame,
                                     87321.25 + static_cast<double>(frame) * 0.125);
        intake_1.set_intake_pressure(intake_route_id, frame,
                                     87321.25 +
                                         static_cast<double>(kFrames + frame) * 0.125);
    }

    auto baseline_session =
        require_session(compile_fixture_session(canonical_engine, scenario));
    auto intake_session =
        require_session(compile_fixture_session(intake_fixture.engine, scenario));
    const auto baseline_output_0 = publish(baseline_session, baseline_0.view(), 0U);
    const auto intake_output_0 = publish(intake_session, intake_0.view(), 0U);
    const auto baseline_output_1 = publish(baseline_session, baseline_1.view(), 1U);
    const auto intake_output_1 = publish(intake_session, intake_1.view(), 1U);

    expect_equal_exhaust(intake_output_0, baseline_output_0,
                         "interleaved intake changed exhaust block zero");
    expect_equal_exhaust(intake_output_1, baseline_output_1,
                         "interleaved intake changed exhaust block one");
    expect(baseline_output_0.intake_route_ids.empty() &&
               baseline_output_0.intake_pressure_pa_abs.empty() &&
               baseline_output_1.intake_route_ids.empty() &&
               baseline_output_1.intake_pressure_pa_abs.empty(),
           "exhaust-only layout did not publish a valid empty intake view");
    expect(intake_output_0.intake_route_ids == std::vector<RouteId>{intake_route_id} &&
               intake_output_1.intake_route_ids ==
                   std::vector<RouteId>{intake_route_id} &&
               intake_output_0.intake_pressure_pa_abs.size() == kFrames &&
               intake_output_1.intake_pressure_pa_abs.size() == kFrames,
           "intake view did not retain filtered authored route order or extent");
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        expect_same_bits(intake_output_0.intake_pressure_pa_abs[frame],
                         intake_0.intake_pressure(intake_route_id, frame),
                         "block-zero intake pressure was transformed");
        expect_same_bits(intake_output_1.intake_pressure_pa_abs[frame],
                         intake_1.intake_pressure(intake_route_id, frame),
                         "block-one intake pressure was transformed");
    }
    expect(intake_output_0.intake_storage_address != 0U &&
               intake_output_0.intake_storage_address ==
                   intake_output_1.intake_storage_address,
           "intake pressure callback scratch was not preallocated and reused");

    constexpr std::size_t mutated_frame = kFrames - 1U;
    constexpr std::size_t mutated_cylinder = 2U;
    intake_0.set_cylinder_pressure(
        mutated_frame, mutated_cylinder,
        intake_0.cylinder_pressure(mutated_frame, mutated_cylinder) + 12345.0);
    auto pressure_session =
        require_session(compile_fixture_session(intake_fixture.engine, scenario));
    const auto pressure_output = publish(pressure_session, intake_0.view(), 0U);
    expect_equal_exhaust(pressure_output, intake_output_0,
                         "cylinder-pressure-only mutation changed exhaust");
    expect(pressure_output.intake_route_ids == intake_output_0.intake_route_ids &&
               pressure_output.intake_pressure_pa_abs.size() ==
                   intake_output_0.intake_pressure_pa_abs.size(),
           "cylinder-pressure-only mutation changed intake shape");
    for (std::size_t index = 0; index < pressure_output.intake_pressure_pa_abs.size();
         ++index) {
        expect_same_bits(pressure_output.intake_pressure_pa_abs[index],
                         intake_output_0.intake_pressure_pa_abs[index],
                         "cylinder-pressure-only mutation changed intake values");
    }
    std::size_t changed_force_count = 0U;
    for (std::size_t index = 0; index < pressure_output.axial_pressure_force_n.size();
         ++index) {
        changed_force_count += bits(pressure_output.axial_pressure_force_n[index]) !=
                               bits(intake_output_0.axial_pressure_force_n[index]);
    }
    expect(
        changed_force_count == 1U &&
            bits(pressure_output.axial_pressure_force_n[mutated_frame * kCylinders +
                                                        mutated_cylinder]) !=
                bits(intake_output_0.axial_pressure_force_n[mutated_frame * kCylinders +
                                                            mutated_cylinder]),
        "cylinder-pressure-only mutation did not remain isolated to one force "
        "lane");

    SyntheticCaptureBlock malformed{intake_fixture.engine, 0U};
    malformed.fill_distinct_excitation();
    malformed.set_intake_pressure(intake_route_id, kFrames - 1U,
                                  std::numeric_limits<double>::quiet_NaN());
    auto malformed_session =
        require_session(compile_fixture_session(intake_fixture.engine, scenario));
    std::size_t callbacks = 0U;
    const auto rejected = malformed_session.process_block(
        malformed.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const IntakePressureBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &,
                              const CylinderAxialPressureForceDiagnosticBlockView &) {
            ++callbacks;
            return true;
        });
    (void)require_fault(rejected, "captured-source-block-invalid",
                        "non-finite intake pressure was admitted");
    expect(callbacks == 0U && malformed_session.faulted() &&
               malformed_session.next_frame_index() == 0U &&
               malformed_session.published_block_count() == 0U,
           "invalid intake pressure entered callback or advanced session progress");
}

void test_split_primary_and_route_delays_preserve_total_arrival(
    const EngineSpec &canonical_engine, RenderScenario scenario) {
    constexpr RationalRateHz rate = kRate;
    constexpr double added_primary_length_m = 0.35329;
    constexpr std::size_t expected_primary_delay_frames = 20U;
    constexpr std::size_t expected_route_delay_frames = 360U;
    constexpr std::size_t expected_total_delay_frames = 380U;
    constexpr std::size_t independently_rounded_primary_frames = 21U;
    static_assert(expected_primary_delay_frames + expected_route_delay_frames ==
                  expected_total_delay_frames);
    const auto rounded_samples = [&](const double length_m) {
        return static_cast<std::size_t>(
            std::round((length_m / 343.0) * static_cast<double>(rate.numerator) /
                       static_cast<double>(rate.denominator)));
    };
    expect(rounded_samples(kTotalAudioLengthM) == expected_route_delay_frames &&
               rounded_samples(kTotalAudioLengthM + added_primary_length_m) ==
                   expected_total_delay_frames &&
               rounded_samples(added_primary_length_m) ==
                   independently_rounded_primary_frames &&
               independently_rounded_primary_frames != expected_primary_delay_frames,
           "primary/route rounding fixture is not sensitive to total-delay factoring");

    auto engine = canonical_engine;
    auto &core = test::low_order_core(engine);
    for (auto &path : core.excitation.cylinder_paths) {
        path.header_primary_length_m.value += added_primary_length_m;
    }
    for (auto &cylinder : core.mechanism.cylinders) {
        cylinder.parameters.header_primary_length_m.value += added_primary_length_m;
    }

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
    for (std::size_t frame = 0; frame < expected_primary_delay_frames; ++frame) {
        for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
            expect(bits(actual_0.post_delay[frame * kCylinders + cylinder]) ==
                       bits(+0.0),
                   "primary propagation arrived before its residual delay");
        }
    }
    for (std::size_t frame = 0; frame < expected_total_delay_frames; ++frame) {
        for (std::size_t route = 0; route < kRoutes; ++route) {
            expect(bits(actual_0.route_bus_values[frame * kRoutes + route]) ==
                       bits(+0.0),
                   "collector output arrived before its preserved total delay");
        }
    }

    bool observed_nonzero_arrival = false;
    std::array<double, kRoutes> expected_buses{};
    for (std::size_t cylinder = 0; cylinder < kCylinders; ++cylinder) {
        const double expected = actual_0.pre_delay[cylinder];
        const double actual =
            actual_0.post_delay[expected_primary_delay_frames * kCylinders + cylinder];
        expect_same_bits(actual, expected,
                         "primary residual changed its cylinder-lane arrival");
        observed_nonzero_arrival = observed_nonzero_arrival || actual != 0.0;
        const bool even_cylinder_id = ((cylinder + 1U) % 2U) == 0U;
        const std::size_t route = even_cylinder_id ? 0U : 1U;
        const double volume = even_cylinder_id ? 0.5 : 1.0;
        expected_buses[route] += independent_route_term(expected, volume);
    }
    for (std::size_t route = 0; route < kRoutes; ++route) {
        expect_same_bits(
            actual_0.route_bus_values[expected_total_delay_frames * kRoutes + route],
            expected_buses[route],
            "primary plus route delay did not preserve the total arrival sample");
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
    constexpr std::size_t lane_frame = kPrimaryDelayFrames;
    constexpr std::size_t arrival_frame = kTotalDelayFrames;
    constexpr std::array<double, 3> expected_delayed{1600.0, 1600.0, -1600.0};
    for (std::size_t cylinder = 0; cylinder < expected_delayed.size(); ++cylinder) {
        expect_same_bits(actual_0.post_delay[lane_frame * 3U + cylinder],
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

void test_independent_sessions_are_bit_deterministic(const EngineSpec &engine,
                                                     const RenderScenario &scenario) {
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
require_fault(const CapturedSourceProcessResult &result, std::string_view code,
              std::string_view message) {
    const auto *failure = std::get_if<FailureContext>(&result);
    expect(failure != nullptr && failure->detail_code == code, std::string{message});
    return *failure;
}

void test_complete_prevalidation_is_terminal_and_does_not_advance(
    const EngineSpec &engine, const RenderScenario &scenario) {
    const auto prove_terminal = [&](SyntheticCaptureBlock &malformed, auto repair,
                                    std::string_view label) {
        auto session = require_session(compile_fixture_session(engine, scenario));
        std::size_t callbacks = 0U;
        const auto first = session.process_block(
            malformed.view(), [&](const presentation::ExhaustExcitationBlockView &,
                                  const IntakePressureBlockView &,
                                  const ExhaustExcitationDiagnosticBlockView &,
                                  const CylinderAxialPressureForceDiagnosticBlockView &) {
                ++callbacks;
                return true;
            });
        const FailureContext first_fault = require_fault(
            first, "captured-source-block-invalid",
            std::string{label} + " was not rejected by full prevalidation");
        expect(callbacks == 0U && session.faulted() &&
                   session.next_frame_index() == 0U &&
                   session.published_block_count() == 0U,
               std::string{label} +
                   " entered callback, advanced delay, or published");

        repair();
        const auto repeated = session.process_block(
            malformed.view(), [&](const presentation::ExhaustExcitationBlockView &,
                                  const IntakePressureBlockView &,
                                  const ExhaustExcitationDiagnosticBlockView &,
                                  const CylinderAxialPressureForceDiagnosticBlockView &) {
                ++callbacks;
                return true;
            });
        const auto &repeated_fault = require_fault(
            repeated, first_fault.detail_code,
            std::string{label} + " fault did not remain terminal after repair");
        expect(repeated_fault == first_fault && callbacks == 0U &&
                   session.next_frame_index() == 0U &&
                   session.published_block_count() == 0U,
               std::string{label} +
                   " failure was not stable and state-preserving");
    };

    {
        SyntheticCaptureBlock malformed{engine, 0U};
        malformed.fill_distinct_excitation();
        const double valid_final_pressure =
            malformed.cylinder_pressure(kFrames - 1U, kCylinders - 1U);
        malformed.set_cylinder_pressure(kFrames - 1U, kCylinders - 1U,
                                        std::numeric_limits<double>::quiet_NaN());
        prove_terminal(malformed,
                       [&] {
                           malformed.set_cylinder_pressure(
                               kFrames - 1U, kCylinders - 1U,
                               valid_final_pressure);
                       },
                       "invalid cylinder pressure");
    }
    {
        SyntheticCaptureBlock malformed{engine, 0U};
        malformed.fill_distinct_excitation();
        malformed.parity_cylinders().back().dynamic_pressure_reverse_pa =
            std::numeric_limits<double>::quiet_NaN();
        prove_terminal(malformed,
                       [&] {
                           malformed.parity_cylinders()
                               .back()
                               .dynamic_pressure_reverse_pa = 1.0;
                       },
                       "invalid exhaust reference parity");
    }
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
                              const IntakePressureBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &,
                              const CylinderAxialPressureForceDiagnosticBlockView &) {
                ++callbacks;
                return false;
            });
        const FailureContext fault =
            require_fault(rejected, "captured-source-consumer-rejected",
                          "false excitation consumer did not reject publication");
        const auto repeated = session.process_block(
            block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const IntakePressureBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &,
                              const CylinderAxialPressureForceDiagnosticBlockView &) {
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
                const IntakePressureBlockView &,
                const ExhaustExcitationDiagnosticBlockView &,
                const CylinderAxialPressureForceDiagnosticBlockView &) -> bool {
                ++callbacks;
                throw std::runtime_error{"intentional excitation consumer failure"};
            });
        const FailureContext fault =
            require_fault(thrown, "captured-source-consumer-threw",
                          "excitation consumer exception escaped its transaction");
        const auto repeated = session.process_block(
            block.view(), [&](const presentation::ExhaustExcitationBlockView &,
                              const IntakePressureBlockView &,
                              const ExhaustExcitationDiagnosticBlockView &,
                              const CylinderAxialPressureForceDiagnosticBlockView &) {
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
    auto intake_fixture = make_interleaved_intake_route_engine(engine);
    SyntheticCaptureBlock block{intake_fixture.engine, 0U};
    block.fill_distinct_excitation();
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
        block.set_intake_pressure(intake_fixture.intake_route_id, frame,
                                  90000.5 + static_cast<double>(frame));
    }
    auto session =
        require_session(compile_fixture_session(intake_fixture.engine, scenario));

    std::size_t outer_callbacks = 0U;
    std::size_t nested_callbacks = 0U;
    std::optional<FailureContext> nested_fault;
    std::exception_ptr callback_error;
    const auto outer = session.process_block(
        block.view(),
        [&](const presentation::ExhaustExcitationBlockView &output,
            const IntakePressureBlockView &intake,
            const ExhaustExcitationDiagnosticBlockView &diagnostic,
            const CylinderAxialPressureForceDiagnosticBlockView &pressure_force) {
            try {
                ++outer_callbacks;
                const auto before =
                    copy_callback_views(output, intake, diagnostic, pressure_force);
                const auto nested = session.process_block(
                    block.view(),
                    [&](const presentation::ExhaustExcitationBlockView &,
                        const IntakePressureBlockView &,
                        const ExhaustExcitationDiagnosticBlockView &,
                        const CylinderAxialPressureForceDiagnosticBlockView &) {
                        ++nested_callbacks;
                        return true;
                    });
                nested_fault =
                    require_fault(nested, "captured-source-consumer-reentrant",
                                  "nested excitation publication was not rejected");
                const auto after =
                    copy_callback_views(output, intake, diagnostic, pressure_force);
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
        require_fault(outer, "captured-source-consumer-reentrant",
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
        auto scenario = canonical_scenario;
        scenario.rates.physics = {10000, 1};
        scenario.rates.capture = {10000, 1};
        expect_compile_rejected(canonical_engine, std::move(scenario),
                                "noncanonical 10 kHz gas-source clock");
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
    test_exact_arithmetic_delay_routes_and_continuity(fixture.engine, fixture.scenario);
    test_pressure_force_uses_each_cylinder_bore(fixture.engine, fixture.scenario);
    test_intake_pressure_is_exact_without_changing_exhaust(fixture.engine,
                                                           fixture.scenario);
    test_split_primary_and_route_delays_preserve_total_arrival(fixture.engine,
                                                               fixture.scenario);
    test_three_cylinder_authored_order_collector(fixture.engine, fixture.scenario);
    test_independent_sessions_are_bit_deterministic(fixture.engine, fixture.scenario);
    test_complete_prevalidation_is_terminal_and_does_not_advance(fixture.engine,
                                                                 fixture.scenario);
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
        expect(argc == 2, "usage: captured_source_excitation_test "
                          "<repository-root>");
        run_tests(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "captured source excitation test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
