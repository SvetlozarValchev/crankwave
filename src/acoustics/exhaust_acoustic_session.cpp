#include "acoustics/exhaust_acoustic_session.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine_sim_offline::acoustics {
namespace {

constexpr contract::RationalRateHz kSourceRate{80'000, 1};
constexpr contract::RationalRateHz kAcousticRate{192'000, 1};
constexpr double kPseudoGasUniversalConstant = 8.31446261815324;
constexpr double kPseudoGasMolarMassKgPerMol = 0.02897;
constexpr double kPseudoGasHeatCapacityRatio = 1.4;
constexpr contract::Sha256Digest kM5ExhaustAcousticNetworkSha256{{
    0x1c, 0x2e, 0x31, 0x48, 0x46, 0xd8, 0xf8, 0x61, 0x44, 0xe7, 0xae,
    0x70, 0x18, 0xa4, 0xad, 0xaa, 0xb4, 0x69, 0xfe, 0x5f, 0xe8, 0xcc,
    0xdf, 0xe0, 0x4d, 0x57, 0xae, 0x73, 0xbc, 0xbc, 0x31, 0xac,
}};

[[nodiscard]] bool positive_finite(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::invalid_argument{message};
    }
}

void require_supported_method(
    const contract::ResolvedValue<contract::MethodIdentity> &method,
    std::string_view expected_id) {
    require(method.value.id == expected_id && method.value.version == 1U &&
                method.value.configuration_sha256 == kM5ExhaustAcousticNetworkSha256,
            "exhaust acoustic assembly selected an unsupported method identity");
}

[[nodiscard]] const contract::AcousticDuctSpec *
find_duct(const contract::ExhaustAcousticAssembly &assembly,
          contract::AcousticDuctId id) noexcept {
    const auto found =
        std::ranges::find(assembly.ducts, id, &contract::AcousticDuctSpec::id);
    return found == assembly.ducts.end() ? nullptr : &*found;
}

[[nodiscard]] UniformCylindricalWaveguideParameters
waveguide_parameters(const contract::AcousticDuctSpec &duct,
                     const contract::ExhaustAcousticAssembly &assembly,
                     const ExhaustAcousticEnvironment &environment) {
    const double radius_m = 0.5 * duct.inner_diameter_m.value;
    const double area_m2 = std::numbers::pi * radius_m * radius_m;
    return {
        duct.length_m.value,
        area_m2,
        environment.ambient_pressure_pa_abs,
        duct.reference_temperature_k.value,
        duct.propagation_loss_np_per_m.value,
        static_cast<double>(assembly.acoustic_rate.value.numerator) /
            static_cast<double>(assembly.acoustic_rate.value.denominator),
        assembly.universal_gas_constant_j_per_mol_k.value,
        assembly.source_molar_mass_kg_per_mol.value,
        assembly.source_heat_capacity_ratio.value,
    };
}

[[nodiscard]] std::string
capture_validation_message(const contract::ValidationReport &report) {
    if (report.issues.empty()) {
        return "exhaust source capture was invalid";
    }
    return "exhaust source capture was invalid at " + report.issues.front().path +
           ": " + report.issues.front().message;
}

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs,
                               std::uint64_t &result) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

} // namespace

std::size_t ExhaustAcousticPressureBlock::frame_count() const noexcept {
    if (outlets[0].pressure_pa.size() != outlets[1].pressure_pa.size()) {
        return 0U;
    }
    return outlets[0].pressure_pa.size();
}

