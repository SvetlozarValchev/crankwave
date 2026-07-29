#include "contract_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace engine_sim_offline::contract::test {
namespace {

constexpr Sha256Digest kExpectedPcg32MethodConfiguration{{
    0xa4, 0x83, 0x83, 0xd2, 0x71, 0x6a, 0x05, 0x9b, 0x0b, 0x60, 0xaa,
    0xbf, 0x4c, 0x6f, 0xeb, 0xb0, 0xa8, 0x1e, 0xdb, 0xad, 0x62, 0x2d,
    0xa4, 0x63, 0x48, 0x23, 0xbf, 0x22, 0x42, 0x1e, 0xaf, 0x0c,
}};
constexpr Sha256Digest kExpectedDerivationMethodConfiguration{{
    0x0e, 0x86, 0xea, 0x38, 0xfb, 0x2e, 0x68, 0x1b, 0xb6, 0x46, 0x3b,
    0x39, 0x16, 0xc6, 0xad, 0x30, 0x53, 0x6f, 0x6a, 0xf0, 0x5c, 0xb0,
    0xbe, 0x1b, 0x00, 0xd3, 0xca, 0xcb, 0x76, 0x35, 0x93, 0xb4,
}};

[[nodiscard]] bool has_issue(const ValidationReport &report, ContractIssueCode code,
                             std::string_view path_fragment) {
    return std::ranges::any_of(report.issues, [&](const ContractIssue &issue) {
        return issue.code == code &&
               issue.path.find(path_fragment) != std::string::npos;
    });
}

[[nodiscard]] RandomPlan
require_random_plan(const ResolvedRandomnessPolicy &policy, const EngineSpec &engine,
                    const RenderScenario &scenario) {
    auto result = compile_random_plan(policy, engine, scenario);
    expect(std::holds_alternative<RandomPlan>(result),
           "valid combustion random plan failed compilation");
    return std::get<RandomPlan>(std::move(result));
}

[[nodiscard]] const CombustionSeed *find_seed(const RandomPlan &plan,
                                               std::uint32_t cylinder_id) {
    const auto found = std::ranges::find(
        plan.combustion_seeds, CylinderId{cylinder_id},
        &CombustionSeed::cylinder_id);
    return found == plan.combustion_seeds.end() ? nullptr : &*found;
}

} // namespace

