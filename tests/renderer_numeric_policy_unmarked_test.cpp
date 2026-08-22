#include "determinism/renderer_numeric_environment.hpp"

#include <stdexcept>
#include <variant>

int main() {
    using namespace crankwave::determinism;

    const auto snapshot = observe_current_thread_renderer_numeric_environment();
    if (snapshot.build_policy != RendererNumericBuildPolicy::unmarked) {
        throw std::runtime_error{"generated-zero numeric policy was marked"};
    }

    const auto result = validate_renderer_numeric_environment(snapshot);
    const auto *error = std::get_if<RendererNumericEnvironmentError>(&result);
    if (error == nullptr) {
        throw std::runtime_error{"generated-zero numeric policy was admitted"};
    }
    if (snapshot.is_linux_x86_64 && snapshot.is_sysv_lp64 &&
        error->code != RendererNumericEnvironmentErrorCode::build_policy_unmarked) {
        throw std::runtime_error{"generated-zero policy failed at the wrong boundary"};
    }
}
