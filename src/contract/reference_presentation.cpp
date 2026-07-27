#include "engine_sim_offline/contract/reference_presentation.hpp"

#include "validation_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace engine_sim_offline::contract {
namespace {

Sha256Digest digest_from_hex(std::string_view text) {
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9') {
            return static_cast<std::uint8_t>(value - '0');
        }
        return static_cast<std::uint8_t>(10 + value - 'a');
    };

    Sha256Digest digest;
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        digest.bytes[index] = static_cast<std::uint8_t>(
            (nibble(text[index * 2]) << 4U) | nibble(text[index * 2 + 1]));
    }
    return digest;
}

void validate_reference_payload(ValidationReport &report,
                                const ReferencePayloadIdentity &actual,
                                std::uint64_t expected_byte_count,
                                std::string_view expected_sha256,
                                const std::string &path) {
    detail::require(report, actual.byte_count == expected_byte_count,
                    ContractIssueCode::inconsistent_semantics, path + ".byte_count",
                    "reference payload byte count must match the frozen fixture");
    detail::require(report, actual.payload_sha256 == digest_from_hex(expected_sha256),
                    ContractIssueCode::inconsistent_semantics, path + ".payload_sha256",
                    "reference payload digest must match the frozen fixture");
}

PresentationValidationContext
make_presentation_context(const ReferencePresentationInputsV1 &inputs) {
    PresentationValidationContext context;
    context.engine_profile_id = inputs.engine.engine_profile_id;
    context.routes.reserve(inputs.engine.routes.size());
    for (const auto &route : inputs.engine.routes) {
        context.routes.push_back(
            {route.route_id, route.semantic_id, route.source_matrix_classification});
    }
    context.rates = inputs.capture.rates;
    const auto capture_rate =
        static_cast<double>(inputs.capture.rates.capture.numerator) /
        static_cast<double>(inputs.capture.rates.capture.denominator);
    if (capture_rate > 0.0) {
        context.total_duration_s =
            static_cast<double>(inputs.capture.record_count) / capture_rate;
        context.audible_start_s =
            static_cast<double>(inputs.capture.audible_start_record) / capture_rate;
        if (inputs.capture.audible_end_record_exclusive >=
            inputs.capture.audible_start_record) {
            context.audible_duration_s =
                static_cast<double>(inputs.capture.audible_end_record_exclusive -
                                    inputs.capture.audible_start_record) /
                capture_rate;
        }
    }
    return context;
}

} // namespace

