#pragma once

#include "identity/simulation_request_identity_writer.hpp"

#include "engine_sim_offline/contract/render_manifest.hpp"
#include "engine_sim_offline/publication.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace engine_sim_offline::artifacts::detail {

using identity::detail::CanonicalJsonWriter;
using identity::detail::write_method_identity;
using identity::detail::write_provenance_bundle_ref;
using identity::detail::write_rational_rate;
using identity::detail::write_random_plan;
using identity::detail::write_render_rates;
using identity::detail::write_resolved;

using ManifestInputsWriter = bool (*)(CanonicalJsonWriter &,
                                      const contract::SimulationManifestInputs &);

[[nodiscard]] bool write_audio_contract(CanonicalJsonWriter &writer,
                                        const contract::AudioContract &audio);
[[nodiscard]] bool
write_completed_manifest_content(CanonicalJsonWriter &writer,
                                 const contract::RenderManifestContent &content,
                                 ManifestInputsWriter write_inputs);
[[nodiscard]] bool write_execution_facts(CanonicalJsonWriter &writer,
                                         const contract::ExecutionFacts &execution);
[[nodiscard]] RenderSinkError
manifest_writer_error(const CanonicalJsonWriter &writer,
                      std::string_view detail_code_domain);

[[nodiscard]] bool
write_presentation_calibration(CanonicalJsonWriter &writer,
                               const contract::PresentationCalibration &presentation);
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
