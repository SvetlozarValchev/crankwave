#include "render/render_request.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace engine_sim_offline::render_detail {

std::vector<contract::AssetPayloadIdentity> asset_payload_identities(
    const compile::detail::ResolvedEnginePackage &engine_package) {
    std::vector<contract::AssetPayloadIdentity> identities;
    identities.reserve(engine_package.presentation.assets.size());
    for (const auto &asset : engine_package.presentation.assets) {
        const auto payload =
            std::ranges::find(engine_package.assets, asset.semantic_id.value,
                              &compile::detail::VerifiedEngineAsset::asset_id);
        if (payload == engine_package.assets.end() ||
            payload->kind != compile::AssetKind::audio) {
            continue;
        }
        identities.push_back({
            asset.id,
            static_cast<std::uint64_t>(payload->bytes.size()),
            payload->content_sha256,
        });
    }
    std::ranges::sort(identities, [](const auto &lhs, const auto &rhs) {
        if (lhs.id != rhs.id) {
            return lhs.id < rhs.id;
        }
        if (lhs.byte_count != rhs.byte_count) {
            return lhs.byte_count < rhs.byte_count;
        }
        return lhs.payload_sha256.bytes < rhs.payload_sha256.bytes;
    });
    return identities;
}

contract::RenderRequestRecord make_render_request_record(
    const compile::detail::ResolvedEnginePackage &engine_package,
    const compile::detail::ResolvedScenarioContracts &scenario) {
    return {
        {
            engine_package.engine,
            engine_package.presentation,
            engine_package.randomness,
            scenario.scenario,
        },
        scenario.combined_provenance,
        scenario.source_matrix,
        asset_payload_identities(engine_package),
    };
}

} // namespace engine_sim_offline::render_detail
