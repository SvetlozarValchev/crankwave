#include "engine_sim_offline/atlas_bake.hpp"

#include "atlas/compiled_atlas_bake_storage.hpp"
#include "compile/compiled_scenario_view.hpp"
#include "engine_sim_offline/contract/audio_atlas.hpp"
#include "engine_sim_offline/contract/common.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline {
namespace {

constexpr compile::SiRate kAtlasDeliveryRate{192000U, 1U};
constexpr compile::SiRate kAtlasPhysicsRate{20000U, 1U};

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
                                               const std::string_view suffix = {}) {
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

[[nodiscard]] bool finite_range(const double minimum,
                                const double maximum) noexcept {
    return std::isfinite(minimum) && std::isfinite(maximum) && minimum < maximum;
}

[[nodiscard]] bool valid_stable_id(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    const auto alphanumeric = [](const char byte) {
        return (byte >= 'a' && byte <= 'z') ||
               (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    return alphanumeric(value.front()) &&
           std::ranges::all_of(value.substr(1), [&](const char byte) {
               return alphanumeric(byte) || byte == '.' || byte == '_' ||
                      byte == '-';
           });
}

void validate_document_header(const authoring::AtlasBakeDocument &document,
                              const compile::CompiledEngine &engine,
                              authoring::DiagnosticReport &report) {
    if (document.schema != authoring::kAtlasBakeSchema) {
        add(report, authoring::DiagnosticCode::unsupported_schema, "/schema",
            "expected schema 'engine-sim-offline/atlas-bake'");
    }
    if (!valid_stable_id(document.id.value)) {
        add(report, authoring::DiagnosticCode::invalid_value, "/id",
            "atlas-bake identity is not a valid stable ID");
    }
    if (document.engine.value != engine.id()) {
        add(report, authoring::DiagnosticCode::inconsistent_value, "/engine",
            "atlas engine identity does not match the exact compiled engine");
    }
    const compile::SiRate rate{document.audio.sample_rate.numerator,
                               document.audio.sample_rate.denominator};
    if (document.audio.sample_rate.unit != "Hz" || rate != kAtlasDeliveryRate) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/audio/sample_rate",
            "the first audio-atlas capture slice requires exactly 192000/1 Hz");
    }
    if (document.audio.buses.empty()) {
        add(report, authoring::DiagnosticCode::missing_value, "/audio/buses",
            "at least one ordered audio bus is required");
    }
    std::unordered_set<std::string_view> bus_ids;
    for (std::size_t index = 0; index < document.audio.buses.size(); ++index) {
        const auto &id = document.audio.buses[index].value;
        const auto path = "/audio/buses/" + std::to_string(index);
        if (!valid_stable_id(id)) {
            add(report, authoring::DiagnosticCode::invalid_value, path,
                "audio bus reference is not a valid stable ID");
        } else if (!bus_ids.insert(id).second) {
            add(report, authoring::DiagnosticCode::duplicate_id, path,
                "audio bus references must be unique");
        }
    }

    const auto &domain = document.domain;
    if (!finite_range(domain.minimum_rpm, domain.maximum_rpm) ||
        !(domain.minimum_rpm > 0.0)) {
        add(report, authoring::DiagnosticCode::invalid_value, "/domain",
            "atlas RPM domain must be finite, positive, and ascending");
    }
    if (!std::isfinite(domain.minimum_load_coordinate) ||
        !std::isfinite(domain.maximum_load_coordinate) ||
        domain.minimum_load_coordinate < -1.0 ||
        domain.maximum_load_coordinate > 1.0 ||
        domain.minimum_load_coordinate > domain.maximum_load_coordinate) {
        add(report, authoring::DiagnosticCode::invalid_value, "/domain",
            "atlas load domain must be a finite ascending subset of [-1, 1]");
    }
    if (!document.stationary_tiles.empty()) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/stationary_tiles",
            "stationary tiles are outside the first moving-segment slice");
    }
    if (!document.transient_performances.empty()) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/transient_performances",
            "transient performances are outside the first moving-segment slice");
    }
    if (!document.lifecycle_performances.empty()) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            "/lifecycle_performances",
            "lifecycle performances are outside the first moving-segment slice");
    }
}