ExhaustAcousticSession::ExhaustAcousticSession(
    const contract::ExhaustAcousticAssembly &assembly,
    ExhaustAcousticEnvironment environment)
    : assembly_(assembly), environment_(environment) {
    require(positive_finite(environment_.ambient_pressure_pa_abs),
            "exhaust acoustic ambient pressure must be positive and finite");
    require(positive_finite(environment_.ambient_temperature_k),
            "exhaust acoustic ambient temperature must be positive and finite");
    require(assembly_.source_interval_rate.value == kSourceRate,
            "exhaust acoustic source rate must be exactly 80000/1 Hz");
    require(assembly_.acoustic_rate.value == kAcousticRate,
            "exhaust acoustic rate must be exactly 192000/1 Hz");
    require(assembly_.universal_gas_constant_j_per_mol_k.value ==
                    kPseudoGasUniversalConstant &&
                assembly_.source_molar_mass_kg_per_mol.value ==
                    kPseudoGasMolarMassKgPerMol &&
                assembly_.source_heat_capacity_ratio.value ==
                    kPseudoGasHeatCapacityRatio,
            "exhaust acoustic gas constants must match the captured pseudo-gas");
    require(positive_finite(assembly_.pa_per_full_scale.value),
            "exhaust acoustic Pa calibration must be positive and finite");

    require_supported_method(assembly_.methods.source_properties,
                             "ideal-pseudo-gas-source-properties");
    require_supported_method(assembly_.methods.reconstruction,
                             "causal-bandlimited-rational-resampling");
    require_supported_method(assembly_.methods.waveguide,
                             "uniform-cylindrical-digital-waveguide");
    require_supported_method(assembly_.methods.junction,
                             "ideal-compact-pressure-junction");
    require_supported_method(assembly_.methods.outlet_reflection,
                             "causal-unflanged-pipe-reflection");
    require_supported_method(assembly_.methods.exterior_radiation,
                             "compact-monopole-free-field-radiation");

    require(assembly_.ducts.size() ==
                kExhaustAcousticPrimaryCount + kExhaustAcousticOutletCount,
            "bounded exhaust network requires exactly six primary and two downstream "
            "ducts");
    require(assembly_.primary_bindings.size() == kExhaustAcousticPrimaryCount,
            "bounded exhaust network requires exactly six primary bindings");
    require(assembly_.junctions.size() == kExhaustAcousticOutletCount,
            "bounded exhaust network requires exactly two junctions");
    require(assembly_.outlets.size() == kExhaustAcousticOutletCount,
            "bounded exhaust network requires exactly two outlets");

    std::unordered_set<std::uint32_t> duct_ids;
    std::size_t primary_duct_count = 0U;
    std::size_t downstream_duct_count = 0U;
    for (const auto &duct : assembly_.ducts) {
        require(duct.id.valid() && duct_ids.insert(duct.id.value).second,
                "exhaust acoustic duct IDs must be nonzero and unique");
        if (duct.kind.value == contract::AcousticDuctKind::primary) {
            ++primary_duct_count;
        } else if (duct.kind.value == contract::AcousticDuctKind::downstream) {
            ++downstream_duct_count;
        } else {
            throw std::invalid_argument{"exhaust acoustic duct kind is unsupported"};
        }
    }
    require(
        primary_duct_count == kExhaustAcousticPrimaryCount &&
            downstream_duct_count == kExhaustAcousticOutletCount,
        "bounded exhaust network requires six primary and two downstream duct kinds");

    std::unordered_set<std::uint32_t> cylinder_ids;
    std::unordered_set<std::uint32_t> port_ids;
    std::unordered_set<std::uint32_t> bound_primary_duct_ids;
    primaries_.reserve(kExhaustAcousticPrimaryCount);
    for (const auto &binding : assembly_.primary_bindings) {
        const auto *duct = find_duct(assembly_, binding.primary_duct_id);
        require(binding.cylinder_id.valid() &&
                    cylinder_ids.insert(binding.cylinder_id.value).second,
                "primary-binding cylinder IDs must be nonzero and unique");
        require(binding.exhaust_port_id.valid() &&
                    port_ids.insert(binding.exhaust_port_id.value).second,
                "primary-binding exhaust-port IDs must be nonzero and unique");
        require(binding.junction_id.valid(),
                "primary-binding junction ID must be nonzero");
        require(duct != nullptr &&
                    duct->kind.value == contract::AcousticDuctKind::primary &&
                    bound_primary_duct_ids.insert(duct->id.value).second,
                "primary binding must own one unique primary duct");
        primaries_.push_back({
            binding.cylinder_id,
            binding.exhaust_port_id,
            binding.primary_duct_id,
            binding.junction_id,
            UniformCylindricalWaveguide{
                waveguide_parameters(*duct, assembly_, environment_)},
        });
    }

    const double specific_gas_constant =
        assembly_.universal_gas_constant_j_per_mol_k.value /
        assembly_.source_molar_mass_kg_per_mol.value;
    const double ambient_density =
        environment_.ambient_pressure_pa_abs /
        (specific_gas_constant * environment_.ambient_temperature_k);
    const double ambient_sound_speed =
        std::sqrt(assembly_.source_heat_capacity_ratio.value * specific_gas_constant *
                  environment_.ambient_temperature_k);
    require(positive_finite(ambient_density) && positive_finite(ambient_sound_speed),
            "exhaust acoustic ambient gas properties did not resolve physically");

    std::unordered_set<std::uint32_t> junction_ids;
    std::unordered_set<std::uint32_t> outlet_route_ids;
    std::unordered_set<std::uint32_t> outlet_downstream_ids;
    std::unordered_set<std::size_t> junction_primary_indices;
    outlets_.reserve(kExhaustAcousticOutletCount);
    for (const auto &outlet_spec : assembly_.outlets) {
        require(outlet_spec.route_id.valid() &&
                    outlet_route_ids.insert(outlet_spec.route_id.value).second,
                "exhaust acoustic outlet route IDs must be nonzero and unique");
        require(positive_finite(outlet_spec.observation_distance_m.value),
                "exhaust acoustic observation distance must be positive and finite");

        const contract::ExhaustAcousticJunction *junction_spec = nullptr;
        for (const auto &candidate : assembly_.junctions) {
            if (candidate.downstream_duct_id == outlet_spec.downstream_duct_id) {
                require(junction_spec == nullptr,
                        "multiple junctions referenced one outlet downstream duct");
                junction_spec = &candidate;
            }
        }
        require(junction_spec != nullptr && junction_spec->id.valid() &&
                    junction_ids.insert(junction_spec->id.value).second,
                "each outlet requires one unique nonzero junction identity");
        require(junction_spec->primary_duct_ids.size() ==
                    kExhaustAcousticPrimariesPerJunction,
                "each bounded exhaust junction requires exactly three primaries");
        require(
            outlet_downstream_ids.insert(outlet_spec.downstream_duct_id.value).second,
            "each outlet requires one unique downstream duct");

        const auto *downstream_duct =
            find_duct(assembly_, outlet_spec.downstream_duct_id);
        require(downstream_duct != nullptr &&
                    downstream_duct->kind.value ==
                        contract::AcousticDuctKind::downstream,
                "outlet must terminate a downstream acoustic duct");
        auto downstream_waveguide = UniformCylindricalWaveguide{
            waveguide_parameters(*downstream_duct, assembly_, environment_)};

        std::array<std::size_t, kExhaustAcousticPrimariesPerJunction> primary_indices{};
        CompactFourPortWaves junction_impedances{};
        for (std::size_t port = 0; port < primary_indices.size(); ++port) {
            const auto primary_duct_id = junction_spec->primary_duct_ids[port];
            const auto found =
                std::ranges::find(primaries_, primary_duct_id, &PrimaryPath::duct_id);
            require(found != primaries_.end() &&
                        found->junction_id == junction_spec->id,
                    "junction primary identity disagrees with its source binding");
            const auto index =
                static_cast<std::size_t>(std::distance(primaries_.begin(), found));
            require(junction_primary_indices.insert(index).second,
                    "a primary duct entered more than one junction");
            primary_indices[port] = index;
            junction_impedances[port] =
                found->waveguide.properties().characteristic_impedance_pa_s_m3;
        }
        junction_impedances[3] =
            downstream_waveguide.properties().characteristic_impedance_pa_s_m3;

        const auto outlet_parameters = UnflangedPipeOutletParameters{
            0.5 * downstream_duct->inner_diameter_m.value,
            downstream_waveguide.properties().sound_speed_m_s,
            downstream_waveguide.properties().characteristic_impedance_pa_s_m3,
            static_cast<double>(kAcousticRate.numerator),
            ambient_density,
            ambient_sound_speed,
            outlet_spec.observation_distance_m.value,
        };
        outlets_.push_back({
            outlet_spec.route_id,
            junction_spec->id,
            outlet_spec.downstream_duct_id,
            primary_indices,
            std::move(downstream_waveguide),
            IdealCompactFourPortJunction{junction_impedances},
            UnflangedPipeOutlet{outlet_parameters},
        });
    }
    require(junction_primary_indices.size() == kExhaustAcousticPrimaryCount,
            "the two junctions must partition all six primaries exactly");
}

