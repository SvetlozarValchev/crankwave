#include "engine_sim_offline/profiles/bmw_m52b28_parity_request.hpp"
#include "excitation/captured_exhaust_excitation.hpp"
#include "reference/p18_reference_catalog.hpp"
#include "reference/p18_reference_fixture_loader.hpp"
#include "reference/p18_reference_full_audit_reader.hpp"
#include "reference/p18_reference_render_session.hpp"
#include "reference/p18_reference_session_adapter.hpp"
#include "reference/reference_parity_v1_reader.hpp"
#include "simulation/legacy_low_order_simulation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

using namespace engine_sim_offline;

constexpr std::size_t kCylinderCount = 6U;
constexpr std::size_t kRouteCount = 2U;
constexpr std::size_t kFramesPerBlock = 200U;
constexpr std::size_t kRequiredFrameCount = 170000U;
constexpr std::size_t kRequiredBlockCount = 850U;
constexpr double kMaximumPipelineSeconds = 60.0;
constexpr double kGrossMinimumCorrelation = 0.90;
constexpr double kGrossMaximumNormalizedRmse = 0.25;
constexpr double kGrossMinimumRmsRatio = 0.10;
constexpr double kGrossMaximumRmsRatio = 10.0;
constexpr double kMaximumCircularCrankErrorRad = 1.0e-10;
constexpr double kLegacyPi = 3.14159265359;

static_assert(kCylinderCount == reference::kReferenceParityV1CylinderCount);
static_assert(kCylinderCount == reference::kP18ReferenceAuditCylinderCount);
static_assert(kRouteCount == reference::kReferenceParityV1RouteCount);
static_assert(kRouteCount == reference::kP18ReferenceAuditBusCount);
static_assert(kFramesPerBlock == excitation::kCapturedExcitationFramesPerBlock);

[[noreturn]] void fail(std::string message) {
    throw std::runtime_error{std::move(message)};
}

[[nodiscard]] std::string
validation_report_text(const contract::ValidationReport &report) {
    std::ostringstream text;
    for (const auto &issue : report.issues) {
        text << "\n  " << issue.path << ": " << issue.message;
    }
    return text.str();
}

