#include "engine_sim_offline/contract/source_matrix.hpp"

#include "validation_support.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

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

bool valid_distribution_intent(DistributionIntent distribution) {
    switch (distribution) {
    case DistributionIntent::local_evaluation:
    case DistributionIntent::distributable:
        return true;
    case DistributionIntent::unspecified:
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

bool valid_omission_kind(OmissionKind kind) {
    switch (kind) {
    case OmissionKind::source_route:
    case OmissionKind::external_system:
    case OmissionKind::presentation_scene:
    case OmissionKind::scenario_behavior:
        return true;
    case OmissionKind::unspecified:
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

ValidationReport validate(const SourceMatrixContract &source_matrix) {
    using detail::require;

    ValidationReport report;
    require(report, is_valid_semantic_id(source_matrix.id),
            ContractIssueCode::invalid_value, "id",
            "source-matrix ID must be a canonical semantic ID");
    require(report, !source_matrix.sha256.is_zero(), ContractIssueCode::invalid_value,
            "sha256", "source-matrix digest must be nonzero");
    require(report, valid_distribution_intent(source_matrix.distribution),
            ContractIssueCode::invalid_value, "distribution",
            "source-matrix distribution intent must be specified");
    require(report, !source_matrix.required_source_routes.empty(),
            ContractIssueCode::missing_value, "required_source_routes",
            "source matrix must declare its physical source routes");
    require(report, !source_matrix.required_artifacts.empty(),
            ContractIssueCode::missing_value, "required_artifacts",
            "source matrix must declare at least one required artifact");

    std::unordered_map<std::string, const ArtifactRequirement *> artifact_by_role;
    for (std::size_t index = 0; index < source_matrix.required_artifacts.size();
         ++index) {
        const auto &artifact = source_matrix.required_artifacts[index];
        const auto path = "required_artifacts[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(artifact.role),
                ContractIssueCode::invalid_value, path + ".role",
                "artifact role must be a canonical semantic ID");
        if (!artifact_by_role.emplace(artifact.role, &artifact).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".role",
                       "required artifact roles must be unique");
        }
        require(report, valid_artifact_kind(artifact.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "artifact kind must be specified");
        require(report,
                (artifact.kind == ArtifactKind::audio) == artifact.audio.has_value(),
                ContractIssueCode::inconsistent_semantics, path + ".audio",
                "exactly audio artifacts carry an audio contract");
        if (artifact.audio.has_value()) {
            validate_audio_contract(report, *artifact.audio, path + ".audio");
        }
    }

    std::unordered_map<std::string, std::size_t> ownership_count;
    std::unordered_set<std::string> owner_semantic_ids;
    for (std::size_t index = 0; index < source_matrix.required_source_routes.size();
         ++index) {
        const auto &route = source_matrix.required_source_routes[index];
        const auto path = "required_source_routes[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(route.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "source-route semantic ID must be canonical");
        if (!owner_semantic_ids.insert(route.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "source-route and output-bus semantic IDs must be unique");
        }
        require(report, valid_source_route_kind(route.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "source matrix accepts physical source-route kinds only");
        require(report, valid_route_disposition(route.disposition),
                ContractIssueCode::invalid_value, path + ".disposition",
                "source-route disposition must be specified");
        if (route.disposition == RouteDisposition::rendered) {
            require(report,
                    route.disposition_reason.empty() && !route.artifact_roles.empty(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "rendered routes require artifacts and no omission reason");
        } else if (route.disposition == RouteDisposition::not_applicable) {
            require(report,
                    !route.disposition_reason.empty() && route.artifact_roles.empty(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "not-applicable routes require a policy-owned reason and "
                    "cannot own artifacts");
        }
        validate_unique_owned_roles(report, route.artifact_roles,
                                    path + ".artifact_roles");
        for (const auto &role : route.artifact_roles) {
            ++ownership_count[role];
            const auto artifact = artifact_by_role.find(role);
            require(report,
                    artifact != artifact_by_role.end() &&
                        artifact->second->kind == ArtifactKind::audio,
                    ContractIssueCode::dangling_reference, path + ".artifact_roles",
                    "source routes may own only declared audio artifacts");
        }
    }

    for (std::size_t index = 0; index < source_matrix.required_output_buses.size();
         ++index) {
        const auto &bus = source_matrix.required_output_buses[index];
        const auto path = "required_output_buses[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(bus.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "output-bus semantic ID must be canonical");
        if (!owner_semantic_ids.insert(bus.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "source-route and output-bus semantic IDs must be unique");
        }
        require(report, valid_output_bus_kind(bus.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "output-bus kind must be specified");
        require(report, !bus.artifact_roles.empty(), ContractIssueCode::missing_value,
                path + ".artifact_roles",
                "every required output bus must own at least one artifact");
        validate_unique_owned_roles(report, bus.artifact_roles,
                                    path + ".artifact_roles");
        for (const auto &role : bus.artifact_roles) {
            ++ownership_count[role];
            const auto artifact = artifact_by_role.find(role);
            require(report,
                    artifact != artifact_by_role.end() &&
                        artifact->second->kind == ArtifactKind::audio,
                    ContractIssueCode::dangling_reference, path + ".artifact_roles",
                    "output buses may own only declared audio artifacts");
        }
    }

    std::unordered_set<std::string> omission_ids;
    for (std::size_t index = 0; index < source_matrix.declared_omissions.size();
         ++index) {
        const auto &omission = source_matrix.declared_omissions[index];
        const auto path = "declared_omissions[" + std::to_string(index) + "]";
        require(report, is_valid_semantic_id(omission.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "omission ID must be canonical");
        if (!omission_ids.insert(omission.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "declared omission IDs must be unique");
        }
        require(report, !owner_semantic_ids.contains(omission.semantic_id),
                ContractIssueCode::inconsistent_semantics, path + ".semantic_id",
                "a matrix entry cannot be both required and explicitly omitted");
        require(report, valid_omission_kind(omission.kind),
                ContractIssueCode::invalid_value, path + ".kind",
                "omission kind must be specified");
        require(report, !omission.rationale.empty(), ContractIssueCode::missing_value,
                path + ".rationale", "an explicit omission requires a rationale");
    }

    for (const auto &[role, artifact] : artifact_by_role) {
        if (artifact->kind == ArtifactKind::audio) {
            require(report, ownership_count[role] == 1,
                    ContractIssueCode::inconsistent_semantics, "required_artifacts",
                    "each required audio artifact must have exactly one route or bus "
                    "owner");
        }
    }
    return report;
}

ValidationReport validate_evidence_rights(const ProvenanceLedger &provenance,
                                          DistributionIntent distribution) {
    ValidationReport report;
    for (std::size_t index = 0; index < provenance.evidence.size(); ++index) {
        const auto &evidence = provenance.evidence[index];
        const auto path = "evidence[" + std::to_string(index) + "].rights";
        if (evidence.rights == RightsDisposition::prohibited) {
            report.add(ContractIssueCode::unsupported_value, path,
                       "prohibited evidence cannot participate in a successful "
                       "render");
        }
        if (distribution == DistributionIntent::distributable &&
            evidence.rights != RightsDisposition::permitted) {
            report.add(ContractIssueCode::unsupported_value, path,
                       "distributable output requires permitted evidence and "
                       "assets");
        }
    }
    return report;
}

const SourceMatrixContract &bmw_m52b28_reference_source_matrix_v1() {
    static const SourceMatrixContract source_matrix = [] {
        constexpr RationalRateHz delivery_rate{192000, 1};
        constexpr std::uint64_t frame_count = 2880000;
        const AudioContract float32_mono{
            delivery_rate,
            frame_count,
            "mono",
            "float32le",
        };
        const AudioContract pcm24_mono{
            delivery_rate,
            frame_count,
            "mono",
            "pcm_s24le",
        };

        SourceMatrixContract result;
        result.id = "bmw-m52b28-reference-source-matrix-v1";
        result.sha256.bytes = {
            0xc7, 0x9a, 0x07, 0x1f, 0xd8, 0xaf, 0xe4, 0x6c, 0xab, 0x0b, 0x67,
            0xb4, 0x04, 0x53, 0x71, 0x79, 0x26, 0x18, 0x24, 0xc0, 0xba, 0xd3,
            0xa5, 0xfc, 0xc0, 0x71, 0xe0, 0x59, 0xc9, 0x9f, 0xd9, 0x11,
        };
        result.distribution = DistributionIntent::local_evaluation;
        result.required_source_routes = {
            {
                "exhaust.reference.0",
                SourceRouteKind::exhaust_outlet,
                RouteDisposition::rendered,
                "",
                {
                    "exhaust.reference.0.dry",
                    "exhaust.reference.0.configured_ir",
                    "exhaust.reference.0.selected",
                },
            },
            {
                "exhaust.reference.1",
                SourceRouteKind::exhaust_outlet,
                RouteDisposition::rendered,
                "",
                {
                    "exhaust.reference.1.dry",
                    "exhaust.reference.1.configured_ir",
                    "exhaust.reference.1.selected",
                },
            },
        };
        result.required_output_buses = {
            {
                "master.reference.raw",
                OutputBusKind::master_reference_raw,
                {"master.reference.raw"},
            },
            {
                "master.reference.audition",
                OutputBusKind::master_reference_audition,
                {"master.reference.audition"},
            },
        };
        result.required_artifacts = {
            {"exhaust.reference.0.dry", ArtifactKind::audio, float32_mono, true},
            {"exhaust.reference.0.configured_ir", ArtifactKind::audio, float32_mono,
             true},
            {"exhaust.reference.0.selected", ArtifactKind::audio, float32_mono, false},
            {"exhaust.reference.1.dry", ArtifactKind::audio, float32_mono, true},
            {"exhaust.reference.1.configured_ir", ArtifactKind::audio, float32_mono,
             true},
            {"exhaust.reference.1.selected", ArtifactKind::audio, float32_mono, false},
            {"master.reference.raw", ArtifactKind::audio, float32_mono, false},
            {"master.reference.audition", ArtifactKind::audio, pcm24_mono, false},
        };
        result.declared_omissions = {
            {
                "intake",
                OmissionKind::source_route,
                "Absent from the exhaust-only reference; it cannot be inferred from "
                "the master.",
            },
            {
                "mechanical.engine",
                OmissionKind::source_route,
                "Absent as a separately observable reference route.",
            },
            {
                "mechanical.starter",
                OmissionKind::source_route,
                "The preserved pull begins with an already-running engine.",
            },
            {
                "drivetrain",
                OmissionKind::external_system,
                "Transmission and drivetrain radiation are outside this neutral "
                "engine reference.",
            },
            {
                "vehicle.tire-road",
                OmissionKind::external_system,
                "Tire, road, and vehicle radiation are outside the engine asset.",
            },
            {
                "presentation.spatial-field",
                OmissionKind::presentation_scene,
                "The static IR is coloration, not a documented cabin, environment, "
                "microphone, or spatial field.",
            },
            {
                "scenario.non-pull-behaviors",
                OmissionKind::scenario_behavior,
                "Startup, shutdown, idle, overrun, fuel cut, and limiter behavior are "
                "not exercised by this pull.",
            },
        };
        return result;
    }();
    return source_matrix;
}

} // namespace engine_sim_offline::contract
