#include "native_responsive_bake_identity.hpp"

#include "engine_sim_offline/artifacts/revengine_container.hpp"
#include "engine_sim_offline/artifacts/revengine_package.hpp"
#include "engine_sim_offline/responsive/directional_cook.hpp"
#include "engine_sim_offline/responsive/held_texture.hpp"
#include "engine_sim_offline/responsive/lifecycle.hpp"
#include "engine_sim_offline/responsive/native_package.hpp"
#include "engine_sim_offline/responsive/package_children.hpp"
#include "engine_sim_offline/responsive/presentation_transfer.hpp"
#include "engine_sim_offline/responsive/profile.hpp"
#include "engine_sim_offline/responsive/scenario_template.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace engine_sim_offline::cli {
namespace {

using Record = std::pair<std::string_view, std::string_view>;

void append_u64(std::vector<std::byte> &output, const std::uint64_t value) {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::byte>((value >> shift) & UINT64_C(0xff)));
    }
}

void append_string(std::vector<std::byte> &output, const std::string_view value) {
    append_u64(output, value.size());
    for (const unsigned char byte : value) {
        output.push_back(static_cast<std::byte>(byte));
    }
}

[[nodiscard]] std::vector<std::byte>
encode_records(const std::string_view schema, const std::span<const Record> records) {
    std::vector<std::byte> output;
    std::size_t capacity = schema.size() + 16U;
    for (const auto &[key, value] : records) {
        capacity += key.size() + value.size() + 16U;
    }
    output.reserve(capacity);
    append_string(output, schema);
    append_u64(output, records.size());
    for (const auto &[key, value] : records) {
        append_string(output, key);
        append_string(output, value);
    }
    return output;
}

} // namespace

