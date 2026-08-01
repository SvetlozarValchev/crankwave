#include "bmw_m52b28_render_gate_support.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

namespace {

namespace gate = engine_sim_offline::test::bmw_m52b28_render_gate;
namespace contract = engine_sim_offline::contract;

// These generic identities were established by a clean public JSON compile/render.
// They intentionally do not inherit obsolete BMW-profile provenance/container IDs.
constexpr std::string_view kExpectedGenericRequestIdentitySha256 =
    "1bc3db1bb89611b6ce9717b0faafa1bc62ed3b56482d0cecbf341d2fbed73127";
constexpr std::uint64_t kExpectedGenericAuditionWaveByteCount = UINT64_C(8640586);
constexpr std::string_view kExpectedGenericAuditionWaveSha256 =
    "f603ffed10dfe95b895084140cac46c448cafc4127c96b1671e53575b47ae552";

[[nodiscard]] std::string validation_text(const contract::ValidationReport &report) {
    std::string result;
    for (const auto &issue : report.issues) {
        if (!result.empty()) {
            result += "; ";
        }
        result += issue.path + ": " + issue.message;
    }
    return result.empty() ? "no validation detail" : result;
}

[[nodiscard]] const contract::RenderSuccess &
require_success(const contract::RenderResult &result) {
    if (const auto *success = std::get_if<contract::RenderSuccess>(&result)) {
        return *success;
    }
    if (const auto *failure = std::get_if<contract::RenderFailure>(&result)) {
        throw std::runtime_error{"public compiled-scenario render failed (" +
                                 failure->context.detail_code +
                                 "): " + failure->context.state_summary + "; " +
                                 validation_text(failure->validation)};
    }
    const auto &unreachable = std::get<contract::UnreachableTarget>(result);
    throw std::runtime_error{
        "public compiled-scenario render returned unreachable target (" +
        unreachable.context.detail_code + "): " + unreachable.context.state_summary};
}

void require_pinned_generic_identities(const gate::RenderIdentityObservation &actual) {
    const bool request_matches =
        actual.simulation_request_sha256 == kExpectedGenericRequestIdentitySha256;
    const bool wave_size_matches =
        actual.audition_wave_byte_count == kExpectedGenericAuditionWaveByteCount;
    const bool wave_hash_matches =
        actual.audition_wave_sha256 == kExpectedGenericAuditionWaveSha256;
    if (request_matches && wave_size_matches && wave_hash_matches) {
        return;
    }

    std::cerr << "checkpoint4.generic-render-identity-mismatch\n"
              << "generic simulation request SHA-256: "
              << actual.simulation_request_sha256 << '\n'
              << "generic whole-audition WAV byte count: "
              << actual.audition_wave_byte_count << '\n'
              << "generic whole-audition WAV SHA-256: " << actual.audition_wave_sha256
              << '\n';
    throw std::runtime_error{"checkpoint4.generic-render-identity-mismatch"};
}

void run(const std::filesystem::path &repository_root, const bool compile_only) {
    auto scenario = gate::compile_authored_scenario(repository_root);
    if (compile_only) {
        std::cout << "public JSON compile succeeded: " << scenario.id() << '\n';
        return;
    }

    std::stop_source cancellation;
    cancellation.request_stop();
    gate::VerifyingMemorySink cancelled_sink;
    const auto cancelled = engine_sim_offline::bake(
        scenario, cancelled_sink,
        engine_sim_offline::RenderControl{cancellation.get_token()});
    const auto *cancelled_failure =
        std::get_if<contract::RenderFailure>(&cancelled);
    gate::expect(
        cancelled_failure != nullptr &&
            cancelled_failure->context.kind == contract::FailureKind::cancelled &&
            cancelled_sink.begin_calls == 0U &&
            engine_sim_offline::validate_bake_result(cancelled, scenario).ok(),
        "pre-requested bake cancellation touched publication or failed validation");

    gate::VerifyingMemorySink sink;
    const auto result = engine_sim_offline::bake(scenario, sink);
    const auto &success = require_success(result);
    const auto oracle = gate::read_bytes(
        repository_root / "reference/oracles/bmw-m52b28/"
                          "bmw-m52b28-last-good-ffcc45c-dyno-1500-6500rpm.wav");
    const auto observation = gate::verify_render_success(success, sink, oracle);
    require_pinned_generic_identities(observation);
}

} // namespace

int main(const int argc, const char *const *argv) {
    try {
        const bool compile_only =
            argc == 3 && std::string_view{argv[2]} == "--compile-only";
        gate::expect(argc == 2 || compile_only,
                     "usage: bmw_m52b28_authored_json_render_gate_test "
                     "<repository-root> [--compile-only]");
        run(std::filesystem::canonical(argv[1]), compile_only);
    } catch (const std::exception &error) {
        std::cerr << "BMW authored JSON render gate failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
