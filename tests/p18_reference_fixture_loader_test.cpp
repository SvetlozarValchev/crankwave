#include "reference/p18_reference_fixture_loader.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

using namespace crankwave::reference;

static_assert(!std::is_default_constructible_v<P18VerifiedReferenceLineage>);
static_assert(!std::is_constructible_v<P18VerifiedReferenceLineage,
                                       std::array<P18ObservedLineageFileIdentity,
                                                  kP18ReferenceLineageFileCount>>);
static_assert(!std::is_copy_assignable_v<P18VerifiedReferenceLineage>);
static_assert(!std::is_move_assignable_v<P18VerifiedReferenceLineage>);

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

#if defined(__linux__)

void test_complete_verified_lineage(const std::filesystem::path &fixture_root) {
    const auto fixture = load_p18_reference_fixture(fixture_root);
    const auto &catalog = p18_reference_catalog_v1();

    for (const auto &expected : catalog.expected_lineage_files) {
        const auto &observed = fixture.verified_lineage.at(expected.file);
        expect(observed.file == expected.file &&
                   observed.byte_count == expected.expected_byte_count &&
                   observed.payload_sha256 == expected.expected_sha256,
               "verified lineage differs from independently observed fixture bytes");
    }

    expect(fixture.audit.frames.size() ==
               catalog.expected_capture.expected_record_count,
           "decoded audit frame count differs from the catalog");
    for (const auto &expected : catalog.expected_executed_seeds) {
        const auto route = static_cast<std::size_t>(expected.route);
        const auto &observed =
            expected.component == P18ReferenceExecutedRandomComponent::air_noise
                ? fixture.component_seeds.air_noise[route]
                : fixture.component_seeds.jitter[route];
        expect(observed.initial_state == expected.expected_initial_state &&
                   observed.stream == expected.expected_stream,
               "decoded executed seed differs from the catalog comparator");
    }

    const auto &observed_comparator =
        fixture.verified_lineage.at(P18ReferenceLineageFile::kernel_oracle_comparator);
    expect(fixture.configured_ir_coefficients.size() ==
                   catalog.expected_kernel.expected_coefficient_count &&
               fixture.derived_identities.configured_ir_kernel_f64le.byte_count ==
                   observed_comparator.byte_count &&
               fixture.derived_identities.configured_ir_kernel_f64le.payload_sha256 ==
                   observed_comparator.payload_sha256,
           "regenerated kernel differs from the independently observed comparator");
    expect(fixture.configured_ir_kernel != nullptr &&
               fixture.derived_identities.configured_ir_kernel_spectrum_f64le
                       .payload_sha256 ==
                   catalog.expected_kernel.expected_spectrum_f64le_sha256,
           "regenerated convolution spectrum differs from the catalog comparator");
    expect(fixture.preflight_duration > std::chrono::nanoseconds::zero(),
           "preflight duration was not observed");
}

class IsolatedFixture {
  public:
    explicit IsolatedFixture(const std::filesystem::path &source_root) {
        constexpr std::string_view pattern =
            "/tmp/crankwave-p18-fixture-XXXXXX";
        std::array<char, 64> writable{};
        expect(pattern.size() + 1 <= writable.size(),
               "temporary fixture template overflow");
        std::copy(pattern.begin(), pattern.end(), writable.begin());
        const auto *created = ::mkdtemp(writable.data());
        expect(created != nullptr, "could not create isolated fixture directory");
        temporary_root_ = created;
        fixture_root_ = temporary_root_ / "fixture";

        try {
            for (const auto &expected :
                 p18_reference_catalog_v1().expected_lineage_files) {
                const auto source = source_root / expected.expected_relative_path;
                const auto destination =
                    fixture_root_ / expected.expected_relative_path;
                std::filesystem::create_directories(destination.parent_path());
                std::filesystem::copy_file(source, destination);
            }
        } catch (...) {
            std::error_code ignored;
            std::filesystem::remove_all(temporary_root_, ignored);
            throw;
        }
    }

