#include "dsp/fixed_fft.hpp"
#include "dsp/source_conditioning_primitives.hpp"
#include "dsp/static_ir_conversion.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"
#include "presentation/causal_reconstruction.hpp"
#include "presentation/exhaust_source_stage.hpp"
#include "presentation/mastering.hpp"
#include "presentation/pcm16_ir_decoder.hpp"
#include "presentation/presentation_render_session.hpp"
#include "reference/p18_reference_audit_reader.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_seed_reader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace {

constexpr std::uint64_t kFixtureAudibleFrameCount = 2'880'000;
constexpr std::uint64_t kFixtureFadeFrameCount = 3'840;

using namespace engine_sim_offline;
using namespace engine_sim_offline::reference;

static_assert(
    static_cast<std::size_t>(P18ReferenceLineageFile::kernel_oracle_comparator) + 1U ==
    kP18ReferenceLineageFileCount);
static_assert(static_cast<std::size_t>(P18ReferenceRoute::exhaust_1) + 1U ==
              kP18ReferenceRouteCount);
static_assert(static_cast<std::size_t>(P18ReferencePresentationMethod::audition_mix) +
                  1U ==
              kP18ReferencePresentationMethodCount);
static_assert(static_cast<std::size_t>(P18ReferenceAudioArtifact::master_audition) +
                  1U ==
              kP18ReferenceAudioArtifactCount);
static_assert(static_cast<std::size_t>(P18ReferenceMasteringPayload::pcm24le) + 1U ==
              kP18ReferenceMasteringPayloadCount);
static_assert(kP18ReferenceExecutedSeedCount ==
              kP18ReferenceAirNoiseSeedCount + kP18ReferenceJitterSeedCount);
static_assert((static_cast<std::size_t>(P18ReferenceExecutedRandomComponent::jitter) +
               1U) *
                  kP18ReferenceRouteCount ==
              kP18ReferenceExecutedSeedCount);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool valid_relative_path(std::string_view value) {
    return !value.empty() && value.front() != '/' && value.back() != '/' &&
           value.find('\\') == std::string_view::npos &&
           value.find("//") == std::string_view::npos && value != "." &&
           value != ".." && !value.starts_with("../") &&
           value.find("/../") == std::string_view::npos && !value.ends_with("/..");
}

void expect_method(const P18ExpectedSemanticMethod &method) {
    expect(contract::is_valid_semantic_id(method.expected_id),
           "catalog method ID is not canonical");
    expect(method.expected_version != 0, "catalog method version is zero");
}

