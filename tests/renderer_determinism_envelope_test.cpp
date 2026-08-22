#include "determinism/renderer_determinism_envelope.hpp"

#include <array>
#include <cerrno>
#include <cfenv>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using namespace crankwave::determinism;

static_assert(!std::is_default_constructible_v<RendererDeterminismEnvelope>);
static_assert(
    !std::is_constructible_v<RendererDeterminismEnvelope, RendererSourceStamp,
                             LoadedRuntimeIdentity, RendererNumericEnvironment>);
static_assert(std::is_copy_constructible_v<RendererDeterminismEnvelope>);
static_assert(std::is_move_constructible_v<RendererDeterminismEnvelope>);
static_assert(!std::is_copy_assignable_v<RendererDeterminismEnvelope>);
static_assert(!std::is_move_assignable_v<RendererDeterminismEnvelope>);
static_assert(std::is_same_v<decltype(&renderer_determinism_envelope),
                             RendererDeterminismEnvelopeResult (*)()>);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string(message)};
    }
}

[[nodiscard]] RendererNumericEnvironmentSnapshot numeric_snapshot() {
    RendererNumericEnvironmentSnapshot snapshot;
    snapshot.build_policy = RendererNumericBuildPolicy::x86_64_v1_strict_v1;
    snapshot.is_linux_x86_64 = true;
    snapshot.is_sysv_lp64 = true;
    snapshot.pointer_storage_bytes = 8;
    snapshot.long_storage_bytes = 8;
    snapshot.arch_get_cpuid_observed = true;
    snapshot.cpuid_enabled = true;
    snapshot.cpuid_leaf1_observed = true;
    snapshot.maximum_basic_cpuid_leaf = 1;
    snapshot.cpuid_leaf1_edx = detail::kRequiredCpuidLeaf1Edx;
    snapshot.binary32 = {2, 24, -125, 128, 4, true};
    snapshot.binary64 = {2, 53, -1021, 1024, 8, true};
    snapshot.extended80 = {2, 64, -16381, 16384, 16, true};
    snapshot.float_evaluation_method = 0;
    snapshot.fe_rounding_mode = FE_TONEAREST;
    snapshot.mxcsr = detail::kRequiredMxcsrControl;
    snapshot.x87_control_word = detail::kRequiredX87Control;
    return snapshot;
}

[[nodiscard]] RendererSourceStamp source_stamp() {
    RendererSourceStamp stamp;
    stamp.source_state = RendererSourceState::clean;
    stamp.full_git_head = "0123456789abcdef0123456789abcdef01234567";
    stamp.source_closure_sha256.bytes[0] = 0x42;
    stamp.compiler_id = "GNU";
    stamp.compiler_version = "13.3.0";
    stamp.target_triple = "x86_64-linux-gnu";
    return stamp;
}

[[nodiscard]] LoadedRuntimeIdentity loaded_runtime() {
    LoadedRuntimeIdentity identity;
    identity.standard_library_id = "libstdcxx";
    identity.standard_library_identity = "standard-library-content-identity";
    identity.standard_library_headers.release = 13;
    identity.math_library_id = "glibc-libm";
    identity.math_library_identity = "math-library-content-identity";
    identity.compiler_runtime_id = "libgcc-s";
    identity.compiler_runtime_identity = "compiler-runtime-content-identity";
    return identity;
}

struct ObserverScript {
    std::array<RendererNumericEnvironmentSnapshot, 2> numeric{numeric_snapshot(),
                                                              numeric_snapshot()};
    RendererSourceStampResult source = source_stamp();
    LoadedRuntimeIdentityResult runtime = loaded_runtime();
    std::array<char, 8> calls{};
    std::size_t call_count = 0;
    std::size_t numeric_count = 0;
};

thread_local ObserverScript *active_script = nullptr;

void record_call(char call) noexcept {
    if (active_script->call_count < active_script->calls.size()) {
        active_script->calls[active_script->call_count++] = call;
    }
}

[[nodiscard]] RendererNumericEnvironmentSnapshot scripted_numeric() noexcept {
    record_call('N');
    errno = ERANGE;
    const auto index = active_script->numeric_count == 0 ? 0U : 1U;
    ++active_script->numeric_count;
    return active_script->numeric[index];
}

[[nodiscard]] RendererSourceStampResult scripted_source() {
    record_call('S');
    errno = EDOM;
    return active_script->source;
}

[[nodiscard]] LoadedRuntimeIdentityResult scripted_runtime() {
    record_call('R');
    errno = EILSEQ;
    return active_script->runtime;
}

