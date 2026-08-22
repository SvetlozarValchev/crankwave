#pragma once

#include "crankwave/publication.hpp"

#include <cstddef>
#include <variant>
#include <vector>

namespace crankwave::artifacts {

struct ManifestEncoding {
    std::vector<std::byte> bytes;
};

using ManifestEncodingResult = std::variant<ManifestEncoding, RenderSinkError>;

} // namespace crankwave::artifacts