void test_ordered_complete_inventory(const P18ReferenceCatalogV1 &catalog) {
    std::unordered_set<std::string_view> lineage_paths;
    for (std::size_t index = 0; index < catalog.expected_lineage_files.size();
         ++index) {
        const auto &file = catalog.expected_lineage_files[index];
        expect(static_cast<std::size_t>(file.file) == index,
               "lineage catalog is not ordered by its enum");
        expect(valid_relative_path(file.expected_relative_path),
               "lineage catalog contains an invalid relative path");
        expect(lineage_paths.insert(file.expected_relative_path).second,
               "lineage catalog contains a duplicate path");
        expect(file.expected_byte_count != 0, "lineage catalog contains a zero size");
        expect(!file.expected_sha256.is_zero(),
               "lineage catalog contains a zero digest");
    }

    std::unordered_set<std::uint32_t> route_ids;
    std::unordered_set<std::string_view> route_semantic_ids;
    for (std::size_t index = 0; index < catalog.expected_routes.size(); ++index) {
        const auto &route = catalog.expected_routes[index];
        expect(static_cast<std::size_t>(route.route) == index,
               "route catalog is not ordered by its enum");
        expect(route.expected_route_id.valid() &&
                   route_ids.insert(route.expected_route_id.value).second,
               "route catalog contains an invalid or duplicate stable ID");
        expect(contract::is_valid_semantic_id(route.expected_semantic_id) &&
                   route_semantic_ids.insert(route.expected_semantic_id).second,
               "route catalog contains an invalid or duplicate semantic ID");
        expect(route.expected_source_matrix_classification !=
                   contract::SourceRouteKind::unspecified,
               "route catalog contains an unspecified classification");
    }

    for (std::size_t index = 0;
         index < catalog.expected_presentation.expected_methods.size(); ++index) {
        const auto &method = catalog.expected_presentation.expected_methods[index];
        expect(static_cast<std::size_t>(method.method) == index,
               "presentation methods are not ordered by their enum");
        expect_method(method.expected_semantic);
    }
    for (std::size_t index = 0;
         index < catalog.expected_presentation.expected_audition_route_order.size();
         ++index) {
        expect(
            static_cast<std::size_t>(
                catalog.expected_presentation.expected_audition_route_order[index]) ==
                index,
            "audition route order is not exhaustive canonical route order");
    }

    std::unordered_set<std::string_view> audio_roles;
    std::unordered_set<std::string_view> audio_paths;
    for (std::size_t index = 0; index < catalog.expected_audio.size(); ++index) {
        const auto &audio = catalog.expected_audio[index];
        expect(static_cast<std::size_t>(audio.audio) == index,
               "audio catalog is not ordered by its enum");
        expect(catalog.find_expected_audio(audio.audio) == &audio,
               "checked audio lookup does not preserve canonical enum order");
        expect(contract::is_valid_semantic_id(audio.expected_role) &&
                   audio_roles.insert(audio.expected_role).second,
               "audio catalog contains an invalid or duplicate role");
        expect(valid_relative_path(audio.expected_relative_path) &&
                   audio_paths.insert(audio.expected_relative_path).second,
               "audio catalog contains an invalid or duplicate path");
        expect(audio.expected_byte_count != 0, "audio catalog contains a zero size");
        expect(!audio.expected_sha256.is_zero(),
               "audio catalog contains a zero digest");
    }
    for (std::size_t index = 0; index < catalog.expected_mastering.size(); ++index) {
        const auto &payload = catalog.expected_mastering[index];
        expect(static_cast<std::size_t>(payload.payload) == index,
               "mastering catalog is not ordered by its enum");
        expect(catalog.find_expected_mastering(payload.payload) == &payload,
               "checked mastering lookup does not preserve canonical enum order");
        expect(!payload.expected_sha256.is_zero(),
               "mastering catalog contains a zero digest");
    }
    expect(catalog.find_expected_audio(static_cast<P18ReferenceAudioArtifact>(
               kP18ReferenceAudioArtifactCount)) == nullptr,
           "checked audio lookup accepted an out-of-range enum");
    expect(catalog.find_expected_mastering(static_cast<P18ReferenceMasteringPayload>(
               kP18ReferenceMasteringPayloadCount)) == nullptr,
           "checked mastering lookup accepted an out-of-range enum");
}

void test_identity_and_seed_shape(const P18ReferenceCatalogV1 &catalog) {
    expect(catalog.expected_reference_inputs_schema_version != 0 &&
               catalog.expected_fixture_schema_version != 0 &&
               catalog.expected_presentation.expected_schema_version != 0,
           "catalog schema version is zero");
    for (const auto id :
         {catalog.expected_fixture_id, catalog.expected_engine_id,
          catalog.expected_engine_profile_id,
          catalog.expected_presentation_calibration_id,
          catalog.expected_audit_lane_semantic_id,
          catalog.expected_presentation.expected_algorithm_record_semantic_id,
          catalog.expected_presentation.expected_algorithm_record_evidence_source_id}) {
        expect(contract::is_valid_semantic_id(id),
               "catalog contains a noncanonical identity");
    }
    expect_method(catalog.expected_audit_reader);
    expect_method(catalog.expected_excitation_adapter);
    expect_method(catalog.expected_excitation_seam);
    expect_method(catalog.expected_random_generator);
    expect_method(catalog.expected_seed_derivation);

    constexpr std::array expected_components{
        P18ReferenceExecutedRandomComponent::air_noise,
        P18ReferenceExecutedRandomComponent::air_noise,
        P18ReferenceExecutedRandomComponent::jitter,
        P18ReferenceExecutedRandomComponent::jitter,
    };
    constexpr std::array expected_routes{
        P18ReferenceRoute::exhaust_0,
        P18ReferenceRoute::exhaust_1,
        P18ReferenceRoute::exhaust_0,
        P18ReferenceRoute::exhaust_1,
    };
    std::unordered_set<std::uint64_t> streams;
    for (std::size_t index = 0; index < catalog.expected_executed_seeds.size();
         ++index) {
        const auto &seed = catalog.expected_executed_seeds[index];
        expect(seed.component == expected_components[index] &&
                   seed.route == expected_routes[index],
               "executed seeds are not in component-then-route order");
        expect(seed.expected_initial_state != 0 && seed.expected_stream != 0 &&
                   seed.expected_stream <= kP18ReferenceMaximumPcgStream,
               "executed seed is zero or noncanonical");
        expect(streams.insert(seed.expected_stream).second,
               "executed seed streams are not unique");
    }
}

