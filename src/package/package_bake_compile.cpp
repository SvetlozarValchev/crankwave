#include "engine_sim_offline/package_bake.hpp"

#include "compile/compiled_scenario_view.hpp"
#include "package/compiled_package_bake_storage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <new>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline {
namespace {

constexpr compile::SiRate kIsolatedComparisonRate{192000U, 1U};
constexpr double kRpmComparisonTolerance = 1.0e-9;

[[nodiscard]] authoring::Diagnostic
make_diagnostic(const authoring::DiagnosticCode code, std::string path,
                std::string message) {
    authoring::Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.json_pointer = std::move(path);
    diagnostic.message = std::move(message);
    return diagnostic;
}

void add(authoring::DiagnosticReport &report, const authoring::DiagnosticCode code,
         std::string path, std::string message) {
    report.diagnostics.push_back(
        make_diagnostic(code, std::move(path), std::move(message)));
}

[[nodiscard]] std::string source_document_path(const std::size_t source_index,
                                               const std::string_view suffix) {
    auto path = "/scenario_sources/" + std::to_string(source_index) + "/document";
    if (!suffix.empty()) {
        if (suffix.front() != '/') {
            path += '/';
        }
        path += suffix;
    }
    return path;
}

void append_prefixed(authoring::DiagnosticReport &destination,
                     authoring::DiagnosticReport source,
                     const std::size_t source_index) {
    for (auto &diagnostic : source.diagnostics) {
        diagnostic.json_pointer =
            source_document_path(source_index, diagnostic.json_pointer);
        destination.diagnostics.push_back(std::move(diagnostic));
    }
}

[[nodiscard]] std::optional<double> rpm(const authoring::Quantity &quantity) noexcept {
    if (!std::isfinite(quantity.value)) {
        return std::nullopt;
    }
    if (quantity.unit == "rpm") {
        return quantity.value;
    }
    if (quantity.unit == "rad/s") {
        return quantity.value * (60.0 / (2.0 * std::numbers::pi));
    }
    return std::nullopt;
}

[[nodiscard]] bool near(const double left, const double right) noexcept {
    return std::abs(left - right) <= kRpmComparisonTolerance;
}

[[nodiscard]] std::optional<std::size_t>
authored_source_index(const authoring::PackageBakeDocument &document,
                      const std::string_view id) noexcept {
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        if (document.scenario_sources[index].id.value == id) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] const PackageBakeScenarioInputView *
scenario_input(const std::span<const PackageBakeScenarioInputView> inputs,
               const std::string_view id) noexcept {
    const auto found =
        std::ranges::find(inputs, id, &PackageBakeScenarioInputView::source_id);
    return found == inputs.end() ? nullptr : &*found;
}

void validate_source_graph(const authoring::PackageBakeDocument &document,
                           const std::span<const PackageBakeScenarioInputView> inputs,
                           authoring::DiagnosticReport &report) {
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        std::size_t authored_count = 0;
        for (const auto &candidate : document.scenario_sources) {
            authored_count += candidate.id.value == source.id.value ? 1U : 0U;
        }
        if (authored_count != 1U) {
            add(report, authoring::DiagnosticCode::duplicate_id,
                "/scenario_sources/" + std::to_string(index) + "/id",
                "package-bake scenario-source identities must be unique");
        }

        std::size_t input_count = 0;
        for (const auto &input : inputs) {
            input_count += input.source_id == source.id.value ? 1U : 0U;
        }
        if (input_count == 0U) {
            add(report, authoring::DiagnosticCode::missing_value,
                source_document_path(index, ""),
                "no parsed scenario document was supplied for scenario source '" +
                    source.id.value + "'");
        } else if (input_count > 1U) {
            add(report, authoring::DiagnosticCode::duplicate_id,
                source_document_path(index, ""),
                "more than one scenario document was supplied for scenario source '" +
                    source.id.value + "'");
        } else if (scenario_input(inputs, source.id.value)->document == nullptr) {
            add(report, authoring::DiagnosticCode::missing_value,
                source_document_path(index, ""),
                "scenario-source input contains a null document view");
        }
    }

    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (!authored_source_index(document, inputs[index].source_id).has_value()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                "/scenario_inputs/" + std::to_string(index) + "/source_id",
                "scenario input '" + std::string{inputs[index].source_id} +
                    "' does not name an authored package source");
        }
    }

    std::vector<std::string_view> used_sources;
    used_sources.reserve(document.running.planes.size() + 1U);
    for (std::size_t index = 0; index < document.running.planes.size(); ++index) {
        const auto &reference = document.running.planes[index].scenario.value;
        if (!authored_source_index(document, reference).has_value()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                "/running/planes/" + std::to_string(index) + "/scenario",
                "running plane does not resolve to an authored scenario source");
        }
        if (std::ranges::find(used_sources, reference) != used_sources.end()) {
            add(report, authoring::DiagnosticCode::duplicate_id,
                "/running/planes/" + std::to_string(index) + "/scenario",
                "each authored scenario source must be used exactly once");
        }
        used_sources.push_back(reference);
    }
    const auto &idle_reference = document.running.idle.scenario.value;
    if (!authored_source_index(document, idle_reference).has_value()) {
        add(report, authoring::DiagnosticCode::dangling_reference,
            "/running/idle/scenario",
            "idle does not resolve to an authored scenario source");
    }
    if (std::ranges::find(used_sources, idle_reference) != used_sources.end()) {
        add(report, authoring::DiagnosticCode::duplicate_id, "/running/idle/scenario",
            "each authored scenario source must be used exactly once");
    }
    used_sources.push_back(idle_reference);

    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &id = document.scenario_sources[index].id.value;
        if (std::ranges::count(used_sources, std::string_view{id}) != 1) {
            add(report, authoring::DiagnosticCode::disconnected_object,
                "/scenario_sources/" + std::to_string(index) + "/id",
                "each authored scenario source must feed exactly one running lane");
        }
    }
}

