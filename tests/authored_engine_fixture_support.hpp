#pragma once

#include "crankwave/contract/engine.hpp"
#include "crankwave/contract/presentation.hpp"
#include "crankwave/contract/provenance.hpp"
#include "crankwave/contract/randomness.hpp"
#include "crankwave/contract/scenario.hpp"

#include <filesystem>
#include <vector>

namespace crankwave::test {

struct AuthoredEngineFixture {
    contract::EngineSpec engine;
    contract::PresentationCalibration presentation;
    contract::ResolvedRandomnessPolicy randomness;
    contract::RenderScenario scenario;
    contract::ProvenanceBundleRef provenance_bundle;
};

// Loads the canonical authored migration fixture only through JSON parse and generic
// compilation.
[[nodiscard]] AuthoredEngineFixture
load_canonical_authored_engine_fixture(const std::filesystem::path &repository_root);

[[nodiscard]] AuthoredEngineFixture
load_authored_engine_fixture(const std::filesystem::path &repository_root,
                             const std::filesystem::path &engine_relative_path,
                             const std::filesystem::path &scenario_relative_path);

// Replaces only motion ownership with a fixed-rate prescribed RPM lane. Callers
// remain responsible for any test-specific horizon, preparation, controls, or ID.
[[nodiscard]] AuthoredEngineFixture
make_prescribed_fixture(const AuthoredEngineFixture &canonical,
                        std::vector<double> post_step_rpm);

[[nodiscard]] contract::RandomPlan
compile_fixture_random_plan(const AuthoredEngineFixture &fixture);

[[nodiscard]] contract::RandomPlan
compile_fixture_random_plan(const AuthoredEngineFixture &fixture,
                            const contract::EngineSpec &engine,
                            const contract::RenderScenario &scenario);

[[nodiscard]] contract::LowOrderEngineCoreV1 &
low_order_core(contract::EngineSpec &engine);

[[nodiscard]] const contract::LowOrderEngineCoreV1 &
low_order_core(const contract::EngineSpec &engine);

[[nodiscard]] contract::LowOrderOperatingPointV1Profile &
operating_profile(contract::EngineSpec &engine);

[[nodiscard]] const contract::LowOrderOperatingPointV1Profile &
operating_profile(const contract::EngineSpec &engine);

} // namespace crankwave::test
