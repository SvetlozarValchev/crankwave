#include "engine_sim_offline/responsive/held_texture.hpp"

#include "identity_bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace engine_sim_offline::responsive {
namespace {

constexpr double kRpmAdjacentMaximumShiftSamples =
    static_cast<double>(kHeldSamplesPerCycle) / 2.0;
constexpr double kLoadAdjacentMaximumShiftSamples =
    static_cast<double>(kHeldSamplesPerCycle) / 8.0;
constexpr std::size_t kSecondPeakExclusionRadiusSamples = 32U;

void require(contract::ValidationReport &report, const bool condition,
             const contract::ContractIssueCode code, std::string path,
             std::string message) {
    if (!condition) {
        report.add(code, std::move(path), std::move(message));
    }
}

[[nodiscard]] contract::ValidationReport
one_issue(const contract::ContractIssueCode code, std::string path,
          std::string message) {
    contract::ValidationReport report;
    report.add(code, std::move(path), std::move(message));
    return report;
}

[[nodiscard]] bool finite(const double value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] std::string_view axis_id(const HeldAlignmentAxis axis) noexcept {
    return axis == HeldAlignmentAxis::rpm ? "rpm" : "load";
}

[[nodiscard]] std::int64_t
signed_circular_difference(std::int64_t index, const std::int64_t count) noexcept {
    while (index >= count / 2) {
        index -= count;
    }
    while (index < -count / 2) {
        index += count;
    }
    return index;
}

void fft(std::vector<double> &real, std::vector<double> &imaginary,
         const bool inverse = false) {
    const std::size_t count = real.size();
    for (std::size_t index = 1U, reversed = 0U; index < count; ++index) {
        std::size_t bit = count >> 1U;
        for (; (reversed & bit) != 0U; bit >>= 1U) {
            reversed ^= bit;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(real[index], real[reversed]);
            std::swap(imaginary[index], imaginary[reversed]);
        }
    }
    for (std::size_t width = 2U; width <= count; width <<= 1U) {
        const double angle = (inverse ? 2.0 : -2.0) * std::numbers::pi_v<double> /
                             static_cast<double>(width);
        const double root_real = std::cos(angle);
        const double root_imaginary = std::sin(angle);
        for (std::size_t begin = 0U; begin < count; begin += width) {
            double twiddle_real = 1.0;
            double twiddle_imaginary = 0.0;
            for (std::size_t offset = 0U; offset < width / 2U; ++offset) {
                const std::size_t even = begin + offset;
                const std::size_t odd = even + width / 2U;
                const double odd_real =
                    real[odd] * twiddle_real - imaginary[odd] * twiddle_imaginary;
                const double odd_imaginary =
                    real[odd] * twiddle_imaginary + imaginary[odd] * twiddle_real;
                const double even_real = real[even];
                const double even_imaginary = imaginary[even];
                real[even] = even_real + odd_real;
                imaginary[even] = even_imaginary + odd_imaginary;
                real[odd] = even_real - odd_real;
                imaginary[odd] = even_imaginary - odd_imaginary;
                const double next_twiddle_real =
                    twiddle_real * root_real - twiddle_imaginary * root_imaginary;
                twiddle_imaginary =
                    twiddle_real * root_imaginary + twiddle_imaginary * root_real;
                twiddle_real = next_twiddle_real;
            }
        }
    }
    if (inverse) {
        for (std::size_t index = 0U; index < count; ++index) {
            real[index] /= static_cast<double>(count);
            imaginary[index] /= static_cast<double>(count);
        }
    }
}

[[nodiscard]] bool circular_correlation(const std::span<const double> left,
                                        const std::span<const double> right,
                                        std::vector<double> &output) {
    if (left.size() != right.size() || left.size() < 2U ||
        (left.size() & (left.size() - 1U)) != 0U) {
        return false;
    }
    const std::size_t count = left.size();
    std::vector<double> left_real(count);
    std::vector<double> left_imaginary(count, 0.0);
    std::vector<double> right_real(count);
    std::vector<double> right_imaginary(count, 0.0);
    double left_mean = 0.0;
    double right_mean = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        left_mean += left[index];
        right_mean += right[index];
    }
    left_mean /= static_cast<double>(count);
    right_mean /= static_cast<double>(count);
    double left_energy = 0.0;
    double right_energy = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        left_real[index] = left[index] - left_mean;
        right_real[index] = right[index] - right_mean;
        left_energy += left_real[index] * left_real[index];
        right_energy += right_real[index] * right_real[index];
    }
    const double normalization = std::sqrt(left_energy * right_energy);
    if (!(normalization > 0.0) || !finite(normalization)) {
        return false;
    }
    fft(left_real, left_imaginary);
    fft(right_real, right_imaginary);
    for (std::size_t index = 0U; index < count; ++index) {
        const double real = left_real[index] * right_real[index] +
                            left_imaginary[index] * right_imaginary[index];
        const double imaginary = left_real[index] * right_imaginary[index] -
                                 left_imaginary[index] * right_real[index];
        left_real[index] = real;
        left_imaginary[index] = imaginary;
    }
    fft(left_real, left_imaginary, true);
    for (double &value : left_real) {
        value /= normalization;
    }
    output = std::move(left_real);
    return true;
}