NativeResponsiveBakeAuthorityV1 native_responsive_bake_authority_v1() {
    using namespace responsive;
    const std::array method_records{
        Record{"profile-selection-policy", kEngineRedlineAffineProfilePolicyId},
        Record{"profile-identity", kResponsiveProfileIdentityMethodId},
        Record{"scenario-template-identity",
               kResponsiveScenarioTemplateIdentityMethodId},
        Record{"held-capture", kHeldCaptureMethodId},
        Record{"held-decomposition", kHeldDecompositionMethodId},
        Record{"held-phase-alignment", kHeldPhaseAlignmentMethodId},
        Record{"held-state-identity", kHeldStateIdentityMethodId},
        Record{"held-texture-identity", kHeldTextureIdentityMethodId},
        Record{"held-grid-identity", kHeldGridIdentityMethodId},
        Record{"held-load-coalescing", kHeldLoadCoalescingMethodId},
        Record{"directional-capture", kDirectionalCaptureMethodId},
        Record{"directional-seam", kDirectionalSeamAlgorithmId},
        Record{"directional-scenario-identity", kDirectionalScenarioIdentityMethodId},
        Record{"directional-cell-identity", kDirectionalCellIdentityMethodId},
        Record{"directional-capture-identity", kDirectionalCaptureIdentityMethodId},
        Record{"directional-model-identity", kDirectionalModelIdentityMethodId},
        Record{"directional-load-coalescing", kDirectionalLoadCoalescingMethodId},
        Record{"lifecycle-scenario-identity", kLifecycleScenarioIdentityMethodId},
        Record{"presentation-transfer-identity",
               kResponsivePresentationIdentityMethodId},
        Record{"presentation-fixed-transfer-kind", kResponsiveFixedTransferKind},
        Record{"presentation-partitioned-transfer-kind",
               kResponsivePartitionedTransferKind},
        Record{"presentation-spectrum-encoding", kResponsiveTransferSpectrumEncoding},
    };
    auto method_preimage = encode_records(
        "engine-sim-offline.native-responsive-method-authority.v1", method_records);

    const auto container_version =
        std::to_string(artifacts::kRevengineContainerVersionV1);
    const auto package_schema_version =
        std::to_string(artifacts::kRevenginePackageSchemaVersion);
    const std::array recipe_records{
        Record{"automatic-profile", kAutomaticResponsiveProfileId},
        Record{"responsive-profile-schema", kResponsiveBakeProfileSchema},
        Record{"profile-outer-min-envelope",
               "affine-clamped-to-max-50rpm-and-0.8-first-anchor-v1"},
        Record{"held-operating-mode-selection",
               "held-speed-for-direct-non-master-rod-else-held-dyno-v1"},
        Record{"held-preparation-admission",
               "legacy-18-cycle-below-threshold-plus-global-17-cycle-20ms-bound-v1"},
        Record{"held-capture-order", "rpm-major-load-minor-v1"},
        Record{"directional-capture-order", "rising-loads-then-falling-loads-v1"},
        Record{"lifecycle-capture-order", "starter-probe-startup-shutdown-elevated-v1"},
        Record{"lifecycle-starter-horizon",
               "max-2.5s-three-target-speed-cycles-after-30pct-gate-20ms-ceiling-v1"},
        Record{"lifecycle-probe-horizon",
               "max-4.8s-ignition-plus-two-running-floor-cycles-20ms-ceiling-v1"},
        Record{"lifecycle-startup-horizon", "release-plus-max-3s-one-second-gate-plus-"
                                            "two-running-floor-cycles-20ms-ceiling-v1"},
        Record{"lifecycle-shutdown-horizon",
               "prep-max-2s-five-idle-cycles-keyoff-max-2.8s-prep-plus-two-idle-cycles-"
               "postroll-2.2s-20ms-ceiling-v1"},
        Record{"lifecycle-presentation-tail-extension",
               "max-transfer-coefficients-minus-30071-ceil-by-3840-blocks-added-after-"
               "base-total-v1"},
        Record{"elevated-shutdown-running-lead",
               "two-nominal-720-degree-cycles-20ms-ceiling-v1"},
        Record{"lifecycle-pre-event-target-cycle-admission",
               "all-positive-length-cycles-crossfade-capped-100ms-v1"},
        Record{"capture-bus-authority",
               "compiled-session-resolved-dry-routes-and-audition-kind-v1"},
        Record{"capture-scheduler", kNativeResponsiveCaptureSchedulerV1},
        Record{"shared-starter-authority",
               "installed-release-exact-two-member-licensed-bundle-v1"},
        Record{"lifecycle-schema", kResponsiveLifecycleSchema},
        Record{"lifecycle-capture-evidence-schema", kLifecycleCaptureEvidenceSchema},
        Record{"startup-admission-evidence-schema",
               kStartupAdmissionFloorEvidenceSchema},
        Record{"startup-admission-schema", kContinuousStartupAdmissionSchema},
        Record{"runtime-schema", kResponsiveRuntimeSchemaV1},
        Record{"native-cache-identity-schema", kNativeResponsiveCacheIdentitySchemaV1},
        Record{"native-bake-report-schema", kNativeResponsiveBakeReportSchemaV2},
        Record{"held-package-path", kResponsiveHeldPackagePathV1},
        Record{"directional-package-path", kResponsiveDirectionalPackagePathV1},
        Record{"lifecycle-package-path", kResponsiveLifecyclePackagePathV1},
        Record{"shared-recorded-starter-package-path",
               kResponsiveSharedRecordedStarterPackagePathV1},
        Record{"root-runtime-path", kResponsiveRuntimePathV1},
        Record{"bake-report-path", kNativeResponsiveBakeReportPathV2},
        Record{"revengine-package-descriptor-path",
               artifacts::kRevenginePackageDescriptorPath},
        Record{"revengine-package-schema", artifacts::kRevenginePackageSchema},
        Record{"revengine-package-schema-version", package_schema_version},
        Record{"revengine-runtime-kind",
               artifacts::kRevengineResponsiveAudioRuntimeKind},
        Record{"revengine-container-version", container_version},
        Record{"publication", "verify-complete-carrier-then-atomic-noreplace-v1"},
    };
    auto recipe_preimage = encode_records(
        "engine-sim-offline.native-responsive-bake-recipe.v1", recipe_records);

    NativeResponsiveBakeAuthorityV1 result;
    result.method_authority_sha256 = contract::sha256(method_preimage);
    result.method_authority_preimage = std::move(method_preimage);
    result.bake_recipe_sha256 = contract::sha256(recipe_preimage);
    result.bake_recipe_preimage = std::move(recipe_preimage);
    return result;
}

} // namespace engine_sim_offline::cli
