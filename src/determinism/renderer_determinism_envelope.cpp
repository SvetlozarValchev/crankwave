#include "determinism/renderer_determinism_envelope.hpp"

#include <cerrno>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace crankwave::determinism {
namespace {

class ErrnoRestore final {
  public:
    ErrnoRestore() noexcept : saved_(errno) {}
    ErrnoRestore(const ErrnoRestore &) = delete;
    ErrnoRestore &operator=(const ErrnoRestore &) = delete;
    ~ErrnoRestore() noexcept {
        errno = saved_;
    }

  private:
    int saved_ = 0;
};

[[nodiscard]] contract::DeterminismEnvelope
make_manifest_identity(const RendererSourceStamp &source_stamp,
                       const LoadedRuntimeIdentity &loaded_runtime,
                       const RendererNumericEnvironment &numeric_environment) {
    contract::DeterminismEnvelope identity;
    identity.build.git_commit_id = source_stamp.full_git_head;
    identity.build.source_closure_sha256 = source_stamp.source_closure_sha256;
    identity.build.compiler_id = source_stamp.compiler_id;
    identity.build.compiler_version = source_stamp.compiler_version;
    identity.build.target_triple = source_stamp.target_triple;
    identity.build.standard_library_id = loaded_runtime.standard_library_id;
    identity.build.standard_library_identity = loaded_runtime.standard_library_identity;
    identity.build.math_library_id = loaded_runtime.math_library_id;
    identity.build.math_library_identity = loaded_runtime.math_library_identity;
    identity.build.compiler_runtime_id = loaded_runtime.compiler_runtime_id;
    identity.build.compiler_runtime_identity = loaded_runtime.compiler_runtime_identity;
    identity.numeric_policy_id = numeric_environment.build_policy_id;
    identity.instruction_set_profile = numeric_environment.instruction_set_profile;
    identity.floating_point.format = numeric_environment.floating_point_format;
    identity.floating_point.rounding = numeric_environment.rounding;
    identity.floating_point.fma_contraction = numeric_environment.fma_contraction;
    identity.floating_point.flush_to_zero = numeric_environment.flush_to_zero;
    identity.floating_point.denormals_are_zero = numeric_environment.denormals_are_zero;
    identity.deterministic_worker_count = 1;
    identity.deterministic_reduction_topology = "serial-stable-order";
    return identity;
}

} // namespace

RendererDeterminismEnvelope::RendererDeterminismEnvelope(
    RendererSourceStamp source_stamp, LoadedRuntimeIdentity loaded_runtime,
    RendererNumericEnvironment numeric_environment, bool production_observation)
    : source_stamp_(std::move(source_stamp)),
      loaded_runtime_(std::move(loaded_runtime)),
      numeric_environment_(numeric_environment),
      manifest_identity_(
          make_manifest_identity(source_stamp_, loaded_runtime_, numeric_environment_)),
      production_observation_(production_observation) {}

const RendererSourceStamp &RendererDeterminismEnvelope::source_stamp() const noexcept {
    return source_stamp_;
}

const LoadedRuntimeIdentity &
RendererDeterminismEnvelope::loaded_runtime() const noexcept {
    return loaded_runtime_;
}

const RendererNumericEnvironment &
RendererDeterminismEnvelope::numeric_environment() const noexcept {
    return numeric_environment_;
}

const contract::DeterminismEnvelope &
RendererDeterminismEnvelope::manifest_identity() const noexcept {
    return manifest_identity_;
}

bool RendererDeterminismEnvelope::production_observation() const noexcept {
    return production_observation_;
}

namespace detail {

struct RendererDeterminismEnvelopeFactory {
    [[nodiscard]] static RendererDeterminismEnvelope
    make(RendererSourceStamp source_stamp, LoadedRuntimeIdentity loaded_runtime,
         RendererNumericEnvironment numeric_environment, bool production_observation) {
        return RendererDeterminismEnvelope{std::move(source_stamp),
                                           std::move(loaded_runtime),
                                           numeric_environment, production_observation};
    }
};

} // namespace detail
namespace {

RendererDeterminismEnvelopeResult compose_renderer_determinism_envelope_impl(
    const detail::RendererDeterminismObservers &observers,
    bool production_observation) {
    const ErrnoRestore restore_errno;
    if (observers.numeric_environment == nullptr || observers.source_stamp == nullptr ||
        observers.loaded_runtime == nullptr) {
        throw std::invalid_argument{"renderer determinism observer set is incomplete"};
    }

    const auto numeric_before = observers.numeric_environment();
    const auto numeric_result = validate_renderer_numeric_environment(numeric_before);
    const auto *numeric_identity =
        std::get_if<RendererNumericEnvironment>(&numeric_result);
    if (numeric_identity == nullptr) {
        return std::get<RendererNumericEnvironmentError>(numeric_result);
    }

    auto source_result = observers.source_stamp();
    std::optional<LoadedRuntimeIdentityResult> runtime_result;
    if (std::holds_alternative<RendererSourceStamp>(source_result)) {
        runtime_result.emplace(observers.loaded_runtime());
    }

    // This is deliberately the final observer call. A full raw comparison retains
    // even sticky x87 status changes that do not affect canonical admission.
    const auto numeric_after = observers.numeric_environment();
    if (numeric_after != numeric_before) {
        return RendererThreadStateChanged{numeric_before, numeric_after};
    }

    if (auto *source_error = std::get_if<RendererSourceStampError>(&source_result)) {
        return std::move(*source_error);
    }
    if (auto *runtime_error =
            std::get_if<LoadedRuntimeError>(&runtime_result.value())) {
        return std::move(*runtime_error);
    }

    return detail::RendererDeterminismEnvelopeFactory::make(
        std::get<RendererSourceStamp>(std::move(source_result)),
        std::get<LoadedRuntimeIdentity>(std::move(runtime_result.value())),
        *numeric_identity, production_observation);
}

} // namespace
namespace detail {

RendererDeterminismEnvelopeResult
compose_renderer_determinism_envelope(const RendererDeterminismObservers &observers) {
    return compose_renderer_determinism_envelope_impl(observers, false);
}

} // namespace detail

RendererDeterminismEnvelopeResult renderer_determinism_envelope() {
    return compose_renderer_determinism_envelope_impl(
        {
            &observe_current_thread_renderer_numeric_environment,
            &renderer_source_stamp,
            &loaded_runtime_identity,
        },
        true);
}

} // namespace crankwave::determinism