ExhaustAcousticPressureBlock
ExhaustAcousticSession::process(const contract::ExhaustPortSubstepCaptureView &source) {
    const auto report = contract::validate(source);
    if (!report.ok()) {
        throw std::invalid_argument{capture_validation_message(report)};
    }
    require(source.interval_count() <=
                dsp::SixChannelCausalResampler::maximum_input_frames_per_call,
            "exhaust source block exceeded the bounded 1600-interval session call");
    require(source.clock().first_sample_index == source_intervals_consumed_,
            "exhaust source block was discontinuous with session state");
    require(source.ports().size() == kExhaustAcousticPrimaryCount,
            "exhaust source block requires exactly six port lanes");

    // Publish no partial block or state if a downstream finite check fails.
    auto candidate = *this;
    auto output = candidate.process_validated(source);
    *this = std::move(candidate);
    return output;
}

ExhaustAcousticPressureBlock ExhaustAcousticSession::process_validated(
    const contract::ExhaustPortSubstepCaptureView &source) {
    std::array<std::size_t, kExhaustAcousticPrimaryCount> lane_to_primary{};
    std::unordered_set<std::size_t> mapped_primaries;
    for (std::size_t lane = 0; lane < source.ports().size(); ++lane) {
        const auto &port = source.ports()[lane];
        const auto found = std::ranges::find_if(primaries_, [&](const auto &primary) {
            return primary.exhaust_port_id == port.id &&
                   primary.cylinder_id == port.cylinder_id;
        });
        require(found != primaries_.end(),
                "exhaust source port identity is absent from the acoustic assembly");
        const auto index =
            static_cast<std::size_t>(std::distance(primaries_.begin(), found));
        require(mapped_primaries.insert(index).second,
                "exhaust source lanes duplicated an acoustic primary identity");
        lane_to_primary[lane] = index;
    }
    require(mapped_primaries.size() == kExhaustAcousticPrimaryCount,
            "exhaust source lanes do not cover all six acoustic primaries");

    std::vector<dsp::SixChannelResamplerFrame> source_flow(source.interval_count());
    for (std::size_t interval = 0; interval < source.interval_count(); ++interval) {
        for (std::size_t lane = 0; lane < source.ports().size(); ++lane) {
            const auto *sample = source.sample(interval, lane);
            if (sample == nullptr) {
                throw std::logic_error{"validated exhaust source indexing failed"};
            }
            const double flow_m3_s =
                sample->signed_mass_flow_kg_s / sample->upstream_density_kg_m3;
            if (!std::isfinite(flow_m3_s)) {
                throw std::domain_error{
                    "exhaust source volume velocity became non-finite"};
            }
            source_flow[interval][lane_to_primary[lane]] = flow_m3_s;
        }
    }

    const auto output_frame_count =
        source_reconstruction_.expected_output_frame_count(source_flow.size());
    std::vector<dsp::SixChannelResamplerFrame> reconstructed(output_frame_count);
    source_reconstruction_.process(source_flow, reconstructed);

    ExhaustAcousticPressureBlock result{
        kAcousticRate,
        acoustic_frames_produced_,
        {
            RadiatedExhaustOutletBlock{outlets_[0].route_id,
                                       std::vector<double>(output_frame_count)},
            RadiatedExhaustOutletBlock{outlets_[1].route_id,
                                       std::vector<double>(output_frame_count)},
        },
    };
    for (std::size_t frame = 0; frame < reconstructed.size(); ++frame) {
        const auto radiated = process_acoustic_frame(reconstructed[frame]);
        result.outlets[0].pressure_pa[frame] = radiated[0];
        result.outlets[1].pressure_pa[frame] = radiated[1];
    }

    std::uint64_t next_source = 0U;
    std::uint64_t next_acoustic = 0U;
    require(
        checked_add(source_intervals_consumed_, source.interval_count(), next_source),
        "exhaust source interval clock overflowed");
    require(checked_add(acoustic_frames_produced_, output_frame_count, next_acoustic),
            "exhaust acoustic frame clock overflowed");
    source_intervals_consumed_ = next_source;
    acoustic_frames_produced_ = next_acoustic;
    return result;
}

