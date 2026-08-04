#pragma once

#include "engine_sim_offline/authoring/diagnostic.hpp"
#include "engine_sim_offline/c_api.h"
#include "engine_sim_offline/compile.hpp"
#include "engine_sim_offline/session.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::c_api {

enum class HandleKind : std::uint8_t {
    engine = 1,
    scenario = 2,
    session = 3,
};

inline constexpr std::uint64_t kHandleSlotMask = UINT64_C(0x000fffff);
inline constexpr std::uint64_t kHandleContextMask = UINT64_C(0x000fffff);
inline constexpr std::uint32_t kHandleKindShift = 20U;
inline constexpr std::uint32_t kHandleContextShift = 24U;
inline constexpr std::uint32_t kHandleGenerationShift = 44U;
inline constexpr std::uint32_t kMaximumHandleGeneration = 0x000fffffU;

struct ErrorRecord {
    eso_status_t status = ESO_STATUS_OK;
    eso_error_stage_t stage = ESO_ERROR_STAGE_NONE;
    eso_error_code_t code = ESO_ERROR_NONE;
    std::string detail_code;
    std::string message;
    std::vector<authoring::Diagnostic> diagnostics;
};

struct SessionEntry {
    explicit SessionEntry(EngineSession session_value)
        : session(std::move(session_value)),
          control_scratch(
              session.descriptor().capacities.control_command_queue_capacity) {}

    EngineSession session;
    // Fixed at session creation. Valid control submission performs no ABI-side
    // allocation and passes only the populated prefix to EngineSession.
    std::vector<EngineControlCommand> control_scratch;
    std::uint64_t emitted_block_count = 0;
    bool terminal = false;
};

template <class Value, HandleKind Kind> class HandleRegistry {
  public:
    explicit HandleRegistry(const std::uint32_t context_tag) noexcept
        : context_tag_(context_tag) {}

    HandleRegistry(const HandleRegistry &) = delete;
    HandleRegistry &operator=(const HandleRegistry &) = delete;
    HandleRegistry(HandleRegistry &&) = delete;
    HandleRegistry &operator=(HandleRegistry &&) = delete;

    template <class Input> [[nodiscard]] std::uint64_t insert(Input &&value) {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            auto &slot = slots_[index];
            if (!slot.retired && !slot.value.has_value()) {
                slot.value.emplace(std::forward<Input>(value));
                return encode(index, slot.generation);
            }
        }
        if (slots_.size() >= kHandleSlotMask) {
            throw std::bad_alloc{};
        }
        slots_.emplace_back();
        auto &slot = slots_.back();
        slot.value.emplace(std::forward<Input>(value));
        return encode(slots_.size() - 1U, slot.generation);
    }

    [[nodiscard]] Value *get(const std::uint64_t handle) noexcept {
        const auto decoded = decode(handle);
        if (!decoded.has_value()) {
            return nullptr;
        }
        auto &slot = slots_[decoded->index];
        if (slot.generation != decoded->generation || !slot.value.has_value()) {
            return nullptr;
        }
        return &*slot.value;
    }

    [[nodiscard]] const Value *get(const std::uint64_t handle) const noexcept {
        const auto decoded = decode(handle);
        if (!decoded.has_value()) {
            return nullptr;
        }
        const auto &slot = slots_[decoded->index];
        if (slot.generation != decoded->generation || !slot.value.has_value()) {
            return nullptr;
        }
        return &*slot.value;
    }

    [[nodiscard]] bool erase(const std::uint64_t handle) noexcept {
        const auto decoded = decode(handle);
        if (!decoded.has_value()) {
            return false;
        }
        auto &slot = slots_[decoded->index];
        if (slot.generation != decoded->generation || !slot.value.has_value()) {
            return false;
        }
        slot.value.reset();
        if (slot.generation == kMaximumHandleGeneration) {
            // Never wrap a generation: retire this slot so an arbitrarily old
            // handle cannot become valid again.
            slot.retired = true;
            slot.generation = 0U;
        } else {
            ++slot.generation;
        }
        return true;
    }

  private:
    struct Slot {
        std::optional<Value> value;
        std::uint32_t generation = 1U;
        bool retired = false;
    };

    struct DecodedHandle {
        std::size_t index = 0;
        std::uint32_t generation = 0;
    };

    [[nodiscard]] std::uint64_t encode(const std::size_t index,
                                       const std::uint32_t generation) noexcept {
        const auto slot = static_cast<std::uint64_t>(index + 1U);
        return (static_cast<std::uint64_t>(generation) << kHandleGenerationShift) |
               (static_cast<std::uint64_t>(context_tag_) << kHandleContextShift) |
               (static_cast<std::uint64_t>(Kind) << kHandleKindShift) | slot;
    }

    [[nodiscard]] std::optional<DecodedHandle>
    decode(const std::uint64_t handle) const noexcept {
        if (handle == ESO_INVALID_HANDLE) {
            return std::nullopt;
        }
        const auto kind =
            static_cast<std::uint8_t>((handle >> kHandleKindShift) & UINT64_C(0x0f));
        const auto context_tag = static_cast<std::uint32_t>(
            (handle >> kHandleContextShift) & kHandleContextMask);
        const auto slot = handle & kHandleSlotMask;
        const auto generation = static_cast<std::uint32_t>(
            (handle >> kHandleGenerationShift) & kMaximumHandleGeneration);
        if (kind != static_cast<std::uint8_t>(Kind) || context_tag != context_tag_ ||
            slot == 0U || generation == 0U || slot > slots_.size()) {
            return std::nullopt;
        }
        return DecodedHandle{static_cast<std::size_t>(slot - 1U), generation};
    }

    std::uint32_t context_tag_ = 0U;
    std::vector<Slot> slots_;
};