struct AdjacentAlignment {
    double shift_right_to_left = 0.0;
    double peak_correlation = 0.0;
    double ambiguity_margin = 0.0;
    double unconstrained_shift_right_to_left = 0.0;
    double unconstrained_peak_correlation = 0.0;
    double weight = 0.0;
};

[[nodiscard]] std::optional<AdjacentAlignment>
adjacent_alignment(const std::span<const double> left,
                   const std::span<const double> right,
                   const double maximum_shift_samples) {
    std::vector<double> correlation;
    if (!circular_correlation(left, right, correlation)) {
        return std::nullopt;
    }
    const std::size_t count = correlation.size();
    std::size_t best_index = 0U;
    double best_value = -std::numeric_limits<double>::infinity();
    std::size_t unconstrained_index = 0U;
    double unconstrained_value = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < count; ++index) {
        const double value = correlation[index];
        if (value > unconstrained_value) {
            unconstrained_value = value;
            unconstrained_index = index;
        }
        const auto signed_index = signed_circular_difference(
            static_cast<std::int64_t>(index), static_cast<std::int64_t>(count));
        if (std::abs(static_cast<double>(signed_index)) <= maximum_shift_samples &&
            value > best_value) {
            best_value = value;
            best_index = index;
        }
    }
    const double before = correlation[(best_index + count - 1U) % count];
    const double center = correlation[best_index];
    const double after = correlation[(best_index + 1U) % count];
    const double curvature = before - 2.0 * center + after;
    const double sub_sample_offset =
        std::abs(curvature) < 1e-15
            ? 0.0
            : std::clamp(0.5 * (before - after) / curvature, -0.5, 0.5);
    const double correlation_offset =
        static_cast<double>(signed_circular_difference(
            static_cast<std::int64_t>(best_index), static_cast<std::int64_t>(count))) +
        sub_sample_offset;
    double second_value = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < count; ++index) {
        const auto distance = std::abs(signed_circular_difference(
            static_cast<std::int64_t>(index) - static_cast<std::int64_t>(best_index),
            static_cast<std::int64_t>(count)));
        if (distance > static_cast<std::int64_t>(kSecondPeakExclusionRadiusSamples)) {
            second_value = std::max(second_value, correlation[index]);
        }
    }
    AdjacentAlignment result;
    result.shift_right_to_left = -correlation_offset;
    result.peak_correlation = center;
    result.ambiguity_margin = center - second_value;
    result.unconstrained_shift_right_to_left = -static_cast<double>(
        signed_circular_difference(static_cast<std::int64_t>(unconstrained_index),
                                   static_cast<std::int64_t>(count)));
    result.unconstrained_peak_correlation = unconstrained_value;
    result.weight = std::max(1e-4, center * center) *
                    std::max(0.02, std::min(1.0, result.ambiguity_margin * 20.0));
    return result;
}

[[nodiscard]] bool solve_linear_system(const std::vector<std::vector<double>> &matrix,
                                       const std::vector<double> &vector,
                                       std::vector<double> &solution) {
    const std::size_t count = vector.size();
    std::vector<std::vector<double>> augmented(count,
                                               std::vector<double>(count + 1U, 0.0));
    for (std::size_t row = 0U; row < count; ++row) {
        std::copy(matrix[row].begin(), matrix[row].end(), augmented[row].begin());
        augmented[row][count] = vector[row];
    }
    for (std::size_t column = 0U; column < count; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1U; row < count; ++row) {
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(augmented[pivot][column]) < 1e-12) {
            return false;
        }
        std::swap(augmented[column], augmented[pivot]);
        const double divisor = augmented[column][column];
        for (std::size_t index = column; index <= count; ++index) {
            augmented[column][index] /= divisor;
        }
        for (std::size_t row = 0U; row < count; ++row) {
            if (row == column) {
                continue;
            }
            const double factor = augmented[row][column];
            for (std::size_t index = column; index <= count; ++index) {
                augmented[row][index] -= factor * augmented[column][index];
            }
        }
    }
    solution.resize(count);
    for (std::size_t row = 0U; row < count; ++row) {
        solution[row] = augmented[row][count];
    }
    return true;
}