[[nodiscard]] RendererDeterminismEnvelopeResult run(ObserverScript &script) {
    active_script = &script;
    try {
        auto result = detail::compose_renderer_determinism_envelope({
            &scripted_numeric,
            &scripted_source,
            &scripted_runtime,
        });
        active_script = nullptr;
        return result;
    } catch (...) {
        active_script = nullptr;
        throw;
    }
}

void expect_calls(const ObserverScript &script, std::string_view expected,
                  std::string_view message) {
    expect(std::string_view(script.calls.data(), script.call_count) == expected,
           message);
}

void expect_manifest_projection(const RendererDeterminismEnvelope &envelope,
                                const RendererSourceStamp &source,
                                const LoadedRuntimeIdentity &runtime) {
    const auto &numeric = envelope.numeric_environment();
    const auto &manifest = envelope.manifest_identity();
    expect(envelope.source_stamp() == source,
           "envelope changed the admitted source stamp");
    expect(envelope.loaded_runtime() == runtime,
           "envelope changed the admitted loaded-runtime evidence");
    expect(manifest.build.git_commit_id == source.full_git_head &&
               manifest.build.source_closure_sha256 == source.source_closure_sha256 &&
               manifest.build.compiler_id == source.compiler_id &&
               manifest.build.compiler_version == source.compiler_version &&
               manifest.build.target_triple == source.target_triple,
           "manifest projection changed source or toolchain identity");
    expect(manifest.build.standard_library_id == runtime.standard_library_id &&
               manifest.build.standard_library_identity ==
                   runtime.standard_library_identity &&
               manifest.build.math_library_id == runtime.math_library_id &&
               manifest.build.math_library_identity == runtime.math_library_identity &&
               manifest.build.compiler_runtime_id == runtime.compiler_runtime_id &&
               manifest.build.compiler_runtime_identity ==
                   runtime.compiler_runtime_identity,
           "manifest projection changed a runtime-provider identity");
    expect(manifest.numeric_policy_id == numeric.build_policy_id &&
               manifest.instruction_set_profile == numeric.instruction_set_profile &&
               manifest.floating_point.format == numeric.floating_point_format &&
               manifest.floating_point.rounding == numeric.rounding &&
               manifest.floating_point.fma_contraction == numeric.fma_contraction &&
               manifest.floating_point.flush_to_zero == numeric.flush_to_zero &&
               manifest.floating_point.denormals_are_zero == numeric.denormals_are_zero,
           "manifest projection changed numeric identity");
    expect(manifest.deterministic_worker_count == 1 &&
               manifest.deterministic_reduction_topology == "serial-stable-order",
           "manifest projection changed the fixed serial execution identity");
}

void test_success_is_sealed_and_preserves_errno() {
    ObserverScript script;
    const auto expected_source = std::get<RendererSourceStamp>(script.source);
    const auto expected_runtime = std::get<LoadedRuntimeIdentity>(script.runtime);
    errno = EBUSY;
    const auto result = run(script);
    expect(errno == EBUSY, "composition changed caller errno");
    expect_calls(script, "NSRN", "successful observer order changed");
    const auto *envelope = std::get_if<RendererDeterminismEnvelope>(&result);
    expect(envelope != nullptr, "complete admitted evidence did not compose");
    expect(!envelope->production_observation(),
           "scripted observers were marked as a production observation");
    expect_manifest_projection(*envelope, expected_source, expected_runtime);
}

void test_numeric_preflight_short_circuits_other_observers() {
    ObserverScript script;
    script.numeric[0].fe_rounding_mode = FE_DOWNWARD;
    script.source = RendererSourceStampError{RendererSourceStampErrorCode::dirty_source,
                                             RendererSourceState::dirty,
                                             "lower-priority source failure"};
    script.runtime = LoadedRuntimeError{LoadedRuntimeErrorCode::provider_missing,
                                        "libm", "tan", "lower-priority runtime"};
    const auto result = run(script);
    const auto *error = std::get_if<RendererNumericEnvironmentError>(&result);
    expect(error != nullptr &&
               error->code ==
                   RendererNumericEnvironmentErrorCode::rounding_mode_mismatch,
           "numeric preflight did not retain its typed rejection");
    expect_calls(script, "N", "numeric rejection did not short-circuit observers");
}

void test_source_failure_skips_runtime_but_gets_final_numeric_sample() {
    ObserverScript script;
    const RendererSourceStampError expected{RendererSourceStampErrorCode::dirty_source,
                                            RendererSourceState::dirty,
                                            "dirty source fixture"};
    script.source = expected;
    const auto result = run(script);
    const auto *error = std::get_if<RendererSourceStampError>(&result);
    expect(error != nullptr && *error == expected,
           "composition changed the typed source rejection");
    expect_calls(script, "NSN", "source rejection observer order changed");
}

