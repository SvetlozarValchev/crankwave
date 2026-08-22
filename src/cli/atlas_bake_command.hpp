#pragma once

#include "cli_app.hpp"

#include <iosfwd>

namespace crankwave::cli {

[[nodiscard]] int execute_bake_atlas(const BakeAtlasCommand &command,
                                     std::ostream &standard_out,
                                     std::ostream &standard_error);

} // namespace crankwave::cli
