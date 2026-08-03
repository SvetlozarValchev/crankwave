#pragma once

#include "dsp/source_conditioning_primitives.hpp"
#include "engine_sim_offline/contract/common.hpp"
#include "presentation/causal_reconstruction.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <type_traits>
#include <vector>

namespace engine_sim_offline::presentation {

inline constexpr contract::RationalRateHz kIntakePressureInputRateHz{20000, 1};
inline constexpr contract::RationalRateHz kIntakePressureSourceRateHz{192000, 1};
inline constexpr double kIntakePressureDcRemovalCutoffHz = 10.0;

/**
 * Callback-scoped, non-owning view of absolute intake-plenum pressure.
 *
 * Storage is a complete frame-major matrix whose inner order is exactly
 * `route_ids()`. Both borrowed ranges must remain alive and immutable until the
 * receiving callback returns. The factory rejects temporary ranges at compile time.
 */
class IntakePressureInputBlockView final {
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
    [[nodiscard]] static IntakePressureInputBlockView
    borrow_for_callback(std::uint64_t first_frame_index,
                        contract::RationalRateHz sample_rate, RouteIdRange &&route_ids,
                        std::size_t frame_count,
                        ValueRange &&frame_major_absolute_pressure_pa) noexcept {
        return {
            first_frame_index,
            sample_rate,
            {std::ranges::data(route_ids), std::ranges::size(route_ids)},
            frame_count,
            {std::ranges::data(frame_major_absolute_pressure_pa),
             std::ranges::size(frame_major_absolute_pressure_pa)},
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

    [[nodiscard]] std::span<const double> absolute_pressure_pa() const noexcept {
        return absolute_pressure_pa_;
    }

    [[nodiscard]] std::span<const double>
    frame_absolute_pressure_pa(std::size_t frame_index) const noexcept {
        return absolute_pressure_pa_.subspan(frame_index * route_count(),
                                             route_count());
    }

  private:
    IntakePressureInputBlockView(std::uint64_t first_frame_index,
                                 contract::RationalRateHz sample_rate,
                                 std::span<const contract::RouteId> route_ids,
                                 std::size_t frame_count,
                                 std::span<const double> absolute_pressure_pa) noexcept
        : first_frame_index_(first_frame_index), sample_rate_(sample_rate),
          route_ids_(route_ids), frame_count_(frame_count),
          absolute_pressure_pa_(absolute_pressure_pa) {}

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::RouteId> route_ids_;
    std::size_t frame_count_ = 0;
    std::span<const double> absolute_pressure_pa_;
};

struct IntakePressureSourceRouteConfiguration {
    contract::RouteId id;
    double reference_pressure_pa = 0.0;
    double source_gain_linear = 0.0;

    friend bool operator==(const IntakePressureSourceRouteConfiguration &,
                           const IntakePressureSourceRouteConfiguration &) = default;
};

struct IntakePressureSourceStageConfiguration {
    std::span<const IntakePressureSourceRouteConfiguration> routes;
    contract::RationalRateHz input_rate = kIntakePressureInputRateHz;
    contract::RationalRateHz source_rate = kIntakePressureSourceRateHz;
    std::size_t input_frames_per_block = kExcitationFramesPerMethodBlock;
};

/**
 * Callback-scoped output view. Values are gauge-pressure pascals after the fixed
 * 10 Hz DC removal and the route's authored dimensionless source gain.
 */
class IntakePressureSourceBlockView final {
  public:
    [[nodiscard]] std::uint64_t first_input_frame_index() const noexcept {
        return first_input_frame_index_;
    }

    [[nodiscard]] std::uint64_t first_source_frame_index() const noexcept {
        return first_source_frame_index_;
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

    [[nodiscard]] std::size_t input_frame_count() const noexcept {
        return input_frame_count_;
    }

    [[nodiscard]] std::size_t frame_count() const noexcept {
        return source_frame_count_;
    }

    [[nodiscard]] std::span<const double> pressure_pa_gauge() const noexcept {
        return pressure_pa_gauge_;
    }

    [[nodiscard]] std::span<const double>
    frame_pressure_pa_gauge(std::size_t frame_index) const noexcept {
        return pressure_pa_gauge_.subspan(frame_index * route_count(), route_count());
    }

  private:
    friend class IntakePressureSourceStage;

    IntakePressureSourceBlockView(std::uint64_t first_input_frame_index,
                                  std::uint64_t first_source_frame_index,
                                  contract::RationalRateHz sample_rate,
                                  std::span<const contract::RouteId> route_ids,
                                  std::size_t input_frame_count,
                                  std::size_t source_frame_count,
                                  std::span<const double> pressure_pa_gauge) noexcept
        : first_input_frame_index_(first_input_frame_index),
          first_source_frame_index_(first_source_frame_index),
          sample_rate_(sample_rate), route_ids_(route_ids),
          input_frame_count_(input_frame_count),
          source_frame_count_(source_frame_count),
          pressure_pa_gauge_(pressure_pa_gauge) {}

    std::uint64_t first_input_frame_index_ = 0;
    std::uint64_t first_source_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::RouteId> route_ids_;
    std::size_t input_frame_count_ = 0;
    std::size_t source_frame_count_ = 0;
    std::span<const double> pressure_pa_gauge_;
};

// Isolated deterministic intake-pressure source primitive. Construction owns and
// preallocates every route and scratch buffer; process() performs no allocation.
class IntakePressureSourceStage final {
  public:
    explicit IntakePressureSourceStage(
        IntakePressureSourceStageConfiguration configuration);

    [[nodiscard]] IntakePressureSourceBlockView
    process(IntakePressureInputBlockView input,
            std::span<double> output_frame_major_pressure_pa_gauge);

    [[nodiscard]] std::span<const IntakePressureSourceRouteConfiguration>
    route_configurations() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId>
    expected_route_ids() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] contract::RationalRateHz input_rate() const noexcept;
    [[nodiscard]] contract::RationalRateHz source_rate() const noexcept;
    [[nodiscard]] std::size_t input_frames_per_block() const noexcept;
    [[nodiscard]] std::uint64_t next_input_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t next_source_frame_index() const noexcept;
    [[nodiscard]] bool terminal_failed() const noexcept;

  private:
    static std::vector<IntakePressureSourceRouteConfiguration>
    validate_configuration(const IntakePressureSourceStageConfiguration &configuration);

    std::vector<IntakePressureSourceRouteConfiguration> route_configurations_;
    std::vector<contract::RouteId> expected_route_ids_;
    contract::RationalRateHz input_rate_{};
    contract::RationalRateHz source_rate_{};
    std::size_t input_frames_per_block_ = 0;
    CausalReconstruction reconstruction_;
    std::vector<dsp::DcRemoval> dc_removers_;
    std::vector<double> gauge_input_scratch_;
    std::vector<double> reconstructed_scratch_;
    std::uint64_t next_input_frame_index_ = 0;
    std::uint64_t next_source_frame_index_ = 0;
    bool terminal_failed_ = false;
};

} // namespace engine_sim_offline::presentation