void validate_stable_capture(const authoring::ScenarioDocument &document,
                             const compile::detail::CompiledScenarioInputsView inputs,
                             const std::size_t source_index,
                             authoring::DiagnosticReport &report) {
    const auto &scenario = inputs.scenario.scenario;
    if (!document.events.empty()) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            source_document_path(source_index, "/events"),
            "normal-running package sources must not change state during capture");
    }
    if (scenario.operating_state.value.size() != 1U) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/initial_state"),
            "normal-running package source must have one stable operating state");
    } else {
        const auto &state = scenario.operating_state.value.front().state;
        if (!state.ignition_enabled || !state.fuel_enabled || !state.dyno_enabled ||
            state.starter_enabled || state.limiter_enabled) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                source_document_path(source_index, "/initial_state"),
                "normal-running package source requires ignition, fuel, and dyno "
                "on with starter and limiter off");
        }
    }

    const auto *preparation =
        std::get_if<contract::FixedHorizonCycleSampling>(&scenario.preparation);
    if (preparation == nullptr ||
        !(preparation->fixed_preparation_horizon_s.value > 0.0) ||
        preparation->trailing_complete_cycle_count.value == 0U) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/preparation"),
            "normal-running package source requires positive fixed-horizon cycle "
            "preparation");
    } else if (scenario.audible_start_s.value + 1.0e-12 <
               preparation->fixed_preparation_horizon_s.value) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/audible_start"),
            "audible capture must begin after fixed-horizon preparation completes");
    }
}