    ~IsolatedFixture() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary_root_, ignored);
    }

    IsolatedFixture(const IsolatedFixture &) = delete;
    IsolatedFixture &operator=(const IsolatedFixture &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const noexcept {
        return fixture_root_;
    }

  private:
    std::filesystem::path temporary_root_;
    std::filesystem::path fixture_root_;
};

[[nodiscard]] const P18ExpectedLineageFile &
expected_lineage(P18ReferenceLineageFile file) {
    for (const auto &expected : p18_reference_catalog_v1().expected_lineage_files) {
        if (expected.file == file) {
            return expected;
        }
    }
    throw std::logic_error{
        "requested P1.8 lineage identity is absent from the catalog"};
}

void expect_preflight_rejected(const std::filesystem::path &fixture_root,
                               std::string_view expected_path,
                               std::string_view failure_message) {
    bool rejected = false;
    try {
        static_cast<void>(load_p18_reference_fixture(fixture_root));
    } catch (const std::runtime_error &error) {
        rejected = std::string_view{error.what()}.find(expected_path) !=
                   std::string_view::npos;
    }
    expect(rejected, failure_message);
}

void test_final_symlink_is_rejected(const std::filesystem::path &fixture_root) {
    IsolatedFixture isolated{fixture_root};
    const auto &manifest = expected_lineage(P18ReferenceLineageFile::manifest);
    const auto candidate = isolated.root() / manifest.expected_relative_path;
    expect(std::filesystem::remove(candidate),
           "could not remove isolated manifest before symlink test");
    std::filesystem::create_symlink(
        std::filesystem::absolute(fixture_root / manifest.expected_relative_path),
        candidate);

    expect_preflight_rejected(isolated.root(), manifest.expected_relative_path,
                              "fixture preflight accepted a final symbolic link");
}

void test_nonretained_lineage_tamper_is_rejected(
    const std::filesystem::path &fixture_root) {
    IsolatedFixture isolated{fixture_root};
    const auto &algorithm_record =
        expected_lineage(P18ReferenceLineageFile::renderer_algorithm_record);
    const auto candidate = isolated.root() / algorithm_record.expected_relative_path;
    std::fstream stream(candidate, std::ios::binary | std::ios::in | std::ios::out);
    expect(stream.is_open(), "could not open isolated algorithm record for tampering");
    char first_byte = 0;
    stream.read(&first_byte, 1);
    expect(stream.gcount() == 1, "could not read isolated algorithm record byte");
    first_byte = static_cast<char>(static_cast<unsigned char>(first_byte) ^ 0x01U);
    stream.seekp(0);
    stream.write(&first_byte, 1);
    stream.flush();
    expect(stream.good(), "could not tamper with isolated algorithm record");
    stream.close();
    expect(std::filesystem::file_size(candidate) ==
               algorithm_record.expected_byte_count,
           "algorithm-record tamper changed its byte count instead of only its digest");

    expect_preflight_rejected(
        isolated.root(), algorithm_record.expected_relative_path,
        "fixture preflight accepted tampered non-retained lineage bytes");
}

#else

void test_non_linux_is_explicitly_unsupported(
    const std::filesystem::path &fixture_root) {
    bool rejected = false;
    try {
        static_cast<void>(load_p18_reference_fixture(fixture_root));
    } catch (const std::runtime_error &error) {
        rejected = std::string_view{error.what()}.find("supported only on Linux") !=
                   std::string_view::npos;
    }
    expect(rejected, "non-Linux fixture preflight did not fail explicitly");
}

#endif

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "fixture-loader test requires one fixture root");
#if defined(__linux__)
        test_complete_verified_lineage(argv[1]);
        test_final_symlink_is_rejected(argv[1]);
        test_nonretained_lineage_tamper_is_rejected(argv[1]);
#else
        test_non_linux_is_explicitly_unsupported(argv[1]);
#endif
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
