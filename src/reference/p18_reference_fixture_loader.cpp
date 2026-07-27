#include "reference/p18_reference_fixture_loader.hpp"

#include "dsp/p18_static_ir_conversion.hpp"
#include "presentation/p18_pcm16_ir_decoder.hpp"

#include <array>
#include <bit>
#include <cerrno>
#include <complex>
#include <cstring>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace engine_sim_offline::reference {
namespace {

constexpr std::string_view kAuditSha256 =
    "93fbaef5fe887ba229d7acc28235d63c98f9205d2fe7e426a3e501473a2643a4";
constexpr std::string_view kSeedSha256 =
    "ca6f9b2d56e2f6729401437a741f605069a7eea21524a85b3dce0322ec30468f";
constexpr std::string_view kConfiguredIrSha256 =
    "75de9db47063395665d36b6d4232f477aae385feaa9ba158353fbdaf122db5cc";

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2 + 1] = kDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

void require_digest(const contract::Sha256Digest &actual, std::string_view expected,
                    std::string_view label) {
    const std::string actual_hex = digest_hex(actual);
    if (actual_hex != expected) {
        throw std::runtime_error{"P1.8 fixture preflight: " + std::string(label) +
                                 " SHA-256 mismatch (expected " +
                                 std::string(expected) + ", got " + actual_hex + ")"};
    }
}

#if defined(__linux__)

class FileDescriptor {
  public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept
        : value_(std::exchange(other.value_, -1)) {}
    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                static_cast<void>(::close(value_));
            }
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }
    [[nodiscard]] int get() const noexcept {
        return value_;
    }
    [[nodiscard]] bool valid() const noexcept {
        return value_ >= 0;
    }

  private:
    int value_ = -1;
};

[[nodiscard]] std::runtime_error io_error(std::string_view operation,
                                          int error_number) {
    return std::runtime_error{"P1.8 fixture preflight: " + std::string(operation) +
                              ": " + std::strerror(error_number)};
}

[[nodiscard]] FileDescriptor open_fixture_root(const std::filesystem::path &root) {
    const auto &native = root.native();
    if (native.empty() || native.find('\0') != std::string::npos) {
        throw std::invalid_argument{
            "P1.8 fixture preflight: fixture root path is empty or contains NUL"};
    }
    FileDescriptor descriptor(
        ::open(native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid()) {
        throw io_error("fixture root is unavailable, not a directory, or a symlink",
                       errno);
    }
    return descriptor;
}

[[nodiscard]] FileDescriptor open_directory_at(int parent, const char *name) {
    FileDescriptor descriptor(
        ::openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid()) {
        throw io_error(std::string("could not open fixed directory ") + name, errno);
    }
    return descriptor;
}

[[nodiscard]] std::vector<std::byte> read_exact_file_at(int parent, const char *name,
                                                        std::size_t expected_size,
                                                        std::string_view label) {
    FileDescriptor descriptor(
        ::openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (!descriptor.valid()) {
        throw io_error("could not open fixed input " + std::string(label), errno);
    }

    struct stat status{};
    if (::fstat(descriptor.get(), &status) != 0) {
        throw io_error("could not inspect fixed input " + std::string(label), errno);
    }
    if (!S_ISREG(status.st_mode)) {
        throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                 std::string(label) + " is not a regular file"};
    }
    if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) !=
                                  static_cast<std::uintmax_t>(expected_size)) {
        throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                 std::string(label) + " must contain exactly " +
                                 std::to_string(expected_size) + " bytes"};
    }

    std::vector<std::byte> bytes(expected_size);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count =
            ::read(descriptor.get(), bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == 0) {
            throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                     std::string(label) + " was truncated while read"};
        } else if (errno != EINTR) {
            throw io_error("could not read fixed input " + std::string(label), errno);
        }
    }
    std::byte extra{};
    for (;;) {
        const auto count = ::read(descriptor.get(), &extra, 1);
        if (count == 0) {
            break;
        }
        if (count > 0) {
            throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                     std::string(label) + " grew while read"};
        }
        if (errno != EINTR) {
            throw io_error("could not finish fixed input " + std::string(label), errno);
        }
    }
    return bytes;
}

#else