[[nodiscard]] std::string failure_text(const contract::FailureContext &failure) {
    std::ostringstream text;
    text << failure.detail_code << " at sample " << failure.sample_index << ": "
         << failure.state_summary;
    return text.str();
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kHex[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kHex[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] const reference::P18ExpectedLineageFile &
required_lineage_file(reference::P18ReferenceLineageFile file) {
    const auto &files = reference::p18_reference_catalog_v1().expected_lineage_files;
    const auto found =
        std::ranges::find(files, file, &reference::P18ExpectedLineageFile::file);
    if (found == files.end()) {
        fail("P1.8 catalog is missing a required comparator file");
    }
    return *found;
}

#if defined(__linux__)

class FileDescriptor final {
  public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            ::close(value_);
        }
    }
    FileDescriptor(const FileDescriptor &) = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
    FileDescriptor(FileDescriptor &&other) noexcept
        : value_(std::exchange(other.value_, -1)) {}
    FileDescriptor &operator=(FileDescriptor &&other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                ::close(value_);
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

[[nodiscard]] bool same_file_state(const struct stat &left,
                                   const struct stat &right) noexcept {
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino &&
           left.st_size == right.st_size &&
           left.st_mtim.tv_sec == right.st_mtim.tv_sec &&
           left.st_mtim.tv_nsec == right.st_mtim.tv_nsec &&
           left.st_ctim.tv_sec == right.st_ctim.tv_sec &&
           left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}

#endif

// The production fixture loader already performs a secure seven-file preflight.
// Comparators are then reopened independently and content-pinned here because that
// loader intentionally does not retain parity or all fourteen audit lanes.
[[nodiscard]] std::vector<std::byte>
read_verified_comparator(const std::filesystem::path &fixture_root,
                         const reference::P18ExpectedLineageFile &expected) {
#if defined(__linux__)
    const auto &root_native = fixture_root.native();
    if (root_native.empty() || root_native.find('\0') != std::string::npos ||
        expected.expected_relative_path.empty() ||
        expected.expected_relative_path.find('/') != std::string_view::npos ||
        expected.expected_relative_path.find('\\') != std::string_view::npos) {
        fail("comparator root or catalog path is not a direct safe path");
    }

    FileDescriptor root{
        ::open(root_native.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (!root.valid()) {
        fail("could not open fixture root for comparator read");
    }
    const std::string relative{expected.expected_relative_path};
    FileDescriptor file{
        ::openat(root.get(), relative.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
    if (!file.valid()) {
        fail("could not securely open comparator " + relative);
    }

    struct stat before{};
    if (::fstat(file.get(), &before) == -1 || !S_ISREG(before.st_mode) ||
        before.st_size < 0 ||
        static_cast<std::uint64_t>(before.st_size) != expected.expected_byte_count ||
        expected.expected_byte_count > std::numeric_limits<std::size_t>::max() ||
        expected.expected_byte_count >
            static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        fail("comparator size or file type differs from the immutable catalog: " +
             relative);
    }

    std::vector<std::byte> bytes(
        static_cast<std::size_t>(expected.expected_byte_count));
    std::uint64_t offset = 0U;
    while (offset < expected.expected_byte_count) {
        const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(
            64U * 1024U, expected.expected_byte_count - offset));
        const auto count = ::pread(file.get(), bytes.data() + offset, request,
                                   static_cast<off_t>(offset));
        if (count > 0) {
            offset += static_cast<std::uint64_t>(count);
        } else if (count == 0) {
            fail("comparator was truncated during read: " + relative);
        } else if (errno != EINTR) {
            fail("could not read comparator: " + relative);
        }
    }
    std::byte extra{};
    ssize_t extra_count = -1;
    do {
        extra_count = ::pread(file.get(), &extra, 1, static_cast<off_t>(offset));
    } while (extra_count == -1 && errno == EINTR);
    struct stat after{};
    if (extra_count != 0 || ::fstat(file.get(), &after) == -1 ||
        !same_file_state(before, after)) {
        fail("comparator changed during read: " + relative);
    }
    if (contract::sha256(bytes) != expected.expected_sha256) {
        fail("comparator SHA-256 differs from the immutable catalog: " + relative);
    }
    return bytes;
#else
    static_cast<void>(fixture_root);
    static_cast<void>(expected);
    fail("the M3 BMW listening gate requires Linux secure fixture reads");
#endif
}

[[nodiscard]] reference::DecodedReferenceParityV1
load_parity(const std::filesystem::path &fixture_root) {
    auto bytes = read_verified_comparator(
        fixture_root,
        required_lineage_file(reference::P18ReferenceLineageFile::parity_evidence));
    auto result = reference::decode_reference_parity_v1(bytes);
    if (auto *decoded = std::get_if<reference::DecodedReferenceParityV1>(&result)) {
        return std::move(*decoded);
    }
    const auto &error = std::get<reference::ReferenceParityV1DecodeError>(result);
    fail("reference-parity.bin failed strict decode at byte " +
         std::to_string(error.byte_offset) + ", record " +
         std::to_string(error.record_index));
}

[[nodiscard]] reference::P18DecodedFullReferenceAudit
load_full_audit(const std::filesystem::path &fixture_root) {
    auto bytes = read_verified_comparator(
        fixture_root,
        required_lineage_file(reference::P18ReferenceLineageFile::audit_input));
    auto result = reference::decode_p18_full_reference_audit(bytes);
    if (auto *decoded = std::get_if<reference::P18DecodedFullReferenceAudit>(&result)) {
        return std::move(*decoded);
    }
    const auto &error = std::get<reference::P18ReferenceAuditDecodeError>(result);
    fail("reference-audit.bin failed strict full-lane decode at byte " +
         std::to_string(error.byte_offset) + ", record " +
         std::to_string(error.record_index));
}

struct SignalMoments {
    long double sum = 0.0L;
    long double square_sum = 0.0L;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    double peak = 0.0;
    std::uint64_t count = 0U;
    std::uint64_t nonzero_count = 0U;
    std::uint64_t positive_count = 0U;
    std::uint64_t negative_count = 0U;

    void add(double value) noexcept {
        sum += value;
        square_sum += static_cast<long double>(value) * value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        peak = std::max(peak, std::abs(value));
        ++count;
        nonzero_count += value != 0.0 ? 1U : 0U;
        positive_count += value > 0.0 ? 1U : 0U;
        negative_count += value < 0.0 ? 1U : 0U;
    }

    [[nodiscard]] double dc() const noexcept {
        return count == 0U ? 0.0
                           : static_cast<double>(sum / static_cast<long double>(count));
    }
    [[nodiscard]] double rms() const noexcept {
        return count == 0U ? 0.0
                           : std::sqrt(static_cast<double>(
                                 square_sum / static_cast<long double>(count)));
    }
};

struct SignalComparison {
    SignalMoments candidate;
    SignalMoments reference;
    long double product_sum = 0.0L;
    long double error_sum = 0.0L;
    long double absolute_error_sum = 0.0L;
    long double square_error_sum = 0.0L;
    double maximum_absolute_error = 0.0;
    std::uint64_t nonfinite_count = 0U;

    void add(double actual, double expected) noexcept {
        if (!std::isfinite(actual) || !std::isfinite(expected)) {
            ++nonfinite_count;
            return;
        }
        candidate.add(actual);
        reference.add(expected);
        const long double error = static_cast<long double>(actual) - expected;
        product_sum += static_cast<long double>(actual) * expected;
        error_sum += error;
        absolute_error_sum += std::abs(error);
        square_error_sum += error * error;
        maximum_absolute_error =
            std::max(maximum_absolute_error, static_cast<double>(std::abs(error)));
    }

    [[nodiscard]] double bias() const noexcept {
        return candidate.count == 0U
                   ? 0.0
                   : static_cast<double>(error_sum /
                                         static_cast<long double>(candidate.count));
    }
    [[nodiscard]] double mae() const noexcept {
        return candidate.count == 0U
                   ? 0.0
                   : static_cast<double>(absolute_error_sum /
                                         static_cast<long double>(candidate.count));
    }
    [[nodiscard]] double rmse() const noexcept {
        return candidate.count == 0U
                   ? 0.0
                   : std::sqrt(static_cast<double>(
                         square_error_sum / static_cast<long double>(candidate.count)));
    }
    [[nodiscard]] double normalized_rmse() const noexcept {
        const double denominator = reference.rms();
        return denominator == 0.0 ? std::numeric_limits<double>::infinity()
                                  : rmse() / denominator;
    }
    [[nodiscard]] double rms_ratio() const noexcept {
        const double denominator = reference.rms();
        return denominator == 0.0 ? std::numeric_limits<double>::infinity()
                                  : candidate.rms() / denominator;
    }
    [[nodiscard]] double correlation() const noexcept {
        if (candidate.count == 0U) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        const long double count = static_cast<long double>(candidate.count);
        const long double covariance =
            product_sum - candidate.sum * reference.sum / count;
        const long double candidate_variance =
            candidate.square_sum - candidate.sum * candidate.sum / count;
        const long double reference_variance =
            reference.square_sum - reference.sum * reference.sum / count;
        if (candidate_variance <= 0.0L || reference_variance <= 0.0L) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return static_cast<double>(covariance /
                                   std::sqrt(candidate_variance * reference_variance));
    }
};

struct CylinderComparisons {
    SignalComparison static_pressure;
    SignalComparison dynamic_forward;
    SignalComparison dynamic_reverse;
    SignalComparison pre_delay;
    SignalComparison post_delay;
};

struct OracleDelay {
    std::array<double, 180U> history{};
    std::size_t next = 0U;
    std::uint64_t accepted = 0U;

    [[nodiscard]] double process(double input) noexcept {
        double output = +0.0;
        if (accepted >= history.size()) {
            output = history[next];
        }
        history[next] = input;
        ++next;
        if (next == history.size()) {
            next = 0U;
        }
        ++accepted;
        return output;
    }
};

struct OracleVerification {
    std::uint64_t pre_delay_bit_mismatches = 0U;
    std::uint64_t post_delay_bit_mismatches = 0U;
    std::uint64_t route_bus_bit_mismatches = 0U;
    std::uint64_t audit_post_to_bus_bit_mismatches = 0U;
};

[[nodiscard]] bool same_binary64(double left, double right) noexcept {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}

[[nodiscard]] OracleVerification
verify_reference_oracle(const reference::DecodedReferenceParityV1 &parity,
                        const reference::P18DecodedFullReferenceAudit &audit) {
    if (parity.frames.size() != audit.frames.size()) {
        fail("parity and full-audit comparator intervals differ");
    }
    std::array<OracleDelay, kCylinderCount> delays{};
    OracleVerification result;
    for (std::size_t frame = 0U; frame < parity.frames.size(); ++frame) {
        const auto &parity_frame = parity.frames[frame];
        const auto &audit_frame = audit.frames[frame];
        const double activity =
            std::min(std::abs(parity_frame.filtered_engine_speed_rpm),
                     parity.metadata.filtered_speed_threshold_rpm) /
            parity.metadata.filtered_speed_threshold_rpm;
        std::array<double, kRouteCount> buses{+0.0, +0.0};
        std::array<double, kRouteCount> buses_from_audit_post{+0.0, +0.0};
        for (std::size_t cylinder = 0U; cylinder < kCylinderCount; ++cylinder) {
            const double activity_squared = activity * activity;
            const double activity_cubed = activity_squared * activity;
            const double speed_scale =
                activity_cubed * parity.metadata.excitation_scale;
            const auto &pressure = parity_frame.cylinders[cylinder];
            const double gauge_static = pressure.static_pressure_pa_abs -
                                        parity.metadata.reference_atmosphere_pa_abs;
            const double static_component =
                parity.metadata.gauge_static_gain * gauge_static;
            const double forward_component = parity.metadata.dynamic_forward_gain *
                                             pressure.dynamic_pressure_forward_pa;
            const double reverse_component = parity.metadata.dynamic_reverse_gain *
                                             pressure.dynamic_pressure_reverse_pa;
            const double pressure_term =
                (static_component + forward_component) + reverse_component;
            const double pre_delay = speed_scale * pressure_term;
            result.pre_delay_bit_mismatches +=
                !same_binary64(pre_delay, audit_frame.pre_delay_cylinders[cylinder])
                    ? 1U
                    : 0U;

            const double post_delay = delays[cylinder].process(pre_delay);
            result.post_delay_bit_mismatches +=
                !same_binary64(post_delay, audit_frame.post_delay_cylinders[cylinder])
                    ? 1U
                    : 0U;

            const auto &descriptor = parity.cylinders[cylinder];
            if (descriptor.route_index >= buses.size()) {
                fail("parity cylinder descriptor names an unavailable route");
            }
            const double route_term =
                descriptor.sound_attenuation_linear *
                ((descriptor.route_audio_volume_linear * post_delay) /
                 parity.metadata.cylinder_count_divisor) *
                (1.0 / (descriptor.exhaust_system_length_m *
                        descriptor.exhaust_system_length_m));
            buses[descriptor.route_index] += route_term;
            const double audit_post_route_term =
                descriptor.sound_attenuation_linear *
                ((descriptor.route_audio_volume_linear *
                  audit_frame.post_delay_cylinders[cylinder]) /
                 parity.metadata.cylinder_count_divisor) *
                (1.0 / (descriptor.exhaust_system_length_m *
                        descriptor.exhaust_system_length_m));
            buses_from_audit_post[descriptor.route_index] += audit_post_route_term;
        }
        for (std::size_t route = 0U; route < kRouteCount; ++route) {
            result.route_bus_bit_mismatches +=
                !same_binary64(buses[route], audit_frame.pre_dsp_buses[route]) ? 1U
                                                                               : 0U;
            result.audit_post_to_bus_bit_mismatches +=
                !same_binary64(buses_from_audit_post[route],
                               audit_frame.pre_dsp_buses[route])
                    ? 1U
                    : 0U;
        }
    }
    return result;
}

struct EvaluationMetrics {
    SignalComparison engine_speed_rpm;
    SignalComparison filtered_engine_speed_rpm;
    SignalComparison crank_angle_rad;
    std::array<CylinderComparisons, kCylinderCount> cylinders{};
    std::array<SignalComparison, kRouteCount> route_buses{};
    std::uint64_t timing_index_mismatches = 0U;
    std::uint64_t control_mismatches = 0U;
    std::uint64_t event_count = 0U;
    std::uint64_t spark_crossing_count = 0U;
    std::uint64_t ignition_accepted_count = 0U;
    std::uint64_t ignition_rejected_count = 0U;
    double maximum_circular_crank_error_rad = 0.0;
};

class ScopedSecondsAccumulator final {
  public:
    explicit ScopedSecondsAccumulator(double &destination) noexcept
        : destination_(destination), started_(std::chrono::steady_clock::now()) {}
    ~ScopedSecondsAccumulator() {
        destination_ +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started_)
                .count();
    }
    ScopedSecondsAccumulator(const ScopedSecondsAccumulator &) = delete;
    ScopedSecondsAccumulator &operator=(const ScopedSecondsAccumulator &) = delete;

  private:
    double &destination_;
    std::chrono::steady_clock::time_point started_;
};

[[nodiscard]] double circular_crank_error(double actual, double expected) noexcept {
    return std::abs(std::remainder(actual - expected, 4.0 * kLegacyPi));
}

void compare_capture_block(const contract::CaptureBlockView &block,
                           std::uint64_t first_frame,
                           const reference::DecodedReferenceParityV1 &reference_parity,
                           EvaluationMetrics &metrics) {
    const auto &candidate_parity = *block.reference_parity();
    for (std::size_t frame = 0U; frame < block.frame_count(); ++frame) {
        const std::size_t global = static_cast<std::size_t>(first_frame) + frame;
        const auto &expected = reference_parity.frames[global];
        const auto &engine = block.engine()[frame];
        metrics.timing_index_mismatches +=
            engine.step_end_index != expected.step_end ? 1U : 0U;
        metrics.engine_speed_rpm.add(engine.engine_speed_rpm,
                                     expected.engine_speed_rpm);
        metrics.filtered_engine_speed_rpm.add(
            candidate_parity.filtered_engine_speed_rpm()[frame],
            expected.filtered_engine_speed_rpm);
        metrics.crank_angle_rad.add(engine.theta_cycle_rad, expected.crank_angle_rad);
        metrics.maximum_circular_crank_error_rad = std::max(
            metrics.maximum_circular_crank_error_rad,
            circular_crank_error(engine.theta_cycle_rad, expected.crank_angle_rad));
        const bool controls_match =
            engine.requested_throttle_01 == expected.controls.requested_throttle_01 &&
            engine.resolved_engine_throttle_01 ==
                expected.controls.resolved_intake_throttle_01 &&
            engine.ignition_enabled == expected.controls.ignition_enabled &&
            engine.fuel_enabled == expected.controls.fuel_enabled &&
            engine.starter_enabled == expected.controls.starter_enabled &&
            engine.dyno_enabled == expected.controls.dyno_enabled;
        metrics.control_mismatches += controls_match ? 0U : 1U;

        for (std::size_t cylinder = 0U; cylinder < kCylinderCount; ++cylinder) {
            const auto &actual =
                candidate_parity.cylinders()[frame * kCylinderCount + cylinder];
            const auto &reference = expected.cylinders[cylinder];
            metrics.cylinders[cylinder].static_pressure.add(
                actual.exhaust_primary_static_pressure_pa_abs,
                reference.static_pressure_pa_abs);
            metrics.cylinders[cylinder].dynamic_forward.add(
                actual.dynamic_pressure_forward_pa,
                reference.dynamic_pressure_forward_pa);
            metrics.cylinders[cylinder].dynamic_reverse.add(
                actual.dynamic_pressure_reverse_pa,
                reference.dynamic_pressure_reverse_pa);
        }
    }

    metrics.event_count += block.event_journal().events().size();
    for (const auto &event : block.event_journal().events()) {
        metrics.spark_crossing_count +=
            std::holds_alternative<contract::SparkCrossing>(event.payload) ? 1U : 0U;
        metrics.ignition_accepted_count +=
            std::holds_alternative<contract::IgnitionAccepted>(event.payload) ? 1U : 0U;
        metrics.ignition_rejected_count +=
            std::holds_alternative<contract::IgnitionRejected>(event.payload) ? 1U : 0U;
    }
}

void compare_excitation_block(
    const excitation::ExhaustExcitationDiagnosticBlockView &diagnostics,
    std::uint64_t first_frame,
    const reference::P18DecodedFullReferenceAudit &reference_audit,
    EvaluationMetrics &metrics) {
    const auto pre = diagnostics.pre_delay_cylinder_values_engine_sim_source_unit();
    const auto post = diagnostics.post_delay_cylinder_values_engine_sim_source_unit();
    const auto buses = diagnostics.route_bus_frames();
    for (std::size_t frame = 0U; frame < buses.size(); ++frame) {
        const auto &reference =
            reference_audit.frames[static_cast<std::size_t>(first_frame) + frame];
        for (std::size_t cylinder = 0U; cylinder < kCylinderCount; ++cylinder) {
            const std::size_t index = frame * kCylinderCount + cylinder;
            metrics.cylinders[cylinder].pre_delay.add(
                pre[index], reference.pre_delay_cylinders[cylinder]);
            metrics.cylinders[cylinder].post_delay.add(
                post[index], reference.post_delay_cylinders[cylinder]);
        }
        for (std::size_t route = 0U; route < kRouteCount; ++route) {
            metrics.route_buses[route].add(
                buses[frame].route_values_engine_sim_source_unit[route],
                reference.pre_dsp_buses[route]);
        }
    }
}

enum class SinkState : std::uint8_t { idle, begun, sealed, aborted };

struct RetainedArtifact {
    PendingArtifact declaration;
    std::vector<std::byte> bytes;
    std::optional<contract::ArtifactRecord> sealed_record;
    std::uint64_t maximum_bytes = 0U;
};

class LocalEvaluationSink final : public RenderSink {
  public:
    [[nodiscard]] RenderSinkStatus
    begin_transaction(const contract::OutputContract &output_contract) override {
        if (state_ != SinkState::idle ||
            output_contract.required_artifacts.size() !=
                reference::kP18PresentationAudioArtifactCount) {
            return protocol_error("local-evaluation-begin-invalid",
                                  "unexpected transaction state or artifact count");
        }
        output_contract_ = output_contract;
        artifacts_.reserve(reference::kP18PresentationAudioArtifactCount);
        state_ = SinkState::begun;
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    declare_artifact(const PendingArtifact &artifact) override {
        if (state_ != SinkState::begun ||
            artifacts_.size() >= reference::kP18PresentationAudioArtifactCount) {
            return protocol_error("local-evaluation-declare-invalid",
                                  "artifact declaration is out of sequence");
        }
        const auto &required = output_contract_->required_artifacts[artifacts_.size()];
        if (artifact.role != required.role || artifact.kind != required.kind ||
            artifact.audio != required.audio ||
            artifact.diagnostic != required.diagnostic ||
            !safe_relative_path(artifact.relative_path)) {
            return protocol_error("local-evaluation-declaration-mismatch",
                                  "artifact differs from ordered local plan");
        }
        for (const auto &prior : artifacts_) {
            if (prior.declaration.role == artifact.role ||
                prior.declaration.relative_path == artifact.relative_path) {
                return protocol_error("local-evaluation-declaration-duplicate",
                                      "artifact role or path is duplicated");
            }
        }
        const auto &expected =
            reference::p18_reference_catalog_v1().expected_audio[artifacts_.size()];
        RetainedArtifact retained;
        retained.declaration = artifact;
        retained.maximum_bytes = expected.expected_byte_count;
        retained.bytes.reserve(static_cast<std::size_t>(retained.maximum_bytes));
        artifacts_.push_back(std::move(retained));
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    write_artifact_chunk(const ArtifactChunk &chunk) override {
        if (state_ != SinkState::begun) {
            return protocol_error("local-evaluation-write-invalid",
                                  "artifact write is outside an active transaction");
        }
        auto *artifact = find(chunk.role);
        if (artifact == nullptr || artifact->sealed_record.has_value() ||
            chunk.byte_offset != artifact->bytes.size() ||
            chunk.bytes.size() > artifact->maximum_bytes - artifact->bytes.size()) {
            return protocol_error("local-evaluation-write-mismatch",
                                  "artifact chunk is missing, noncontiguous, or over "
                                  "its fixed bound");
        }
        artifact->bytes.insert(artifact->bytes.end(), chunk.bytes.begin(),
                               chunk.bytes.end());
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus
    seal_artifact(const contract::ArtifactRecord &record) override {
        if (state_ != SinkState::begun) {
            return protocol_error("local-evaluation-seal-invalid",
                                  "artifact seal is outside an active transaction");
        }
        auto *artifact = find(record.role);
        if (artifact == nullptr || artifact->sealed_record.has_value() ||
            record.kind != artifact->declaration.kind ||
            record.relative_path != artifact->declaration.relative_path ||
            record.audio != artifact->declaration.audio ||
            record.diagnostic != artifact->declaration.diagnostic ||
            record.byte_count != artifact->bytes.size() ||
            record.byte_count != artifact->maximum_bytes ||
            record.payload_sha256 != contract::sha256(artifact->bytes)) {
            return protocol_error("local-evaluation-seal-mismatch",
                                  "sealed observation differs from retained bytes");
        }
        artifact->sealed_record = record;
        if (std::ranges::all_of(artifacts_, [](const auto &candidate) {
                return candidate.sealed_record.has_value();
            })) {
            state_ = SinkState::sealed;
        }
        return std::nullopt;
    }

    [[nodiscard]] RenderSinkStatus commit(const contract::RenderManifest &) override {
        clear();
        state_ = SinkState::aborted;
        return protocol_error("local-evaluation-commit-forbidden",
                              "a listening-gate sink cannot publish a production "
                              "manifest");
    }

    void abort() noexcept override {
        clear();
        state_ = SinkState::aborted;
    }

    [[nodiscard]] const std::vector<RetainedArtifact> &sealed_artifacts() const {
        if (state_ != SinkState::sealed ||
            artifacts_.size() != reference::kP18PresentationAudioArtifactCount) {
            fail("local-evaluation sink does not hold eight sealed WAVs");
        }
        return artifacts_;
    }

  private:
    [[nodiscard]] static RenderSinkStatus protocol_error(std::string code,
                                                         std::string message) {
        return RenderSinkError{RenderSinkErrorKind::protocol_violation, std::move(code),
                               std::move(message)};
    }

    [[nodiscard]] static bool safe_relative_path(std::string_view path) {
        if (path.empty() || path.front() == '/' || path.back() == '/' ||
            path.find('\\') != std::string_view::npos) {
            return false;
        }
        std::size_t start = 0U;
        while (start < path.size()) {
            const auto end = path.find('/', start);
            const auto part = path.substr(start, end - start);
            if (part.empty() || part == "." || part == "..") {
                return false;
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1U;
        }
        return true;
    }

    [[nodiscard]] RetainedArtifact *find(std::string_view role) noexcept {
        const auto found = std::ranges::find(
            artifacts_, role, [](const auto &artifact) -> std::string_view {
                return artifact.declaration.role;
            });
        return found == artifacts_.end() ? nullptr : &*found;
    }

    void clear() noexcept {
        artifacts_.clear();
        output_contract_.reset();
    }

    SinkState state_ = SinkState::idle;
    std::optional<contract::OutputContract> output_contract_;
    std::vector<RetainedArtifact> artifacts_;
};

[[nodiscard]] reference::P18PresentationSessionPlan candidate_plan() {
    auto plan = reference::make_p18_reference_presentation_session_plan();
    constexpr std::array<std::string_view,
                         reference::kP18PresentationAudioArtifactCount>
        kPaths{
            "audio/exhaust-route-1-candidate-dry.wav",
            "audio/exhaust-route-1-candidate-configured-ir.wav",
            "audio/exhaust-route-1-candidate-selected.wav",
            "audio/exhaust-route-2-candidate-dry.wav",
            "audio/exhaust-route-2-candidate-configured-ir.wav",
            "audio/exhaust-route-2-candidate-selected.wav",
            "audio/bmw-m52b28-m3-candidate-raw.wav",
            "audio/bmw-m52b28-m3-candidate-listening.wav",
        };
    for (std::size_t index = 0U; index < kPaths.size(); ++index) {
        plan.audio_artifacts[index].relative_path = kPaths[index];
    }
    return plan;
}

struct GateReport {
    std::vector<std::string> failures;

    void require(bool condition, std::string message) {
        if (!condition) {
            failures.push_back(std::move(message));
        }
    }
    [[nodiscard]] bool passed() const noexcept {
        return failures.empty();
    }
};

void require_not_grossly_mismatched(GateReport &gate, const SignalComparison &value,
                                    std::uint64_t required_count, std::string label) {
    const double correlation = value.correlation();
    const double nrmse = value.normalized_rmse();
    const double rms_ratio = value.rms_ratio();
    gate.require(value.nonfinite_count == 0U,
                 label + " contains non-finite candidate/reference values");
    gate.require(value.candidate.count == required_count,
                 label + " is missing comparator frames");
    gate.require(std::isfinite(correlation) && correlation >= kGrossMinimumCorrelation,
                 label + " correlation is below the gross-mismatch floor");
    gate.require(std::isfinite(nrmse) && nrmse <= kGrossMaximumNormalizedRmse,
                 label + " normalized RMSE exceeds the gross-mismatch ceiling");
    gate.require(std::isfinite(rms_ratio) && rms_ratio >= kGrossMinimumRmsRatio &&
                     rms_ratio <= kGrossMaximumRmsRatio,
                 label + " RMS ratio indicates collapse or explosion");
}

[[nodiscard]] GateReport evaluate_gates(const EvaluationMetrics &metrics,
                                        const OracleVerification &oracle,
                                        std::uint64_t frame_count,
                                        std::uint64_t block_count,
                                        double pipeline_seconds) {
    GateReport gate;
    gate.require(oracle.pre_delay_bit_mismatches == 0U &&
                     oracle.post_delay_bit_mismatches == 0U &&
                     oracle.route_bus_bit_mismatches == 0U &&
                     oracle.audit_post_to_bus_bit_mismatches == 0U,
                 "strict parity/audit oracle reconstruction is not bit-exact");
    gate.require(frame_count == kRequiredFrameCount,
                 "candidate simulation did not produce 170000 frames");
    gate.require(block_count == kRequiredBlockCount,
                 "candidate simulation did not produce 850 complete blocks");
    gate.require(metrics.timing_index_mismatches == 0U,
                 "capture step indices diverge from the comparator timeline");
    gate.require(metrics.control_mismatches == 0U,
                 "capture controls diverge from the comparator timeline");
    gate.require(metrics.engine_speed_rpm.nonfinite_count == 0U &&
                     metrics.engine_speed_rpm.candidate.count == kRequiredFrameCount &&
                     metrics.engine_speed_rpm.maximum_absolute_error == 0.0,
                 "prescribed engine RPM is missing, non-finite, or changed");
    gate.require(metrics.filtered_engine_speed_rpm.nonfinite_count == 0U &&
                     metrics.filtered_engine_speed_rpm.candidate.count ==
                         kRequiredFrameCount &&
                     metrics.filtered_engine_speed_rpm.maximum_absolute_error == 0.0,
                 "filtered RPM is missing, non-finite, or changed");
    gate.require(metrics.crank_angle_rad.nonfinite_count == 0U &&
                     metrics.crank_angle_rad.candidate.count == kRequiredFrameCount &&
                     metrics.maximum_circular_crank_error_rad <=
                         kMaximumCircularCrankErrorRad,
                 "crank timing exceeds the admitted circular-error bound");
    gate.require(pipeline_seconds <= kMaximumPipelineSeconds,
                 "pre-presentation physics/excitation diagnostic already exceeded "
                 "the 60-second acceptance bound");

    for (std::size_t cylinder = 0U; cylinder < kCylinderCount; ++cylinder) {
        const std::string prefix = "cylinder." + std::to_string(cylinder + 1U) + ".";
        const auto &value = metrics.cylinders[cylinder];
        require_not_grossly_mismatched(gate, value.static_pressure, kRequiredFrameCount,
                                       prefix + "primary_static_pressure");
        require_not_grossly_mismatched(gate, value.dynamic_forward, kRequiredFrameCount,
                                       prefix + "dynamic_forward_pressure");
        require_not_grossly_mismatched(gate, value.dynamic_reverse, kRequiredFrameCount,
                                       prefix + "dynamic_reverse_pressure");
        require_not_grossly_mismatched(gate, value.pre_delay, kRequiredFrameCount,
                                       prefix + "pre_delay_excitation");
        require_not_grossly_mismatched(gate, value.post_delay, kRequiredFrameCount,
                                       prefix + "post_delay_excitation");
    }
    for (std::size_t route = 0U; route < kRouteCount; ++route) {
        const auto &value = metrics.route_buses[route];
        const std::string label =
            "route." + std::to_string(route + 1U) + ".pre_dsp_bus";
        require_not_grossly_mismatched(gate, value, kRequiredFrameCount, label);
        gate.require(value.candidate.nonzero_count > 1000U &&
                         value.candidate.positive_count > 0U &&
                         value.candidate.negative_count > 0U,
                     label + " lacks sustained bipolar activity");
    }
    return gate;
}

void append_comparison(std::ostringstream &text, std::string_view label,
                       const SignalComparison &value) {
    text << label << ".count=" << value.candidate.count << '\n'
         << label << ".nonfinite_count=" << value.nonfinite_count << '\n'
         << label << ".candidate_dc=" << value.candidate.dc() << '\n'
         << label << ".candidate_min=" << value.candidate.minimum << '\n'
         << label << ".candidate_max=" << value.candidate.maximum << '\n'
         << label << ".candidate_peak=" << value.candidate.peak << '\n'
         << label << ".candidate_rms=" << value.candidate.rms() << '\n'
         << label << ".reference_dc=" << value.reference.dc() << '\n'
         << label << ".reference_min=" << value.reference.minimum << '\n'
         << label << ".reference_max=" << value.reference.maximum << '\n'
         << label << ".reference_peak=" << value.reference.peak << '\n'
         << label << ".reference_rms=" << value.reference.rms() << '\n'
         << label << ".bias=" << value.bias() << '\n'
         << label << ".mae=" << value.mae() << '\n'
         << label << ".rmse=" << value.rmse() << '\n'
         << label << ".normalized_rmse=" << value.normalized_rmse() << '\n'
         << label << ".maximum_absolute_error=" << value.maximum_absolute_error << '\n'
         << label << ".correlation=" << value.correlation() << '\n'
         << label << ".rms_ratio=" << value.rms_ratio() << '\n';
}

[[nodiscard]] std::string make_verification(
    const reference::P18LoadedReferenceFixture &fixture,
    const reference::P18SealedPresentationEvidence &evidence,
    const LocalEvaluationSink &sink, const EvaluationMetrics &metrics,
    const OracleVerification &oracle, const GateReport &gate, std::uint64_t frame_count,
    std::uint64_t block_count, double comparator_load_seconds, double oracle_seconds,
    double request_compile_seconds, double diagnostic_seconds, double callback_seconds,
    double pipeline_seconds, double presentation_process_seconds,
    double presentation_finish_seconds) {
    std::ostringstream text;
    text << std::setprecision(17)
         << "M3 BMW M52B28 physics-generated listening-gate verification\n"
         << "claim=local-evaluation candidate; not a production publication\n"
         << "exact_reference_match=no\n"
         << "reference_audit_used_as_renderer_input=no\n"
         << "renderer_input=live CapturedExhaustExcitationSession buses only\n"
         << "presentation_path=unchanged P18PresentationSession\n"
         << "production_manifest_written=no\n"
         << "gate_status=" << (gate.passed() ? "pass" : "fail") << '\n'
         << "gate.gross_minimum_correlation=" << kGrossMinimumCorrelation << '\n'
         << "gate.gross_maximum_normalized_rmse=" << kGrossMaximumNormalizedRmse << '\n'
         << "gate.gross_rms_ratio_range=" << kGrossMinimumRmsRatio << ','
         << kGrossMaximumRmsRatio << '\n'
         << "gate.maximum_pipeline_seconds=" << kMaximumPipelineSeconds << '\n'
         << "gate.maximum_circular_crank_error_rad=" << kMaximumCircularCrankErrorRad
         << '\n';
    for (std::size_t index = 0U; index < gate.failures.size(); ++index) {
        text << "gate.failure." << index << '=' << gate.failures[index] << '\n';
    }

    text << "\ntimings\n"
         << "fixture_preflight_seconds="
         << std::chrono::duration<double>(fixture.preflight_duration).count() << '\n'
         << "comparator_load_decode_seconds=" << comparator_load_seconds << '\n'
         << "reference_oracle_seconds=" << oracle_seconds << '\n'
         << "request_and_session_compile_seconds=" << request_compile_seconds << '\n'
         << "physics_excitation_diagnostic_seconds=" << diagnostic_seconds << '\n'
         << "validation_compare_excitation_callback_seconds=" << callback_seconds
         << '\n'
         << "simulation_outside_callback_seconds="
         << std::max(0.0, diagnostic_seconds - callback_seconds) << '\n'
         << "physics_excitation_presentation_pipeline_seconds=" << pipeline_seconds
         << '\n'
         << "presentation_process_seconds=" << presentation_process_seconds << '\n'
         << "presentation_finish_seconds=" << presentation_finish_seconds << '\n'
         << "observed_presentation_execution_seconds="
         << std::chrono::duration<double>(evidence.execution().facts().wall_elapsed)
                .count()
         << '\n';

    text << "\nextents_and_timing\n"
         << "input_frames=" << frame_count << '\n'
         << "input_blocks=" << block_count << '\n'
         << "presentation_processed_blocks=" << evidence.stats().processed_block_count
         << '\n'
         << "presentation_warmup_blocks=" << evidence.stats().warmup_block_count << '\n'
         << "presentation_published_blocks=" << evidence.stats().published_block_count
         << '\n'
         << "presentation_published_source_frames="
         << evidence.stats().published_source_frame_count << '\n'
         << "timing_index_mismatches=" << metrics.timing_index_mismatches << '\n'
         << "control_mismatches=" << metrics.control_mismatches << '\n'
         << "maximum_circular_crank_error_rad="
         << metrics.maximum_circular_crank_error_rad << '\n'
         << "event_count=" << metrics.event_count << '\n'
         << "spark_crossing_count=" << metrics.spark_crossing_count << '\n'
         << "ignition_accepted_count=" << metrics.ignition_accepted_count << '\n'
         << "ignition_rejected_count=" << metrics.ignition_rejected_count << '\n'
         << "unmatched.event_timestamps=no frozen event-timestamp comparator lane\n"
         << "unmatched.torque=no frozen torque comparator lane\n";

    text << "\nreference_oracle\n"
         << "pre_delay_binary64_bit_mismatches=" << oracle.pre_delay_bit_mismatches
         << '\n'
         << "post_delay_binary64_bit_mismatches=" << oracle.post_delay_bit_mismatches
         << '\n'
         << "route_bus_binary64_bit_mismatches=" << oracle.route_bus_bit_mismatches
         << '\n'
         << "audit_post_to_bus_binary64_bit_mismatches="
         << oracle.audit_post_to_bus_bit_mismatches << '\n';

    text << "\ncomparisons\n";
    append_comparison(text, "engine_speed_rpm", metrics.engine_speed_rpm);
    append_comparison(text, "filtered_engine_speed_rpm",
                      metrics.filtered_engine_speed_rpm);
    append_comparison(text, "crank_angle_rad", metrics.crank_angle_rad);
    for (std::size_t cylinder = 0U; cylinder < kCylinderCount; ++cylinder) {
        const std::string prefix = "cylinder." + std::to_string(cylinder + 1U) + ".";
        const auto &value = metrics.cylinders[cylinder];
        append_comparison(text, prefix + "primary_static_pressure_pa_abs",
                          value.static_pressure);
        append_comparison(text, prefix + "dynamic_forward_pressure_pa",
                          value.dynamic_forward);
        append_comparison(text, prefix + "dynamic_reverse_pressure_pa",
                          value.dynamic_reverse);
        append_comparison(text, prefix + "pre_delay_engine_sim_source_unit",
                          value.pre_delay);
        append_comparison(text, prefix + "post_delay_engine_sim_source_unit",
                          value.post_delay);
    }
    for (std::size_t route = 0U; route < kRouteCount; ++route) {
        const std::string prefix =
            "route." + std::to_string(route + 1U) + ".pre_dsp_bus";
        append_comparison(text, prefix, metrics.route_buses[route]);
        text << prefix << ".candidate_nonzero_count="
             << metrics.route_buses[route].candidate.nonzero_count << '\n'
             << prefix << ".candidate_positive_count="
             << metrics.route_buses[route].candidate.positive_count << '\n'
             << prefix << ".candidate_negative_count="
             << metrics.route_buses[route].candidate.negative_count << '\n';
    }

    text << "\nverified_fixture_inputs\n";
    for (const auto file : {reference::P18ReferenceLineageFile::parity_evidence,
                            reference::P18ReferenceLineageFile::audit_input,
                            reference::P18ReferenceLineageFile::component_seed_input,
                            reference::P18ReferenceLineageFile::configured_ir_input}) {
        const auto &identity = fixture.verified_lineage.at(file);
        const auto &expected = required_lineage_file(file);
        text << expected.expected_relative_path << ".bytes=" << identity.byte_count
             << '\n'
             << expected.expected_relative_path
             << ".sha256=" << digest_hex(identity.payload_sha256) << '\n';
    }

    text << "\ncandidate_audio_artifacts\n";
    const auto &retained = sink.sealed_artifacts();
    for (std::size_t index = 0U; index < retained.size(); ++index) {
        const auto &record = *retained[index].sealed_record;
        if (record != evidence.artifacts()[index]) {
            fail("retained candidate artifact differs from presentation evidence");
        }
        text << record.relative_path << ".role=" << record.role << '\n'
             << record.relative_path << ".bytes=" << record.byte_count << '\n'
             << record.relative_path << ".sha256=" << digest_hex(record.payload_sha256)
             << '\n';
    }
    return text.str();
}

class StagingDirectory final {
  public:
    explicit StagingDirectory(std::filesystem::path path) : path_(std::move(path)) {}
    ~StagingDirectory() {
        if (active_) {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }
    }
    StagingDirectory(const StagingDirectory &) = delete;
    StagingDirectory &operator=(const StagingDirectory &) = delete;
    [[nodiscard]] const std::filesystem::path &path() const noexcept {
        return path_;
    }
    void release() noexcept {
        active_ = false;
    }

  private:
    std::filesystem::path path_;
    bool active_ = true;
};

void write_exact_file(const std::filesystem::path &path,
                      std::span<const std::byte> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("could not create local-evaluation output " + path.string());
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
        fail("could not write local-evaluation output " + path.string());
    }
}

void write_exact_file(const std::filesystem::path &path, std::string_view text) {
    write_exact_file(path,
                     {reinterpret_cast<const std::byte *>(text.data()), text.size()});
}

[[nodiscard]] double
publish_local_evaluation(const std::filesystem::path &output_directory,
                         const LocalEvaluationSink &sink, std::string verification) {
    if (output_directory.empty()) {
        fail("output directory is empty");
    }
    std::error_code error;
    if (std::filesystem::exists(output_directory, error) || error) {
        fail("output directory already exists or cannot be inspected: " +
             output_directory.string());
    }
    auto parent = output_directory.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::filesystem::create_directories(parent, error);
    if (error) {
        fail("could not create output parent: " + error.message());
    }

    std::filesystem::path staging;
    for (std::uint32_t attempt = 0U; attempt < 100U; ++attempt) {
        const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
        staging = parent / (".m3-bmw-listening-stage-" + std::to_string(token) + "-" +
                            std::to_string(attempt));
        if (std::filesystem::create_directory(staging, error)) {
            break;
        }
        if (error) {
            fail("could not create local-evaluation staging directory: " +
                 error.message());
        }
    }
    if (staging.empty() || !std::filesystem::exists(staging)) {
        fail("could not allocate a unique local-evaluation staging directory");
    }
    StagingDirectory guard{staging};

    const auto write_started = std::chrono::steady_clock::now();
    for (const auto &artifact : sink.sealed_artifacts()) {
        const auto path = staging / artifact.declaration.relative_path;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            fail("could not create candidate audio directory: " + error.message());
        }
        write_exact_file(path, artifact.bytes);
    }
    const double audio_write_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - write_started)
            .count();
    verification += "\npublication\nlocal_evaluation_audio_write_seconds=" +
                    std::to_string(audio_write_seconds) +
                    "\nproduction_manifest_written=no\n";
    write_exact_file(staging / "verification.txt", verification);

    std::filesystem::rename(staging, output_directory, error);
    if (error) {
        fail("could not atomically publish local-evaluation directory: " +
             error.message());
    }
    guard.release();
    return audio_write_seconds;
}

[[nodiscard]] profiles::BmwM52b28ParityRequest
require_bmw_request(const reference::DecodedReferenceParityV1 &parity) {
    std::vector<double> rpm;
    rpm.reserve(parity.frames.size());
    for (const auto &frame : parity.frames) {
        rpm.push_back(frame.engine_speed_rpm);
    }
    auto result = profiles::make_bmw_m52b28_parity_request(std::move(rpm));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        fail("canonical BMW request admission failed" +
             validation_report_text(*report));
    }
    return std::get<profiles::BmwM52b28ParityRequest>(std::move(result));
}

[[nodiscard]] simulation::LegacyLowOrderSimulationSession
require_simulation(const profiles::BmwM52b28ParityRequest &request) {
    auto result = simulation::compile_legacy_low_order_simulation_session(
        request.engine, request.scenario);
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        fail("BMW simulation admission failed" + validation_report_text(*report));
    }
    return std::get<simulation::LegacyLowOrderSimulationSession>(std::move(result));
}

[[nodiscard]] excitation::CapturedExhaustExcitationSession
require_excitation(const profiles::BmwM52b28ParityRequest &request) {
    auto result =
        excitation::compile_captured_exhaust_excitation_session(request.engine);
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        fail("BMW excitation admission failed" + validation_report_text(*report));
    }
    return std::get<excitation::CapturedExhaustExcitationSession>(std::move(result));
}

int run(int argc, char **argv) {
    if (argc != 3) {
        fail("usage: engine-sim-offline-m3-bmw-listening <fixture-root> "
             "<new-output-directory>");
    }
    const std::filesystem::path fixture_root{argv[1]};
    const std::filesystem::path output_directory{argv[2]};
    const auto command_started = std::chrono::steady_clock::now();

    auto fixture = reference::load_p18_reference_fixture(fixture_root);
    // The standard audit copy exists only because the accepted fixture loader
    // validates all lineage in one transaction. Remove it before renderer setup;
    // candidate buses are the sole presentation input below.
    fixture.audit.frames.clear();
    fixture.audit.frames.shrink_to_fit();

    const auto comparator_started = std::chrono::steady_clock::now();
    const auto parity = load_parity(fixture_root);
    const auto audit = load_full_audit(fixture_root);
    const double comparator_load_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      comparator_started)
            .count();
    if (parity.frames.size() != kRequiredFrameCount ||
        audit.frames.size() != kRequiredFrameCount) {
        fail("strict comparator decoders returned the wrong canonical extent");
    }

    const auto oracle_started = std::chrono::steady_clock::now();
    const auto oracle = verify_reference_oracle(parity, audit);
    const double oracle_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - oracle_started)
            .count();
    if (oracle.pre_delay_bit_mismatches != 0U ||
        oracle.post_delay_bit_mismatches != 0U ||
        oracle.route_bus_bit_mismatches != 0U ||
        oracle.audit_post_to_bus_bit_mismatches != 0U) {
        fail("reference parity-to-audit oracle reconstruction is not bit-exact");
    }

    const auto compile_started = std::chrono::steady_clock::now();
    auto request = require_bmw_request(parity);
    auto simulation = require_simulation(request);
    auto excitation = require_excitation(request);
    const double request_compile_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      compile_started)
            .count();

    EvaluationMetrics metrics;
    std::vector<presentation::ExhaustExcitationFrame> candidate_bus_trace;
    candidate_bus_trace.reserve(kRequiredFrameCount);
    std::uint64_t frame_cursor = 0U;
    std::uint64_t block_count = 0U;
    double callback_seconds = 0.0;
    const auto pipeline_started = std::chrono::steady_clock::now();
    while (frame_cursor < kRequiredFrameCount) {
        const std::uint64_t expected_first = frame_cursor;
        std::exception_ptr callback_failure;
        bool capture_callback_called = false;
        auto simulation_result =
            simulation.publish_next_block([&](const contract::CaptureBlockView &block) {
                ScopedSecondsAccumulator callback_timer{callback_seconds};
                capture_callback_called = true;
                try {
                    if (block.clock() !=
                            contract::CaptureClock{{10000, 1},
                                                   expected_first,
                                                   expected_first + 1U,
                                                   contract::SamplePhase::post_step} ||
                        block.frame_count() != kFramesPerBlock ||
                        !block.reference_parity().has_value()) {
                        fail("candidate capture block has the wrong clock or extent");
                    }
                    const auto report =
                        contract::validate(block, request.engine, request.scenario);
                    if (!report.ok()) {
                        fail("candidate capture block failed request-aware validation" +
                             validation_report_text(report));
                    }
                    compare_capture_block(block, expected_first, parity, metrics);

                    std::exception_ptr excitation_callback_failure;
                    auto excitation_result = excitation.process_block(
                        block,
                        [&](const presentation::ExhaustExcitationBlockView &output,
                            const excitation::ExhaustExcitationDiagnosticBlockView
                                &diagnostics) {
                            try {
                                if (output.first_frame_index() != expected_first ||
                                    diagnostics.first_frame_index() != expected_first ||
                                    output.frames().size() != kFramesPerBlock ||
                                    diagnostics.cylinder_ids() !=
                                        std::array<contract::CylinderId,
                                                   kCylinderCount>{
                                            contract::CylinderId{1},
                                            contract::CylinderId{2},
                                            contract::CylinderId{3},
                                            contract::CylinderId{4},
                                            contract::CylinderId{5},
                                            contract::CylinderId{6}} ||
                                    output.route_ids() !=
                                        std::array<contract::RouteId, kRouteCount>{
                                            contract::RouteId{1},
                                            contract::RouteId{2}}) {
                                    fail("candidate excitation callback metadata "
                                         "changed");
                                }
                                compare_excitation_block(diagnostics, expected_first,
                                                         audit, metrics);
                                candidate_bus_trace.insert(candidate_bus_trace.end(),
                                                           output.frames().begin(),
                                                           output.frames().end());
                                return true;
                            } catch (...) {
                                excitation_callback_failure = std::current_exception();
                                return false;
                            }
                        });
                    if (excitation_callback_failure != nullptr) {
                        std::rethrow_exception(excitation_callback_failure);
                    }
                    if (const auto *failure =
                            std::get_if<contract::FailureContext>(&excitation_result)) {
                        fail("candidate excitation failed: " + failure_text(*failure));
                    }
                    const auto *published =
                        std::get_if<excitation::ExhaustExcitationBlockPublished>(
                            &excitation_result);
                    if (published == nullptr ||
                        published->block_ordinal != block_count ||
                        published->first_frame_index != expected_first ||
                        published->frame_count != kFramesPerBlock) {
                        fail("candidate excitation publication progress changed");
                    }
                    return true;
                } catch (...) {
                    callback_failure = std::current_exception();
                    return false;
                }
            });
        if (callback_failure != nullptr) {
            std::rethrow_exception(callback_failure);
        }
        if (const auto *failure =
                std::get_if<contract::FailureContext>(&simulation_result)) {
            fail("candidate simulation failed: " + failure_text(*failure));
        }
        const auto *published =
            std::get_if<simulation::LegacySimulationBlockPublished>(&simulation_result);
        if (!capture_callback_called || published == nullptr ||
            published->block_ordinal != block_count ||
            published->first_sample_index != expected_first ||
            published->frame_count != kFramesPerBlock) {
            fail("candidate simulation publication progress changed");
        }
        frame_cursor += published->frame_count;
        ++block_count;
    }

    std::uint64_t completion_callbacks = 0U;
    auto completion =
        simulation.publish_next_block([&](const contract::CaptureBlockView &) {
            ++completion_callbacks;
            return true;
        });
    const auto *completed =
        std::get_if<simulation::LegacySimulationCompleted>(&completion);
    if (completed == nullptr || completed->sample_count != kRequiredFrameCount ||
        completed->block_count != kRequiredBlockCount || completion_callbacks != 0U ||
        !simulation.completed() || simulation.faulted() || excitation.faulted() ||
        excitation.next_frame_index() != kRequiredFrameCount ||
        excitation.published_block_count() != kRequiredBlockCount ||
        candidate_bus_trace.size() != kRequiredFrameCount) {
        fail("candidate simulation/excitation did not complete stably");
    }

    const double diagnostic_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      pipeline_started)
            .count();
    auto gate =
        evaluate_gates(metrics, oracle, frame_cursor, block_count, diagnostic_seconds);
    if (!gate.passed()) {
        std::ostringstream message;
        message << "candidate failed its full excitation diagnostic before "
                   "presentation";
        for (const auto &failure : gate.failures) {
            message << "\n  " << failure;
        }
        fail(message.str());
    }

    LocalEvaluationSink sink;
    auto plan = candidate_plan();
    reference::P18PresentationSession presentation{
        sink,
        std::move(plan),
        reference::p18_reference_presentation_seeds(fixture.component_seeds),
        fixture.configured_ir_kernel,
    };
    constexpr std::array<contract::RouteId, kRouteCount> kRouteIds{
        contract::RouteId{1}, contract::RouteId{2}};
    double presentation_process_seconds = 0.0;
    const auto presentation_process_started = std::chrono::steady_clock::now();
    for (std::size_t block = 0U; block < kRequiredBlockCount; ++block) {
        std::span<const presentation::ExhaustExcitationFrame> frames{
            candidate_bus_trace.data() + block * kFramesPerBlock, kFramesPerBlock};
        presentation.process(
            presentation::ExhaustExcitationBlockView::borrow_for_callback(
                block * kFramesPerBlock, contract::RationalRateHz{10000, 1}, kRouteIds,
                frames));
    }
    presentation_process_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      presentation_process_started)
            .count();

    const auto finish_started = std::chrono::steady_clock::now();
    auto evidence = presentation.finish();
    const double presentation_finish_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - finish_started)
            .count();
    const double pipeline_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      pipeline_started)
            .count();
    gate.require(pipeline_seconds <= kMaximumPipelineSeconds,
                 "physics-to-WAV pipeline exceeded the 60-second acceptance bound");
    const auto verification = make_verification(
        fixture, evidence, sink, metrics, oracle, gate, frame_cursor, block_count,
        comparator_load_seconds, oracle_seconds, request_compile_seconds,
        diagnostic_seconds, callback_seconds, pipeline_seconds,
        presentation_process_seconds, presentation_finish_seconds);
    if (!gate.passed()) {
        std::ostringstream message;
        message << "candidate failed before WAV publication";
        for (const auto &failure : gate.failures) {
            message << "\n  " << failure;
        }
        fail(message.str());
    }

    const double audio_write_seconds =
        publish_local_evaluation(output_directory, sink, verification);
    const double command_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      command_started)
            .count();
    const auto audition =
        output_directory / "audio/bmw-m52b28-m3-candidate-listening.wav";
    std::cout << std::setprecision(17) << "output=" << output_directory.string() << '\n'
              << "audition=" << audition.string() << '\n'
              << "verification=" << (output_directory / "verification.txt").string()
              << '\n'
              << "exact_reference_match=no\n"
              << "reference_audit_used_as_renderer_input=no\n"
              << "gate_status=pass\n"
              << "pipeline_seconds=" << pipeline_seconds << '\n'
              << "audio_write_seconds=" << audio_write_seconds << '\n'
              << "total_command_seconds=" << command_seconds << '\n'
              << "wav_count=8\n"
              << "production_manifest_written=no\n";
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "M3 BMW listening gate failed: " << error.what() << '\n';
        return 1;
    }
}
