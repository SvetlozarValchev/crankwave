#include "reference/p18_reference_fixture_loader.hpp"

#include "artifacts/directory_render_sink_support.hpp"
#include "artifacts/secure_filesystem_support.hpp"
#include "contract/sha256_stream.hpp"
#include "dsp/static_ir_conversion.hpp"
#include "presentation/pcm16_ir_decoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <complex>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::reference {
namespace {

[[nodiscard]] constexpr std::size_t
lineage_index(P18ReferenceLineageFile file) noexcept {
    return static_cast<std::size_t>(file);
}

#if defined(__linux__)

constexpr std::size_t kReadBufferBytes = 64U * 1024U;

[[nodiscard]] bool retain_file_bytes(P18ReferenceLineageFile file) noexcept {
    switch (file) {
    case P18ReferenceLineageFile::audit_input:
    case P18ReferenceLineageFile::component_seed_input:
    case P18ReferenceLineageFile::configured_ir_input:
        return true;
    case P18ReferenceLineageFile::manifest:
    case P18ReferenceLineageFile::parity_evidence:
    case P18ReferenceLineageFile::renderer_algorithm_record:
    case P18ReferenceLineageFile::kernel_oracle_comparator:
        return false;
    }
    return false;
}

struct ObservedRead {
    P18ObservedLineageFileIdentity identity;
    std::vector<std::byte> retained_bytes;
};

[[nodiscard]] std::runtime_error preflight_error(std::string_view operation,
                                                 std::string_view detail) {
    return std::runtime_error{"P1.8 fixture preflight: " + std::string(operation) +
                              ": " + std::string(detail)};
}

void require_catalog_match(const P18ExpectedLineageFile &expected,
                           const P18ObservedLineageFileIdentity &observed) {
    if (observed.byte_count != expected.expected_byte_count) {
        throw preflight_error(expected.expected_relative_path,
                              "observed byte count differs from the immutable catalog");
    }
    if (observed.payload_sha256 != expected.expected_sha256) {
        throw preflight_error(
            expected.expected_relative_path,
            "SHA-256 mismatch (expected " +
                artifacts::detail::digest_hex(expected.expected_sha256) + ", got " +
                artifacts::detail::digest_hex(observed.payload_sha256) + ")");
    }
}

[[nodiscard]] artifacts::detail::FileDescriptor
open_fixture_root(const std::filesystem::path &root) {
    const auto &native = root.native();
    if (native.empty() || native.find('\0') != std::string::npos) {
        throw std::invalid_argument{
            "P1.8 fixture preflight: fixture root path is empty or contains NUL"};
    }
    artifacts::detail::FileDescriptor descriptor(
        ::open(native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid()) {
        throw preflight_error(
            "fixture root",
            artifacts::detail::errno_message(
                "root is unavailable, not a directory, or a symbolic link", errno));
    }
    return descriptor;
}

[[nodiscard]] bool same_file_state(const struct stat &lhs,
                                   const struct stat &rhs) noexcept {
    return lhs.st_dev == rhs.st_dev && lhs.st_ino == rhs.st_ino &&
           lhs.st_size == rhs.st_size && lhs.st_mtim.tv_sec == rhs.st_mtim.tv_sec &&
           lhs.st_mtim.tv_nsec == rhs.st_mtim.tv_nsec &&
           lhs.st_ctim.tv_sec == rhs.st_ctim.tv_sec &&
           lhs.st_ctim.tv_nsec == rhs.st_ctim.tv_nsec;
}

[[nodiscard]] ObservedRead read_catalog_file(int root_fd,
                                             const P18ExpectedLineageFile &expected,
                                             bool retain_bytes) {
    if (!artifacts::detail::valid_relative_path(expected.expected_relative_path)) {
        throw preflight_error(expected.expected_relative_path,
                              "catalog path is not a safe portable relative path");
    }
    auto opened =
        artifacts::detail::open_file_beneath(root_fd, expected.expected_relative_path);
    if (const auto *error = std::get_if<RenderSinkError>(&opened)) {
        throw preflight_error(expected.expected_relative_path, error->message);
    }
    auto file = std::move(std::get<artifacts::detail::FileDescriptor>(opened));

    struct stat before{};
    if (::fstat(file.get(), &before) == -1) {
        throw preflight_error(
            expected.expected_relative_path,
            artifacts::detail::errno_message("could not inspect fixed file", errno));
    }
    if (!S_ISREG(before.st_mode) || before.st_size < 0) {
        throw preflight_error(expected.expected_relative_path,
                              "fixed descendant is not a regular file");
    }
    const auto observed_size = static_cast<std::uint64_t>(before.st_size);
    if (observed_size != expected.expected_byte_count ||
        observed_size > std::numeric_limits<std::size_t>::max() ||
        observed_size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        throw preflight_error(expected.expected_relative_path,
                              "file size differs from the immutable catalog");
    }

    ObservedRead result;
    result.identity.file = expected.file;
    result.identity.byte_count = observed_size;
    if (retain_bytes) {
        result.retained_bytes.resize(static_cast<std::size_t>(observed_size));
    }
    std::array<std::byte, kReadBufferBytes> buffer{};
    contract::detail::Sha256Stream hash;
    std::uint64_t offset = 0;
    while (offset < observed_size) {
        const auto request = static_cast<std::size_t>(
            std::min<std::uint64_t>(kReadBufferBytes, observed_size - offset));
        auto *destination =
            retain_bytes ? result.retained_bytes.data() + offset : buffer.data();
        const auto count =
            ::pread(file.get(), destination, request, static_cast<off_t>(offset));
        if (count > 0) {
            const auto consumed = static_cast<std::size_t>(count);
            hash.update(std::span<const std::byte>{destination, consumed});
            offset += static_cast<std::uint64_t>(consumed);
        } else if (count == 0) {
            throw preflight_error(expected.expected_relative_path,
                                  "file was truncated while reading");
        } else if (errno != EINTR) {
            throw preflight_error(
                expected.expected_relative_path,
                artifacts::detail::errno_message("could not read fixed file", errno));
        }
    }
    std::byte extra{};
    ssize_t extra_count = -1;
    do {
        extra_count = ::pread(file.get(), &extra, 1, static_cast<off_t>(offset));
    } while (extra_count == -1 && errno == EINTR);
    if (extra_count != 0) {
        throw preflight_error(expected.expected_relative_path,
                              extra_count > 0 ? "file grew while reading"
                                              : "could not finish fixed-file read");
    }
    struct stat after{};
    if (::fstat(file.get(), &after) == -1 || !same_file_state(before, after)) {
        throw preflight_error(expected.expected_relative_path,
                              "file identity or state changed while reading");
    }
    result.identity.payload_sha256 = hash.finish();
    require_catalog_match(expected, result.identity);
    return result;
}

[[nodiscard]] std::vector<std::byte> serialize_f64le(std::span<const double> values) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    std::vector<std::byte> bytes(values.size() * sizeof(double));
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto bits = std::bit_cast<std::uint64_t>(values[index]);
        for (std::uint32_t byte_index = 0; byte_index < 8; ++byte_index) {
            bytes[index * 8 + byte_index] =
                static_cast<std::byte>(bits >> (byte_index * 8U));
        }
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte>
serialize_complex_f64le(std::span<const std::complex<double>> values) {
    std::vector<std::byte> bytes(values.size() * 2 * sizeof(double));
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::array pair{values[index].real(), values[index].imag()};
        for (std::size_t part = 0; part < pair.size(); ++part) {
            const auto bits = std::bit_cast<std::uint64_t>(pair[part]);
            for (std::uint32_t byte_index = 0; byte_index < 8; ++byte_index) {
                bytes[(index * 2 + part) * 8 + byte_index] =
                    static_cast<std::byte>(bits >> (byte_index * 8U));
            }
        }
    }
    return bytes;
}

template <class Error>
[[nodiscard]] std::runtime_error decode_error(std::string_view label,
                                              const Error &error) {
    return std::runtime_error{"P1.8 fixture preflight: strict " + std::string(label) +
                              " decode failed (code " +
                              std::to_string(static_cast<unsigned>(error.code)) +
                              ", byte " + std::to_string(error.byte_offset) + ")"};
}

#endif

} // namespace

