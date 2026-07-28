#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "presentation/exhaust_excitation_block.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

namespace engine_sim_offline::excitation {

inline constexpr std::uint32_t kCapturedExcitationFramesPerBlock = 200U;
inline constexpr std::size_t kCapturedExcitationCylinderCount = 6U;
inline constexpr std::size_t kCapturedExcitationRouteCount = 2U;

/**
 * Callback-scoped diagnostics for the physical-capture to exhaust-excitation seam.
 *
 * Cylinder values are frame-major in `cylinder_ids()` order. Route frames use
 * `route_ids()` order and are the same immutable storage published through the
 * presentation view. All spans expire when the receiving callback returns.
 */
class ExhaustExcitationDiagnosticBlockView final {
  public:
    template <class PreDelayRange, class PostDelayRange, class RouteFrameRange>
        requires std::is_lvalue_reference_v<PreDelayRange &&> &&
                 std::is_lvalue_reference_v<PostDelayRange &&> &&
                 std::is_lvalue_reference_v<RouteFrameRange &&> &&
                 std::ranges::contiguous_range<PreDelayRange> &&
                 std::ranges::sized_range<PreDelayRange> &&
                 std::ranges::contiguous_range<PostDelayRange> &&
                 std::ranges::sized_range<PostDelayRange> &&
                 std::ranges::contiguous_range<RouteFrameRange> &&
                 std::ranges::sized_range<RouteFrameRange> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<PreDelayRange>>,
                     double> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<PostDelayRange>>,
                     double> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<RouteFrameRange>>,
                     presentation::ExhaustExcitationFrame>
    [[nodiscard]] static ExhaustExcitationDiagnosticBlockView borrow_for_callback(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::array<contract::CylinderId, kCapturedExcitationCylinderCount> cylinder_ids,
        std::array<contract::RouteId, kCapturedExcitationRouteCount> route_ids,
        PreDelayRange &&pre_delay, PostDelayRange &&post_delay,
        RouteFrameRange &&route_bus_frames) noexcept {
        return {
            first_frame_index,
            sample_rate,
            cylinder_ids,
            route_ids,
            {std::ranges::data(pre_delay), std::ranges::size(pre_delay)},
            {std::ranges::data(post_delay), std::ranges::size(post_delay)},
            {std::ranges::data(route_bus_frames), std::ranges::size(route_bus_frames)},
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept;
    [[nodiscard]] const std::array<contract::CylinderId,
                                   kCapturedExcitationCylinderCount> &
    cylinder_ids() const noexcept;
    [[nodiscard]] const std::array<contract::RouteId, kCapturedExcitationRouteCount> &
    route_ids() const noexcept;
    [[nodiscard]] std::span<const double>
    pre_delay_cylinder_values_engine_sim_source_unit() const noexcept;
    [[nodiscard]] std::span<const double>
    post_delay_cylinder_values_engine_sim_source_unit() const noexcept;
    [[nodiscard]] std::span<const presentation::ExhaustExcitationFrame>
    route_bus_frames() const noexcept;

  private:
    ExhaustExcitationDiagnosticBlockView(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::array<contract::CylinderId, kCapturedExcitationCylinderCount> cylinder_ids,
        std::array<contract::RouteId, kCapturedExcitationRouteCount> route_ids,
        std::span<const double> pre_delay, std::span<const double> post_delay,
        std::span<const presentation::ExhaustExcitationFrame>
            route_bus_frames) noexcept;

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::array<contract::CylinderId, kCapturedExcitationCylinderCount> cylinder_ids_{};
    std::array<contract::RouteId, kCapturedExcitationRouteCount> route_ids_{};
    std::span<const double> pre_delay_;
    std::span<const double> post_delay_;
    std::span<const presentation::ExhaustExcitationFrame> route_bus_frames_;
};

using ExhaustExcitationConsumer =
    std::function<bool(const presentation::ExhaustExcitationBlockView &,
                       const ExhaustExcitationDiagnosticBlockView &)>;

struct ExhaustExcitationBlockPublished {
    std::uint64_t block_ordinal = 0;
    std::uint64_t first_frame_index = 0;
    std::uint32_t frame_count = 0;
    std::uint64_t published_frame_count = 0;

    friend bool operator==(const ExhaustExcitationBlockPublished &,
                           const ExhaustExcitationBlockPublished &) = default;
};

using CapturedExhaustExcitationProcessResult =
    std::variant<ExhaustExcitationBlockPublished, contract::FailureContext>;

namespace detail {
class CapturedExhaustExcitationState;
}

/**
 * Stateful, bounded adapter from validated M3 capture blocks to the accepted
 * two-route presentation seam. It owns all delay history and callback scratch and
 * retains no EngineSpec or CaptureBlock reference.
 */
class CapturedExhaustExcitationSession final {
  public:
    CapturedExhaustExcitationSession(const CapturedExhaustExcitationSession &) = delete;
    CapturedExhaustExcitationSession &
    operator=(const CapturedExhaustExcitationSession &) = delete;
    CapturedExhaustExcitationSession(CapturedExhaustExcitationSession &&) noexcept;
    CapturedExhaustExcitationSession &
    operator=(CapturedExhaustExcitationSession &&) noexcept;
    ~CapturedExhaustExcitationSession();

    [[nodiscard]] CapturedExhaustExcitationProcessResult
    process_block(const contract::CaptureBlockView &block,
                  const ExhaustExcitationConsumer &consumer);

    [[nodiscard]] std::uint64_t next_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t published_block_count() const noexcept;
    [[nodiscard]] bool faulted() const noexcept;

  private:
    explicit CapturedExhaustExcitationSession(
        std::unique_ptr<detail::CapturedExhaustExcitationState> state) noexcept;

    std::unique_ptr<detail::CapturedExhaustExcitationState> state_;

    friend std::variant<CapturedExhaustExcitationSession, contract::ValidationReport>
    compile_captured_exhaust_excitation_session(const contract::EngineSpec &);
};

using CapturedExhaustExcitationCompileResult =
    std::variant<CapturedExhaustExcitationSession, contract::ValidationReport>;

// Resolves the exact LegacyLowOrderV1 excitation profile into an owned session.
[[nodiscard]] CapturedExhaustExcitationCompileResult
compile_captured_exhaust_excitation_session(const contract::EngineSpec &engine);

} // namespace engine_sim_offline::excitation
