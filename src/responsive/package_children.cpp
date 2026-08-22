#include "crankwave/responsive/package_children.hpp"

#include "crankwave/artifacts/crankwave_container.hpp"
#include "crankwave/authoring/json.hpp"
#include "crankwave/authoring/parse.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace crankwave::responsive {
namespace {

using Error = NativeResponsivePackageError;
using ErrorCode = NativeResponsivePackageErrorCode;

constexpr std::size_t kMaximumChildCount = 3U;
constexpr std::size_t kMaximumJsonBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumTokenBytes = 96U;
constexpr std::uint64_t kFixedFftSize = 65'536U;
constexpr std::uint64_t kFixedCoefficientCount = 30'071U;
constexpr std::uint64_t kPartitionedFftSize = 8'192U;
constexpr std::uint64_t kPartitionFrameCount = 3'840U;
constexpr std::uint64_t kMaximumPartitionedCoefficientCount = 570'654U;
constexpr std::string_view kSharedStarterAudioPackagePath =
    "shared-recorded-starter/audio/recorded-starter.cropped.192000hz.mono.f32le";
constexpr std::string_view kSharedStarterManifestSha256 =
    "73110090f07df4523081fac3452ee1cc0b3aab6b0b8a356186ca18db3c011bc2";
constexpr std::string_view kSharedStarterPayloadSha256 =
    "b25b6277e375d5dd92cec98e7d33765a6898461e00597935cd526c850db8c0be";

[[nodiscard]] Error error(const ErrorCode code, std::string detail_code,
                          std::string path, std::string message) {
    return {code, std::move(detail_code), std::move(path), std::move(message)};
}

[[nodiscard]] Error cancelled_error() {
    return error(ErrorCode::cancelled, "responsive-child-encoding-cancelled", "",
                 "responsive child package encoding was cancelled");
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64U);
    for (const auto value : digest.bytes) {
        result.push_back(digits[value >> 4U]);
        result.push_back(digits[value & 0x0fU]);
    }
    return result;
}

