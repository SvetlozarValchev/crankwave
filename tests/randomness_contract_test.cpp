#include "contract_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
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
                    const PresentationCalibration &presentation,
                    const RenderScenario &scenario) {
    auto result = compile_random_plan(policy, engine, presentation, scenario);
    expect(std::holds_alternative<RandomPlan>(result),
           "valid multi-owner random plan failed compilation");
    return std::get<RandomPlan>(std::move(result));
}

[[nodiscard]] const ComponentSeed *
find_seed(const RandomPlan &plan, RandomComponentKind kind, std::uint32_t owner_id) {
    const auto found =
        std::ranges::find_if(plan.component_seeds, [&](const ComponentSeed &seed) {
            const auto actual_owner = seed.cylinder_id.has_value()
                                          ? seed.cylinder_id->value
                                          : seed.route_id->value;
            return seed.kind == kind && actual_owner == owner_id;
        });
    return found == plan.component_seeds.end() ? nullptr : &*found;
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

    InputBuilder manifest_builder;
    const auto content = make_manifest_content(manifest_builder);
    expect(validate(content, manifest_builder.provenance, make_source_matrix()).ok(),
           "valid manifest randomness binding was rejected");
    expect(
        content.randomness.component_seeds ==
            std::vector<ComponentSeed>{
                {RandomComponentKind::combustion, CylinderId{1}, std::nullopt,
                 UINT64_C(0x6ba3d060370e05fa), UINT64_C(0x3e13b1e68ef2f790)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{1},
                 UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{1},
                 UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
            },
        "canonical provisioned component inventory, order, or seed values changed");

    auto multi_owner_inputs = content.inputs.resolved;
    auto &multi_owner_profile =
        std::get<LegacyLowOrderV1Profile>(multi_owner_inputs.engine.physics_profile);
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
        auto cylinder = multi_owner_inputs.engine.cylinders.front();
        cylinder.id = CylinderId{id};
        multi_owner_inputs.engine.cylinders.push_back(std::move(cylinder));

        auto stream = multi_owner_profile.core.combustion_random_streams.front();
        stream.cylinder_id = CylinderId{id};
        stream.pcg32_initial_state.value = bmw_initial_states[id - 1U];
        stream.pcg32_stream.value = bmw_streams[id - 1U];
        multi_owner_profile.core.combustion_random_streams.push_back(
            std::move(stream));
    }
    auto route_2 = multi_owner_inputs.presentation.routes.front();
    route_2.route_id = RouteId{2};
    multi_owner_inputs.presentation.routes.push_back(std::move(route_2));

    const auto multi_owner_plan = require_random_plan(
        multi_owner_inputs.randomness, multi_owner_inputs.engine,
        multi_owner_inputs.presentation, multi_owner_inputs.scenario);
    expect(
        multi_owner_plan.component_seeds ==
            std::vector<ComponentSeed>{
                {RandomComponentKind::combustion, CylinderId{1}, std::nullopt,
                 UINT64_C(0x6ba3d060370e05fa), UINT64_C(0x3e13b1e68ef2f790)},
                {RandomComponentKind::combustion, CylinderId{2}, std::nullopt,
                 UINT64_C(0xb1ab9b6c6217bdf3), UINT64_C(0x7681d4f9a6c78e3f)},
                {RandomComponentKind::combustion, CylinderId{3}, std::nullopt,
                 UINT64_C(0x0c2447917cd77f40), UINT64_C(0x4c09e08d851104f5)},
                {RandomComponentKind::combustion, CylinderId{4}, std::nullopt,
                 UINT64_C(0xfc83080b6c8b1a98), UINT64_C(0x686f68f85fd7d169)},
                {RandomComponentKind::combustion, CylinderId{5}, std::nullopt,
                 UINT64_C(0x1f0c63f1d677237b), UINT64_C(0x3507d87731683125)},
                {RandomComponentKind::combustion, CylinderId{6}, std::nullopt,
                 UINT64_C(0xad811f42fb6dafa3), UINT64_C(0x50900fae5afa96cf)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{1},
                 UINT64_C(0x75bc579d4c90a640), UINT64_C(0x7e4ef6200e7c70c1)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{2},
                 UINT64_C(0x208e57f73615bd95), UINT64_C(0x786d92e584c43b78)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{1},
                 UINT64_C(0x9e2b91cd0dc51cfc), UINT64_C(0x1ae6ee3019603abb)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{2},
                 UINT64_C(0xdb7540a0c8b54d74), UINT64_C(0x41ddcdeb066bf214)},
            },
        "multi-owner canonical plan order or BMW seed mapping changed");
    expect(std::ranges::none_of(multi_owner_plan.component_seeds,
                                [](const ComponentSeed &seed) {
                                    return seed.kind == RandomComponentKind::starter;
                                }),
           "unimplemented starter randomness was claimed as provisioned");

    auto reordered_inputs = multi_owner_inputs;
    std::ranges::reverse(reordered_inputs.engine.cylinders);
    std::ranges::reverse(reordered_inputs.presentation.routes);
    std::ranges::reverse(
        std::get<LegacyLowOrderV1Profile>(reordered_inputs.engine.physics_profile)
            .core.combustion_random_streams);
    expect(require_random_plan(reordered_inputs.randomness, reordered_inputs.engine,
                               reordered_inputs.presentation,
                               reordered_inputs.scenario) == multi_owner_plan,
           "container reordering changed stable-owner plan identity");

    auto zero_scale_inputs = multi_owner_inputs;
    auto &zero_profile =
        std::get<LegacyLowOrderV1Profile>(zero_scale_inputs.engine.physics_profile);
    zero_profile.core.fuel.burning_efficiency_randomness_01.value = 0.0;
    zero_scale_inputs.presentation.conditioning.air_noise_mix_01.value = 0.0;
    zero_scale_inputs.presentation.conditioning.jitter_scale.value = 0.0;
    expect(require_random_plan(zero_scale_inputs.randomness, zero_scale_inputs.engine,
                               zero_scale_inputs.presentation,
                               zero_scale_inputs.scenario) == multi_owner_plan,
           "zero scale hid streams that current executors still instantiate");

    auto inserted_inputs = multi_owner_inputs;
    auto cylinder_7 = inserted_inputs.engine.cylinders.front();
    cylinder_7.id = CylinderId{7};
    inserted_inputs.engine.cylinders.insert(inserted_inputs.engine.cylinders.begin(),
                                            std::move(cylinder_7));
    auto &inserted_profile =
        std::get<LegacyLowOrderV1Profile>(inserted_inputs.engine.physics_profile);
    const auto inserted_derivation = derive_component_seeds({
        inserted_inputs.randomness.seed_namespace_id.value,
        inserted_inputs.scenario.public_seed.value,
        {{"combustion", 6}},
    });
    expect(std::holds_alternative<ComponentSeedDerivation>(inserted_derivation),
           "inserted stable owner seed derivation failed");
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
    auto route_3 = inserted_inputs.presentation.routes.front();
    route_3.route_id = RouteId{3};
    inserted_inputs.presentation.routes.insert(
        inserted_inputs.presentation.routes.begin(), std::move(route_3));
    const auto inserted_plan =
        require_random_plan(inserted_inputs.randomness, inserted_inputs.engine,
                            inserted_inputs.presentation, inserted_inputs.scenario);
    for (const auto &prior : multi_owner_plan.component_seeds) {
        const auto owner_id = prior.cylinder_id.has_value() ? prior.cylinder_id->value
                                                            : prior.route_id->value;
        const auto *inserted = find_seed(inserted_plan, prior.kind, owner_id);
        expect(inserted != nullptr && *inserted == prior,
               "inserting a new stable owner rekeyed an existing stream");
    }

    auto generator_drift = content;
    generator_drift.randomness.generator.id += "-drift";
    report =
        validate(generator_drift, manifest_builder.provenance, make_source_matrix());
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "randomness.generator"),
           "executed generator drifted from the resolved policy");

    auto derivation_drift = content;
    ++derivation_drift.randomness.derivation.version;
    report =
        validate(derivation_drift, manifest_builder.provenance, make_source_matrix());
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "randomness.derivation"),
           "executed derivation drifted from the resolved policy");

    RenderManifest first{content, std::nullopt};
    auto changed_content = content;
    changed_content.inputs.resolved.randomness.seed_namespace_id.value += ".variant";
    report =
        validate(changed_content, manifest_builder.provenance, make_source_matrix());
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "combustion_random_streams"),
           "seed namespace changed without invalidating cached executable seeds");
    RenderManifest second{changed_content, std::nullopt};
    expect(!same_content_identity(first, second),
           "seed namespace failed to participate in render content identity");

    auto fake_manifest_seed = content;
    ++fake_manifest_seed.randomness.component_seeds.front().initial_state;
    report =
        validate(fake_manifest_seed, manifest_builder.provenance, make_source_matrix());
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "randomness.component_seeds"),
           "manifest accepted a fabricated combustion initialization");

    auto coordinated_fake_seed = content;
    auto &stored_stream =
        std::get<LegacyLowOrderV1Profile>(
            coordinated_fake_seed.inputs.resolved.engine.physics_profile)
            .core.combustion_random_streams.front();
    ++stored_stream.pcg32_initial_state.value;
    ++coordinated_fake_seed.randomness.component_seeds.front().initial_state;
    report = validate(coordinated_fake_seed, manifest_builder.provenance,
                      make_source_matrix());
    expect(!report.ok() && has_issue(report, ContractIssueCode::inconsistent_semantics,
                                     "combustion_random_streams"),
           "coordinated engine/manifest seed fabrication bypassed canonical "
           "derivation");
}

} // namespace engine_sim_offline::contract::test