using SourceIndexById = std::unordered_map<std::string_view, std::size_t>;

[[nodiscard]] SourceIndexById validate_source_inputs(
    const authoring::AtlasBakeDocument &document,
    const std::span<const AtlasBakeScenarioInputView> inputs,
    authoring::DiagnosticReport &report) {
    SourceIndexById source_indices;
    if (document.scenario_sources.empty()) {
        add(report, authoring::DiagnosticCode::missing_value, "/scenario_sources",
            "at least one scenario source is required");
    }
    if (inputs.size() != document.scenario_sources.size()) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            "/scenario_sources",
            "parsed scenario inputs must match scenario_sources count and authored "
            "order exactly");
    }

    std::unordered_set<std::string_view> uris;
    for (std::size_t index = 0; index < document.scenario_sources.size(); ++index) {
        const auto &source = document.scenario_sources[index];
        const auto base = "/scenario_sources/" + std::to_string(index);
        if (!valid_stable_id(source.id.value)) {
            add(report, authoring::DiagnosticCode::invalid_value, base + "/id",
                "scenario-source identity is not a valid stable ID");
        } else if (!source_indices.emplace(source.id.value, index).second) {
            add(report, authoring::DiagnosticCode::duplicate_id, base + "/id",
                "scenario-source identities must be unique");
        }
        if (source.uri.empty()) {
            add(report, authoring::DiagnosticCode::invalid_value, base + "/uri",
                "scenario-source URI must not be empty");
        } else if (!uris.insert(source.uri).second) {
            add(report, authoring::DiagnosticCode::duplicate_id, base + "/uri",
                "scenario-source URIs must be unique");
        }
        if (index >= inputs.size()) {
            add(report, authoring::DiagnosticCode::missing_value,
                source_document_path(index),
                "no parsed scenario document was supplied in this authored slot");
            continue;
        }
        if (inputs[index].source_id != source.id.value) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                "/scenario_inputs/" + std::to_string(index) + "/source_id",
                "scenario input identity does not match the authored source at "
                "this position");
        }
        if (inputs[index].document == nullptr) {
            add(report, authoring::DiagnosticCode::missing_value,
                source_document_path(index),
                "scenario-source input contains a null document view");
        }
    }
    for (std::size_t index = document.scenario_sources.size(); index < inputs.size();
         ++index) {
        add(report, authoring::DiagnosticCode::dangling_reference,
            "/scenario_inputs/" + std::to_string(index) + "/source_id",
            "scenario input has no authored scenario-source slot");
    }
    return source_indices;
}