[[nodiscard]] bool valid(eso_utf8_view_t view) noexcept;
[[nodiscard]] bool valid(eso_byte_view_t view) noexcept;

void clear_error(eso_context &context) noexcept;

[[nodiscard]] eso_status_t set_error(eso_context &context, eso_status_t status,
                                     eso_error_stage_t stage, eso_error_code_t code,
                                     std::string detail_code, std::string message);

[[nodiscard]] eso_status_t set_diagnostics(eso_context &context, eso_status_t status,
                                           eso_error_stage_t stage,
                                           std::string detail_code, std::string message,
                                           authoring::DiagnosticReport report);

void set_unexpected_error_noexcept(eso_context &context, bool resource) noexcept;

template <class Operation>
[[nodiscard]] eso_status_t boundary(eso_context &context,
                                    Operation &&operation) noexcept {
    try {
        return std::forward<Operation>(operation)();
    } catch (const std::bad_alloc &) {
        set_unexpected_error_noexcept(context, true);
        return ESO_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        set_unexpected_error_noexcept(context, false);
        return ESO_STATUS_INTERNAL_ERROR;
    }
}

[[nodiscard]] eso_status_t copy_text(std::string_view text,
                                     eso_mutable_utf8_buffer_t buffer) noexcept;

[[nodiscard]] eso_diagnostic_severity_t
diagnostic_severity(authoring::DiagnosticSeverity severity) noexcept;
[[nodiscard]] eso_diagnostic_code_t
diagnostic_code(authoring::DiagnosticCode code) noexcept;
[[nodiscard]] eso_error_code_t session_error_code(EngineSessionErrorCode code) noexcept;
[[nodiscard]] eso_error_code_t
control_error_code(EngineControlRejectionCode code) noexcept;
[[nodiscard]] eso_audio_bus_kind_t audio_bus_kind(EngineAudioBusKind kind) noexcept;
[[nodiscard]] eso_source_route_kind_t
source_route_kind(contract::SourceRouteKind kind) noexcept;
[[nodiscard]] eso_audio_signal_disposition_t
audio_signal_disposition(EngineAudioSignalDisposition disposition) noexcept;

[[nodiscard]] eso_quantity_value_t
quantity_value(const contract::QuantityValue &value) noexcept;
[[nodiscard]] eso_torque_value_nm_t
torque_value(const contract::TorqueValueNm &value) noexcept;
[[nodiscard]] eso_engine_telemetry_t
engine_telemetry(const contract::EngineCaptureSample &engine) noexcept;
[[nodiscard]] eso_session_telemetry_t
session_telemetry(const EngineTelemetryFrame &frame) noexcept;
[[nodiscard]] eso_completed_cycle_evidence_t
completed_cycle_evidence(const EngineCompletedCycleEvidence &cycle) noexcept;

} // namespace engine_sim_offline::c_api

struct eso_context {
    explicit eso_context(std::uint32_t tag)
        : engines(tag), scenarios(tag), sessions(tag), context_tag(tag) {}

    engine_sim_offline::c_api::HandleRegistry<
        engine_sim_offline::compile::CompiledEngine,
        engine_sim_offline::c_api::HandleKind::engine>
        engines;
    engine_sim_offline::c_api::HandleRegistry<
        engine_sim_offline::compile::CompiledScenario,
        engine_sim_offline::c_api::HandleKind::scenario>
        scenarios;
    engine_sim_offline::c_api::HandleRegistry<
        engine_sim_offline::c_api::SessionEntry,
        engine_sim_offline::c_api::HandleKind::session>
        sessions;
    std::optional<engine_sim_offline::c_api::ErrorRecord> last_error;
    std::uint32_t context_tag = 0U;
};
