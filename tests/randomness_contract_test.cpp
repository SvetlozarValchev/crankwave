#include "contract_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>

namespace crankwave::contract::test {
namespace {

constexpr Sha256Digest kExpectedPcg32MethodConfiguration{{
    0xe9, 0x86, 0xac, 0x47, 0x97, 0x1f, 0xa1, 0xd6, 0xab, 0x27, 0xf8,
    0xe9, 0x5f, 0x86, 0x2a, 0x29, 0x3e, 0x54, 0xec, 0xad, 0xfb, 0xe0,
    0x09, 0xb1, 0x74, 0xe8, 0x9f, 0x5b, 0x90, 0x57, 0x5d, 0x70,
}};
constexpr Sha256Digest kExpectedDerivationMethodConfiguration{{
    0x31, 0x3c, 0x5e, 0xaf, 0xaf, 0xae, 0x9b, 0x65, 0x2d, 0x01, 0xc2,
    0x14, 0x7c, 0x31, 0xb3, 0x65, 0xfc, 0x4e, 0x51, 0xf2, 0x8e, 0xe6,
    0xb3, 0x5c, 0x7e, 0x43, 0x54, 0x49, 0xed, 0x0d, 0x79, 0xf4,
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
    if (policy.generator.value.configuration_sha256 !=
            kExpectedPcg32MethodConfiguration ||
        policy.derivation.value.configuration_sha256 !=
            kExpectedDerivationMethodConfiguration) {
        constexpr std::string_view digits = "0123456789abcdef";
        for (const auto &value : {policy.generator.value.configuration_sha256,
                                  policy.derivation.value.configuration_sha256}) {
            for (const auto byte : value.bytes) {
                std::cerr << digits[byte >> 4U] << digits[byte & UINT8_C(0x0f)];
            }
            std::cerr << '\n';
        }
    }
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
                 UINT64_C(0x638e648507353211), UINT64_C(0x09d037c702feb066)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{1},
                 UINT64_C(0x0d172bd0b6609980), UINT64_C(0x40bb189b0ce745fe)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{1},
                 UINT64_C(0xa7cd663a89695273), UINT64_C(0x0794616d5c2a0127)},
            },
        "canonical provisioned component inventory, order, or seed values changed");

    auto operating_inputs = content.inputs.resolved;
    const auto operating_random_plan =
        require_random_plan(operating_inputs.randomness, operating_inputs.engine,
                            operating_inputs.presentation, operating_inputs.scenario);
    expect(operating_random_plan.component_seeds == content.randomness.component_seeds,
           "copying operating-profile inputs changed random ownership");

    auto multi_owner_inputs = content.inputs.resolved;
    for (std::uint32_t id = 2; id <= 6; ++id) {
        auto cylinder = multi_owner_inputs.engine.cylinders.front();
        cylinder.id = CylinderId{id};
        multi_owner_inputs.engine.cylinders.push_back(std::move(cylinder));
    }
    auto route_2 = multi_owner_inputs.presentation.routes.front();
    route_2.route_id = RouteId{2};
    multi_owner_inputs.presentation.routes.push_back(std::move(route_2));
    auto engine_route_2 = multi_owner_inputs.engine.routes.front();
    engine_route_2.id = RouteId{2};
    multi_owner_inputs.engine.routes.push_back(std::move(engine_route_2));

    const auto multi_owner_plan = require_random_plan(
        multi_owner_inputs.randomness, multi_owner_inputs.engine,
        multi_owner_inputs.presentation, multi_owner_inputs.scenario);
    expect(
        multi_owner_plan.component_seeds ==
            std::vector<ComponentSeed>{
                {RandomComponentKind::combustion, CylinderId{1}, std::nullopt,
                 UINT64_C(0x638e648507353211), UINT64_C(0x09d037c702feb066)},
                {RandomComponentKind::combustion, CylinderId{2}, std::nullopt,
                 UINT64_C(0x8378fa334d97535e), UINT64_C(0x45f2daf9901678c3)},
                {RandomComponentKind::combustion, CylinderId{3}, std::nullopt,
                 UINT64_C(0xb7f998bfc604914e), UINT64_C(0x0ca67d0274c3a0a2)},
                {RandomComponentKind::combustion, CylinderId{4}, std::nullopt,
                 UINT64_C(0x0d3eccc412acfd53), UINT64_C(0x1213a1f55f64b73e)},
                {RandomComponentKind::combustion, CylinderId{5}, std::nullopt,
                 UINT64_C(0x74774e667b6044c3), UINT64_C(0x67c5e156e65edc4e)},
                {RandomComponentKind::combustion, CylinderId{6}, std::nullopt,
                 UINT64_C(0x8ee0a57a30066164), UINT64_C(0x798f3346d42f6acd)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{1},
                 UINT64_C(0x0d172bd0b6609980), UINT64_C(0x40bb189b0ce745fe)},
                {RandomComponentKind::presentation_air_noise, std::nullopt, RouteId{2},
                 UINT64_C(0xfe16c9e3ea44a31a), UINT64_C(0x311ac0d4f16c0d57)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{1},
                 UINT64_C(0xa7cd663a89695273), UINT64_C(0x0794616d5c2a0127)},
                {RandomComponentKind::presentation_jitter, std::nullopt, RouteId{2},
                 UINT64_C(0x0fcabae05f0d4195), UINT64_C(0x26228b61f534bb77)},
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
    expect(require_random_plan(reordered_inputs.randomness, reordered_inputs.engine,
                               reordered_inputs.presentation,
                               reordered_inputs.scenario) == multi_owner_plan,
           "container reordering changed stable-owner plan identity");

    auto zero_scale_inputs = multi_owner_inputs;
    auto &zero_profile = std::get<LowOrderOperatingPointV1Profile>(
        zero_scale_inputs.engine.physics_profile);
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
    auto route_3 = inserted_inputs.presentation.routes.front();
    route_3.route_id = RouteId{3};
    inserted_inputs.presentation.routes.insert(
        inserted_inputs.presentation.routes.begin(), std::move(route_3));
    auto engine_route_3 = inserted_inputs.engine.routes.front();
    engine_route_3.id = RouteId{3};
    inserted_inputs.engine.routes.insert(inserted_inputs.engine.routes.begin(),
                                         std::move(engine_route_3));
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
                                     "randomness.component_seeds"),
           "seed namespace changed without invalidating the executable random plan");
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
}

} // namespace crankwave::contract::test
