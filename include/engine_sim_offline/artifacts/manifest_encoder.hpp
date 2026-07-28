#pragma once

#include "engine_sim_offline/render.hpp"

#include <cstddef>
#include <variant>
#include <vector>

namespace engine_sim_offline::artifacts {

struct ManifestEncoding {
    std::vector<std::byte> bytes;
};

using ManifestEncodingResult = std::variant<ManifestEncoding, RenderSinkError>;

} // namespace engine_sim_offline::artifacts