[[nodiscard]] double periodic_sample(const std::span<const double> samples,
                                     const double position) {
    const double count = static_cast<double>(samples.size());
    double wrapped = std::fmod(position, count);
    if (wrapped < 0.0) {
        wrapped += count;
    }
    const auto left = static_cast<std::size_t>(std::floor(wrapped));
    const auto right = (left + 1U) % samples.size();
    return samples[left] +
           (samples[right] - samples[left]) * (wrapped - static_cast<double>(left));
}

[[nodiscard]] std::vector<double> circular_shift(const std::span<const double> samples,
                                                 const double shift_samples) {
    std::vector<double> output(samples.size());
    for (std::size_t index = 0U; index < samples.size(); ++index) {
        output[index] =
            periodic_sample(samples, static_cast<double>(index) - shift_samples);
    }
    return output;
}

[[nodiscard]] HeldDistributionSummary
summarize_distribution(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto quantile = [&](const double amount) {
        return values[static_cast<std::size_t>(
            std::floor(static_cast<double>(values.size() - 1U) * amount))];
    };
    return {values.front(), quantile(0.5), quantile(0.9), values.back()};
}

[[nodiscard]] contract::Sha256Digest
coalesced_cell_identity(const HeldCookedCell &cell) {
    detail::CanonicalIdentityBytes identity{kHeldLoadCoalescingMethodId};
    identity.string(cell.id);
    identity.f64(cell.rpm);
    identity.string(cell.lane_id);
    identity.f64(cell.throttle_01);
    identity.u64(cell.routes.size());
    for (const auto &route : cell.routes) {
        identity.string(route.route_id);
        identity.f64(route.source_interval.start_revolutions);
        identity.f64(route.source_interval.end_revolutions);
        identity.f64(route.telemetry.mean_manifold_pressure_pa_abs);
        identity.digest(route.mean_payload_sha256);
        identity.digest(route.residual_payload_sha256);
        identity.digest(route.identity_sha256);
    }
    identity.u64(cell.coalesced_authored_lanes.size());
    for (const auto &lane : cell.coalesced_authored_lanes) {
        identity.string(lane);
    }
    identity.u64(cell.coalesced_capture_throttles_01.size());
    for (const double throttle : cell.coalesced_capture_throttles_01) {
        identity.f64(throttle);
    }
    return identity.finish();
}

[[nodiscard]] bool routes_are_identical(const HeldCookedCell &left,
                                        const HeldCookedCell &right) {
    if (left.routes.size() != right.routes.size()) {
        return false;
    }
    for (const auto &left_route : left.routes) {
        const auto right_route =
            std::ranges::find_if(right.routes, [&](const HeldCookedRoute &candidate) {
                return candidate.route_id == left_route.route_id;
            });
        if (right_route == right.routes.end() ||
            left_route.telemetry.mean_manifold_pressure_pa_abs !=
                right_route->telemetry.mean_manifold_pressure_pa_abs ||
            left_route.source_interval.start_revolutions !=
                right_route->source_interval.start_revolutions ||
            left_route.source_interval.end_revolutions !=
                right_route->source_interval.end_revolutions ||
            left_route.mean_payload_sha256 != right_route->mean_payload_sha256 ||
            left_route.residual_payload_sha256 !=
                right_route->residual_payload_sha256) {
            return false;
        }
    }
    return true;
}

struct WorkEdge {
    std::size_t left = 0U;
    std::size_t right = 0U;
    HeldAlignmentAxis axis = HeldAlignmentAxis::rpm;
    AdjacentAlignment alignment;
    double lifted_shift = 0.0;
};