std::array<double, kExhaustAcousticOutletCount>
ExhaustAcousticSession::process_acoustic_frame(
    const dsp::SixChannelResamplerFrame &source_flow_m3_s) {
    std::array<WaveguideArrivalFrame, kExhaustAcousticPrimaryCount> primary_arrivals{};
    std::array<WaveguideLaunchFrame, kExhaustAcousticPrimaryCount> primary_launches{};
    for (std::size_t primary = 0; primary < primaries_.size(); ++primary) {
        primary_arrivals[primary] = primaries_[primary].waveguide.arrivals();
        const double launched = primary_arrivals[primary].at_upstream_pa +
                                primaries_[primary]
                                        .waveguide.properties()
                                        .characteristic_impedance_pa_s_m3 *
                                    source_flow_m3_s[primary];
        if (!std::isfinite(launched)) {
            throw std::domain_error{"Norton source boundary became non-finite"};
        }
        primary_launches[primary].from_upstream_pa = launched;
    }

    std::array<WaveguideArrivalFrame, kExhaustAcousticOutletCount>
        downstream_arrivals{};
    std::array<WaveguideLaunchFrame, kExhaustAcousticOutletCount> downstream_launches{};
    std::array<double, kExhaustAcousticOutletCount> radiated_pressure_pa{};
    for (std::size_t route = 0; route < outlets_.size(); ++route) {
        auto &path = outlets_[route];
        downstream_arrivals[route] = path.downstream_waveguide.arrivals();
        CompactFourPortWaves arriving{};
        for (std::size_t port = 0; port < path.primary_indices.size(); ++port) {
            arriving[port] =
                primary_arrivals[path.primary_indices[port]].at_downstream_pa;
        }
        arriving[3] = downstream_arrivals[route].at_upstream_pa;
        const auto scattering = path.junction.scatter(arriving);
        for (std::size_t port = 0; port < path.primary_indices.size(); ++port) {
            primary_launches[path.primary_indices[port]].from_downstream_pa =
                scattering.departing_pressure_waves_pa[port];
        }
        downstream_launches[route].from_upstream_pa =
            scattering.departing_pressure_waves_pa[3];

        const auto outlet_frame =
            path.outlet.process(downstream_arrivals[route].at_downstream_pa);
        downstream_launches[route].from_downstream_pa =
            outlet_frame.reflected_pressure_wave_pa;
        radiated_pressure_pa[route] = outlet_frame.radiated_pressure_pa;
        if (!std::isfinite(radiated_pressure_pa[route])) {
            throw std::domain_error{"radiated exhaust pressure became non-finite"};
        }
    }

    // Every boundary above observed the same current-frame arrivals. Only after all
    // Norton, junction, and outlet equations close do launches enter duct histories.
    for (std::size_t primary = 0; primary < primaries_.size(); ++primary) {
        primaries_[primary].waveguide.commit(primary_launches[primary]);
    }
    for (std::size_t route = 0; route < outlets_.size(); ++route) {
        outlets_[route].downstream_waveguide.commit(downstream_launches[route]);
    }
    return radiated_pressure_pa;
}

std::string_view ExhaustAcousticSession::assembly_id() const noexcept {
    return assembly_.assembly_id.value;
}

const ExhaustAcousticEnvironment &ExhaustAcousticSession::environment() const noexcept {
    return environment_;
}

double ExhaustAcousticSession::pa_per_full_scale() const noexcept {
    return assembly_.pa_per_full_scale.value;
}

std::uint64_t ExhaustAcousticSession::source_intervals_consumed() const noexcept {
    return source_intervals_consumed_;
}

std::uint64_t ExhaustAcousticSession::acoustic_frames_produced() const noexcept {
    return acoustic_frames_produced_;
}

} // namespace engine_sim_offline::acoustics
