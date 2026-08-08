#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace engine_sim_offline::cli {

inline constexpr std::string_view kNativeResponsiveCaptureSchedulerV1 =
    "bounded-six-worker-canonical-result-order-cancel-drain-v1";

// These byte strings are part of the native bake cache boundary. They are encoded
// as a versioned sequence of length-prefixed UTF-8 key/value pairs, not as an
// implementation-dependent object layout or incidental JSON formatting.
struct NativeResponsiveBakeAuthorityV1 {
    std::vector<std::byte> method_authority_preimage;
    contract::Sha256Digest method_authority_sha256;
    std::vector<std::byte> bake_recipe_preimage;
    contract::Sha256Digest bake_recipe_sha256;

    friend bool operator==(const NativeResponsiveBakeAuthorityV1 &,
                           const NativeResponsiveBakeAuthorityV1 &) = default;
};

[[nodiscard]] NativeResponsiveBakeAuthorityV1
native_responsive_bake_authority_v1();

} // namespace engine_sim_offline::cli
