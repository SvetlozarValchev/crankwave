#include "engine_sim_offline/profiles/bmw_m52b28_operating_profile.hpp"

#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

using namespace engine_sim_offline;

constexpr std::string_view kOperatingRoot =
    "engine.physics.low-order-operating-point-v1";
constexpr std::string_view kLegacyRoot =
    "engine.physics.legacy-low-order-v1";
constexpr std::string_view kOperatingResolutionPrefix =
    "bmw-m52b28-operating-profile-resolution-";
constexpr std::string_view kOperatingProvenanceDigestGrammar =
    "engine-sim-offline.bmw-m52b28-operating-profile-provenance-ledger-digest.v1";

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] =
            kDigits[digest.bytes[index] & UINT8_C(0x0f)];
    }
    return result;
}

[[nodiscard]] contract::Sha256Digest file_sha256(const char *path) {
    std::ifstream input{path, std::ios::binary};
    expect(input.is_open(), "required operating-profile evidence could not be opened");
    const std::string bytes{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    expect(!input.bad(),
           "required operating-profile evidence could not be read completely");
    return contract::sha256(
        std::as_bytes(std::span<const char>{bytes.data(), bytes.size()}));
}

[[nodiscard]] profiles::BmwM52b28OperatingProfile make_exact_profile() {
    auto result = profiles::make_bmw_m52b28_operating_profile();
    const auto *profile =
        std::get_if<profiles::BmwM52b28OperatingProfile>(&result);
    if (profile == nullptr) {
        const auto &report = std::get<contract::ValidationReport>(result);
        for (const auto &issue : report.issues) {
            std::cerr << issue.path << ": " << issue.message << '\n';
        }
        throw std::runtime_error{"canonical BMW operating profile did not validate"};
    }
    return *profile;
}

[[nodiscard]] const contract::LowOrderOperatingPointV1Profile &
operating_profile(const profiles::BmwM52b28OperatingProfile &profile) {
    const auto *operating =
        std::get_if<contract::LowOrderOperatingPointV1Profile>(
            &profile.engine.physics_profile);
    expect(operating != nullptr,
           "canonical BMW profile has the wrong physics alternative");
    return *operating;
}

[[nodiscard]] contract::Sha256Digest digest_from_evidence(
    const profiles::BmwM52b28OperatingProfile &profile,
    std::string_view evidence_id) {
    const auto evidence =
        std::ranges::find(profile.provenance.evidence, evidence_id,
                          &contract::EvidenceSource::id);
    expect(evidence != profile.provenance.evidence.end() &&
               evidence->content_sha256.has_value(),
           "required operating-profile evidence digest is absent");
    return *evidence->content_sha256;
}

template <class Mutation>
void expect_mutation_rejected(const profiles::BmwM52b28OperatingProfile &exact,
                              const char *message, Mutation &&mutation) {
    auto changed = exact;
    std::forward<Mutation>(mutation)(changed);
    expect(!profiles::validate_bmw_m52b28_operating_profile(changed).ok(),
           message);
}

[[nodiscard]] bool starts_with(std::string_view value,
                               std::string_view prefix) {
    return value.starts_with(prefix);
}

[[nodiscard]] bool is_legacy_profile_specific_suffix(
    std::string_view suffix) {
    return suffix == ".mechanism.crank.fixed_crank_friction_magnitude_nm" ||
           suffix == ".losses.included_terms" ||
           suffix == ".losses.omitted_terms";
}

[[nodiscard]] bool is_operating_profile_specific_suffix(
    std::string_view suffix) {
    return suffix.starts_with(".aggregate_loss.") ||
           suffix.starts_with(".accessory_configuration.") ||
           suffix.starts_with(".starter.") || suffix == ".cycle_quadrature";
}

[[nodiscard]] std::set<std::string>
core_resolution_suffixes(const contract::ProvenanceLedger &provenance,
                         std::string_view root, bool legacy) {
    std::set<std::string> result;
    for (const auto &resolution : provenance.resolutions) {
        if (!starts_with(resolution.parameter_path, root)) {
            continue;
        }
        const auto suffix =
            std::string_view{resolution.parameter_path}.substr(root.size());
        if ((legacy && is_legacy_profile_specific_suffix(suffix)) ||
            (!legacy && is_operating_profile_specific_suffix(suffix))) {
            continue;
        }
        expect(result.emplace(suffix).second,
               "duplicate core resolution suffix was admitted");
    }
    return result;
}

void test_exact_profile_authorities(
    const profiles::BmwM52b28OperatingProfile &profile) {
    expect(profiles::validate_bmw_m52b28_operating_profile(profile).ok(),
           "canonical BMW operating profile failed exact validation");
    expect(profile.engine.engine_id.value == "bmw-m52b28" &&
               profile.engine.profile_id.value ==
                   "bmw-m52b28-low-order-operating-point-v1" &&
               profile.engine.provenance_schema_id ==
                   "engine-sim-offline.bmw-m52b28-operating-profile-"
                   "provenance.v1" &&
               profile.provenance.schema_id ==
                   profile.engine.provenance_schema_id &&
               profile.provenance.bundle.id ==
                   "bmw-m52b28-low-order-operating-point-v1-provenance",
           "canonical BMW operating identity changed");

    constexpr std::string_view kExpectedBundleSha256 =
        "e68b08c059ffcf6f107572d1291c3f71ac151c21ba5646453f3fcd1677073b0b";
    const auto actual_bundle_sha256 = digest_hex(profile.provenance.bundle.sha256);
    if (actual_bundle_sha256 != kExpectedBundleSha256) {
        std::cerr << "BMW operating profile provenance SHA-256: "
                  << actual_bundle_sha256 << '\n';
    }
    expect(actual_bundle_sha256 == kExpectedBundleSha256,
           "BMW operating provenance bundle identity changed");
    expect(contract::canonical_provenance_ledger_digest(
               profile.provenance, kOperatingProvenanceDigestGrammar) ==
               profile.provenance.bundle.sha256,
           "BMW operating provenance bundle is not its canonical self-seal");

    const auto &operating = operating_profile(profile);
    const auto &loss = operating.aggregate_loss;
    expect(std::bit_cast<std::uint64_t>(loss.constant_fmep_bar.value) ==
                   UINT64_C(0x3fd999999999999a) &&
               std::bit_cast<std::uint64_t>(
                   loss.peak_pressure_coefficient.value) ==
                   UINT64_C(0x3f747ae147ae147b) &&
               std::bit_cast<std::uint64_t>(
                   loss.mean_piston_speed_coefficient_bar_s_per_m.value) ==
                   UINT64_C(0x3fb70a3d70a3d70a) &&
               std::bit_cast<std::uint64_t>(
                   loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2
                       .value) == UINT64_C(0x3f4d7dbf487fcb92) &&
               std::bit_cast<std::uint64_t>(
                   loss.required_oil_temperature_k.value) ==
                   UINT64_C(0x4076b26666666666),
           "canonical BMW loss tuple or oil condition changed");
    expect(loss.included_terms.value == UINT64_C(0x7e) &&
               operating.starter.mechanically_disengaged.value &&
               operating.starter.included_terms.value == UINT64_C(0x80) &&
               (contract::indicated_gas_torque_term_mask() |
                loss.included_terms.value |
                operating.starter.included_terms.value) == UINT64_C(0xff),
           "canonical BMW torque accounting partition changed");

    constexpr std::string_view kAccessorySha256 =
        "ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97";
    expect(operating.accessory_configuration.configuration_id.value ==
                   "bmw-m52b28-warm-stock-accessories-v1" &&
               digest_hex(
                   operating.accessory_configuration.content_sha256.value) ==
                   kAccessorySha256 &&
               digest_hex(digest_from_evidence(
                   profile, "operating-accessory-configuration")) ==
                   kAccessorySha256,
           "canonical BMW accessory descriptor binding changed");

    const auto &implemented =
        simulation::implemented_cycle_accounting_method_identities();
    expect(operating.cycle_quadrature.value == implemented.cycle_quadrature &&
               profile.engine.methods.losses.value ==
                   implemented.aggregate_loss &&
               digest_hex(operating.cycle_quadrature.value
                              .configuration_sha256) ==
                   "57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac" &&
               digest_hex(profile.engine.methods.losses.value
                              .configuration_sha256) ==
                   "6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489",
           "canonical BMW cycle-accounting method authority changed");

    const contract::TorqueCapability expected_capability{
        {
            contract::Availability::unavailable,
            contract::Completeness::incomplete,
            0,
            0,
        },
        {
            contract::Availability::available,
            contract::Completeness::complete,
            UINT64_C(0xff),
            0,
        },
        false,
    };
    expect(profile.engine.torque_capability.value == expected_capability,
           "canonical BMW torque capability changed");

    const auto admitted_m3_method = std::ranges::find(
        profile.provenance.evidence,
        std::string{"admitted-m3-core-method-configuration"},
        &contract::EvidenceSource::id);
    expect(admitted_m3_method != profile.provenance.evidence.end() &&
               admitted_m3_method->locator ==
                   "git-blob:760ddd8e436704ed707623a6dad0e6556606d08f" &&
               admitted_m3_method->revision ==
                   "aa1c9a1553b301300258e9fc1de16e6e47c2012c:"
                   "docs/model/M3_PARITY_MODEL.md" &&
               admitted_m3_method->content_sha256.has_value() &&
               digest_hex(*admitted_m3_method->content_sha256) ==
                   "435441890e0a5f8d01e81995f64f33d4c554144f5b1436895e6816f6db85e34c",
           "admitted M3 method authority is not its truthful frozen record");

    constexpr std::array<std::string_view, 3> kLocalEvaluationEvidence{
        "reference-fixture-manifest",
        "reference-parity-evidence",
        "reference-component-seed-evidence",
    };
    for (const auto evidence_id : kLocalEvaluationEvidence) {
        const auto evidence =
            std::ranges::find(profile.provenance.evidence, evidence_id,
                              &contract::EvidenceSource::id);
        expect(evidence != profile.provenance.evidence.end() &&
                   evidence->rights ==
                       contract::RightsDisposition::local_evaluation_only,
               "fixture-derived M4 authority lost its local-evaluation boundary");
    }
}

void test_exact_evidence_files(
    const profiles::BmwM52b28OperatingProfile &profile,
    const char *model_record_path, const char *accessory_descriptor_path) {
    expect(file_sha256(model_record_path) ==
               digest_from_evidence(profile, "operating-point-model-record"),
           "M4 model-record bytes do not match their provenance evidence");
    expect(file_sha256(accessory_descriptor_path) ==
               digest_from_evidence(profile,
                                    "operating-accessory-configuration"),
           "BMW accessory-descriptor bytes do not match their provenance evidence");
}

void test_fresh_core_provenance_and_shared_values(
    const profiles::BmwM52b28OperatingProfile &profile) {
    const auto parity =
        profiles::detail::build_bmw_m52b28_parity_request_unvalidated({});
    const auto &legacy =
        std::get<contract::LegacyLowOrderV1Profile>(
            parity.engine.physics_profile);
    const auto &operating = operating_profile(profile);

    const auto legacy_suffixes =
        core_resolution_suffixes(parity.provenance, kLegacyRoot, true);
    const auto operating_suffixes =
        core_resolution_suffixes(profile.provenance, kOperatingRoot, false);
    expect(!legacy_suffixes.empty() && legacy_suffixes == operating_suffixes,
           "M3 and operating profiles do not resolve the same core leaf inventory");

    for (const auto &resolution : profile.provenance.resolutions) {
        expect(starts_with(resolution.id, kOperatingResolutionPrefix),
               "operating profile reused a non-operating resolution identity");
        expect(resolution.parameter_path.find(kLegacyRoot) == std::string::npos,
               "operating profile resolution retained the legacy root");
        expect(std::ranges::none_of(
                   resolution.dependency_parameter_paths,
                   [](const auto &dependency) {
                       return dependency.find(kLegacyRoot) != std::string::npos;
                   }),
               "operating profile dependency retained the legacy root");
    }

    const auto &legacy_core = legacy.core;
    const auto &operating_core = operating.core;
    expect(legacy_core.mechanism.cylinders.size() ==
               operating_core.mechanism.cylinders.size(),
           "fresh operating construction changed the cylinder count");
    expect(legacy_core.mechanism.crank.crank_tdc_reference_rad.value ==
               operating_core.mechanism.crank.crank_tdc_reference_rad.value,
           "fresh operating construction changed the crank TDC reference");
    expect(legacy_core.mechanism.cylinders.front().parameters.bore_m.value ==
               operating_core.mechanism.cylinders.front().parameters.bore_m.value,
           "fresh operating construction changed a cylinder bore");
    expect(legacy_core.gas_path.intake.plenum_volume_m3.value ==
               operating_core.gas_path.intake.plenum_volume_m3.value,
           "fresh operating construction changed the intake plenum volume");
    expect(std::ranges::equal(
               legacy_core.gas_path.head.intake_flow,
               operating_core.gas_path.head.intake_flow,
               [](const auto &legacy_point, const auto &operating_point) {
                   return legacy_point.sample_id.value ==
                              operating_point.sample_id.value &&
                          legacy_point.lift_m.value ==
                              operating_point.lift_m.value &&
                          legacy_point.source_cfm_at_28_inh2o.value ==
                              operating_point.source_cfm_at_28_inh2o.value &&
                          legacy_point.resolved_k.value ==
                              operating_point.resolved_k.value;
               }),
           "fresh operating construction changed the intake-flow table");
    expect(legacy_core.valvetrain.intake.lobes.size() ==
               operating_core.valvetrain.intake.lobes.size(),
           "fresh operating construction changed the intake-lobe count");
    expect(legacy_core.ignition.firing_order.value ==
               operating_core.ignition.firing_order.value,
           "fresh operating construction changed the firing order");
    expect(legacy_core.fuel.energy_density_j_per_kg.value ==
               operating_core.fuel.energy_density_j_per_kg.value,
           "fresh operating construction changed fuel energy density");
    expect(legacy_core.combustion_random_streams.size() ==
               operating_core.combustion_random_streams.size(),
           "fresh operating construction changed the combustion stream count");
    expect(std::ranges::equal(
               legacy_core.excitation.routes, operating_core.excitation.routes,
               [](const auto &legacy_route, const auto &operating_route) {
                   return legacy_route.route_id == operating_route.route_id &&
                          legacy_route.exhaust_system_length_m.value ==
                              operating_route.exhaust_system_length_m.value &&
                          legacy_route.audio_volume_linear.value ==
                              operating_route.audio_volume_linear.value;
               }),
           "fresh operating construction changed the excitation routes");
    expect(legacy_core.mechanism.crank.crankshaft_mass_kg.resolution_id !=
                   operating_core.mechanism.crank.crankshaft_mass_kg.resolution_id,
           "operating core shallow-copied an M3 resolution identity");
}

void test_exact_mutation_rejection(
    const profiles::BmwM52b28OperatingProfile &exact) {
    const auto mutate_operating = [](auto &profile) -> auto & {
        return std::get<contract::LowOrderOperatingPointV1Profile>(
            profile.engine.physics_profile);
    };
    expect_mutation_rejected(
        exact, "changed constant FMEP was accepted", [&](auto &changed) {
            mutate_operating(changed).aggregate_loss.constant_fmep_bar.value =
                std::nextafter(0.4, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed peak-pressure coefficient was accepted",
        [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss.peak_pressure_coefficient.value =
                std::nextafter(0.005, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed mean-speed coefficient was accepted",
        [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value =
                std::nextafter(0.09, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed squared-speed coefficient was accepted",
        [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss
                .mean_piston_speed_squared_coefficient_bar_s2_per_m2.value =
                std::nextafter(0.0009, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed oil condition was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss.required_oil_temperature_k.value =
                std::nextafter(363.15, 364.0);
        });
    expect_mutation_rejected(
        exact, "changed aggregate term scope was accepted", [&](auto &changed) {
            mutate_operating(changed).aggregate_loss.included_terms.value ^= 0x02;
        });
    expect_mutation_rejected(
        exact, "changed accessory ID was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .accessory_configuration.configuration_id.value += "-changed";
        });
    expect_mutation_rejected(
        exact, "changed accessory digest was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .accessory_configuration.content_sha256.value.bytes.front() ^=
                UINT8_C(0x80);
        });
    expect_mutation_rejected(
        exact, "engaged starter was accepted", [&](auto &changed) {
            mutate_operating(changed).starter.mechanically_disengaged.value =
                false;
        });
    expect_mutation_rejected(
        exact, "changed starter term scope was accepted", [&](auto &changed) {
            mutate_operating(changed).starter.included_terms.value = 0;
        });
    expect_mutation_rejected(
        exact, "changed quadrature authority was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .cycle_quadrature.value.configuration_sha256.bytes.front() ^=
                UINT8_C(0x80);
        });
    expect_mutation_rejected(
        exact, "changed aggregate-loss authority was accepted", [&](auto &changed) {
            changed.engine.methods.losses.value.configuration_sha256.bytes.front() ^=
                UINT8_C(0x80);
        });
    expect_mutation_rejected(
        exact, "changed torque capability was accepted", [&](auto &changed) {
            changed.engine.torque_capability.value
                .cycle_mean_net_shaft.completeness =
                contract::Completeness::incomplete;
        });
    expect_mutation_rejected(
        exact, "changed accessory evidence was accepted", [&](auto &changed) {
            const auto evidence = std::ranges::find(
                changed.provenance.evidence,
                std::string{"operating-accessory-configuration"},
                &contract::EvidenceSource::id);
            expect(evidence != changed.provenance.evidence.end() &&
                       evidence->content_sha256.has_value(),
                   "accessory evidence disappeared before mutation");
            evidence->content_sha256->bytes.front() ^= UINT8_C(0x80);
        });
    expect_mutation_rejected(
        exact, "coordinated and resealed accessory-authority drift was accepted",
        [&](auto &changed) {
            auto &changed_operating = mutate_operating(changed);
            changed_operating.accessory_configuration.content_sha256.value.bytes
                .front() ^= UINT8_C(0x80);
            const auto evidence = std::ranges::find(
                changed.provenance.evidence,
                std::string{"operating-accessory-configuration"},
                &contract::EvidenceSource::id);
            expect(evidence != changed.provenance.evidence.end() &&
                       evidence->content_sha256.has_value(),
                   "accessory evidence disappeared before coordinated mutation");
            evidence->content_sha256->bytes.front() ^= UINT8_C(0x80);
            changed.provenance.bundle.sha256 =
                contract::canonical_provenance_ledger_digest(
                    changed.provenance, kOperatingProvenanceDigestGrammar);
        });
}

void test_invalid_internal_profile_kind_rejected() {
    bool rejected = false;
    try {
        profiles::detail::BmwProvenanceBuilder builder{
            static_cast<profiles::detail::BmwProfileKind>(UINT8_C(0xff)),
        };
        static_cast<void>(builder);
    } catch (const std::logic_error &) {
        rejected = true;
    }
    expect(rejected, "unknown internal BMW profile kind did not fail closed");
}

void run_tests(const char *model_record_path,
               const char *accessory_descriptor_path) {
    const auto exact = make_exact_profile();
    test_exact_profile_authorities(exact);
    test_exact_evidence_files(exact, model_record_path,
                              accessory_descriptor_path);
    test_fresh_core_provenance_and_shared_values(exact);
    test_exact_mutation_rejection(exact);
    test_invalid_internal_profile_kind_rejected();
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error{
                "expected M4 model-record and accessory-descriptor paths"};
        }
        run_tests(argv[1], argv[2]);
    } catch (const std::exception &error) {
        std::cerr << "BMW operating-profile test failure: " << error.what()
                  << '\n';
        return 1;
    }
    return 0;
}