[[nodiscard]] contract::ValidationReport
validate_alignment_input(const ResponsiveBakeProfile &profile,
                         const std::span<const HeldCookedCell> cells) {
    using enum contract::ContractIssueCode;
    auto report = validate_responsive_bake_profile(profile);
    require(report,
            cells.size() == kResponsiveRpmAnchorCount * kResponsiveLoadLaneCount,
            inconsistent_shape, "cells",
            "phase alignment requires the complete authored RPM/load rectangle");
    std::map<std::string, std::size_t, std::less<>> ids;
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        const auto &cell = cells[index];
        const std::size_t rpm_index = index / kResponsiveLoadLaneCount;
        const std::size_t lane_index = index % kResponsiveLoadLaneCount;
        if (rpm_index < kResponsiveRpmAnchorCount) {
            require(report,
                    cell.rpm == profile.rpm.anchors[rpm_index] &&
                        cell.lane_id == profile.capture.load_lanes[lane_index].id &&
                        cell.throttle_01 ==
                            profile.capture.load_lanes[lane_index].throttle_01,
                    inconsistent_semantics, "cells[" + std::to_string(index) + "]",
                    "phase-alignment cells are not in canonical RPM-major lane order");
        }
        require(report, !cell.id.empty() && ids.emplace(cell.id, index).second,
                duplicate_identity, "cells[" + std::to_string(index) + "].id",
                "phase-alignment cell IDs must be nonempty and unique");
        require(report, !cell.routes.empty(), missing_value,
                "cells[" + std::to_string(index) + "].routes",
                "phase-alignment cell has no source routes");
        if (cell.routes.empty()) {
            continue;
        }
        if (index != 0U) {
            require(report, cell.routes.size() == cells.front().routes.size(),
                    inconsistent_shape, "cells[" + std::to_string(index) + "].routes",
                    "every phase-alignment cell must use the same route count");
        }
        std::map<std::string, bool, std::less<>> route_ids;
        for (std::size_t route_index = 0U; route_index < cell.routes.size();
             ++route_index) {
            const auto &route = cell.routes[route_index];
            require(report,
                    !route.route_id.empty() &&
                        route_ids.emplace(route.route_id, true).second &&
                        route.texture.mean.size() == kHeldSamplesPerCycle &&
                        std::ranges::all_of(
                            route.texture.mean,
                            [](const float value) { return std::isfinite(value); }),
                    inconsistent_shape,
                    "cells[" + std::to_string(index) + "].routes[" +
                        std::to_string(route_index) + "]",
                    "phase alignment requires a finite 4096-sample mean per route");
            if (index != 0U && route_index < cells.front().routes.size()) {
                require(report,
                        route.route_id == cells.front().routes[route_index].route_id,
                        inconsistent_semantics,
                        "cells[" + std::to_string(index) + "].routes",
                        "every phase-alignment cell must use the same ordered routes");
            }
        }
    }
    return report;
}

[[nodiscard]] std::vector<double> summed_mean(const HeldCookedCell &cell) {
    std::vector<double> result(kHeldSamplesPerCycle, 0.0);
    for (const auto &route : cell.routes) {
        for (std::size_t index = 0U; index < result.size(); ++index) {
            result[index] += static_cast<double>(route.texture.mean[index]);
        }
    }
    return result;
}

[[nodiscard]] contract::Sha256Digest
phase_alignment_identity(const HeldPhaseAlignment &alignment) {
    detail::CanonicalIdentityBytes identity{kHeldPhaseAlignmentMethodId};
    identity.string(alignment.method);
    identity.string(alignment.reference_cell_id);
    identity.u64(alignment.cells.size());
    for (const auto &cell : alignment.cells) {
        identity.string(cell.cell_id);
        identity.f64(cell.shift_to_canonical_samples);
    }
    const auto &report = alignment.report;
    identity.u64(report.edge_count);
    const auto add_summary = [&](const HeldDistributionSummary &summary) {
        identity.f64(summary.minimum);
        identity.f64(summary.p50);
        identity.f64(summary.p90);
        identity.f64(summary.maximum);
    };
    add_summary(report.edge_residual_samples);
    identity.f64(report.minimum_peak_correlation);
    identity.f64(report.minimum_ambiguity_margin);
    identity.u64(report.unconstrained_peak_outside_adjacent_window_count);
    add_summary(report.direct_midpoint_retained_target_01);
    add_summary(report.aligned_midpoint_retained_target_01);
    identity.u64(report.midpoint_comparisons.size());
    for (const auto &comparison : report.midpoint_comparisons) {
        identity.string(comparison.left_cell_id);
        identity.string(comparison.right_cell_id);
        identity.string(axis_id(comparison.axis));
        identity.f64(comparison.direct_midpoint_rms);
        identity.f64(comparison.aligned_midpoint_rms);
        identity.f64(comparison.linear_anchor_rms_target);
        identity.f64(comparison.direct_retained_target_01);
        identity.f64(comparison.aligned_retained_target_01);
    }
    identity.u64(report.preserved_fixed_cell_count);
    identity.f64(report.maximum_preserved_fixed_shift_error_samples);
    identity.u64(report.adjacent_edges.size());
    for (const auto &edge : report.adjacent_edges) {
        identity.string(edge.left_cell_id);
        identity.string(edge.right_cell_id);
        identity.string(axis_id(edge.axis));
        identity.f64(edge.constrained_shift_right_to_left_samples);
        identity.f64(edge.constrained_peak_correlation);
        identity.f64(edge.ambiguity_margin);
        identity.f64(edge.unconstrained_shift_right_to_left_samples);
        identity.f64(edge.unconstrained_peak_correlation);
        identity.f64(edge.lifted_shift_right_to_left_samples);
        identity.f64(edge.solved_shift_delta_samples);
        identity.f64(edge.weight);
    }
    return identity.finish();
}

