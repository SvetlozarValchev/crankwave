#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crankwave::contract {

template <class Tag> struct StableId {
    std::uint32_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    friend constexpr auto operator<=>(const StableId &, const StableId &) = default;
};

using EngineId = StableId<struct EngineIdTag>;
using CrankshaftId = StableId<struct CrankshaftIdTag>;
using BankId = StableId<struct BankIdTag>;
using IntakeId = StableId<struct IntakeIdTag>;
using CylinderId = StableId<struct CylinderIdTag>;
using PortId = StableId<struct PortIdTag>;
using GasVolumeId = StableId<struct GasVolumeIdTag>;
using FlowEdgeId = StableId<struct FlowEdgeIdTag>;
using RouteId = StableId<struct RouteIdTag>;
using AudioAssetId = StableId<struct AudioAssetIdTag>;
using RigId = StableId<struct RigIdTag>;
using VehicleId = StableId<struct VehicleIdTag>;
using TransmissionId = StableId<struct TransmissionIdTag>;
using GearId = StableId<struct GearIdTag>;

struct Sha256Digest {
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] bool is_zero() const noexcept;

    friend bool operator==(const Sha256Digest &, const Sha256Digest &) = default;
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::byte> payload) noexcept;

struct RationalRateHz {
    std::uint64_t numerator = 0;
    std::uint64_t denominator = 0;

    friend bool operator==(const RationalRateHz &, const RationalRateHz &) = default;
};

// Binary64 scenario times are resolved to integer frame indices once, at admission.
// The bound keeps every accepted index in binary64's consecutive-integer range; all
// scheduling after this conversion uses integers and never repeated floating addition.
inline constexpr std::uint64_t kMaximumResolvedFrameIndex = (UINT64_C(1) << 53U) - 1U;

[[nodiscard]] std::optional<std::uint64_t>
resolve_frame_index(double time_s, const RationalRateHz &rate) noexcept;

struct RenderRates {
    RationalRateHz physics;
    RationalRateHz capture;
    RationalRateHz source_processing;
    RationalRateHz acoustic;
    RationalRateHz delivery;

    friend bool operator==(const RenderRates &, const RenderRates &) = default;
};

struct MethodIdentity {
    std::string id;
    std::uint32_t version = 0;
    Sha256Digest configuration_sha256;

    friend bool operator==(const MethodIdentity &, const MethodIdentity &) = default;
};

struct MethodSelection {
    std::string id;
    std::uint32_t version = 0;

    friend bool operator==(const MethodSelection &, const MethodSelection &) = default;
};

// Canonical identity of the currently admitted low-order engine/excitation model.
// Keeping this in the contract registry prevents compilers and executors from
// duplicating an opaque configuration digest.
[[nodiscard]] const MethodIdentity &legacy_low_order_v1_method_identity();

enum class ContractIssueCode : std::uint8_t {
    missing_value,
    invalid_value,
    duplicate_identity,
    dangling_reference,
    inconsistent_shape,
    inconsistent_semantics,
    unsupported_value,
};

struct ContractIssue {
    ContractIssueCode code = ContractIssueCode::invalid_value;
    std::string path;
    std::string message;

    friend bool operator==(const ContractIssue &, const ContractIssue &) = default;
};

struct ValidationReport {
    std::vector<ContractIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }

    void add(ContractIssueCode code, std::string path, std::string message);

    void append(ValidationReport other);
};

[[nodiscard]] bool is_valid_semantic_id(std::string_view value) noexcept;
[[nodiscard]] ValidationReport validate(const RationalRateHz &rate);
[[nodiscard]] ValidationReport validate(const RenderRates &rates);
[[nodiscard]] ValidationReport validate(const MethodIdentity &method);

} // namespace crankwave::contract
