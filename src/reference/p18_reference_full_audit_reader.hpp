#pragma once

#include "reference/p18_reference_audit_reader.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <variant>
#include <vector>

namespace engine_sim_offline::reference {

// Reference-only comparator lanes from one ESOAUD01 record. These values are
// deliberately not presentation input: a listening-gate tool may compare a live
// excitation session with them, but must pass only the live session's output view to
// the presentation renderer.
struct P18FullReferenceAuditFrame {
    std::array<double, kP18ReferenceAuditCylinderCount> pre_delay_cylinders{};
    std::array<double, kP18ReferenceAuditCylinderCount> post_delay_cylinders{};
    std::array<double, kP18ReferenceAuditBusCount> pre_dsp_buses{};

    friend bool operator==(const P18FullReferenceAuditFrame &,
                           const P18FullReferenceAuditFrame &) = default;
};

struct P18DecodedFullReferenceAudit {
    std::vector<P18FullReferenceAuditFrame> frames;

    friend bool operator==(const P18DecodedFullReferenceAudit &,
                           const P18DecodedFullReferenceAudit &) = default;
};

using P18FullReferenceAuditDecodeResult =
    std::variant<P18DecodedFullReferenceAudit, P18ReferenceAuditDecodeError>;

// Strict, path-free ESOAUD01 decoder retaining all fourteen binary64 lanes. The
// canonical framing, record indices, interval, and finiteness checks are delegated
// to decode_p18_reference_audit before any full-lane result is published.
[[nodiscard]] P18FullReferenceAuditDecodeResult
decode_p18_full_reference_audit(std::span<const std::byte> bytes);

} // namespace engine_sim_offline::reference