void validate_moving_segments(const authoring::AtlasBakeDocument &document,
                              const SourceIndexById &source_indices,
                              authoring::DiagnosticReport &report) {
    if (document.moving_segments.empty()) {
        add(report, authoring::DiagnosticCode::missing_value, "/moving_segments",
            "at least one moving segment is required");
    }
    std::unordered_set<std::string_view> segment_ids;
    std::vector<std::size_t> source_use_count(document.scenario_sources.size(), 0U);
    for (std::size_t index = 0; index < document.moving_segments.size(); ++index) {
        const auto &segment = document.moving_segments[index];
        const auto base = "/moving_segments/" + std::to_string(index);
        if (!valid_stable_id(segment.id.value)) {
            add(report, authoring::DiagnosticCode::invalid_value, base + "/id",
                "moving-segment identity is not a valid stable ID");
        } else if (!segment_ids.insert(segment.id.value).second) {
            add(report, authoring::DiagnosticCode::duplicate_id, base + "/id",
                "moving-segment identities must be unique");
        }
        if (segment.direction != authoring::AtlasBakeMovingDirection::rising &&
            segment.direction != authoring::AtlasBakeMovingDirection::falling) {
            add(report, authoring::DiagnosticCode::invalid_value,
                base + "/direction", "moving-segment direction is invalid");
        }
        if (!std::isfinite(segment.load_coordinate) ||
            segment.load_coordinate < document.domain.minimum_load_coordinate ||
            segment.load_coordinate > document.domain.maximum_load_coordinate) {
            add(report, authoring::DiagnosticCode::out_of_range,
                base + "/load_coordinate",
                "moving-segment load coordinate lies outside the atlas domain");
        }
        if ((segment.state_mask & ~contract::kAudioAtlasKnownStateMask) != 0U) {
            add(report, authoring::DiagnosticCode::out_of_range,
                base + "/state_mask",
                "moving-segment state mask contains an unknown state bit");
        }
        if (!finite_range(segment.captured_rpm.minimum,
                          segment.captured_rpm.maximum) ||
            !(segment.captured_rpm.minimum > 0.0)) {
            add(report, authoring::DiagnosticCode::invalid_value,
                base + "/captured_rpm",
                "captured RPM range must be finite, positive, and ascending");
        }
        if (!finite_range(segment.usable_rpm.minimum,
                          segment.usable_rpm.maximum) ||
            segment.usable_rpm.minimum <= segment.captured_rpm.minimum ||
            segment.usable_rpm.maximum >= segment.captured_rpm.maximum) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                base + "/usable_rpm",
                "usable RPM range must be a strict interior of captured RPM");
        }
        if (segment.usable_rpm.minimum < document.domain.minimum_rpm ||
            segment.usable_rpm.maximum > document.domain.maximum_rpm) {
            add(report, authoring::DiagnosticCode::out_of_range,
                base + "/usable_rpm",
                "usable RPM range lies outside the atlas domain");
        }
        const auto &slope = segment.normalized_rpm_slope;
        if (!std::isfinite(slope.minimum_per_second) ||
            !std::isfinite(slope.maximum_per_second) ||
            slope.minimum_per_second > slope.maximum_per_second) {
            add(report, authoring::DiagnosticCode::invalid_value,
                base + "/normalized_rpm_slope",
                "normalized RPM-slope range must be finite and ascending");
        } else if (segment.direction ==
                       authoring::AtlasBakeMovingDirection::rising &&
                   !(slope.minimum_per_second > 0.0)) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                base + "/normalized_rpm_slope",
                "a rising segment requires a strictly positive normalized slope range");
        } else if (segment.direction ==
                       authoring::AtlasBakeMovingDirection::falling &&
                   !(slope.maximum_per_second < 0.0)) {
            add(report, authoring::DiagnosticCode::inconsistent_value,
                base + "/normalized_rpm_slope",
                "a falling segment requires a strictly negative normalized slope range");
        }
        const auto &handoff = segment.handoff;
        if (handoff.transition_frames == 0U ||
            !std::isfinite(handoff.maximum_rpm_error) ||
            handoff.maximum_rpm_error < 0.0 ||
            !std::isfinite(
                handoff.maximum_normalized_rpm_slope_error_per_second) ||
            handoff.maximum_normalized_rpm_slope_error_per_second < 0.0 ||
            !std::isfinite(handoff.maximum_load_error) ||
            handoff.maximum_load_error < 0.0 || handoff.maximum_load_error > 2.0 ||
            !std::isfinite(handoff.maximum_crank_phase_error_revolutions) ||
            handoff.maximum_crank_phase_error_revolutions < 0.0 ||
            handoff.maximum_crank_phase_error_revolutions > 0.5) {
            add(report, authoring::DiagnosticCode::invalid_value,
                base + "/handoff", "moving-segment handoff envelope is invalid");
        }

        const auto found = source_indices.find(segment.scenario.value);
        if (found == source_indices.end()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                base + "/scenario",
                "moving-segment scenario does not resolve to an authored source");
        } else if (++source_use_count[found->second] != 1U) {
            add(report, authoring::DiagnosticCode::duplicate_id,
                base + "/scenario",
                "each moving segment must own an independent source scenario");
        }
    }
    for (std::size_t index = 0; index < source_use_count.size(); ++index) {
        if (source_use_count[index] == 0U) {
            add(report, authoring::DiagnosticCode::disconnected_object,
                "/scenario_sources/" + std::to_string(index) + "/id",
                "scenario source is not owned by a moving segment");
        }
    }
}

