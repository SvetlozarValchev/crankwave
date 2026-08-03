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
    case RouteDisposition::declared_silent:
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
        } else if (route.disposition == RouteDisposition::declared_silent) {
            require(report,
                    !route.disposition_reason.empty() && !route.artifact_roles.empty(),
                    ContractIssueCode::inconsistent_semantics, path,
                    "declared-silent routes require an explicit reason and "
                    "diagnostic artifacts");
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
            if (route.disposition == RouteDisposition::declared_silent &&
                artifact != artifact_by_role.end()) {
                require(report, artifact->second->diagnostic,
                        ContractIssueCode::inconsistent_semantics,
                        path + ".artifact_roles",
                        "declared-silent route artifacts must be diagnostic");
            }
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

} // namespace engine_sim_offline::contract
