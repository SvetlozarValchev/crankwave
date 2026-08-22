#pragma once

#include "crankwave/contract/common.hpp"
#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/torque.hpp"

#include <array>
#include <concepts>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace crankwave::contract {

struct RenderScenario;

inline constexpr double kMixtureFractionUnityTolerance = 1.0e-12;

namespace detail {

/**
 * A callback view may borrow only a contiguous, sized lvalue range whose element
 * type is exact. Requiring the lvalue at the factory boundary prevents a temporary
 * vector/array from being silently converted to a const span and returned dangling.
 */
template <class Range, class Element>
concept CallbackBorrowRange =
    std::is_lvalue_reference_v<Range &&> && std::ranges::contiguous_range<Range> &&
    std::ranges::sized_range<Range> &&
    std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, Element>;

template <class Element, CallbackBorrowRange<Element> Range>
[[nodiscard]] std::span<const Element> callback_span(Range &&range) noexcept {
    return {std::ranges::data(range), std::ranges::size(range)};
}

} // namespace detail

enum class SamplePhase : std::uint8_t {
    unspecified,
    pre_step,
    post_step,
};

struct CaptureClock {
    RationalRateHz rate;
    std::uint64_t first_sample_index = 0;
    std::uint64_t first_timestamp_tick = 0;
    SamplePhase phase = SamplePhase::unspecified;

    [[nodiscard]] double timestamp_s(std::uint32_t frame_offset) const noexcept;

    friend bool operator==(const CaptureClock &, const CaptureClock &) = default;
};

struct PortIdentity {
    PortId id;
    CylinderId cylinder_id;
    PortKind kind = PortKind::unspecified;

    friend bool operator==(const PortIdentity &, const PortIdentity &) = default;
};

struct GasVolumeIdentity {
    GasVolumeId id;
    GasVolumeKind kind = GasVolumeKind::unspecified;

    friend bool operator==(const GasVolumeIdentity &,
                           const GasVolumeIdentity &) = default;
};

struct FlowEdgeIdentity {
    FlowEdgeId id;
    GasVolumeId endpoint_0_volume_id;
    GasVolumeId endpoint_1_volume_id;

    friend bool operator==(const FlowEdgeIdentity &,
                           const FlowEdgeIdentity &) = default;
};

struct RouteIdentity {
    RouteId id;
    SourceRouteKind kind = SourceRouteKind::unspecified;
    std::optional<GasVolumeId> source_volume_id;
    std::optional<RouteId> default_parent_route_id;
    std::optional<std::string> emitter_anchor_id;

    friend bool operator==(const RouteIdentity &, const RouteIdentity &) = default;
};

/**
 * Callback-scoped, non-owning identity view.
 *
 * Every referenced span must remain alive and immutable until the callback that
 * received this view returns. Do not retain, enqueue, or cross a thread boundary with
 * this view. The named factory makes the borrowing boundary explicit and avoids
 * accidental aggregate construction from temporary storage.
 */
class CaptureLayoutView {
  public:
    template <class CylinderRange, class PortRange, class GasVolumeRange,
              class FlowEdgeRange, class RouteRange>
        requires detail::CallbackBorrowRange<CylinderRange, CylinderId> &&
                 detail::CallbackBorrowRange<PortRange, PortIdentity> &&
                 detail::CallbackBorrowRange<GasVolumeRange, GasVolumeIdentity> &&
                 detail::CallbackBorrowRange<FlowEdgeRange, FlowEdgeIdentity> &&
                 detail::CallbackBorrowRange<RouteRange, RouteIdentity>
    [[nodiscard]] static CaptureLayoutView
    borrow_for_callback(EngineId engine_id, CylinderRange &&cylinders,
                        PortRange &&ports, GasVolumeRange &&gas_volumes,
                        FlowEdgeRange &&flow_edges, RouteRange &&routes) noexcept {
        return {
            engine_id,
            detail::callback_span<CylinderId>(std::forward<CylinderRange>(cylinders)),
            detail::callback_span<PortIdentity>(std::forward<PortRange>(ports)),
            detail::callback_span<GasVolumeIdentity>(
                std::forward<GasVolumeRange>(gas_volumes)),
            detail::callback_span<FlowEdgeIdentity>(
                std::forward<FlowEdgeRange>(flow_edges)),
            detail::callback_span<RouteIdentity>(std::forward<RouteRange>(routes)),
        };
    }