void validate_compiled_source(
    const authoring::AtlasBakeDocument &atlas,
    const authoring::ScenarioDocument &authored_scenario,
    const compile::detail::CompiledScenarioInputsView compiled,
    const std::size_t source_index, authoring::DiagnosticReport &report) {
    const auto &scenario = compiled.scenario.scenario;
    if (authored_scenario.engine.value != atlas.engine.value ||
        scenario.engine_profile_id != atlas.engine.value) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/engine"),
            "source scenario engine does not match the atlas engine");
    }
    if (scenario.public_seed.value != atlas.public_seed) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/public_seed"),
            "source scenario public seed must equal the atlas public seed");
    }
    const auto has_rate = [](const auto &rate, const compile::SiRate expected) {
        return rate.numerator == expected.numerator_hz &&
               rate.denominator == expected.denominator;
    };
    if (!has_rate(scenario.rates.physics, kAtlasPhysicsRate) ||
        !has_rate(scenario.rates.capture, kAtlasPhysicsRate)) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            source_document_path(source_index, "/rates"),
            "atlas sources require canonical 20000/1 Hz physics and capture clocks");
    }
    if (!has_rate(scenario.rates.source_processing, kAtlasDeliveryRate) ||
        !has_rate(scenario.rates.acoustic, kAtlasDeliveryRate) ||
        !has_rate(scenario.rates.delivery, kAtlasDeliveryRate)) {
        add(report, authoring::DiagnosticCode::unsupported_capability,
            source_document_path(source_index, "/rates"),
            "atlas sources require canonical 192000/1 Hz source, acoustics, and delivery clocks");
    }
    if (compiled.scenario.request_input.total_physics_frames == 0U ||
        compiled.scenario.request_input.audible_delivery_frames == 0U ||
        !(scenario.total_duration_s.value > 0.0) ||
        !(scenario.audible_duration_s.value > 0.0)) {
        add(report, authoring::DiagnosticCode::inconsistent_value,
            source_document_path(source_index, "/total_duration"),
            "atlas source must compile to a finite positive capture horizon");
    }
    for (const auto &required : atlas.audio.buses) {
        const auto found = std::ranges::find(
            compiled.scenario.request_input.selected_audio_buses, required.value,
            &compile::detail::ResolvedAudioBusDescriptor::authored_id);
        if (found == compiled.scenario.request_input.selected_audio_buses.end()) {
            add(report, authoring::DiagnosticCode::dangling_reference,
                source_document_path(source_index, "/output/buses"),
                "source scenario does not select required atlas audio bus '" +
                    required.value + "'");
        }
    }
}

} // namespace

