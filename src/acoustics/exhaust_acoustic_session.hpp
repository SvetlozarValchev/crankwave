#pragma once

#include "acoustics/ideal_compact_junction.hpp"
#include "acoustics/unflanged_pipe_outlet.hpp"
#include "acoustics/uniform_cylindrical_waveguide.hpp"
#include "dsp/six_channel_causal_resampler.hpp"
#include "engine_sim_offline/contract/capture.hpp"
#include "engine_sim_offline/contract/exhaust_acoustics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace engine_sim_offline::acoustics {

inline constexpr std::size_t kExhaustAcousticPrimaryCount = 6;
inline constexpr std::size_t kExhaustAcousticOutletCount = 2;
inline constexpr std::size_t kExhaustAcousticPrimariesPerJunction = 3;

// Scenario-owned conditions that are deliberately absent from the fixed exhaust
// assembly. There is no default atmosphere: callers must pass the same resolved
// ambient pressure and temperature that own the gas simulation.
struct ExhaustAcousticEnvironment {
    double ambient_pressure_pa_abs = 0.0;
    double ambient_temperature_k = 0.0;

    friend bool operator==(const ExhaustAcousticEnvironment &,
                           const ExhaustAcousticEnvironment &) = default;
};

struct RadiatedExhaustOutletBlock {
    contract::RouteId route_id;
    std::vector<double> pressure_pa;

    friend bool operator==(const RadiatedExhaustOutletBlock &,
                           const RadiatedExhaustOutletBlock &) = default;
};

// Acoustic frame m is observed at m / 192000 seconds. The two outlet entries are
// in the resolved assembly's outlet order and retain their exact route identities.
struct ExhaustAcousticPressureBlock {
    contract::RationalRateHz rate;
    std::uint64_t first_frame_index = 0;
    std::array<RadiatedExhaustOutletBlock, kExhaustAcousticOutletCount> outlets;

    [[nodiscard]] std::size_t frame_count() const noexcept;

    friend bool operator==(const ExhaustAcousticPressureBlock &,
                           const ExhaustAcousticPressureBlock &) = default;
};

// The bounded M5 six-primary/two-collector/two-outlet network. Construction admits
// exactly the frozen topology and method identities. Each process call consumes at
// most one 1,600-interval source block and preserves all reconstruction, waveguide,
// reflection, derivative, and observer-delay state across calls.
class ExhaustAcousticSession final {
  public:
    ExhaustAcousticSession(const contract::ExhaustAcousticAssembly &assembly,
                           ExhaustAcousticEnvironment environment);

    [[nodiscard]] ExhaustAcousticPressureBlock
    process(const contract::ExhaustPortSubstepCaptureView &source);

    [[nodiscard]] std::string_view assembly_id() const noexcept;
    [[nodiscard]] const ExhaustAcousticEnvironment &environment() const noexcept;
    [[nodiscard]] double pa_per_full_scale() const noexcept;
    [[nodiscard]] std::uint64_t source_intervals_consumed() const noexcept;
    [[nodiscard]] std::uint64_t acoustic_frames_produced() const noexcept;

  private:
    struct PrimaryPath {
        contract::CylinderId cylinder_id;
        contract::PortId exhaust_port_id;
        contract::AcousticDuctId duct_id;
        contract::AcousticJunctionId junction_id;
        UniformCylindricalWaveguide waveguide;
    };

    struct OutletPath {
        contract::RouteId route_id;
        contract::AcousticJunctionId junction_id;
        contract::AcousticDuctId downstream_duct_id;
        std::array<std::size_t, kExhaustAcousticPrimariesPerJunction> primary_indices{};
        UniformCylindricalWaveguide downstream_waveguide;
        IdealCompactFourPortJunction junction;
        UnflangedPipeOutlet outlet;
    };

    [[nodiscard]] ExhaustAcousticPressureBlock
    process_validated(const contract::ExhaustPortSubstepCaptureView &source);
    [[nodiscard]] std::array<double, kExhaustAcousticOutletCount>
    process_acoustic_frame(const dsp::SixChannelResamplerFrame &source_flow_m3_s);

    contract::ExhaustAcousticAssembly assembly_;
    ExhaustAcousticEnvironment environment_;
    dsp::SixChannelCausalResampler source_reconstruction_;
    std::vector<PrimaryPath> primaries_;
    std::vector<OutletPath> outlets_;
    std::uint64_t source_intervals_consumed_ = 0;
    std::uint64_t acoustic_frames_produced_ = 0;
};

} // namespace engine_sim_offline::acoustics