    [[nodiscard]] EngineId engine_id() const noexcept;
    [[nodiscard]] std::span<const CylinderId> cylinders() const noexcept;
    [[nodiscard]] std::span<const PortIdentity> ports() const noexcept;
    [[nodiscard]] std::span<const GasVolumeIdentity> gas_volumes() const noexcept;
    [[nodiscard]] std::span<const FlowEdgeIdentity> flow_edges() const noexcept;
    [[nodiscard]] std::span<const RouteIdentity> routes() const noexcept;

  private:
    CaptureLayoutView(EngineId engine_id, std::span<const CylinderId> cylinders,
                      std::span<const PortIdentity> ports,
                      std::span<const GasVolumeIdentity> gas_volumes,
                      std::span<const FlowEdgeIdentity> flow_edges,
                      std::span<const RouteIdentity> routes) noexcept;

    EngineId engine_id_;
    std::span<const CylinderId> cylinders_;
    std::span<const PortIdentity> ports_;
    std::span<const GasVolumeIdentity> gas_volumes_;
    std::span<const FlowEdgeIdentity> flow_edges_;
    std::span<const RouteIdentity> routes_;
};

enum class CaptureValidity : std::uint32_t {
    mechanism = 1U << 0U,
    thermodynamic_state = 1U << 1U,
    composition = 1U << 2U,
    gas_exchange = 1U << 3U,
    combustion = 1U << 4U,
    torque = 1U << 5U,
};

using CaptureValidityMask = std::uint32_t;

[[nodiscard]] constexpr CaptureValidityMask
capture_validity_mask(CaptureValidity validity) noexcept {
    return static_cast<CaptureValidityMask>(validity);
}

struct MixtureFractions {
    double fuel = 0.0;
    double inert = 0.0;
    double oxygen = 0.0;

    friend bool operator==(const MixtureFractions &,
                           const MixtureFractions &) = default;
};

struct EngineCaptureSample {
    std::uint64_t step_end_index = 0;
    CaptureValidityMask validity = 0;
    double theta_rad = 0.0;
    double theta_cycle_rad = 0.0;
    double angular_speed_rad_s = 0.0;
    double angular_acceleration_rad_s2 = 0.0;
    double engine_speed_rpm = 0.0;
    double requested_throttle_01 = 0.0;
    double resolved_engine_throttle_01 = 0.0;
    double intake_plate_position_01 = 0.0;
    double main_flow_multiplier_01 = 0.0;
    bool ignition_enabled = false;
    bool fuel_enabled = false;
    bool starter_enabled = false;
    bool dyno_enabled = false;
    bool limiter_enabled = false;
    bool limiter_cut_active = false;
    double requested_external_resisting_torque_nm = 0.0;
    TorqueTelemetry torque;
};

struct CylinderCaptureSample {
    CaptureValidityMask validity = 0;
    double chamber_volume_m3 = 0.0;
    double chamber_dvolume_dtheta_m3_per_rad = 0.0;
    double piston_velocity_m_s = 0.0;
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double amount_mol = 0.0;
    MixtureFractions composition;
    double combustion_heat_release_j = 0.0;
    double flame_radius_m = 0.0;
    double flame_axial_travel_m = 0.0;
    bool flame_active = false;
    TorqueValueNm indicated_gas_torque;
};

struct PortCaptureSample {
    CaptureValidityMask validity = 0;
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double signed_mass_flow_kg_s = 0.0;
    double effective_flow_area_m2 = 0.0;
    // Restriction K in mol/s = K * pressure_flow_potential:
    // square metres times sqrt(mol/kg).
    double effective_molar_flow_conductance_m2_sqrt_mol_per_kg = 0.0;
    double valve_lift_m = 0.0;
};

