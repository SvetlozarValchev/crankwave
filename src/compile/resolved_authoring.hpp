#pragma once

#include "engine_sim_offline/contract/presentation.hpp"
#include "engine_sim_offline/contract/source_matrix.hpp"

#include <string>
#include <vector>

namespace engine_sim_offline::compile::detail {

// Engine-owned scenario inputs retained after authoring resolution. Keeping these
// descriptors beside both resolvers avoids a lossy translation layer and keeps the
// public JSON identity separate from internal route IDs.
struct ResolvedFuelDescriptor {
    std::string authored_id;
    contract::ResolvedValue<std::string> fuel_id;
    contract::ResolvedValue<double> lower_heating_value_j_per_kg;
    contract::ResolvedValue<double> molecular_mass_kg_per_mol;
    contract::ResolvedValue<double> molecular_air_fuel_ratio;

    friend bool operator==(const ResolvedFuelDescriptor &,
                           const ResolvedFuelDescriptor &) = default;
};

struct ResolvedAudioBusDescriptor {
    std::string authored_id;
    std::string semantic_id;
    contract::OutputBusKind kind = contract::OutputBusKind::unspecified;
    // Order is the authored deterministic arithmetic reduction order.
    std::vector<contract::RouteId> routes;
    double gain_linear = 1.0;
    contract::AudioSampleEncoding sample_encoding =
        contract::AudioSampleEncoding::float32le;
    bool selectable = true;
    bool diagnostic = false;

    friend bool operator==(const ResolvedAudioBusDescriptor &,
                           const ResolvedAudioBusDescriptor &) = default;
};

} // namespace engine_sim_offline::compile::detail
