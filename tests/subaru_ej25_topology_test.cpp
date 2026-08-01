#include "authored_engine_fixture_support.hpp"

#include "engine_sim_offline/contract/capture.hpp"
#include "simulation/low_order_capture_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

namespace contract = engine_sim_offline::contract;
namespace simulation = engine_sim_offline::simulation;
namespace test = engine_sim_offline::test;

constexpr double kLegacyPi = 3.14159265359;
constexpr double kDegreesToRadians = kLegacyPi / 180.0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error{std::string{message}};
    }
}

[[nodiscard]] bool near(double left, double right,
                        double tolerance = 1.0e-12) noexcept {
    return std::abs(left - right) <= tolerance;
}

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

[[nodiscard]] contract::Sha256Digest nonzero_request_identity() {
    contract::Sha256Digest identity;
    identity.bytes.back() = 1U;
    return identity;
}

[[nodiscard]] const contract::BankSpec &find_bank(const contract::EngineSpec &engine,
                                                  std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.banks, semantic_id, [](const auto &bank) {
            return std::string_view{bank.semantic_id.value};
        });
    if (found == engine.banks.end()) {
        throw std::runtime_error{"missing resolved bank " + std::string{semantic_id}};
    }
    return *found;
}

[[nodiscard]] const contract::CylinderSpec &
find_cylinder(const contract::EngineSpec &engine, std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.cylinders, semantic_id, [](const auto &cylinder) {
            return std::string_view{cylinder.semantic_id.value};
        });
    if (found == engine.cylinders.end()) {
        throw std::runtime_error{"missing resolved cylinder " +
                                 std::string{semantic_id}};
    }
    return *found;
}

[[nodiscard]] const contract::LegacyCylinderAssembly &
find_core_cylinder(const contract::LowOrderEngineCoreV1 &core,
                   contract::CylinderId id) {
    const auto found =
        std::ranges::find(core.mechanism.cylinders, id, [](const auto &cylinder) {
            return cylinder.topology.cylinder_id;
        });
    if (found == core.mechanism.cylinders.end()) {
        throw std::runtime_error{"resolved core omitted an EJ25 cylinder"};
    }
    return *found;
}

[[nodiscard]] std::size_t cylinder_index(const contract::EngineSpec &engine,
                                         std::string_view semantic_id) {
    const auto found =
        std::ranges::find(engine.cylinders, semantic_id, [](const auto &cylinder) {
            return std::string_view{cylinder.semantic_id.value};
        });
    if (found == engine.cylinders.end()) {
        throw std::runtime_error{"missing capture cylinder " +
                                 std::string{semantic_id}};
    }
    return static_cast<std::size_t>(found - engine.cylinders.begin());
}

void verify_resolved_topology(const test::AuthoredEngineFixture &fixture) {
    const auto &engine = fixture.engine;
    expect(engine.cylinder_layout.value == contract::CylinderLayoutKind::flat_engine,
           "opposed EJ25 did not resolve to the flat-engine contract kind");
    expect(engine.banks.size() == 2U && engine.cylinders.size() == 4U,
           "EJ25 bank or cylinder cardinality changed");

    const auto &positive = find_bank(engine, "bank-positive");
    const auto &negative = find_bank(engine, "bank-negative");
    expect(positive.angle_rad.has_value() && negative.angle_rad.has_value() &&
               near(positive.angle_rad->value, 90.0 * kDegreesToRadians) &&
               near(negative.angle_rad->value, -90.0 * kDegreesToRadians) &&
               near(std::abs(std::remainder(positive.angle_rad->value -
                                                negative.angle_rad->value,
                                            2.0 * kLegacyPi)),
                    kLegacyPi),
           "EJ25 antipodal bank axes were not retained explicitly");

    struct ExpectedCylinder {
        std::string_view id;
        std::string_view bank;
        double raw_journal_degrees;
        double effective_journal_degrees;
        double geometric_tdc_degrees;
    };
    constexpr std::array expected{
        ExpectedCylinder{"cylinder-1", "bank-positive", 0.0, -90.0, 0.0},
        ExpectedCylinder{"cylinder-3", "bank-positive", 180.0, 90.0, 180.0},
        ExpectedCylinder{"cylinder-2", "bank-negative", 180.0, 270.0, 0.0},
        ExpectedCylinder{"cylinder-4", "bank-negative", 0.0, 90.0, 180.0},
    };

    const auto &core = test::low_order_core(engine);
    expect(core.mechanism.cylinders.size() == expected.size(),
           "EJ25 executable mechanism cylinder count changed");
    for (const auto &item : expected) {
        const auto &cylinder = find_cylinder(engine, item.id);
        const auto &bank = find_bank(engine, item.bank);
        const auto &assembly = find_core_cylinder(core, cylinder.id);
        const auto &direct =
            std::get<contract::LegacyDirectJournalKinematics>(assembly.kinematics);
        expect(cylinder.bank_id == bank.id &&
                   near(cylinder.journal_phase_rad.value,
                        item.raw_journal_degrees * kDegreesToRadians) &&
                   near(direct.journal_angle_rad.value,
                        item.effective_journal_degrees * kDegreesToRadians),
               "EJ25 raw/effective journal phase mapping changed");
        const double geometric_tdc =
            std::remainder(core.mechanism.cranks.front().crank_tdc_reference_rad.value +
                               direct.journal_angle_rad.value - kLegacyPi / 2.0,
                           2.0 * kLegacyPi);
        expect(near(std::abs(geometric_tdc),
                    item.geometric_tdc_degrees * kDegreesToRadians),
               "EJ25 derived geometric TDC pairing changed");
    }
}