struct GasVolumeCaptureSample {
    CaptureValidityMask validity = 0;
    double volume_m3 = 0.0;
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double amount_mol = 0.0;
    double thermal_energy_j = 0.0;
    double momentum_x_kg_m_s = 0.0;
    double momentum_y_kg_m_s = 0.0;
    MixtureFractions composition;
};

struct FlowEdgeCaptureSample {
    CaptureValidityMask validity = 0;
    double signed_mass_flow_kg_s = 0.0;
};

struct GasSourceRouteCaptureSample {
    CaptureValidityMask validity = 0;
    double pressure_pa_abs = 0.0;
    double temperature_k = 0.0;
    double signed_mass_flow_kg_s = 0.0;
    double effective_area_m2 = 0.0;
};

struct MechanicalSourceRouteCaptureSample {
    CaptureValidityMask validity = 0;
    // Engine-local body wrench at the route's declared emitter anchor.
    std::array<double, 3> force_xyz_n{};
    std::array<double, 3> torque_xyz_nm{};
};

using SourceRouteCaptureSample =
    std::variant<GasSourceRouteCaptureSample, MechanicalSourceRouteCaptureSample>;

struct ReferenceParityCylinderSample {
    double exhaust_primary_static_pressure_pa_abs = 0.0;
    double dynamic_pressure_forward_pa = 0.0;
    double dynamic_pressure_reverse_pa = 0.0;
};

/**
 * Callback-scoped borrowed M3 comparator view. Its spans obey the same lifetime rule
 * as CaptureBlockView and are never retained by a consumer.
 */
class ReferenceParityBlockView {
  public:
    template <class SpeedRange, class CylinderRange>
        requires detail::CallbackBorrowRange<SpeedRange, double> &&
                 detail::CallbackBorrowRange<CylinderRange,
                                             ReferenceParityCylinderSample>
    [[nodiscard]] static ReferenceParityBlockView
    borrow_for_callback(SpeedRange &&filtered_engine_speed_rpm,
                        CylinderRange &&cylinders) noexcept {
        return {
            detail::callback_span<double>(
                std::forward<SpeedRange>(filtered_engine_speed_rpm)),
            detail::callback_span<ReferenceParityCylinderSample>(
                std::forward<CylinderRange>(cylinders)),
        };
    }

    [[nodiscard]] std::span<const double> filtered_engine_speed_rpm() const noexcept;
    [[nodiscard]] std::span<const ReferenceParityCylinderSample>
    cylinders() const noexcept;

  private:
    ReferenceParityBlockView(
        std::span<const double> filtered_engine_speed_rpm,
        std::span<const ReferenceParityCylinderSample> cylinders) noexcept;

    std::span<const double> filtered_engine_speed_rpm_;
    std::span<const ReferenceParityCylinderSample> cylinders_;
};

enum class IgnitionRejection : std::uint8_t {
    unspecified,
    active_flame,
    no_fuel,
    mixture_low,
    mixture_high,
};

enum class FlameExtinctionReason : std::uint8_t {
    unspecified,
    intake_transfer,
    no_geometric_progress,
};

struct SparkCrossing {
    CylinderId cylinder_id;
    double raw_saved_angle_rad = 0.0;
    double raw_current_angle_rad = 0.0;
    double adjusted_current_angle_rad = 0.0;
    double adjusted_spark_angle_rad = 0.0;
    double timing_advance_rad = 0.0;
};

struct LimiterStateChanged {
    bool old_active = false;
    bool new_active = false;
    bool overspeed_refreshed = false;
    double resulting_timer_s = 0.0;
};

struct IgnitionAccepted {
    CylinderId cylinder_id;
    double efficiency_01 = 0.0;
    double flame_speed_m_s = 0.0;
};

struct IgnitionRejected {
    CylinderId cylinder_id;
    IgnitionRejection reason = IgnitionRejection::unspecified;
};

struct FlameExtinguished {
    CylinderId cylinder_id;
    std::uint8_t gas_substep_index = 0;
    FlameExtinctionReason reason = FlameExtinctionReason::unspecified;
};

