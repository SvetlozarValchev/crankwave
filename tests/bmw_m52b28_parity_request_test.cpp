#include "engine_sim_offline/artifacts/simulation_manifest_encoder.hpp"
#include "engine_sim_offline/profiles/bmw_m52b28_parity_request.hpp"

#include "reference/p18_reference_seed_reader.hpp"
#include "reference/reference_parity_v1_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace engine_sim_offline;

constexpr contract::Sha256Digest kExpectedComponentSeedSha256{{
    0xca, 0x6f, 0x9b, 0x2d, 0x56, 0xe2, 0xf6, 0x72, 0x94, 0x01, 0x43,
    0x7a, 0x74, 0x1f, 0x60, 0x50, 0x69, 0xa7, 0xee, 0xa2, 0x15, 0x24,
    0xa8, 0x5b, 0x3d, 0xce, 0x03, 0x22, 0xec, 0x30, 0x46, 0x8f,
}};

constexpr std::string_view kExpectedRequestIdentitySha256 =
    "f6f0ffc8d32167a52785003d9fb4568b32cc23adfc8e1ca4e7263210701f5aa4";

void expect(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] std::vector<std::byte> read_bytes(const std::string &path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    expect(input.is_open(), "could not open BMW request reference input");
    const auto end = input.tellg();
    expect(end >= 0, "could not measure BMW request reference input");
    input.seekg(0);
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    expect(input.gcount() == static_cast<std::streamsize>(bytes.size()),
           "BMW request reference input read was incomplete");
    return bytes;
}

[[nodiscard]] std::string digest_hex(const contract::Sha256Digest &digest) {
    constexpr char kDigits[] = "0123456789abcdef";
    std::string result(digest.bytes.size() * 2U, '0');
    for (std::size_t index = 0; index < digest.bytes.size(); ++index) {
        result[index * 2U] = kDigits[digest.bytes[index] >> 4U];
        result[index * 2U + 1U] = kDigits[digest.bytes[index] & 0x0fU];
    }
    return result;
}

