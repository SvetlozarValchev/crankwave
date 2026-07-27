#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

namespace engine_sim_offline::cli {

inline constexpr int kExitSuccess = 0;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitUnavailable = 69;

[[nodiscard]] std::string_view version_label() noexcept;

[[nodiscard]] int run_cli(std::span<const std::string_view> arguments,
                          std::ostream &standard_out, std::ostream &standard_error);

} // namespace engine_sim_offline::cli
