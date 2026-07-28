#include "render/render_request.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace engine_sim_offline::render_detail {

std::vector<contract::AssetPayloadIdentity>
asset_payload_identities(const RenderSpecification &specification) {
    std::vector<contract::AssetPayloadIdentity> identities;
    identities.reserve(specification.asset_payloads.size());
    for (const auto &payload : specification.asset_payloads) {
        identities.push_back({
            payload.id,
            static_cast<std::uint64_t>(payload.bytes.size()),
            contract::sha256(payload.bytes),
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

contract::RenderRequestRecord
make_render_request_record(const RenderSpecification &specification,
                           const contract::RenderScenario &scenario) {
    return {
        {specification.engine, specification.presentation, specification.randomness,
         scenario},
        specification.provenance,
        specification.source_matrix,
        asset_payload_identities(specification),
    };
}

} // namespace engine_sim_offline::render_detail
