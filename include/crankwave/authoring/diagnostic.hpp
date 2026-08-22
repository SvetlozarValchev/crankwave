#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace crankwave::authoring {

enum class DiagnosticSeverity : std::uint8_t {
    error,
    warning,
};

enum class DiagnosticCode : std::uint8_t {
    malformed_document,
    unsupported_schema,
    missing_value,
    unknown_field,
    invalid_type,
    invalid_unit,
    invalid_value,
    out_of_range,
    duplicate_id,
    dangling_reference,
    forbidden_cycle,
    disconnected_object,
    inconsistent_value,
    unsupported_capability,
    missing_asset,
    asset_hash_mismatch,
    resource_limit,
    internal_failure,
};

struct SourcePosition {
    std::uint64_t byte_offset = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    friend bool operator==(const SourcePosition &, const SourcePosition &) = default;
};

struct DiagnosticSubject {
    std::string object_kind;
    std::string object_id;

    friend bool operator==(const DiagnosticSubject &,
                           const DiagnosticSubject &) = default;
};

struct RelatedDiagnosticLocation {
    std::string json_pointer;
    std::optional<DiagnosticSubject> subject;
    std::string message;

    friend bool operator==(const RelatedDiagnosticLocation &,
                           const RelatedDiagnosticLocation &) = default;
};

// json_pointer is an RFC 6901 path into the authored document. It may be empty for a
// document-level syntax failure. subject remains absent when parsing failed before an
// object identity was available.
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::error;
    DiagnosticCode code = DiagnosticCode::invalid_value;
    std::string json_pointer;
    std::optional<DiagnosticSubject> subject;
    std::optional<SourcePosition> source_position;
    std::string message;
    std::vector<RelatedDiagnosticLocation> related;

    friend bool operator==(const Diagnostic &, const Diagnostic &) = default;
};

struct DiagnosticReport {
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &diagnostic : diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::error) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool ok() const noexcept {
        return !has_errors();
    }

    friend bool operator==(const DiagnosticReport &,
                           const DiagnosticReport &) = default;
};

} // namespace crankwave::authoring