void validate_audio_contract(const authoring::PackageBakeDocument &document,
                             const compile::detail::CompiledScenarioInputsView inputs,
                             const std::size_t source_index,
                             authoring::DiagnosticReport &report) {
    const auto &scenario = inputs.scenario.scenario;
    if (scenario.public_seed.value != document.public_seed) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/public_seed"),
            "scenario public seed must equal the package public seed");
    }
    if (scenario.rates.delivery.numerator != kIsolatedComparisonRate.numerator_hz ||
        scenario.rates.delivery.denominator != kIsolatedComparisonRate.denominator) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/rates/delivery"),
            "scenario delivery rate must equal the current isolated A/B package "
            "rate 192000/1 Hz");
    }
    for (std::size_t bus_index = 0; bus_index < document.audio.buses.size();
         ++bus_index) {
        const auto &required_id = document.audio.buses[bus_index].value;
        const auto found = std::ranges::find(
            inputs.scenario.request_input.selected_audio_buses, required_id,
            &compile::detail::ResolvedAudioBusDescriptor::authored_id);
        if (found == inputs.scenario.request_input.selected_audio_buses.end()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                source_document_path(source_index, "/output/buses"),
                "scenario output does not contain required package audio bus '" +
                    required_id + "'");
        }
    }
}

void validate_running_plane(const authoring::PackageBakeRunningPlane &plane,
                            const compile::detail::CompiledScenarioInputsView inputs,
                            const CompiledPackageBakeRpmRange &rpm_range,
                            const std::size_t source_index,
                            authoring::DiagnosticReport &report) {
    const auto &scenario = inputs.scenario.scenario;
    const auto *dyno = std::get_if<contract::HeldDyno>(&scenario.mode);
    if (dyno == nullptr) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            source_document_path(source_index, "/mode/type"),
            "running package planes require finite HeldDyno captures");
        return;
    }

    const auto audible_start = contract::resolve_frame_index(
        scenario.audible_start_s.value, dyno->target_engine_speed_rpm.rate);
    const auto audible_duration = contract::resolve_frame_index(
        scenario.audible_duration_s.value, dyno->target_engine_speed_rpm.rate);
    if (!audible_start.has_value() || !audible_duration.has_value() ||
        *audible_duration == 0U ||
        *audible_start > dyno->target_engine_speed_rpm.post_step_rpm.size() ||
        *audible_duration >
            dyno->target_engine_speed_rpm.post_step_rpm.size() - *audible_start) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/audible_duration"),
            "HeldDyno audible interval does not resolve inside its finite target "
            "trajectory");
        return;
    }

    const auto begin = dyno->target_engine_speed_rpm.post_step_rpm.begin() +
                       static_cast<std::ptrdiff_t>(*audible_start);
    const auto end = begin + static_cast<std::ptrdiff_t>(*audible_duration);
    constexpr double kMonotonicTolerance = 1.0e-9;
    bool monotonic = true;
    for (auto current = begin + 1; current != end; ++current) {
        if (plane.direction == authoring::PackageBakeRunningDirection::rising) {
            monotonic = monotonic && *current + kMonotonicTolerance >= *(current - 1);
        } else {
            monotonic = monotonic && *current <= *(current - 1) + kMonotonicTolerance;
        }
    }

    const auto [minimum, maximum] = std::minmax_element(begin, end);
    const bool covers =
        *minimum <= rpm_range.padded_minimum_rpm + kRpmComparisonTolerance &&
        *maximum >= rpm_range.padded_maximum_rpm - kRpmComparisonTolerance;
    if (!monotonic || !covers) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/mode/target_engine_speed"),
            "HeldDyno audible target must be monotonic in the authored direction "
            "and cover the complete padded RPM bank");
    }
}

void validate_idle(const compile::detail::CompiledScenarioInputsView inputs,
                   const CompiledPackageBakeRpmRange &rpm_range,
                   const std::size_t source_index,
                   authoring::DiagnosticReport &report) {
    const auto *held = std::get_if<contract::HeldSpeed>(&inputs.scenario.scenario.mode);
    if (held == nullptr ||
        !near(held->engine_speed_rpm.value, rpm_range.playback_minimum_rpm)) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/mode/target_engine_speed"),
            "idle package source must be HeldSpeed at the playback minimum RPM");
    }
}

} // namespace

