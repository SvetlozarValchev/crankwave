#pragma once

#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/result.hpp"
#include "engine_sim_offline/contract/scenario.hpp"
#include "simulation/legacy_low_order_gas.hpp"
#include "simulation/legacy_low_order_mechanics.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace engine_sim_offline::simulation {

inline constexpr std::uint32_t kLegacyCaptureFramesPerBlock = 200U;
inline constexpr std::uint32_t kLegacyMaximumEventsPerFrame = 19U;

using LegacyCaptureBlockConsumer =
    std::function<bool(const contract::CaptureBlockView &)>;

struct LegacySimulationBlockPublished {
    std::uint64_t block_ordinal = 0;
    std::uint64_t first_sample_index = 0;
    std::uint32_t frame_count = 0;
    std::uint64_t published_sample_count = 0;

    friend bool operator==(const LegacySimulationBlockPublished &,
                           const LegacySimulationBlockPublished &) = default;
};

struct LegacySimulationCompleted {
    std::uint64_t sample_count = 0;
    std::uint64_t block_count = 0;

    friend bool operator==(const LegacySimulationCompleted &,
                           const LegacySimulationCompleted &) = default;
};

using LegacySimulationAdvanceResult =
    std::variant<LegacySimulationBlockPublished, LegacySimulationCompleted,
                 contract::FailureContext>;

namespace detail {
class LegacyLowOrderCaptureBuffer;
}

/**
 * One admitted legacy-low-order simulation job.
 *
 * The mechanics and gas sessions are deliberately private and advance only as one
 * transaction here. A caller can therefore neither feed a step from another
 * mechanics session into gas nor observe a half-built capture block. Capture views
 * borrow session-owned storage and are valid only for the synchronous consumer call.
 */
class LegacyLowOrderSimulationSession final {
  public:
    LegacyLowOrderSimulationSession(const LegacyLowOrderSimulationSession &) = delete;
    LegacyLowOrderSimulationSession &
    operator=(const LegacyLowOrderSimulationSession &) = delete;
    LegacyLowOrderSimulationSession(LegacyLowOrderSimulationSession &&) noexcept;
    LegacyLowOrderSimulationSession &
    operator=(LegacyLowOrderSimulationSession &&) noexcept;
    ~LegacyLowOrderSimulationSession();

    // Builds, validates, and synchronously publishes at most one 200-frame block.
    // Consumer rejection or an exception is a stable terminal contract failure.
    [[nodiscard]] LegacySimulationAdvanceResult
    publish_next_block(const LegacyCaptureBlockConsumer &consumer);

    [[nodiscard]] bool faulted() const noexcept;
    [[nodiscard]] bool completed() const noexcept;
    [[nodiscard]] std::uint64_t published_sample_count() const noexcept;
    [[nodiscard]] std::uint64_t published_block_count() const noexcept;

  private:
    LegacyLowOrderSimulationSession(LegacyLowOrderMechanicsSession mechanics,
                                    LegacyLowOrderGasSession gas,
                                    detail::LegacyLowOrderCaptureBuffer capture,
                                    std::uint64_t expected_samples,
                                    std::string model_id, std::string profile_id,
                                    std::string scenario_id,
                                    contract::EngineId engine_id);

    [[nodiscard]] contract::FailureContext
    fault(contract::FailureKind kind, std::string detail_code,
          std::string state_summary,
          const LegacyMechanismStep *mechanics = nullptr) const;
    [[nodiscard]] LegacySimulationAdvanceResult fail(contract::FailureContext failure);

    LegacyLowOrderMechanicsSession mechanics_;
    LegacyLowOrderGasSession gas_;
    std::unique_ptr<detail::LegacyLowOrderCaptureBuffer> capture_;
    std::uint64_t expected_samples_ = 0;
    std::uint64_t published_sample_count_ = 0;
    std::uint64_t published_block_count_ = 0;
    std::string model_id_;
    std::string profile_id_;
    std::string scenario_id_;
    contract::EngineId engine_id_;
    bool consumer_callback_active_ = false;
    std::optional<LegacySimulationCompleted> terminal_completion_;
    std::optional<contract::FailureContext> terminal_fault_;

    friend std::variant<LegacyLowOrderSimulationSession, contract::ValidationReport>
    compile_legacy_low_order_simulation_session(const contract::EngineSpec &,
                                                const contract::RenderScenario &);
};

using LegacySimulationCompileResult =
    std::variant<LegacyLowOrderSimulationSession, contract::ValidationReport>;

// Compiles one coherent mechanics+gas+capture session. It retains no references to
// either request and has no fixture-reader or presentation dependency.
[[nodiscard]] LegacySimulationCompileResult
compile_legacy_low_order_simulation_session(const contract::EngineSpec &engine,
                                            const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::simulation