ValidationReport validate(const ReferencePresentationInputsV1 &inputs,
                          const ProvenanceLedger &provenance,
                          const SourceMatrixContract &source_matrix) {
    using detail::append_prefixed;
    using detail::require;

    ValidationReport report;
    append_prefixed(report, validate(provenance), "provenance");
    append_prefixed(report, validate(source_matrix), "source_matrix");

    require(report, inputs.schema_version == 1, ContractIssueCode::unsupported_value,
            "schema_version", "reference-presentation input schema must be version 1");
    require(report,
            inputs.fixture.fixture_id == "bmw-m52b28-p18-reference-capture-v1" &&
                inputs.fixture.fixture_schema_version == 1,
            ContractIssueCode::inconsistent_semantics, "fixture",
            "reference presentation requires the frozen BMW P1.8 fixture identity");
    validate_reference_payload(
        report, inputs.fixture.manifest, UINT64_C(21435),
        "52d694ba6edc8771b5a4c394d5b62573c22b38e8ba4ef7e2f5bc8c8fb6decc07",
        "fixture.manifest");
    validate_reference_payload(
        report, inputs.fixture.parity_evidence, UINT64_C(38080608),
        "19d351b54c8eb8b509cd72ea03061b01f92722cbfa48d27a2342ca7203ffa94c",
        "fixture.parity_evidence");
    validate_reference_payload(
        report, inputs.fixture.audit_input, UINT64_C(21760064),
        "93fbaef5fe887ba229d7acc28235d63c98f9205d2fe7e426a3e501473a2643a4",
        "fixture.audit_input");
    validate_reference_payload(
        report, inputs.fixture.component_seed_input, UINT64_C(216),
        "ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f",
        "fixture.component_seed_input");
    validate_reference_payload(
        report, inputs.fixture.renderer_algorithm_record, UINT64_C(20832),
        "0e6b1183d421088b4d0b49ea96545034b5ef338363e5ae2e30d81c182c96a008",
        "fixture.renderer_algorithm_record");
    validate_reference_payload(
        report, inputs.fixture.configured_ir_input, UINT64_C(78602),
        "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc",
        "fixture.configured_ir_input");
    validate_reference_payload(
        report, inputs.fixture.kernel_oracle_comparator, UINT64_C(240568),
        "940e3f585cbdf34df6e9073db629c02b585d6e09c4d3c31a393eb3759f357598",
        "fixture.kernel_oracle_comparator");

    append_prefixed(report, validate(inputs.audit_reader), "audit_reader");
    append_prefixed(report, validate(inputs.excitation_adapter), "excitation_adapter");
    append_prefixed(report, validate(inputs.excitation_seam), "excitation_seam");
    require(report,
            inputs.audit_reader.id == "p18-reference-audit-reader-v1" &&
                inputs.audit_reader.version == 1,
            ContractIssueCode::inconsistent_semantics, "audit_reader",
            "reference audit reader method must match the frozen semantic contract");
    require(report,
            inputs.excitation_adapter.id ==
                    "p18-reference-audit-excitation-adapter-v1" &&
                inputs.excitation_adapter.version == 1,
            ContractIssueCode::inconsistent_semantics, "excitation_adapter",
            "reference excitation adapter must match the frozen semantic contract");
    require(report,
            inputs.audit_lane_semantic_id == "legacy_reference.exhaust_bus_pre_dsp",
            ContractIssueCode::inconsistent_semantics, "audit_lane_semantic_id",
            "reference audit lane must match the frozen route");
    require(report,
            inputs.excitation_seam.id == "exhaust-excitation-block-v1" &&
                inputs.excitation_seam.version == 1,
            ContractIssueCode::inconsistent_semantics, "excitation_seam",
            "typed excitation seam must match the frozen semantic contract");

    require(report,
            inputs.engine.engine_id == "bmw-m52b28" &&
                inputs.engine.engine_profile_id == "bmw-m52b28-p18-reference",
            ContractIssueCode::inconsistent_semantics, "engine",
            "reference presentation must identify the frozen BMW M52B28 context");
    require(report, inputs.engine.routes.size() == 2,
            ContractIssueCode::inconsistent_shape, "engine.routes",
            "reference presentation requires exactly two source routes");
    std::unordered_set<std::uint32_t> route_ids;
    std::unordered_set<std::string> route_semantic_ids;
    for (std::size_t index = 0; index < inputs.engine.routes.size(); ++index) {
        const auto &route = inputs.engine.routes[index];
        const auto path = "engine.routes[" + std::to_string(index) + "]";
        require(report, route.route_id.valid(), ContractIssueCode::invalid_value,
                path + ".route_id", "reference route ID must be nonzero");
        if (!route_ids.insert(route.route_id.value).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".route_id",
                       "reference route IDs must be unique");
        }
        require(report, is_valid_semantic_id(route.semantic_id),
                ContractIssueCode::invalid_value, path + ".semantic_id",
                "reference route semantic ID must be canonical");
        if (!route_semantic_ids.insert(route.semantic_id).second) {
            report.add(ContractIssueCode::duplicate_identity, path + ".semantic_id",
                       "reference route semantic IDs must be unique");
        }
        const auto expected_semantic_id =
            index == 0 ? "exhaust.reference.0" : "exhaust.reference.1";
        require(report,
                route.route_id == RouteId{static_cast<std::uint32_t>(index + 1)} &&
                    route.semantic_id == expected_semantic_id &&
                    route.source_matrix_classification ==
                        SourceRouteKind::exhaust_outlet,
                ContractIssueCode::inconsistent_semantics, path,
                "reference routes must use local stable IDs 1/2 for the frozen "
                "ordered bus pair and its source-matrix classifications");
    }

    const RenderRates expected_rates{
        {10000, 1}, {10000, 1}, {192000, 1}, {192000, 1}, {192000, 1},
    };
    append_prefixed(report, validate(inputs.capture.rates), "capture.rates");
    require(report,
            inputs.capture.rates == expected_rates &&
                inputs.capture.record_count == UINT64_C(170000) &&
                inputs.capture.consumed_start_record == 0 &&
                inputs.capture.consumed_end_record_exclusive == UINT64_C(170000) &&
                inputs.capture.audible_start_record == UINT64_C(20000) &&
                inputs.capture.audible_end_record_exclusive == UINT64_C(170000) &&
                inputs.capture.block_frames == UINT64_C(200) &&
                inputs.capture.total_source_frames == UINT64_C(3264000) &&
                inputs.capture.audible_source_start_frame == UINT64_C(384000) &&
                inputs.capture.audible_source_end_frame_exclusive ==
                    UINT64_C(3264000) &&
                inputs.capture.delivery_frame_count == UINT64_C(2880000) &&
                inputs.capture.public_seed == UINT64_C(12648430),
            ContractIssueCode::inconsistent_semantics, "capture",
            "reference capture clocks, consumed windows, block size, frame counts, "
            "and seed must match the frozen fixture");

    const auto &frozen_matrix = bmw_m52b28_reference_source_matrix_v1();
    require(report, source_matrix == frozen_matrix,
            ContractIssueCode::inconsistent_semantics, "source_matrix",
            "reference-presentation inputs require the exact frozen BMW matrix");
    for (std::size_t index = 0; index < inputs.engine.routes.size() &&
                                index < frozen_matrix.required_source_routes.size();
         ++index) {
        const auto &route = inputs.engine.routes[index];
        const auto &required = frozen_matrix.required_source_routes[index];
        require(report,
                route.semantic_id == required.semantic_id &&
                    route.source_matrix_classification == required.kind &&
                    required.disposition == RouteDisposition::rendered,
                ContractIssueCode::inconsistent_semantics,
                "engine.routes[" + std::to_string(index) + "]",
                "reference route context and routing classification must exactly "
                "match the frozen source matrix");
    }

    const auto presentation_context = make_presentation_context(inputs);
    require(report,
            inputs.presentation.calibration_id == "bmw-m52b28-p18-presentation-v1" &&
                inputs.presentation.engine_profile_id.value ==
                    inputs.engine.engine_profile_id,
            ContractIssueCode::inconsistent_semantics, "presentation",
            "reference presentation calibration and engine context must match");
    require(report,
            inputs.presentation.algorithm_record.content_sha256.value ==
                inputs.fixture.renderer_algorithm_record.payload_sha256,
            ContractIssueCode::inconsistent_semantics,
            "presentation.algorithm_record.content_sha256",
            "presentation algorithm record must equal its frozen fixture identity");
    require(report,
            inputs.presentation.assets.size() == 1 &&
                inputs.presentation.assets.front().content_sha256.value ==
                    inputs.fixture.configured_ir_input.payload_sha256,
            ContractIssueCode::inconsistent_semantics, "presentation.assets",
            "presentation IR asset must equal the frozen execution input");
    append_prefixed(report,
                    validate(inputs.presentation, presentation_context, provenance),
                    "presentation");
    append_prefixed(
        report,
        validate_p18_reference_presentation(inputs.presentation, presentation_context),
        "presentation.p18_reference");
    return report;
}

} // namespace engine_sim_offline::contract