[[nodiscard]] contract::Sha256Digest
held_grid_identity(const ResponsiveBakeProfile &profile, const HeldCookedGrid &grid) {
    detail::CanonicalIdentityBytes identity{kHeldGridIdentityMethodId};
    identity.digest(profile.selection_identity_sha256);
    identity.u64(grid.cells.size());
    for (const auto &cell : grid.cells) {
        identity.string(cell.id);
        identity.digest(cell.identity_sha256);
    }
    identity.digest(grid.phase_alignment.identity_sha256);
    identity.string(kHeldLoadCoalescingMethodId);
    return identity.finish();
}

} // namespace

HeldResult<std::vector<HeldCookedCell>>
coalesce_duplicate_held_loads(const std::span<const HeldCookedCell> cells) {
    using enum contract::ContractIssueCode;
    std::vector<HeldCookedCell> result;
    result.reserve(cells.size());
    for (std::size_t index = 0U; index < cells.size(); ++index) {
        const auto &cell = cells[index];
        if (cell.routes.empty()) {
            return one_issue(missing_value,
                             "cells[" + std::to_string(index) + "].routes",
                             "duplicate-load coalescing requires source routes");
        }
        const double map = cell.routes.front().telemetry.mean_manifold_pressure_pa_abs;
        if (!finite(cell.rpm) || !finite(cell.throttle_01) || !finite(map)) {
            return one_issue(invalid_value, "cells[" + std::to_string(index) + "]",
                             "duplicate-load coordinates must be finite");
        }
        std::map<std::string, bool, std::less<>> route_ids;
        for (std::size_t route_index = 0U; route_index < cell.routes.size();
             ++route_index) {
            const auto &route = cell.routes[route_index];
            if (route.route_id.empty() ||
                !route_ids.emplace(route.route_id, true).second ||
                !finite(route.telemetry.mean_manifold_pressure_pa_abs) ||
                route.telemetry.mean_manifold_pressure_pa_abs != map) {
                return one_issue(inconsistent_semantics,
                                 "cells[" + std::to_string(index) + "].routes[" +
                                     std::to_string(route_index) + "]",
                                 "duplicate-load coalescing requires unique routes at "
                                 "one shared MAP coordinate");
            }
        }
        const auto existing =
            std::ranges::find_if(result, [&](const HeldCookedCell &candidate) {
                return candidate.rpm == cell.rpm &&
                       candidate.routes.front()
                               .telemetry.mean_manifold_pressure_pa_abs == map;
            });
        if (existing == result.end()) {
            auto retained = cell;
            retained.coalesced_authored_lanes = {cell.lane_id};
            retained.coalesced_capture_throttles_01 = {cell.throttle_01};
            result.push_back(std::move(retained));
            continue;
        }
        if (!routes_are_identical(*existing, cell)) {
            return one_issue(inconsistent_semantics,
                             "cells[" + std::to_string(index) + "]",
                             "distinct held audio occupies the same "
                             "RPM/manifold-pressure coordinate");
        }
        existing->coalesced_authored_lanes.push_back(cell.lane_id);
        existing->coalesced_capture_throttles_01.push_back(cell.throttle_01);
    }
    for (auto &cell : result) {
        cell.identity_sha256 = coalesced_cell_identity(cell);
    }
    return result;
}

