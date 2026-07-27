#include "engine_sim_offline/artifacts/reference_manifest_encoder.hpp"

#include "reference_manifest_test_support.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;
using namespace engine_sim_offline::artifacts;
using namespace engine_sim_offline::contract;
using namespace engine_sim_offline::contract::test;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] std::string digest_hex(const Sha256Digest &digest) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] RenderManifest make_manifest(ReferenceManifestFixture &fixture) {
    return {
        fixture.content,
        ExecutionFacts{
            "reference-manifest-encoder-test-v2",
            "2026-07-27T12:34:56Z",
            std::chrono::nanoseconds{UINT64_C(1234567890)},
            "linux",
            "test-cpu",
            16,
            1,
            1,
            UINT64_C(123456),
        },
    };
}

[[nodiscard]] std::vector<std::byte> require_encoding(const RenderManifest &manifest) {
    auto result = encode_reference_manifest_v2(manifest);
    if (const auto *error = std::get_if<RenderSinkError>(&result)) {
        throw std::runtime_error(error->detail_code + ": " + error->message);
    }
    return std::move(std::get<ManifestEncoding>(result).bytes);
}

[[nodiscard]] std::string as_string(const std::vector<std::byte> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

void expect_error(const RenderManifest &manifest, std::string_view detail_code) {
    const auto result = encode_reference_manifest_v2(manifest);
    const auto *error = std::get_if<RenderSinkError>(&result);
    expect(error != nullptr, "invalid manifest unexpectedly encoded");
    expect(error->kind == RenderSinkErrorKind::protocol_violation,
           "invalid manifest returned the wrong error kind");
    expect(error->detail_code == detail_code,
           "invalid manifest returned the wrong detail code");
}

void test_complete_golden_document() {
    ReferenceManifestFixture fixture;
    const auto manifest = make_manifest(fixture);
    expect(validate(manifest, fixture.builder.provenance,
                    bmw_m52b28_reference_source_matrix_v1())
               .ok(),
           "encoder golden fixture is not a complete valid manifest");

    const auto first = require_encoding(manifest);
    const auto second = require_encoding(manifest);
    expect(first == second, "identical manifests produced different bytes");
    const auto document = as_string(first);
    expect(document.starts_with("{\"wire_schema\":\"engine-sim-offline.render-manifest."
                                "reference-presentation.v2\",\"content\":{"),
           "canonical manifest prefix changed");
    expect(document.ends_with("}}\n"), "canonical manifest suffix changed");
    expect(std::count(document.begin(), document.end(), '\n') == 1,
           "canonical manifest contains non-terminal whitespace");
    expect(document.find("\"public_seed\":\"0x0000000000c0ffee\"") != std::string::npos,
           "u64 seed encoding changed");
    const auto source_digest_key = document.find("\"source_closure_sha256\":");
    const auto standard_identity_key = document.find("\"standard_library_identity\":");
    const auto math_identity_key = document.find("\"math_library_identity\":");
    const auto compiler_runtime_id_key =
        document.find("\"compiler_runtime_id\":\"libgcc-s\"");
    const auto compiler_runtime_identity_key =
        document.find("\"compiler_runtime_identity\":");
    const auto numeric_policy_key =
        document.find("\"numeric_policy_id\":"
                      "\"x86-64-v1-binary64-x87-extended-strict-v1\"");
    expect(source_digest_key != std::string::npos &&
               standard_identity_key != std::string::npos &&
               math_identity_key != std::string::npos &&
               compiler_runtime_id_key != std::string::npos &&
               compiler_runtime_identity_key != std::string::npos &&
               numeric_policy_key != std::string::npos &&
               source_digest_key < standard_identity_key &&
               standard_identity_key < math_identity_key &&
               math_identity_key < compiler_runtime_id_key &&
               compiler_runtime_id_key < compiler_runtime_identity_key &&
               compiler_runtime_identity_key < numeric_policy_key,
           "runtime/numeric identities or canonical determinism-key order changed");
    expect(document.find("\"impulse_response_gain_linear\":{"
                         "\"value\":\"0x3f50624dd2f1a9fc\"") != std::string::npos,
           "binary64 bit encoding changed");
    expect(document.find("\"cylinder_id\":null") != std::string::npos &&
               document.find("\"peak_resident_bytes\":"
                             "\"0x000000000001e240\"") != std::string::npos,
           "optional encoding changed");

    const auto actual_hash = digest_hex(sha256(first));
    constexpr std::string_view kExpectedHash =
        "ccda33c6cbf976e2267c093d36db52950cfceaa0419583df4c18ad82fcd28eba";
    if (actual_hash != kExpectedHash) {
        std::cerr << "reference manifest golden hash: " << actual_hash << '\n';
        throw std::runtime_error("canonical reference manifest hash changed");
    }
}

void test_fail_closed_boundaries() {
    ReferenceManifestFixture fixture;
    auto manifest = make_manifest(fixture);
    manifest.content.inputs = SimulationManifestInputs{};
    expect_error(manifest, "reference-manifest-input-kind-unsupported");

    manifest = make_manifest(fixture);
    manifest.execution.reset();
    expect_error(manifest, "reference-manifest-execution-missing");

    manifest = make_manifest(fixture);
    reference_inputs(manifest.content).presentation.conditioning.jitter_scale.value =
        std::numeric_limits<double>::infinity();
    expect_error(manifest, "reference-manifest-wire-nonfinite");

    manifest = make_manifest(fixture);
    manifest.execution->host_os = std::string{"\xc0\xaf", 2};
    expect_error(manifest, "reference-manifest-wire-invalid-utf8");

    manifest = make_manifest(fixture);
    manifest.content.output_contract.distribution = DistributionIntent::unspecified;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.schema_version = 1;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.schema_version = 3;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.build.standard_library_id = "libcxx";
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.build.math_library_id = "other-libm";
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.build.compiler_runtime_id = "other-runtime";
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.numeric_policy_id = "other-numeric-policy-v1";
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.instruction_set_profile = "x86-64-v2";
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.floating_point.flush_to_zero = true;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.content.determinism.deterministic_worker_count = 2;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    reference_inputs(manifest.content).schema_version = 2;
    expect_error(manifest, "reference-manifest-wire-unrepresentable");

    manifest = make_manifest(fixture);
    manifest.execution->host_os.assign(4U * 1024U * 1024U, 'x');
    expect_error(manifest, "reference-manifest-wire-size-exceeded");

    for (const auto &invalid_utf8 : {
             std::string{"\xed\xa0\x80", 3},
             std::string{"\xf4\x90\x80\x80", 4},
             std::string{"\xe2\x82", 2},
         }) {
        manifest = make_manifest(fixture);
        manifest.execution->host_os = invalid_utf8;
        expect_error(manifest, "reference-manifest-wire-invalid-utf8");
    }
}

void test_canonical_scalars_order_and_escaping() {
    ReferenceManifestFixture fixture;
    auto positive_zero = make_manifest(fixture);
    auto negative_zero = positive_zero;
    reference_inputs(positive_zero.content)
        .presentation.conditioning.jitter_scale.value = 0.0;
    reference_inputs(negative_zero.content)
        .presentation.conditioning.jitter_scale.value =
        std::bit_cast<double>(UINT64_C(0x8000000000000000));
    expect(require_encoding(positive_zero) == require_encoding(negative_zero),
           "binary64 zero signs did not canonicalize equally");

    auto ordered = make_manifest(fixture);
    auto reordered = ordered;
    std::swap(reordered.content.routes[0], reordered.content.routes[1]);
    expect(require_encoding(ordered) != require_encoding(reordered),
           "manifest encoder sorted a semantic vector");

    auto escaped = make_manifest(fixture);
    escaped.execution->host_os = "lin\nux\t\"\\\x01";
    const auto escaped_document = as_string(require_encoding(escaped));
    expect(escaped_document.find("\"host_os\":\"lin\\nux\\t\\\"\\\\\\u0001\"") !=
               std::string::npos,
           "canonical JSON escaping changed");

    auto unicode = make_manifest(fixture);
    unicode.execution->host_os = std::string{"linux-\xe2\x82\xac", 9};
    const auto unicode_document = as_string(require_encoding(unicode));
    expect(unicode_document.find(std::string{"\"host_os\":\"linux-\xe2\x82\xac\"",
                                             21}) != std::string::npos,
           "valid non-ASCII UTF-8 was not preserved directly");

    auto maximum = make_manifest(fixture);
    reference_inputs(maximum.content).capture.record_count =
        std::numeric_limits<std::uint64_t>::max();
    expect(as_string(require_encoding(maximum))
                   .find("\"record_count\":\"0xffffffffffffffff\"") !=
               std::string::npos,
           "maximum u64 encoding changed");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--emit-golden") {
            ReferenceManifestFixture fixture;
            std::cout << as_string(require_encoding(make_manifest(fixture)));
            return EXIT_SUCCESS;
        }
        expect(argc == 1, "unsupported reference manifest encoder test argument");
        test_complete_golden_document();
        test_fail_closed_boundaries();
        test_canonical_scalars_order_and_escaping();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
