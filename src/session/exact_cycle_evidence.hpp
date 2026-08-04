#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/session.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace engine_sim_offline::session {

enum class ExactCycleEvidenceErrorCode : std::uint8_t {
    invalid_configuration,
    malformed_capture_clock,
    noncontiguous_sample,
    nonfinite_sample,
    multiple_boundaries_in_interval,
    ordinal_overflow,
    output_capacity_exceeded,
    nonfinite_result,
};

struct ExactCycleEvidenceError {
    ExactCycleEvidenceErrorCode code =
        ExactCycleEvidenceErrorCode::invalid_configuration;
    std::uint64_t physics_frame = 0;

    friend bool operator==(const ExactCycleEvidenceError &,
                           const ExactCycleEvidenceError &) = default;
};

// Observes post-step engine capture without feeding any value back into simulation,
// excitation, presentation, or audio delivery. Completed evidence is appended to the
// caller's bounded scratch only at an exact 4*pi output-crank boundary.
class ExactCycleEvidenceAccumulator final {
  public:
    ExactCycleEvidenceAccumulator(double crank_tdc_reference_rad,
                                  contract::RationalRateHz delivery_rate) noexcept;

    [[nodiscard]] std::optional<ExactCycleEvidenceError>
    consume(const contract::CaptureClock &clock,
            std::span<const contract::EngineCaptureSample> samples,
            std::vector<EngineCompletedCycleEvidence> &completed) noexcept;

  private:
    struct TorquePoint {
        double value_nm = 0.0;
        contract::Availability availability = contract::Availability::unavailable;
        contract::Completeness completeness = contract::Completeness::incomplete;
        contract::QuantityUnavailableReason unavailable_reason =
            contract::QuantityUnavailableReason::model_not_admitted;
        contract::TorqueTermMask included_terms = 0;
        contract::TorqueTermMask omitted_terms = 0;
    };

    struct Point {
        std::uint64_t physics_frame = 0;
        double physical_tick = 0.0;
        double time_s = 0.0;
        double theta_rad = 0.0;
        double requested_throttle_01 = 0.0;
        double resolved_throttle_01 = 0.0;
        double intake_plate_position_01 = 0.0;
        TorquePoint net_shaft;
        EngineCycleStateFlagMask state_flags = 0;
        EngineCycleBoundaryEvidence boundary;
    };

    struct ControlAccumulator {
        double time_integral_s = 0.0;
        double minimum_01 = 0.0;
        double maximum_01 = 0.0;
        double last_value_01 = 0.0;
        std::uint32_t change_count = 0;
        bool has_value = false;
    };

    struct TorqueAccumulator {
        double angular_work_j = 0.0;
        contract::Availability availability = contract::Availability::available;
        contract::Completeness completeness = contract::Completeness::complete;
        contract::QuantityUnavailableReason unavailable_reason =
            contract::QuantityUnavailableReason::none;
        contract::TorqueTermMask included_terms = 0;
        contract::TorqueTermMask omitted_terms = 0;
        bool has_metadata = false;
    };

    [[nodiscard]] std::optional<ExactCycleEvidenceError>
    advance(const Point &current,
            std::vector<EngineCompletedCycleEvidence> &completed) noexcept;
    [[nodiscard]] Point interpolate_boundary(const Point &left, const Point &right,
                                             std::int64_t ordinal,
                                             double theta_rad) const noexcept;
    void begin_cycle(const Point &boundary) noexcept;
    void integrate_interval(const Point &left, const Point &right) noexcept;
    [[nodiscard]] EngineCompletedCycleEvidence
    finish_cycle(const Point &boundary) const noexcept;
    void absorb_torque_metadata(const TorquePoint &torque) noexcept;
    [[nodiscard]] double boundary_theta(std::int64_t ordinal) const noexcept;
    [[nodiscard]] double delivery_frame(double physical_tick) const noexcept;
    [[nodiscard]] std::optional<ExactCycleEvidenceError>
    fail(ExactCycleEvidenceErrorCode code, std::uint64_t physics_frame) noexcept;

    double crank_tdc_reference_rad_ = 0.0;
    contract::RationalRateHz delivery_rate_;
    contract::RationalRateHz active_capture_rate_;
    std::int64_t next_boundary_ordinal_ = 0;
    double next_boundary_theta_rad_ = 0.0;
    std::uint64_t completed_cycle_count_ = 0;
    bool configuration_valid_ = false;
    bool accumulating_full_cycle_ = false;
    std::optional<Point> previous_;
    EngineCycleBoundaryEvidence cycle_start_boundary_;
    double cycle_start_time_s_ = 0.0;
    EngineCycleStateFlagMask cycle_start_state_flags_ = 0;
    EngineCycleStateFlagMask cycle_end_state_flags_ = 0;
    EngineCycleStateFlagMask state_transition_flags_ = 0;
    bool has_interval_state_ = false;
    bool control_change_overflow_ = false;
    ControlAccumulator requested_throttle_;
    ControlAccumulator resolved_throttle_;
    ControlAccumulator intake_plate_position_;
    TorqueAccumulator net_shaft_;
    std::optional<ExactCycleEvidenceError> terminal_error_;
};

[[nodiscard]] const char *
exact_cycle_evidence_error_message(ExactCycleEvidenceErrorCode code) noexcept;

} // namespace engine_sim_offline::session