HeldResult<HeldPhaseAlignment>
align_held_texture_grid(const ResponsiveBakeProfile &profile,
                        const std::span<const HeldCookedCell> uncoalesced_cells,
                        const std::optional<HeldPriorPhaseAlignment> &prior) {
    using enum contract::ContractIssueCode;
    auto report = validate_alignment_input(profile, uncoalesced_cells);
    if (!report.ok()) {
        return report;
    }
    const std::size_t node_count = uncoalesced_cells.size();
    std::map<std::string, std::size_t, std::less<>> node_index;
    std::vector<std::vector<double>> signals;
    signals.reserve(node_count);
    for (std::size_t index = 0U; index < node_count; ++index) {
        node_index.emplace(uncoalesced_cells[index].id, index);
        signals.push_back(summed_mean(uncoalesced_cells[index]));
    }

    std::vector<std::optional<double>> fixed_shifts(node_count);
    std::string prior_reference;
    if (prior.has_value()) {
        prior_reference = prior->reference_cell_id;
        std::map<std::string, bool, std::less<>> prior_ids;
        for (std::size_t index = 0U; index < prior->cells.size(); ++index) {
            const auto &cell = prior->cells[index];
            require(report, finite(cell.shift_to_canonical_samples), invalid_value,
                    "prior.cells[" + std::to_string(index) + "]",
                    "prior phase-alignment shifts must be finite");
            require(report, prior_ids.emplace(cell.cell_id, true).second,
                    duplicate_identity,
                    "prior.cells[" + std::to_string(index) + "].cell_id",
                    "prior phase-alignment cell IDs must be unique");
            const auto found = node_index.find(cell.cell_id);
            if (found != node_index.end() && finite(cell.shift_to_canonical_samples)) {
                fixed_shifts[found->second] = cell.shift_to_canonical_samples;
            }
        }
    }
    if (!report.ok()) {
        return report;
    }

    std::size_t reference_rpm_index = 0U;
    for (std::size_t index = 1U; index < profile.rpm.anchors.size(); ++index) {
        if (std::abs(profile.rpm.anchors[index] - 3000.0) <
            std::abs(profile.rpm.anchors[reference_rpm_index] - 3000.0)) {
            reference_rpm_index = index;
        }
    }
    const std::size_t default_reference_index =
        reference_rpm_index * kResponsiveLoadLaneCount +
        (kResponsiveLoadLaneCount - 1U);
    std::size_t reference_index = default_reference_index;
    const auto prior_reference_found = node_index.find(prior_reference);
    if (prior_reference_found != node_index.end() &&
        fixed_shifts[prior_reference_found->second].has_value()) {
        reference_index = prior_reference_found->second;
    }
    if (std::ranges::none_of(fixed_shifts,
                             [](const auto &value) { return value.has_value(); })) {
        fixed_shifts[reference_index] = 0.0;
    }

    std::vector<WorkEdge> edges;
    edges.reserve(kResponsiveLoadLaneCount * (kResponsiveRpmAnchorCount - 1U) +
                  kResponsiveRpmAnchorCount * (kResponsiveLoadLaneCount - 1U));
    const auto add_edge = [&](const std::size_t left, const std::size_t right,
                              const HeldAlignmentAxis axis) -> bool {
        const double maximum_shift = axis == HeldAlignmentAxis::rpm
                                         ? kRpmAdjacentMaximumShiftSamples
                                         : kLoadAdjacentMaximumShiftSamples;
        auto alignment =
            adjacent_alignment(signals[left], signals[right], maximum_shift);
        if (!alignment.has_value()) {
            return false;
        }
        edges.push_back({left, right, axis, *alignment, 0.0});
        return true;
    };
    for (std::size_t lane = 0U; lane < kResponsiveLoadLaneCount; ++lane) {
        for (std::size_t rpm = 0U; rpm + 1U < kResponsiveRpmAnchorCount; ++rpm) {
            if (!add_edge(rpm * kResponsiveLoadLaneCount + lane,
                          (rpm + 1U) * kResponsiveLoadLaneCount + lane,
                          HeldAlignmentAxis::rpm)) {
                return one_issue(invalid_value, "cells",
                                 "phase-alignment cell has zero correlation energy");
            }
        }
    }
    for (std::size_t rpm = 0U; rpm < kResponsiveRpmAnchorCount; ++rpm) {
        for (std::size_t lane = 0U; lane + 1U < kResponsiveLoadLaneCount; ++lane) {
            if (!add_edge(rpm * kResponsiveLoadLaneCount + lane,
                          rpm * kResponsiveLoadLaneCount + lane + 1U,
                          HeldAlignmentAxis::load)) {
                return one_issue(invalid_value, "cells",
                                 "phase-alignment cell has zero correlation energy");
            }
        }
    }

    std::vector<std::optional<double>> tree_shifts = fixed_shifts;
    std::size_t unused_count = static_cast<std::size_t>(std::ranges::count_if(
        tree_shifts, [](const auto &value) { return !value.has_value(); }));
    while (unused_count != 0U) {
        const WorkEdge *selected = nullptr;
        for (const auto &edge : edges) {
            const bool left_known = tree_shifts[edge.left].has_value();
            const bool right_known = tree_shifts[edge.right].has_value();
            if (left_known == right_known) {
                continue;
            }
            if (selected == nullptr ||
                edge.alignment.weight > selected->alignment.weight) {
                selected = &edge;
            }
        }
        if (selected == nullptr) {
            return one_issue(inconsistent_semantics, "cells",
                             "phase-alignment grid is disconnected");
        }
        if (tree_shifts[selected->left].has_value()) {
            tree_shifts[selected->right] =
                *tree_shifts[selected->left] + selected->alignment.shift_right_to_left;
        } else {
            tree_shifts[selected->left] =
                *tree_shifts[selected->right] - selected->alignment.shift_right_to_left;
        }
        --unused_count;
    }

    for (auto &edge : edges) {
        const double predicted = *tree_shifts[edge.right] - *tree_shifts[edge.left];
        const double turns =
            std::floor((predicted - edge.alignment.shift_right_to_left) /
                           static_cast<double>(kHeldSamplesPerCycle) +
                       0.5);
        edge.lifted_shift = edge.alignment.shift_right_to_left +
                            turns * static_cast<double>(kHeldSamplesPerCycle);
    }

    std::vector<std::size_t> variables;
    std::vector<std::optional<std::size_t>> variable_index(node_count);
    for (std::size_t index = 0U; index < node_count; ++index) {
        if (!fixed_shifts[index].has_value()) {
            variable_index[index] = variables.size();
            variables.push_back(index);
        }
    }
    std::vector<std::vector<double>> matrix(variables.size(),
                                            std::vector<double>(variables.size(), 0.0));
    std::vector<double> vector(variables.size(), 0.0);
    for (const auto &edge : edges) {
        const double weight = edge.alignment.weight;
        const auto left_variable = variable_index[edge.left];
        const auto right_variable = variable_index[edge.right];
        if (left_variable.has_value()) {
            matrix[*left_variable][*left_variable] += weight;
            vector[*left_variable] -= weight * edge.lifted_shift;
            if (!right_variable.has_value()) {
                vector[*left_variable] += weight * *fixed_shifts[edge.right];
            }
        }
        if (right_variable.has_value()) {
            matrix[*right_variable][*right_variable] += weight;
            vector[*right_variable] += weight * edge.lifted_shift;
            if (!left_variable.has_value()) {
                vector[*right_variable] += weight * *fixed_shifts[edge.left];
            }
        }
        if (left_variable.has_value() && right_variable.has_value()) {
            matrix[*left_variable][*right_variable] -= weight;
            matrix[*right_variable][*left_variable] -= weight;
        }
    }
    std::vector<double> shifts(node_count, 0.0);
    for (std::size_t index = 0U; index < node_count; ++index) {
        if (fixed_shifts[index].has_value()) {
            shifts[index] = *fixed_shifts[index];
        }
    }
    if (!variables.empty()) {
        std::vector<double> solved;
        if (!solve_linear_system(matrix, vector, solved)) {
            return one_issue(inconsistent_semantics, "cells",
                             "phase-alignment graph system is singular");
        }
        for (std::size_t index = 0U; index < variables.size(); ++index) {
            shifts[variables[index]] = solved[index];
        }
    }
    if (!std::ranges::all_of(shifts,
                             [](const double value) { return finite(value); })) {
        return one_issue(invalid_value, "cells",
                         "phase-alignment solve produced a non-finite shift");
    }

    HeldPhaseAlignment result;
    result.reference_cell_id = uncoalesced_cells[reference_index].id;
    result.cells.reserve(node_count);
    for (std::size_t index = 0U; index < node_count; ++index) {
        result.cells.push_back({uncoalesced_cells[index].id, shifts[index]});
    }

    std::vector<double> edge_residuals;
    std::vector<double> direct_retained;
    std::vector<double> aligned_retained;
    edge_residuals.reserve(edges.size());
    direct_retained.reserve(edges.size());
    aligned_retained.reserve(edges.size());
    result.report.edge_count = edges.size();
    result.report.minimum_peak_correlation = std::numeric_limits<double>::infinity();
    result.report.minimum_ambiguity_margin = std::numeric_limits<double>::infinity();
    result.report.midpoint_comparisons.reserve(edges.size());
    result.report.adjacent_edges.reserve(edges.size());
    for (const auto &edge : edges) {
        const auto shifted_left = circular_shift(signals[edge.left], shifts[edge.left]);
        const auto shifted_right =
            circular_shift(signals[edge.right], shifts[edge.right]);
        double left_square = 0.0;
        double right_square = 0.0;
        double direct_square = 0.0;
        double aligned_square = 0.0;
        for (std::size_t index = 0U; index < kHeldSamplesPerCycle; ++index) {
            left_square += signals[edge.left][index] * signals[edge.left][index];
            right_square += signals[edge.right][index] * signals[edge.right][index];
            const double direct =
                0.5 * (signals[edge.left][index] + signals[edge.right][index]);
            const double aligned = 0.5 * (shifted_left[index] + shifted_right[index]);
            direct_square += direct * direct;
            aligned_square += aligned * aligned;
        }
        const double left_rms =
            std::sqrt(left_square / static_cast<double>(kHeldSamplesPerCycle));
        const double right_rms =
            std::sqrt(right_square / static_cast<double>(kHeldSamplesPerCycle));
        const double target_rms = 0.5 * (left_rms + right_rms);
        const double direct_rms =
            std::sqrt(direct_square / static_cast<double>(kHeldSamplesPerCycle));
        const double aligned_rms =
            std::sqrt(aligned_square / static_cast<double>(kHeldSamplesPerCycle));
        const double direct_ratio = direct_rms / std::max(target_rms, 1e-30);
        const double aligned_ratio = aligned_rms / std::max(target_rms, 1e-30);
        result.report.midpoint_comparisons.push_back(
            {uncoalesced_cells[edge.left].id, uncoalesced_cells[edge.right].id,
             edge.axis, direct_rms, aligned_rms, target_rms, direct_ratio,
             aligned_ratio});
        direct_retained.push_back(direct_ratio);
        aligned_retained.push_back(aligned_ratio);
        edge_residuals.push_back(
            std::abs(shifts[edge.right] - shifts[edge.left] - edge.lifted_shift));
        result.report.minimum_peak_correlation = std::min(
            result.report.minimum_peak_correlation, edge.alignment.peak_correlation);
        result.report.minimum_ambiguity_margin = std::min(
            result.report.minimum_ambiguity_margin, edge.alignment.ambiguity_margin);
        if (std::abs(edge.alignment.unconstrained_shift_right_to_left) >
            kLoadAdjacentMaximumShiftSamples) {
            ++result.report.unconstrained_peak_outside_adjacent_window_count;
        }
        result.report.adjacent_edges.push_back(
            {uncoalesced_cells[edge.left].id, uncoalesced_cells[edge.right].id,
             edge.axis, edge.alignment.shift_right_to_left,
             edge.alignment.peak_correlation, edge.alignment.ambiguity_margin,
             edge.alignment.unconstrained_shift_right_to_left,
             edge.alignment.unconstrained_peak_correlation, edge.lifted_shift,
             shifts[edge.right] - shifts[edge.left], edge.alignment.weight});
    }
    result.report.edge_residual_samples =
        summarize_distribution(std::move(edge_residuals));
    result.report.direct_midpoint_retained_target_01 =
        summarize_distribution(std::move(direct_retained));
    result.report.aligned_midpoint_retained_target_01 =
        summarize_distribution(std::move(aligned_retained));
    result.report.preserved_fixed_cell_count =
        static_cast<std::uint64_t>(std::ranges::count_if(
            fixed_shifts, [](const auto &value) { return value.has_value(); }));
    for (std::size_t index = 0U; index < node_count; ++index) {
        if (fixed_shifts[index].has_value()) {
            result.report.maximum_preserved_fixed_shift_error_samples =
                std::max(result.report.maximum_preserved_fixed_shift_error_samples,
                         std::abs(shifts[index] - *fixed_shifts[index]));
        }
    }
    result.identity_sha256 = phase_alignment_identity(result);
    return result;
}

