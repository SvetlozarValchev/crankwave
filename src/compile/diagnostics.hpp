#pragma once

#include "crankwave/authoring/diagnostic.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace crankwave::compile::detail {

[[nodiscard]] inline authoring::DiagnosticReport
diagnostic(authoring::DiagnosticCode code, std::string_view path, std::string message) {
    authoring::Diagnostic value;
    value.code = code;
    value.json_pointer = std::string{path};
    value.message = std::move(message);
    return authoring::DiagnosticReport{{std::move(value)}};
}

[[nodiscard]] inline authoring::DiagnosticReport
resource_failure(std::string_view operation) {
    return diagnostic(authoring::DiagnosticCode::resource_limit, "",
                      "allocation failed while " + std::string{operation});
}

[[nodiscard]] inline authoring::DiagnosticReport
internal_failure(std::string_view operation) {
    return diagnostic(authoring::DiagnosticCode::internal_failure, "",
                      "unexpected failure while " + std::string{operation});
}

} // namespace crankwave::compile::detail