[[nodiscard]] std::vector<std::byte> read_exact_file(const std::filesystem::path &path,
                                                     std::size_t expected_size,
                                                     std::string_view label) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                 std::string(label) +
                                 " is unavailable or not a regular file"};
    }
    if (std::filesystem::file_size(path, error) != expected_size || error) {
        throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                 std::string(label) + " must contain exactly " +
                                 std::to_string(expected_size) + " bytes"};
    }
    std::ifstream stream(path, std::ios::binary);
    std::vector<std::byte> bytes(expected_size);
    stream.read(reinterpret_cast<char *>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        stream.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error{"P1.8 fixture preflight: fixed input " +
                                 std::string(label) + " changed while read"};
    }
    return bytes;
}

#endif

[[nodiscard]] std::vector<std::byte> serialize_f64le(std::span<const double> values) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    std::vector<std::byte> bytes(values.size() * sizeof(double));
    for (std::size_t index = 0; index < values.size(); ++index) {
        const std::uint64_t bits = std::bit_cast<std::uint64_t>(values[index]);
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
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(pair[part]);
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

} // namespace

P18LoadedReferenceFixture
load_p18_reference_fixture(const std::filesystem::path &fixture_root) {
    const auto started = std::chrono::steady_clock::now();

#if defined(__linux__)
    const FileDescriptor root = open_fixture_root(fixture_root);
    const auto audit_bytes =
        read_exact_file_at(root.get(), "reference-audit.bin",
                           kP18ReferenceAuditByteCount, "reference-audit.bin");
    const auto seed_bytes =
        read_exact_file_at(root.get(), "component-seeds.bin",
                           kP18ReferenceSeedByteCount, "component-seeds.bin");
    const FileDescriptor presentation = open_directory_at(root.get(), "presentation");
    const auto ir_bytes = read_exact_file_at(presentation.get(), "smooth_39.wav",
                                             kP18ReferenceConfiguredIrWaveByteCount,
                                             "presentation/smooth_39.wav");
#else
    const auto audit_bytes =
        read_exact_file(fixture_root / "reference-audit.bin",
                        kP18ReferenceAuditByteCount, "reference-audit.bin");
    const auto seed_bytes =
        read_exact_file(fixture_root / "component-seeds.bin",
                        kP18ReferenceSeedByteCount, "component-seeds.bin");
    const auto ir_bytes = read_exact_file(
        fixture_root / "presentation" / "smooth_39.wav",
        kP18ReferenceConfiguredIrWaveByteCount, "presentation/smooth_39.wav");
#endif

    P18ReferenceFixtureDigests digests{
        contract::sha256(audit_bytes),
        contract::sha256(seed_bytes),
        contract::sha256(ir_bytes),
        {},
        {},
    };
    require_digest(digests.reference_audit, kAuditSha256, "reference-audit.bin");
    require_digest(digests.component_seeds, kSeedSha256, "component-seeds.bin");
    require_digest(digests.configured_ir_wave, kConfiguredIrSha256,
                   "presentation/smooth_39.wav");

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
    auto ir_result = presentation::decode_p18_pcm16_ir_wave(ir_bytes);
    const auto *ir = std::get_if<presentation::P18DecodedPcm16Ir>(&ir_result);
    if (ir == nullptr) {
        throw decode_error("configured IR",
                           std::get<presentation::P18Pcm16IrDecodeError>(ir_result));
    }
    if (ir->samples.size() != kP18ReferenceConfiguredIrSampleCount ||
        ir->meaningful_support_frames != kP18ReferenceConfiguredIrSupportFrames) {
        throw std::runtime_error{
            "P1.8 fixture preflight: configured IR must decode to 33705 samples "
            "with 6907 meaningful-support frames"};
    }

    auto coefficients = dsp::p18_convert_static_ir(
        ir->samples, ir->meaningful_support_frames,
        std::bit_cast<double>(kP18ReferenceConfiguredIrGainBits));
    if (coefficients.size() != dsp::P18FixedConvolutionKernel::coefficient_count) {
        throw std::runtime_error{
            "P1.8 fixture preflight: regenerated configured IR must contain "
            "exactly 30071 coefficients"};
    }
    const auto coefficient_bytes = serialize_f64le(coefficients);
    digests.configured_ir_kernel_f64le = contract::sha256(coefficient_bytes);

    auto kernel = std::make_shared<const dsp::P18FixedConvolutionKernel>(coefficients);
    const auto spectrum_bytes = serialize_complex_f64le(kernel->spectrum());
    digests.configured_ir_kernel_spectrum_f64le = contract::sha256(spectrum_bytes);
    const auto finished = std::chrono::steady_clock::now();
    return {
        std::move(*audit),
        std::move(*seeds),
        std::move(coefficients),
        std::move(kernel),
        digests,
        std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started),
    };
}

} // namespace engine_sim_offline::reference
