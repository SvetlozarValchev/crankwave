#pragma once

#include "crankwave/authoring/diagnostic.hpp"
#include "crankwave/c_api.h"
#include "crankwave/compile.hpp"
#include "crankwave/session.hpp"

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

namespace crankwave::c_api {

enum class HandleKind : std::uint8_t {
    engine = 1,
    scenario = 2,
    session = 3,
    crankwave = 4,
};

inline constexpr std::uint64_t kHandleSlotMask = UINT64_C(0x000fffff);
inline constexpr std::uint64_t kHandleContextMask = UINT64_C(0x000fffff);
inline constexpr std::uint32_t kHandleKindShift = 20U;
inline constexpr std::uint32_t kHandleContextShift = 24U;
inline constexpr std::uint32_t kHandleGenerationShift = 44U;
inline constexpr std::uint32_t kMaximumHandleGeneration = 0x000fffffU;

struct ErrorRecord {
    crankwave_status_t status = CRANKWAVE_STATUS_OK;
    crankwave_error_stage_t stage = CRANKWAVE_ERROR_STAGE_NONE;
    crankwave_error_code_t code = CRANKWAVE_ERROR_NONE;
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

struct CrankwaveEntry {
    std::vector<std::byte> bytes;
    std::string engine_id;
    std::string profile_id;
    std::uint64_t entry_count = 0U;
    std::uint64_t held_cell_count = 0U;
    std::uint64_t directional_capture_count = 0U;
    std::uint64_t lifecycle_capture_count = 0U;
    contract::Sha256Digest container_sha256;
    contract::Sha256Digest cache_identity_sha256;
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
        if (handle == CRANKWAVE_INVALID_HANDLE) {
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

[[nodiscard]] bool valid(crankwave_utf8_view_t view) noexcept;
[[nodiscard]] bool valid(crankwave_byte_view_t view) noexcept;

void clear_error(crankwave_context &context) noexcept;

[[nodiscard]] crankwave_status_t set_error(crankwave_context &context, crankwave_status_t status,
                                     crankwave_error_stage_t stage, crankwave_error_code_t code,
                                     std::string detail_code, std::string message);

[[nodiscard]] crankwave_status_t set_diagnostics(crankwave_context &context, crankwave_status_t status,
                                           crankwave_error_stage_t stage,
                                           std::string detail_code, std::string message,
                                           authoring::DiagnosticReport report);

void set_unexpected_error_noexcept(crankwave_context &context, bool resource) noexcept;

template <class Operation>
[[nodiscard]] crankwave_status_t boundary(crankwave_context &context,
                                    Operation &&operation) noexcept {
    try {
        return std::forward<Operation>(operation)();
    } catch (const std::bad_alloc &) {
        set_unexpected_error_noexcept(context, true);
        return CRANKWAVE_STATUS_RESOURCE_EXHAUSTED;
    } catch (...) {
        set_unexpected_error_noexcept(context, false);
        return CRANKWAVE_STATUS_INTERNAL_ERROR;
    }
}

[[nodiscard]] crankwave_status_t copy_text(std::string_view text,
                                     crankwave_mutable_utf8_buffer_t buffer) noexcept;

[[nodiscard]] crankwave_diagnostic_severity_t
diagnostic_severity(authoring::DiagnosticSeverity severity) noexcept;
[[nodiscard]] crankwave_diagnostic_code_t
diagnostic_code(authoring::DiagnosticCode code) noexcept;
[[nodiscard]] crankwave_error_code_t session_error_code(EngineSessionErrorCode code) noexcept;
[[nodiscard]] crankwave_error_code_t
control_error_code(EngineControlRejectionCode code) noexcept;
[[nodiscard]] crankwave_audio_bus_kind_t audio_bus_kind(EngineAudioBusKind kind) noexcept;
[[nodiscard]] crankwave_source_route_kind_t
source_route_kind(contract::SourceRouteKind kind) noexcept;
[[nodiscard]] crankwave_audio_signal_disposition_t
audio_signal_disposition(EngineAudioSignalDisposition disposition) noexcept;

[[nodiscard]] crankwave_quantity_value_t
quantity_value(const contract::QuantityValue &value) noexcept;
[[nodiscard]] crankwave_torque_value_nm_t
torque_value(const contract::TorqueValueNm &value) noexcept;
[[nodiscard]] crankwave_engine_telemetry_t
engine_telemetry(const contract::EngineCaptureSample &engine) noexcept;
[[nodiscard]] crankwave_session_telemetry_t
session_telemetry(const EngineTelemetryFrame &frame) noexcept;
[[nodiscard]] crankwave_completed_cycle_evidence_t
completed_cycle_evidence(const EngineCompletedCycleEvidence &cycle) noexcept;

} // namespace crankwave::c_api

struct crankwave_context {
    explicit crankwave_context(std::uint32_t tag)
        : engines(tag), scenarios(tag), sessions(tag), crankwaves(tag),
          context_tag(tag) {}

    crankwave::c_api::HandleRegistry<
        crankwave::compile::CompiledEngine,
        crankwave::c_api::HandleKind::engine>
        engines;
    crankwave::c_api::HandleRegistry<
        crankwave::compile::CompiledScenario,
        crankwave::c_api::HandleKind::scenario>
        scenarios;
    crankwave::c_api::HandleRegistry<
        crankwave::c_api::SessionEntry,
        crankwave::c_api::HandleKind::session>
        sessions;
    crankwave::c_api::HandleRegistry<
        crankwave::c_api::CrankwaveEntry,
        crankwave::c_api::HandleKind::crankwave>
        crankwaves;
    std::optional<crankwave::c_api::ErrorRecord> last_error;
    std::uint32_t context_tag = 0U;
};
