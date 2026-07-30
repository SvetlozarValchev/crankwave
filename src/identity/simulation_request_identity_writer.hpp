#pragma once

#include "identity/canonical_json_writer.hpp"

#include "engine_sim_offline/contract/engine.hpp"
#include "engine_sim_offline/contract/provenance.hpp"
#include "engine_sim_offline/contract/randomness.hpp"
#include "engine_sim_offline/contract/scenario.hpp"

namespace engine_sim_offline::identity::detail {

template <class T, class WriteValue>
[[nodiscard]] bool write_resolved(CanonicalJsonWriter &writer,
                                  const contract::ResolvedValue<T> &resolved,
                                  WriteValue write_value) {
    return writer.begin_object() && writer.key("value") &&
           write_value(writer, resolved.value) && writer.key("resolution_id") &&
           writer.string_value(resolved.resolution_id) && writer.end_object();
}

[[nodiscard]] bool write_rational_rate(CanonicalJsonWriter &writer,
                                       const contract::RationalRateHz &rate);
[[nodiscard]] bool write_render_rates(CanonicalJsonWriter &writer,
                                      const contract::RenderRates &rates);
[[nodiscard]] bool write_method_identity(CanonicalJsonWriter &writer,
                                         const contract::MethodIdentity &method);
[[nodiscard]] bool write_random_plan(CanonicalJsonWriter &writer,
                                     const contract::RandomPlan &random_plan);
[[nodiscard]] bool
write_provenance_bundle_ref(CanonicalJsonWriter &writer,
                            const contract::ProvenanceBundleRef &provenance);
[[nodiscard]] bool write_engine_spec(CanonicalJsonWriter &writer,
                                     const contract::EngineSpec &engine);
[[nodiscard]] bool write_render_scenario(CanonicalJsonWriter &writer,
                                         const contract::RenderScenario &scenario);

} // namespace engine_sim_offline::identity::detail