using EngineEventPayload =
    std::variant<SparkCrossing, LimiterStateChanged, IgnitionAccepted, IgnitionRejected,
                 FlameExtinguished>;

struct EngineEvent {
    std::uint32_t frame_offset = 0;
    std::uint8_t ordinal_within_step = 0;
    EngineEventPayload payload;
};

/**
 * Callback-scoped borrowed compressed-row journal. `offsets` contains one more
 * element than the block frame count.
 */
class EventJournalView {
  public:
    template <class OffsetRange, class EventRange>
        requires detail::CallbackBorrowRange<OffsetRange, std::uint32_t> &&
                 detail::CallbackBorrowRange<EventRange, EngineEvent>
    [[nodiscard]] static EventJournalView
    borrow_for_callback(OffsetRange &&offsets, EventRange &&events) noexcept {
        return {
            detail::callback_span<std::uint32_t>(std::forward<OffsetRange>(offsets)),
            detail::callback_span<EngineEvent>(std::forward<EventRange>(events)),
        };
    }

    [[nodiscard]] std::span<const std::uint32_t> offsets() const noexcept;
    [[nodiscard]] std::span<const EngineEvent> events() const noexcept;

  private:
    EventJournalView(std::span<const std::uint32_t> offsets,
                     std::span<const EngineEvent> events) noexcept;

    std::span<const std::uint32_t> offsets_;
    std::span<const EngineEvent> events_;
};

/**
 * Callback-scoped, non-owning simulator output.
 *
 * All entity arrays are frame-major: sample_index =
 * frame_index * layout_entity_count + entity_index. Entity order is exactly the
 * corresponding CaptureLayoutView span. All referenced storage must remain alive and
 * immutable until the callback returns; never retain or enqueue this view.
 */
class CaptureBlockView {
  public:
    template <class EngineRange, class CylinderRange, class PortRange,
              class GasVolumeRange, class FlowEdgeRange, class SourceRouteRange>
        requires detail::CallbackBorrowRange<EngineRange, EngineCaptureSample> &&
                 detail::CallbackBorrowRange<CylinderRange, CylinderCaptureSample> &&
                 detail::CallbackBorrowRange<PortRange, PortCaptureSample> &&
                 detail::CallbackBorrowRange<GasVolumeRange, GasVolumeCaptureSample> &&
                 detail::CallbackBorrowRange<FlowEdgeRange, FlowEdgeCaptureSample> &&
                 detail::CallbackBorrowRange<SourceRouteRange, SourceRouteCaptureSample>
    [[nodiscard]] static CaptureBlockView borrow_for_callback(
        CaptureLayoutView layout, CaptureClock clock, std::uint32_t frame_count,
        std::uint32_t declared_block_capacity_frames,
        std::uint32_t declared_event_journal_capacity_records, EngineRange &&engine,
        CylinderRange &&cylinders, PortRange &&ports, GasVolumeRange &&gas_volumes,
        FlowEdgeRange &&flow_edges, SourceRouteRange &&source_routes,
        EventJournalView event_journal,
        std::optional<ReferenceParityBlockView> reference_parity =
            std::nullopt) noexcept {
        return {
            layout,
            clock,
            frame_count,
            declared_block_capacity_frames,
            declared_event_journal_capacity_records,
            detail::callback_span<EngineCaptureSample>(
                std::forward<EngineRange>(engine)),
            detail::callback_span<CylinderCaptureSample>(
                std::forward<CylinderRange>(cylinders)),
            detail::callback_span<PortCaptureSample>(std::forward<PortRange>(ports)),
            detail::callback_span<GasVolumeCaptureSample>(
                std::forward<GasVolumeRange>(gas_volumes)),
            detail::callback_span<FlowEdgeCaptureSample>(
                std::forward<FlowEdgeRange>(flow_edges)),
            detail::callback_span<SourceRouteCaptureSample>(
                std::forward<SourceRouteRange>(source_routes)),
            event_journal,
            reference_parity,
        };
    }