void test_subsystem_constant_alignment(const P18ReferenceCatalogV1 &catalog) {
    const auto &capture = catalog.expected_capture;
    const contract::RationalRateHz source_rate{
        presentation::CausalReconstruction::kSourceRate, 1};
    expect(contract::validate(capture.expected_rates).ok(),
           "catalog render rates are invalid");
    expect(capture.expected_rates.physics == presentation::kExcitationRateHz &&
               capture.expected_rates.capture == presentation::kExcitationRateHz &&
               capture.expected_rates.source_processing == source_rate &&
               capture.expected_rates.acoustic == source_rate &&
               capture.expected_rates.delivery == source_rate,
           "catalog rates differ from source-stage rates");
    expect(capture.expected_record_count == kP18ReferenceAuditRecordCount &&
               capture.expected_consumed_start_record ==
                   kP18ReferenceAuditIntervalStart &&
               capture.expected_consumed_end_record_exclusive ==
                   kP18ReferenceAuditIntervalEndExclusive &&
               capture.expected_physics_frames_per_block ==
                   presentation::kExcitationFramesPerMethodBlock &&
               capture.expected_source_frames_per_block ==
                   presentation::kSourceFramesPerMethodBlock &&
               capture.expected_public_seed != 0,
           "catalog capture differs from decoder or source-stage shape");
    expect(capture.expected_consumed_start_record <
                   capture.expected_audible_start_record &&
               capture.expected_audible_start_record <
                   capture.expected_audible_end_record_exclusive &&
               capture.expected_audible_end_record_exclusive ==
                   capture.expected_consumed_end_record_exclusive &&
               capture.expected_record_count ==
                   capture.expected_consumed_end_record_exclusive -
                       capture.expected_consumed_start_record &&
               (capture.expected_audible_start_record -
                capture.expected_consumed_start_record) /
                       capture.expected_physics_frames_per_block *
                       capture.expected_source_frames_per_block ==
                   capture.expected_audible_source_start_frame &&
               capture.expected_record_count /
                       capture.expected_physics_frames_per_block *
                       capture.expected_source_frames_per_block ==
                   capture.expected_total_source_frame_count &&
               capture.expected_audible_source_end_frame_exclusive -
                       capture.expected_audible_source_start_frame ==
                   capture.expected_delivery_frame_count,
           "catalog record and source-frame intervals are inconsistent");
    expect(kP18ReferenceRouteCount == kP18ReferenceAuditBusCount &&
               kP18ReferenceRouteCount == kP18ReferenceSeedRouteCount,
           "catalog route count differs between its historical fixture inputs");
    constexpr std::array<contract::RouteId, kP18ReferenceRouteCount> fixture_route_ids{
        contract::RouteId{1}, contract::RouteId{2}};
    for (std::size_t index = 0; index < catalog.expected_routes.size(); ++index) {
        expect(catalog.expected_routes[index].expected_route_id ==
                   fixture_route_ids[index],
               "catalog route ID differs from the source-stage route ID");
    }

    const auto &lineage = catalog.expected_lineage_files;
    expect(lineage[static_cast<std::size_t>(P18ReferenceLineageFile::audit_input)]
                       .expected_byte_count == kP18ReferenceAuditByteCount &&
               lineage[static_cast<std::size_t>(
                           P18ReferenceLineageFile::component_seed_input)]
                       .expected_byte_count == kP18ReferenceSeedByteCount,
           "catalog input size differs from its strict decoder");

    const auto &media = catalog.expected_presentation.expected_configured_ir_media;
    expect(media.expected_asset_id.valid() &&
               contract::is_valid_semantic_id(media.expected_semantic_id) &&
               contract::is_valid_semantic_id(media.expected_evidence_source_id),
           "configured-IR catalog identity is invalid");
    expect(
        media.expected_encoding == contract::AudioSampleEncoding::pcm_s16le &&
            media.expected_channel_layout == contract::AudioChannelLayout::mono &&
            media.expected_sample_rate ==
                contract::RationalRateHz{presentation::kConfiguredIrSampleRateHz, 1} &&
            media.expected_frame_count ==
                presentation::kMaximumConfiguredIrFrameCount &&
            media.expected_frame_count ==
                dsp::StaticIrConversionLimits::maximum_source_frame_count,
        "configured-IR catalog media differs from decoder or converter limits");
    expect(catalog.expected_kernel.expected_coefficient_count ==
                   dsp::FixedConvolutionKernel::coefficient_count &&
               catalog.expected_kernel.expected_coefficient_count ==
                   dsp::static_ir_target_count(
                       media.expected_meaningful_support_frame_count) &&
               !catalog.expected_kernel.expected_coefficient_f64le_sha256.is_zero() &&
               !catalog.expected_kernel.expected_spectrum_f64le_sha256.is_zero(),
           "kernel catalog differs from conversion or convolution shape");
    expect(lineage[static_cast<std::size_t>(
                       P18ReferenceLineageFile::kernel_oracle_comparator)]
                       .expected_byte_count ==
                   catalog.expected_kernel.expected_coefficient_count *
                       sizeof(double) &&
               lineage[static_cast<std::size_t>(
                           P18ReferenceLineageFile::kernel_oracle_comparator)]
                       .expected_sha256 ==
                   catalog.expected_kernel.expected_coefficient_f64le_sha256,
           "kernel lineage differs from the derived-kernel comparator");

    const auto processed_blocks =
        capture.expected_record_count / capture.expected_physics_frames_per_block;
    const auto warmup_blocks = (capture.expected_audible_start_record -
                                capture.expected_consumed_start_record) /
                               capture.expected_physics_frames_per_block;
    const auto published_blocks = (capture.expected_audible_end_record_exclusive -
                                   capture.expected_audible_start_record) /
                                  capture.expected_physics_frames_per_block;
    const auto processed_source_frames =
        processed_blocks * capture.expected_source_frames_per_block;
    const auto warmup_source_frames =
        warmup_blocks * capture.expected_source_frames_per_block;
    const auto published_source_frames =
        published_blocks * capture.expected_source_frames_per_block;
    const auto &render = catalog.expected_render;
    expect(
        render.expected_input_frame_count == kP18ReferenceAuditRecordCount &&
            render.expected_processed_block_count == processed_blocks &&
            render.expected_warmup_block_count == warmup_blocks &&
            render.expected_published_block_count == published_blocks &&
            render.expected_processed_source_frame_count == processed_source_frames &&
            render.expected_warmup_source_frame_count == warmup_source_frames &&
            render.expected_published_source_frame_count == published_source_frames &&
            render.expected_published_source_frame_count == kFixtureAudibleFrameCount,
        "render catalog differs from renderer or mastering horizons");
    expect(capture.expected_total_source_frame_count ==
                   render.expected_processed_source_frame_count &&
               capture.expected_audible_source_start_frame ==
                   render.expected_warmup_source_frame_count &&
               capture.expected_audible_source_end_frame_exclusive ==
                   render.expected_processed_source_frame_count &&
               capture.expected_delivery_frame_count ==
                   render.expected_published_source_frame_count &&
               render.expected_saturation_count == 0 &&
               render.expected_faded_absolute_peak_binary32_bits != 0,
           "capture and render comparator horizons disagree");

    const auto &scalars = catalog.expected_presentation.expected_scalars;
    expect(scalars.expected_publication_calibration_gain_linear.expected_ieee754_bits ==
                   std::bit_cast<std::uint64_t>(dsp::kSourcePublicationCalibration) &&
               scalars.expected_audition_monitoring_gain_linear.expected_ieee754_bits ==
                   std::bit_cast<std::uint64_t>(128.0) &&
               scalars.expected_audition_fade_in_duration_s.expected_ieee754_bits ==
                   std::bit_cast<std::uint64_t>(
                       static_cast<double>(kFixtureFadeFrameCount) /
                       static_cast<double>(
                           presentation::CausalReconstruction::kSourceRate)) &&
               scalars.expected_audition_fade_out_duration_s.expected_ieee754_bits ==
                   scalars.expected_audition_fade_in_duration_s.expected_ieee754_bits,
           "presentation scalar catalog differs from publication or mastering");
}

} // namespace

int main() {
    try {
        const auto &catalog = p18_reference_catalog_v1();
        expect(&catalog == &p18_reference_catalog_v1(),
               "catalog accessor did not return one immutable instance");
        test_ordered_complete_inventory(catalog);
        test_identity_and_seed_shape(catalog);
        test_subsystem_constant_alignment(catalog);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
