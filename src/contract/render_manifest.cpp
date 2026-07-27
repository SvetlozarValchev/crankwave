#include "engine_sim_offline/contract/render_manifest.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::contract {
namespace {

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

std::optional<std::uint64_t> integral_frame_count(double duration_s,
                                                  const RationalRateHz &rate) {
    if (!std::isfinite(duration_s) || duration_s <= 0.0 || rate.numerator == 0 ||
        rate.denominator == 0) {
        return std::nullopt;
    }

    const auto frames = static_cast<long double>(duration_s) *
                        static_cast<long double>(rate.numerator) /
                        static_cast<long double>(rate.denominator);
    const auto exclusive_limit = std::ldexp(1.0L, 64);
    if (!std::isfinite(frames) || frames < 1.0L || frames >= exclusive_limit) {
        return std::nullopt;
    }

    const auto rounded = std::round(frames);
    // A scenario duration is currently encoded as binary64 while its rate is
    // rational. Accommodate only the representational error of that binary64
    // product, not an arbitrary fraction of a delivery frame.
    const auto tolerance =
        8.0L * static_cast<long double>(std::numeric_limits<double>::epsilon()) *
        std::max(1.0L, std::abs(frames));
    if (std::abs(frames - rounded) > tolerance) {
        return std::nullopt;
    }
    if (rounded >= exclusive_limit) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(rounded);
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

} // namespace

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
    append_prefixed(report, validate(source_matrix), "source_matrix");

    const auto &frozen_reference = bmw_m52b28_reference_source_matrix_v1();
    if (source_matrix.id == frozen_reference.id ||
        content.output_contract.source_matrix_id == frozen_reference.id) {
        require(report, source_matrix == frozen_reference,
                ContractIssueCode::inconsistent_semantics, "source_matrix",
                "the frozen BMW reference matrix must exactly match its built-in "
                "approved contract");
    }