void test_runtime_failure_is_retained_after_stable_final_sample() {
    ObserverScript script;
    const LoadedRuntimeError expected{LoadedRuntimeErrorCode::symbol_interposed, "libm",
                                      "tan", "interposed runtime fixture"};
    script.runtime = expected;
    const auto result = run(script);
    const auto *error = std::get_if<LoadedRuntimeError>(&result);
    expect(error != nullptr && *error == expected,
           "composition changed the typed runtime rejection");
    expect_calls(script, "NSRN", "runtime rejection observer order changed");
}

void test_numeric_mutation_outranks_observer_failure() {
    ObserverScript script;
    script.source = RendererSourceStampError{RendererSourceStampErrorCode::dirty_source,
                                             RendererSourceState::dirty,
                                             "source failure hidden by mutation"};
    script.numeric[1].x87_status_word = 1;
    const auto result = run(script);
    const auto *changed = std::get_if<RendererThreadStateChanged>(&result);
    expect(changed != nullptr && changed->before == script.numeric[0] &&
               changed->after == script.numeric[1],
           "numeric mutation did not retain exact before/after evidence");
    expect_calls(script, "NSN", "mutation detection observer order changed");
}

void test_incomplete_private_observer_set_throws() {
    bool threw = false;
    try {
        (void)detail::compose_renderer_determinism_envelope(
            {nullptr, &scripted_source, &scripted_runtime});
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    expect(threw, "incomplete private observer set did not fail safely");
}

void test_live_composition_is_read_only_and_fails_closed() {
    const auto before = observe_current_thread_renderer_numeric_environment();
    errno = E2BIG;
    const auto result = renderer_determinism_envelope();
    const int errno_after = errno;
    const auto after = observe_current_thread_renderer_numeric_environment();
    expect(errno_after == E2BIG, "live composition changed caller errno");
    expect(after == before, "live composition changed calling-thread numeric state");

    if (const auto *envelope = std::get_if<RendererDeterminismEnvelope>(&result)) {
        expect(envelope->production_observation(),
               "zero-argument live observer was marked as scripted");
        expect_manifest_projection(*envelope, envelope->source_stamp(),
                                   envelope->loaded_runtime());
        return;
    }
    if (const auto *source_error = std::get_if<RendererSourceStampError>(&result)) {
        expect(source_error->code == RendererSourceStampErrorCode::dirty_source ||
                   source_error->code ==
                       RendererSourceStampErrorCode::unavailable_source ||
                   source_error->code ==
                       RendererSourceStampErrorCode::unavailable_toolchain,
               "live source admission failed for malformed embedded evidence");
        return;
    }
    if (std::holds_alternative<LoadedRuntimeError>(result)) {
        return;
    }
    throw std::runtime_error{
        "admitted build returned an unexpected live determinism failure"};
}

void test_numeric_rejection_is_local_to_the_calling_thread() {
    const auto main_before = observe_current_thread_renderer_numeric_environment();
    bool mutation_installed = false;
    bool rejected_as_rounding = false;
    bool errno_preserved = false;
    std::thread worker([&] {
        mutation_installed = std::fesetround(FE_DOWNWARD) == 0;
        errno = ENOTRECOVERABLE;
        const auto result = renderer_determinism_envelope();
        errno_preserved = errno == ENOTRECOVERABLE;
        const auto *error = std::get_if<RendererNumericEnvironmentError>(&result);
        rejected_as_rounding =
            error != nullptr &&
            error->code == RendererNumericEnvironmentErrorCode::rounding_mode_mismatch;
    });
    worker.join();
    const auto main_after = observe_current_thread_renderer_numeric_environment();
    expect(mutation_installed, "worker could not install directed rounding");
    expect(rejected_as_rounding,
           "worker-local numeric mutation escaped determinism preflight");
    expect(errno_preserved, "worker rejection changed worker errno");
    expect(main_after == main_before,
           "worker-local numeric mutation reached the main thread");
}

} // namespace

int main() {
    test_success_is_sealed_and_preserves_errno();
    test_numeric_preflight_short_circuits_other_observers();
    test_source_failure_skips_runtime_but_gets_final_numeric_sample();
    test_runtime_failure_is_retained_after_stable_final_sample();
    test_numeric_mutation_outranks_observer_failure();
    test_incomplete_private_observer_set_throws();
    test_live_composition_is_read_only_and_fails_closed();
    test_numeric_rejection_is_local_to_the_calling_thread();
}
