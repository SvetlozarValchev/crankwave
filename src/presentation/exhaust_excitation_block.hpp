#pragma once

#include "engine_sim_offline/contract/common.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace engine_sim_offline::presentation {

/**
 * Callback-scoped, non-owning excitation view.
 *
 * Values retain the uncalibrated engine_sim_source_unit. They are neither pressure
 * nor microphone samples. Storage is a flat frame-major matrix whose inner route
 * order is exactly `route_ids()`. Both referenced ranges must remain alive and
 * immutable until the receiving callback returns. The factory rejects temporary
 * ranges at compile time.
 */
class ExhaustExcitationBlockView final {
  public:
    template <class RouteIdRange, class ValueRange>
        requires std::is_lvalue_reference_v<RouteIdRange &&> &&
                 std::is_lvalue_reference_v<ValueRange &&> &&
                 std::ranges::contiguous_range<RouteIdRange> &&
                 std::ranges::sized_range<RouteIdRange> &&
                 std::ranges::contiguous_range<ValueRange> &&
                 std::ranges::sized_range<ValueRange> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<RouteIdRange>>,
                     contract::RouteId> &&
                 std::same_as<std::remove_cv_t<std::ranges::range_value_t<ValueRange>>,
                              double>
    [[nodiscard]] static ExhaustExcitationBlockView borrow_for_callback(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        RouteIdRange &&route_ids, std::size_t frame_count,
        ValueRange &&frame_major_values_engine_sim_source_unit) noexcept {
        return {
            first_frame_index,
            sample_rate,
            {std::ranges::data(route_ids), std::ranges::size(route_ids)},
            frame_count,
            {std::ranges::data(frame_major_values_engine_sim_source_unit),
             std::ranges::size(frame_major_values_engine_sim_source_unit)},
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept {
        return first_frame_index_;
    }

    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept {
        return sample_rate_;
    }

    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept {
        return route_ids_;
    }

    [[nodiscard]] std::size_t route_count() const noexcept {
        return route_ids_.size();
    }

    [[nodiscard]] std::size_t frame_count() const noexcept {
        return frame_count_;
    }

    [[nodiscard]] std::span<const double>
    values_engine_sim_source_unit() const noexcept {
        return values_engine_sim_source_unit_;
    }

    [[nodiscard]] std::span<const double>
    frame_values_engine_sim_source_unit(std::size_t frame_index) const noexcept {
        return values_engine_sim_source_unit_.subspan(frame_index * route_count(),
                                                      route_count());
    }

    [[nodiscard]] double
    value_engine_sim_source_unit(std::size_t frame_index,
                                 std::size_t route_index) const noexcept {
        return values_engine_sim_source_unit_[frame_index * route_count() +
                                              route_index];
    }

  private:
    ExhaustExcitationBlockView(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::span<const contract::RouteId> route_ids, std::size_t frame_count,
        std::span<const double> values_engine_sim_source_unit) noexcept
        : first_frame_index_(first_frame_index), sample_rate_(sample_rate),
          route_ids_(route_ids), frame_count_(frame_count),
          values_engine_sim_source_unit_(values_engine_sim_source_unit) {}

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::RouteId> route_ids_;
    std::size_t frame_count_ = 0;
    std::span<const double> values_engine_sim_source_unit_;
};

} // namespace engine_sim_offline::presentation
