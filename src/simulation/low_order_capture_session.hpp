#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_inertial_dyno_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace engine_sim_offline::simulation {

using LowOrderCaptureBlockConsumer =
    std::function<bool(const contract::CaptureBlockView &)>;

struct LowOrderCaptureBlockPublished {
    std::uint64_t block_ordinal = 0;
    std::uint64_t first_sample_index = 0;
    std::uint32_t frame_count = 0;
    std::uint64_t published_sample_count = 0;

    friend bool operator==(const LowOrderCaptureBlockPublished &,
                           const LowOrderCaptureBlockPublished &) = default;
};

struct LowOrderCaptureCompleted {
    std::uint64_t sample_count = 0;
    std::uint64_t block_count = 0;
    std::optional<contract::HeldSpeedOperatingPointResult> held_speed_operating_point;
    std::optional<contract::InertialDynoResult> inertial_dyno;

    friend bool operator==(const LowOrderCaptureCompleted &,
                           const LowOrderCaptureCompleted &) = default;
};

using LowOrderCaptureAdvanceResult =
    std::variant<LowOrderCaptureBlockPublished, LowOrderCaptureCompleted,
                 contract::FailureContext>;

namespace detail {
class LowOrderCaptureBuffer;
}

/**
 * One admitted low-order capture job.
 *
 * The mechanics and gas sessions are deliberately private and advance only as one
 * transaction here. A caller can therefore neither feed a step from another
 * mechanics session into gas nor observe a half-built capture block. Capture views
 * borrow session-owned storage and are valid only for the synchronous consumer call.
 */
class LowOrderCaptureSession final {
  public:
    LowOrderCaptureSession(const LowOrderCaptureSession &) = delete;
    LowOrderCaptureSession &operator=(const LowOrderCaptureSession &) = delete;
    LowOrderCaptureSession(LowOrderCaptureSession &&) noexcept;
    LowOrderCaptureSession &operator=(LowOrderCaptureSession &&) noexcept;
    ~LowOrderCaptureSession();

    // Builds, validates, and synchronously publishes at most one declared-capacity
    // block.
    // Consumer rejection or an exception is a stable terminal contract failure.
    [[nodiscard]] LowOrderCaptureAdvanceResult
    publish_next_block(const LowOrderCaptureBlockConsumer &consumer);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::uint64_t published_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t published_block_count() const noexcept;

  private:
    using ProfilePolicy =
        std::variant<LowOrderOperatingPointV1Runtime, LowOrderInertialDynoV1Runtime>;

    LowOrderCaptureSession(LowOrderEngineCoreV1Runtime core,
                           ProfilePolicy profile_policy,
                           detail::LowOrderCaptureBuffer capture,
                           contract::RationalRateHz rate,
                           std::uint64_t expected_samples, std::string model_id,
                           std::string profile_id, std::string scenario_id,
                           contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          const LegacyMechanismStep *mechanics = nullptr) const;
    [[nodiscard]] LowOrderCaptureAdvanceResult fail(contract::FailureContext failure);

    LowOrderEngineCoreV1Runtime core_;
    ProfilePolicy profile_policy_;
    std::unique_ptr<detail::LowOrderCaptureBuffer> capture_;
    contract::RationalRateHz rate_;
    std::uint64_t expected_samples_ = 0;
    std::uint64_t published_sample_count_ = 0;
    std::uint64_t published_block_count_ = 0;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    bool consumer_callback_active_ = false;
    std::optional<LowOrderCaptureCompleted> terminal_completion_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LowOrderCaptureSession, contract::ValidationReport>
    compile_low_order_capture_session(const contract::EngineSpec &,
                                      const contract::RenderScenario &,
                                      const contract::RandomPlan &,
                                      const contract::Sha256Digest &);
};

using LowOrderCaptureCompileResult =
    std::variant<LowOrderCaptureSession, contract::ValidationReport>;

// Compiles one coherent mechanics+gas+capture session. It retains no references to
// either request and has no fixture-reader or presentation dependency.
// The request identity binds operating-point and inertial evidence to the admitted
// render request.
[[nodiscard]] LowOrderCaptureCompileResult compile_low_order_capture_session(
    const contract::EngineSpec &engine, const contract::RenderScenario &scenario,
    const contract::RandomPlan &random_plan,
    const contract::Sha256Digest &simulation_request_identity_v3_sha256);

} // namespace engine_sim_offline::simulation