[[nodiscard]] std::string as_string(const std::vector<std::byte> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::vector<double> decode_rpm_lane(const std::vector<std::byte> &bytes) {
    auto decoded = reference::decode_reference_parity_v1(bytes);
    const auto *evidence = std::get_if<reference::DecodedReferenceParityV1>(&decoded);
    expect(evidence != nullptr, "exact BMW parity evidence failed to decode");
    std::vector<double> rpm;
    rpm.reserve(evidence->frames.size());
    for (const auto &frame : evidence->frames) {
        rpm.push_back(frame.engine_speed_rpm);
    }
    return rpm;
}

[[nodiscard]] profiles::BmwM52b28ParityRequest
make_exact_request(const std::vector<double> &rpm) {
    auto result = profiles::make_bmw_m52b28_parity_request(rpm);
    const auto *request = std::get_if<profiles::BmwM52b28ParityRequest>(&result);
    expect(request != nullptr, "exact RPM lane did not construct the BMW request");
    return *request;
}

const contract::LegacyLowOrderV1Profile &
legacy_profile(const profiles::BmwM52b28ParityRequest &request) {
    return std::get<contract::LegacyLowOrderV1Profile>(request.engine.physics_profile);
}

const contract::FixedRateRpmTrajectory &
fixed_rpm(const profiles::BmwM52b28ParityRequest &request) {
    return std::get<contract::FixedRateRpmTrajectory>(
        std::get<contract::PrescribedKinematicSweep>(request.scenario.mode)
            .trajectory.rpm);
}

[[nodiscard]] artifacts::SimulationRequestIdentityEncoding
encode_request_identity(const profiles::BmwM52b28ParityRequest &request) {
    auto result = artifacts::encode_simulation_request_identity_v1(
        request.engine, request.scenario, request.provenance.bundle);
    const auto *encoding =
        std::get_if<artifacts::SimulationRequestIdentityEncoding>(&result);
    expect(encoding != nullptr, "exact BMW request identity failed to encode");
    return *encoding;
}

void test_exact_request_identity(const profiles::BmwM52b28ParityRequest &request) {
    const auto first = encode_request_identity(request);
    const auto second = encode_request_identity(request);
    expect(first == second,
           "identical BMW requests produced different identity encodings");
    expect(first.sha256 == contract::sha256(first.bytes),
           "BMW request identity reported a digest for different bytes");

    const auto actual_sha256 = digest_hex(first.sha256);
    if (actual_sha256 != kExpectedRequestIdentitySha256) {
        std::cerr << "BMW M52B28 request identity SHA-256: " << actual_sha256 << '\n';
        throw std::runtime_error{"BMW request identity does not match its golden"};
    }

    const auto document = as_string(first.bytes);
    constexpr std::string_view kPrefix =
        "{\"wire_schema\":\"engine-sim-offline.simulation-request-identity.v1\","
        "\"engine\":";
    expect(document.starts_with(kPrefix),
           "BMW request identity root or canonical member order changed");
    expect(document.ends_with("}\n"),
           "BMW request identity does not have exactly one terminal LF");
    expect(std::count(document.begin(), document.end(), '\n') == 1,
           "BMW request identity contains non-terminal whitespace");
    expect(document.size() < 128U * 1024U,
           "BMW request identity unexpectedly expanded the owned RPM lane");
    expect(document.find("\"sample_count\":\"0x0000000000029810\"") !=
                   std::string::npos &&
               document.find("\"samples_f64le_sha256\":"
                             "\"b6206910b9c7b19694e35e08a3c5d8450f03cfdcbf43e818eb2a060"
                             "6927d8cda\"") != std::string::npos &&
               document.find("\"post_step_rpm\":") == std::string::npos,
           "BMW request identity changed its compact fixed-RPM descriptor");

    auto changed_engine = request;
    changed_engine.engine.display_name.value += " changed";
    expect(encode_request_identity(changed_engine).sha256 != first.sha256,
           "BMW engine mutation did not change the complete request identity");

    auto changed_scenario = request;
    std::get<contract::PrescribedKinematicSweep>(changed_scenario.scenario.mode)
        .throttle_01.points.back()
        .value = 0.84;
    expect(encode_request_identity(changed_scenario).sha256 != first.sha256,
           "BMW scenario mutation did not change the complete request identity");

    auto changed_provenance = request;
    changed_provenance.provenance.bundle.id += "-changed";
    expect(encode_request_identity(changed_provenance).sha256 != first.sha256,
           "BMW provenance mutation did not change the complete request identity");
}

template <class Mutation>
void expect_mutation_rejected(const profiles::BmwM52b28ParityRequest &canonical,
                              Mutation mutation, const char *message) {
    auto changed = canonical;
    mutation(changed);
    expect(!profiles::validate_bmw_m52b28_parity_request(changed).ok(), message);
}

void test_exact_request(const std::vector<double> &rpm,
                        const reference::P18DecodedReferenceSeeds &reference_seeds,
                        const contract::Sha256Digest &component_seed_sha256) {
    const auto request = make_exact_request(rpm);
    expect(profiles::validate_bmw_m52b28_parity_request(request).ok(),
           "constructed BMW request failed exact revalidation");

    expect(
        request.engine.schema_version == 1 &&
            request.engine.id == contract::EngineId{1} &&
            request.engine.engine_id.value == "bmw-m52b28" &&
            request.engine.profile_id.value == "bmw-m52b28-legacy-low-order-v1" &&
            request.engine.banks.size() == 1 && request.engine.cylinders.size() == 6 &&
            request.engine.ports.size() == 12 &&
            request.engine.gas_volumes.size() == 22 &&
            request.engine.flow_edges.size() == 34 && request.engine.routes.size() == 2,
        "BMW request engine identity or topology shape changed");
    expect(request.engine.total_displacement_m3.value == 0.0027930517982299274,
           "generic BMW displacement changed");
    const auto included_torque_terms =
        contract::torque_term_mask(contract::TorqueTerm::indicated_gas) |
        contract::torque_term_mask(contract::TorqueTerm::crank_friction);
    const auto omitted_torque_terms =
        contract::known_torque_term_mask() & ~included_torque_terms;
    expect(request.engine.torque_capability.value.instantaneous_net_shaft ==
                   contract::NetTorqueFormCapability{
                       contract::Availability::available,
                       contract::Completeness::incomplete,
                       included_torque_terms,
                       omitted_torque_terms,
                   } &&
               request.engine.torque_capability.value.cycle_mean_net_shaft ==
                   contract::NetTorqueFormCapability{} &&
               !request.engine.torque_capability.value.equivalent_inertia_available,
           "BMW temporal torque capability changed");

    const auto &profile = legacy_profile(request);
    expect(profile.mechanism.cylinders.size() == 6 &&
               profile.gas_path.exhaust_routes.size() == 2 &&
               profile.combustion_random_streams.size() == 6 &&
               profile.fuel.turbulence_to_flame_speed_ratio_triangle_radius.value ==
                   5.0,
           "BMW executable parity profile shape changed");
    for (std::size_t index = 0; index < profile.combustion_random_streams.size();
         ++index) {
        const auto &actual = profile.combustion_random_streams[index];
        const auto &expected = reference_seeds.combustion[index];
        expect(actual.cylinder_id ==
                       contract::CylinderId{static_cast<std::uint32_t>(index + 1U)} &&
                   actual.pcg32_initial_state.value == expected.initial_state &&
                   actual.pcg32_stream.value == expected.stream,
               "sealed BMW combustion stream differs from component-seed evidence");
    }

    expect(request.scenario.schema_version == 1 &&
               request.scenario.scenario_id == "bmw-m52b28-reference-pull-v1" &&
               request.scenario.total_duration_s.value == 17.0 &&
               request.scenario.audible_start_s.value == 2.0 &&
               request.scenario.audible_duration_s.value == 15.0 &&
               request.scenario.rates.physics == contract::RationalRateHz{10000, 1} &&
               request.scenario.rates.delivery == contract::RationalRateHz{192000, 1},
           "BMW scenario identity, horizon, or rates changed");
    expect(fixed_rpm(request).post_step_rpm == rpm &&
               fixed_rpm(request).post_step_rpm.size() == 170000,
           "BMW request did not own the exact decoded RPM lane");

    const auto seed_evidence = std::ranges::find(
        request.provenance.evidence, std::string{"reference-component-seed-evidence"},
        &contract::EvidenceSource::id);
    expect(seed_evidence != request.provenance.evidence.end() &&
               seed_evidence->content_sha256.has_value() &&
               !request.provenance.bundle.sha256.is_zero(),
           "BMW request provenance omitted component seeds or self identity");
    expect(*seed_evidence->content_sha256 == component_seed_sha256,
           "BMW component-seed evidence digest differs from the complete file");

    test_exact_request_identity(request);

    expect_mutation_rejected(
        request, [](auto &changed) { changed.engine.profile_id.value += "-changed"; },
        "mutated BMW profile identity passed exact validation");
    expect_mutation_rejected(
        request,
        [](auto &changed) {
            std::get<contract::LegacyLowOrderV1Profile>(changed.engine.physics_profile)
                .fuel.turbulence_to_flame_speed_ratio_triangle_radius.value = 4.0;
        },
        "mutated BMW fuel interpolation radius passed exact validation");
    expect_mutation_rejected(
        request,
        [](auto &changed) {
            std::get<contract::LegacyLowOrderV1Profile>(changed.engine.physics_profile)
                .combustion_random_streams.front()
                .pcg32_initial_state.value ^= UINT64_C(1);
        },
        "mutated BMW combustion stream passed exact validation");
    expect_mutation_rejected(
        request,
        [](auto &changed) {
            auto &throttle =
                std::get<contract::PrescribedKinematicSweep>(changed.scenario.mode)
                    .throttle_01;
            throttle.points.back().value = 0.84;
        },
        "mutated BMW throttle boundary passed exact validation");
    expect_mutation_rejected(
        request,
        [](auto &changed) {
            auto &trajectory = std::get<contract::FixedRateRpmTrajectory>(
                std::get<contract::PrescribedKinematicSweep>(changed.scenario.mode)
                    .trajectory.rpm);
            trajectory.post_step_rpm.front() += 1.0;
            trajectory.samples_f64le_sha256 =
                contract::canonical_binary64_le_sha256(trajectory.post_step_rpm);
        },
        "self-consistent but noncanonical BMW RPM lane passed exact validation");
    expect_mutation_rejected(
        request,
        [](auto &changed) {
            changed.provenance.evidence.front().content_sha256->bytes.front() ^= 1U;
            changed.provenance.bundle.sha256 =
                contract::canonical_provenance_ledger_digest(
                    changed.provenance,
                    "engine-sim-offline.m3-bmw-provenance-ledger-digest.v1");
        },
        "self-consistent but noncanonical BMW provenance passed exact validation");
}

void test_input_rejections(const std::vector<double> &rpm) {
    auto short_rpm = rpm;
    short_rpm.pop_back();
    expect(std::holds_alternative<contract::ValidationReport>(
               profiles::make_bmw_m52b28_parity_request(std::move(short_rpm))),
           "short BMW RPM lane constructed a request");

    auto changed_rpm = rpm;
    changed_rpm.front() += 1.0;
    expect(std::holds_alternative<contract::ValidationReport>(
               profiles::make_bmw_m52b28_parity_request(std::move(changed_rpm))),
           "content-changed BMW RPM lane constructed a request");
}

} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 3, "usage: bmw_m52b28_parity_request_test "
                          "<reference-parity.bin> <component-seeds.bin>");
        const auto parity_bytes = read_bytes(argv[1]);
        const auto rpm = decode_rpm_lane(parity_bytes);

        const auto seed_bytes = read_bytes(argv[2]);
        const auto component_seed_sha256 = contract::sha256(seed_bytes);
        expect(component_seed_sha256 == kExpectedComponentSeedSha256,
               "component-seeds.bin complete-file SHA-256 changed");
        const auto seed_result = reference::decode_p18_reference_seeds(seed_bytes);
        const auto *reference_seeds =
            std::get_if<reference::P18DecodedReferenceSeeds>(&seed_result);
        expect(reference_seeds != nullptr, "component-seeds.bin failed strict decode");

        test_exact_request(rpm, *reference_seeds, component_seed_sha256);
        test_input_rejections(rpm);
    } catch (const std::exception &error) {
        std::cerr << "BMW M52B28 parity request test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