void verify_dynamic_capture(const test::AuthoredEngineFixture &fixture) {
    const auto horizon = contract::resolve_frame_index(
        fixture.scenario.total_duration_s.value, fixture.scenario.rates.physics);
    expect(horizon.has_value() && *horizon == 5400U,
           "EJ25 topology scenario did not retain its exact finite horizon");

    auto result = simulation::compile_low_order_capture_session(
        fixture.engine, fixture.scenario, test::compile_fixture_random_plan(fixture),
        nonzero_request_identity(),
        simulation::LowOrderExecutionExtent::finite_scenario(*horizon));
    if (const auto *report = std::get_if<contract::ValidationReport>(&result)) {
        throw std::runtime_error{"EJ25 runtime admission failed: " +
                                 validation_text(*report)};
    }
    auto session = std::get<simulation::LowOrderCaptureSession>(std::move(result));

    const auto cylinder_1 = cylinder_index(fixture.engine, "cylinder-1");
    const auto cylinder_2 = cylinder_index(fixture.engine, "cylinder-2");
    const auto cylinder_3 = cylinder_index(fixture.engine, "cylinder-3");
    const auto cylinder_4 = cylinder_index(fixture.engine, "cylinder-4");
    bool observed = false;
    std::uint64_t observed_frames = 0U;
    while (!session.completed()) {
        auto published =
            session.publish_next_block([&](const contract::CaptureBlockView &block) {
                const auto report =
                    contract::validate(block, fixture.engine, fixture.scenario);
                if (!report.ok()) {
                    throw std::runtime_error{"EJ25 capture block failed validation: " +
                                             validation_text(report)};
                }
                expect(block.frame_count() > 0U,
                       "EJ25 runtime published an empty capture block");
                for (std::size_t frame = 0U; frame < block.frame_count(); ++frame) {
                    const auto *one = block.cylinder_sample(frame, cylinder_1);
                    const auto *two = block.cylinder_sample(frame, cylinder_2);
                    const auto *three = block.cylinder_sample(frame, cylinder_3);
                    const auto *four = block.cylinder_sample(frame, cylinder_4);
                    expect(one != nullptr && two != nullptr && three != nullptr &&
                               four != nullptr,
                           "EJ25 runtime omitted a cylinder sample");
                    expect(one->chamber_volume_m3 == two->chamber_volume_m3 &&
                               one->chamber_dvolume_dtheta_m3_per_rad ==
                                   two->chamber_dvolume_dtheta_m3_per_rad &&
                               one->piston_velocity_m_s == two->piston_velocity_m_s,
                           "EJ25 zero-degree opposed piston pair diverged");
                    expect(three->chamber_volume_m3 == four->chamber_volume_m3 &&
                               three->chamber_dvolume_dtheta_m3_per_rad ==
                                   four->chamber_dvolume_dtheta_m3_per_rad &&
                               three->piston_velocity_m_s == four->piston_velocity_m_s,
                           "EJ25 180-degree opposed piston pair diverged");
                    if (!observed) {
                        expect(std::isfinite(one->chamber_volume_m3) &&
                                   one->chamber_volume_m3 > 0.0 &&
                                   one->chamber_volume_m3 != three->chamber_volume_m3,
                               "EJ25 two geometric TDC groups collapsed to one phase");
                    }
                    observed = true;
                }
                observed_frames += block.frame_count();
                return true;
            });
        if (const auto *failure = std::get_if<contract::FailureContext>(&published)) {
            throw std::runtime_error{"EJ25 capture faulted (" + failure->detail_code +
                                     "): " + failure->state_summary};
        }
    }
    expect(observed && observed_frames == *horizon &&
               session.published_sample_count() == *horizon && session.completed() &&
               !session.faulted(),
           "EJ25 finite dynamic capture did not complete its exact horizon");
}

void run(const std::filesystem::path &repository_root) {
    const auto fixture = test::load_authored_engine_fixture(
        repository_root, "data/engines/subaru-ej25-cleanroom/engine.json",
        "data/engines/subaru-ej25-cleanroom/scenarios/"
        "topology-free-engine-1500rpm.json");
    verify_resolved_topology(fixture);
    verify_dynamic_capture(fixture);
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error{"expected repository root argument"};
        }
        run(argv[1]);
    } catch (const std::exception &error) {
        std::cerr << "Subaru EJ25 topology failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
