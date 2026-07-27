#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace engine_sim_offline::presentation {

inline constexpr std::size_t kP18ExhaustRouteCount = 2;

// The values retain the narrow reference renderer's uncalibrated
// engine_sim_source_unit. They are neither pressure nor microphone samples.
struct ExhaustExcitationFrame {
    std::array<double, kP18ExhaustRouteCount> route_values_engine_sim_source_unit{};

    friend bool operator==(const ExhaustExcitationFrame &,
                           const ExhaustExcitationFrame &) = default;
};

/**
 * Callback-scoped, non-owning excitation view.
 *
 * Route values are frame-major and use the exact order in `route_ids()`. The
 * referenced frame storage must remain alive and immutable until the receiving
 * callback returns. The factory rejects temporary ranges at compile time.
 */
class ExhaustExcitationBlockView {
  public:
    template <class FrameRange>
        requires std::is_lvalue_reference_v<FrameRange &&> &&
                 std::ranges::contiguous_range<FrameRange> &&
                 std::ranges::sized_range<FrameRange> &&
                 std::same_as<std::remove_cv_t<std::ranges::range_value_t<FrameRange>>,
                              ExhaustExcitationFrame>
    [[nodiscard]] static ExhaustExcitationBlockView
    borrow_for_callback(std::uint64_t first_frame_index,
                        contract::RationalRateHz sample_rate,
                        std::array<contract::RouteId, kP18ExhaustRouteCount> route_ids,
                        FrameRange &&frames) noexcept {
        return {
            first_frame_index,
            sample_rate,
            route_ids,
            {std::ranges::data(frames), std::ranges::size(frames)},
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept {
        return first_frame_index_;
    }

    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept {
        return sample_rate_;
    }

    [[nodiscard]] const std::array<contract::RouteId, kP18ExhaustRouteCount> &
    route_ids() const noexcept {
        return route_ids_;
    }

    [[nodiscard]] std::span<const ExhaustExcitationFrame> frames() const noexcept {
        return frames_;
    }

  private:
    ExhaustExcitationBlockView(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::array<contract::RouteId, kP18ExhaustRouteCount> route_ids,
        std::span<const ExhaustExcitationFrame> frames) noexcept
        : first_frame_index_(first_frame_index), sample_rate_(sample_rate),
          route_ids_(route_ids), frames_(frames) {}

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::array<contract::RouteId, kP18ExhaustRouteCount> route_ids_{};
    std::span<const ExhaustExcitationFrame> frames_;
};

} // namespace engine_sim_offline::presentation
