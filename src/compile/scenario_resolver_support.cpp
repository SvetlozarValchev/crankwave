#include "compile/scenario_resolver_internal.hpp"

#include "compile/diagnostics.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine_sim_offline::compile::detail::scenario_resolution {
namespace {

constexpr std::string_view kFuelProjectionDescriptor =
    "method_id: scenario-fuel-projection-v1\n"
    "version: 1\n"
    "input: resolved engine fuel plus authored scenario fuel selection\n"
    "output: scenario fuel identity, lower heating value, and mass AFR\n"
    "arithmetic: heating value is projected exactly; mass AFR uses the admitted "
    "legacy pseudo-gas molecular-to-mass written operation\n";

class DigestPayload final {
  public:
    void byte(std::uint8_t value) {
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void u64(std::uint64_t value) {
        for (std::uint32_t index = 0; index < 8U; ++index) {
            byte(static_cast<std::uint8_t>(value >> (index * 8U)));
        }
    }

    void string(std::string_view value) {
        u64(static_cast<std::uint64_t>(value.size()));
        const auto payload =
            std::as_bytes(std::span<const char>{value.data(), value.size()});
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }

    [[nodiscard]] contract::Sha256Digest finish() const noexcept {
        return contract::sha256(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
};

} // namespace

const contract::MethodIdentity &fuel_projection_method_identity() {
    static const contract::MethodIdentity method{
        "scenario-fuel-projection-v1",
        1,
        contract::sha256(std::as_bytes(std::span<const char>{
            kFuelProjectionDescriptor.data(), kFuelProjectionDescriptor.size()})),
    };
    return method;
}

void append(authoring::DiagnosticReport &destination,
            authoring::DiagnosticReport source) {
    destination.diagnostics.insert(destination.diagnostics.end(),
                                   std::make_move_iterator(source.diagnostics.begin()),
                                   std::make_move_iterator(source.diagnostics.end()));
}

authoring::DiagnosticCode diagnostic_code(contract::ContractIssueCode code) noexcept {
    using enum authoring::DiagnosticCode;
    switch (code) {
    case contract::ContractIssueCode::missing_value:
        return missing_value;
    case contract::ContractIssueCode::invalid_value:
        return invalid_value;
    case contract::ContractIssueCode::duplicate_identity:
        return duplicate_id;
    case contract::ContractIssueCode::dangling_reference:
        return dangling_reference;
    case contract::ContractIssueCode::inconsistent_shape:
    case contract::ContractIssueCode::inconsistent_semantics:
        return inconsistent_value;
    case contract::ContractIssueCode::unsupported_value:
        return unsupported_capability;
    }
    return internal_failure;
}

std::string json_pointer_from_contract_path(std::string_view path) {
    if (path.starts_with("scenario.")) {
        path.remove_prefix(std::string_view{"scenario."}.size());
    }

    std::string pointer;
    pointer.reserve(path.size() + 1U);
    pointer.push_back('/');
    for (const char byte : path) {
        if (byte == '.' || byte == '[') {
            pointer.push_back('/');
        } else if (byte != ']') {
            pointer.push_back(byte);
        }
    }
    constexpr std::string_view resolved_value_segment = "/value";
    std::size_t value = 0;
    while ((value = pointer.find(resolved_value_segment, value)) != std::string::npos) {
        const auto after = value + resolved_value_segment.size();
        if (after == pointer.size() || pointer[after] == '/') {
            pointer.erase(value, resolved_value_segment.size());
        } else {
            value = after;
        }
    }
    return pointer == "/" ? std::string{} : pointer;
}

void append_contract_report(authoring::DiagnosticReport &destination,
                            contract::ValidationReport source,
                            std::optional<authoring::DiagnosticCode> forced_code,
                            std::string_view forced_pointer) {
    for (auto &issue : source.issues) {
        auto report = diagnostic(forced_code.value_or(diagnostic_code(issue.code)),
                                 forced_pointer.empty()
                                     ? json_pointer_from_contract_path(issue.path)
                                     : forced_pointer,
                                 std::move(issue.message));
        append(destination, std::move(report));
    }
}

bool approximately_equal(double left, double right) noexcept {
    if (left == right) {
        return true;
    }
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return false;
    }
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <=
           32.0 * std::numeric_limits<double>::epsilon() * scale;
}

double radians_per_second_to_rpm(double value) noexcept {
    return value * 30.0 / std::numbers::pi_v<double>;
}

contract::TrajectoryInterpolation
contract_interpolation(authoring::TrajectoryInterpolation value) noexcept {
    switch (value) {
    case authoring::TrajectoryInterpolation::right_continuous_hold:
        return contract::TrajectoryInterpolation::right_continuous_hold;
    case authoring::TrajectoryInterpolation::linear:
        return contract::TrajectoryInterpolation::linear;
    }
    return contract::TrajectoryInterpolation::right_continuous_hold;
}

std::string_view sample_encoding_id(contract::AudioSampleEncoding encoding) noexcept {
    switch (encoding) {
    case contract::AudioSampleEncoding::pcm_s16le:
        return "pcm_s16le";
    case contract::AudioSampleEncoding::float32le:
        return "float32le";
    case contract::AudioSampleEncoding::pcm_s24le:
        return "pcm_s24le";
    }
    return {};
}

const contract::ResolutionRecord *
find_resolution(const contract::ProvenanceLedger &provenance,
                std::string_view resolution_id) noexcept {
    const auto found = std::ranges::find(provenance.resolutions, resolution_id,
                                         &contract::ResolutionRecord::id);
    return found == provenance.resolutions.end() ? nullptr : &*found;
}

const contract::ResolutionRecord *
find_resolution_path(const contract::ProvenanceLedger &provenance,
                     std::string_view parameter_path) noexcept {
    const auto found = std::ranges::find(provenance.resolutions, parameter_path,
                                         &contract::ResolutionRecord::parameter_path);
    return found == provenance.resolutions.end() ? nullptr : &*found;
}

contract::Sha256Digest
source_matrix_digest(const contract::SourceMatrixContract &matrix) {
    DigestPayload writer;
    writer.string("engine-sim-offline-authored-source-matrix-v1");
    writer.string(matrix.id);
    writer.byte(static_cast<std::uint8_t>(matrix.distribution));

    writer.u64(static_cast<std::uint64_t>(matrix.required_source_routes.size()));
    for (const auto &route : matrix.required_source_routes) {
        writer.string(route.semantic_id);
        writer.byte(static_cast<std::uint8_t>(route.kind));
        writer.byte(static_cast<std::uint8_t>(route.disposition));
        writer.string(route.disposition_reason);
        writer.u64(static_cast<std::uint64_t>(route.artifact_roles.size()));
        for (const auto &role : route.artifact_roles) {
            writer.string(role);
        }
    }

    writer.u64(static_cast<std::uint64_t>(matrix.required_output_buses.size()));
    for (const auto &bus : matrix.required_output_buses) {
        writer.string(bus.semantic_id);
        writer.byte(static_cast<std::uint8_t>(bus.kind));
        writer.u64(static_cast<std::uint64_t>(bus.artifact_roles.size()));
        for (const auto &role : bus.artifact_roles) {
            writer.string(role);
        }
    }

    writer.u64(static_cast<std::uint64_t>(matrix.required_artifacts.size()));
    for (const auto &artifact : matrix.required_artifacts) {
        writer.string(artifact.role);
        writer.byte(static_cast<std::uint8_t>(artifact.kind));
        writer.byte(artifact.audio.has_value() ? 1U : 0U);
        if (artifact.audio.has_value()) {
            writer.u64(artifact.audio->sample_rate.numerator);
            writer.u64(artifact.audio->sample_rate.denominator);
            writer.u64(artifact.audio->frame_count);
            writer.string(artifact.audio->channel_layout_id);
            writer.string(artifact.audio->sample_encoding_id);
        }
        writer.byte(artifact.diagnostic ? 1U : 0U);
    }

    writer.u64(static_cast<std::uint64_t>(matrix.declared_omissions.size()));
    for (const auto &omission : matrix.declared_omissions) {
        writer.string(omission.semantic_id);
        writer.byte(static_cast<std::uint8_t>(omission.kind));
        writer.string(omission.rationale);
    }
    return writer.finish();
}

} // namespace engine_sim_offline::compile::detail::scenario_resolution
