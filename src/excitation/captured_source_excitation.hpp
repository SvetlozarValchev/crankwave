#pragma once

#include "crankwave/contract/capture.hpp"
#include "crankwave/contract/parity_model.hpp"
#include "crankwave/contract/result.hpp"
#include "crankwave/contract/scenario.hpp"
#include "presentation/exhaust_excitation_block.hpp"

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

namespace crankwave::excitation {

inline constexpr contract::RationalRateHz kPreviewCapturedSourceRateHz{10000U, 1U};
inline constexpr contract::RationalRateHz kCapturedSourceRateHz{20000U, 1U};

/**
 * Callback-scoped absolute intake-plenum pressure captured at the simulation clock.
 *
 * Values are frame-major in `route_ids()` order. The storage is owned and reused by
 * the captured-source session, so every span expires when the callback returns.
 */
class IntakePressureBlockView final {
  public:
    template <class RouteIdRange, class PressureRange>
        requires std::is_lvalue_reference_v<RouteIdRange &&> &&
                 std::is_lvalue_reference_v<PressureRange &&> &&
                 std::ranges::contiguous_range<RouteIdRange> &&
                 std::ranges::sized_range<RouteIdRange> &&
                 std::ranges::contiguous_range<PressureRange> &&
                 std::ranges::sized_range<PressureRange> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<RouteIdRange>>,
                     contract::RouteId> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<PressureRange>>,
                     double>
    [[nodiscard]] static IntakePressureBlockView
    borrow_for_callback(std::uint64_t first_frame_index,
                        contract::RationalRateHz sample_rate, RouteIdRange &&route_ids,
                        std::size_t frame_count,
                        PressureRange &&pressure_pa_abs) noexcept {
        return {
            first_frame_index,
            sample_rate,
            {std::ranges::data(route_ids), std::ranges::size(route_ids)},
            frame_count,
            {std::ranges::data(pressure_pa_abs), std::ranges::size(pressure_pa_abs)},
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::span<const double> pressure_pa_abs() const noexcept;
    [[nodiscard]] std::span<const double>
    frame_pressure_pa_abs(std::size_t frame_index) const noexcept;
    [[nodiscard]] double pressure_pa_abs(std::size_t frame_index,
                                         std::size_t route_index) const noexcept;

  private:
    IntakePressureBlockView(std::uint64_t first_frame_index,
                            contract::RationalRateHz sample_rate,
                            std::span<const contract::RouteId> route_ids,
                            std::size_t frame_count,
                            std::span<const double> pressure_pa_abs) noexcept;

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::RouteId> route_ids_;
    std::size_t frame_count_ = 0;
    std::span<const double> pressure_pa_abs_;
};

/**
 * Callback-scoped diagnostics for the physical-capture to exhaust-excitation seam.
 *
 * Cylinder values are frame-major in `cylinder_ids()` order. Post-delay values are
 * the identity-stable primary lanes immediately before the collector. Route values
 * use `route_ids()` order, include the shared downstream propagation delay, and are
 * the same immutable flat storage published through the presentation view. All spans
 * expire when the receiving callback returns.
 */
class ExhaustExcitationDiagnosticBlockView final {
  public:
    template <class CylinderIdRange, class RouteIdRange, class PreDelayRange,
              class PostDelayRange, class RouteValueRange>
        requires std::is_lvalue_reference_v<CylinderIdRange &&> &&
                 std::is_lvalue_reference_v<RouteIdRange &&> &&
                 std::is_lvalue_reference_v<PreDelayRange &&> &&
                 std::is_lvalue_reference_v<PostDelayRange &&> &&
                 std::is_lvalue_reference_v<RouteValueRange &&> &&
                 std::ranges::contiguous_range<CylinderIdRange> &&
                 std::ranges::sized_range<CylinderIdRange> &&
                 std::ranges::contiguous_range<RouteIdRange> &&
                 std::ranges::sized_range<RouteIdRange> &&
                 std::ranges::contiguous_range<PreDelayRange> &&
                 std::ranges::sized_range<PreDelayRange> &&
                 std::ranges::contiguous_range<PostDelayRange> &&
                 std::ranges::sized_range<PostDelayRange> &&
                 std::ranges::contiguous_range<RouteValueRange> &&
                 std::ranges::sized_range<RouteValueRange> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<CylinderIdRange>>,
                     contract::CylinderId> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<RouteIdRange>>,
                     contract::RouteId> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<PreDelayRange>>,
                     double> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<PostDelayRange>>,
                     double> &&
                 std::same_as<
                     std::remove_cv_t<std::ranges::range_value_t<RouteValueRange>>,
                     double>
    [[nodiscard]] static ExhaustExcitationDiagnosticBlockView borrow_for_callback(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        CylinderIdRange &&cylinder_ids, RouteIdRange &&route_ids,
        std::size_t frame_count, PreDelayRange &&pre_delay, PostDelayRange &&post_delay,
        RouteValueRange &&route_bus_values) noexcept {
        return {
            first_frame_index,
            sample_rate,
            {std::ranges::data(cylinder_ids), std::ranges::size(cylinder_ids)},
            {std::ranges::data(route_ids), std::ranges::size(route_ids)},
            frame_count,
            {std::ranges::data(pre_delay), std::ranges::size(pre_delay)},
            {std::ranges::data(post_delay), std::ranges::size(post_delay)},
            {std::ranges::data(route_bus_values), std::ranges::size(route_bus_values)},
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept;
    [[nodiscard]] std::span<const contract::CylinderId> cylinder_ids() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId> route_ids() const noexcept;
    [[nodiscard]] std::size_t cylinder_count() const noexcept;
    [[nodiscard]] std::size_t route_count() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::span<const double>
    pre_delay_cylinder_values_crankwave_source_unit() const noexcept;
    [[nodiscard]] std::span<const double>
    post_delay_cylinder_values_crankwave_source_unit() const noexcept;
    [[nodiscard]] std::span<const double>
    route_bus_values_crankwave_source_unit() const noexcept;

  private:
    ExhaustExcitationDiagnosticBlockView(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::span<const contract::CylinderId> cylinder_ids,
        std::span<const contract::RouteId> route_ids, std::size_t frame_count,
        std::span<const double> pre_delay, std::span<const double> post_delay,
        std::span<const double> route_bus_values) noexcept;

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::CylinderId> cylinder_ids_;
    std::span<const contract::RouteId> route_ids_;
    std::size_t frame_count_ = 0;
    std::span<const double> pre_delay_;
    std::span<const double> post_delay_;
    std::span<const double> route_bus_values_;
};

/**
 * Callback-scoped scalar axial combustion/compression pressure force on each piston
 * crown. Values are newtons, frame-major in `cylinder_ids()` order, on the canonical
 * 20 kHz post-step capture clock. Positive means chamber pressure exceeds crankcase
 * pressure and acts from chamber toward crankcase along the cylinder axis. This is
 * not piston slap, an engine-body wrench, or radiated acoustic pressure.
 */
class CylinderAxialPressureForceDiagnosticBlockView final {
  public:
    template <class CylinderIdRange, class ForceRange>
        requires contract::detail::CallbackBorrowRange<CylinderIdRange,
                                                       contract::CylinderId> &&
                 contract::detail::CallbackBorrowRange<ForceRange, double>
    [[nodiscard]] static CylinderAxialPressureForceDiagnosticBlockView
    borrow_for_callback(std::uint64_t first_frame_index,
                        contract::RationalRateHz sample_rate,
                        CylinderIdRange &&cylinder_ids, std::size_t frame_count,
                        ForceRange &&force_n) noexcept {
        return {
            first_frame_index,
            sample_rate,
            contract::detail::callback_span<contract::CylinderId>(
                std::forward<CylinderIdRange>(cylinder_ids)),
            frame_count,
            contract::detail::callback_span<double>(std::forward<ForceRange>(force_n)),
        };
    }

    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] contract::RationalRateHz sample_rate() const noexcept;
    [[nodiscard]] std::span<const contract::CylinderId> cylinder_ids() const noexcept;
    [[nodiscard]] std::size_t cylinder_count() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::span<const double> force_n() const noexcept;
    [[nodiscard]] std::span<const double>
    frame_force_n(std::size_t frame_index) const noexcept;

  private:
    CylinderAxialPressureForceDiagnosticBlockView(
        std::uint64_t first_frame_index, contract::RationalRateHz sample_rate,
        std::span<const contract::CylinderId> cylinder_ids, std::size_t frame_count,
        std::span<const double> force_n) noexcept;

    std::uint64_t first_frame_index_ = 0;
    contract::RationalRateHz sample_rate_{};
    std::span<const contract::CylinderId> cylinder_ids_;
    std::size_t frame_count_ = 0;
    std::span<const double> force_n_;
};

/**
 * Non-owning, synchronous callback for one excitation publication.
 *
 * The target is borrowed only for the duration of process_block(). The representation
 * is fixed at a context pointer plus function pointer so callback adaptation cannot
 * allocate on the block-producing path.
 */
class CapturedSourceConsumer final {
  public:
    constexpr CapturedSourceConsumer() noexcept = default;

    template <class Consumer>
        requires(!std::same_as<std::remove_cvref_t<Consumer>, CapturedSourceConsumer> &&
                 std::is_object_v<std::remove_reference_t<Consumer>> &&
                 std::invocable<
                     Consumer &, const presentation::ExhaustExcitationBlockView &,
                     const IntakePressureBlockView &,
                     const ExhaustExcitationDiagnosticBlockView &,
                     const CylinderAxialPressureForceDiagnosticBlockView &> &&
                 std::convertible_to<
                     std::invoke_result_t<
                         Consumer &, const presentation::ExhaustExcitationBlockView &,
                         const IntakePressureBlockView &,
                         const ExhaustExcitationDiagnosticBlockView &,
                         const CylinderAxialPressureForceDiagnosticBlockView &>,
                     bool>)
    CapturedSourceConsumer(Consumer &&consumer) noexcept
        : context_(
              const_cast<void *>(static_cast<const void *>(std::addressof(consumer)))),
          invoke_(
              [](void *context, const presentation::ExhaustExcitationBlockView &block,
                 const IntakePressureBlockView &intake,
                 const ExhaustExcitationDiagnosticBlockView &diagnostics,
                 const CylinderAxialPressureForceDiagnosticBlockView &pressure_force)
                  -> bool {
                  using Target = std::remove_reference_t<Consumer>;
                  return static_cast<bool>(std::invoke(*static_cast<Target *>(context),
                                                       block, intake, diagnostics,
                                                       pressure_force));
              }) {}

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return context_ != nullptr && invoke_ != nullptr;
    }

    [[nodiscard]] bool operator()(
        const presentation::ExhaustExcitationBlockView &block,
        const IntakePressureBlockView &intake,
        const ExhaustExcitationDiagnosticBlockView &diagnostics,
        const CylinderAxialPressureForceDiagnosticBlockView &pressure_force) const {
        return invoke_(context_, block, intake, diagnostics, pressure_force);
    }

  private:
    void *context_ = nullptr;
    bool (*invoke_)(void *, const presentation::ExhaustExcitationBlockView &,
                    const IntakePressureBlockView &,
                    const ExhaustExcitationDiagnosticBlockView &,
                    const CylinderAxialPressureForceDiagnosticBlockView &) = nullptr;
};

static_assert(std::is_trivially_copyable_v<CapturedSourceConsumer>);

struct CapturedSourceBlockPublished {
    std::uint64_t block_ordinal = 0;
    std::uint64_t first_frame_index = 0;
    std::uint32_t frame_count = 0;
    std::uint64_t published_frame_count = 0;

    friend bool operator==(const CapturedSourceBlockPublished &,
                           const CapturedSourceBlockPublished &) = default;
};

using CapturedSourceProcessResult =
    std::variant<CapturedSourceBlockPublished, contract::FailureContext>;

namespace detail {
class CapturedSourceExcitationState;
}

/**
 * Stateful, bounded adapter from validated M3 capture blocks to the accepted
 * presentation seam. The currently admitted product always couples low-order gas,
 * exhaust, intake, and cylinder thermodynamic capture in this one transaction; a
 * future product that admits mechanical-only capture may split that admission then.
 * It owns all IDs, delay history, and callback scratch and retains no EngineSpec or
 * CaptureBlock reference.
 */
class CapturedSourceExcitationSession final {
  public:
    CapturedSourceExcitationSession(const CapturedSourceExcitationSession &) = delete;
    CapturedSourceExcitationSession &
    operator=(const CapturedSourceExcitationSession &) = delete;
    CapturedSourceExcitationSession(CapturedSourceExcitationSession &&) noexcept;
    CapturedSourceExcitationSession &
    operator=(CapturedSourceExcitationSession &&) noexcept;
    ~CapturedSourceExcitationSession();

    [[nodiscard]] CapturedSourceProcessResult
    process_block(const contract::CaptureBlockView &block,
                  const CapturedSourceConsumer &consumer);

    [[nodiscard]] std::uint64_t next_frame_index() const noexcept;
    [[nodiscard]] std::uint64_t published_block_count() const noexcept;
    [[nodiscard]] std::span<const contract::RouteId> exhaust_route_ids() const noexcept;
    [[nodiscard]] std::span<const double>
    exhaust_valve_reference_mass_flow_kg_s() const noexcept;
    [[nodiscard]] bool faulted() const noexcept;

  private:
    explicit CapturedSourceExcitationSession(
        std::unique_ptr<detail::CapturedSourceExcitationState> state) noexcept;

    std::unique_ptr<detail::CapturedSourceExcitationState> state_;

    friend std::variant<CapturedSourceExcitationSession, contract::ValidationReport>
    compile_captured_source_excitation_session(const contract::EngineSpec &,
                                               const contract::LowOrderEngineCoreV1 &,
                                               const contract::RenderScenario &);
};

using CapturedSourceExcitationCompileResult =
    std::variant<CapturedSourceExcitationSession, contract::ValidationReport>;

// Resolves the exact low-order core excitation profile into an owned session.
[[nodiscard]] CapturedSourceExcitationCompileResult
compile_captured_source_excitation_session(const contract::EngineSpec &engine,
                                           const contract::LowOrderEngineCoreV1 &core,
                                           const contract::RenderScenario &scenario);

} // namespace crankwave::excitation