const P18ObservedLineageFileIdentity &
P18VerifiedReferenceLineage::at(P18ReferenceLineageFile file) const {
    const auto index = lineage_index(file);
    if (index >= files_.size() || files_[index].file != file) {
        throw std::out_of_range{"P1.8 verified-lineage file identity is unavailable"};
    }
    return files_[index];
}

P18LoadedReferenceFixture
load_p18_reference_fixture(const std::filesystem::path &fixture_root) {
#if !defined(__linux__)
    static_cast<void>(fixture_root);
    throw std::runtime_error{
        "P1.8 fixture preflight is supported only on Linux because secure "
        "descriptor-relative file opening is unavailable on this platform"};
#else
    const auto started = std::chrono::steady_clock::now();
    const auto &catalog = p18_reference_catalog_v1();
    std::array<ObservedRead, kP18ReferenceLineageFileCount> reads{};

    const auto root = open_fixture_root(fixture_root);
    for (std::size_t index = 0; index < catalog.expected_lineage_files.size();
         ++index) {
        const auto &expected = catalog.expected_lineage_files[index];
        if (lineage_index(expected.file) != index) {
            throw std::logic_error{
                "P1.8 lineage catalog must preserve exhaustive enum order"};
        }
        reads[index] =
            read_catalog_file(root.get(), expected, retain_file_bytes(expected.file));
    }

    std::array<P18ObservedLineageFileIdentity, kP18ReferenceLineageFileCount>
        observed_files{};
    for (std::size_t index = 0; index < reads.size(); ++index) {
        observed_files[index] = reads[index].identity;
    }
    P18VerifiedReferenceLineage lineage{std::move(observed_files)};
    auto audit_bytes = std::move(
        reads[lineage_index(P18ReferenceLineageFile::audit_input)].retained_bytes);
    auto seed_bytes =
        std::move(reads[lineage_index(P18ReferenceLineageFile::component_seed_input)]
                      .retained_bytes);
    auto ir_bytes =
        std::move(reads[lineage_index(P18ReferenceLineageFile::configured_ir_input)]
                      .retained_bytes);

    auto audit_result = decode_p18_reference_audit(audit_bytes);
    const auto *audit = std::get_if<P18DecodedReferenceAudit>(&audit_result);
    if (audit == nullptr) {
        throw decode_error("reference audit",
                           std::get<P18ReferenceAuditDecodeError>(audit_result));
    }
    auto seed_result = decode_p18_reference_seeds(seed_bytes);
    const auto *seeds = std::get_if<P18DecodedReferenceSeeds>(&seed_result);
    if (seeds == nullptr) {
        throw decode_error("component seed",
                           std::get<P18ReferenceSeedDecodeError>(seed_result));
    }
    auto ir_result = presentation::decode_pcm16_ir_wave(ir_bytes);
    const auto *ir = std::get_if<presentation::DecodedPcm16Ir>(&ir_result);
    if (ir == nullptr) {
        throw decode_error("configured IR",
                           std::get<presentation::Pcm16IrDecodeError>(ir_result));
    }
    const auto &expected_ir =
        catalog.expected_presentation.expected_configured_ir_media;
    if (ir->samples.size() != expected_ir.expected_frame_count ||
        ir->meaningful_support_frames !=
            expected_ir.expected_meaningful_support_frame_count) {
        throw preflight_error("configured IR",
                              "decoded media shape differs from the catalog");
    }

    const auto gain_bits =
        catalog.expected_presentation.expected_scalars
            .expected_impulse_response_gain_linear.expected_ieee754_bits;
    auto coefficients = dsp::convert_static_ir(
        ir->samples, ir->meaningful_support_frames, std::bit_cast<double>(gain_bits));
    if (coefficients.size() != catalog.expected_kernel.expected_coefficient_count ||
        coefficients.size() != dsp::FixedConvolutionKernel::coefficient_count) {
        throw preflight_error("configured IR",
                              "regenerated kernel shape differs from the catalog");
    }

    const auto coefficient_bytes = serialize_f64le(coefficients);
    P18ReferenceDerivedIdentities derived;
    derived.configured_ir_kernel_f64le = {
        static_cast<std::uint64_t>(coefficient_bytes.size()),
        contract::sha256(coefficient_bytes),
    };
    const auto &observed_comparator =
        lineage.at(P18ReferenceLineageFile::kernel_oracle_comparator);
    if (derived.configured_ir_kernel_f64le.byte_count !=
            observed_comparator.byte_count ||
        derived.configured_ir_kernel_f64le.payload_sha256 !=
            observed_comparator.payload_sha256) {
        throw preflight_error(
            "configured IR kernel",
            "regenerated coefficients differ from the observed comparator file");
    }

    auto kernel = std::make_shared<const dsp::FixedConvolutionKernel>(coefficients);
    const auto spectrum_bytes = serialize_complex_f64le(kernel->spectrum());
    derived.configured_ir_kernel_spectrum_f64le = {
        static_cast<std::uint64_t>(spectrum_bytes.size()),
        contract::sha256(spectrum_bytes),
    };
    const auto finished = std::chrono::steady_clock::now();
    return {
        std::move(*audit),
        std::move(*seeds),
        std::move(coefficients),
        std::move(kernel),
        std::move(lineage),
        derived,
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started),
    };
#endif
}

} // namespace engine_sim_offline::reference