void run_randomness_contract_tests() {
    InputBuilder builder;
    const auto policy = make_randomness_policy(builder);
    expect(validate(policy, builder.provenance).ok(),
           "valid resolved randomness policy was rejected");
    expect(policy.generator.value == pcg32_generator_method_identity() &&
               policy.derivation.value == component_seed_derivation_method_identity(),
           "fixture randomness policy did not use the admitted exact methods");
    expect(policy.generator.value.configuration_sha256 ==
                   kExpectedPcg32MethodConfiguration &&
               policy.derivation.value.configuration_sha256 ==
                   kExpectedDerivationMethodConfiguration,
           "randomness method descriptor identity changed");

    auto malformed = policy;
    malformed.seed_namespace_id.value = "Invalid Namespace";
    auto report = validate(malformed, builder.provenance);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "seed_namespace_id.value"),
           "noncanonical seed namespace was accepted");

    malformed = policy;
    malformed.generator.resolution_id.clear();
    report = validate(malformed, builder.provenance);
    expect(!report.ok() &&
               has_issue(report, ContractIssueCode::invalid_value,
                         "generator.resolution_id") &&
               has_issue(report, ContractIssueCode::dangling_reference,
                         "generator.resolution_id"),
           "missing generator resolution was accepted");

    malformed = policy;
    malformed.generator.resolution_id = policy.derivation.resolution_id;
    report = validate(malformed, builder.provenance);
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "generator.resolution_id"),
           "derivation resolution was allowed to authorize the generator");

    malformed = policy;
    malformed.generator.value.id = "Invalid Method";
    report = validate(malformed, builder.provenance);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "generator.value.id"),
           "invalid generator method identity was accepted");

    malformed = policy;
    malformed.derivation.value.configuration_sha256 = {};
    report = validate(malformed, builder.provenance);
    expect(!report.ok() && has_issue(report, ContractIssueCode::invalid_value,
                                     "derivation.value.configuration_sha256"),
           "unidentified derivation configuration was accepted");

    malformed = policy;
    malformed.generator.value.configuration_sha256 = digest(99);
    report = validate(malformed, builder.provenance);
    expect(!report.ok() && has_issue(report, ContractIssueCode::unsupported_value,
                                     "generator.value"),
           "structurally valid but unimplemented generator identity was accepted");

    const auto engine = make_engine(builder);
    const auto scenario = make_scenario(builder, engine);
    const auto single_plan = require_random_plan(policy, engine, scenario);
    expect(single_plan.combustion_seeds ==
               std::vector<CombustionSeed>{
                   {CylinderId{1}, UINT64_C(0x6ba3d060370e05fa),
                    UINT64_C(0x3e13b1e68ef2f790)},
               },
           "canonical one-cylinder combustion seed changed");

    auto multi_engine = engine;
    auto &multi_profile =
        std::get<LegacyLowOrderV1Profile>(multi_engine.physics_profile);
    constexpr std::array<std::uint64_t, 6> bmw_initial_states{
        UINT64_C(0x6ba3d060370e05fa), UINT64_C(0xb1ab9b6c6217bdf3),
        UINT64_C(0x0c2447917cd77f40), UINT64_C(0xfc83080b6c8b1a98),
        UINT64_C(0x1f0c63f1d677237b), UINT64_C(0xad811f42fb6dafa3),
    };
    constexpr std::array<std::uint64_t, 6> bmw_streams{
        UINT64_C(0x3e13b1e68ef2f790), UINT64_C(0x7681d4f9a6c78e3f),
        UINT64_C(0x4c09e08d851104f5), UINT64_C(0x686f68f85fd7d169),
        UINT64_C(0x3507d87731683125), UINT64_C(0x50900fae5afa96cf),
    };
    for (std::uint32_t id = 2; id <= 6; ++id) {
        auto cylinder = multi_engine.cylinders.front();
        cylinder.id = CylinderId{id};
        multi_engine.cylinders.push_back(std::move(cylinder));

        auto stream = multi_profile.core.combustion_random_streams.front();
        stream.cylinder_id = CylinderId{id};
        stream.pcg32_initial_state.value = bmw_initial_states[id - 1U];
        stream.pcg32_stream.value = bmw_streams[id - 1U];
        multi_profile.core.combustion_random_streams.push_back(std::move(stream));
    }

    const auto multi_plan = require_random_plan(policy, multi_engine, scenario);
    expect(multi_plan.combustion_seeds ==
               std::vector<CombustionSeed>{
                   {CylinderId{1}, bmw_initial_states[0], bmw_streams[0]},
                   {CylinderId{2}, bmw_initial_states[1], bmw_streams[1]},
                   {CylinderId{3}, bmw_initial_states[2], bmw_streams[2]},
                   {CylinderId{4}, bmw_initial_states[3], bmw_streams[3]},
                   {CylinderId{5}, bmw_initial_states[4], bmw_streams[4]},
                   {CylinderId{6}, bmw_initial_states[5], bmw_streams[5]},
               },
           "BMW combustion seed inventory, order, or values changed");

    auto reordered_engine = multi_engine;
    std::ranges::reverse(reordered_engine.cylinders);
    std::ranges::reverse(
        std::get<LegacyLowOrderV1Profile>(reordered_engine.physics_profile)
            .core.combustion_random_streams);
    expect(require_random_plan(policy, reordered_engine, scenario) == multi_plan,
           "container reordering changed stable-cylinder plan identity");

    auto zero_scale_engine = multi_engine;
    std::get<LegacyLowOrderV1Profile>(zero_scale_engine.physics_profile)
        .core.fuel.burning_efficiency_randomness_01.value = 0.0;
    expect(require_random_plan(policy, zero_scale_engine, scenario) == multi_plan,
           "zero combustion scale hid streams instantiated by the executor");

    auto inserted_engine = multi_engine;
    auto cylinder_7 = inserted_engine.cylinders.front();
    cylinder_7.id = CylinderId{7};
    inserted_engine.cylinders.insert(inserted_engine.cylinders.begin(),
                                     std::move(cylinder_7));
    auto &inserted_profile =
        std::get<LegacyLowOrderV1Profile>(inserted_engine.physics_profile);
    const auto inserted_derivation = derive_component_seeds({
        policy.seed_namespace_id.value,
        scenario.public_seed.value,
        {{"combustion", 6}},
    });
    expect(std::holds_alternative<ComponentSeedDerivation>(inserted_derivation),
           "inserted stable cylinder seed derivation failed");
    const auto inserted_initialization =
        std::get<ComponentSeedDerivation>(inserted_derivation)
            .ordered_components.front()
            .initialization;
    auto stream_7 = inserted_profile.core.combustion_random_streams.front();
    stream_7.cylinder_id = CylinderId{7};
    stream_7.pcg32_initial_state.value = inserted_initialization.initial_state;
    stream_7.pcg32_stream.value = inserted_initialization.stream;
    inserted_profile.core.combustion_random_streams.insert(
        inserted_profile.core.combustion_random_streams.begin(),
        std::move(stream_7));
    const auto inserted_plan = require_random_plan(policy, inserted_engine, scenario);
    for (const auto &prior : multi_plan.combustion_seeds) {
        const auto *inserted = find_seed(inserted_plan, prior.cylinder_id.value);
        expect(inserted != nullptr && *inserted == prior,
               "inserting a stable cylinder rekeyed an existing stream");
    }

    auto fabricated_engine = engine;
    ++std::get<LegacyLowOrderV1Profile>(fabricated_engine.physics_profile)
          .core.combustion_random_streams.front()
          .pcg32_initial_state.value;
    const auto fabricated = compile_random_plan(policy, fabricated_engine, scenario);
    expect(std::holds_alternative<ValidationReport>(fabricated) &&
               has_issue(std::get<ValidationReport>(fabricated),
                         ContractIssueCode::inconsistent_semantics,
                         "combustion_random_streams"),
           "fabricated cached combustion initialization bypassed derivation");
}

} // namespace engine_sim_offline::contract::test
