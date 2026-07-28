#include "profiles/bmw_m52b28_profile_internal.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::profiles {
namespace {

constexpr contract::Sha256Digest kExpectedRpmSha256{{
    0xb6, 0x20, 0x69, 0x10, 0xb9, 0xc7, 0xb1, 0x96, 0x94, 0xe3, 0x5e,
    0x08, 0xa3, 0xc5, 0xd8, 0x45, 0x0f, 0x03, 0xcf, 0xdc, 0xbf, 0x43,
    0xe8, 0x18, 0xeb, 0x2a, 0x06, 0x06, 0x92, 0x7d, 0x8c, 0xda,
}};

contract::ValidationReport
validate_rpm_lane(std::span<const double> post_step_rpm,
                  const contract::Sha256Digest *declared_sha256) {
    contract::ValidationReport report;
    if (post_step_rpm.size() != 170000U) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "scenario.mode.trajectory.rpm",
                   "BMW parity RPM lane requires exactly 170000 samples");
        return report;
    }

    const auto content_sha256 = contract::canonical_binary64_le_sha256(post_step_rpm);
    if (content_sha256 != kExpectedRpmSha256 ||
        (declared_sha256 != nullptr && *declared_sha256 != content_sha256)) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "scenario.mode.trajectory.rpm",
                   "BMW parity RPM lane canonical content identity changed");
    }
    return report;
}

void append(contract::ValidationReport &destination,
            contract::ValidationReport source) {
    destination.append(std::move(source));
}

} // namespace

namespace detail {

contract::ValidationReport
validate_bmw_m52b28_parity_rpm_input(std::span<const double> post_step_rpm) {
    return validate_rpm_lane(post_step_rpm, nullptr);
}

contract::ValidationReport validate_bmw_m52b28_parity_rpm_trajectory(
    const contract::FixedRateRpmTrajectory &trajectory) {
    return validate_rpm_lane(trajectory.post_step_rpm,
                             &trajectory.samples_f64le_sha256);
}

} // namespace detail

contract::ValidationReport
validate_bmw_m52b28_parity_request(const BmwM52b28ParityRequest &request) {
    contract::ValidationReport report;
    const auto *sweep =
        std::get_if<contract::PrescribedKinematicSweep>(&request.scenario.mode);
    const contract::FixedRateRpmTrajectory *rpm = nullptr;
    if (sweep != nullptr) {
        rpm = std::get_if<contract::FixedRateRpmTrajectory>(&sweep->trajectory.rpm);
    }
    if (rpm == nullptr) {
        report.add(contract::ContractIssueCode::inconsistent_semantics,
                   "scenario.mode.trajectory.rpm",
                   "BMW parity request requires the fixed post-step RPM lane");
        return report;
    }

    auto rpm_report = detail::validate_bmw_m52b28_parity_rpm_trajectory(*rpm);
    if (!rpm_report.ok()) {
        append(report, std::move(rpm_report));
        return report;
    }

    append(report, contract::validate(request.engine, request.provenance));
    append(report, contract::validate(request.scenario, request.provenance));
    append(report, contract::validate_for_engine(request.scenario, request.engine));

    auto expected = detail::build_bmw_m52b28_parity_request_unvalidated(
        std::vector<double>{rpm->post_step_rpm.begin(), rpm->post_step_rpm.end()});
    if (request != expected) {
        report.add(
            contract::ContractIssueCode::inconsistent_semantics,
            "bmw_m52b28_parity_request",
            "request differs from the exact normative BMW M52B28 parity request");
    }
    return report;
}

} // namespace engine_sim_offline::profiles