namespace detail {

class CompiledPackageBakeBuilder final {
  public:
    [[nodiscard]] static PackageBakeCompileResult build(
        const authoring::PackageBakeDocument &document,
        const compile::CompiledEngine &engine,
        const std::span<const PackageBakeScenarioInputView> scenario_inputs) noexcept {
        try {
            authoring::DiagnosticReport report;
            if (document.schema != "engine-sim-offline/package-bake") {
                add(report, authoring::DiagnosticCode::unsupported_schema, "/schema",
                    "expected schema 'engine-sim-offline/package-bake'");
            }
            if (document.engine.value != engine.id()) {
                add(report, authoring::DiagnosticCode::inconsistent_value, "/engine",
                    "package engine identity does not match the exact compiled "
                    "engine");
            }
            const compile::SiRate package_rate{document.audio.sample_rate.numerator,
                                               document.audio.sample_rate.denominator};
            if (document.audio.sample_rate.unit != "Hz" ||
                package_rate != kIsolatedComparisonRate) {
                add(report, authoring::DiagnosticCode::unsupported_capability,
                    "/audio/sample_rate",
                    "the current isolated source/baked A/B gate requires exactly "
                    "192000/1 Hz");
            }

            const auto playback_minimum = rpm(document.running.rpm_range.minimum);
            const auto playback_maximum = rpm(document.running.rpm_range.maximum);
            if (!playback_minimum.has_value() || !playback_maximum.has_value() ||
                !(*playback_minimum > 0.0) ||
                !(*playback_maximum > *playback_minimum)) {
                add(report, authoring::DiagnosticCode::invalid_value,
                    "/running/rpm_range",
                    "running RPM range must contain finite ascending angular-speed "
                    "quantities");
            } else {
                const auto playback_intervals =
                    (*playback_maximum - *playback_minimum) /
                    kPackageBakeMethodGeometry.rpm_grid_spacing;
                if (!near(playback_intervals, std::round(playback_intervals))) {
                    add(report, authoring::DiagnosticCode::inconsistent_value,
                        "/running/rpm_range",
                        "running RPM span must close exactly on the current uniform "
                        "grid spacing");
                }
                const auto padded_minimum =
                    *playback_minimum -
                    kPackageBakeMethodGeometry.rpm_grid_spacing *
                        kPackageBakeMethodGeometry.padding_rows_per_side;
                if (!(padded_minimum > 0.0)) {
                    add(report, authoring::DiagnosticCode::out_of_range,
                        "/running/rpm_range/minimum",
                        "selector padding must retain a positive minimum RPM");
                }
            }

            validate_source_graph(document, scenario_inputs, report);
            if (report.has_errors()) {
                return report;
            }

            auto storage =
                std::make_shared<CompiledPackageBakeStorage>(CompiledPackageBakeStorage{
                    document.id.value,
                    engine,
                    document.public_seed,
                    package_rate,
                    {},
                    kPackageBakeMethodGeometry,
                    {
                        *playback_minimum,
                        *playback_maximum,
                        *playback_minimum -
                            kPackageBakeMethodGeometry.rpm_grid_spacing *
                                kPackageBakeMethodGeometry.padding_rows_per_side,
                        *playback_maximum +
                            kPackageBakeMethodGeometry.rpm_grid_spacing *
                                kPackageBakeMethodGeometry.padding_rows_per_side,
                    },
                    {},
                    {},
                    0U,
                });
            storage->audio_bus_ids.reserve(document.audio.buses.size());
            for (const auto &bus : document.audio.buses) {
                storage->audio_bus_ids.push_back(bus.value);
            }
            storage->scenario_sources.reserve(document.scenario_sources.size());

            for (std::size_t index = 0; index < document.scenario_sources.size();
                 ++index) {
                const auto &source = document.scenario_sources[index];
                const auto *input = scenario_input(scenario_inputs, source.id.value);
                auto compiled_result =
                    compile::compile_scenario(engine, *input->document);
                if (auto *diagnostics =
                        std::get_if<authoring::DiagnosticReport>(&compiled_result)) {
                    append_prefixed(report, std::move(*diagnostics), index);
                    continue;
                }
                auto scenario =
                    std::get<compile::CompiledScenario>(std::move(compiled_result));
                const auto inputs =
                    compile::detail::CompiledScenarioViewAccess::inputs(scenario);
                validate_stable_capture(*input->document, inputs, index, report);
                validate_audio_contract(document, inputs, index, report);
                storage->scenario_sources.push_back(
                    {source.id.value, source.uri, std::move(scenario)});
            }
            if (report.has_errors()) {
                return report;
            }

            storage->running_planes.reserve(document.running.planes.size());
            for (std::size_t index = 0; index < document.running.planes.size();
                 ++index) {
                const auto &plane = document.running.planes[index];
                const auto source_index =
                    *authored_source_index(document, plane.scenario.value);
                const auto &scenario = storage->scenario_sources[source_index].scenario;
                const auto inputs =
                    compile::detail::CompiledScenarioViewAccess::inputs(scenario);
                validate_running_plane(plane, inputs, storage->rpm_range, source_index,
                                       report);
                storage->running_planes.push_back(
                    {plane.id.value, plane.load_coordinate, plane.direction,
                     source_index, scenario});
            }

            storage->idle_scenario_source_index =
                *authored_source_index(document, document.running.idle.scenario.value);
            validate_idle(
                compile::detail::CompiledScenarioViewAccess::inputs(
                    storage->scenario_sources[storage->idle_scenario_source_index]
                        .scenario),
                storage->rpm_range, storage->idle_scenario_source_index, report);
            if (report.has_errors()) {
                return report;
            }
            return CompiledPackageBake{std::move(storage)};
        } catch (const std::bad_alloc &) {
            return authoring::DiagnosticReport{{make_diagnostic(
                authoring::DiagnosticCode::resource_limit, "",
                "allocation failed while compiling package-bake plan")}};
        } catch (...) {
            return authoring::DiagnosticReport{{make_diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "unexpected failure while compiling package-bake plan")}};
        }
    }
};

} // namespace detail