HeldResult<HeldCookedGrid>
cook_held_texture_grid(const ResponsiveBakeProfile &profile,
                       const std::span<const HeldCookedCell> uncoalesced_cells,
                       const std::optional<HeldPriorPhaseAlignment> &prior) {
    using enum contract::ContractIssueCode;
    auto alignment_result = align_held_texture_grid(profile, uncoalesced_cells, prior);
    if (const auto *failure =
            std::get_if<contract::ValidationReport>(&alignment_result)) {
        return *failure;
    }
    auto coalesced_result = coalesce_duplicate_held_loads(uncoalesced_cells);
    if (const auto *failure =
            std::get_if<contract::ValidationReport>(&coalesced_result)) {
        return *failure;
    }
    HeldCookedGrid result;
    result.cells = std::get<std::vector<HeldCookedCell>>(std::move(coalesced_result));
    result.phase_alignment = std::get<HeldPhaseAlignment>(std::move(alignment_result));
    std::map<std::string, bool, std::less<>> retained;
    for (const auto &cell : result.cells) {
        retained.emplace(cell.id, true);
    }
    std::erase_if(result.phase_alignment.cells, [&](const HeldPhaseShift &cell) {
        return !retained.contains(cell.cell_id);
    });
    if (!retained.contains(result.phase_alignment.reference_cell_id) ||
        result.phase_alignment.cells.size() != result.cells.size()) {
        return one_issue(
            inconsistent_semantics, "phase_alignment",
            "coalesced held grid invalidated its phase-alignment reference");
    }
    result.phase_alignment.identity_sha256 =
        phase_alignment_identity(result.phase_alignment);
    result.identity_sha256 = held_grid_identity(profile, result);
    return result;
}

} // namespace engine_sim_offline::responsive
