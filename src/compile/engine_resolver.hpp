#pragma once

#include "compile/resolved_authoring.hpp"
#include "crankwave/authoring/engine_document.hpp"
#include "crankwave/compile.hpp"
#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/presentation.hpp"
#include "crankwave/contract/randomness.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crankwave::compile::detail {

struct VerifiedEngineAsset {
    AssetKind kind = AssetKind::audio;
    std::string asset_id;
    std::string locator;
    contract::Sha256Digest content_sha256;
    std::vector<std::byte> bytes;

    friend bool operator==(const VerifiedEngineAsset &,
                           const VerifiedEngineAsset &) = default;
};

// Complete engine-owned output of the portable authoring resolver. Scenario-owned
// clocks, controls, operating state, and public seed deliberately do not enter here.
struct ResolvedEnginePackage {
    contract::EngineSpec engine;
    contract::PresentationCalibration presentation;
    contract::ResolvedRandomnessPolicy randomness;
    std::optional<ResolvedRigDescriptor> rig;
    std::vector<StableIdAssignment> stable_id_assignments;
    contract::ProvenanceLedger provenance;
    std::vector<VerifiedEngineAsset> assets;
    std::vector<ResolvedFuelDescriptor> fuels;
    std::vector<ResolvedAudioBusDescriptor> audio_buses;
};

using EngineResolutionResult = CompileResult<ResolvedEnginePackage>;

[[nodiscard]] EngineResolutionResult resolve_engine_package(
    const authoring::EnginePackageDocument &document,
    std::span<const AssetPayloadView> assets) noexcept;

} // namespace crankwave::compile::detail
