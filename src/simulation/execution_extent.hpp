#pragma once

#include <cstdint>
#include <optional>

namespace crankwave::simulation {

enum class LowOrderExecutionExtentKind : std::uint8_t {
    finite_scenario,
    open_ended,
};

// Execution lifetime is selected by the session owner. A finite session ends at the
// exact authored physics horizon. An open session has no synthetic terminal frame;
// it can end only when its owner destroys it or a typed runtime fault occurs.
class LowOrderExecutionExtent final {
  public:
    [[nodiscard]] static constexpr LowOrderExecutionExtent
    finite_scenario(std::uint64_t physics_frame_count) noexcept {
        return {LowOrderExecutionExtentKind::finite_scenario, physics_frame_count};
    }

    [[nodiscard]] static constexpr LowOrderExecutionExtent open_ended() noexcept {
        return {LowOrderExecutionExtentKind::open_ended, 0U};
    }

    [[nodiscard]] constexpr LowOrderExecutionExtentKind kind() const noexcept {
        return kind_;
    }

    [[nodiscard]] constexpr bool is_open_ended() const noexcept {
        return kind_ == LowOrderExecutionExtentKind::open_ended;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return is_open_ended() || finite_physics_frame_count_ > 0U;
    }

    [[nodiscard]] constexpr std::optional<std::uint64_t>
    finite_physics_frame_count() const noexcept {
        if (is_open_ended()) {
            return std::nullopt;
        }
        return finite_physics_frame_count_;
    }

    friend constexpr bool operator==(const LowOrderExecutionExtent &,
                                     const LowOrderExecutionExtent &) = default;

  private:
    constexpr LowOrderExecutionExtent(LowOrderExecutionExtentKind kind,
                                      std::uint64_t finite_physics_frame_count) noexcept
        : kind_(kind), finite_physics_frame_count_(finite_physics_frame_count) {}

    LowOrderExecutionExtentKind kind_ = LowOrderExecutionExtentKind::finite_scenario;
    std::uint64_t finite_physics_frame_count_ = 0U;
};

} // namespace crankwave::simulation