namespace detail {

class CompiledAtlasBakeBuilder final {
  public:
    [[nodiscard]] static AtlasBakeCompileResult build(
        const authoring::AtlasBakeDocument &document,
        const compile::CompiledEngine &engine,
        const std::span<const AtlasBakeScenarioInputView> scenario_inputs) noexcept {
        try {
            authoring::DiagnosticReport report;
            validate_document_header(document, engine, report);
            const auto source_indices =
                validate_source_inputs(document, scenario_inputs, report);
            validate_moving_segments(document, source_indices, report);
            if (report.has_errors()) {
                return report;
            }

            auto storage = std::make_shared<CompiledAtlasBakeStorage>(
                CompiledAtlasBakeStorage{
                    document.id.value,
                    engine,
                    document.public_seed,
                    {document.audio.sample_rate.numerator,
                     document.audio.sample_rate.denominator},
                    {},
                    document.domain,
                    {},
                    {},
                });
            storage->scenario_sources.reserve(document.scenario_sources.size());
            for (std::size_t index = 0; index < document.scenario_sources.size();
                 ++index) {
                auto result = compile::compile_scenario(
                    engine, *scenario_inputs[index].document);
                if (auto *diagnostics =
                        std::get_if<authoring::DiagnosticReport>(&result)) {
                    append_prefixed(report, std::move(*diagnostics), index);
                    continue;
                }
                auto scenario =
                    std::get<compile::CompiledScenario>(std::move(result));
                const auto compiled =
                    compile::detail::CompiledScenarioViewAccess::inputs(scenario);
                validate_compiled_source(document, *scenario_inputs[index].document,
                                         compiled, index, report);

                if (index == 0U) {
                    storage->audio_buses.reserve(document.audio.buses.size());
                    for (const auto &required : document.audio.buses) {
                        const auto resolved = std::ranges::find(
                            compiled.scenario.request_input.selected_audio_buses,
                            required.value,
                            &compile::detail::ResolvedAudioBusDescriptor::authored_id);
                        if (resolved != compiled.scenario.request_input
                                            .selected_audio_buses.end()) {
                            storage->audio_buses.push_back(
                                {required.value, resolved->semantic_id});
                        }
                    }
                }
                storage->scenario_sources.push_back(
                    {document.scenario_sources[index].id.value,
                     document.scenario_sources[index].uri, std::move(scenario)});
            }
            if (report.has_errors()) {
                return report;
            }

            storage->moving_segments.reserve(document.moving_segments.size());
            for (const auto &segment : document.moving_segments) {
                const auto source_index = source_indices.at(segment.scenario.value);
                storage->moving_segments.push_back(
                    {segment, source_index,
                     storage->scenario_sources[source_index].scenario});
            }
            return CompiledAtlasBake{std::move(storage)};
        } catch (const std::bad_alloc &) {
            return authoring::DiagnosticReport{{make_diagnostic(
                authoring::DiagnosticCode::resource_limit, "",
                "allocation failed while compiling audio-atlas bake plan")}};
        } catch (...) {
            return authoring::DiagnosticReport{{make_diagnostic(
                authoring::DiagnosticCode::internal_failure, "",
                "unexpected failure while compiling audio-atlas bake plan")}};
        }
    }
};

} // namespace detail

CompiledAtlasBake::CompiledAtlasBake(
    std::shared_ptr<const detail::CompiledAtlasBakeStorage> storage) noexcept
    : storage_(std::move(storage)) {}

CompiledAtlasBake::CompiledAtlasBake(CompiledAtlasBake &&other) noexcept
    : storage_(other.storage_) {}

CompiledAtlasBake &CompiledAtlasBake::operator=(CompiledAtlasBake &&other) noexcept {
    storage_ = other.storage_;
    return *this;
}

std::string_view CompiledAtlasBake::id() const noexcept {
    return storage_->id;
}

compile::CompiledEngine CompiledAtlasBake::engine() const noexcept {
    return storage_->engine;
}

std::uint64_t CompiledAtlasBake::public_seed() const noexcept {
    return storage_->public_seed;
}

compile::SiRate CompiledAtlasBake::audio_sample_rate() const noexcept {
    return storage_->audio_sample_rate;
}

std::span<const CompiledAtlasBakeAudioBus>
CompiledAtlasBake::audio_buses() const noexcept {
    return storage_->audio_buses;
}

const authoring::AtlasBakeDomain &CompiledAtlasBake::domain() const noexcept {
    return storage_->domain;
}

std::span<const CompiledAtlasBakeScenarioSource>
CompiledAtlasBake::scenario_sources() const noexcept {
    return storage_->scenario_sources;
}

std::span<const CompiledAtlasBakeMovingSegment>
CompiledAtlasBake::moving_segments() const noexcept {
    return storage_->moving_segments;
}

AtlasBakeCompileResult compile_atlas_bake(
    const authoring::AtlasBakeDocument &document,
    const compile::CompiledEngine &engine,
    const std::span<const AtlasBakeScenarioInputView> scenario_inputs) noexcept {
    return detail::CompiledAtlasBakeBuilder::build(document, engine, scenario_inputs);
}

} // namespace engine_sim_offline