class JsonWriter final {
  public:
    [[nodiscard]] bool begin_object() {
        if (!before_value()) {
            return false;
        }
        bytes_.push_back('{');
        stack_.push_back({Kind::object, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_object() {
        if (stack_.empty() || stack_.back().kind != Kind::object ||
            stack_.back().awaiting_value) {
            return fail("invalid object close");
        }
        bytes_.push_back('}');
        stack_.pop_back();
        return size_ok();
    }

    [[nodiscard]] bool begin_array() {
        if (!before_value()) {
            return false;
        }
        bytes_.push_back('[');
        stack_.push_back({Kind::array, 0U, false});
        return true;
    }

    [[nodiscard]] bool end_array() {
        if (stack_.empty() || stack_.back().kind != Kind::array) {
            return fail("invalid array close");
        }
        bytes_.push_back(']');
        stack_.pop_back();
        return size_ok();
    }

    [[nodiscard]] bool key(const std::string_view value) {
        if (stack_.empty() || stack_.back().kind != Kind::object ||
            stack_.back().awaiting_value) {
            return fail("object key is out of sequence");
        }
        auto &context = stack_.back();
        if (context.count != 0U) {
            bytes_.push_back(',');
        }
        if (!append_string(value)) {
            return false;
        }
        bytes_.push_back(':');
        context.awaiting_value = true;
        return size_ok();
    }

    [[nodiscard]] bool string_value(const std::string_view value) {
        return before_value() && append_string(value);
    }

    [[nodiscard]] bool number_value(const double value) {
        if (!std::isfinite(value) || !before_value()) {
            return fail("non-finite JSON number");
        }
        std::array<char, 64U> encoded{};
        const auto normalized = value == 0.0 ? 0.0 : value;
        const auto [end, conversion_error] = std::to_chars(
            encoded.data(), encoded.data() + encoded.size(), normalized,
            std::chars_format::general, std::numeric_limits<double>::max_digits10);
        if (conversion_error != std::errc{}) {
            return fail("JSON number conversion failed");
        }
        bytes_.append(encoded.data(), static_cast<std::size_t>(end - encoded.data()));
        return size_ok();
    }

    [[nodiscard]] bool uint_value(const std::uint64_t value) {
        if (!before_value()) {
            return false;
        }
        std::array<char, 32U> encoded{};
        const auto [end, conversion_error] =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), value);
        if (conversion_error != std::errc{}) {
            return fail("JSON integer conversion failed");
        }
        bytes_.append(encoded.data(), static_cast<std::size_t>(end - encoded.data()));
        return size_ok();
    }

    [[nodiscard]] bool bool_value(const bool value) {
        if (!before_value()) {
            return false;
        }
        bytes_.append(value ? "true" : "false");
        return size_ok();
    }

    [[nodiscard]] std::variant<std::vector<std::byte>, Error>
    finish(const std::string_view path) {
        if (!failed_ && !root_written_) {
            static_cast<void>(fail("JSON root is absent"));
        }
        if (!failed_ && !stack_.empty()) {
            static_cast<void>(fail("JSON containers remain open"));
        }
        if (failed_) {
            return error(ErrorCode::invalid_argument,
                         "responsive-child-json-encoding-failed", std::string{path},
                         failure_);
        }
        bytes_.push_back('\n');
        std::vector<std::byte> output(bytes_.size());
        std::transform(
            bytes_.begin(), bytes_.end(), output.begin(), [](const char byte) {
                return static_cast<std::byte>(static_cast<unsigned char>(byte));
            });
        return output;
    }

  private:
    enum class Kind : std::uint8_t { object, array };
    struct Context {
        Kind kind = Kind::object;
        std::size_t count = 0U;
        bool awaiting_value = false;
    };

    [[nodiscard]] bool before_value() {
        if (failed_) {
            return false;
        }
        if (stack_.empty()) {
            if (root_written_) {
                return fail("multiple JSON roots");
            }
            root_written_ = true;
            return true;
        }
        auto &context = stack_.back();
        if (context.kind == Kind::object) {
            if (!context.awaiting_value) {
                return fail("object value has no key");
            }
            context.awaiting_value = false;
            ++context.count;
            return true;
        }
        if (context.count != 0U) {
            bytes_.push_back(',');
        }
        ++context.count;
        return size_ok();
    }

    [[nodiscard]] bool append_string(const std::string_view value) {
        constexpr std::string_view digits = "0123456789abcdef";
        bytes_.push_back('"');
        for (const unsigned char character : value) {
            switch (character) {
            case '"':
                bytes_.append("\\\"");
                break;
            case '\\':
                bytes_.append("\\\\");
                break;
            case '\b':
                bytes_.append("\\b");
                break;
            case '\f':
                bytes_.append("\\f");
                break;
            case '\n':
                bytes_.append("\\n");
                break;
            case '\r':
                bytes_.append("\\r");
                break;
            case '\t':
                bytes_.append("\\t");
                break;
            default:
                if (character < 0x20U) {
                    bytes_.append("\\u00");
                    bytes_.push_back(digits[character >> 4U]);
                    bytes_.push_back(digits[character & 0x0fU]);
                } else {
                    bytes_.push_back(static_cast<char>(character));
                }
                break;
            }
        }
        bytes_.push_back('"');
        return size_ok();
    }

    [[nodiscard]] bool size_ok() {
        return bytes_.size() <= kMaximumJsonBytes ||
               fail("JSON document exceeds the responsive child limit");
    }

    [[nodiscard]] bool fail(std::string message) {
        if (!failed_) {
            failed_ = true;
            failure_ = std::move(message);
        }
        return false;
    }

    std::string bytes_;
    std::vector<Context> stack_;
    bool root_written_ = false;
    bool failed_ = false;
    std::string failure_;
};

[[nodiscard]] bool field(JsonWriter &writer, const std::string_view key,
                         const std::string_view value) {
    return writer.key(key) && writer.string_value(value);
}

[[nodiscard]] bool field(JsonWriter &writer, const std::string_view key,
                         const double value) {
    return writer.key(key) && writer.number_value(value);
}

[[nodiscard]] bool uint_field(JsonWriter &writer, const std::string_view key,
                              const std::uint64_t value) {
    return writer.key(key) && writer.uint_value(value);
}

[[nodiscard]] bool bool_field(JsonWriter &writer, const std::string_view key,
                              const bool value) {
    return writer.key(key) && writer.bool_value(value);
}

[[nodiscard]] bool digest_field(JsonWriter &writer, const std::string_view key,
                                const contract::Sha256Digest &value) {
    return field(writer, key, digest_hex(value));
}

template <class Range>
[[nodiscard]] bool string_array(JsonWriter &writer, const std::string_view key,
                                const Range &values) {
    if (!writer.key(key) || !writer.begin_array()) {
        return false;
    }
    for (const auto &value : values) {
        if (!writer.string_value(value)) {
            return false;
        }
    }
    return writer.end_array();
}

template <class Range>
[[nodiscard]] bool number_array(JsonWriter &writer, const std::string_view key,
                                const Range &values) {
    if (!writer.key(key) || !writer.begin_array()) {
        return false;
    }
    for (const auto value : values) {
        if (!writer.number_value(value)) {
            return false;
        }
    }
    return writer.end_array();
}

[[nodiscard]] bool identity_object(JsonWriter &writer,
                                   const ResponsivePackageProvenanceV1 &provenance,
                                   const bool engine) {
    return writer.begin_object() &&
           field(writer, "id",
                 engine ? provenance.engine_id : provenance.renderer_build_id) &&
           digest_field(writer, "sha256",
                        engine ? provenance.compiled_engine_sha256
                               : provenance.renderer_source_sha256) &&
           writer.end_object();
}

[[nodiscard]] std::string portable_token(const std::string_view value) {
    std::string result;
    result.reserve(std::min(value.size(), kMaximumTokenBytes));
    bool prior_separator = false;
    for (const unsigned char character : value) {
        char encoded = '-';
        if (character >= 'A' && character <= 'Z') {
            encoded = static_cast<char>(character - 'A' + 'a');
        } else if ((character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9')) {
            encoded = static_cast<char>(character);
        }
        const bool separator = encoded == '-';
        if (separator && (result.empty() || prior_separator)) {
            continue;
        }
        if (result.size() == kMaximumTokenBytes) {
            break;
        }
        result.push_back(encoded);
        prior_separator = separator;
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "artifact" : result;
}

[[nodiscard]] std::string short_digest(const contract::Sha256Digest &digest) {
    return digest_hex(digest).substr(0U, 16U);
}

[[nodiscard]] std::vector<std::byte>
float32_le_bytes(const std::span<const float> samples) {
    std::vector<std::byte> result;
    result.reserve(samples.size() * sizeof(float));
    for (const float sample : samples) {
        const auto bits = std::bit_cast<std::uint32_t>(sample);
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            result.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
        }
    }
    return result;
}

[[nodiscard]] bool finite_samples(const std::span<const float> samples) {
    return std::ranges::all_of(samples,
                               [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] bool finite_all(const std::initializer_list<double> values) {
    return std::ranges::all_of(values,
                               [](const double value) { return std::isfinite(value); });
}

[[nodiscard]] bool valid_bus(const std::string_view value) {
    return value.size() <= 127U && contract::is_valid_semantic_id(value) &&
           value.find('/') == value.npos;
}

[[nodiscard]] bool same_lanes(const ResponsiveBakeProfile &profile,
                              const std::string_view lane_id, const double throttle) {
    return std::ranges::any_of(profile.capture.load_lanes, [&](const auto &lane) {
        return lane.id == lane_id && lane.throttle_01 == throttle;
    });
}

[[nodiscard]] bool is_anchor(const ResponsiveBakeProfile &profile, const double rpm) {
    return std::ranges::find(profile.rpm.anchors, rpm) != profile.rpm.anchors.end();
}

[[nodiscard]] std::optional<Error>
validate_core(const ResponsivePackageChildrenViewV1 &input,
              const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    if (input.profile == nullptr || input.held == nullptr ||
        input.directional == nullptr || input.presentation == nullptr) {
        return error(ErrorCode::invalid_argument,
                     "responsive-child-cooked-input-missing", "/children",
                     "profile, held, directional and presentation inputs are required");
    }
    const auto &profile = *input.profile;
    const auto &held = *input.held;
    const auto &directional = *input.directional;
    const auto &presentation = *input.presentation;
    const auto &provenance = input.provenance;
    if (!validate_responsive_bake_profile(profile).ok() ||
        !contract::is_valid_semantic_id(provenance.engine_id) ||
        provenance.compiled_engine_sha256.is_zero() ||
        provenance.renderer_source_sha256.is_zero() ||
        provenance.renderer_build_id != "crankwave-renderer-build" ||
        directional.engine_id != provenance.engine_id ||
        presentation.engine_id != provenance.engine_id ||
        held.identity_sha256.is_zero() || directional.identity_sha256.is_zero() ||
        presentation.identity_sha256.is_zero()) {
        return error(
            ErrorCode::invalid_identity, "responsive-child-common-identity-invalid",
            "/children",
            "cooked products and package provenance do not share one valid identity");
    }
    if (profile.capture.physics_rate_hz != kResponsiveRuntimePhysicsRateHzV1 ||
        directional.outer_minimum_rpm != profile.rpm.outer_minimum_rpm ||
        directional.outer_maximum_rpm != profile.rpm.outer_maximum_rpm ||
        directional.rpm_anchors != profile.rpm.anchors ||
        !(directional.outer_maximum_rpm > directional.outer_minimum_rpm)) {
        return error(ErrorCode::topology_mismatch,
                     "responsive-child-profile-domain-mismatch", "/children/domain",
                     "cooked directional domain differs from the selected profile");
    }
    if (directional.selected_bus_ids.empty() ||
        presentation.routes.size() != directional.selected_bus_ids.size() ||
        presentation.audition_dry_bus_order != directional.selected_bus_ids ||
        !valid_bus(presentation.audition_bus_id) ||
        !(std::isfinite(presentation.captured_to_source_scale) &&
          presentation.captured_to_source_scale > 0.0) ||
        !(std::isfinite(presentation.master_volume_linear) &&
          presentation.master_volume_linear > 0.0)) {
        return error(
            ErrorCode::topology_mismatch,
            "responsive-child-presentation-topology-mismatch", "/children/presentation",
            "presentation routes or audition order differ from cooked dry buses");
    }
    std::set<std::string_view> buses;
    for (std::size_t index = 0U; index < directional.selected_bus_ids.size(); ++index) {
        const auto &bus = directional.selected_bus_ids[index];
        const auto &route = presentation.routes[index];
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (!valid_bus(bus) || !buses.insert(bus).second || route.dry_bus_id != bus ||
            !valid_bus(route.source_route_id) ||
            !contract::is_valid_semantic_id(route.impulse_response_asset_id) ||
            route.impulse_response_payload_sha256.is_zero() ||
            !finite_all({route.impulse_response_gain_linear, route.wet_mix_01}) ||
            route.impulse_response_gain_linear < 0.0 || route.wet_mix_01 < 0.0 ||
            route.wet_mix_01 > 1.0 ||
            route.transfer_index >= presentation.transfers.size()) {
            return error(ErrorCode::invalid_argument,
                         "responsive-child-presentation-route-invalid",
                         "/children/presentation/routes/" + std::to_string(index),
                         "presentation route is invalid or out of canonical bus order");
        }
    }
    if (presentation.transfers.empty()) {
        return error(ErrorCode::invalid_argument,
                     "responsive-child-presentation-transfer-missing",
                     "/children/presentation/transfers",
                     "presentation requires at least one compiled transfer");
    }
    std::vector<bool> used_transfers(presentation.transfers.size(), false);
    for (const auto &route : presentation.routes) {
        used_transfers[route.transfer_index] = true;
    }
    for (std::size_t index = 0U; index < presentation.transfers.size(); ++index) {
        const auto &transfer = presentation.transfers[index];
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        std::uint64_t expected_bytes = 0U;
        bool shape_ok = false;
        if (transfer.shape == ResponsiveTransferShape::fixed_overlap_save) {
            expected_bytes = kFixedFftSize * 2U * sizeof(double);
            shape_ok = transfer.fft_size == kFixedFftSize &&
                       transfer.coefficient_count == kFixedCoefficientCount &&
                       transfer.partition_frame_count == 0U &&
                       transfer.partition_count == 1U;
        } else {
            const auto partitions =
                (transfer.coefficient_count + kPartitionFrameCount - 1U) /
                kPartitionFrameCount;
            if (partitions <= std::numeric_limits<std::uint64_t>::max() /
                                  (kPartitionedFftSize * 2U * sizeof(double))) {
                expected_bytes = partitions * kPartitionedFftSize * 2U * sizeof(double);
            }
            shape_ok =
                transfer.fft_size == kPartitionedFftSize &&
                transfer.coefficient_count > kFixedCoefficientCount &&
                transfer.coefficient_count <= kMaximumPartitionedCoefficientCount &&
                transfer.partition_frame_count == kPartitionFrameCount &&
                transfer.partition_count == partitions;
        }
        if (!used_transfers[index] || !shape_ok ||
            transfer.spectrum_encoding != kResponsiveTransferSpectrumEncoding ||
            transfer.spectrum_bytes.size() != expected_bytes ||
            transfer.spectrum_sha256.is_zero() ||
            contract::sha256(transfer.spectrum_bytes) != transfer.spectrum_sha256 ||
            transfer.identity_sha256.is_zero()) {
            return error(ErrorCode::invalid_member,
                         "responsive-child-presentation-transfer-invalid",
                         "/children/presentation/transfers/" + std::to_string(index),
                         "compiled transfer shape, use, bytes or identity is invalid");
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
validate_held(const ResponsivePackageChildrenViewV1 &input,
              const std::stop_token stop_token) {
    const auto &profile = *input.profile;
    const auto &held = *input.held;
    const auto &buses = input.directional->selected_bus_ids;
    if (held.cells.empty() || held.phase_alignment.cells.size() != held.cells.size() ||
        held.phase_alignment.method != kHeldPhaseAlignmentMethodId ||
        held.phase_alignment.reference_cell_id.empty() ||
        held.phase_alignment.identity_sha256.is_zero()) {
        return error(ErrorCode::invalid_argument, "responsive-child-held-grid-invalid",
                     "/children/held", "held grid or phase alignment is incomplete");
    }
    std::set<std::pair<double, std::string>> coordinates;
    std::set<std::string_view> cell_ids;
    for (std::size_t cell_index = 0U; cell_index < held.cells.size(); ++cell_index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &cell = held.cells[cell_index];
        if (cell.id.empty() || !cell_ids.insert(cell.id).second ||
            !is_anchor(profile, cell.rpm) ||
            !same_lanes(profile, cell.lane_id, cell.throttle_01) ||
            !coordinates.emplace(cell.rpm, cell.lane_id).second ||
            cell.routes.size() != buses.size() || cell.identity_sha256.is_zero() ||
            cell.coalesced_authored_lanes.empty() ||
            cell.coalesced_authored_lanes.size() !=
                cell.coalesced_capture_throttles_01.size()) {
            return error(ErrorCode::invalid_argument,
                         "responsive-child-held-cell-invalid",
                         "/children/held/cells/" + std::to_string(cell_index),
                         "held cell coordinate, identity or route shape is invalid");
        }
        for (std::size_t route_index = 0U; route_index < cell.routes.size();
             ++route_index) {
            const auto &route = cell.routes[route_index];
            const auto mean = std::span<const float>{route.texture.mean};
            const auto residuals = std::span<const float>{route.texture.residuals};
            const auto &metrics = route.texture.metrics;
            const bool boundaries_zero = [&]() {
                for (std::size_t cycle = 0U; cycle < kHeldCapturedCycleCount; ++cycle) {
                    const auto offset = cycle * kHeldSamplesPerCycle;
                    if (residuals[offset] != 0.0F ||
                        residuals[offset + kHeldSamplesPerCycle - 1U] != 0.0F) {
                        return false;
                    }
                }
                return true;
            }();
            if (route.route_id != buses[route_index] ||
                route.source_interval.end_revolutions -
                        route.source_interval.start_revolutions !=
                    kHeldCycleRevolutions * kHeldCapturedCycleCount ||
                mean.size() != kHeldSamplesPerCycle ||
                residuals.size() != kHeldNormalizedSampleCount ||
                !finite_samples(mean) || !finite_samples(residuals) ||
                !boundaries_zero ||
                held_float32_payload_identity(mean) != route.mean_payload_sha256 ||
                held_float32_payload_identity(residuals) !=
                    route.residual_payload_sha256 ||
                route.identity_sha256.is_zero() ||
                !finite_all({route.source_interval.start_revolutions,
                             route.source_interval.end_revolutions,
                             route.telemetry.mean_manifold_pressure_pa_abs,
                             route.telemetry.rpm_error_rms,
                             route.telemetry.maximum_absolute_rpm_error,
                             metrics.source_rms, metrics.mean_rms,
                             metrics.raw_residual_rms_over_source_rms,
                             metrics.tapered_residual_rms_over_source_rms,
                             metrics.reconstruction_error_rms_over_source_rms,
                             metrics.mean_seam_absolute_delta,
                             metrics.mean_seam_over_mean_derivative_rms,
                             metrics.mean_closure_correction_absolute,
                             metrics.mean_closure_correction_over_source_rms,
                             metrics.maximum_residual_boundary_magnitude})) {
                return error(
                    ErrorCode::invalid_member, "responsive-child-held-route-invalid",
                    "/children/held/cells/" + std::to_string(cell_index) + "/routes/" +
                        std::to_string(route_index),
                    "held route payload, hash, metrics or interval is invalid");
            }
        }
    }
    std::set<std::string_view> aligned;
    for (const auto &shift : held.phase_alignment.cells) {
        if (!cell_ids.contains(shift.cell_id) ||
            !aligned.insert(shift.cell_id).second ||
            !std::isfinite(shift.shift_to_canonical_samples)) {
            return error(ErrorCode::invalid_argument,
                         "responsive-child-held-alignment-invalid",
                         "/children/held/phase_alignment",
                         "held phase alignment is not a one-to-one finite cell map");
        }
    }
    if (!cell_ids.contains(held.phase_alignment.reference_cell_id)) {
        return error(ErrorCode::invalid_argument,
                     "responsive-child-held-reference-invalid",
                     "/children/held/phase_alignment/reference_cell_id",
                     "held phase reference is not a retained cell");
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
validate_directional(const ResponsivePackageChildrenViewV1 &input,
                     const std::stop_token stop_token) {
    const auto &profile = *input.profile;
    const auto &model = *input.directional;
    if (model.route_directions.size() != model.selected_bus_ids.size() * 2U ||
        !model.combined_seam_gate.passed ||
        !finite_all(
            {model.combined_seam_gate.maximum_seam_over_source_adjacent_derivative_rms,
             model.combined_seam_gate.maximum_correction_rms_over_source_rms,
             model.combined_seam_gate
                 .observed_maximum_seam_over_source_adjacent_derivative_rms,
             model.combined_seam_gate
                 .observed_maximum_correction_rms_over_source_rms})) {
        return error(ErrorCode::invalid_argument,
                     "responsive-child-directional-model-invalid",
                     "/children/directional",
                     "directional topology or combined seam gate is invalid");
    }
    for (std::size_t route_index = 0U; route_index < model.route_directions.size();
         ++route_index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto bus_index = route_index / 2U;
        const auto expected_direction = route_index % 2U == 0U
                                            ? DirectionalSweepDirection::rising
                                            : DirectionalSweepDirection::falling;
        const auto &route = model.route_directions[route_index];
        if (route.bus_id != model.selected_bus_ids[bus_index] ||
            route.direction != expected_direction || route.cells.empty() ||
            route.identity_sha256.is_zero()) {
            return error(ErrorCode::topology_mismatch,
                         "responsive-child-directional-route-invalid",
                         "/children/directional/routes/" + std::to_string(route_index),
                         "directional route order or identity is invalid");
        }
        std::map<double, std::vector<double>> loads_by_rpm;
        std::set<std::pair<double, std::string>> coordinates;
        for (std::size_t cell_index = 0U; cell_index < route.cells.size();
             ++cell_index) {
            const auto &cell = route.cells[cell_index];
            const auto source = std::span<const float>{cell.source};
            const auto closed = std::span<const float>{cell.seam_closed};
            if (!is_anchor(profile, cell.rpm) || cell.direction != expected_direction ||
                !same_lanes(profile, cell.lane_id, cell.capture_throttle_01) ||
                !coordinates.emplace(cell.rpm, cell.lane_id).second ||
                source.size() !=
                    kDirectionalSamplesPerCycle * kDirectionalSourceCycleCount ||
                closed.size() != source.size() || !finite_samples(source) ||
                !finite_samples(closed) ||
                directional_float32_payload_identity(source) !=
                    cell.source_payload_sha256 ||
                directional_float32_payload_identity(closed) !=
                    cell.seam_closed_payload_sha256 ||
                cell.identity_sha256.is_zero() ||
                cell.source_cycle_end_revolutions -
                        cell.source_cycle_begin_revolutions !=
                    kDirectionalCycleRevolutions * kDirectionalSourceCycleCount ||
                cell.source_cycle_begin_revolutions < 0.0 ||
                std::round(cell.source_cycle_begin_revolutions /
                           kDirectionalCycleRevolutions) !=
                    cell.source_cycle_begin_revolutions /
                        kDirectionalCycleRevolutions ||
                cell.telemetry.endpoint_count == 0U ||
                cell.telemetry.state_masks.empty() ||
                !finite_all(
                    {cell.crossing_revolutions, cell.source_cycle_begin_revolutions,
                     cell.source_cycle_end_revolutions,
                     cell.telemetry.mean_manifold_pressure_pa_abs,
                     cell.telemetry.mean_engine_speed_rpm,
                     cell.telemetry.mean_requested_throttle_01,
                     cell.telemetry.mean_resolved_engine_throttle_01,
                     cell.closure.signal_rms,
                     cell.closure.source_adjacent_derivative_rms,
                     cell.closure.seam_absolute_delta,
                     cell.closure.seam_over_source_adjacent_derivative_rms,
                     cell.closure
                         .maximum_adjacent_delta_over_source_adjacent_derivative_rms,
                     cell.closure.correction_rms_over_source_rms}) ||
                !(cell.telemetry.mean_manifold_pressure_pa_abs > 0.0)) {
                return error(ErrorCode::invalid_member,
                             "responsive-child-directional-cell-invalid",
                             "/children/directional/routes/" +
                                 std::to_string(route_index) + "/cells/" +
                                 std::to_string(cell_index),
                             "directional cell coordinate, payload, evidence or "
                             "identity is invalid");
            }
            loads_by_rpm[cell.rpm].push_back(
                cell.telemetry.mean_manifold_pressure_pa_abs);
        }
        if (loads_by_rpm.size() != profile.rpm.anchors.size()) {
            return error(ErrorCode::topology_mismatch,
                         "responsive-child-directional-anchor-missing",
                         "/children/directional/routes/" + std::to_string(route_index),
                         "directional route does not cover every RPM anchor");
        }
        for (auto &[rpm, loads] : loads_by_rpm) {
            static_cast<void>(rpm);
            std::ranges::sort(loads);
            if (std::adjacent_find(loads.begin(), loads.end()) != loads.end()) {
                return error(ErrorCode::topology_mismatch,
                             "responsive-child-directional-load-duplicate",
                             "/children/directional/routes/" +
                                 std::to_string(route_index),
                             "directional retained load coordinates are not unique");
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool write_audio(JsonWriter &writer,
                               const std::optional<std::string_view> bus_id) {
    bool ok = writer.key("audio") && writer.begin_object();
    ok = ok && uint_field(writer, "sample_rate_hz", kResponsiveRuntimeSampleRateHzV1);
    ok = ok && field(writer, "encoding", "float32le");
    ok = ok && field(writer, "channel_layout", "mono");
    if (bus_id.has_value()) {
        ok = ok && field(writer, "bus_id", *bus_id);
    }
    return ok && writer.end_object();
}

[[nodiscard]] bool write_load_lanes(JsonWriter &writer,
                                    const ResponsiveBakeProfile &profile) {
    bool ok = writer.key("load_lanes") && writer.begin_array();
    for (const auto &lane : profile.capture.load_lanes) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "id", lane.id);
        ok = ok && field(writer, "throttle01", lane.throttle_01);
        ok = ok && writer.end_object();
    }
    return ok && writer.end_array();
}

[[nodiscard]] bool
write_provenance_identities(JsonWriter &writer,
                            const ResponsivePackageProvenanceV1 &provenance) {
    bool ok = writer.key("engine") && identity_object(writer, provenance, true);
    ok = ok && writer.key("renderer_build") &&
         identity_object(writer, provenance, false);
    return ok;
}

[[nodiscard]] bool
write_held_route_cell(JsonWriter &writer, const HeldCookedCell &cell,
                      const HeldCookedRoute &route, const std::string_view route_token,
                      const std::size_t route_index,
                      std::vector<PortableResponsivePackageMember> &members) {
    const auto mean_name = "h-" + short_digest(route.mean_payload_sha256) + "-" +
                           short_digest(cell.identity_sha256).substr(0U, 10U) + "-r" +
                           std::to_string(route_index) + ".mean.f32le";
    const auto residual_name = "h-" + short_digest(route.residual_payload_sha256) +
                               "-" +
                               short_digest(cell.identity_sha256).substr(0U, 10U) +
                               "-r" + std::to_string(route_index) + ".residuals.f32le";
    const auto mean_relative_path = "audio/" + mean_name;
    const auto residual_relative_path = "audio/" + residual_name;
    members.push_back(
        {"held/" + mean_relative_path, float32_le_bytes(route.texture.mean)});
    members.push_back(
        {"held/" + residual_relative_path, float32_le_bytes(route.texture.residuals)});

    const auto &metrics = route.texture.metrics;
    const double tapered_residual_rms =
        metrics.source_rms * metrics.tapered_residual_rms_over_source_rms;
    bool ok = writer.begin_object();
    ok = ok && field(writer, "id", cell.id + "-" + std::string{route_token});
    ok = ok && field(writer, "rpm", cell.rpm);
    ok = ok && field(writer, "lane", cell.lane_id);
    ok = ok && field(writer, "capture_throttle_01", cell.throttle_01);
    ok = ok && writer.key("capture_provenance") && writer.begin_object();
    ok = ok && string_array(writer, "coalesced_authored_lanes",
                            cell.coalesced_authored_lanes);
    ok = ok && number_array(writer, "coalesced_capture_throttles_01",
                            cell.coalesced_capture_throttles_01);
    ok = ok && writer.end_object();
    ok = ok && field(writer, "manifold_pressure_pa_abs",
                     route.telemetry.mean_manifold_pressure_pa_abs);
    ok = ok && field(writer, "source_cycle_begin_revolutions",
                     route.source_interval.start_revolutions);
    ok = ok && field(writer, "source_cycle_end_revolutions",
                     route.source_interval.end_revolutions);
    ok = ok && writer.key("mean") && writer.begin_object();
    ok = ok && field(writer, "relative_path", mean_relative_path);
    ok = ok && uint_field(writer, "sample_count", kHeldSamplesPerCycle);
    ok = ok &&
         uint_field(writer, "byte_count", members[members.size() - 2U].bytes.size());
    ok = ok && digest_field(writer, "payload_sha256", route.mean_payload_sha256);
    ok = ok && writer.end_object();
    ok = ok && writer.key("residual_bank") && writer.begin_object();
    ok = ok && field(writer, "relative_path", residual_relative_path);
    ok = ok && uint_field(writer, "cycle_count", kHeldCapturedCycleCount);
    ok = ok && uint_field(writer, "samples_per_cycle", kHeldSamplesPerCycle);
    ok = ok && uint_field(writer, "sample_count", kHeldNormalizedSampleCount);
    ok = ok && uint_field(writer, "byte_count", members.back().bytes.size());
    ok = ok && digest_field(writer, "payload_sha256", route.residual_payload_sha256);
    ok = ok && field(writer, "selection_contract",
                     "change residual ordinal only at a 720-degree boundary");
    ok = ok && writer.end_object();
    ok = ok && writer.key("capture_fidelity") && writer.begin_object();
    ok = ok && field(writer, "mean_manifold_pressure_pa_abs",
                     route.telemetry.mean_manifold_pressure_pa_abs);
    ok = ok && field(writer, "rpm_error_rms", route.telemetry.rpm_error_rms);
    ok = ok && field(writer, "maximum_absolute_rpm_error",
                     route.telemetry.maximum_absolute_rpm_error);
    ok = ok && uint_field(writer, "telemetry_endpoint_count",
                          route.telemetry.telemetry_endpoint_count);
    ok = ok && writer.end_object();
    ok = ok && writer.key("decomposition_metrics") && writer.begin_object();
    ok = ok && field(writer, "source_rms", metrics.source_rms);
    ok = ok && field(writer, "mean_rms", metrics.mean_rms);
    ok = ok && field(writer, "raw_residual_rms_over_source_rms",
                     metrics.raw_residual_rms_over_source_rms);
    ok = ok && field(writer, "tapered_residual_rms_over_source_rms",
                     metrics.tapered_residual_rms_over_source_rms);
    ok = ok && field(writer, "reconstruction_error_rms_over_source_rms",
                     metrics.reconstruction_error_rms_over_source_rms);
    ok = ok &&
         field(writer, "mean_seam_absolute_delta", metrics.mean_seam_absolute_delta);
    ok = ok && field(writer, "mean_seam_over_mean_derivative_rms",
                     metrics.mean_seam_over_mean_derivative_rms);
    ok = ok && field(writer, "mean_closure_correction_absolute",
                     metrics.mean_closure_correction_absolute);
    ok = ok && field(writer, "mean_closure_correction_over_source_rms",
                     metrics.mean_closure_correction_over_source_rms);
    ok = ok && field(writer, "maximum_residual_boundary_magnitude",
                     metrics.maximum_residual_boundary_magnitude);
    ok = ok && uint_field(writer, "residual_taper_frames_per_edge",
                          metrics.residual_taper_frames_per_edge);
    ok = ok && field(writer, "tapered_residual_rms", tapered_residual_rms);
    ok = ok && field(writer, "tapered_residual_power",
                     tapered_residual_rms * tapered_residual_rms);
    ok = ok && writer.end_object();
    return ok && writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_held_route(const ResponsivePackageChildrenViewV1 &input,
                  const std::size_t route_index,
                  std::vector<PortableResponsivePackageMember> &members,
                  const std::stop_token stop_token) {
    const auto &profile = *input.profile;
    const auto &held = *input.held;
    const auto &bus = input.directional->selected_bus_ids[route_index];
    const auto route_token = portable_token(bus);
    JsonWriter writer;
    bool ok = writer.begin_object();
    ok =
        ok && field(writer, "schema", "crankwave/responsive-audio-held-route");
    ok = ok && field(writer, "id",
                     input.provenance.engine_id + "-exact-held-" + route_token +
                         "-live-preview");
    ok = ok && field(writer, "engine", input.provenance.engine_id);
    ok = ok && write_audio(writer, bus);
    ok = ok && writer.key("phase") && writer.begin_object();
    ok = ok && field(writer, "cycle_revolutions", kHeldCycleRevolutions);
    ok = ok && uint_field(writer, "samples_per_cycle", kHeldSamplesPerCycle);
    ok = ok && uint_field(writer, "residual_cycle_count", kHeldCapturedCycleCount);
    ok = ok && field(writer, "residual_boundary_value", 0.0);
    ok = ok && writer.key("residual_taper") && writer.begin_object();
    ok = ok && field(writer, "shape", "smoothstep");
    ok = ok && field(writer, "fraction_per_edge", kHeldResidualTaperFractionPerEdge);
    ok = ok &&
         uint_field(writer, "frames_per_edge",
                    static_cast<std::uint64_t>(std::llround(
                        kHeldSamplesPerCycle * kHeldResidualTaperFractionPerEdge)));
    ok = ok && writer.end_object() && writer.end_object();
    ok = ok && writer.key("domain") && writer.begin_object();
    ok = ok && number_array(writer, "rpm_anchors", profile.rpm.anchors);
    ok = ok &&
         field(writer, "load_coordinate", "measured-intake-manifold-pressure-pa-abs");
    ok = ok && write_load_lanes(writer, profile);
    ok = ok && writer.end_object();
    ok = ok && writer.key("cells") && writer.begin_array();
    for (const auto &cell : held.cells) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        ok = ok && write_held_route_cell(writer, cell, cell.routes[route_index],
                                         route_token, route_index, members);
    }
    ok = ok && writer.end_array();
    ok = ok && writer.key("provenance") && writer.begin_object();
    ok = ok && write_provenance_identities(writer, input.provenance);
    ok = ok && field(writer, "capture_method", kHeldCaptureMethodId);
    ok = ok && field(writer, "decomposition_method", kHeldDecompositionMethodId);
    ok = ok && digest_field(writer, "capture_identity_sha256", held.identity_sha256);
    ok = ok && writer.end_object();
    ok = ok && writer.end_object();
    static_cast<void>(ok);
    return writer.finish("held/" + route_token + ".json");
}

[[nodiscard]] std::string
transfer_filename(const ResponsiveCompiledTransfer &transfer) {
    return "ir-" + short_digest(transfer.spectrum_sha256) + "-" +
           short_digest(transfer.identity_sha256).substr(0U, 8U) +
           (transfer.shape == ResponsiveTransferShape::fixed_overlap_save
                ? "-kernel-complex-f64le.bin"
                : "-partitioned-kernels-complex-f64le.bin");
}

[[nodiscard]] bool write_transfer(JsonWriter &writer,
                                  const ResponsiveCompiledTransfer &transfer) {
    bool ok = writer.begin_object();
    if (transfer.shape == ResponsiveTransferShape::uniform_partitioned_overlap_save) {
        ok = ok && field(writer, "kind", kResponsivePartitionedTransferKind);
    }
    ok = ok && uint_field(writer, "fft_size", transfer.fft_size);
    ok = ok && uint_field(writer, "coefficient_count", transfer.coefficient_count);
    if (transfer.shape == ResponsiveTransferShape::uniform_partitioned_overlap_save) {
        ok = ok && uint_field(writer, "partition_frame_count",
                              transfer.partition_frame_count);
        ok = ok && uint_field(writer, "partition_count", transfer.partition_count);
    }
    ok = ok && field(writer, "spectrum_encoding", transfer.spectrum_encoding);
    ok = ok && field(writer, "spectrum_path", transfer_filename(transfer));
    ok =
        ok && uint_field(writer, "spectrum_byte_count", transfer.spectrum_bytes.size());
    ok = ok && digest_field(writer, "spectrum_sha256", transfer.spectrum_sha256);
    return ok && writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_held_root(const ResponsivePackageChildrenViewV1 &input,
                 const std::vector<std::string> &route_manifest_names,
                 std::vector<PortableResponsivePackageMember> &members,
                 const std::stop_token stop_token) {
    const auto &profile = *input.profile;
    const auto &held = *input.held;
    const auto &presentation = *input.presentation;
    const auto &buses = input.directional->selected_bus_ids;
    for (const auto &transfer : presentation.transfers) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        members.push_back(
            {"held/" + transfer_filename(transfer), transfer.spectrum_bytes});
    }

    JsonWriter writer;
    bool ok = writer.begin_object();
    ok = ok &&
         field(writer, "schema", "crankwave/responsive-audio-held-texture");
    ok = ok &&
         field(writer, "id",
               input.provenance.engine_id + "-exact-held-phase-texture-live-preview");
    ok = ok && field(writer, "engine", input.provenance.engine_id);
    ok = ok && writer.key("fidelity") && writer.begin_object();
    ok = ok && field(writer, "purpose",
                     "interactive-source-b-versus-source-a-audition-preview");
    ok = ok && uint_field(writer, "physics_rate_hz", profile.capture.physics_rate_hz);
    ok = ok &&
         bool_field(writer, "canonical_offline_bake", input.canonical_offline_bake);
    ok = ok && writer.end_object();
    ok = ok && writer.key("representation") && writer.begin_object();
    ok = ok &&
         field(writer, "kind", "cyclic-mean-plus-boundary-zero-cycle-residual-bank");
    ok = ok && bool_field(writer, "timeline_included", false);
    constexpr std::array runtime_coordinates{"rpm",
                                             "measured-intake-manifold-pressure-pa-abs",
                                             "unwrapped-crank-revolutions"};
    ok = ok && string_array(writer, "runtime_coordinates", runtime_coordinates);
    constexpr std::array internal_state{"residual-cycle-ordinal"};
    ok = ok && string_array(writer, "internal_runtime_state", internal_state);
    ok = ok && writer.end_object();
    ok = ok && writer.key("domain") && writer.begin_object();
    ok = ok && field(writer, "minimum_rpm", profile.rpm.anchors.front());
    ok = ok && field(writer, "maximum_rpm", profile.rpm.anchors.back());
    ok = ok && number_array(writer, "rpm_anchors", profile.rpm.anchors);
    ok = ok && write_load_lanes(writer, profile);
    ok = ok && uint_field(writer, "operating_cell_count", held.cells.size());
    ok = ok && writer.end_object();
    ok = ok && string_array(writer, "dry_bus_ids", buses);
    ok = ok && writer.key("route_manifests") && writer.begin_array();
    for (std::size_t index = 0U; index < buses.size(); ++index) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "bus_id", buses[index]);
        ok = ok && field(writer, "manifest_path", route_manifest_names[index]);
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array();
    ok = ok && writer.key("presentation") && writer.begin_object();
    ok = ok && field(writer, "audition_bus_id", presentation.audition_bus_id);
    ok = ok && string_array(writer, "audition_dry_bus_order",
                            presentation.audition_dry_bus_order);
    ok = ok && field(writer, "captured_to_source_scale",
                     presentation.captured_to_source_scale);
    ok = ok && field(writer, "master_volume_linear", presentation.master_volume_linear);
    ok = ok && writer.key("routes") && writer.begin_array();
    for (const auto &route : presentation.routes) {
        const auto &transfer = presentation.transfers[route.transfer_index];
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "dry_bus_id", route.dry_bus_id);
        ok = ok && field(writer, "source_route_id", route.source_route_id);
        ok = ok && field(writer, "impulse_response_asset_id",
                         route.impulse_response_asset_id);
        ok = ok && digest_field(writer, "impulse_response_payload_sha256",
                                route.impulse_response_payload_sha256);
        ok = ok && field(writer, "impulse_response_gain_linear",
                         route.impulse_response_gain_linear);
        ok = ok && field(writer, "wet_mix_01", route.wet_mix_01);
        ok = ok && writer.key("transfer") && write_transfer(writer, transfer);
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array() && writer.end_object();
    ok = ok && writer.key("phase_alignment") && writer.begin_object();
    ok = ok && field(writer, "method", held.phase_alignment.method);
    ok = ok &&
         field(writer, "reference_cell_id", held.phase_alignment.reference_cell_id);
    ok = ok && field(writer, "unit", "phase-samples");
    ok = ok && field(writer, "interpolation", "unwrapped-linear");
    ok = ok && writer.key("cells") && writer.begin_array();
    for (const auto &shift : held.phase_alignment.cells) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "id", shift.cell_id);
        ok = ok && field(writer, "shift_to_canonical_samples",
                         shift.shift_to_canonical_samples);
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array() && writer.end_object();
    ok = ok && writer.key("texture_selection") && writer.begin_object();
    ok = ok && field(writer, "algorithm", "splitmix64-shuffled-bags-v1");
    ok = ok && uint_field(writer, "bank_size", kHeldCapturedCycleCount);
    ok = ok && field(writer, "public_seed", std::to_string(kHeldPublicSeed));
    ok = ok && bool_field(writer, "no_adjacent_repeat", true);
    ok = ok && field(writer, "change_phase", "720-degree-boundary");
    ok = ok && writer.end_object();
    ok = ok && writer.key("interpolation") && writer.begin_object();
    ok = ok && writer.key("mean") && writer.begin_object();
    ok = ok && field(writer, "method", "common-delay-phase-warp");
    ok = ok && field(writer, "energy_target", "linear-anchor-rms");
    ok = ok && writer.end_object();
    ok = ok && writer.key("residual") && writer.begin_object();
    ok = ok && field(writer, "cross_cell_correlation", "independent");
    ok = ok && field(writer, "energy_target", "linear-anchor-power");
    ok = ok && field(writer, "normalization", "sqrt(Ptarget/sum(w_i^2 P_i))");
    ok = ok && writer.end_object() && writer.end_object();
    ok = ok && writer.key("provenance") && writer.begin_object();
    ok = ok && write_provenance_identities(writer, input.provenance);
    ok = ok && writer.key("capture_identity") && writer.begin_object();
    ok = ok && field(writer, "engine_id", input.provenance.engine_id);
    ok = ok && field(writer, "capture_method", kHeldCaptureMethodId);
    ok = ok && field(writer, "decomposition_method", kHeldDecompositionMethodId);
    ok = ok && writer.key("state_capture_identities") && writer.begin_array();
    for (const auto &cell : held.cells) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "rpm", cell.rpm);
        ok = ok && digest_field(writer, "sha256", cell.identity_sha256);
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array();
    ok = ok && digest_field(writer, "sha256", held.identity_sha256);
    ok = ok && writer.end_object() && writer.end_object() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish(kResponsiveHeldPackagePathV1);
}

[[nodiscard]] std::string_view
direction_name(const DirectionalSweepDirection direction) {
    return direction == DirectionalSweepDirection::rising ? "rising" : "falling";
}

[[nodiscard]] bool
write_directional_cell(JsonWriter &writer, const DirectionalCookedCell &cell,
                       const std::size_t route_index,
                       std::vector<PortableResponsivePackageMember> &members) {
    const auto filename = "d-" + short_digest(cell.seam_closed_payload_sha256) + "-" +
                          short_digest(cell.identity_sha256).substr(0U, 10U) + "-r" +
                          std::to_string(route_index) + ".phase.f32le";
    const auto relative_path = "audio/" + filename;
    members.push_back(
        {"directional/" + relative_path, float32_le_bytes(cell.seam_closed)});
    bool ok = writer.begin_object();
    ok = ok && field(writer, "id", cell.id);
    ok = ok && field(writer, "rpm", cell.rpm);
    ok = ok && field(writer, "lane", cell.lane_id);
    ok = ok && field(writer, "capture_throttle_01", cell.capture_throttle_01);
    ok = ok && field(writer, "manifold_pressure_pa_abs",
                     cell.telemetry.mean_manifold_pressure_pa_abs);
    ok = ok && field(writer, "relative_path", relative_path);
    ok = ok && uint_field(writer, "byte_count", members.back().bytes.size());
    ok = ok && digest_field(writer, "payload_sha256", cell.seam_closed_payload_sha256);
    ok = ok && digest_field(writer, "source_capture_payload_sha256",
                            cell.source_payload_sha256);
    ok = ok && field(writer, "source_cycle_origin_revolutions", 0.0);
    ok = ok && field(writer, "source_cycle_begin_revolutions",
                     cell.source_cycle_begin_revolutions);
    ok = ok && field(writer, "source_cycle_end_revolutions",
                     cell.source_cycle_end_revolutions);
    ok = ok && field(writer, "loop_join_normalized_error",
                     cell.closure.seam_over_source_adjacent_derivative_rms);
    ok = ok && field(writer, "seam_closure_algorithm", kDirectionalSeamAlgorithmId);
    ok = ok && writer.key("capture_fidelity") && writer.begin_object();
    ok = ok && field(writer, "mean_manifold_pressure_pa_abs",
                     cell.telemetry.mean_manifold_pressure_pa_abs);
    ok = ok && field(writer, "mean_rpm", cell.telemetry.mean_engine_speed_rpm);
    ok = ok && field(writer, "mean_requested_throttle_01",
                     cell.telemetry.mean_requested_throttle_01);
    ok = ok && field(writer, "mean_resolved_throttle_01",
                     cell.telemetry.mean_resolved_engine_throttle_01);
    ok = ok && writer.key("state_masks") && writer.begin_array();
    for (const auto state_mask : cell.telemetry.state_masks) {
        ok = ok && writer.uint_value(state_mask);
    }
    ok = ok && writer.end_array();
    ok = ok &&
         uint_field(writer, "telemetry_endpoint_count", cell.telemetry.endpoint_count);
    ok = ok && writer.end_object();
    return ok && writer.end_object();
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_directional_route(const ResponsivePackageChildrenViewV1 &input,
                         const std::size_t route_direction_index,
                         std::vector<PortableResponsivePackageMember> &members,
                         const std::stop_token stop_token) {
    const auto &profile = *input.profile;
    const auto &model = *input.directional;
    const auto &route = model.route_directions[route_direction_index];
    const auto direction = direction_name(route.direction);
    const auto token = portable_token(route.bus_id);
    JsonWriter writer;
    bool ok = writer.begin_object();
    ok = ok && field(writer, "schema",
                     "crankwave/responsive-audio-directional-route");
    ok = ok && field(writer, "id",
                     input.provenance.engine_id + "-directional-" +
                         std::string{direction} + "-" + token + "-live-preview");
    ok = ok && field(writer, "engine", input.provenance.engine_id);
    ok = ok && write_audio(writer, route.bus_id);
    ok = ok && writer.key("phase") && writer.begin_object();
    ok = ok && field(writer, "cycle_revolutions", kDirectionalCycleRevolutions);
    ok = ok && uint_field(writer, "samples_per_cycle", kDirectionalSamplesPerCycle);
    ok = ok && uint_field(writer, "cycle_count", kDirectionalSourceCycleCount);
    ok = ok && field(writer, "source_cycle_origin_revolutions", 0.0);
    ok = ok && writer.end_object();
    ok = ok && writer.key("domain") && writer.begin_object();
    ok = ok && field(writer, "minimum_rpm", model.outer_minimum_rpm);
    ok = ok && field(writer, "maximum_rpm", model.outer_maximum_rpm);
    ok = ok && number_array(writer, "rpm_anchors", model.rpm_anchors);
    ok = ok &&
         field(writer, "load_coordinate", "measured-intake-manifold-pressure-pa-abs");
    ok = ok && write_load_lanes(writer, profile);
    ok = ok && writer.end_object();
    ok = ok && writer.key("seam_closure") && writer.begin_object();
    ok = ok && field(writer, "algorithm", kDirectionalSeamAlgorithmId);
    ok = ok && bool_field(writer, "phase_aligned", true);
    ok = ok && field(writer, "blend_cycle_fraction_per_side", 0.125);
    ok = ok && uint_field(writer, "source_cycle_count", kDirectionalSourceCycleCount);
    ok = ok && field(writer, "source_cycle_origin_revolutions", 0.0);
    ok = ok && writer.end_object();
    ok = ok && writer.key("load_cell_coalescing") && writer.begin_object();
    ok = ok && field(writer, "policy", kDirectionalLoadCoalescingMethodId);
    ok = ok && writer.key("aliases") && writer.begin_array();
    for (const auto &alias : route.coalesced_load_aliases) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "route_id", alias.bus_id);
        ok = ok && field(writer, "direction", direction_name(alias.direction));
        ok = ok && field(writer, "rpm", alias.rpm);
        ok = ok &&
             field(writer, "manifold_pressure_pa_abs", alias.manifold_pressure_pa_abs);
        ok = ok && field(writer, "retained_lane", alias.retained_lane_id);
        ok = ok && field(writer, "coalesced_lane", alias.coalesced_lane_id);
        ok = ok && digest_field(writer, "source_capture_payload_sha256",
                                alias.source_payload_sha256);
        ok = ok && digest_field(writer, "seam_closed_payload_sha256",
                                alias.seam_closed_payload_sha256);
        ok = ok &&
             field(writer, "reason",
                   "identical-coordinate-and-byte-identical-source-and-closed-payload");
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array() && writer.end_object();
    ok = ok && writer.key("cells") && writer.begin_array();
    for (const auto &cell : route.cells) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        ok = ok && write_directional_cell(writer, cell, route_direction_index, members);
    }
    ok = ok && writer.end_array();
    ok = ok && writer.key("provenance") && writer.begin_object();
    ok = ok && write_provenance_identities(writer, input.provenance);
    ok = ok && field(writer, "representation",
                     std::string{direction} + "-prescribed-exponential-dry-route-three-"
                                              "cycle-phase-units-with-seam-closure");
    ok = ok && field(writer, "capture_method", kDirectionalCaptureMethodId);
    ok = ok && digest_field(writer, "capture_identity_sha256", model.identity_sha256);
    ok = ok && uint_field(writer, "physics_rate_hz", profile.capture.physics_rate_hz);
    ok = ok && uint_field(writer, "source_delivery_rate_hz",
                          kResponsiveRuntimeSampleRateHzV1);
    ok = ok && writer.end_object() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish("directional/" + token + "-" + std::string{direction} +
                         ".json");
}

[[nodiscard]] std::variant<std::vector<std::byte>, Error>
encode_directional_root(const ResponsivePackageChildrenViewV1 &input,
                        const std::vector<std::array<std::string, 2U>> &paths) {
    const auto &model = *input.directional;
    const auto &gate = model.combined_seam_gate;
    JsonWriter writer;
    bool ok = writer.begin_object();
    ok = ok && field(writer, "schema",
                     "crankwave/responsive-audio-directional-texture");
    ok =
        ok && field(writer, "id",
                    input.provenance.engine_id + "-directional-transient-live-preview");
    ok = ok && field(writer, "engine", input.provenance.engine_id);
    ok = ok && write_audio(writer, std::nullopt);
    ok = ok && writer.key("domain") && writer.begin_object();
    ok = ok && field(writer, "minimum_rpm", model.outer_minimum_rpm);
    ok = ok && field(writer, "maximum_rpm", model.outer_maximum_rpm);
    ok = ok && number_array(writer, "rpm_anchors", model.rpm_anchors);
    ok = ok &&
         field(writer, "load_coordinate", "measured-intake-manifold-pressure-pa-abs");
    ok = ok && writer.end_object();
    ok = ok && string_array(writer, "dry_bus_ids", model.selected_bus_ids);
    ok = ok && writer.key("route_manifests") && writer.begin_array();
    for (std::size_t index = 0U; index < model.selected_bus_ids.size(); ++index) {
        ok = ok && writer.begin_object();
        ok = ok && field(writer, "bus_id", model.selected_bus_ids[index]);
        ok = ok && field(writer, "rising_manifest_path", paths[index][0]);
        ok = ok && field(writer, "falling_manifest_path", paths[index][1]);
        ok = ok && writer.end_object();
    }
    ok = ok && writer.end_array();
    ok = ok && writer.key("provenance") && writer.begin_object();
    ok = ok && write_provenance_identities(writer, input.provenance);
    ok = ok && field(writer, "representation",
                     "live-rpm-map-crank-indexed-n-route-dry-directional-units-with-"
                     "phase-aligned-seam-closure");
    ok = ok && field(writer, "capture_method", kDirectionalCaptureMethodId);
    ok = ok && digest_field(writer, "capture_identity_sha256", model.identity_sha256);
    ok = ok && writer.end_object();
    ok = ok && writer.key("seam_closure") && writer.begin_object();
    ok = ok && field(writer, "algorithm", kDirectionalSeamAlgorithmId);
    ok = ok && bool_field(writer, "phase_aligned", true);
    ok = ok && field(writer, "blend_cycle_fraction_per_side", 0.125);
    ok = ok && field(writer, "source_cycle_origin_revolutions", 0.0);
    ok = ok && writer.key("join_gate") && writer.begin_object();
    ok = ok && field(writer, "maximum_seam_over_source_adjacent_derivative_rms",
                     gate.maximum_seam_over_source_adjacent_derivative_rms);
    ok = ok && field(writer, "maximum_correction_rms_over_source_rms",
                     gate.maximum_correction_rms_over_source_rms);
    ok =
        ok && field(writer, "observed_maximum_seam_over_source_adjacent_derivative_rms",
                    gate.observed_maximum_seam_over_source_adjacent_derivative_rms);
    ok = ok && field(writer, "observed_maximum_correction_rms_over_source_rms",
                     gate.observed_maximum_correction_rms_over_source_rms);
    ok = ok && bool_field(writer, "passed", gate.passed);
    ok = ok && writer.end_object() && writer.end_object() && writer.end_object();
    static_cast<void>(ok);
    return writer.finish(kResponsiveDirectionalPackagePathV1);
}

[[nodiscard]] std::string_view bytes_view(const std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return {};
    }
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] const PortableResponsivePackageMember *
find_member(const std::span<const PortableResponsivePackageMember> members,
            const std::string_view path) {
    const auto found =
        std::ranges::find(members, path, &PortableResponsivePackageMember::path);
    return found == members.end() ? nullptr : &*found;
}

[[nodiscard]] bool json_string_equals(const authoring::JsonValue value,
                                      const std::string_view expected) {
    const auto parsed = value.string();
    return parsed.has_value() && *parsed == expected;
}

[[nodiscard]] bool json_digest_equals(const authoring::JsonValue value,
                                      const contract::Sha256Digest &expected) {
    return json_string_equals(value, digest_hex(expected));
}

[[nodiscard]] bool
json_string_array_equals(const authoring::JsonValue value,
                         const std::span<const std::string> expected) {
    if (value.kind() != authoring::JsonKind::array || value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (!json_string_equals(value.at(index), expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool json_exact_keys(const authoring::JsonValue value,
                                   const std::initializer_list<std::string_view> keys) {
    return value.kind() == authoring::JsonKind::object && value.size() == keys.size() &&
           std::ranges::all_of(keys,
                               [&](const auto key) { return value.find(key).valid(); });
}

[[nodiscard]] std::optional<std::uint64_t> json_uint(const authoring::JsonValue value) {
    const auto number = value.number();
    if (!number.has_value() || *number < 0.0 ||
        *number > static_cast<double>(contract::kMaximumResolvedFrameIndex) ||
        std::floor(*number) != *number) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*number);
}

[[nodiscard]] std::optional<Error>
validate_lifecycle_child_closure(const ResponsiveOptionalChildPackageV1 &child,
                                 const authoring::JsonValue root,
                                 const contract::Sha256Digest &held_manifest_sha256,
                                 const std::stop_token stop_token) {
    const bool has_elevated = root.find("shutdown_elevated").valid();
    if (!child.dry_bus_ids.empty() ||
        !json_exact_keys(
            root,
            has_elevated
                ? std::initializer_list<std::string_view>{"schema", "id", "engine",
                                                          "audio", "starter", "startup",
                                                          "shutdown",
                                                          "shutdown_elevated",
                                                          "startup_admission",
                                                          "provenance"}
                : std::initializer_list<std::string_view>{
                      "schema", "id", "engine", "audio", "starter", "startup",
                      "shutdown", "startup_admission", "provenance"})) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-lifecycle-root-shape-invalid", child.runtime_path,
                     "lifecycle child root has unexpected fields or dry routes");
    }
    const auto audio = root.find("audio");
    if (!json_exact_keys(audio,
                         {"sample_rate_hz", "encoding", "channel_layout", "bus_id"}) ||
        json_uint(audio.find("sample_rate_hz")) != kResponsiveRuntimeSampleRateHzV1 ||
        !json_string_equals(audio.find("encoding"), "float32le") ||
        !json_string_equals(audio.find("channel_layout"), "mono") ||
        !json_string_equals(audio.find("bus_id"), "master.engine.audition")) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-lifecycle-audio-format-invalid",
                     child.runtime_path + "/audio",
                     "lifecycle child audio format differs from playback");
    }
    struct RolePaths {
        std::string_view runtime_role;
        std::string_view stem;
        std::string_view runtime_member;
    };
    constexpr std::array<RolePaths, 4U> roles{{
        {"starter", "starter", "starter"},
        {"startup", "startup", "startup"},
        {"shutdown", "shutdown", "shutdown"},
        {"shutdown-elevated", "shutdown-high-rpm", "shutdown_elevated"},
    }};
    const auto role_count = has_elevated ? 4U : 3U;
    const auto scenarios = root.find("provenance").find("scenarios");
    if (scenarios.kind() != authoring::JsonKind::array ||
        scenarios.size() != role_count ||
        child.members.size() != role_count * 3U + 2U) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-lifecycle-publication-count-invalid",
                     child.runtime_path,
                     "lifecycle child does not contain exactly one closed artifact, "
                     "scenario and evidence set per runtime role");
    }
    for (std::size_t index = 0U; index < role_count; ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &role = roles[index];
        const auto presentation = root.find(role.runtime_member);
        const auto artifact = presentation.find("artifact");
        const auto audio_path =
            "audio/" + std::string{role.stem} + ".master-engine-audition.f32le";
        const auto frames = json_uint(artifact.find("frame_count"));
        const auto *audio_member =
            find_member(child.members, "lifecycle/" + audio_path);
        if (!json_exact_keys(artifact, {"path", "sha256", "frame_count"}) ||
            !json_string_equals(artifact.find("path"), audio_path) ||
            !frames.has_value() || *frames < 2U ||
            *frames > kNativeResponsiveMaximumPackagePayloadBytes / sizeof(float) ||
            audio_member == nullptr ||
            audio_member->bytes.size() != *frames * sizeof(float) ||
            !json_digest_equals(artifact.find("sha256"),
                                contract::sha256(audio_member->bytes))) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-audio-artifact-invalid",
                         "lifecycle/" + audio_path,
                         "lifecycle audio artifact path, size or digest is invalid");
        }
        const auto scenario = scenarios.at(index);
        const auto scenario_path =
            "source/scenarios/" + std::string{role.stem} + "-10khz.json";
        const auto evidence_path = "evidence/" + std::string{role.stem} + ".json";
        const auto *scenario_member =
            find_member(child.members, "lifecycle/" + scenario_path);
        const auto *evidence_member =
            find_member(child.members, "lifecycle/" + evidence_path);
        const auto derived_id = scenario.find("derived_id").string();
        if (!json_exact_keys(scenario,
                             {"role", "canonical_source_id", "derived_id", "path",
                              "sha256", "evidence_path", "evidence_sha256"}) ||
            !json_string_equals(scenario.find("role"), role.runtime_role) ||
            !json_string_equals(scenario.find("path"), scenario_path) ||
            !json_string_equals(scenario.find("evidence_path"), evidence_path) ||
            !derived_id.has_value() || !contract::is_valid_semantic_id(*derived_id) ||
            scenario_member == nullptr || evidence_member == nullptr ||
            !json_digest_equals(scenario.find("sha256"),
                                contract::sha256(scenario_member->bytes)) ||
            !json_digest_equals(scenario.find("evidence_sha256"),
                                contract::sha256(evidence_member->bytes))) {
            return error(ErrorCode::invalid_member,
                         "responsive-lifecycle-source-evidence-binding-invalid",
                         "lifecycle/" + scenario_path,
                         "lifecycle source/evidence paths or digests are invalid");
        }
        authoring::AuthoringParseLimits scenario_limits;
        scenario_limits.json.maximum_input_bytes = kMaximumJsonBytes;
        auto parsed_scenario = authoring::parse_scenario_document(
            bytes_view(scenario_member->bytes), scenario_limits);
        const auto *scenario_document =
            std::get_if<authoring::ScenarioDocument>(&parsed_scenario);
        if (scenario_document == nullptr ||
            scenario_document->id.value != *derived_id) {
            return error(ErrorCode::malformed_child_manifest,
                         "responsive-lifecycle-source-scenario-invalid",
                         "lifecycle/" + scenario_path,
                         "lifecycle source scenario does not pass strict authoring "
                         "validation or bind its derived identity");
        }
        auto parsed_evidence = authoring::parse_json(
            bytes_view(evidence_member->bytes), {kMaximumJsonBytes, 128U, 262'144U});
        const auto *evidence_document =
            std::get_if<authoring::JsonDocument>(&parsed_evidence);
        if (evidence_document == nullptr ||
            !json_exact_keys(evidence_document->root(),
                             {"schema", "role", "physics_rate_hz", "delivery_rate_hz",
                              "scenario_id", "points", "completed_cycles"}) ||
            !json_string_equals(evidence_document->root().find("schema"),
                                kLifecycleCaptureEvidenceSchema) ||
            !json_string_equals(evidence_document->root().find("role"), role.stem) ||
            !json_string_equals(evidence_document->root().find("scenario_id"),
                                *derived_id)) {
            return error(ErrorCode::malformed_child_manifest,
                         "responsive-lifecycle-capture-evidence-invalid",
                         "lifecycle/" + evidence_path,
                         "lifecycle capture evidence shape, role or scenario binding "
                         "is invalid");
        }
    }
    const auto admission = root.find("startup_admission");
    const auto admission_binding = admission.find("evidence");
    constexpr std::string_view admission_path = "evidence/startup-admission.json";
    const auto *admission_member =
        find_member(child.members, "lifecycle/evidence/startup-admission.json");
    if (admission_member == nullptr ||
        !json_string_equals(admission_binding.find("path"), admission_path) ||
        !json_digest_equals(admission_binding.find("sha256"),
                            contract::sha256(admission_member->bytes)) ||
        !json_digest_equals(admission_binding.find("corrected_held_manifest_sha256"),
                            held_manifest_sha256)) {
        return error(ErrorCode::invalid_identity,
                     "responsive-lifecycle-admission-evidence-binding-invalid",
                     "lifecycle/evidence/startup-admission.json",
                     "startup admission evidence does not bind its exact bytes and "
                     "held manifest");
    }
    auto parsed_admission = authoring::parse_json(bytes_view(admission_member->bytes),
                                                  {kMaximumJsonBytes, 128U, 262'144U});
    const auto *admission_document =
        std::get_if<authoring::JsonDocument>(&parsed_admission);
    if (admission_document == nullptr ||
        !json_string_equals(admission_document->root().find("schema"),
                            kStartupAdmissionFloorEvidenceSchema) ||
        !json_digest_equals(admission_document->root().find("atlas_manifest_sha256"),
                            held_manifest_sha256)) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-lifecycle-admission-evidence-invalid",
                     "lifecycle/evidence/startup-admission.json",
                     "startup admission evidence schema or held binding is invalid");
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view
optional_child_schema(const ResponsiveOptionalChildRole role) {
    switch (role) {
    case ResponsiveOptionalChildRole::motoring:
        return "crankwave/responsive-audio-state-phase-texture";
    case ResponsiveOptionalChildRole::lifecycle:
        return "crankwave/responsive-audio-lifecycle";
    case ResponsiveOptionalChildRole::shared_recorded_starter:
        return "crankwave/shared-recorded-starter";
    }
    return {};
}

[[nodiscard]] std::string_view
optional_child_path(const ResponsiveOptionalChildRole role) {
    switch (role) {
    case ResponsiveOptionalChildRole::motoring:
        return "motoring/runtime.json";
    case ResponsiveOptionalChildRole::lifecycle:
        return "lifecycle/runtime.json";
    case ResponsiveOptionalChildRole::shared_recorded_starter:
        return "shared-recorded-starter/runtime.json";
    }
    return {};
}

[[nodiscard]] std::optional<Error>
validate_optional_child(const ResponsiveOptionalChildPackageV1 &child,
                        const EncodedResponsivePackageChildrenV1 &core,
                        const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        return cancelled_error();
    }
    const auto expected_path = optional_child_path(child.role);
    if (child.runtime_path != expected_path || child.members.empty() ||
        child.engine_id != core.provenance.engine_id ||
        child.compiled_engine_sha256 != core.provenance.compiled_engine_sha256 ||
        child.renderer_build_id != core.provenance.renderer_build_id ||
        child.renderer_source_sha256 != core.provenance.renderer_source_sha256) {
        return error(
            ErrorCode::invalid_identity, "responsive-optional-child-identity-invalid",
            std::string{expected_path},
            "optional child role, root path or declared provenance is invalid");
    }
    const auto slash = child.runtime_path.rfind('/');
    if (slash == std::string::npos) {
        return error(ErrorCode::invalid_member,
                     "responsive-optional-child-root-invalid", child.runtime_path,
                     "optional child runtime must be rooted in one package directory");
    }
    const auto prefix = child.runtime_path.substr(0U, slash + 1U);
    std::set<std::string_view> paths;
    std::uint64_t total_bytes = 0U;
    for (const auto &member : child.members) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        if (!artifacts::is_portable_crankwave_path(member.path) ||
            !member.path.starts_with(prefix) || member.bytes.empty() ||
            !paths.insert(member.path).second ||
            member.bytes.size() >
                kNativeResponsiveMaximumPackagePayloadBytes - total_bytes) {
            return error(ErrorCode::invalid_member,
                         "responsive-optional-child-member-invalid", member.path,
                         "optional child members must be unique, nonempty, bounded and "
                         "remain under their role root");
        }
        total_bytes += member.bytes.size();
    }
    const auto *root_member = find_member(child.members, child.runtime_path);
    if (root_member == nullptr || root_member->bytes.size() > kMaximumJsonBytes) {
        return error(ErrorCode::missing_member,
                     "responsive-optional-child-runtime-missing", child.runtime_path,
                     "optional child runtime manifest is absent or oversized");
    }
    authoring::JsonParseLimits limits;
    limits.maximum_input_bytes = kMaximumJsonBytes;
    limits.maximum_nodes = 262'144U;
    auto parsed = authoring::parse_json(bytes_view(root_member->bytes), limits);
    const auto *document = std::get_if<authoring::JsonDocument>(&parsed);
    if (document == nullptr) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-optional-child-json-malformed", child.runtime_path,
                     "optional child runtime manifest is not bounded valid JSON");
    }
    const auto root = document->root();
    if (root.kind() != authoring::JsonKind::object ||
        !json_string_equals(root.find("schema"), optional_child_schema(child.role))) {
        return error(ErrorCode::malformed_child_manifest,
                     "responsive-optional-child-schema-mismatch", child.runtime_path,
                     "optional child runtime schema does not match its typed role");
    }
    if (child.role == ResponsiveOptionalChildRole::shared_recorded_starter) {
        const auto *audio_member =
            find_member(child.members, kSharedStarterAudioPackagePath);
        if (!child.dry_bus_ids.empty() || child.members.size() != 2U ||
            !json_string_equals(root.find("id"), "shared-recorded-starter-cc0-v1") ||
            root_member->bytes.size() != 3'560U || audio_member == nullptr ||
            audio_member->bytes.size() != 5'883'648U ||
            digest_hex(contract::sha256(root_member->bytes)) !=
                kSharedStarterManifestSha256 ||
            digest_hex(contract::sha256(audio_member->bytes)) !=
                kSharedStarterPayloadSha256) {
            return error(ErrorCode::topology_mismatch,
                         "responsive-shared-starter-contract-mismatch",
                         child.runtime_path,
                         "shared starter must be the exact two-member CC0 "
                         "package admitted by playback and have no dry routes");
        }
        return std::nullopt;
    }
    const auto provenance = root.find("provenance");
    if (!json_string_equals(root.find("engine"), child.engine_id) ||
        !json_string_equals(provenance.find("engine").find("id"), child.engine_id) ||
        !json_digest_equals(provenance.find("engine").find("sha256"),
                            child.compiled_engine_sha256) ||
        !json_string_equals(provenance.find("renderer_build").find("id"),
                            child.renderer_build_id) ||
        !json_digest_equals(provenance.find("renderer_build").find("sha256"),
                            child.renderer_source_sha256)) {
        return error(
            ErrorCode::invalid_identity,
            "responsive-optional-child-manifest-provenance-mismatch",
            child.runtime_path,
            "optional child manifest provenance differs from the core package");
    }
    if (child.role == ResponsiveOptionalChildRole::motoring) {
        if (child.dry_bus_ids != core.runtime.dry_bus_ids ||
            !json_string_array_equals(root.find("dry_bus_ids"),
                                      core.runtime.dry_bus_ids)) {
            return error(ErrorCode::topology_mismatch,
                         "responsive-motoring-child-topology-mismatch",
                         child.runtime_path,
                         "motoring child dry routes differ from the core package");
        }
    } else {
        if (!child.dry_bus_ids.empty() ||
            !json_digest_equals(root.find("startup_admission")
                                    .find("evidence")
                                    .find("corrected_held_manifest_sha256"),
                                core.held_manifest_sha256)) {
            return error(ErrorCode::invalid_identity,
                         "responsive-lifecycle-held-binding-mismatch",
                         child.runtime_path,
                         "lifecycle startup admission does not bind the exact "
                         "generated held manifest");
        }
        if (const auto failure = validate_lifecycle_child_closure(
                child, root, core.held_manifest_sha256, stop_token)) {
            return failure;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<Error>
validate_complete_member_set(std::vector<PortableResponsivePackageMember> &members,
                             const std::stop_token stop_token) {
    std::ranges::sort(members, {}, &PortableResponsivePackageMember::path);
    std::uint64_t total_bytes = 0U;
    for (std::size_t index = 0U; index < members.size(); ++index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto &member = members[index];
        if (!artifacts::is_portable_crankwave_path(member.path) ||
            member.bytes.empty() ||
            (index != 0U && members[index - 1U].path == member.path) ||
            member.bytes.size() >
                kNativeResponsiveMaximumPackagePayloadBytes - total_bytes) {
            return error(
                ErrorCode::invalid_member, "responsive-child-member-set-invalid",
                member.path,
                "generated child members are invalid, duplicated, empty or oversized");
        }
        total_bytes += member.bytes.size();
    }
    return std::nullopt;
}

[[nodiscard]] ResponsivePackageChildrenResultV1
encode_core_children(const ResponsivePackageChildrenViewV1 &input,
                     const std::stop_token stop_token) {
    if (const auto failure = validate_core(input, stop_token)) {
        return *failure;
    }
    if (const auto failure = validate_held(input, stop_token)) {
        return *failure;
    }
    if (const auto failure = validate_directional(input, stop_token)) {
        return *failure;
    }

    EncodedResponsivePackageChildrenV1 output;
    output.provenance = input.provenance;
    output.runtime.engine_id = input.provenance.engine_id;
    output.runtime.compiled_engine_provenance_sha256 =
        input.provenance.compiled_engine_sha256;
    output.runtime.physics_rate_hz = input.profile->capture.physics_rate_hz;
    output.runtime.minimum_rpm = input.directional->outer_minimum_rpm;
    output.runtime.maximum_rpm = input.directional->outer_maximum_rpm;
    output.runtime.canonical_offline_bake = input.canonical_offline_bake;
    output.runtime.audition_bus_id = input.presentation->audition_bus_id;
    output.runtime.dry_bus_ids = input.directional->selected_bus_ids;
    output.runtime.held_package_path = std::string{kResponsiveHeldPackagePathV1};
    output.runtime.directional_package_path =
        std::string{kResponsiveDirectionalPackagePathV1};

    std::vector<std::string> held_route_names;
    held_route_names.reserve(output.runtime.dry_bus_ids.size());
    for (std::size_t route_index = 0U; route_index < output.runtime.dry_bus_ids.size();
         ++route_index) {
        if (stop_token.stop_requested()) {
            return cancelled_error();
        }
        const auto name =
            portable_token(output.runtime.dry_bus_ids[route_index]) + ".json";
        auto encoded =
            encode_held_route(input, route_index, output.members, stop_token);
        if (const auto *failure = std::get_if<Error>(&encoded)) {
            return *failure;
        }
        held_route_names.push_back(name);
        output.members.push_back(
            {"held/" + name, std::get<std::vector<std::byte>>(std::move(encoded))});
    }
    auto held_root =
        encode_held_root(input, held_route_names, output.members, stop_token);
    if (const auto *failure = std::get_if<Error>(&held_root)) {
        return *failure;
    }
    auto held_root_bytes = std::get<std::vector<std::byte>>(std::move(held_root));
    output.held_manifest_sha256 = contract::sha256(held_root_bytes);
    output.members.push_back(
        {std::string{kResponsiveHeldPackagePathV1}, std::move(held_root_bytes)});

    std::vector<std::array<std::string, 2U>> directional_paths(
        output.runtime.dry_bus_ids.size());
    for (std::size_t bus_index = 0U; bus_index < output.runtime.dry_bus_ids.size();
         ++bus_index) {
        const auto token = portable_token(output.runtime.dry_bus_ids[bus_index]);
        for (std::size_t direction_index = 0U; direction_index < 2U;
             ++direction_index) {
            if (stop_token.stop_requested()) {
                return cancelled_error();
            }
            const auto route_index = bus_index * 2U + direction_index;
            const auto direction = direction_name(
                input.directional->route_directions[route_index].direction);
            const auto name = token + "-" + std::string{direction} + ".json";
            auto encoded = encode_directional_route(input, route_index, output.members,
                                                    stop_token);
            if (const auto *failure = std::get_if<Error>(&encoded)) {
                return *failure;
            }
            directional_paths[bus_index][direction_index] = name;
            output.members.push_back(
                {"directional/" + name,
                 std::get<std::vector<std::byte>>(std::move(encoded))});
        }
    }
    auto directional_root = encode_directional_root(input, directional_paths);
    if (const auto *failure = std::get_if<Error>(&directional_root)) {
        return *failure;
    }
    output.members.push_back(
        {std::string{kResponsiveDirectionalPackagePathV1},
         std::get<std::vector<std::byte>>(std::move(directional_root))});

    if (const auto failure = validate_complete_member_set(output.members, stop_token)) {
        return *failure;
    }
    return output;
}

} // namespace

ResponsivePackageChildrenResultV1
encode_responsive_package_children_v1(const ResponsivePackageChildrenViewV1 &input,
                                      const std::stop_token stop_token) {
    auto encoded = encode_core_children(input, stop_token);
    if (const auto *failure = std::get_if<Error>(&encoded)) {
        return *failure;
    }
    auto core = std::get<EncodedResponsivePackageChildrenV1>(std::move(encoded));
    if (input.optional_children.empty()) {
        return core;
    }
    return attach_responsive_optional_children_v1(std::move(core),
                                                  input.optional_children, stop_token);
}

ResponsivePackageChildrenResultV1 attach_responsive_optional_children_v1(
    EncodedResponsivePackageChildrenV1 core,
    std::vector<ResponsiveOptionalChildPackageV1> optional_children,
    const std::stop_token stop_token) {
    if (optional_children.size() > kMaximumChildCount) {
        return error(
            ErrorCode::resource_limit, "responsive-optional-child-count-exceeded",
            "/children/optional",
            "at most one motoring, lifecycle and shared starter child may be attached");
    }
    std::set<ResponsiveOptionalChildRole> roles;
    for (const auto &child : optional_children) {
        if (!roles.insert(child.role).second) {
            return error(ErrorCode::duplicate_member,
                         "responsive-optional-child-role-duplicate", child.runtime_path,
                         "an optional child role may be attached exactly once");
        }
        if (const auto failure = validate_optional_child(child, core, stop_token)) {
            return *failure;
        }
    }
    for (auto &child : optional_children) {
        switch (child.role) {
        case ResponsiveOptionalChildRole::motoring:
            core.runtime.motoring_package_path = child.runtime_path;
            break;
        case ResponsiveOptionalChildRole::lifecycle:
            core.runtime.lifecycle_package_path = child.runtime_path;
            break;
        case ResponsiveOptionalChildRole::shared_recorded_starter:
            core.runtime.shared_recorded_starter_package_path = child.runtime_path;
            break;
        }
        for (auto &member : child.members) {
            core.members.push_back(std::move(member));
        }
    }
    if (const auto failure = validate_complete_member_set(core.members, stop_token)) {
        return *failure;
    }
    return core;
}

NativeResponsiveCookedPackageBuildResultV2
build_native_responsive_package_from_encoded_v2(
    NativeResponsiveEncodedPackageInputV2 input, const std::stop_token stop_token) {
    if (input.identity.engine_id != input.children.provenance.engine_id ||
        input.identity.backend.source_closure_sha256 !=
            input.children.provenance.renderer_source_sha256) {
        return error(
            ErrorCode::invalid_identity, "native-responsive-cooked-provenance-mismatch",
            "/children/provenance",
            "package identity backend and cooked child provenance must match exactly");
    }
    input.children.runtime.renderer_compatibility =
        std::move(input.renderer_compatibility);
    const bool shared_child =
        input.children.runtime.shared_recorded_starter_package_path.has_value();
    if (shared_child != input.identity.shared_recorded_starter.has_value()) {
        return error(
            ErrorCode::invalid_identity,
            "native-responsive-cooked-starter-identity-mismatch",
            "/children/shared_recorded_starter",
            "shared starter package and bake identity must be supplied together");
    }
    const auto held_manifest_sha256 = input.children.held_manifest_sha256;
    NativeResponsivePackageInputV2 package_input{std::move(input.identity),
                                                 std::move(input.children.runtime),
                                                 std::move(input.children.members)};
    auto package =
        build_native_responsive_package_v2(std::move(package_input), stop_token);
    if (const auto *failure = std::get_if<Error>(&package)) {
        return *failure;
    }
    return NativeResponsiveCookedPackageV2{
        std::get<NativeResponsivePackageV2>(std::move(package)), held_manifest_sha256};
}

NativeResponsiveCookedPackageBuildResultV2
build_native_responsive_package_from_cooked_v2(
    NativeResponsiveCookedPackageInputV2 input, const std::stop_token stop_token) {
    auto encoded = encode_responsive_package_children_v1(input.children, stop_token);
    if (const auto *failure = std::get_if<Error>(&encoded)) {
        return *failure;
    }
    return build_native_responsive_package_from_encoded_v2(
        {std::move(input.identity),
         std::get<EncodedResponsivePackageChildrenV1>(std::move(encoded)),
         std::move(input.renderer_compatibility)},
        stop_token);
}

} // namespace crankwave::responsive
