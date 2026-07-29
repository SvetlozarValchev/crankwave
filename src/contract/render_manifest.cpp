#include "engine_sim_offline/contract/render_manifest.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

[[nodiscard]] bool is_lower_hex(std::string_view value) noexcept {
    return !value.empty() && std::ranges::all_of(value, [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] bool is_nonzero_lower_hex(std::string_view value) noexcept {
    return is_lower_hex(value) &&
           value.find_first_not_of('0') != std::string_view::npos;
}

[[nodiscard]] bool is_canonical_decimal(std::string_view value,
                                        bool require_positive) noexcept {
    if (value.empty() || (value.size() > 1 && value.front() == '0') ||
        !std::ranges::all_of(value, [](char character) {
            return character >= '0' && character <= '9';
        })) {
        return false;
    }
    return !require_positive || value != "0";
}

[[nodiscard]] bool is_canonical_dotted_decimal(std::string_view value) noexcept {
    std::size_t component_count = 0;
    while (!value.empty()) {
        const auto position = value.find('.');
        const auto component = value.substr(0, position);
        if (!is_canonical_decimal(component, false)) {
            return false;
        }
        ++component_count;
        if (position == std::string_view::npos) {
            break;
        }
        if (position + 1 == value.size()) {
            return false;
        }
        value.remove_prefix(position + 1);
    }
    return component_count >= 2;
}

[[nodiscard]] bool is_canonical_git_commit_id(std::string_view value) noexcept {
    return (value.size() == 40 || value.size() == 64) && is_nonzero_lower_hex(value);
}

[[nodiscard]] bool is_admitted_compiler_identity(const BuildIdentity &build) noexcept {
    if (!is_canonical_dotted_decimal(build.compiler_version)) {
        return false;
    }
    return (build.compiler_id == "GNU" && build.target_triple == "x86_64-linux-gnu") ||
           (build.compiler_id == "Clang" &&
            build.target_triple == "x86_64-pc-linux-gnu");
}

bool valid_artifact_kind(ArtifactKind kind) {
    switch (kind) {
    case ArtifactKind::audio:
    case ArtifactKind::telemetry:
    case ArtifactKind::routing_report:
    case ArtifactKind::validation_report:
        return true;
    case ArtifactKind::unspecified:
        return false;
    }
    return false;
}

bool valid_source_route_kind(SourceRouteKind kind) {
    switch (kind) {
    case SourceRouteKind::unspecified:
        return false;
    case SourceRouteKind::exhaust_outlet:
    case SourceRouteKind::intake_inlet:
    case SourceRouteKind::mechanical_engine:
    case SourceRouteKind::mechanical_starter:
        return true;
    }
    return false;
}

bool valid_output_bus_kind(OutputBusKind kind) {
    switch (kind) {
    case OutputBusKind::master_engine_raw:
    case OutputBusKind::master_engine_audition:
    case OutputBusKind::master_reference_raw:
    case OutputBusKind::master_reference_audition:
        return true;
    case OutputBusKind::unspecified:
        return false;
    }
    return false;
}

bool valid_route_disposition(RouteDisposition disposition) {
    switch (disposition) {
    case RouteDisposition::rendered:
    case RouteDisposition::not_applicable:
        return true;
    case RouteDisposition::unspecified:
        return false;
    }
    return false;
}

bool valid_random_component_kind(RandomComponentKind kind) {
    switch (kind) {
    case RandomComponentKind::combustion:
    case RandomComponentKind::presentation_jitter:
    case RandomComponentKind::presentation_air_noise:
    case RandomComponentKind::starter:
        return true;
    case RandomComponentKind::unspecified:
        return false;
    }
    return false;
}

void validate_audio_contract(ValidationReport &report, const AudioContract &audio,
                             const std::string &path) {
    detail::append_prefixed(report, validate(audio.sample_rate), path + ".sample_rate");
    detail::require(report, audio.frame_count > 0, ContractIssueCode::invalid_value,
                    path + ".frame_count",
                    "audio artifact must contain at least one frame");
    detail::require(report, is_valid_semantic_id(audio.channel_layout_id),
                    ContractIssueCode::invalid_value, path + ".channel_layout_id",
                    "channel-layout ID must be a canonical semantic ID");
    detail::require(report, is_valid_semantic_id(audio.sample_encoding_id),
                    ContractIssueCode::invalid_value, path + ".sample_encoding_id",
                    "sample-encoding ID must be a canonical semantic ID");
}

void validate_delivery_audio_contract(ValidationReport &report,
                                      const AudioContract &audio,
                                      const RationalRateHz &delivery_rate,
                                      std::optional<std::uint64_t> frame_count,
                                      const std::string &path) {
    validate_audio_contract(report, audio, path);
    detail::require(report, audio.sample_rate == delivery_rate,
                    ContractIssueCode::inconsistent_semantics, path + ".sample_rate",
                    "audio sample rate must equal the manifest delivery rate");
    if (frame_count.has_value()) {
        detail::require(report, audio.frame_count == *frame_count,
                        ContractIssueCode::inconsistent_semantics,
                        path + ".frame_count",
                        "audio frame count must exactly cover the audible duration");
    }
}

bool valid_relative_artifact_path(const std::string &path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos) {
        return false;
    }
    if (std::ranges::any_of(
            path, [](unsigned char byte) { return byte <= 0x1fU || byte == 0x7fU; })) {
        return false;
    }

    std::size_t begin = 0;
    while (begin <= path.size()) {
        const auto end = path.find('/', begin);
        const auto component = path.substr(begin, end - begin);
        if (component.empty() || component == "." || component == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return true;
}

std::string portable_path_key(std::string_view path) {
    std::string key;
    key.reserve(path.size());
    for (const char character : path) {
        if (character >= 'A' && character <= 'Z') {
            key.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            key.push_back(character);
        }
    }
    return key;
}

bool valid_utc_timestamp(std::string_view value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return false;
    }
    const auto digit = [](char character) {
        return character >= '0' && character <= '9';
    };
    for (const auto index :
         {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U, 11U, 12U, 14U, 15U, 17U, 18U}) {
        if (!digit(value[index])) {
            return false;
        }
    }
    if (value.size() > 20) {
        if (value[19] != '.' || value.size() == 21) {
            return false;
        }
        for (std::size_t index = 20; index + 1 < value.size(); ++index) {
            if (!digit(value[index])) {
                return false;
            }
        }
    }
    const auto two_digits = [&](std::size_t index) {
        return static_cast<unsigned>(value[index] - '0') * 10U +
               static_cast<unsigned>(value[index + 1] - '0');
    };
    const auto four_digits = [&](std::size_t index) {
        return static_cast<unsigned>(value[index] - '0') * 1000U +
               static_cast<unsigned>(value[index + 1] - '0') * 100U +
               static_cast<unsigned>(value[index + 2] - '0') * 10U +
               static_cast<unsigned>(value[index + 3] - '0');
    };
    const auto year = four_digits(0);
    const auto month = two_digits(5);
    const auto day = two_digits(8);
    const auto hour = two_digits(11);
    const auto minute = two_digits(14);
    const auto second = two_digits(17);
    if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    constexpr std::array<unsigned, 12> days_per_month{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    auto maximum_day = days_per_month[month - 1];
    const bool leap_year = year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
    if (month == 2 && leap_year) {
        maximum_day = 29;
    }
    return day >= 1 && day <= maximum_day;
}

void validate_unique_owned_roles(ValidationReport &report,
                                 const std::vector<std::string> &roles,
                                 const std::string &path) {
    std::unordered_set<std::string> unique_roles;
    for (std::size_t index = 0; index < roles.size(); ++index) {
        const auto role_path = path + "[" + std::to_string(index) + "]";
        detail::require(report, is_valid_semantic_id(roles[index]),
                        ContractIssueCode::invalid_value, role_path,
                        "owned artifact role must be a canonical semantic ID");
        if (!unique_roles.insert(roles[index]).second) {
            report.add(ContractIssueCode::duplicate_identity, role_path,
                       "an owner cannot name the same artifact role twice");
        }
    }
}

struct ManifestRouteView {
    RouteId route_id;
    std::string_view semantic_id;
    SourceRouteKind kind = SourceRouteKind::unspecified;
};

struct ManifestInputView {
    const PresentationCalibration *presentation = nullptr;
    const EngineSpec *simulation_engine = nullptr;
    std::vector<ManifestRouteView> routes;
    RenderRates rates;
    std::uint64_t public_seed = 0;
    std::optional<std::uint64_t> delivery_frame_count;
    std::string route_validation_path;
};

ManifestInputView make_input_view(const SimulationManifestInputs &inputs) {
    ManifestInputView view;
    view.presentation = &inputs.resolved.presentation;
    view.simulation_engine = &inputs.resolved.engine;
    view.routes.reserve(inputs.resolved.engine.routes.size());
    for (const auto &route : inputs.resolved.engine.routes) {
        view.routes.push_back({route.id, route.semantic_id.value, route.kind.value});
    }
    view.rates = inputs.resolved.scenario.rates;
    view.public_seed = inputs.resolved.scenario.public_seed.value;
    view.delivery_frame_count =
        resolve_frame_index(inputs.resolved.scenario.audible_duration_s.value,
                            inputs.resolved.scenario.rates.delivery);
    view.route_validation_path = "inputs.simulation.resolved.engine.routes";
    return view;
}

} // namespace

ValidationReport validate_render_admission(const EngineSpec &engine,
                                           const PresentationCalibration &presentation,
                                           const ResolvedRandomnessPolicy &randomness,
                                           const RenderScenario &scenario,
                                           const ProvenanceLedger &provenance,
                                           const SourceMatrixContract &source_matrix) {
    using detail::append_prefixed;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate(provenance), "provenance");
    append_prefixed(report, validate(source_matrix), "source_matrix");
    append_prefixed(report, validate(engine, provenance), "engine");
    append_prefixed(report, validate(scenario, provenance), "scenario");
    append_prefixed(report, validate_for_engine(scenario, engine), "engine_scenario");
    append_prefixed(report, validate(presentation, engine, scenario, provenance),
                    "presentation");
    append_prefixed(report, validate(randomness, provenance), "randomness");
    auto random_plan = compile_random_plan(randomness, engine, presentation, scenario);
    if (auto *plan_report = std::get_if<ValidationReport>(&random_plan)) {
        append_prefixed(report, std::move(*plan_report), "random_plan");
    }

    const auto &bmw_baseline = bmw_m52b28_reference_source_matrix_v1();
    if (source_matrix.id == bmw_baseline.id) {
        require(report, source_matrix == bmw_baseline,
                ContractIssueCode::inconsistent_semantics, "source_matrix",
                "the built-in BMW baseline matrix must exactly match its approved "
                "contract");
    }

    const auto expected_frames =
        resolve_frame_index(scenario.audible_duration_s.value, scenario.rates.delivery);
    require(report, expected_frames.has_value() && *expected_frames > 0,
            ContractIssueCode::inconsistent_semantics, "scenario.audible_duration_s",
            "audible duration and delivery rate must resolve to a positive integral "
            "frame count");
    for (std::size_t index = 0; index < source_matrix.required_artifacts.size();
         ++index) {
        const auto &requirement = source_matrix.required_artifacts[index];
        if (!requirement.audio.has_value()) {
            continue;
        }
        validate_delivery_audio_contract(
            report, *requirement.audio, scenario.rates.delivery, expected_frames,
            "source_matrix.required_artifacts[" + std::to_string(index) + "].audio");
    }

    require(report, engine.routes.size() == source_matrix.required_source_routes.size(),
            ContractIssueCode::inconsistent_shape, "engine.routes",
            "resolved engine source routes must exactly match the selected source "
            "matrix");

    for (std::size_t index = 0; index < engine.routes.size(); ++index) {
        const auto &engine_route = engine.routes[index];
        const auto path = "engine.routes[" + std::to_string(index) + "]";
        const auto requirement = std::ranges::find(
            source_matrix.required_source_routes, engine_route.semantic_id.value,
            &SourceRouteRequirement::semantic_id);
        require(report, requirement != source_matrix.required_source_routes.end(),
                ContractIssueCode::inconsistent_semantics, path + ".semantic_id",
                "resolved engine route is absent from the selected source matrix");
        if (requirement == source_matrix.required_source_routes.end()) {
            continue;
        }
        require(report, engine_route.kind.value == requirement->kind,
                ContractIssueCode::inconsistent_semantics, path + ".kind",
                "resolved engine route kind must match the selected source matrix");
        const auto presentation_configured = std::ranges::any_of(
            presentation.routes, [&](const RoutePresentation &route) {
                return route.route_id == engine_route.id;
            });
        require(report,
                presentation_configured ==
                    (requirement->disposition == RouteDisposition::rendered),
                ContractIssueCode::inconsistent_semantics, path,
                "exactly rendered source-matrix routes require presentation "
                "configuration");
    }

    for (std::size_t index = 0; index < source_matrix.required_source_routes.size();
         ++index) {
        const auto &required_route = source_matrix.required_source_routes[index];
        require(report,
                std::ranges::any_of(engine.routes,
                                    [&](const RouteSpec &engine_route) {
                                        return engine_route.semantic_id.value ==
                                                   required_route.semantic_id &&
                                               engine_route.kind.value ==
                                                   required_route.kind;
                                    }),
                ContractIssueCode::missing_value,
                "source_matrix.required_source_routes[" + std::to_string(index) + "]",
                "selected source-matrix route is absent from the resolved engine");
    }

    return report;
}

OutputContract resolve_output_contract(const SourceMatrixContract &source_matrix) {
    return {
        source_matrix.id,
        source_matrix.sha256,
        source_matrix.distribution,
        source_matrix.required_source_routes,
        source_matrix.required_output_buses,
        source_matrix.required_artifacts,
        source_matrix.declared_omissions,
    };
}

bool same_content_identity(const RenderManifest &lhs, const RenderManifest &rhs) {
    return lhs.content == rhs.content;
}

ValidationReport validate(const RenderManifestContent &content,
                          const ProvenanceLedger &provenance,
                          const SourceMatrixContract &source_matrix) {
    using detail::append_prefixed;
    using detail::require;

    ValidationReport report;
    append_prefixed(report,
                    validate_render_admission(content.inputs.resolved.engine,
                                              content.inputs.resolved.presentation,
                                              content.inputs.resolved.randomness,
                                              content.inputs.resolved.scenario,
                                              provenance, source_matrix),
                    "admission");
    const auto input_view = make_input_view(content.inputs);

    require(report, content.schema_version == 6, ContractIssueCode::unsupported_value,
            "schema_version", "render-manifest schema must be version 6");
    require(report, content.rates == input_view.rates,
            ContractIssueCode::inconsistent_semantics, "rates",
            "manifest rates must equal the selected input rates");
    append_prefixed(report, validate(content.rates), "rates");

    require(report, is_valid_semantic_id(content.provenance.id),
            ContractIssueCode::invalid_value, "provenance.id",
            "provenance bundle ID must be a canonical semantic ID");
    require(report, !content.provenance.sha256.is_zero(),
            ContractIssueCode::invalid_value, "provenance.sha256",
            "provenance bundle digest must be nonzero");
    require(report, content.provenance == provenance.bundle,
            ContractIssueCode::inconsistent_semantics, "provenance",
            "manifest provenance reference must identify the supplied ledger "
            "bundle exactly");

    const auto &build = content.determinism.build;
    require(report,
            !build.git_commit_id.empty() && !build.compiler_id.empty() &&
                !build.compiler_version.empty() && !build.compiler_runtime_id.empty() &&
                !build.compiler_runtime_identity.empty() &&
                !build.target_triple.empty() && !build.standard_library_id.empty() &&
                !build.standard_library_identity.empty() &&
                !build.math_library_id.empty() && !build.math_library_identity.empty(),
            ContractIssueCode::missing_value, "determinism.build",
            "deterministic build identity must be complete");
    require(report, is_canonical_git_commit_id(build.git_commit_id),
            ContractIssueCode::invalid_value, "determinism.build.git_commit_id",
            "renderer Git commit must be a nonzero lowercase 40- or 64-digit object "
            "ID");
    require(report, !build.source_closure_sha256.is_zero(),
            ContractIssueCode::invalid_value, "determinism.build.source_closure_sha256",
            "renderer source-closure digest must be nonzero");
    require(report, is_admitted_compiler_identity(build),
            ContractIssueCode::unsupported_value, "determinism.build.compiler_identity",
            "renderer compiler ID/version/target triple is outside the admitted "
            "GNU or Clang Linux x86-64 build identity");
    require(report, is_valid_semantic_id(content.determinism.numeric_policy_id),
            ContractIssueCode::invalid_value, "determinism.numeric_policy_id",
            "numeric-policy ID must be a canonical semantic ID");
    require(report, is_valid_semantic_id(content.determinism.instruction_set_profile),
            ContractIssueCode::invalid_value, "determinism.instruction_set_profile",
            "instruction-set profile must be a canonical semantic ID");
    const auto &floating_point = content.determinism.floating_point;
    require(report,
            floating_point.format == "ieee754_binary64" &&
                floating_point.rounding == "nearest_ties_to_even" &&
                !floating_point.fma_contraction && !floating_point.flush_to_zero &&
                !floating_point.denormals_are_zero,
            ContractIssueCode::unsupported_value, "determinism.floating_point",
            "current model contract requires strict IEEE-754 binary64 semantics");
    require(report, content.determinism.deterministic_worker_count > 0,
            ContractIssueCode::invalid_value, "determinism.deterministic_worker_count",
            "deterministic worker count must be positive");
    require(report,
            is_valid_semantic_id(content.determinism.deterministic_reduction_topology),
            ContractIssueCode::invalid_value,
            "determinism.deterministic_reduction_topology",
            "reduction-topology ID must be canonical");
    append_prefixed(report, validate(content.randomness.generator),
                    "randomness.generator");
    append_prefixed(report, validate(content.randomness.derivation),
                    "randomness.derivation");
    require(report,
            content.randomness.generator ==
                content.inputs.resolved.randomness.generator.value,
            ContractIssueCode::inconsistent_semantics, "randomness.generator",
            "executed generator must equal the resolved randomness policy");
    require(report,
            content.randomness.derivation ==
                content.inputs.resolved.randomness.derivation.value,
            ContractIssueCode::inconsistent_semantics, "randomness.derivation",
            "executed derivation must equal the resolved randomness policy");
    require(report, content.randomness.public_seed == input_view.public_seed,
            ContractIssueCode::inconsistent_semantics, "randomness.public_seed",
            "manifest random seed must equal the selected input public seed");
    std::unordered_set<std::string> component_seed_ids;
    std::unordered_map<std::uint32_t, std::uint32_t> combustion_seed_count;
    std::unordered_map<std::uint32_t, std::uint32_t> jitter_seed_count;
    std::unordered_map<std::uint32_t, std::uint32_t> air_noise_seed_count;
    for (std::size_t index = 0; index < content.randomness.component_seeds.size();
         ++index) {
        const auto &seed = content.randomness.component_seeds[index];
        const auto path = "randomness.component_seeds[" + std::to_string(index) + "]";
        require(report, valid_random_component_kind(seed.kind),
                ContractIssueCode::unsupported_value, path + ".kind",
                "random component kind must be recognized");
        const auto cylinder_owned = seed.kind == RandomComponentKind::combustion;
        const auto route_owned =
            seed.kind == RandomComponentKind::presentation_jitter ||
            seed.kind == RandomComponentKind::presentation_air_noise ||
            seed.kind == RandomComponentKind::starter;
        require(report,
                cylinder_owned ==
                        (seed.cylinder_id.has_value() && !seed.route_id.has_value()) &&
                    route_owned ==
                        (seed.route_id.has_value() && !seed.cylinder_id.has_value()),
                ContractIssueCode::inconsistent_semantics, path,
                "random seed owner must match its component kind");
        if (seed.cylinder_id.has_value()) {
            require(report,
                    input_view.simulation_engine != nullptr &&
                        std::ranges::any_of(input_view.simulation_engine->cylinders,
                                            [&](const CylinderSpec &cylinder) {
                                                return cylinder.id == *seed.cylinder_id;
                                            }),
                    ContractIssueCode::dangling_reference, path + ".cylinder_id",
                    "combustion seed requires a simulated input and known cylinder");
        }
        if (seed.route_id.has_value()) {
            const auto route = std::ranges::find(input_view.routes, *seed.route_id,
                                                 &ManifestRouteView::route_id);
            require(report, route != input_view.routes.end(),
                    ContractIssueCode::dangling_reference, path + ".route_id",
                    "presentation/starter seed references an unknown route");
            if (route != input_view.routes.end() &&
                seed.kind == RandomComponentKind::starter) {
                require(report, route->kind == SourceRouteKind::mechanical_starter,
                        ContractIssueCode::inconsistent_semantics, path + ".route_id",
                        "starter randomness must belong to a starter route");
            }
            if (route != input_view.routes.end() &&
                (seed.kind == RandomComponentKind::presentation_jitter ||
                 seed.kind == RandomComponentKind::presentation_air_noise)) {
                require(report,
                        std::ranges::any_of(
                            input_view.presentation->routes,
                            [&](const RoutePresentation &presentation_route) {
                                return presentation_route.route_id == *seed.route_id;
                            }),
                        ContractIssueCode::inconsistent_semantics, path + ".route_id",
                        "presentation randomness must belong to a configured route");
            }
        }
        require(report,
                seed.stream <= (std::numeric_limits<std::uint64_t>::max() >> 1U),
                ContractIssueCode::invalid_value, path + ".stream",
                "PCG32 stream selector must fit before odd-increment encoding");
        const auto owner_value =
            seed.cylinder_id.has_value()
                ? seed.cylinder_id->value
                : (seed.route_id.has_value() ? seed.route_id->value : 0U);
        const auto seed_key = std::to_string(static_cast<std::uint8_t>(seed.kind)) +
                              ":" + std::to_string(owner_value);
        if (!component_seed_ids.insert(seed_key).second) {
            report.add(ContractIssueCode::duplicate_identity, path,
                       "each stochastic component owns exactly one stream");
        }
        if (seed.route_id.has_value() &&
            seed.kind == RandomComponentKind::presentation_jitter) {
            ++jitter_seed_count[seed.route_id->value];
        }
        if (seed.route_id.has_value() &&
            seed.kind == RandomComponentKind::presentation_air_noise) {
            ++air_noise_seed_count[seed.route_id->value];
        }
        if (seed.cylinder_id.has_value() &&
            seed.kind == RandomComponentKind::combustion) {
            ++combustion_seed_count[seed.cylinder_id->value];
        }
    }
    if (input_view.simulation_engine != nullptr) {
        for (const auto &cylinder : input_view.simulation_engine->cylinders) {
            require(report, combustion_seed_count[cylinder.id.value] == 1,
                    ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                    "implemented combustion requires exactly one initialized stream "
                    "per cylinder");
        }
    }
    for (const auto &route : input_view.presentation->routes) {
        require(report, jitter_seed_count[route.route_id.value] == 1,
                ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                "implemented presentation jitter requires exactly one initialized "
                "stream per configured route");
        require(report, air_noise_seed_count[route.route_id.value] == 1,
                ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                "implemented presentation air noise requires exactly one initialized "
                "stream per configured route");
    }
    auto expected_random_plan = compile_random_plan(
        content.inputs.resolved.randomness, content.inputs.resolved.engine,
        content.inputs.resolved.presentation, content.inputs.resolved.scenario);
    if (const auto *expected = std::get_if<RandomPlan>(&expected_random_plan)) {
        require(report, content.randomness == *expected,
                ContractIssueCode::inconsistent_semantics, "randomness.component_seeds",
                "initialized random plan must exactly equal canonical component "
                "derivation and ordering");
    }
    require(report, content.output_contract == resolve_output_contract(source_matrix),
            ContractIssueCode::inconsistent_semantics, "output_contract",
            "resolved output contract must exactly match the selected source matrix");

    const auto expected_frames = input_view.delivery_frame_count;
    require(report, expected_frames.has_value() && *expected_frames > 0,
            ContractIssueCode::inconsistent_semantics, "inputs.delivery_frame_count",
            "selected inputs must resolve a positive delivery frame count");

    std::unordered_map<std::string, const ArtifactRequirement *> requirement_by_role;
    for (std::size_t index = 0;
         index < content.output_contract.required_artifacts.size(); ++index) {
        const auto &requirement = content.output_contract.required_artifacts[index];
        const auto path =
            "output_contract.required_artifacts[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(requirement.role),
                ContractIssueCode::invalid_value, path + ".role",
                "artifact role must be a canonical semantic ID");
        if (!requirement_by_role.emplace(requirement.role, &requirement).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".role",
                       "required artifact roles must be unique");
        }
        require(report, valid_artifact_kind(requirement.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "artifact kind must be specified");
        require(report,
                (requirement.kind == ArtifactKind::audio) ==
                    requirement.audio.has_value(),
                ContractIssueCode::inconsistent_semantics, path + ".audio",
                "exactly audio artifacts carry an audio contract");
        if (requirement.audio.has_value()) {
            validate_delivery_audio_contract(report, *requirement.audio,
                                             content.rates.delivery, expected_frames,
                                             path + ".audio");
        }
    }

    std::unordered_map<std::string, const ArtifactRecord *> artifact_by_role;
    std::unordered_set<std::string> artifact_paths;
    for (std::size_t index = 0; index < content.artifacts.size(); ++index) {
        const auto &artifact = content.artifacts[index];
        const auto path = "artifacts[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(artifact.role),
                ContractIssueCode::invalid_value, path + ".role",
                "artifact role must be a canonical semantic ID");
        if (!artifact_by_role.emplace(artifact.role, &artifact).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".role",
                       "artifact roles must be unique");
        }
        require(report, valid_relative_artifact_path(artifact.relative_path),
                ContractIssueCode::invalid_value, path + ".relative_path",
                "artifact path must be normalized, relative, portable, and free of "
                "control characters");
        if (!artifact_paths.insert(portable_path_key(artifact.relative_path)).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".relative_path",
                       "artifact payload paths must be unique on portable "
                       "case-insensitive filesystems");
        }
        require(report, artifact.byte_count > 0, ContractIssueCode::invalid_value,
                path + ".byte_count", "successful artifact payload must be nonempty");
        require(report, !artifact.payload_sha256.is_zero(),
                ContractIssueCode::invalid_value, path + ".payload_sha256",
                "artifact digest must be nonzero");
        require(report, valid_artifact_kind(artifact.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "artifact kind must be specified");
        require(report,
                (artifact.kind == ArtifactKind::audio) == artifact.audio.has_value(),
                ContractIssueCode::inconsistent_semantics, path + ".audio",
                "exactly audio artifacts carry an audio contract");
        if (artifact.audio.has_value()) {
            validate_delivery_audio_contract(report, *artifact.audio,
                                             content.rates.delivery, expected_frames,
                                             path + ".audio");
        }
        const auto requirement = requirement_by_role.find(artifact.role);
        if (requirement == requirement_by_role.end()) {
            require(report, artifact.diagnostic,
                    ContractIssueCode::inconsistent_semantics, path + ".diagnostic",
                    "undeclared extra artifacts must be explicitly diagnostic");
        } else {
            require(report,
                    artifact.kind == requirement->second->kind &&
                        artifact.audio == requirement->second->audio &&
                        artifact.diagnostic == requirement->second->diagnostic,
                    ContractIssueCode::inconsistent_semantics, path,
                    "artifact media and diagnostic contract must exactly match its "
                    "requirement");
        }
    }
    for (const auto &[role, requirement] : requirement_by_role) {
        static_cast<void>(requirement);
        require(report, artifact_by_role.contains(role),
                ContractIssueCode::missing_value, "artifacts",
                "every required artifact role must have exactly one payload");
    }
    std::unordered_map<std::string, const SourceRouteRequirement *>
        required_route_by_semantic_id;
    for (const auto &route : content.output_contract.required_source_routes) {
        required_route_by_semantic_id.emplace(route.semantic_id, &route);
    }

    std::unordered_map<std::uint32_t, const ManifestRouteView *> input_route_by_id;
    for (const auto &route : input_view.routes) {
        input_route_by_id.emplace(route.route_id.value, &route);
    }
    require(report,
            input_view.routes.size() ==
                content.output_contract.required_source_routes.size(),
            ContractIssueCode::inconsistent_shape, input_view.route_validation_path,
            "manifest input source routes must exactly match the selected source "
            "matrix");

    std::unordered_set<std::uint32_t> manifested_route_ids;
    std::unordered_set<std::string> manifested_route_semantic_ids;
    std::unordered_map<std::string, std::size_t> artifact_ownership_count;
    for (std::size_t index = 0; index < content.routes.size(); ++index) {
        const auto &route = content.routes[index];
        const auto path = "routes[" + std::to_string(index) + "]";
        const auto input_route = input_route_by_id.find(route.route_id.value);
        require(report,
                route.route_id.valid() && input_route != input_route_by_id.end(),
                ContractIssueCode::dangling_reference, path + ".route_id",
                "manifest route references an unknown input source route");
        if (!manifested_route_ids.insert(route.route_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".route_id",
                       "manifest route IDs must be unique");
        }
        require(report, is_valid_semantic_id(route.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "manifest source-route semantic ID must be canonical");
        if (!manifested_route_semantic_ids.insert(route.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "manifest source-route semantic IDs must be unique");
        }
        require(report, valid_source_route_kind(route.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "manifest source-route kind must be recognized");
        require(report, valid_route_disposition(route.disposition),
                ContractIssueCode::invalid_value, path + ".disposition",
                "manifest route disposition must be specified");
        if (input_route != input_route_by_id.end()) {
            require(report,
                    route.semantic_id == input_route->second->semantic_id &&
                        route.kind == input_route->second->kind,
                    ContractIssueCode::inconsistent_semantics, path,
                    "manifest source-route identity must match the selected inputs");
        }

        const auto required_route =
            required_route_by_semantic_id.find(route.semantic_id);
        require(report, required_route != required_route_by_semantic_id.end(),
                ContractIssueCode::inconsistent_semantics, path + ".semantic_id",
                "manifest contains a source route absent from the selected source "
                "matrix");
        if (required_route != required_route_by_semantic_id.end()) {
            require(report,
                    route.kind == required_route->second->kind &&
                        route.disposition == required_route->second->disposition &&
                        route.disposition_reason ==
                            required_route->second->disposition_reason &&
                        route.artifact_roles == required_route->second->artifact_roles,
                    ContractIssueCode::inconsistent_semantics, path,
                    "manifest source-route disposition, reason, and artifact "
                    "ownership must exactly match the selected source matrix");
        }

        const auto presentation_configured = std::ranges::any_of(
            input_view.presentation->routes,
            [&](const RoutePresentation &presentation_route) {
                return presentation_route.route_id == route.route_id;
            });
        require(report,
                presentation_configured ==
                    (route.disposition == RouteDisposition::rendered),
                ContractIssueCode::inconsistent_semantics, path + ".disposition",
                "exactly rendered selected routes must have a presentation "
                "configuration");

        if (route.disposition == RouteDisposition::rendered) {
            require(report,
                    route.disposition_reason.empty() && !route.artifact_roles.empty(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "rendered route needs artifacts and no not-applicable reason");
        } else if (route.disposition == RouteDisposition::not_applicable) {
            require(report,
                    !route.disposition_reason.empty() && route.artifact_roles.empty(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "not-applicable route needs a reason and cannot name artifacts");
        }
        validate_unique_owned_roles(report, route.artifact_roles,
                                    path + ".artifact_roles");
        for (const auto &role : route.artifact_roles) {
            ++artifact_ownership_count[role];
            const auto artifact = artifact_by_role.find(role);
            require(report,
                    artifact != artifact_by_role.end() &&
                        artifact->second->kind == ArtifactKind::audio,
                    ContractIssueCode::dangling_reference, path + ".artifact_roles",
                    "source routes may own only emitted audio artifacts");
        }
    }
    require(report, content.routes.size() == input_view.routes.size(),
            ContractIssueCode::inconsistent_shape, "routes",
            "manifest must contain exactly one record for every input source route");
    for (const auto &route : input_view.routes) {
        require(report, manifested_route_ids.contains(route.route_id.value),
                ContractIssueCode::missing_value, "routes",
                "manifest must contain every selected input source route");
    }
    for (const auto &required_route : content.output_contract.required_source_routes) {
        require(report,
                manifested_route_semantic_ids.contains(required_route.semantic_id),
                ContractIssueCode::missing_value, "routes",
                "manifest must contain every source-matrix route");
    }

    std::unordered_map<std::string, const OutputBusRequirement *>
        required_bus_by_semantic_id;
    for (const auto &bus : content.output_contract.required_output_buses) {
        required_bus_by_semantic_id.emplace(bus.semantic_id, &bus);
    }
    std::unordered_set<std::string> manifested_bus_semantic_ids;
    for (std::size_t index = 0; index < content.output_buses.size(); ++index) {
        const auto &bus = content.output_buses[index];
        const auto path = "output_buses[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(bus.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "manifest output-bus semantic ID must be canonical");
        if (!manifested_bus_semantic_ids.insert(bus.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "manifest output-bus semantic IDs must be unique");
        }
        require(report, valid_output_bus_kind(bus.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "manifest output-bus kind must be specified");
        const auto required_bus = required_bus_by_semantic_id.find(bus.semantic_id);
        require(report, required_bus != required_bus_by_semantic_id.end(),
                ContractIssueCode::inconsistent_semantics, path + ".semantic_id",
                "manifest contains an output bus absent from the selected source "
                "matrix");
        if (required_bus != required_bus_by_semantic_id.end()) {
            require(report,
                    bus.kind == required_bus->second->kind &&
                        bus.artifact_roles == required_bus->second->artifact_roles,
                    ContractIssueCode::inconsistent_semantics, path,
                    "manifest output bus and artifact ownership must exactly match the "
                    "selected source matrix");
        }
        require(report, !bus.artifact_roles.empty(), ContractIssueCode::missing_value,
                path + ".artifact_roles",
                "manifest output bus must own at least one artifact");
        validate_unique_owned_roles(report, bus.artifact_roles,
                                    path + ".artifact_roles");
        for (const auto &role : bus.artifact_roles) {
            ++artifact_ownership_count[role];
            const auto artifact = artifact_by_role.find(role);
            require(report,
                    artifact != artifact_by_role.end() &&
                        artifact->second->kind == ArtifactKind::audio,
                    ContractIssueCode::dangling_reference, path + ".artifact_roles",
                    "output buses may own only emitted audio artifacts");
        }
    }
    require(report,
            content.output_buses.size() ==
                content.output_contract.required_output_buses.size(),
            ContractIssueCode::inconsistent_shape, "output_buses",
            "manifest output buses must exactly match the selected source matrix");
    for (const auto &required_bus : content.output_contract.required_output_buses) {
        require(report, manifested_bus_semantic_ids.contains(required_bus.semantic_id),
                ContractIssueCode::missing_value, "output_buses",
                "manifest must contain every source-matrix output bus");
    }

    for (const auto &[role, requirement] : requirement_by_role) {
        if (requirement->kind == ArtifactKind::audio) {
            require(report, artifact_ownership_count[role] == 1,
                    ContractIssueCode::inconsistent_semantics, "artifacts",
                    "each required audio artifact must have exactly one physical "
                    "route or output-bus owner");
        }
    }

    append_prefixed(
        report,
        validate_evidence_rights(provenance, content.output_contract.distribution),
        "provenance");

    return report;
}

ValidationReport validate(const ExecutionFacts &execution) {
    using detail::require;

    ValidationReport report;
    require(report, is_valid_semantic_id(execution.run_id),
            ContractIssueCode::invalid_value, "run_id",
            "execution run ID must be canonical");
    require(report, valid_utc_timestamp(execution.started_utc),
            ContractIssueCode::invalid_value, "started_utc",
            "execution start time must use canonical RFC 3339 UTC syntax");
    require(report, execution.wall_elapsed.count() > 0,
            ContractIssueCode::invalid_value, "wall_elapsed",
            "completed render wall time must be positive");
    require(report, !execution.host_os.empty(), ContractIssueCode::missing_value,
            "host_os", "execution host OS must be recorded");
    require(report, !execution.cpu_model.empty(), ContractIssueCode::missing_value,
            "cpu_model", "execution CPU model must be recorded");
    require(report, execution.logical_cpu_count > 0, ContractIssueCode::invalid_value,
            "logical_cpu_count", "logical CPU count must be positive");
    require(report, execution.observed_process_threads > 0,
            ContractIssueCode::invalid_value, "observed_process_threads",
            "observed process thread count must be positive");
    require(report, execution.concurrent_render_jobs > 0,
            ContractIssueCode::invalid_value, "concurrent_render_jobs",
            "concurrent render-job count must be positive");
    if (execution.peak_resident_bytes.has_value()) {
        require(report, *execution.peak_resident_bytes > 0,
                ContractIssueCode::invalid_value, "peak_resident_bytes",
                "recorded peak resident memory must be positive");
    }
    return report;
}

ValidationReport validate(const RenderManifest &manifest,
                          const ProvenanceLedger &provenance,
                          const SourceMatrixContract &source_matrix) {
    ValidationReport report = validate(manifest.content, provenance, source_matrix);
    detail::require(report, manifest.execution.has_value(),
                    ContractIssueCode::missing_value, "execution",
                    "a completed render manifest must include execution facts");
    if (manifest.execution.has_value()) {
        detail::append_prefixed(report, validate(*manifest.execution), "execution");
    }
    return report;
}

} // namespace engine_sim_offline::contract
