#include "compile/stable_id.hpp"

#include "compile/diagnostics.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace crankwave::compile::detail {
namespace {

struct OwnedStableIdSource {
    std::string authored_id;
    std::string json_pointer;
};

[[nodiscard]] bool valid_authored_id(std::string_view value) noexcept {
    if (value.empty() || value.size() > 128U) {
        return false;
    }
    const auto alphanumeric = [](const char byte) {
        return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9');
    };
    if (!alphanumeric(value.front())) {
        return false;
    }
    return std::ranges::all_of(value.substr(1), [&](const char byte) {
        return alphanumeric(byte) || byte == '.' || byte == '_' || byte == '-';
    });
}

[[nodiscard]] bool canonical_less(std::string_view left,
                                  std::string_view right) noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(),
        [](const char left_byte, const char right_byte) {
            return static_cast<unsigned char>(left_byte) <
                   static_cast<unsigned char>(right_byte);
        });
}

} // namespace

StableIdAssignmentResult
assign_stable_runtime_ids(std::string_view object_namespace,
                          std::span<const StableIdSource> sources) noexcept {
    try {
        if (!contract::is_valid_semantic_id(object_namespace)) {
            return diagnostic(authoring::DiagnosticCode::invalid_value, "",
                              "runtime object namespace must be a canonical "
                              "semantic ID");
        }
        if (sources.size() >
            static_cast<std::size_t>(std::numeric_limits<RuntimeObjectId>::max())) {
            return diagnostic(authoring::DiagnosticCode::resource_limit, "",
                              "object namespace exceeds the runtime-ID capacity");
        }

        std::vector<OwnedStableIdSource> ordered;
        ordered.reserve(sources.size());
        for (const auto &source : sources) {
            if (!valid_authored_id(source.authored_id)) {
                return diagnostic(
                    authoring::DiagnosticCode::invalid_value, source.json_pointer,
                    "stable ID is not in the canonical authoring grammar");
            }
            ordered.push_back({
                std::string{source.authored_id},
                std::string{source.json_pointer},
            });
        }
        std::ranges::sort(ordered, [](const auto &left, const auto &right) {
            return canonical_less(left.authored_id, right.authored_id);
        });

        for (std::size_t index = 1; index < ordered.size(); ++index) {
            if (ordered[index - 1U].authored_id == ordered[index].authored_id) {
                auto report = diagnostic(authoring::DiagnosticCode::duplicate_id,
                                         ordered[index].json_pointer,
                                         "stable ID '" + ordered[index].authored_id +
                                             "' is duplicated in object namespace '" +
                                             std::string{object_namespace} + "'");
                report.diagnostics.front().related.push_back({
                    ordered[index - 1U].json_pointer,
                    std::nullopt,
                    "first declaration",
                });
                return report;
            }
        }

        std::vector<StableIdAssignment> assignments;
        assignments.reserve(ordered.size());
        for (std::size_t index = 0; index < ordered.size(); ++index) {
            assignments.push_back({
                std::string{object_namespace},
                std::move(ordered[index].authored_id),
                static_cast<RuntimeObjectId>(index + 1U),
            });
        }
        return assignments;
    } catch (const std::bad_alloc &) {
        return resource_failure("assigning stable runtime IDs");
    } catch (...) {
        return internal_failure("assigning stable runtime IDs");
    }
}

} // namespace crankwave::compile::detail
