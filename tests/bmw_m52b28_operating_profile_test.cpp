#include "engine_sim_offline/profiles/bmw_m52b28_operating_profile.hpp"

#include "profiles/bmw_m52b28_profile_internal.hpp"
#include "simulation/cycle_accounting_method_registry.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
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
constexpr std::string_view kLegacyRoot = "engine.physics.legacy-low-order-v1";
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
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & UINT8_C(0x0f)];
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
    const auto *profile = std::get_if<profiles::BmwM52b28OperatingProfile>(&result);
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
    const auto *operating = std::get_if<contract::LowOrderOperatingPointV1Profile>(
        &profile.engine.physics_profile);
    expect(operating != nullptr,
           "canonical BMW profile has the wrong physics alternative");
    return *operating;
}

[[nodiscard]] contract::Sha256Digest
digest_from_evidence(const profiles::BmwM52b28OperatingProfile &profile,
                     std::string_view evidence_id) {
    const auto evidence = std::ranges::find(profile.provenance.evidence, evidence_id,
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
    expect(!profiles::validate_bmw_m52b28_operating_profile(changed).ok(), message);
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
    return value.starts_with(prefix);
}

[[nodiscard]] std::string_view
resolution_parameter_path(const contract::ProvenanceLedger &provenance,
                          std::string_view resolution_id) {
    const auto resolution = std::ranges::find(
        provenance.resolutions, resolution_id, &contract::ResolutionRecord::id);
    expect(resolution != provenance.resolutions.end(),
           "resolved BMW value has no provenance resolution");
    return resolution->parameter_path;
}

void replace_all(std::string &value, std::string_view from, std::string_view to) {
    for (std::size_t offset = value.find(from); offset != std::string::npos;
         offset = value.find(from, offset + to.size())) {
        value.replace(offset, from.size(), to);
    }
}

[[nodiscard]] bool is_legacy_profile_specific_suffix(std::string_view suffix) {
    return suffix == ".mechanism.crank.fixed_crank_friction_magnitude_nm" ||
           suffix == ".losses.included_terms" || suffix == ".losses.omitted_terms";
}

[[nodiscard]] bool is_operating_profile_specific_suffix(std::string_view suffix) {
    return suffix.starts_with(".aggregate_loss.") ||
           suffix.starts_with(".accessory_configuration.") ||
           suffix.starts_with(".starter.") ||
           suffix.starts_with(".exhaust_acoustics.") || suffix == ".cycle_quadrature";
}

[[nodiscard]] std::set<std::string>
core_resolution_suffixes(const contract::ProvenanceLedger &provenance,
                         std::string_view root, bool legacy) {
    std::set<std::string> result;
    for (const auto &resolution : provenance.resolutions) {
        if (!starts_with(resolution.parameter_path, root)) {
            continue;
        }
        auto suffix = resolution.parameter_path.substr(root.size());
        if ((legacy && is_legacy_profile_specific_suffix(suffix)) ||
            (!legacy && is_operating_profile_specific_suffix(suffix))) {
            continue;
        }
        if (!legacy) {
            replace_all(suffix, ".exhaust.outlet.front", ".exhaust.reference.0");
            replace_all(suffix, ".exhaust.outlet.rear", ".exhaust.reference.1");
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
               profile.provenance.schema_id == profile.engine.provenance_schema_id &&
               profile.provenance.bundle.id ==
                   "bmw-m52b28-low-order-operating-point-v1-provenance",
           "canonical BMW operating identity changed");

    constexpr std::string_view kExpectedBundleSha256 =
        "42c1f2121315e166f1eb65807cc6140a62f8a4892cf74a532894a94c5091e5e9";
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
               std::bit_cast<std::uint64_t>(loss.peak_pressure_coefficient.value) ==
                   UINT64_C(0x3f747ae147ae147b) &&
               std::bit_cast<std::uint64_t>(
                   loss.mean_piston_speed_coefficient_bar_s_per_m.value) ==
                   UINT64_C(0x3fb70a3d70a3d70a) &&
               std::bit_cast<std::uint64_t>(
                   loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2.value) ==
                   UINT64_C(0x3f4d7dbf487fcb92) &&
               std::bit_cast<std::uint64_t>(loss.required_oil_temperature_k.value) ==
                   UINT64_C(0x4076b26666666666),
           "canonical BMW loss tuple or oil condition changed");
    expect(loss.included_terms.value == UINT64_C(0x7e) &&
               operating.starter.mechanically_disengaged.value &&
               operating.starter.included_terms.value == UINT64_C(0x80) &&
               (contract::indicated_gas_torque_term_mask() | loss.included_terms.value |
                operating.starter.included_terms.value) == UINT64_C(0xff),
           "canonical BMW torque accounting partition changed");

    constexpr std::string_view kAccessorySha256 =
        "ce3cd1bfa0265e5d82e93a70f515cd86d16efa8da4ad5432057372da2b9d8e97";
    expect(operating.accessory_configuration.configuration_id.value ==
                   "bmw-m52b28-warm-stock-accessories-v1" &&
               digest_hex(operating.accessory_configuration.content_sha256.value) ==
                   kAccessorySha256 &&
               digest_hex(digest_from_evidence(
                   profile, "operating-accessory-configuration")) == kAccessorySha256,
           "canonical BMW accessory descriptor binding changed");

    const auto &implemented =
        simulation::implemented_cycle_accounting_method_identities();
    expect(operating.cycle_quadrature.value == implemented.cycle_quadrature &&
               profile.engine.methods.losses.value == implemented.aggregate_loss &&
               digest_hex(operating.cycle_quadrature.value.configuration_sha256) ==
                   "57c9b1517deede3285b5c801cb66386a841d0b0dde08bece7eb05fae869a63ac" &&
               digest_hex(profile.engine.methods.losses.value.configuration_sha256) ==
                   "6fa03e2d9eabfdc7af99dd3e2b2658808dbe388260391780dab4c80bc0c79489",
           "canonical BMW cycle-accounting method authority changed");

    const contract::TorqueCapability expected_capability{
        {
            contract::Availability::available,
            contract::Completeness::complete,
            UINT64_C(0xff),
            0,
        },
        {
            contract::Availability::available,
            contract::Completeness::complete,
            UINT64_C(0xff),
            0,
        },
        true,
    };
    expect(profile.engine.torque_capability.value == expected_capability,
           "canonical BMW torque capability changed");

    const auto admitted_m3_method =
        std::ranges::find(profile.provenance.evidence,
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

    expect(digest_hex(digest_from_evidence(profile, "m5-exhaust-acoustic-network")) ==
               "1c2e314846d8f86144e7ae7018a4adaab469fe5fe8ccdfe04d57ae73bcbc31ac",
           "M5 exhaust acoustic authority is not its frozen record");

    constexpr std::array<std::string_view, 3> kLocalEvaluationEvidence{
        "reference-fixture-manifest",
        "reference-parity-evidence",
        "reference-component-seed-evidence",
    };
    for (const auto evidence_id : kLocalEvaluationEvidence) {
        const auto evidence = std::ranges::find(
            profile.provenance.evidence, evidence_id, &contract::EvidenceSource::id);
        expect(evidence != profile.provenance.evidence.end() &&
                   evidence->rights ==
                       contract::RightsDisposition::local_evaluation_only,
               "fixture-derived M4 authority lost its local-evaluation boundary");
    }
}

void test_exact_operating_exhaust_semantics(
    const profiles::BmwM52b28OperatingProfile &profile) {
    const auto &engine = profile.engine;
    const auto route_front = std::ranges::find(engine.routes, contract::RouteId{1},
                                               &contract::RouteSpec::id);
    const auto route_rear = std::ranges::find(engine.routes, contract::RouteId{2},
                                              &contract::RouteSpec::id);
    const auto collector_front =
        std::ranges::find(engine.gas_volumes, contract::GasVolumeId{21},
                          &contract::GasVolumeSpec::id);
    const auto collector_rear =
        std::ranges::find(engine.gas_volumes, contract::GasVolumeId{22},
                          &contract::GasVolumeSpec::id);
    const auto outlet_edge_front =
        std::ranges::find(engine.flow_edges, contract::FlowEdgeId{33},
                          &contract::FlowEdgeSpec::id);
    const auto outlet_edge_rear =
        std::ranges::find(engine.flow_edges, contract::FlowEdgeId{34},
                          &contract::FlowEdgeSpec::id);
    expect(route_front != engine.routes.end() && route_rear != engine.routes.end() &&
               collector_front != engine.gas_volumes.end() &&
               collector_rear != engine.gas_volumes.end() &&
               outlet_edge_front != engine.flow_edges.end() &&
               outlet_edge_rear != engine.flow_edges.end(),
           "canonical BMW physical exhaust identities disappeared");
    expect(route_front->semantic_id.value == "exhaust.outlet.front" &&
               route_rear->semantic_id.value == "exhaust.outlet.rear" &&
               collector_front->semantic_id.value == "exhaust.collector.front" &&
               collector_rear->semantic_id.value == "exhaust.collector.rear" &&
               outlet_edge_front->semantic_id.value ==
                   "flow.collector-outlet.front" &&
               outlet_edge_rear->semantic_id.value ==
                   "flow.collector-outlet.rear",
           "canonical BMW operating exhaust retained anonymous route semantics");

    expect(resolution_parameter_path(profile.provenance,
                                     route_front->semantic_id.resolution_id) ==
                   "engine.routes.exhaust.outlet.front.semantic_id" &&
               resolution_parameter_path(profile.provenance,
                                         route_rear->semantic_id.resolution_id) ==
                   "engine.routes.exhaust.outlet.rear.semantic_id" &&
               resolution_parameter_path(profile.provenance,
                                         collector_front->semantic_id.resolution_id) ==
                   "engine.gas_volumes.exhaust.collector.front.semantic_id" &&
               resolution_parameter_path(profile.provenance,
                                         collector_rear->semantic_id.resolution_id) ==
                   "engine.gas_volumes.exhaust.collector.rear.semantic_id" &&
               resolution_parameter_path(
                   profile.provenance,
                   outlet_edge_front->semantic_id.resolution_id) ==
                   "engine.flow_edges.flow.collector-outlet.front.semantic_id" &&
               resolution_parameter_path(
                   profile.provenance,
                   outlet_edge_rear->semantic_id.resolution_id) ==
                   "engine.flow_edges.flow.collector-outlet.rear.semantic_id",
           "canonical BMW operating exhaust provenance paths disagree with their "
           "semantics");

    for (const auto &resolution : profile.provenance.resolutions) {
        expect(resolution.parameter_path.find("exhaust.reference.") ==
                       std::string::npos &&
                   resolution.parameter_path.find("exhaust.collector.0") ==
                       std::string::npos &&
                   resolution.parameter_path.find("exhaust.collector.1") ==
                       std::string::npos &&
                   resolution.parameter_path.find("flow.collector-outlet.0") ==
                       std::string::npos &&
                   resolution.parameter_path.find("flow.collector-outlet.1") ==
                       std::string::npos,
               "canonical BMW operating provenance retained an anonymous exhaust "
               "path");
        for (const auto &dependency : resolution.dependency_parameter_paths) {
            expect(dependency.find("exhaust.reference.") == std::string::npos &&
                       dependency.find("exhaust.collector.0") == std::string::npos &&
                       dependency.find("exhaust.collector.1") == std::string::npos &&
                       dependency.find("flow.collector-outlet.0") ==
                           std::string::npos &&
                       dependency.find("flow.collector-outlet.1") ==
                           std::string::npos,
                   "canonical BMW operating dependency retained an anonymous "
                   "exhaust path");
        }
    }
}

void test_exact_m5_exhaust_acoustic_assembly(
    const profiles::BmwM52b28OperatingProfile &profile) {
    const auto &assembly = operating_profile(profile).exhaust_acoustics;
    expect(assembly.assembly_id.value == "declared-test-cell-twin-open-pipe" &&
               assembly.source_interval_rate.value ==
                   contract::RationalRateHz{80000, 1} &&
               assembly.acoustic_rate.value == contract::RationalRateHz{192000, 1} &&
               assembly.universal_gas_constant_j_per_mol_k.value == 8.31446261815324 &&
               assembly.source_molar_mass_kg_per_mol.value == 0.02897 &&
               assembly.source_heat_capacity_ratio.value == 1.4 &&
               assembly.pa_per_full_scale.value == 256.0,
           "canonical BMW M5 acoustic assembly scalars changed");

    constexpr std::array<std::string_view, 6> kMethodIds{
        "ideal-pseudo-gas-source-properties",
        "causal-bandlimited-rational-resampling",
        "uniform-cylindrical-digital-waveguide",
        "ideal-compact-pressure-junction",
        "causal-unflanged-pipe-reflection",
        "compact-monopole-free-field-radiation",
    };
    const std::array<const contract::ResolvedValue<contract::MethodIdentity> *, 6>
        methods{
            &assembly.methods.source_properties, &assembly.methods.reconstruction,
            &assembly.methods.waveguide,         &assembly.methods.junction,
            &assembly.methods.outlet_reflection, &assembly.methods.exterior_radiation,
        };
    for (std::size_t index = 0; index < methods.size(); ++index) {
        expect(
            methods[index]->value.id == kMethodIds[index] &&
                methods[index]->value.version == 1 &&
                digest_hex(methods[index]->value.configuration_sha256) ==
                    "1c2e314846d8f86144e7ae7018a4adaab469fe5fe8ccdfe04d57ae73bcbc31ac",
            "canonical BMW M5 acoustic method identity changed");
    }

    expect(assembly.ducts.size() == 8 && assembly.primary_bindings.size() == 6 &&
               assembly.junctions.size() == 2 && assembly.outlets.size() == 2,
           "canonical BMW M5 acoustic assembly cardinality changed");
    for (std::size_t index = 0; index < 6; ++index) {
        const auto number = static_cast<std::uint32_t>(index + 1U);
        const auto &duct = assembly.ducts[index];
        const auto &binding = assembly.primary_bindings[index];
        expect(duct.id == contract::AcousticDuctId{number} &&
                   duct.semantic_id.value == "primary-" + std::to_string(number) &&
                   duct.kind.value == contract::AcousticDuctKind::primary &&
                   duct.length_m.value == 0.300 &&
                   duct.inner_diameter_m.value == 0.042 &&
                   duct.reference_temperature_k.value == 800.0 &&
                   duct.propagation_loss_np_per_m.value == 0.10,
               "canonical BMW M5 primary geometry changed");
        expect(binding.cylinder_id == contract::CylinderId{number} &&
                   binding.exhaust_port_id == contract::PortId{number * 2U} &&
                   binding.primary_duct_id == contract::AcousticDuctId{number} &&
                   binding.junction_id ==
                       contract::AcousticJunctionId{number <= 3U ? 1U : 2U},
               "canonical BMW M5 cylinder/primary/junction binding changed");
    }
    for (std::size_t index = 0; index < 2; ++index) {
        const auto &duct = assembly.ducts[index + 6U];
        expect(
            duct.id ==
                    contract::AcousticDuctId{static_cast<std::uint32_t>(index + 7U)} &&
                duct.semantic_id.value ==
                    (index == 0 ? "downstream-front" : "downstream-rear") &&
                duct.kind.value == contract::AcousticDuctKind::downstream &&
                duct.length_m.value == 1.500 && duct.inner_diameter_m.value == 0.046 &&
                duct.reference_temperature_k.value == 600.0 &&
                duct.propagation_loss_np_per_m.value == 0.10,
            "canonical BMW M5 downstream geometry changed");
    }
    expect(assembly.junctions[0].id == contract::AcousticJunctionId{1} &&
               assembly.junctions[0].semantic_id.value == "junction-front" &&
               assembly.junctions[0].primary_duct_ids ==
                   std::vector<contract::AcousticDuctId>{contract::AcousticDuctId{1},
                                                         contract::AcousticDuctId{2},
                                                         contract::AcousticDuctId{3}} &&
               assembly.junctions[0].downstream_duct_id ==
                   contract::AcousticDuctId{7} &&
               assembly.junctions[1].id == contract::AcousticJunctionId{2} &&
               assembly.junctions[1].semantic_id.value == "junction-rear" &&
               assembly.junctions[1].primary_duct_ids ==
                   std::vector<contract::AcousticDuctId>{contract::AcousticDuctId{4},
                                                         contract::AcousticDuctId{5},
                                                         contract::AcousticDuctId{6}} &&
               assembly.junctions[1].downstream_duct_id == contract::AcousticDuctId{8},
           "canonical BMW M5 1-2-3 / 4-5-6 junction topology changed");
    expect(assembly.outlets[0].route_id == contract::RouteId{1} &&
               assembly.outlets[0].downstream_duct_id == contract::AcousticDuctId{7} &&
               assembly.outlets[0].observation_distance_m.value == 1.0 &&
               resolution_parameter_path(
                   profile.provenance,
                   assembly.outlets[0].observation_distance_m.resolution_id) ==
                   std::string{kOperatingRoot} +
                       ".exhaust_acoustics.outlets.exhaust.outlet.front."
                       "observation_distance_m" &&
               assembly.outlets[1].route_id == contract::RouteId{2} &&
               assembly.outlets[1].downstream_duct_id == contract::AcousticDuctId{8} &&
               assembly.outlets[1].observation_distance_m.value == 1.0 &&
               resolution_parameter_path(
                   profile.provenance,
                   assembly.outlets[1].observation_distance_m.resolution_id) ==
                   std::string{kOperatingRoot} +
                       ".exhaust_acoustics.outlets.exhaust.outlet.rear."
                       "observation_distance_m",
           "canonical BMW M5 unflanged one-metre outlet binding changed");
}

void test_exact_evidence_files(const profiles::BmwM52b28OperatingProfile &profile,
                               const char *model_record_path,
                               const char *accessory_descriptor_path,
                               const char *topology_correction_path) {
    expect(file_sha256(model_record_path) ==
               digest_from_evidence(profile, "operating-point-model-record"),
           "M4 model-record bytes do not match their provenance evidence");
    expect(file_sha256(accessory_descriptor_path) ==
               digest_from_evidence(profile, "operating-accessory-configuration"),
           "BMW accessory-descriptor bytes do not match their provenance evidence");
    expect(file_sha256(topology_correction_path) ==
               digest_from_evidence(profile, "m4-bmw-exhaust-topology-correction"),
           "BMW M4 exhaust correction bytes do not match their provenance evidence");
    const auto m5_network_path =
        std::filesystem::path{topology_correction_path}.parent_path() / "model" /
        "M5_EXHAUST_ACOUSTIC_NETWORK.md";
    expect(file_sha256(m5_network_path.c_str()) ==
               digest_from_evidence(profile, "m5-exhaust-acoustic-network"),
           "BMW M5 exhaust acoustic bytes do not match their provenance evidence");
}

void test_fresh_core_provenance_and_shared_values(
    const profiles::BmwM52b28OperatingProfile &profile) {
    const auto parity =
        profiles::detail::build_bmw_m52b28_parity_request_unvalidated({});
    const auto &legacy =
        std::get<contract::LegacyLowOrderV1Profile>(parity.engine.physics_profile);
    const auto &operating = operating_profile(profile);

    const auto legacy_suffixes =
        core_resolution_suffixes(parity.provenance, kLegacyRoot, true);
    const auto operating_suffixes =
        core_resolution_suffixes(profile.provenance, kOperatingRoot, false);
    expect(!legacy_suffixes.empty() && legacy_suffixes == operating_suffixes,
           "M3 and operating profiles do not resolve the same core leaf inventory");

    expect(parity.engine.routes[0].semantic_id.value == "exhaust.reference.0" &&
               parity.engine.routes[1].semantic_id.value == "exhaust.reference.1",
           "operating route cutover mutated the isolated M3 reference oracle");

    for (const auto &resolution : profile.provenance.resolutions) {
        expect(starts_with(resolution.id, kOperatingResolutionPrefix),
               "operating profile reused a non-operating resolution identity");
        expect(resolution.parameter_path.find(kLegacyRoot) == std::string::npos,
               "operating profile resolution retained the legacy root");
        expect(std::ranges::none_of(resolution.dependency_parameter_paths,
                                    [](const auto &dependency) {
                                        return dependency.find(kLegacyRoot) !=
                                               std::string::npos;
                                    }),
               "operating profile dependency retained the legacy root");
    }

    const auto &legacy_core = legacy.core;
    const auto &operating_core = operating.core;
    constexpr std::array<contract::RouteId, 6> kLegacyCylinderRoutes{
        contract::RouteId{2}, contract::RouteId{1}, contract::RouteId{2},
        contract::RouteId{1}, contract::RouteId{2}, contract::RouteId{1},
    };
    constexpr std::array<contract::RouteId, 6> kOperatingCylinderRoutes{
        contract::RouteId{1}, contract::RouteId{1}, contract::RouteId{1},
        contract::RouteId{2}, contract::RouteId{2}, contract::RouteId{2},
    };
    constexpr std::array<contract::RouteId, 6> kOperatingFiringRoutes{
        contract::RouteId{1}, contract::RouteId{2}, contract::RouteId{1},
        contract::RouteId{2}, contract::RouteId{1}, contract::RouteId{2},
    };
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
                          legacy_point.lift_m.value == operating_point.lift_m.value &&
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
    expect(legacy_core.gas_path.exhaust_routes.size() == 2U &&
               operating_core.gas_path.exhaust_routes.size() == 2U &&
               legacy_core.excitation.routes.size() == 2U &&
               operating_core.excitation.routes.size() == 2U,
           "BMW exhaust route count changed");
    expect(
        legacy_core.gas_path.exhaust_routes[0].parameters.audio_volume_linear.value ==
                0.5 &&
            legacy_core.gas_path.exhaust_routes[1]
                    .parameters.audio_volume_linear.value == 1.0 &&
            legacy_core.excitation.routes[0].audio_volume_linear.value == 0.5 &&
            legacy_core.excitation.routes[1].audio_volume_linear.value == 1.0,
        "M3 oracle exhaust authority changed");
    expect(operating_core.gas_path.exhaust_routes[0]
                       .parameters.audio_volume_linear.value == 1.0 &&
               operating_core.gas_path.exhaust_routes[1]
                       .parameters.audio_volume_linear.value == 1.0 &&
               operating_core.excitation.routes[0].audio_volume_linear.value == 1.0 &&
               operating_core.excitation.routes[1].audio_volume_linear.value == 1.0,
           "M4 exhaust routes do not have equal gross authority");

    for (std::size_t index = 0; index < kOperatingCylinderRoutes.size(); ++index) {
        const auto expected_legacy_route = kLegacyCylinderRoutes[index];
        const auto expected_operating_route = kOperatingCylinderRoutes[index];
        expect(legacy_core.mechanism.cylinders[index].topology.exhaust_route_id ==
                       expected_legacy_route &&
                   legacy_core.excitation.cylinder_paths[index].route_id ==
                       expected_legacy_route,
               "M3 oracle cylinder routing changed");
        expect(operating_core.mechanism.cylinders[index].topology.exhaust_route_id ==
                       expected_operating_route &&
                   operating_core.excitation.cylinder_paths[index].route_id ==
                       expected_operating_route,
               "M4 mechanism and excitation cylinder routing disagree");

        const auto &assembly = operating_core.mechanism.cylinders[index];
        const auto edge = std::ranges::find(
            profile.engine.flow_edges, assembly.topology.primary_to_collector_edge_id,
            &contract::FlowEdgeSpec::id);
        const auto route = std::ranges::find(
            operating_core.gas_path.exhaust_routes, expected_operating_route,
            [](const auto &candidate) { return candidate.topology.route_id; });
        expect(edge != profile.engine.flow_edges.end() &&
                   route != operating_core.gas_path.exhaust_routes.end() &&
                   edge->endpoint_0_volume_id ==
                       assembly.topology.exhaust_primary_volume_id &&
                   edge->endpoint_1_volume_id == route->topology.collector_volume_id,
               "M4 primary edge does not terminate at its declared collector");
    }

    std::array<contract::RouteId, 6> operating_firing_routes{};
    for (std::size_t index = 0; index < operating_firing_routes.size(); ++index) {
        const auto cylinder_id = operating_core.ignition.firing_order.value[index];
        const auto assembly = std::ranges::find(
            operating_core.mechanism.cylinders, cylinder_id,
            [](const auto &candidate) { return candidate.topology.cylinder_id; });
        expect(assembly != operating_core.mechanism.cylinders.end(),
               "M4 firing-order cylinder is absent from the mechanism");
        operating_firing_routes[index] = assembly->topology.exhaust_route_id;
    }
    expect(operating_firing_routes == kOperatingFiringRoutes,
           "M4 firing order does not alternate adjacent-cylinder manifolds");
    expect(legacy_core.mechanism.crank.crankshaft_mass_kg.resolution_id !=
               operating_core.mechanism.crank.crankshaft_mass_kg.resolution_id,
           "operating core shallow-copied an M3 resolution identity");
}

void test_exact_mutation_rejection(const profiles::BmwM52b28OperatingProfile &exact) {
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
        exact, "changed peak-pressure coefficient was accepted", [&](auto &changed) {
            mutate_operating(changed).aggregate_loss.peak_pressure_coefficient.value =
                std::nextafter(0.005, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed mean-speed coefficient was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss.mean_piston_speed_coefficient_bar_s_per_m.value =
                std::nextafter(0.09, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed squared-speed coefficient was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .aggregate_loss.mean_piston_speed_squared_coefficient_bar_s2_per_m2
                .value = std::nextafter(0.0009, 1.0);
        });
    expect_mutation_rejected(
        exact, "changed oil condition was accepted", [&](auto &changed) {
            mutate_operating(changed).aggregate_loss.required_oil_temperature_k.value =
                std::nextafter(363.15, 364.0);
        });
    expect_mutation_rejected(
        exact, "changed aggregate term scope was accepted", [&](auto &changed) {
            mutate_operating(changed).aggregate_loss.included_terms.value ^= 0x02;
        });
    expect_mutation_rejected(
        exact, "changed accessory ID was accepted", [&](auto &changed) {
            mutate_operating(changed).accessory_configuration.configuration_id.value +=
                "-changed";
        });
    expect_mutation_rejected(
        exact, "changed accessory digest was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .accessory_configuration.content_sha256.value.bytes.front() ^=
                UINT8_C(0x80);
        });
    expect_mutation_rejected(exact, "engaged starter was accepted", [&](auto &changed) {
        mutate_operating(changed).starter.mechanically_disengaged.value = false;
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
        exact, "changed M5 primary length was accepted", [&](auto &changed) {
            mutate_operating(changed).exhaust_acoustics.ducts[0].length_m.value = 0.38;
        });
    expect_mutation_rejected(exact, "cross-bound M5 primary was accepted",
                             [&](auto &changed) {
                                 mutate_operating(changed)
                                     .exhaust_acoustics.primary_bindings[0]
                                     .junction_id = contract::AcousticJunctionId{2};
                             });
    expect_mutation_rejected(
        exact, "changed M5 acoustic method was accepted", [&](auto &changed) {
            mutate_operating(changed).exhaust_acoustics.methods.waveguide.value.id +=
                "-changed";
        });
    expect_mutation_rejected(
        exact, "changed M5 Pa calibration was accepted", [&](auto &changed) {
            mutate_operating(changed).exhaust_acoustics.pa_per_full_scale.value = 128.0;
        });
    expect_mutation_rejected(
        exact, "changed torque capability was accepted", [&](auto &changed) {
            changed.engine.torque_capability.value.cycle_mean_net_shaft.completeness =
                contract::Completeness::incomplete;
        });
    expect_mutation_rejected(exact, "odd/even M4 mechanism route mutation was accepted",
                             [&](auto &changed) {
                                 mutate_operating(changed)
                                     .core.mechanism.cylinders[0]
                                     .topology.exhaust_route_id = contract::RouteId{2};
                             });
    expect_mutation_rejected(
        exact, "cross-bound M4 collector edge was accepted", [&](auto &changed) {
            const auto edge =
                std::ranges::find(changed.engine.flow_edges, contract::FlowEdgeId{6},
                                  &contract::FlowEdgeSpec::id);
            expect(edge != changed.engine.flow_edges.end(),
                   "canonical cylinder-1 collector edge disappeared");
            edge->endpoint_1_volume_id = contract::GasVolumeId{22};
        });
    expect_mutation_rejected(
        exact, "cross-bound M4 excitation path was accepted", [&](auto &changed) {
            mutate_operating(changed).core.excitation.cylinder_paths[0].route_id =
                contract::RouteId{2};
        });
    expect_mutation_rejected(
        exact, "missing M4 excitation path was accepted", [&](auto &changed) {
            mutate_operating(changed).core.excitation.cylinder_paths.pop_back();
        });
    expect_mutation_rejected(
        exact, "unequal M4 gas-route authority was accepted", [&](auto &changed) {
            mutate_operating(changed)
                .core.gas_path.exhaust_routes[0]
                .parameters.audio_volume_linear.value = std::nextafter(1.0, 0.0);
        });
    expect_mutation_rejected(
        exact, "unequal M4 excitation-route authority was accepted",
        [&](auto &changed) {
            mutate_operating(changed)
                .core.excitation.routes[0]
                .audio_volume_linear.value = std::nextafter(1.0, 0.0);
        });
    expect_mutation_rejected(
        exact, "changed accessory evidence was accepted", [&](auto &changed) {
            const auto evidence =
                std::ranges::find(changed.provenance.evidence,
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
            const auto evidence =
                std::ranges::find(changed.provenance.evidence,
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

void run_tests(const char *model_record_path, const char *accessory_descriptor_path,
               const char *topology_correction_path) {
    const auto exact = make_exact_profile();
    test_exact_profile_authorities(exact);
    test_exact_operating_exhaust_semantics(exact);
    test_exact_m5_exhaust_acoustic_assembly(exact);
    test_exact_evidence_files(exact, model_record_path, accessory_descriptor_path,
                              topology_correction_path);
    test_fresh_core_provenance_and_shared_values(exact);
    test_exact_mutation_rejection(exact);
    test_invalid_internal_profile_kind_rejected();
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error{
                "expected M4 model-record, accessory-descriptor, and exhaust-"
                "correction paths"};
        }
        run_tests(argv[1], argv[2], argv[3]);
    } catch (const std::exception &error) {
        std::cerr << "BMW operating-profile test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