CompiledPackageBake::CompiledPackageBake(
    std::shared_ptr<const detail::CompiledPackageBakeStorage> storage) noexcept
    : storage_(std::move(storage)) {}

CompiledPackageBake::CompiledPackageBake(CompiledPackageBake &&other) noexcept
    : storage_(other.storage_) {}

CompiledPackageBake &
CompiledPackageBake::operator=(CompiledPackageBake &&other) noexcept {
    storage_ = other.storage_;
    return *this;
}

std::string_view CompiledPackageBake::id() const noexcept {
    return storage_->id;
}

compile::CompiledEngine CompiledPackageBake::engine() const noexcept {
    return storage_->engine;
}

std::uint64_t CompiledPackageBake::public_seed() const noexcept {
    return storage_->public_seed;
}

compile::SiRate CompiledPackageBake::audio_sample_rate() const noexcept {
    return storage_->audio_sample_rate;
}

std::span<const std::string> CompiledPackageBake::audio_bus_ids() const noexcept {
    return storage_->audio_bus_ids;
}

const PackageBakeMethodGeometry &CompiledPackageBake::method_geometry() const noexcept {
    return storage_->geometry;
}

const CompiledPackageBakeRpmRange &CompiledPackageBake::rpm_range() const noexcept {
    return storage_->rpm_range;
}

std::span<const CompiledPackageBakeScenarioSource>
CompiledPackageBake::scenario_sources() const noexcept {
    return storage_->scenario_sources;
}

std::span<const CompiledPackageBakeRunningPlane>
CompiledPackageBake::running_planes() const noexcept {
    return storage_->running_planes;
}

compile::CompiledScenario CompiledPackageBake::idle_scenario() const noexcept {
    return storage_->scenario_sources[storage_->idle_scenario_source_index].scenario;
}

std::size_t CompiledPackageBake::idle_scenario_source_index() const noexcept {
    return storage_->idle_scenario_source_index;
}

PackageBakeCompileResult compile_package_bake(
    const authoring::PackageBakeDocument &document,
    const compile::CompiledEngine &engine,
    const std::span<const PackageBakeScenarioInputView> scenario_inputs) noexcept {
    return detail::CompiledPackageBakeBuilder::build(document, engine, scenario_inputs);
}

} // namespace engine_sim_offline