    require(report, content.schema_version > 0, ContractIssueCode::invalid_value,
            "schema_version", "render-manifest schema version must be positive");
    append_prefixed(report, validate(content.resolved_inputs.engine, provenance),
                    "resolved_inputs.engine");
    append_prefixed(report,
                    validate(content.resolved_inputs.presentation,
                             content.resolved_inputs.engine,
                             content.resolved_inputs.scenario, provenance),
                    "resolved_inputs.presentation");
    if (source_matrix.id == frozen_reference.id) {
        append_prefixed(
            report,
            validate_p18_reference_presentation(content.resolved_inputs.presentation,
                                                content.resolved_inputs.engine,
                                                content.resolved_inputs.scenario),
            "resolved_inputs.presentation.p18_reference");
    }
    append_prefixed(report, validate(content.resolved_inputs.scenario, provenance),
                    "resolved_inputs.scenario");
    append_prefixed(report,
                    validate_for_engine(content.resolved_inputs.scenario,
                                        content.resolved_inputs.engine),
                    "resolved_inputs");
    require(report, content.rates == content.resolved_inputs.scenario.rates,
            ContractIssueCode::inconsistent_semantics, "rates",
            "manifest rates must equal the resolved scenario rates");
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
            !build.project_revision.empty() && !build.compiler_id.empty() &&
                !build.compiler_version.empty() && !build.target_triple.empty() &&
                !build.standard_library_id.empty() &&
                !build.standard_library_version.empty() &&
                !build.math_library_id.empty() && !build.math_library_version.empty(),
            ContractIssueCode::missing_value, "determinism.build",
            "deterministic build identity must be complete");
    require(report, !build.source_tree_sha256.is_zero(),
            ContractIssueCode::invalid_value, "determinism.build.source_tree_sha256",
            "source-tree digest must be nonzero");
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
            content.randomness.public_seed ==
                content.resolved_inputs.scenario.public_seed.value,
            ContractIssueCode::inconsistent_semantics, "randomness.public_seed",
            "manifest random seed must equal the resolved scenario public seed");
    constexpr std::string_view p18_generator_id = "p18_reference_pcg32_v1";
    if (source_matrix.id == frozen_reference.id) {
        require(report,
                content.randomness.generator.id == p18_generator_id &&
                    content.randomness.generator.version == 1,
                ContractIssueCode::inconsistent_semantics, "randomness.generator",
                "the frozen P1.8 route requires p18_reference_pcg32_v1 version 1");
        require(report,
                content.randomness.derivation.id ==
                        "sha256_length_prefixed_capture_component_pcg32_v1" &&
                    content.randomness.derivation.version == 1,
                ContractIssueCode::inconsistent_semantics, "randomness.derivation",
                "the frozen P1.8 route requires its recorded component-seed "
                "derivation");
        require(report, content.randomness.public_seed == UINT64_C(12648430),
                ContractIssueCode::inconsistent_semantics, "randomness.public_seed",
                "the frozen P1.8 route requires public seed 0xC0FFEE");
    } else {
        require(report, content.randomness.generator.id != p18_generator_id,
                ContractIssueCode::unsupported_value, "randomness.generator",
                "the P1.8 generator is reserved for the frozen reference route");
    }

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
                    std::ranges::any_of(content.resolved_inputs.engine.cylinders,
                                        [&](const CylinderSpec &cylinder) {
                                            return cylinder.id == *seed.cylinder_id;
                                        }),
                    ContractIssueCode::dangling_reference, path + ".cylinder_id",
                    "combustion seed references an unknown cylinder");
        }
        if (seed.route_id.has_value()) {
            const auto route = std::ranges::find(content.resolved_inputs.engine.routes,
                                                 *seed.route_id, &RouteSpec::id);
            require(report, route != content.resolved_inputs.engine.routes.end(),
                    ContractIssueCode::dangling_reference, path + ".route_id",
                    "presentation/starter seed references an unknown route");
            if (route != content.resolved_inputs.engine.routes.end() &&
                seed.kind == RandomComponentKind::starter) {
                require(report,
                        route->kind.value == SourceRouteKind::mechanical_starter,
                        ContractIssueCode::inconsistent_semantics, path + ".route_id",
                        "starter randomness must belong to a starter route");
            }
            if (route != content.resolved_inputs.engine.routes.end() &&
                (seed.kind == RandomComponentKind::presentation_jitter ||
                 seed.kind == RandomComponentKind::presentation_air_noise)) {
                require(report,
                        std::ranges::any_of(
                            content.resolved_inputs.presentation.routes,
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
    const auto &legacy_profile = std::get<LegacyLowOrderV1Profile>(
        content.resolved_inputs.engine.physics_profile);
    if (legacy_profile.fuel.burning_efficiency_randomness_01.value > 0.0) {
        for (const auto &cylinder : content.resolved_inputs.engine.cylinders) {
            require(report, combustion_seed_count[cylinder.id.value] == 1,
                    ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                    "active combustion variation requires exactly one stream "
                    "per cylinder");
        }
    }
    for (const auto &route : content.resolved_inputs.presentation.routes) {
        if (content.resolved_inputs.presentation.conditioning.jitter_scale.value >
            0.0) {
            require(report, jitter_seed_count[route.route_id.value] == 1,
                    ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                    "active presentation jitter requires exactly one stream per "
                    "configured route");
        }
        if (content.resolved_inputs.presentation.conditioning.air_noise_mix_01.value >
            0.0) {
            require(report, air_noise_seed_count[route.route_id.value] == 1,
                    ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                    "active presentation air noise requires exactly one stream per "
                    "configured route");
        }
    }
    if (source_matrix.id == frozen_reference.id) {
        for (const auto &route : content.resolved_inputs.engine.routes) {
            require(report,
                    jitter_seed_count[route.id.value] == 1 &&
                        air_noise_seed_count[route.id.value] == 1,
                    ContractIssueCode::inconsistent_shape, "randomness.component_seeds",
                    "P1.8 requires exactly one jitter and one air-noise stream "
                    "per reference exhaust route");
        }
        for (std::size_t index = 0; index < content.randomness.component_seeds.size();
             ++index) {
            const auto &seed = content.randomness.component_seeds[index];
            if (!seed.route_id.has_value() ||
                (seed.kind != RandomComponentKind::presentation_jitter &&
                 seed.kind != RandomComponentKind::presentation_air_noise)) {
                continue;
            }
            const auto route = std::ranges::find(content.resolved_inputs.engine.routes,
                                                 *seed.route_id, &RouteSpec::id);
            if (route == content.resolved_inputs.engine.routes.end()) {
                continue;
            }

            std::optional<std::pair<std::uint64_t, std::uint64_t>> expected;
            if (route->semantic_id.value == "exhaust.reference.0") {
                expected = seed.kind == RandomComponentKind::presentation_jitter
                               ? std::pair{UINT64_C(0x9e2b91cd0dc51cfc),
                                           UINT64_C(0x1ae6ee3019603abb)}
                               : std::pair{UINT64_C(0x75bc579d4c90a640),
                                           UINT64_C(0x7e4ef6200e7c70c1)};
            } else if (route->semantic_id.value == "exhaust.reference.1") {
                expected = seed.kind == RandomComponentKind::presentation_jitter
                               ? std::pair{UINT64_C(0xdb7540a0c8b54d74),
                                           UINT64_C(0x41ddcdeb066bf214)}
                               : std::pair{UINT64_C(0x208e57f73615bd95),
                                           UINT64_C(0x786d92e584c43b78)};
            }
            require(report,
                    expected.has_value() && seed.initial_state == expected->first &&
                        seed.stream == expected->second,
                    ContractIssueCode::inconsistent_semantics,
                    "randomness.component_seeds[" + std::to_string(index) + "]",
                    "P1.8 presentation stream state/selector must match the frozen "
                    "route record");
        }
    }

    require(report, content.output_contract == resolve_output_contract(source_matrix),
            ContractIssueCode::inconsistent_semantics, "output_contract",
            "resolved output contract must exactly match the selected source matrix");

    const auto expected_frames =
        integral_frame_count(content.resolved_inputs.scenario.audible_duration_s.value,
                             content.rates.delivery);
    require(report, expected_frames.has_value(),
            ContractIssueCode::inconsistent_semantics,
            "resolved_inputs.scenario.audible_duration_s",
            "audible duration and delivery rate must resolve to a positive integral "
            "frame count");

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

    std::unordered_map<std::uint32_t, const RouteSpec *> engine_route_by_id;
    for (const auto &route : content.resolved_inputs.engine.routes) {
        engine_route_by_id.emplace(route.id.value, &route);
    }
    require(report,
            content.resolved_inputs.engine.routes.size() ==
                content.output_contract.required_source_routes.size(),
            ContractIssueCode::inconsistent_shape, "resolved_inputs.engine.routes",
            "resolved engine source routes must exactly match the selected source "
            "matrix");

    std::unordered_set<std::uint32_t> manifested_route_ids;
    std::unordered_set<std::string> manifested_route_semantic_ids;
    std::unordered_map<std::string, std::size_t> artifact_ownership_count;
    for (std::size_t index = 0; index < content.routes.size(); ++index) {
        const auto &route = content.routes[index];
        const auto path = "routes[" + std::to_string(index) + "]";
        const auto engine_route = engine_route_by_id.find(route.route_id.value);
        require(report,
                route.route_id.valid() && engine_route != engine_route_by_id.end(),
                ContractIssueCode::dangling_reference, path + ".route_id",
                "manifest route references an unknown engine source route");
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
        if (engine_route != engine_route_by_id.end()) {
            require(report,
                    route.semantic_id == engine_route->second->semantic_id.value &&
                        route.kind == engine_route->second->kind.value,
                    ContractIssueCode::inconsistent_semantics, path,
                    "manifest source-route identity must match the resolved engine");
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
            content.resolved_inputs.presentation.routes,
            [&](const RoutePresentation &presentation_route) {
                return presentation_route.route_id == route.route_id;
            });
        require(report,
                presentation_configured ==
                    (route.disposition == RouteDisposition::rendered),
                ContractIssueCode::inconsistent_semantics, path + ".disposition",
                "exactly rendered physical routes must have a presentation "
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
    require(report,
            content.routes.size() == content.resolved_inputs.engine.routes.size(),
            ContractIssueCode::inconsistent_shape, "routes",
            "manifest must contain exactly one record for every engine source route");
    for (const auto &route : content.resolved_inputs.engine.routes) {
        require(report, manifested_route_ids.contains(route.id.value),
                ContractIssueCode::missing_value, "routes",
                "manifest must contain every resolved engine source route");
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

    for (const auto &evidence : provenance.evidence) {
        if (evidence.rights == RightsDisposition::prohibited) {
            report.add(ContractIssueCode::unsupported_value, "provenance.evidence",
                       "prohibited evidence cannot participate in a successful render");
        }
        if (content.output_contract.distribution == DistributionIntent::distributable &&
            evidence.rights != RightsDisposition::permitted) {
            report.add(ContractIssueCode::unsupported_value,
                       "output_contract.distribution",
                       "distributable output requires permitted evidence and assets");
        }
    }

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
