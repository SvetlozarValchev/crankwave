#pragma once

#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace engine_sim_offline::contract {

struct ReferencePayloadIdentity {
    // Identity of the complete frozen file, including any container/header bytes.
    std::uint64_t byte_count = 0;
    Sha256Digest payload_sha256;

    friend bool operator==(const ReferencePayloadIdentity &,
                           const ReferencePayloadIdentity &) = default;
};

struct ReferenceFixtureIdentityV1 {
    std::string fixture_id;
    std::uint32_t fixture_schema_version = 0;
    // The manifest is the lineage root. Parity is evidence only; M2 must not read it.
    ReferencePayloadIdentity manifest;
    ReferencePayloadIdentity parity_evidence;
    // Audit buses and component seeds are M2 execution inputs.
    ReferencePayloadIdentity audit_input;
    ReferencePayloadIdentity component_seed_input;
    // This is normative method evidence, not an executable blob.
    ReferencePayloadIdentity renderer_algorithm_record;
    // The configured source IR is an M2 execution input.
    ReferencePayloadIdentity configured_ir_input;
    // This must be regenerated and compared, never used as the convolution input.
    ReferencePayloadIdentity kernel_oracle_comparator;

    friend bool operator==(const ReferenceFixtureIdentityV1 &,
                           const ReferenceFixtureIdentityV1 &) = default;
};

struct ReferenceRouteIdentity {
    RouteId route_id;
    std::string semantic_id;
    // This is only the frozen source-matrix routing classification. The captured
    // reference bus is not asserted to be a physical exhaust-outlet observable.
    SourceRouteKind source_matrix_classification = SourceRouteKind::unspecified;

    friend bool operator==(const ReferenceRouteIdentity &,
                           const ReferenceRouteIdentity &) = default;
};

struct ReferenceEngineContext {
    // These identify only the narrow presentation context, not an executed physics
    // model. Route IDs are repository-local and do not claim source runtime indices.
    std::string engine_id;
    std::string engine_profile_id;
    std::vector<ReferenceRouteIdentity> routes;

    friend bool operator==(const ReferenceEngineContext &,
                           const ReferenceEngineContext &) = default;
};

struct ReferenceCaptureWindow {
    RenderRates rates;
    std::uint64_t record_count = 0;
    std::uint64_t consumed_start_record = 0;
    std::uint64_t consumed_end_record_exclusive = 0;
    std::uint64_t audible_start_record = 0;
    std::uint64_t audible_end_record_exclusive = 0;
    std::uint64_t block_frames = 0;
    std::uint64_t total_source_frames = 0;
    std::uint64_t audible_source_start_frame = 0;
    std::uint64_t audible_source_end_frame_exclusive = 0;
    std::uint64_t delivery_frame_count = 0;
    std::uint64_t public_seed = 0;

    friend bool operator==(const ReferenceCaptureWindow &,
                           const ReferenceCaptureWindow &) = default;
};

struct ReferencePresentationInputsV1 {
    std::uint32_t schema_version = 0;
    ReferenceFixtureIdentityV1 fixture;
    MethodIdentity audit_reader;
    MethodIdentity excitation_adapter;
    std::string audit_lane_semantic_id;
    MethodIdentity excitation_seam;
    ReferenceEngineContext engine;
    ReferenceCaptureWindow capture;
    PresentationCalibration presentation;

    friend bool operator==(const ReferencePresentationInputsV1 &,
                           const ReferencePresentationInputsV1 &) = default;
};

[[nodiscard]] ValidationReport validate(const ReferencePresentationInputsV1 &inputs,
                                        const ProvenanceLedger &provenance,
                                        const SourceMatrixContract &source_matrix);

} // namespace engine_sim_offline::contract