    [[nodiscard]] const CaptureLayoutView &layout() const noexcept;
    [[nodiscard]] const CaptureClock &clock() const noexcept;
    [[nodiscard]] std::uint32_t frame_count() const noexcept;
    [[nodiscard]] std::uint32_t declared_block_capacity_frames() const noexcept;
    [[nodiscard]] std::uint32_t
    declared_event_journal_capacity_records() const noexcept;
    [[nodiscard]] std::span<const EngineCaptureSample> engine() const noexcept;
    [[nodiscard]] std::span<const CylinderCaptureSample> cylinders() const noexcept;
    [[nodiscard]] std::span<const PortCaptureSample> ports() const noexcept;
    [[nodiscard]] std::span<const GasVolumeCaptureSample> gas_volumes() const noexcept;
    [[nodiscard]] std::span<const FlowEdgeCaptureSample> flow_edges() const noexcept;
    [[nodiscard]] std::span<const SourceRouteCaptureSample>
    source_routes() const noexcept;
    [[nodiscard]] const EventJournalView &event_journal() const noexcept;
    [[nodiscard]] const std::optional<ReferenceParityBlockView> &
    reference_parity() const noexcept;

    [[nodiscard]] const EngineCaptureSample *
    engine_sample(std::size_t frame_index) const noexcept;
    [[nodiscard]] const CylinderCaptureSample *
    cylinder_sample(std::size_t frame_index, std::size_t cylinder_index) const noexcept;
    [[nodiscard]] const PortCaptureSample *
    port_sample(std::size_t frame_index, std::size_t port_index) const noexcept;
    [[nodiscard]] const GasVolumeCaptureSample *
    gas_volume_sample(std::size_t frame_index, std::size_t volume_index) const noexcept;
    [[nodiscard]] const FlowEdgeCaptureSample *
    flow_edge_sample(std::size_t frame_index, std::size_t edge_index) const noexcept;
    [[nodiscard]] const SourceRouteCaptureSample *
    source_route_sample(std::size_t frame_index,
                        std::size_t route_index) const noexcept;
    [[nodiscard]] const GasSourceRouteCaptureSample *
    gas_source_route_sample(std::size_t frame_index,
                            std::size_t route_index) const noexcept;
    [[nodiscard]] const MechanicalSourceRouteCaptureSample *
    mechanical_source_route_sample(std::size_t frame_index,
                                   std::size_t route_index) const noexcept;

  private:
    CaptureBlockView(CaptureLayoutView layout, CaptureClock clock,
                     std::uint32_t frame_count,
                     std::uint32_t declared_block_capacity_frames,
                     std::uint32_t declared_event_journal_capacity_records,
                     std::span<const EngineCaptureSample> engine,
                     std::span<const CylinderCaptureSample> cylinders,
                     std::span<const PortCaptureSample> ports,
                     std::span<const GasVolumeCaptureSample> gas_volumes,
                     std::span<const FlowEdgeCaptureSample> flow_edges,
                     std::span<const SourceRouteCaptureSample> source_routes,
                     EventJournalView event_journal,
                     std::optional<ReferenceParityBlockView> reference_parity) noexcept;

    CaptureLayoutView layout_;
    CaptureClock clock_;
    std::uint32_t frame_count_;
    std::uint32_t declared_block_capacity_frames_;
    std::uint32_t declared_event_journal_capacity_records_;
    std::span<const EngineCaptureSample> engine_;
    std::span<const CylinderCaptureSample> cylinders_;
    std::span<const PortCaptureSample> ports_;
    std::span<const GasVolumeCaptureSample> gas_volumes_;
    std::span<const FlowEdgeCaptureSample> flow_edges_;
    std::span<const SourceRouteCaptureSample> source_routes_;
    EventJournalView event_journal_;
    std::optional<ReferenceParityBlockView> reference_parity_;
};

[[nodiscard]] ValidationReport validate(const CaptureLayoutView &layout);
[[nodiscard]] ValidationReport validate(const CaptureBlockView &block);
[[nodiscard]] ValidationReport validate(const CaptureBlockView &block,
                                        const EngineSpec &engine,
                                        const RenderScenario &scenario);

} // namespace crankwave::contract
