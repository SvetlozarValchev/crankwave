#pragma once

#include "canonical_json_writer.hpp"

#include "engine_sim_offline/contract/reference_presentation.hpp"
#include "engine_sim_offline/contract/render_manifest.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace engine_sim_offline::artifacts::detail {

[[nodiscard]] bool write_rational_rate(CanonicalJsonWriter &writer,
                                       const contract::RationalRateHz &rate);
[[nodiscard]] bool write_render_rates(CanonicalJsonWriter &writer,
                                      const contract::RenderRates &rates);
[[nodiscard]] bool write_method_identity(CanonicalJsonWriter &writer,
                                         const contract::MethodIdentity &method);
[[nodiscard]] bool write_audio_contract(CanonicalJsonWriter &writer,
                                        const contract::AudioContract &audio);

template <class T, class WriteValue>
[[nodiscard]] bool write_resolved(CanonicalJsonWriter &writer,
                                  const contract::ResolvedValue<T> &resolved,
                                  WriteValue write_value) {
    return writer.begin_object() && writer.key("value") &&
           write_value(writer, resolved.value) && writer.key("resolution_id") &&
           writer.string_value(resolved.resolution_id) && writer.end_object();
}

[[nodiscard]] bool
write_presentation_calibration(CanonicalJsonWriter &writer,
                               const contract::PresentationCalibration &presentation);
[[nodiscard]] bool
write_reference_inputs(CanonicalJsonWriter &writer,
                       const contract::ReferencePresentationInputsV1 &inputs);
[[nodiscard]] bool write_output_contract(CanonicalJsonWriter &writer,
                                         const contract::OutputContract &output);
[[nodiscard]] bool write_route_records(CanonicalJsonWriter &writer,
                                       std::span<const contract::RouteRecord> routes);
[[nodiscard]] bool
write_output_bus_records(CanonicalJsonWriter &writer,
                         std::span<const contract::OutputBusRecord> buses);
[[nodiscard]] bool
write_artifact_records(CanonicalJsonWriter &writer,
                       std::span<const contract::ArtifactRecord> artifacts);

} // namespace engine_sim_offline::artifacts::detail
