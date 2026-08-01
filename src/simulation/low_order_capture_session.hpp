#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/live_control.hpp"
#include "simulation/low_order_dynamic_crank_runtime.hpp"
#include "simulation/low_order_engine_core_v1_runtime.hpp"
#include "simulation/low_order_inertial_dyno_v1_runtime.hpp"
#include "simulation/low_order_operating_point_v1_runtime.hpp"
#include "simulation/low_order_prescribed_kinematic_runtime.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace engine_sim_offline::simulation {

/**
 * Non-owning, synchronous callback for one capture publication.
 *
 * The target is borrowed only for the duration of publish_next_block(). Keeping this
 * seam as a context pointer plus function pointer prevents type-erasure storage from
 * allocating on the block-producing path.
 */
class LowOrderCaptureBlockConsumer final {
  public:
    constexpr LowOrderCaptureBlockConsumer() noexcept = default;

    template <class Consumer>
        requires(!std::same_as<std::remove_cvref_t<Consumer>,
                               LowOrderCaptureBlockConsumer> &&
                 std::is_object_v<std::remove_reference_t<Consumer>> &&
                 std::invocable<Consumer &, const contract::CaptureBlockView &> &&
                 std::convertible_to<
                     std::invoke_result_t<Consumer &,
                                          const contract::CaptureBlockView &>,
                     bool>)
    LowOrderCaptureBlockConsumer(Consumer &&consumer) noexcept
        : context_(
              const_cast<void *>(static_cast<const void *>(std::addressof(consumer)))),
          invoke_([](void *context, const contract::CaptureBlockView &block) -> bool {
              using Target = std::remove_reference_t<Consumer>;
              return static_cast<bool>(
                  std::invoke(*static_cast<Target *>(context), block));
          }) {}

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return context_ != nullptr && invoke_ != nullptr;
    }

    [[nodiscard]] bool operator()(const contract::CaptureBlockView &block) const {
        return invoke_(context_, block);
    }

  private:
    void *context_ = nullptr;
    bool (*invoke_)(void *, const contract::CaptureBlockView &) = nullptr;
};

static_assert(std::is_trivially_copyable_v<LowOrderCaptureBlockConsumer>);

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
    [[nodiscard]] LowOrderCaptureAdvanceResult
    publish_next_block(const LowOrderCaptureBlockConsumer &consumer,
                       const detail::LowOrderLiveControlProvider &live_controls);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::uint64_t published_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t published_block_count() const noexcept;
    [[nodiscard]] std::optional<HeldDynoRuntimeStateView>
    held_dyno_state() const noexcept;
    [[nodiscard]] std::optional<FreeVehicleRuntimeStateView>
    free_vehicle_state() const noexcept;

  private:
    using ProfilePolicy =
        std::variant<LowOrderOperatingPointV1Runtime, LowOrderInertialDynoV1Runtime,
                     LowOrderDynamicCrankRuntime, LowOrderPrescribedKinematicRuntime>;

    LowOrderCaptureSession(LowOrderEngineCoreV1Runtime core,
                           ProfilePolicy profile_policy,
                           detail::LowOrderCaptureBuffer capture,
                           contract::RationalRateHz rate,
                           LowOrderExecutionExtent execution_extent,
                           std::string model_id, std::string profile_id,
                           std::string scenario_id, contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          const LegacyMechanismStep *mechanics = nullptr) const;
    [[nodiscard]] LowOrderCaptureAdvanceResult fail(contract::FailureContext failure);
    [[nodiscard]] LowOrderCaptureAdvanceResult
    publish_next_block_impl(const LowOrderCaptureBlockConsumer &consumer,
                            const detail::LowOrderLiveControlProvider *live_controls);

    LowOrderEngineCoreV1Runtime core_;
    ProfilePolicy profile_policy_;
    std::unique_ptr<detail::LowOrderCaptureBuffer> capture_;
    contract::RationalRateHz rate_;
    LowOrderExecutionExtent execution_extent_ =
        LowOrderExecutionExtent::finite_scenario(0U);
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
                                      const contract::Sha256Digest &,
                                      LowOrderExecutionExtent);
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
    const contract::Sha256Digest &simulation_request_identity_v6_sha256,
    LowOrderExecutionExtent execution_extent);

} // namespace engine_sim_offline::simulation
