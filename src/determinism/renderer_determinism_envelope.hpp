#pragma once

#include "determinism/loaded_runtime_identity.hpp"
#include "determinism/renderer_numeric_environment.hpp"
#include "determinism/renderer_source_stamp.hpp"
#include "crankwave/contract/render_manifest.hpp"

#include <variant>

namespace crankwave::determinism {

namespace detail {
struct RendererDeterminismEnvelopeFactory;
}

// A successfully composed renderer identity. Construction is private so callers can
// retain or copy its evidence without mutating components. Only the zero-argument
// production observer marks an envelope as acceptable for publication; the scripted
// composition seam exists for focused tests and is explicitly non-production.
class RendererDeterminismEnvelope final {
  public:
    RendererDeterminismEnvelope(const RendererDeterminismEnvelope &) = default;
    RendererDeterminismEnvelope(RendererDeterminismEnvelope &&) noexcept = default;
    RendererDeterminismEnvelope &
    operator=(const RendererDeterminismEnvelope &) = delete;
    RendererDeterminismEnvelope &operator=(RendererDeterminismEnvelope &&) = delete;

    [[nodiscard]] const RendererSourceStamp &source_stamp() const noexcept;
    [[nodiscard]] const LoadedRuntimeIdentity &loaded_runtime() const noexcept;
    [[nodiscard]] const RendererNumericEnvironment &
    numeric_environment() const noexcept;
    [[nodiscard]] const contract::DeterminismEnvelope &
    manifest_identity() const noexcept;
    // True only when the zero-argument production observer composed this value.
    // The private scripted test seam deliberately produces false.
    [[nodiscard]] bool production_observation() const noexcept;

  private:
    friend struct detail::RendererDeterminismEnvelopeFactory;

    RendererDeterminismEnvelope(RendererSourceStamp source_stamp,
                                LoadedRuntimeIdentity loaded_runtime,
                                RendererNumericEnvironment numeric_environment,
                                bool production_observation);

    RendererSourceStamp source_stamp_;
    LoadedRuntimeIdentity loaded_runtime_;
    RendererNumericEnvironment numeric_environment_;
    contract::DeterminismEnvelope manifest_identity_;
    bool production_observation_ = false;
};

// A changed snapshot means one of the other observers mutated renderer-visible
// thread state. It outranks their individual errors because those errors were
// observed under a numerically unstable transaction.
struct RendererThreadStateChanged {
    RendererNumericEnvironmentSnapshot before;
    RendererNumericEnvironmentSnapshot after;

    friend bool operator==(const RendererThreadStateChanged &,
                           const RendererThreadStateChanged &) = default;
};

// Each error alternative retains the exact typed rejection from its owning observer.
using RendererDeterminismEnvelopeResult =
    std::variant<RendererDeterminismEnvelope, RendererNumericEnvironmentError,
                 RendererSourceStampError, LoadedRuntimeError,
                 RendererThreadStateChanged>;

// The only production entry point: no cache, CLI value, or caller identity can
// participate in the result.
[[nodiscard]] RendererDeterminismEnvelopeResult renderer_determinism_envelope();

namespace detail {

// Private pure-composition seam for focused error-order and mutation tests. These
// observers are never accepted by the renderer, CLI, or public API.
struct RendererDeterminismObservers {
    RendererNumericEnvironmentSnapshot (*numeric_environment)() noexcept;
    RendererSourceStampResult (*source_stamp)();
    LoadedRuntimeIdentityResult (*loaded_runtime)();
};

[[nodiscard]] RendererDeterminismEnvelopeResult
compose_renderer_determinism_envelope(const RendererDeterminismObservers &observers);

} // namespace detail
} // namespace crankwave::determinism
